#include "buzzer_task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "drv_buzzer.h"
#include "pico/stdlib.h"

#include "keil_fc14_pcm.inc"

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

typedef enum buzzer_task_mode_t {
    BUZZER_TASK_MODE_PCM = 0,
    BUZZER_TASK_MODE_SCORE,
} buzzer_task_mode_t;

static const drv_buzzer_note_t c_tKeilKeygenNotes[] = {
    { NOTE_E6, 120 }, { NOTE_B6, 120 }, { NOTE_E7, 120 }, { NOTE_DS7,120 },
    { NOTE_B6, 120 }, { NOTE_GS6,120 }, { NOTE_B6, 120 }, { NOTE_E7, 120 },
    { NOTE_FS7,120 }, { NOTE_E7, 120 }, { NOTE_DS7,120 }, { NOTE_B6, 120 },
    { NOTE_CS7,120 }, { NOTE_B6, 120 }, { NOTE_GS6,120 }, { NOTE_B6, 120 },
    { NOTE_A6, 120 }, { NOTE_E7, 120 }, { NOTE_A7, 120 }, { NOTE_GS7,120 },
    { NOTE_E7, 120 }, { NOTE_CS7,120 }, { NOTE_E7, 120 }, { NOTE_A7, 120 },
    { NOTE_B7, 120 }, { NOTE_A7, 120 }, { NOTE_GS7,120 }, { NOTE_E7, 120 },
    { NOTE_FS7,120 }, { NOTE_E7, 120 }, { NOTE_CS7,120 }, { NOTE_E7, 120 },
    { NOTE_B5,  80 }, { NOTE_E6,  80 }, { NOTE_GS6, 80 }, { NOTE_B6,  80 },
    { NOTE_E7,  80 }, { NOTE_B6,  80 }, { NOTE_GS6, 80 }, { NOTE_E6,  80 },
    { NOTE_A5,  80 }, { NOTE_E6,  80 }, { NOTE_A6,  80 }, { NOTE_CS7, 80 },
    { NOTE_E7,  80 }, { NOTE_CS7, 80 }, { NOTE_A6,  80 }, { NOTE_E6,  80 },
    { NOTE_E7,  90 }, { NOTE_FS7, 90 }, { NOTE_GS7, 90 }, { NOTE_B7, 150 },
    { NOTE_GS7, 90 }, { NOTE_FS7, 90 }, { NOTE_E7,  90 }, { NOTE_DS7,150 },
    { NOTE_E7,  90 }, { NOTE_FS7, 90 }, { NOTE_GS7, 90 }, { NOTE_A7, 150 },
    { NOTE_GS7, 90 }, { NOTE_E7,  90 }, { NOTE_CS7, 90 }, { NOTE_B6, 150 },
    { NOTE_E7,  45 }, { NOTE_B7,  45 }, { NOTE_GS7, 45 }, { NOTE_E7,  45 },
    { NOTE_CS7, 45 }, { NOTE_E7,  45 }, { NOTE_GS7, 45 }, { NOTE_B7,  90 },
    { DRV_BUZZER_REST, 45 },
    { NOTE_A7,  45 }, { NOTE_E7,  45 }, { NOTE_CS7, 45 }, { NOTE_A6,  45 },
    { NOTE_CS7, 45 }, { NOTE_E7,  45 }, { NOTE_A7,  90 }, { DRV_BUZZER_REST, 45 },
    { NOTE_E6, 100 }, { NOTE_GS6,100 }, { NOTE_B6, 100 }, { NOTE_E7, 160 },
    { NOTE_DS7,80 }, { NOTE_E7,  80 }, { NOTE_FS7,100 }, { NOTE_E7, 100 },
    { NOTE_CS7,100 }, { NOTE_B6, 160 }, { DRV_BUZZER_REST, 60 },
    { NOTE_A6, 100 }, { NOTE_CS7,100 }, { NOTE_E7, 100 }, { NOTE_A7, 180 },
    { NOTE_GS7,90 }, { NOTE_E7,  90 }, { NOTE_B6, 120 }, { NOTE_E7, 220 },
};

