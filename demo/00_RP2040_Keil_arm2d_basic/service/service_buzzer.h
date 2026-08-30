#ifndef __SERVICE_BUZZER_H__
#define __SERVICE_BUZZER_H__

#include <stdbool.h>
#include <stdint.h>

void service_buzzer_init(void);
void service_buzzer_task(void);
void service_buzzer_beep(uint16_t hwFrequencyHz, uint16_t hwDurationMS);
bool service_buzzer_play_key_feedback(bool bLongPress);
bool service_buzzer_play_power_on_chime(void);
bool service_buzzer_play_power_off_chime(void);
bool service_buzzer_is_playing(void);

#endif