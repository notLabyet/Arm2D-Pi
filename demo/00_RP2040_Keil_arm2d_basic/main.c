#include "platform/pi_platform.h"
#include "platform/st7789_simple.h"
#include "arm_2d.h"
#include "arm_2d_disp_adapter_0.h"

#include "service/ui/arm_2d_inlude.h"
#include "service/service.h"

int main(void)
{
    platform_init();
    st7789_set_backlight(false);

    service_init();

    arm_2d_init();
    disp_adapter0_init();
    arm_2d_scene_show_data_init(&DISP0_ADAPTER);

    /* 首帧写入 LCD 后再点亮背光，避免启动时出现未初始化画面。 */
    while (arm_fsm_rt_cpl != disp_adapter0_task()) {
    }
    st7789_set_backlight(true);

    for (;;)
    {
        service_task();
        disp_adapter0_task();
    }
}