static const drv_buzzer_score_t c_tBuzzerScoreKeilKeygen = {
    "Keil keygen FC14 buzzer cut",
    c_tKeilKeygenNotes,
    (uint16_t)(sizeof(c_tKeilKeygenNotes) / sizeof(c_tKeilKeygenNotes[0])),
};

static const drv_buzzer_pcm_t c_tBuzzerPcmKeilFc14 = {
    "Keil FC14 rendered PCM",
    c_chKeilFc14Pcm,
    KEIL_FC14_PCM_SAMPLE_COUNT,
    KEIL_FC14_PCM_SAMPLE_RATE_HZ,
};

static bool s_bInited;
static bool s_bPausePrinted;
static uint32_t s_wRestartMS;
static buzzer_task_mode_t s_tMode = BUZZER_TASK_MODE_PCM;

void buzzer_task_init(void)
{
    drv_buzzer_init();
    s_bInited = true;
    s_bPausePrinted = false;
    s_wRestartMS = 0u;

#if BUZZER_TASK_ENABLE_PRINTF
    printf("BUZZER task: GPIO%u PWM passive DET402, pcm=%s samples=%lu rate=%uHz\r\n",
           (unsigned)DRV_BUZZER_PIN,
           c_tBuzzerPcmKeilFc14.pchName,
           (unsigned long)c_tBuzzerPcmKeilFc14.wCount,
           (unsigned)c_tBuzzerPcmKeilFc14.hwSampleRateHz);
#endif

    if (s_tMode == BUZZER_TASK_MODE_PCM) {
        (void)drv_buzzer_pcm_start(&c_tBuzzerPcmKeilFc14);
    } else {
        (void)drv_buzzer_score_start(&c_tBuzzerScoreKeilKeygen);
    }
}

void buzzer_task(uint32_t wPeriodMS)
{
    uint32_t wNowMS = to_ms_since_boot(get_absolute_time());

    if (!s_bInited) {
        buzzer_task_init();
    }

    if (wPeriodMS == 0u) {
        wPeriodMS = BUZZER_TASK_REPEAT_PAUSE_MS;
    }

    if (s_tMode == BUZZER_TASK_MODE_PCM) {
        if (!drv_buzzer_pcm_is_active() &&
            (drv_buzzer_pcm_get_index() >= c_tBuzzerPcmKeilFc14.wCount) &&
            (s_wRestartMS == 0u)) {
            s_wRestartMS = wNowMS + wPeriodMS;

#if BUZZER_TASK_ENABLE_PRINTF
            if (!s_bPausePrinted) {
                printf("BUZZER PCM: loop end, pause %lums\r\n", (unsigned long)wPeriodMS);
                s_bPausePrinted = true;
            }
#endif
        }

        if (!drv_buzzer_pcm_is_active() && (s_wRestartMS != 0u) &&
            ((int32_t)(wNowMS - s_wRestartMS) >= 0)) {
            s_wRestartMS = 0u;
            s_bPausePrinted = false;
            (void)drv_buzzer_pcm_start(&c_tBuzzerPcmKeilFc14);
        }
        return;
    }

    if (!drv_buzzer_score_task()) {
        if (s_wRestartMS == 0u) {
            s_wRestartMS = wNowMS + wPeriodMS;
#if BUZZER_TASK_ENABLE_PRINTF
            if (!s_bPausePrinted) {
                printf("BUZZER: score end, pause %lums\r\n", (unsigned long)wPeriodMS);
                s_bPausePrinted = true;
            }
#endif
        }

        if ((int32_t)(wNowMS - s_wRestartMS) >= 0) {
            s_wRestartMS = 0u;
            s_bPausePrinted = false;
            (void)drv_buzzer_score_start(&c_tBuzzerScoreKeilKeygen);
        }
    }
}
