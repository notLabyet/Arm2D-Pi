/**
 * @file drv_buzzer.c
 * @brief Implementation: PWM tone generation, score scheduler, and PCM via PWM IRQ.
 */

#include "drv_buzzer.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

/** PCM path uses 8-bit sample values as PWM compare max; wrap 255 gives fine duty steps. */
#define DRV_BUZZER_PCM_PWM_WRAP       255u

/** High-level operating mode (mutually exclusive playback paths). */
typedef enum drv_buzzer_mode_t {
    DRV_BUZZER_MODE_IDLE = 0,
    DRV_BUZZER_MODE_SCORE,
    DRV_BUZZER_MODE_PCM,
} drv_buzzer_mode_t;

/** PWM slice tied to DRV_BUZZER_PIN. */
static uint s_wSlice;
/** Channel A or B within s_wSlice for DRV_BUZZER_PIN. */
static uint s_wChannel;
/** Separate slice used only to generate wrap interrupts at PCM sample rate. */
static uint s_wPcmClockSlice;
static bool s_bInited;
static drv_buzzer_mode_t s_tMode = DRV_BUZZER_MODE_IDLE;

/** Score playback state. */
static const drv_buzzer_score_t *s_ptScore;
static uint16_t s_hwNoteIndex;
static uint32_t s_wNextNoteMS;

/** PCM descriptor and IRQ-driven position. */
static const drv_buzzer_pcm_t *s_ptPcm;
static volatile bool s_bPcmActive;
static volatile uint32_t s_wPcmIndex;

/**
 * Configure the buzzer PWM for a given output frequency and duty cycle.
 *
 * @param wFreqHz      Output frequency; 0 disables PWM and drives GPIO low.
 * @param hwDutyPermille Duty 0–1000 (thousandths of period high).
 *
 * The RP2040 PWM clock divider and wrap are derived from clk_sys so the tone
 * stays correct if the core clock changes (within hardware limits).
 */
static void drv_buzzer_pwm_set(uint32_t wFreqHz, uint16_t hwDutyPermille)
{
    uint32_t wClockHz = clock_get_hz(clk_sys);
    uint32_t wWrap;
    uint32_t wLevel;
    uint32_t wDiv16;
    pwm_config tConfig;

    if (wFreqHz == 0u) {
        pwm_set_enabled(s_wSlice, false);
        gpio_set_function(DRV_BUZZER_PIN, GPIO_FUNC_SIO);
        gpio_set_dir(DRV_BUZZER_PIN, GPIO_OUT);
        gpio_put(DRV_BUZZER_PIN, 0);
        return;
    }

    if (hwDutyPermille > 1000u) {
        hwDutyPermille = 1000u;
    }

    /* Integer+fractional divider in 16.4 fixed point, clamped to PWM HW range. */
    wDiv16 = (uint32_t)(((uint64_t)wClockHz * 16u +
                       ((uint64_t)wFreqHz * 65536u - 1u)) /
                       ((uint64_t)wFreqHz * 65536u));
    if (wDiv16 < 16u) {
        wDiv16 = 16u;
    } else if (wDiv16 > 4095u) {
        wDiv16 = 4095u;
    }

    /* Counts per PWM period from sysclk and divider. */
    wWrap = (uint32_t)(((uint64_t)wClockHz * 16u) /
                       ((uint64_t)wFreqHz * wDiv16));
    if (wWrap == 0u) {
        wWrap = 1u;
    }
    wWrap -= 1u;
    if (wWrap > 65535u) {
        wWrap = 65535u;
    }

    /* Compare level for requested duty; cannot exceed wrap. */
    wLevel = ((wWrap + 1u) * hwDutyPermille) / 1000u;
    if (wLevel > wWrap) {
        wLevel = wWrap;
    }

    tConfig = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&tConfig, wDiv16 / 16u, wDiv16 & 0x0Fu);
    pwm_config_set_wrap(&tConfig, (uint16_t)wWrap);
    pwm_init(s_wSlice, &tConfig, false);
    pwm_set_chan_level(s_wSlice, s_wChannel, (uint16_t)wLevel);
    gpio_set_function(DRV_BUZZER_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(s_wSlice, true);
}

