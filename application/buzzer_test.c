#include "buzzer_test.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include "keil_fc14_pcm.inc"

#define BUZZER_REST 0u
#define BUZZER_PCM_PWM_WRAP        255u
#define BUZZER_PCM_CARRIER_HZ      20000u
#define BUZZER_PCM_CLOCK_SLICE     7u

typedef enum buzzer_play_mode_t {
    BUZZER_PLAY_MODE_PCM = 0,
    BUZZER_PLAY_MODE_SCORE,
} buzzer_play_mode_t;

#define NOTE_A5     880u
#define NOTE_B5     988u
#define NOTE_E6     1319u
#define NOTE_GS6    1661u
#define NOTE_A6     1760u
#define NOTE_B6     1976u
#define NOTE_CS7    2217u
#define NOTE_DS7    2489u
#define NOTE_E7     2637u
#define NOTE_FS7    2960u
#define NOTE_GS7    3322u
#define NOTE_A7     3520u
#define NOTE_B7     3951u

static uint s_wSlice;
static uint s_wChannel;
static uint s_wPcmClockSlice;
static bool s_bInited;
static uint16_t s_hwNoteIndex;
static uint32_t s_wNextEventMS;
static bool s_bPausePrinted;
static const buzzer_score_t *s_ptScore = &c_tBuzzerScoreKeilKeygen;
static const buzzer_pcm_t *s_ptPcm = &c_tBuzzerPcmKeilFc14;
static buzzer_play_mode_t s_tPlayMode = BUZZER_PLAY_MODE_PCM;
static volatile bool s_bPcmActive;
static volatile uint32_t s_wPcmIndex;
static uint32_t s_wPcmRestartMS;

/*
 * Keil keygen-style single-channel score for a passive buzzer.
 * It is arranged from the downloaded FC14 tracker module into a compact
 * frequency/duration table that is easy to replace with another effect.
 */
static const buzzer_note_t c_tKeilKeygenNotes[] = {
    /* phrase A */
    { NOTE_E6, 120 }, { NOTE_B6, 120 }, { NOTE_E7, 120 }, { NOTE_DS7,120 },
    { NOTE_B6, 120 }, { NOTE_GS6,120 }, { NOTE_B6, 120 }, { NOTE_E7, 120 },
    { NOTE_FS7,120 }, { NOTE_E7, 120 }, { NOTE_DS7,120 }, { NOTE_B6, 120 },
    { NOTE_CS7,120 }, { NOTE_B6, 120 }, { NOTE_GS6,120 }, { NOTE_B6, 120 },

    /* phrase B */
    { NOTE_A6, 120 }, { NOTE_E7, 120 }, { NOTE_A7, 120 }, { NOTE_GS7,120 },
    { NOTE_E7, 120 }, { NOTE_CS7,120 }, { NOTE_E7, 120 }, { NOTE_A7, 120 },
    { NOTE_B7, 120 }, { NOTE_A7, 120 }, { NOTE_GS7,120 }, { NOTE_E7, 120 },
    { NOTE_FS7,120 }, { NOTE_E7, 120 }, { NOTE_CS7,120 }, { NOTE_E7, 120 },

    /* crunchy arpeggio turnaround */
    { NOTE_B5,  80 }, { NOTE_E6,  80 }, { NOTE_GS6, 80 }, { NOTE_B6,  80 },
    { NOTE_E7,  80 }, { NOTE_B6,  80 }, { NOTE_GS6, 80 }, { NOTE_E6,  80 },
    { NOTE_A5,  80 }, { NOTE_E6,  80 }, { NOTE_A6,  80 }, { NOTE_CS7, 80 },
    { NOTE_E7,  80 }, { NOTE_CS7, 80 }, { NOTE_A6,  80 }, { NOTE_E6,  80 },

    /* lead lick */
    { NOTE_E7,  90 }, { NOTE_FS7, 90 }, { NOTE_GS7, 90 }, { NOTE_B7, 150 },
    { NOTE_GS7, 90 }, { NOTE_FS7, 90 }, { NOTE_E7,  90 }, { NOTE_DS7,150 },
    { NOTE_E7,  90 }, { NOTE_FS7, 90 }, { NOTE_GS7, 90 }, { NOTE_A7, 150 },
    { NOTE_GS7, 90 }, { NOTE_E7,  90 }, { NOTE_CS7, 90 }, { NOTE_B6, 150 },

    /* chiptune sparkle */
    { NOTE_E7,  45 }, { NOTE_B7,  45 }, { NOTE_GS7, 45 }, { NOTE_E7,  45 },
    { NOTE_CS7, 45 }, { NOTE_E7,  45 }, { NOTE_GS7, 45 }, { NOTE_B7,  90 },
    { BUZZER_REST, 45 },
    { NOTE_A7,  45 }, { NOTE_E7,  45 }, { NOTE_CS7, 45 }, { NOTE_A6,  45 },
    { NOTE_CS7, 45 }, { NOTE_E7,  45 }, { NOTE_A7,  90 }, { BUZZER_REST, 45 },

    /* final hook */
    { NOTE_E6, 100 }, { NOTE_GS6,100 }, { NOTE_B6, 100 }, { NOTE_E7, 160 },
    { NOTE_DS7,80 }, { NOTE_E7,  80 }, { NOTE_FS7,100 }, { NOTE_E7, 100 },
    { NOTE_CS7,100 }, { NOTE_B6, 160 }, { BUZZER_REST, 60 },
    { NOTE_A6, 100 }, { NOTE_CS7,100 }, { NOTE_E7, 100 }, { NOTE_A7, 180 },
    { NOTE_GS7,90 }, { NOTE_E7,  90 }, { NOTE_B6, 120 }, { NOTE_E7, 220 },
};

