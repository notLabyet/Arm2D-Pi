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

#include <stdio.h>

#include "arm_2d.h"
#include "arm_2d_helper.h"
#include "arm_2d_disp_adapters.h"
#include "arm_2d_scenes.h"
#include "arm_2d_scene_radars.h"

#ifdef RTE_Acceleration_Arm_2D_Extra_Benchmark
#   include "arm_2d_benchmark.h"
#endif


#include "qmi8658c_task.h"
#include "bm8563_task.h"
#include "drv_paj7620.h"
#include "hardware/pwm.h"
#include "usb_mouse.h"
#include "usb_msc_sd.h"
#include "hardware/clocks.h"
#include "tufty_sdcard.h"
#include "tufty_qoi_scene.h"
#include "tufty_lmsk_scene.h"
#include "ir_test.h"
#include "light_test.h"
#include "buzzer_test.h"
/*============================ MACROS ========================================*/
#ifndef TUFTY_SDCARD_RUN_PERF_TEST
#   define TUFTY_SDCARD_RUN_PERF_TEST 0
#endif

#ifndef TUFTY_IR_TEST_ENABLE
#   define TUFTY_IR_TEST_ENABLE 0
#endif

#ifndef TUFTY_LIGHT_TEST_ENABLE
#   define TUFTY_LIGHT_TEST_ENABLE 1
#endif

#ifndef TUFTY_IMU_SAMPLE_ENABLE
#   define TUFTY_IMU_SAMPLE_ENABLE 0
#endif

#ifndef TUFTY_BUZZER_TEST_ENABLE
#   define TUFTY_BUZZER_TEST_ENABLE 1
#endif
/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ LOCAL VARIABLES ===============================*/
/*============================ PROTOTYPES ====================================*/
/*============================ IMPLEMENTATION ================================*/

void scene_radars_loader(void)
{
    arm_2d_scene_radars_init(&DISP0_ADAPTER);
}





typedef struct demo_scene_t {
    int32_t nLastInMS;
    void (*fnLoader)(void);
} demo_scene_t;

static demo_scene_t const c_SceneLoaders[] = {

#if 0
    {
        20000,
        scene_radars_loader,
    },
    {
        20000,
        scene_blink_loader,
    },
    {
        20000,
        scene_space_badge_loader,
    },
#else
    {
        .fnLoader = 
        tufty_qoi_scene_loader,
        //tufty_lmsk_scene_loader,
        //scene_large_lmsk_loader,
        //scene_qoi_animation_loader,
        //scene_lmsk_loader,
        //scene_radars_loader,
        //scene_qoi_loader
        //scene_zhrgb565_loader,
        //scene_bubble_charging_loader,
        //scene_watch_face_01_loader,
        //scene_waveform_loader,
        //scene_mask_generator_loader,
        //scene_ring_indicator_loader,
        //scene_meter_loader,
        //scene_histogram_loader,
        //scene_blink_loader,
        //scene_histogram_loader,
        //scene_flight_attitude_instrument_loader,
        //scene_radars_loader,
        //scene_music_player_loader,
        //scene_meter_loader,
        //scene_rickrolling_loader,
        //scene_space_badge_loader,
        //scene_qrcode_loader,
        //scene_mono_clock_loader
    },
#endif

};

static
struct {
    int8_t chIndex;
    bool bIsTimeout;
    int32_t nDelay;
    int64_t lTimeStamp;
    
} s_tDemoCTRL = {
    .chIndex = -1,
    .bIsTimeout = true,
};

