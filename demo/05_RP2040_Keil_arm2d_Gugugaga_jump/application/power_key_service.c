#include "power_key_service.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/time.h"
#include "power_key_audit.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/padsbank0.h"

#define POWER_KEY_DEBOUNCE_MS           20u
#define POWER_KEY_PRESS_MS              8u
#define POWER_KEY_GLIDE_HOLD_MS         220u
/* Short release debounce; re-arm promptly after an intentional release. */
#define POWER_KEY_RELEASE_MS            30u
/* Provisional compatibility for reported U/L readings; electrical cause unverified. */
#define POWER_KEY_HELD_RELEASE_MS       280u
#define POWER_KEY_OFF_HOLD_MS           1000u

enum {
    POWER_KEY_START = 0,
    POWER_KEY_SOURCE_CHECK,
    POWER_KEY_WAIT_POWER_UP,
    POWER_KEY_WAIT_RELEASE,
    POWER_KEY_READY,
    POWER_KEY_OFF,
};

static volatile uint8_t s_state;
static volatile uint32_t s_changed_ms, s_last_poll_ms;
static volatile bool s_raw_pressed;
static volatile bool s_pressed;
static volatile bool s_press_pending;
static volatile bool s_press_reported;
static volatile bool s_airborne;
static volatile bool s_hold_used_in_air;
static volatile uint16_t s_last_low_ms, s_last_high_ms, s_max_sample_gap_ms;
static volatile uint16_t s_continuous_low_ms;
static int s_alarm_num = -1;

/* Diagnostic path: independent of debounce state and its millisecond clock.
 * Keep the last 64 input/configuration changes for debugger inspection.
 * All timestamps are microseconds modulo 2^32. No IO writes or logging here. */
typedef struct {
    uint32_t time_us, sio_input, status, ctrl, pad;
} power_key_trace_entry_t;
static volatile power_key_trace_entry_t s_key_trace[64];
static volatile uint32_t s_key_trace_count, s_key_edges, s_key_changed_us;
static volatile uint32_t s_key_prev_ctrl, s_key_prev_pad, s_key_prev_oe;
static volatile uint8_t s_key_levels, s_key_faults;
static bool s_key_audit_started;
static volatile uint16_t s_key_sio_changes, s_key_pad_changes;
static volatile uint8_t s_key_sio_seen, s_key_pad_seen;
static uint32_t s_key_window_us;

static void power_key_audit_sample(void)
{
    uint32_t now = time_us_32();
    uint32_t input = sio_hw->gpio_in;
    uint32_t status = iobank0_hw->io[POWER_UP_CHECK_PIN].status;
    uint32_t ctrl = iobank0_hw->io[POWER_UP_CHECK_PIN].ctrl;
    uint32_t pad = padsbank0_hw->io[POWER_UP_CHECK_PIN];
    uint32_t oe = sio_hw->gpio_oe & (1u << POWER_UP_CHECK_PIN);
    uint8_t levels = (uint8_t)(((input >> POWER_UP_CHECK_PIN) & 1u)
                           | (((status >> 17) & 1u) << 1));
    uint8_t faults = ((ctrl & 31u) != GPIO_FUNC_SIO ? 1u : 0u)
                   | (oe ? 2u : 0u)
                   | ((ctrl & 0x00033300u) ? 4u : 0u)
                   | ((pad & 0xccu) != 0x48u ? 8u : 0u);
    s_key_faults |= faults;
    if (!s_key_audit_started) { s_key_window_us = now; }
    s_key_sio_seen |= (uint8_t)(1u << (levels & 1u));
    s_key_pad_seen |= (uint8_t)(1u << ((levels >> 1) & 1u));
    if (s_key_audit_started) {
        if (((levels ^ s_key_levels) & 1u) && s_key_sio_changes < UINT16_MAX) {
            s_key_sio_changes++;
        }
        if (((levels ^ s_key_levels) & 2u) && s_key_pad_changes < UINT16_MAX) {
            s_key_pad_changes++;
        }
    }
    if (!s_key_audit_started || ((levels ^ s_key_levels) & 1u)) {
        if (s_key_audit_started) { s_key_edges++; }
        s_key_changed_us = now;
    }
    if (!s_key_audit_started || levels != s_key_levels
    || ctrl != s_key_prev_ctrl || pad != s_key_prev_pad || oe != s_key_prev_oe) {
        unsigned index = s_key_trace_count & 63u;
        s_key_trace[index] = (power_key_trace_entry_t){now, input, status, ctrl, pad};
        s_key_trace_count++;
    }
    s_key_levels = levels;
    s_key_prev_ctrl = ctrl;
    s_key_prev_pad = pad;
    s_key_prev_oe = oe;
    s_key_audit_started = true;
}