const buzzer_score_t c_tBuzzerScoreKeilKeygen = {
    "Keil keygen FC14 buzzer cut",
    c_tKeilKeygenNotes,
    (uint16_t)(sizeof(c_tKeilKeygenNotes) / sizeof(c_tKeilKeygenNotes[0])),
    BUZZER_TEST_REPEAT_PAUSE_MS,
};

const buzzer_pcm_t c_tBuzzerPcmKeilFc14 = {
    "Keil FC14 rendered PCM",
    c_chKeilFc14Pcm,
    KEIL_FC14_PCM_SAMPLE_COUNT,
    KEIL_FC14_PCM_SAMPLE_RATE_HZ,
    BUZZER_TEST_REPEAT_PAUSE_MS,
};

static void buzzer_pcm_stop(void)
{
    pwm_set_irq_enabled(s_wPcmClockSlice, false);
    pwm_set_enabled(s_wPcmClockSlice, false);
    pwm_clear_irq(s_wPcmClockSlice);
    s_bPcmActive = false;
    s_wPcmIndex = 0u;
    pwm_set_chan_level(s_wSlice, s_wChannel, 0u);
}

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
            s_bPcmActive = false;
            pwm_set_irq_enabled(s_wPcmClockSlice, false);
            pwm_set_enabled(s_wPcmClockSlice, false);
            pwm_set_chan_level(s_wSlice, s_wChannel, 0u);
        }
    }
}

