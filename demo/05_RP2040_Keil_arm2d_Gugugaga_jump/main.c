/****************************************************************************
*  Copyright 2021 Gorgon Meducer (Email:embedded_zhuoran@hotmail.com)       *
*                                                                           *
*  Licensed under the Apache License, Version 2.0 (the "License");          *
*  you may not use this file except in compliance with the License.         *
*  You may obtain a copy of the License at                                  *
*                                                                           *
*     http://www.apache.org/licenses/LICENSE-2.0                            *
*                                                                           *
*  Unless required by applicable law or agreed to in writing, software      *
*  distributed under the License is distributed on an "AS IS" BASIS,        *
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
*  See the License for the specific language governing permissions and      *
*  limitations under the License.                                           *
*                                                                           *
****************************************************************************/
/*============================ INCLUDES ======================================*/
#include "platform/platform.h"
#include "platform/display_profile.h"
#include "arm_2d_scene_benchmark_generic.h"
#include "arm_2d_scene_infinite_corridor.h"
#include "arm_2d_scene_platformer.h"
#include <stdio.h>
#include "arm_2d.h"
#include "arm_2d_helper.h"
#include "arm_2d_disp_adapters.h"
#include "arm_2d_scenes.h"
#include "qmi8658c_task.h"
#include "qmi8658_motion.h"
#include "power_key_service.h"
#include "bm8563_task.h"
#include "drv_paj7620.h"
#include "hardware/pwm.h"
#include "usb_mouse.h"
#include "usb_msc_sd.h"
#include "hardware/clocks.h"
#include "rp2040_sdcard.h"
#include "fal.h"
#include "ir_task.h"
#include "light_task.h"
#include "buzzer_task.h"
/*============================ MACROS ========================================*/
#ifndef RP2040_SDCARD_RUN_PERF_TEST
#   define RP2040_SDCARD_RUN_PERF_TEST 0
#endif

#ifndef RP2040_IR_TASK_ENABLE
#   define RP2040_IR_TASK_ENABLE 0
#endif

#ifndef RP2040_LIGHT_TASK_ENABLE
#   define RP2040_LIGHT_TASK_ENABLE 0
#endif

#ifndef RP2040_IMU_SAMPLE_ENABLE
#   define RP2040_IMU_SAMPLE_ENABLE 1
#endif

#ifndef RP2040_BUZZER_TASK_ENABLE
#   define RP2040_BUZZER_TASK_ENABLE 0
#endif

#ifndef RP2040_USB_MSC_SD_ENABLE
#   define RP2040_USB_MSC_SD_ENABLE 0
#endif
/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ LOCAL VARIABLES ===============================*/
/*============================ PROTOTYPES ====================================*/
/*============================ IMPLEMENTATION ================================*/

static void system_init(void)
{
    platform_init();

    arm_2d_init();
    disp_adapter0_init();

}

static void usb_mouse_startup_poll(uint32_t delay_ms)
{
    uint32_t const start = get_system_ms();

    while ((uint32_t)(get_system_ms() - start) < delay_ms) {
        usb_mouse_task();
        sleep_ms(1);
    }
}

char qmi8658_init_ret;
int main(void) 
{
    system_init();
    (void)fal_init();
	sleep_ms(500);
#if RP2040_SDCARD_RUN_PERF_TEST
    printf("\r\nRP2040 SDIO/FatFs performance test start\r\n");
    (void)rp2040_sdcard_default_perf_test();
#endif

#if RP2040_USB_MSC_SD_ENABLE
    (void)usb_msc_sd_init();
#endif

//	usb_mouse_init();
//	usb_mouse_startup_poll(500u);

    __cycleof__("printf") {
        printf("Hello RP2040!\r\n");
		printf("clk_sys = %d\r\n",clock_get_hz(clk_sys));		
		printf("clk_usb = %d\r\n",clock_get_hz(clk_usb));
    }
	qmi8658_init_ret = qmi8658c_init();
    if (qmi8658_init_ret) {
        qmi8658_motion_init();
    }
	sleep_ms(10);
	bm8563_hander_init();
#if RP2040_IR_TASK_ENABLE
    ir_task_init();
#endif
#if RP2040_LIGHT_TASK_ENABLE
    light_task_init();
#endif
#if RP2040_BUZZER_TASK_ENABLE
    buzzer_task_init();

#endif
    bool bKeySampling = power_key_service_start_sampling();
    arm_2d_scene_platformer_init(&DISP0_ADAPTER);

    bool bDisplayBusy = false;
    uint32_t wNextFrameUs = time_us_32();
    while (true) {
        uint32_t const wNow = get_system_ms();
#if RP2040_IMU_SAMPLE_ENABLE
        /* Keep blocking I2C reads between frames, not between PFB strips. */
        if (qmi8658_init_ret && !bDisplayBusy) {
            (void)qmi8658_motion_poll(wNow);
        }
#endif
//		bm8563_read(&tbm8563, &bm_time);
        if (!bKeySampling) {
            power_key_service_poll(to_ms_since_boot(get_absolute_time()));
        }
#if RP2040_IR_TASK_ENABLE
        ir_task(IR_TASK_SEND_INTERVAL_MS);
#endif
#if RP2040_LIGHT_TASK_ENABLE
        light_task(LIGHT_TASK_INTERVAL_MS);
#endif
#if RP2040_BUZZER_TASK_ENABLE
        buzzer_task(BUZZER_TASK_REPEAT_PAUSE_MS);
#endif
#if RP2040_USB_MSC_SD_ENABLE
	    usb_mouse_task();
#endif
        uint32_t wNowUs = time_us_32();
        if (!bDisplayBusy && (int32_t)(wNowUs - wNextFrameUs) >= 0) {
            /* Start-to-start cadence; an over-budget frame adds no idle wait.
             * Avoid the adapter macro's integer 1000/60 == 16 ms rounding. */
            wNextFrameUs = wNowUs + 16667u;
            bDisplayBusy = true;
            platform_display_profile_frame_begin(wNowUs);
        }
        if (bDisplayBusy) {
            uint32_t wTaskStartUs = time_us_32();
            arm_fsm_rt_t tResult = __disp_adapter0_task();
            uint32_t wTaskEndUs = time_us_32();
            platform_display_profile_task_time(wTaskEndUs - wTaskStartUs);
            if (arm_fsm_rt_cpl == tResult) {
                platform_display_profile_frame_end(wTaskEndUs);
                bDisplayBusy = false;
            }
        }
    }
}
