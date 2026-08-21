#ifndef BUZZER_TASK_H
#define BUZZER_TASK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum app_alert_t {
	APP_ALERT_COUNTDOWN_FINISHED_DEFAULT,
	APP_ALERT_COUNTDOWN_FINISHED_BIRTHDAY,
} app_alert_t;

typedef void (*app_alert_handler_t)(app_alert_t tAlert, void *pTarget);

void buzzer_task_init(void);
void buzzer_task(uint32_t wNowMS);

void app_buzzer_init(void);
void app_buzzer_set_tone(uint16_t hwFrequencyHz);
void app_buzzer_beep(uint16_t hwFrequencyHz, uint16_t hwDurationMS);
bool app_buzzer_play_countdown_finished_chime(void);
bool app_buzzer_play_happy_birthday(void);
bool app_buzzer_play_power_key_feedback(bool bLongPress);
bool app_buzzer_play_power_key_double_press_feedback(void);
bool app_buzzer_play_power_keys_together_feedback(void);
bool app_buzzer_play_power_on_chime(void);
bool app_buzzer_play_power_off_chime(void);
bool app_buzzer_is_playing(void);
void app_alert_set_handler(app_alert_handler_t fnHandler, void *pTarget);
void app_alert_play(app_alert_t tAlert);

#endif