static void buzzer_pcm_pwm_init(void)
{
    uint32_t wClockHz = clock_get_hz(clk_sys);
    uint32_t wDiv16;
    pwm_config tConfig = pwm_get_default_config();

    wDiv16 = (uint32_t)(((uint64_t)wClockHz * 16u +
                       ((uint64_t)BUZZER_PCM_CARRIER_HZ *
                        (BUZZER_PCM_PWM_WRAP + 1u) - 1u)) /
                       ((uint64_t)BUZZER_PCM_CARRIER_HZ *
                        (BUZZER_PCM_PWM_WRAP + 1u)));
    if (wDiv16 < 16u) {
        wDiv16 = 16u;
    } else if (wDiv16 > 4095u) {
        wDiv16 = 4095u;
    }

    pwm_config_set_clkdiv_int_frac(&tConfig, wDiv16 / 16u, wDiv16 & 0x0Fu);
    pwm_config_set_wrap(&tConfig, BUZZER_PCM_PWM_WRAP);
    pwm_init(s_wSlice, &tConfig, false);
    pwm_set_chan_level(s_wSlice, s_wChannel, 0u);
    gpio_set_function(BUZZER_TEST_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(s_wSlice, true);
}

static void buzzer_pcm_start(void)
{
    uint32_t wClockHz = clock_get_hz(clk_sys);
    uint32_t wWrap;
    pwm_config tClockConfig;

    if ((s_ptPcm == NULL) || (s_ptPcm->pchSamples == NULL) ||
        (s_ptPcm->wCount == 0u) || (s_ptPcm->hwSampleRateHz == 0u)) {
        return;
    }

    buzzer_pcm_stop();
    buzzer_pcm_pwm_init();
    s_wPcmIndex = 0u;
    s_bPcmActive = true;

    wWrap = (wClockHz / s_ptPcm->hwSampleRateHz) - 1u;
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
}

static void buzzer_pwm_set(uint32_t wFreqHz, uint16_t hwDutyPermille)
{
    uint32_t wClockHz = clock_get_hz(clk_sys);
    uint32_t wWrap;
    uint32_t wLevel;
    uint32_t wDiv16;
    pwm_config tConfig;

    if (wFreqHz == 0u) {
        pwm_set_enabled(s_wSlice, false);
        gpio_set_function(BUZZER_TEST_PIN, GPIO_FUNC_SIO);
        gpio_set_dir(BUZZER_TEST_PIN, GPIO_OUT);
        gpio_put(BUZZER_TEST_PIN, 0);
        return;
    }

    if (hwDutyPermille > 1000u) {
        hwDutyPermille = 1000u;
    }

    wDiv16 = (uint32_t)(((uint64_t)wClockHz * 16u +
                       ((uint64_t)wFreqHz * 65536u - 1u)) /
                       ((uint64_t)wFreqHz * 65536u));
    if (wDiv16 < 16u) {
        wDiv16 = 16u;
    } else if (wDiv16 > 4095u) {
        wDiv16 = 4095u;
    }

    wWrap = (uint32_t)(((uint64_t)wClockHz * 16u) /
                       ((uint64_t)wFreqHz * wDiv16));
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

    tConfig = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&tConfig, wDiv16 / 16u, wDiv16 & 0x0Fu);
    pwm_config_set_wrap(&tConfig, (uint16_t)wWrap);
    pwm_init(s_wSlice, &tConfig, false);
    pwm_set_chan_level(s_wSlice, s_wChannel, (uint16_t)wLevel);
    gpio_set_function(BUZZER_TEST_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(s_wSlice, true);
}

void buzzer_test_init(void)
{
    s_wSlice = pwm_gpio_to_slice_num(BUZZER_TEST_PIN);
    s_wChannel = pwm_gpio_to_channel(BUZZER_TEST_PIN);
    s_wPcmClockSlice = BUZZER_PCM_CLOCK_SLICE;

    gpio_init(BUZZER_TEST_PIN);

    s_bInited = true;
    s_hwNoteIndex = 0u;
    s_wNextEventMS = 0u;
    s_bPausePrinted = false;
    s_wPcmRestartMS = 0u;

#if BUZZER_TEST_ENABLE_PRINTF
    printf("BUZZER test: GPIO%u PWM passive DET402, pcm=%s samples=%lu rate=%uHz\r\n",
           (unsigned)BUZZER_TEST_PIN,
           s_ptPcm->pchName,
           (unsigned long)s_ptPcm->wCount,
           (unsigned)s_ptPcm->hwSampleRateHz);
#endif

    if (s_tPlayMode == BUZZER_PLAY_MODE_PCM) {
        buzzer_pcm_start();
    } else {
        buzzer_pwm_set(0u, 0u);
    }
}

void buzzer_test_tone(uint16_t hwFreqHz, uint16_t hwDurationMS)
{
    buzzer_pcm_stop();
    buzzer_pwm_set(hwFreqHz, BUZZER_TEST_DUTY_PERMILLE);
    sleep_ms(hwDurationMS);
    buzzer_pwm_set(0u, 0u);
}

void buzzer_test_set_score(const buzzer_score_t *ptScore)
{
    if ((ptScore == NULL) || (ptScore->ptNotes == NULL) || (ptScore->hwCount == 0u)) {
        return;
    }

    s_ptScore = ptScore;
    s_tPlayMode = BUZZER_PLAY_MODE_SCORE;
    buzzer_pcm_stop();
    s_hwNoteIndex = 0u;
    s_wNextEventMS = 0u;
    s_bPausePrinted = false;
    buzzer_pwm_set(0u, 0u);
}

void buzzer_test_set_pcm(const buzzer_pcm_t *ptPcm)
{
    if ((ptPcm == NULL) || (ptPcm->pchSamples == NULL) ||
        (ptPcm->wCount == 0u) || (ptPcm->hwSampleRateHz == 0u)) {
        return;
    }

    s_ptPcm = ptPcm;
    s_tPlayMode = BUZZER_PLAY_MODE_PCM;
    s_wPcmRestartMS = 0u;
    if (s_bInited) {
        buzzer_pcm_start();
    }
}

void buzzer_test_task(void)
{
    uint32_t wNowMS = to_ms_since_boot(get_absolute_time());
    const buzzer_note_t *ptNote;

    if (!s_bInited) {
        buzzer_test_init();
    }

    if (s_tPlayMode == BUZZER_PLAY_MODE_PCM) {
        if (!s_bPcmActive && (s_wPcmIndex >= s_ptPcm->wCount) &&
            (s_wPcmRestartMS == 0u)) {
            s_wPcmRestartMS = wNowMS + s_ptPcm->hwRepeatPauseMS;

#if BUZZER_TEST_ENABLE_PRINTF
            printf("BUZZER PCM: loop end, pause %ums\r\n",
                   (unsigned)s_ptPcm->hwRepeatPauseMS);
#endif
        }

        if (!s_bPcmActive && (s_wPcmRestartMS != 0u) &&
            ((int32_t)(wNowMS - s_wPcmRestartMS) >= 0)) {
            s_wPcmRestartMS = 0u;
            buzzer_pcm_start();
        }
        return;
    }

    if ((int32_t)(wNowMS - s_wNextEventMS) < 0) {
        return;
    }

    if (s_hwNoteIndex >= s_ptScore->hwCount) {
        buzzer_pwm_set(0u, 0u);
        s_hwNoteIndex = 0u;
        s_wNextEventMS = wNowMS + s_ptScore->hwRepeatPauseMS;

#if BUZZER_TEST_ENABLE_PRINTF
        if (!s_bPausePrinted) {
            printf("BUZZER: loop end, pause %ums\r\n",
                   (unsigned)s_ptScore->hwRepeatPauseMS);
            s_bPausePrinted = true;
        }
#endif
        return;
    }

    s_bPausePrinted = false;
    ptNote = &s_ptScore->ptNotes[s_hwNoteIndex++];

    if (ptNote->hwFreqHz == BUZZER_REST) {
        buzzer_pwm_set(0u, 0u);
    } else {
        buzzer_pwm_set(ptNote->hwFreqHz, BUZZER_TEST_DUTY_PERMILLE);
    }

    s_wNextEventMS = wNowMS + ptNote->hwDurationMS;
}
