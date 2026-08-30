#include "service_buzzer.h"

#include "drv_buzzer.h"
#include "pico/stdlib.h"

/* 定时单音在主循环中到期后关闭，无需阻塞显示刷新。 */
static uint32_t s_wStopMS;
static bool s_bTimedTone;

/* 复用 4 号工程的按键反馈音高和时长。 */
static const drv_buzzer_note_t c_tShortPressNotes[] = {
    {1200u, 100u},
};

static const drv_buzzer_note_t c_tLongPressNotes[] = {
    {1600u, 100u},
};

static const drv_buzzer_score_t c_tShortPressScore = {
    "Key short press",
    c_tShortPressNotes,
    1u,
};

static const drv_buzzer_score_t c_tLongPressScore = {
    "Key long press",
    c_tLongPressNotes,
    1u,
};

/* 与 4 号工程一致的开机 C5-E5-G5 和关机 G5-E5-C5 提示音。 */
static const drv_buzzer_note_t c_tPowerOnNotes[] = {
    {523u, 60u},
    {DRV_BUZZER_REST, 40u},
    {659u, 60u},
    {DRV_BUZZER_REST, 40u},
    {784u, 100u},
};

static const drv_buzzer_note_t c_tPowerOffNotes[] = {
    {784u, 100u},
    {DRV_BUZZER_REST, 40u},
    {659u, 60u},
    {DRV_BUZZER_REST, 40u},
    {523u, 60u},
};

static const drv_buzzer_score_t c_tPowerOnScore = {
    "Power on",
    c_tPowerOnNotes,
    5u,
};

static const drv_buzzer_score_t c_tPowerOffScore = {
    "Power off",
    c_tPowerOffNotes,
    5u,
};

void service_buzzer_init(void)
{
    drv_buzzer_init();
    s_bTimedTone = false;
}

void service_buzzer_task(void)
{
    uint32_t wNowMS = to_ms_since_boot(get_absolute_time());

    /* 定时单音和乐谱均由主循环推进。 */
    if (s_bTimedTone && ((int32_t)(wNowMS - s_wStopMS) >= 0)) {
        drv_buzzer_stop();
        s_bTimedTone = false;
    }

    if (drv_buzzer_score_is_active()) {
        (void)drv_buzzer_score_task();
    }
}

void service_buzzer_beep(uint16_t hwFrequencyHz, uint16_t hwDurationMS)
{
    if ((0u == hwFrequencyHz) || (0u == hwDurationMS)) {
        drv_buzzer_stop();
        s_bTimedTone = false;
        return;
    }

    drv_buzzer_set_tone(hwFrequencyHz, DRV_BUZZER_DUTY_PERMILLE);
    s_wStopMS = to_ms_since_boot(get_absolute_time()) + hwDurationMS;
    s_bTimedTone = true;
}

bool service_buzzer_play_key_feedback(bool bLongPress)
{
    s_bTimedTone = false;
    /* 新的按键提示音会替换当前正在播放的乐谱。 */
    return drv_buzzer_score_start(bLongPress
                                  ? &c_tLongPressScore
                                  : &c_tShortPressScore);
}

bool service_buzzer_play_power_on_chime(void)
{
    s_bTimedTone = false;
    return drv_buzzer_score_start(&c_tPowerOnScore);
}

bool service_buzzer_play_power_off_chime(void)
{
    s_bTimedTone = false;
    return drv_buzzer_score_start(&c_tPowerOffScore);
}

bool service_buzzer_is_playing(void)
{
    return s_bTimedTone || drv_buzzer_score_is_active();
}