/**
 * Fixed high-frequency PWM carrier for PCM: sample value only changes duty,
 * keeping the acoustic fundamental near DRV_BUZZER_PCM_CARRIER_HZ.
 */
static void drv_buzzer_pcm_pwm_init(void)
{
    uint32_t wClockHz = clock_get_hz(clk_sys);
    uint32_t wDiv16;
    pwm_config tConfig = pwm_get_default_config();

    wDiv16 = (uint32_t)(((uint64_t)wClockHz * 16u +
                       ((uint64_t)DRV_BUZZER_PCM_CARRIER_HZ *
                        (DRV_BUZZER_PCM_PWM_WRAP + 1u) - 1u)) /
                       ((uint64_t)DRV_BUZZER_PCM_CARRIER_HZ *
                        (DRV_BUZZER_PCM_PWM_WRAP + 1u)));
    if (wDiv16 < 16u) {
        wDiv16 = 16u;
    } else if (wDiv16 > 4095u) {
        wDiv16 = 4095u;
    }

    pwm_config_set_clkdiv_int_frac(&tConfig, wDiv16 / 16u, wDiv16 & 0x0Fu);
    pwm_config_set_wrap(&tConfig, DRV_BUZZER_PCM_PWM_WRAP);
    pwm_init(s_wSlice, &tConfig, false);
    pwm_set_chan_level(s_wSlice, s_wChannel, 0u);
    gpio_set_function(DRV_BUZZER_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(s_wSlice, true);
}

/**
 * PWM wrap interrupt: on the “clock” slice, push the next PCM sample to the buzzer channel.
 * End-of-buffer calls drv_buzzer_pcm_stop() from IRQ context.
 */
void PWM_IRQ_WRAP_Handler(void)
{
    if (pwm_get_irq_status_mask() & (1u << s_wPcmClockSlice)) {
        pwm_clear_irq(s_wPcmClockSlice);

        if (s_bPcmActive && (s_ptPcm != NULL) &&
            (s_ptPcm->pchSamples != NULL) &&
            (s_wPcmIndex < s_ptPcm->wCount)) {
            pwm_set_chan_level(s_wSlice, s_wChannel,
                               s_ptPcm->pchSamples[s_wPcmIndex++]);
        } else {
            drv_buzzer_pcm_stop();
        }
    }
}

void drv_buzzer_init(void)
{
    s_wSlice = pwm_gpio_to_slice_num(DRV_BUZZER_PIN);
    s_wChannel = pwm_gpio_to_channel(DRV_BUZZER_PIN);
    s_wPcmClockSlice = DRV_BUZZER_PCM_CLOCK_SLICE;

    gpio_init(DRV_BUZZER_PIN);
    s_bInited = true;
    drv_buzzer_stop();
}

void drv_buzzer_stop(void)
{
    drv_buzzer_pcm_stop();
    drv_buzzer_pwm_set(0u, 0u);
    s_tMode = DRV_BUZZER_MODE_IDLE;
    s_ptScore = NULL;
    s_hwNoteIndex = 0u;
    s_wNextNoteMS = 0u;
}

void drv_buzzer_set_tone(uint16_t hwFreqHz, uint16_t hwDutyPermille)
{
    if (!s_bInited) {
        drv_buzzer_init();
    }

    drv_buzzer_pcm_stop();
    s_tMode = (hwFreqHz == 0u) ? DRV_BUZZER_MODE_IDLE : DRV_BUZZER_MODE_SCORE;
    drv_buzzer_pwm_set(hwFreqHz, hwDutyPermille);
}

bool drv_buzzer_score_start(const drv_buzzer_score_t *ptScore)
{
    if ((ptScore == NULL) || (ptScore->ptNotes == NULL) || (ptScore->hwCount == 0u)) {
        return false;
    }

    if (!s_bInited) {
        drv_buzzer_init();
    }

    drv_buzzer_pcm_stop();
    s_ptScore = ptScore;
    s_hwNoteIndex = 0u;
    s_wNextNoteMS = 0u;
    s_tMode = DRV_BUZZER_MODE_SCORE;
    drv_buzzer_pwm_set(0u, 0u);
    return true;
}

bool drv_buzzer_score_task(void)
{
    uint32_t wNowMS;
    const drv_buzzer_note_t *ptNote;

    if ((s_tMode != DRV_BUZZER_MODE_SCORE) || (s_ptScore == NULL)) {
        return false;
    }

    wNowMS = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(wNowMS - s_wNextNoteMS) < 0) {
        return true;
    }

    if (s_hwNoteIndex >= s_ptScore->hwCount) {
        drv_buzzer_pwm_set(0u, 0u);
        s_tMode = DRV_BUZZER_MODE_IDLE;
        return false;
    }

    ptNote = &s_ptScore->ptNotes[s_hwNoteIndex++];
    drv_buzzer_pwm_set(ptNote->hwFreqHz, DRV_BUZZER_DUTY_PERMILLE);
    s_wNextNoteMS = wNowMS + ptNote->hwDurationMS;

    return true;
}

