#ifndef POWER_TASK_H
#define POWER_TASK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct app_button_t {
	uint8_t chPin;
	bool bActiveLow;
	bool bPressed;
	bool bCandidatePressed;
	bool bPressLatched;
	bool bReleaseLatched;
	bool bLongPressLatched;
	bool bLongPressTriggered;
	uint32_t wCandidateSinceMS;
	uint32_t wPressedSinceMS;
	uint32_t wDebounceMS;
} app_button_t;

typedef enum app_power_key_event_t {
	APP_POWER_KEY_EVENT_LEFT_SHORT_PRESS = 0,
	APP_POWER_KEY_EVENT_RIGHT_SHORT_PRESS,
	APP_POWER_KEY_EVENT_RIGHT_LONG_PRESS,
	APP_POWER_KEY_EVENT_BOTH_PRESSED,
} app_power_key_event_t;

void power_task_init(void);
void power_task(uint32_t wNowMS);

void app_button_init(app_button_t *ptButton,
					 uint8_t chPin,
					 bool bActiveLow,
					 uint32_t wDebounceMS);
void app_button_poll(app_button_t *ptButton, uint32_t wNowMS);
bool app_button_is_pressed(const app_button_t *ptButton);
bool app_button_was_pressed(app_button_t *ptButton);
bool app_button_was_released(app_button_t *ptButton);
bool app_button_was_long_pressed(app_button_t *ptButton);
bool app_power_keys_were_pressed_together(void);
bool app_power_key_was_event(app_power_key_event_t tEvent);
bool app_power_key_was_active(void);
void app_power_key_init(void);
void app_power_key_task(uint32_t wNowMS);

#endif
