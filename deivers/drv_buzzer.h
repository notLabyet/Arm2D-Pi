/**
 * @file drv_buzzer.h
 * @brief Passive buzzer driver for RP2040 using PWM and optional PCM playback.
 *
 * Hardware (Crystal_Mouse / RP2040 family):
 *   BEEF_EN -> GPIO23 -> series resistor -> passive magnetic buzzer (DET402-G-1).
 *   A passive buzzer needs a square wave or PWM tone; DC on/off does not produce sound.
 *
 * Features:
 *   - Steady tone: set frequency and duty (permille) on the buzzer PWM slice.
 *   - Score playback: timed sequence of (frequency, duration) notes; poll drv_buzzer_score_task().
 *   - PCM playback: 8-bit unsigned samples at a given rate; a second PWM slice fires
 *     wrap IRQs to update the carrier duty (AM-style) for simple audio.
 */

#ifndef DRV_BUZZER_H
#define DRV_BUZZER_H

#include <stdbool.h>
#include <stdint.h>

/** GPIO index driving the buzzer enable path (override before including if board differs). */
#ifndef DRV_BUZZER_PIN
#   define DRV_BUZZER_PIN                 23u
#endif

/** Default tone duty cycle in parts per thousand (0–1000). 500 ≈ 50% for audible fundamental. */
#ifndef DRV_BUZZER_DUTY_PERMILLE
#   define DRV_BUZZER_DUTY_PERMILLE       500u
#endif

/** PWM carrier frequency used during PCM playback (Hz); higher reduces audible PWM whistle. */
#ifndef DRV_BUZZER_PCM_CARRIER_HZ
#   define DRV_BUZZER_PCM_CARRIER_HZ      20000u
#endif

/**
 * PWM slice index used only as a periodic interrupt source for PCM sample stepping.
 * Must not be the same slice as DRV_BUZZER_PIN unless you know the pin mapping;
 * default 7 is chosen to avoid clashing with GPIO23’s slice on RP2040.
 */
#ifndef DRV_BUZZER_PCM_CLOCK_SLICE
#   define DRV_BUZZER_PCM_CLOCK_SLICE     7u
#endif

/** Sentinel frequency in scores meaning “rest” (silence for the note duration). */
#define DRV_BUZZER_REST                   0u

/** One musical step: output frequency and how long to hold it (milliseconds). */
typedef struct drv_buzzer_note_t {
    uint16_t hwFreqHz;
    uint16_t hwDurationMS;
} drv_buzzer_note_t;

/** Named melody: pointer to note array and number of notes. */
typedef struct drv_buzzer_score_t {
    const char *pchName;
    const drv_buzzer_note_t *ptNotes;
    uint16_t hwCount;
} drv_buzzer_score_t;

/**
 * Raw PCM clip: unsigned 8-bit samples, sample count, and playback sample rate.
 * Samples modulate the buzzer PWM level in the PCM IRQ handler.
 */
typedef struct drv_buzzer_pcm_t {
    const char *pchName;
    const uint8_t *pchSamples;
    uint32_t wCount;
    uint16_t hwSampleRateHz;
} drv_buzzer_pcm_t;

/** Reserve PWM/GPIO for the buzzer and leave output silent until a tone is requested. */
void drv_buzzer_init(void);

/** Stop PCM, score state, and force GPIO low (no PWM). */
void drv_buzzer_stop(void);

/**
 * Output a steady square wave at hwFreqHz with given duty (permille).
 * Stops any active PCM playback. hwFreqHz == 0 silences and returns pin to GPIO low.
 */
void drv_buzzer_set_tone(uint16_t hwFreqHz, uint16_t hwDutyPermille);

/** Begin score playback from the first note; returns false if ptScore is invalid. */
bool drv_buzzer_score_start(const drv_buzzer_score_t *ptScore);

/**
 * Advance score timing; call from main loop while a score is active.
 * Uses to_ms_since_boot() for scheduling. Returns false when the score has finished.
 */
bool drv_buzzer_score_task(void);

/** True while a score sequence is in progress (not yet finished). */
bool drv_buzzer_score_is_active(void);

/** Start PCM playback; returns false if descriptor or parameters are invalid. */
bool drv_buzzer_pcm_start(const drv_buzzer_pcm_t *ptPcm);

/** Halt PCM IRQ and PWM updates; leaves buzzer channel at zero level. */
void drv_buzzer_pcm_stop(void);

/** True while PCM IRQ-driven playback is running. */
bool drv_buzzer_pcm_is_active(void);

/** Current sample index during PCM (volatile in implementation; for diagnostics/UI). */
uint32_t drv_buzzer_pcm_get_index(void);

#endif
