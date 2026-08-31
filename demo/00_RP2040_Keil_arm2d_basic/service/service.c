#include "service.h"

void service_init(void)
{
    /* 与 4 号工程一致：蜂鸣器先就绪，再初始化电源按键并播放开机音。 */
    service_sensor_init();
    service_buzzer_init();
    service_key_init();
    (void)service_buzzer_play_power_on_chime();
}

void service_task(void)
{
    /* 先采集数据和更新按键事件，再根据事件触发非阻塞提示音。 */
    service_sensor_task();
    service_key_task();
    if (service_key_was_long_pressed()) {
        (void)service_buzzer_play_power_off_chime();
        service_key_request_power_off();
    } else if (service_key_was_right_long_pressed()) {
        (void)service_buzzer_play_key_feedback(true);
    } else if (service_key_was_short_pressed()
            || service_key_was_right_short_pressed()) {
        (void)service_buzzer_play_key_feedback(false);
    }
    service_buzzer_task();
}