/* load scene one by one */
void before_scene_switching_handler(void *pTarget,
                                    arm_2d_scene_player_t *ptPlayer,
                                    arm_2d_scene_t *ptScene)
{

    switch (arm_2d_scene_player_get_switching_status(&DISP0_ADAPTER)) {
        case ARM_2D_SCENE_SWITCH_STATUS_MANUAL_CANCEL:
            s_tDemoCTRL.chIndex--;
            break;
        default:
            s_tDemoCTRL.chIndex++;
            break;
    }

    if (s_tDemoCTRL.chIndex >= dimof(c_SceneLoaders)) {
        s_tDemoCTRL.chIndex = 0;
    } else if (s_tDemoCTRL.chIndex < 0) {
        s_tDemoCTRL.chIndex += dimof(c_SceneLoaders);
    }

    /* call loader */
    arm_with(const demo_scene_t, &c_SceneLoaders[s_tDemoCTRL.chIndex]) {
        if (_->nLastInMS > 0) {
            s_tDemoCTRL.bIsTimeout = false;
            s_tDemoCTRL.lTimeStamp = 0;
            s_tDemoCTRL.nDelay = _->nLastInMS;
        }
        _->fnLoader();
    }
}


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
int16_t DATA_GY_ACC[6];
#define POWER_KEEP_PIN 2
#define POWER_UP_CHECK_PIN 9
extern fsm_rt_t power_task();
int main(void) 
{
    uint32_t wLastIMUSampleMS = 0;
	
    system_init();
#if TUFTY_SDCARD_RUN_PERF_TEST
    printf("\r\nTufty2040 SDIO/FatFs performance test start\r\n");
    (void)tufty_sdcard_default_perf_test();
#endif

    (void)usb_msc_sd_init();

    usb_mouse_init();
    usb_mouse_startup_poll(500u);

    __cycleof__("printf") {
        printf("Hello Tufty2040!\r\n");
		printf("clk_sys = %d\r\n",clock_get_hz(clk_sys));		
		printf("clk_usb = %d\r\n",clock_get_hz(clk_usb));
    }

#if defined( __PERF_COUNTER_COREMARK__ ) && __PERF_COUNTER_COREMARK__
    printf("\r\nRun Coremark 1.0...\r\n");
    coremark_main();
#endif
#ifdef RTE_Acceleration_Arm_2D_Extra_Benchmark
    arm_2d_run_benchmark();
#else
    arm_2d_scene_player_register_before_switching_event_handler(
            &DISP0_ADAPTER,
            before_scene_switching_handler);
            
    arm_2d_scene_player_switch_to_next_scene(&DISP0_ADAPTER);
#endif
	
	
	qmi8658_init_ret = qmi8658c_init();
	sleep_ms(10);

	bm8563_hander_init();
#if TUFTY_IR_TEST_ENABLE
    ir_test_init(IR_TEST_CARRIER_HZ, IR_TEST_DUTY_PERMILLE);
#endif
#if TUFTY_LIGHT_TEST_ENABLE
    light_test_init();
#endif
#if TUFTY_BUZZER_TEST_ENABLE
    buzzer_test_init();
#endif

    while (true) {
        uint32_t const wNow = get_system_ms();

		if(TUFTY_IMU_SAMPLE_ENABLE && qmi8658_init_ret && ((uint32_t)(wNow - wLastIMUSampleMS) >= 10)){
            wLastIMUSampleMS = wNow;
			QMI8658A_ReadData(DATA_GY_ACC);
//            usb_mouse_update_imu_raw(DATA_GY_ACC);
		}
//		bm8563_read(&tbm8563, &bm_time);
		power_task();
//        usb_mouse_task();
#if TUFTY_IR_TEST_ENABLE
        ir_test_task();
#endif
#if TUFTY_LIGHT_TEST_ENABLE
        light_test_task();
#endif
#if TUFTY_BUZZER_TEST_ENABLE
        buzzer_test_task();
#endif
        disp_adapter0_task(60);

        if (!s_tDemoCTRL.bIsTimeout) {

            if (arm_2d_helper_is_time_out(s_tDemoCTRL.nDelay, &s_tDemoCTRL.lTimeStamp)) {
                s_tDemoCTRL.bIsTimeout = true;

                arm_2d_scene_player_switch_to_next_scene(&DISP0_ADAPTER);
            }
        }

    }
    //return 0;
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
             * 上电后立即拉高保持电源，
             * 防止松开按键后系统掉�?             */
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
             * 按键低电平：说明是手动按键触发上�?             * 按键高电平：说明不是按键上电，关闭保持电源，等待按键
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
             * 等待按键按下，低电平持续20ms认为有效
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
             * 等待按键松开，高电平持续20ms认为松手有效
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
             * 系统已上电状态下，检测再次长按关�?             */
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
             * 拉低保持电源，系统掉�?             */
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
