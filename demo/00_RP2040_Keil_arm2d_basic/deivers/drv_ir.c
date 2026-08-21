/**
 * @file drv_ir.c
 * @brief IR transmit (PWM burst) and receive (GPIO edge timing capture + decode).
 *
 * Frame format (single byte, LSB first after start):
 *   Start: long mark, short space
 *   Each bit: fixed-length mark, short space = 0, long space = 1
 *   Trailing mark pulse
 *
 * TX is cooperative: drv_ir_send_task() advances a precomputed step table on time_us_32().
 * RX uses GPIO callbacks to record time between edges; drv_ir_receive_snapshot() copies
 * a completed frame when inter-pulse gap exceeds DRV_IR_FRAME_GAP_US.
 */

#include "drv_ir.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

/** Bit cell timings (microseconds); tuned for the demodulator and LED drive. */
#define DRV_IR_MARK_US                 600u
#define DRV_IR_START_MARK_US           2400u
#define DRV_IR_START_SPACE_US          600u
#define DRV_IR_ZERO_SPACE_US           600u
#define DRV_IR_ONE_SPACE_US            1600u
#define DRV_IR_FRAME_GAP_US            5000u
#define DRV_IR_TX_STEP_COUNT           19u

/** One TX segment: carrier on (mark) or off (space) for wDurationUS. */
typedef struct drv_ir_tx_step_t {
    bool bMark;
    uint32_t wDurationUS;
} drv_ir_tx_step_t;

static uint32_t s_wCarrierHz = DRV_IR_CARRIER_HZ;
static uint16_t s_hwDutyPermille = DRV_IR_DUTY_PERMILLE;
static uint s_wSlice;
static uint s_wChannel;

/** RX ISR state: edge list while a frame is being received. */
static volatile bool s_bRxReceiving;
static volatile bool s_bRxDone;
static volatile bool s_bRxOverflow;
static volatile uint8_t s_chRxCount;
static volatile uint32_t s_wLastEdgeUS;
static volatile uint8_t s_chRxLevel[DRV_IR_RX_CAPTURE_MAX];
static volatile uint32_t s_wRxDurationUS[DRV_IR_RX_CAPTURE_MAX];

/** TX polling state. */
static bool s_bTxActive;
static uint8_t s_chTxStepIndex;
static uint32_t s_wTxNextUS;
static drv_ir_tx_step_t s_tTxSteps[DRV_IR_TX_STEP_COUNT];

/** True if wValue lies within [wTarget - wTolerance, wTarget + wTolerance]. */
static bool drv_ir_us_in_range(uint32_t wValue, uint32_t wTarget, uint32_t wTolerance)
{
    return (wValue >= (wTarget - wTolerance)) && (wValue <= (wTarget + wTolerance));
}

