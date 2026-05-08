#ifndef __BUZZER_TEST_H__
#define __BUZZER_TEST_H__

#include <stdint.h>

/*
 * Schematic: BEEF_EN -> GPIO23 -> R13 100R -> BUZZER1 DET402.
 * DET402-G-1 is a passive magnetic buzzer, so it needs a PWM tone.
 */
#ifndef BUZZER_TEST_PIN
#   define BUZZER_TEST_PIN                 23u
#endif

#ifndef BUZZER_TEST_DUTY_PERMILLE
#   define BUZZER_TEST_DUTY_PERMILLE       500u
#endif

#ifndef BUZZER_TEST_REPEAT_PAUSE_MS
#   define BUZZER_TEST_REPEAT_PAUSE_MS     900u
#endif

#ifndef BUZZER_TEST_ENABLE_PRINTF
#   define BUZZER_TEST_ENABLE_PRINTF       1
#endif

typedef struct buzzer_note_t {
    uint16_t hwFreqHz;
    uint16_t hwDurationMS;
} buzzer_note_t;

typedef struct buzzer_score_t {
    const char *pchName;
    const buzzer_note_t *ptNotes;
    uint16_t hwCount;
    uint16_t hwRepeatPauseMS;
} buzzer_score_t;

typedef struct buzzer_pcm_t {
    const char *pchName;
    const uint8_t *pchSamples;
    uint32_t wCount;
    uint16_t hwSampleRateHz;
    uint16_t hwRepeatPauseMS;
} buzzer_pcm_t;

extern const buzzer_score_t c_tBuzzerScoreKeilKeygen;
extern const buzzer_pcm_t c_tBuzzerPcmKeilFc14;

void buzzer_test_init(void);
void buzzer_test_tone(uint16_t hwFreqHz, uint16_t hwDurationMS);
void buzzer_test_set_score(const buzzer_score_t *ptScore);
void buzzer_test_set_pcm(const buzzer_pcm_t *ptPcm);
void buzzer_test_task(void);

#endif