void power_key_service_get_audit(power_key_audit_t *out)
{
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t now = time_us_32();
    uint32_t age = s_key_audit_started ? (now - s_key_changed_us) / 1000u : 0;
    uint32_t window = s_key_audit_started ? (now - s_key_window_us) / 1000u : 0;
    *out = (power_key_audit_t){
        .edges = s_key_edges,
        .age_ms = (uint16_t)(age > 9999u ? 9999u : age),
        .levels = s_key_levels, .faults = s_key_faults,
        .sio_changes = s_key_sio_changes, .pad_changes = s_key_pad_changes,
        .window_ms = (uint16_t)(window > UINT16_MAX ? UINT16_MAX : window),
        .sio_seen = s_key_sio_seen, .pad_seen = s_key_pad_seen,
    };
    s_key_sio_changes = s_key_pad_changes = 0;
    s_key_sio_seen = s_key_pad_seen = 0;
    s_key_window_us = now;
    restore_interrupts(irq_state);
}

static void power_key_sample_irq(unsigned alarm_num)
{
    power_key_service_poll(to_ms_since_boot(get_absolute_time()));
    if (hardware_alarm_set_target(alarm_num,
                                  delayed_by_us(get_absolute_time(), 1000u))) {
        hardware_alarm_force_irq(alarm_num);
    }
}

bool power_key_service_start_sampling(void)
{
    if (s_alarm_num >= 0) {
        return true;
    }
    s_alarm_num = hardware_alarm_claim_unused(false);
    if (s_alarm_num < 0) {
        return false;
    }
    power_key_service_poll(to_ms_since_boot(get_absolute_time()));
    hardware_alarm_set_callback((unsigned)s_alarm_num, power_key_sample_irq);
    if (hardware_alarm_set_target((unsigned)s_alarm_num,
                                  delayed_by_us(get_absolute_time(), 1000u))) {
        hardware_alarm_force_irq((unsigned)s_alarm_num);
    }
    return true;
}

bool power_key_service_glide_ready(void)
{
    uint32_t irq_state = save_and_disable_interrupts();
    bool ready = s_state == POWER_KEY_READY && s_pressed
              && s_continuous_low_ms >= POWER_KEY_GLIDE_HOLD_MS;
    restore_interrupts(irq_state);
    return ready;
}

uint16_t power_key_service_max_sample_gap_ms(void)
{
    return s_max_sample_gap_ms;
}

void power_key_service_get_pulse_ms(uint16_t *low_ms, uint16_t *high_ms)
{
    uint32_t irq_state = save_and_disable_interrupts();
    *low_ms = s_last_low_ms;
    *high_ms = s_last_high_ms;
    restore_interrupts(irq_state);
}

bool power_key_service_is_pressed(void)
{
    return s_state == POWER_KEY_READY && s_pressed;
}

bool power_key_service_is_raw_pressed(void)
{
    return s_raw_pressed;
}

void power_key_service_set_airborne(bool airborne)
{
    uint32_t irq_state = save_and_disable_interrupts();
    s_airborne = airborne;
    if (airborne && s_pressed) {
        s_hold_used_in_air = true;
    }
    restore_interrupts(irq_state);
}

