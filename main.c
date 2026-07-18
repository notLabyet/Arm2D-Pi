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
#include "arm_2d_scene_benchmark_generic.h"
#include <stdio.h>
#include "arm_2d.h"
#include "arm_2d_helper.h"
#include "arm_2d_disp_adapters.h"
#include "arm_2d_scenes.h"
#include "drv_QMI8658.h"
#include "bm8563_task.h"
#include "drv_paj7620.h"
#include "qmi8658c_task.h"
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
#   define RP2040_IR_TASK_ENABLE 1
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
#   define RP2040_USB_MSC_SD_ENABLE 1
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
int16_t DATA_GY_ACC_RAW[6];
float DATA_GY_ACC[6];
#define POWER_KEEP_PIN 2
#define POWER_UP_CHECK_PIN 9
extern fsm_rt_t power_task();
int main(void)
{
    uint32_t wLastIMUSampleMS = 0;

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

    usb_mouse_init();
    //usb_mouse_startup_poll(500u);

    __cycleof__("printf") {
        printf("Hello RP2040!\r\n");
		printf("clk_sys = %d\r\n",clock_get_hz(clk_sys));
		printf("clk_usb = %d\r\n",clock_get_hz(clk_usb));
    }
	qmi8658_init_ret = qmi8658c_init();
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
    arm_2d_scene0_init(&DISP0_ADAPTER);
    wLastIMUSampleMS = get_system_ms();

    while (true) {
        uint32_t const wNow = get_system_ms();
#if RP2040_IMU_SAMPLE_ENABLE
        if (qmi8658_init_ret &&
            ((uint32_t)(wNow - wLastIMUSampleMS) >= 20)) {
            wLastIMUSampleMS = wNow;
            QMI8658A_ReadData(DATA_GY_ACC_RAW);
        }
#endif
//		bm8563_read(&tbm8563, &bm_time);
		power_task();
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
        disp_adapter0_task(60);
    }
}
fsm_rt_t power_task(void)
{
    static uint32_t delay = 0;
    static uint8_t chState = 0;

    enum {
        START = 0,
        POWER_SOURCE_CHECK,
        WAIT_POWER_UP_KEY,
        RELEASE_CHECK,
        PUSH_CHECK,
        POWER_OFF
    };

    uint32_t now = get_system_ms();

    switch (chState) {

        case START:
        {
            gpio_init(POWER_KEEP_PIN);
            gpio_set_function(POWER_KEEP_PIN, GPIO_FUNC_SIO);
            gpio_set_dir(POWER_KEEP_PIN, GPIO_OUT);

            /*
             * Assert the power-hold pin immediately after boot so the system
             * stays powered after the button is released.
             */
            gpio_put(POWER_KEEP_PIN, 1);

            gpio_init(POWER_UP_CHECK_PIN);
            gpio_set_function(POWER_UP_CHECK_PIN, GPIO_FUNC_SIO);
            gpio_set_dir(POWER_UP_CHECK_PIN, GPIO_IN);
            gpio_pull_up(POWER_UP_CHECK_PIN);

            delay = now;
            chState = POWER_SOURCE_CHECK;
            break;
        }

        case POWER_SOURCE_CHECK:
        {
            /*
             * Low level: boot was triggered by the power button.
             * High level: boot was not button-triggered; release power-hold
             * and wait for a valid button press.
             */
            if (gpio_get(POWER_UP_CHECK_PIN) == 0) {
                delay = now;
                chState = RELEASE_CHECK;
            } else {
                gpio_put(POWER_KEEP_PIN, 0);
                delay = now;
                chState = WAIT_POWER_UP_KEY;
            }
            break;
        }

        case WAIT_POWER_UP_KEY:
        {
            /*
             * Wait for the button to be pressed. A low level lasting 20 ms is
             * treated as a valid press.
             */
            if (gpio_get(POWER_UP_CHECK_PIN) == 0) {
                if ((uint32_t)(now - delay) >= 20) {
                    gpio_put(POWER_KEEP_PIN, 1);
                    delay = now;
                    chState = RELEASE_CHECK;
                }
            } else {
                delay = now;
            }
            break;
        }

        case RELEASE_CHECK:
        {
            /*
             * Wait for the button to be released. A high level lasting 20 ms is
             * treated as a valid release.
             */
            if (gpio_get(POWER_UP_CHECK_PIN) == 1) {
                if ((uint32_t)(now - delay) >= 20) {
                    delay = now;
                    chState = PUSH_CHECK;
                }
            } else {
                delay = now;
            }
            break;
        }

        case PUSH_CHECK:
        {
            /*
             * While the system is powered, detect another long press to power
             * off.
             */
            if (gpio_get(POWER_UP_CHECK_PIN) == 0) {
                if ((uint32_t)(now - delay) >= 1000) {
                    chState = POWER_OFF;
                }
            } else {
                delay = now;
            }
            break;
        }

        case POWER_OFF:
        {
            /*
             * Deassert the power-hold pin to cut system power.
             */
            gpio_put(POWER_KEEP_PIN, 0);
            break;
        }

        default:
        {
            chState = START;
            break;
        }
    }

    return fsm_rt_on_going;
}