bool drv_buzzer_score_is_active(void)
{
    return s_tMode == DRV_BUZZER_MODE_SCORE;
}

bool drv_buzzer_pcm_start(const drv_buzzer_pcm_t *ptPcm)
{
    uint32_t wClockHz;
    uint32_t wWrap;
    pwm_config tClockConfig;

    if ((ptPcm == NULL) || (ptPcm->pchSamples == NULL) ||
        (ptPcm->wCount == 0u) || (ptPcm->hwSampleRateHz == 0u)) {
        return false;
    }

    if (!s_bInited) {
        drv_buzzer_init();
    }

    drv_buzzer_pcm_stop();
    s_ptPcm = ptPcm;
    drv_buzzer_pcm_pwm_init();
    s_wPcmIndex = 0u;
    s_bPcmActive = true;
    s_tMode = DRV_BUZZER_MODE_PCM;

    /* Second slice: wrap rate = sample rate for IRQ pacing. */
    wClockHz = clock_get_hz(clk_sys);
    wWrap = (wClockHz / ptPcm->hwSampleRateHz) - 1u;
    if (wWrap > 65535u) {
        wWrap = 65535u;
    }

    tClockConfig = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&tClockConfig, 1u, 0u);
    pwm_config_set_wrap(&tClockConfig, (uint16_t)wWrap);
    pwm_init(s_wPcmClockSlice, &tClockConfig, false);
    pwm_clear_irq(s_wPcmClockSlice);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, PWM_IRQ_WRAP_Handler);
    irq_clear_pending(PWM_IRQ_WRAP);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_set_irq_enabled(s_wPcmClockSlice, true);
    pwm_set_enabled(s_wPcmClockSlice, true);

    return true;
}

void drv_buzzer_pcm_stop(void)
{
    bool bWasAtEnd = false;

    if ((s_ptPcm != NULL) && (s_wPcmIndex >= s_ptPcm->wCount)) {
        bWasAtEnd = true;
    }

    pwm_set_irq_enabled(s_wPcmClockSlice, false);
    pwm_set_enabled(s_wPcmClockSlice, false);
    pwm_clear_irq(s_wPcmClockSlice);
    s_bPcmActive = false;
    if (!bWasAtEnd) {
        s_wPcmIndex = 0u;
    }
    pwm_set_chan_level(s_wSlice, s_wChannel, 0u);
}

bool drv_buzzer_pcm_is_active(void)
{
    return s_bPcmActive;
}

uint32_t drv_buzzer_pcm_get_index(void)
{
    return s_wPcmIndex;
}
