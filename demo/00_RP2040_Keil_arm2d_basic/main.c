#include "platform/pi_platform.h"
#include "arm_2d.h"
#include "arm_2d_disp_adapter_0.h"

int main(void)
{
    platform_init();
    arm_2d_init();
    disp_adapter0_init();

    while (true) {
        disp_adapter0_task();
    }
}