/** Disable PWM and drive TX pin to inactive level (no IR emission). */
static void drv_ir_tx_pwm_off(void)
{
    pwm_set_enabled(s_wSlice, false);
    gpio_set_function(DRV_IR_TX_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(DRV_IR_TX_PIN, GPIO_OUT);
    gpio_put(DRV_IR_TX_PIN, DRV_IR_TX_ACTIVE_LOW ? 1 : 0);
}

/** Route TX pin to PWM and enable slice (carrier on). */
static void drv_ir_tx_pwm_on(void)
{
    gpio_set_function(DRV_IR_TX_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(s_wSlice, true);
}

/**
 * GPIO IRQ on RX pin: record duration since last edge and logical level after edge.
 * Long idle gap restarts capture (new frame). Overflow if more edges than buffer.
 */
static void drv_ir_rx_gpio_irq(uint gpio, uint32_t events)
{
    uint32_t wNowUS;
    bool bNewLevel;
    uint32_t wDurationUS;

    (void)events;

    if (gpio != DRV_IR_RX_PIN) {
        return;
    }

    wNowUS = time_us_32();
    bNewLevel = gpio_get(DRV_IR_RX_PIN) ? true : false;
    wDurationUS = wNowUS - s_wLastEdgeUS;

    if (!s_bRxReceiving || (wDurationUS > DRV_IR_FRAME_GAP_US)) {
        s_bRxReceiving = true;
        s_bRxDone = false;
        s_bRxOverflow = false;
        s_chRxCount = 0;
        s_wLastEdgeUS = wNowUS;
        return;
    }

    if (!s_bRxDone) {
        uint8_t chCount = s_chRxCount;

        if (chCount < DRV_IR_RX_CAPTURE_MAX) {
            /* Store inverted sense: 0 = low after edge (demodulated burst seen). */
            s_chRxLevel[chCount] = bNewLevel ? 0u : 1u;
            s_wRxDurationUS[chCount] = wDurationUS;
            s_chRxCount = (uint8_t)(chCount + 1u);
        } else {
            s_bRxOverflow = true;
        }
    }

    s_wLastEdgeUS = wNowUS;
}

/**
 * If no edge arrived for DRV_IR_FRAME_GAP_US, treat capture as complete (main context).
 * Interrupts are briefly masked to avoid races with the ISR.
 */
static void drv_ir_rx_finalize_if_idle(void)
{
    uint32_t wState = save_and_disable_interrupts();

    if (s_bRxReceiving && !s_bRxDone &&
        ((uint32_t)(time_us_32() - s_wLastEdgeUS) > DRV_IR_FRAME_GAP_US)) {
        s_bRxReceiving = false;
        s_bRxDone = true;
    }

    restore_interrupts(wState);
}

void drv_ir_reset_rx(void)
{
    uint32_t wState = save_and_disable_interrupts();

    s_bRxReceiving = false;
    s_bRxDone = false;
    s_bRxOverflow = false;
    s_chRxCount = 0;
    s_wLastEdgeUS = time_us_32();

    restore_interrupts(wState);
}

void drv_ir_set_carrier(uint32_t wCarrierHz, uint16_t hwDutyPermille)
{
    uint32_t wClockHz = clock_get_hz(clk_sys);
    uint32_t wWrap;
    uint32_t wLevel;
    uint32_t wDiv16;
    pwm_config tConfig;

    if (wCarrierHz == 0u) {
        wCarrierHz = 1u;
    }

    if (hwDutyPermille > 1000u) {
        hwDutyPermille = 1000u;
    }

    wDiv16 = (uint32_t)(((uint64_t)wClockHz * 16u +
                       ((uint64_t)wCarrierHz * 65536u - 1u)) /
                       ((uint64_t)wCarrierHz * 65536u));
    if (wDiv16 < 16u) {
        wDiv16 = 16u;
    } else if (wDiv16 > 4095u) {
        wDiv16 = 4095u;
    }

    wWrap = (uint32_t)(((uint64_t)wClockHz * 16u) /
                       ((uint64_t)wCarrierHz * wDiv16));
    if (wWrap == 0u) {
        wWrap = 1u;
    }
    wWrap -= 1u;
    if (wWrap > 65535u) {
        wWrap = 65535u;
    }

    wLevel = ((wWrap + 1u) * hwDutyPermille) / 1000u;
    if (wLevel > wWrap) {
        wLevel = wWrap;
    }

    s_wCarrierHz = wCarrierHz;
    s_hwDutyPermille = hwDutyPermille;

    tConfig = pwm_get_default_config();
    pwm_config_set_wrap(&tConfig, (uint16_t)wWrap);
    pwm_config_set_clkdiv_int_frac(&tConfig, (uint8_t)(wDiv16 >> 4),
                                   (uint8_t)(wDiv16 & 0x0fu));
    pwm_init(s_wSlice, &tConfig, false);
    pwm_set_chan_level(s_wSlice, s_wChannel, (uint16_t)wLevel);

#if DRV_IR_TX_ACTIVE_LOW
    pwm_set_output_polarity(s_wSlice,
                            s_wChannel == PWM_CHAN_A,
                            s_wChannel == PWM_CHAN_B);
#else
    pwm_set_output_polarity(s_wSlice, false, false);
#endif

    drv_ir_tx_pwm_off();
}

void drv_ir_init(uint32_t wCarrierHz, uint16_t hwDutyPermille)
{
    s_wSlice = pwm_gpio_to_slice_num(DRV_IR_TX_PIN);
    s_wChannel = pwm_gpio_to_channel(DRV_IR_TX_PIN);

    gpio_init(DRV_IR_RX_PIN);
    gpio_set_function(DRV_IR_RX_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(DRV_IR_RX_PIN, GPIO_IN);
    gpio_pull_up(DRV_IR_RX_PIN);

    drv_ir_set_carrier(wCarrierHz, hwDutyPermille);
    drv_ir_reset_rx();

    gpio_set_irq_enabled_with_callback(DRV_IR_RX_PIN,
                                       GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
                                       true,
                                       drv_ir_rx_gpio_irq);
}

bool drv_ir_send_byte_start(uint8_t chData)
{
    uint8_t chIndex = 0;

    if (s_bTxActive) {
        return false;
    }

    s_tTxSteps[chIndex++] = (drv_ir_tx_step_t){ true, DRV_IR_START_MARK_US };
    s_tTxSteps[chIndex++] = (drv_ir_tx_step_t){ false, DRV_IR_START_SPACE_US };

    for (uint8_t chBit = 0; chBit < 8u; chBit++) {
        s_tTxSteps[chIndex++] = (drv_ir_tx_step_t){ true, DRV_IR_MARK_US };
        s_tTxSteps[chIndex++] = (drv_ir_tx_step_t){
            false,
            (chData & (1u << chBit)) ? DRV_IR_ONE_SPACE_US : DRV_IR_ZERO_SPACE_US
        };
    }

    s_tTxSteps[chIndex++] = (drv_ir_tx_step_t){ true, DRV_IR_MARK_US };

    s_chTxStepIndex = 0u;
    s_wTxNextUS = time_us_32();
    s_bTxActive = true;
    (void)drv_ir_send_task();

    return true;
}

bool drv_ir_send_task(void)
{
    uint32_t wNowUS;

    if (!s_bTxActive) {
        return false;
    }

    wNowUS = time_us_32();
    if ((int32_t)(wNowUS - s_wTxNextUS) < 0) {
        return true;
    }

    if (s_chTxStepIndex >= DRV_IR_TX_STEP_COUNT) {
        drv_ir_tx_pwm_off();
        s_bTxActive = false;
        return false;
    }

    if (s_tTxSteps[s_chTxStepIndex].bMark) {
        drv_ir_tx_pwm_on();
    } else {
        drv_ir_tx_pwm_off();
    }

    s_wTxNextUS = wNowUS + s_tTxSteps[s_chTxStepIndex].wDurationUS;
    s_chTxStepIndex++;

    return true;
}

bool drv_ir_is_sending(void)
{
    return s_bTxActive;
}

bool drv_ir_receive_snapshot(drv_ir_rx_capture_t *ptCapture)
{
    bool bDone;

    if (ptCapture == NULL) {
        return false;
    }

    drv_ir_rx_finalize_if_idle();

    uint32_t wState = save_and_disable_interrupts();
    bDone = s_bRxDone;

    if (bDone) {
        uint8_t chCount = s_chRxCount;
        uint32_t wLowPulseCount = 0;
        uint32_t wTotalLowUS = 0;

        ptCapture->chCount = chCount;
        ptCapture->bOverflow = s_bRxOverflow;

        for (uint8_t chIndex = 0; chIndex < chCount; chIndex++) {
            uint8_t chLevel = s_chRxLevel[chIndex];
            uint32_t wDurationUS = s_wRxDurationUS[chIndex];

            ptCapture->chLevel[chIndex] = chLevel;
            ptCapture->wDurationUS[chIndex] = wDurationUS;

            if (chLevel == 0u) {
                wLowPulseCount++;
                wTotalLowUS += wDurationUS;
            }
        }

        ptCapture->wLowPulseCount = wLowPulseCount;
        ptCapture->wTotalLowUS = wTotalLowUS;
        s_bRxDone = false;
    }

    restore_interrupts(wState);

    return bDone;
}

bool drv_ir_decode_capture(const drv_ir_rx_capture_t *ptCapture, uint8_t *pchData)
{
    uint8_t chData = 0;
    uint8_t chIndex = 0;

    if ((ptCapture == NULL) || (pchData == NULL)) {
        return false;
    }

    /* Skip leading high-idle segments until first low (start mark). */
    while ((chIndex < ptCapture->chCount) && (ptCapture->chLevel[chIndex] != 0u)) {
        chIndex++;
    }

    if ((chIndex + 18u) >= ptCapture->chCount) {
        return false;
    }

    if (!drv_ir_us_in_range(ptCapture->wDurationUS[chIndex],
                            DRV_IR_START_MARK_US, 900u)) {
        return false;
    }
    chIndex++;

    if ((ptCapture->chLevel[chIndex] != 1u) ||
        !drv_ir_us_in_range(ptCapture->wDurationUS[chIndex],
                            DRV_IR_START_SPACE_US, 350u)) {
        return false;
    }
    chIndex++;

    for (uint8_t chBit = 0; chBit < 8u; chBit++) {
        if ((ptCapture->chLevel[chIndex] != 0u) ||
            !drv_ir_us_in_range(ptCapture->wDurationUS[chIndex],
                                DRV_IR_MARK_US, 350u)) {
            return false;
        }
        chIndex++;

        if (ptCapture->chLevel[chIndex] != 1u) {
            return false;
        }

        if (ptCapture->wDurationUS[chIndex] > 1000u) {
            if (!drv_ir_us_in_range(ptCapture->wDurationUS[chIndex],
                                    DRV_IR_ONE_SPACE_US, 700u)) {
                return false;
            }
            chData |= (uint8_t)(1u << chBit);
        } else {
            if (!drv_ir_us_in_range(ptCapture->wDurationUS[chIndex],
                                    DRV_IR_ZERO_SPACE_US, 350u)) {
                return false;
            }
        }
        chIndex++;
    }

    if ((chIndex < ptCapture->chCount) &&
        (ptCapture->chLevel[chIndex] == 0u) &&
        !drv_ir_us_in_range(ptCapture->wDurationUS[chIndex],
                            DRV_IR_MARK_US, 350u)) {
        return false;
    }

    *pchData = chData;
    return true;
}

uint32_t drv_ir_get_carrier_hz(void)
{
    return s_wCarrierHz;
}

uint16_t drv_ir_get_duty_permille(void)
{
    return s_hwDutyPermille;
}

bool drv_ir_get_rx_level(void)
{
    return gpio_get(DRV_IR_RX_PIN) ? true : false;
}