void power_key_service_poll(uint32_t now_ms)
{
    bool raw_pressed;
    uint32_t gap_ms = 0;

    if (s_state != POWER_KEY_START) {
        gap_ms = now_ms - s_last_poll_ms;
        if (gap_ms > s_max_sample_gap_ms) {
            s_max_sample_gap_ms = (uint16_t)(gap_ms > 9999u ? 9999u : gap_ms);
        }
    }
    s_last_poll_ms = now_ms;

    if (s_state == POWER_KEY_START) {
        gpio_init(POWER_KEEP_PIN);
        gpio_set_function(POWER_KEEP_PIN, GPIO_FUNC_SIO);
        gpio_set_dir(POWER_KEEP_PIN, GPIO_OUT);
        gpio_put(POWER_KEEP_PIN, 1);
        gpio_init(POWER_UP_CHECK_PIN);
        gpio_set_function(POWER_UP_CHECK_PIN, GPIO_FUNC_SIO);
        gpio_set_dir(POWER_UP_CHECK_PIN, GPIO_IN);
        gpio_pull_up(POWER_UP_CHECK_PIN);
        s_raw_pressed = gpio_get(POWER_UP_CHECK_PIN) == 0;
        s_pressed = s_raw_pressed;
        s_changed_ms = now_ms;
        s_state = POWER_KEY_SOURCE_CHECK;
        return;
    }

    power_key_audit_sample();
    raw_pressed = gpio_get(POWER_UP_CHECK_PIN) == 0;
    /* Preserve observed hold across scheduler delays and release bounce.
     * A delayed sample earns only 1 ms, never its whole unobserved gap. */
    if (raw_pressed && s_raw_pressed) {
        uint32_t held_ms = s_continuous_low_ms + (gap_ms <= 5u ? gap_ms : 1u);
        s_continuous_low_ms = (uint16_t)(held_ms > POWER_KEY_GLIDE_HOLD_MS
                                     ? POWER_KEY_GLIDE_HOLD_MS : held_ms);
    }
    if (raw_pressed != s_raw_pressed) {
        uint32_t duration_ms = now_ms - s_changed_ms;
        uint16_t duration = (uint16_t)(duration_ms > 9999u ? 9999u : duration_ms);
        if (s_raw_pressed) {
            s_last_low_ms = duration;
        } else {
            s_last_high_ms = duration;
        }
        s_raw_pressed = raw_pressed;
        s_changed_ms = now_ms;
    }
    /* Keep quick press response, but do not split a hold into new presses
     * when the power-key input briefly returns high. Startup is unchanged. */
    uint32_t debounce_ms = s_state == POWER_KEY_READY
                        ? (s_raw_pressed ? POWER_KEY_PRESS_MS
                           : (s_continuous_low_ms >= POWER_KEY_GLIDE_HOLD_MS
                              ? POWER_KEY_HELD_RELEASE_MS : POWER_KEY_RELEASE_MS))
                        : POWER_KEY_DEBOUNCE_MS;
    if ((uint32_t)(now_ms - s_changed_ms) >= debounce_ms) {
        s_pressed = s_raw_pressed;
        if (!s_pressed) {
            s_continuous_low_ms = 0;
        }
    }

    switch (s_state) {
        case POWER_KEY_SOURCE_CHECK:
            if (s_raw_pressed) {
                s_state = POWER_KEY_WAIT_RELEASE;
            } else {
                /* Preserve the existing USB/startup power-source behaviour. */
                gpio_put(POWER_KEEP_PIN, 0);
                s_state = POWER_KEY_WAIT_POWER_UP;
            }
            break;

        case POWER_KEY_WAIT_POWER_UP:
            if (s_pressed && s_raw_pressed) {
                gpio_put(POWER_KEEP_PIN, 1);
                s_state = POWER_KEY_WAIT_RELEASE;
            }
            break;

        case POWER_KEY_WAIT_RELEASE:
            if (!s_pressed && !s_raw_pressed
            &&  (uint32_t)(now_ms - s_changed_ms) >= POWER_KEY_DEBOUNCE_MS) {
                s_press_reported = false;
                s_state = POWER_KEY_READY;
            }
            break;

        case POWER_KEY_READY:
            if (s_pressed) {
                if (s_airborne) {
                    s_hold_used_in_air = true;
                }
                if (!s_press_reported) {
                    s_press_pending = true;
                    s_press_reported = true;
                }
                if (s_raw_pressed && !s_hold_used_in_air
                &&  (uint32_t)(now_ms - s_changed_ms) >= POWER_KEY_OFF_HOLD_MS) {
                    s_press_pending = false;
                    s_state = POWER_KEY_OFF;
                    gpio_put(POWER_KEEP_PIN, 0);
                }
            } else {
                s_press_reported = false;
                s_hold_used_in_air = false;
            }
            break;

        case POWER_KEY_OFF:
            gpio_put(POWER_KEEP_PIN, 0);
            break;

        default:
            s_state = POWER_KEY_START;
            break;
    }
}

bool power_key_service_consume_press(void)
{
    uint32_t irq_state = save_and_disable_interrupts();
    bool pressed = s_press_pending;

    s_press_pending = false;
    restore_interrupts(irq_state);
    return pressed;
}
