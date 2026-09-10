/****************************************************************************
*  Copyright 2025 Gorgon Meducer (Email:embedded_zhuoran@hotmail.com)       *
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
#include "./platform.h"

#include "arm_2d.h"
#include "arm_2d_helper.h"
#include "arm_2d_disp_adapters.h"

#include "st7789_simple.h"
#include "display_profile.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/vreg_and_chip_reset.h"
#include "hardware/structs/vreg_and_chip_reset.h"
#include "hardware/sync.h"

/* Boot at the SDK default 125 MHz, then overclock after a short settling
 * delay so the external flash can complete its cold-start sequence.
 */
#ifndef RP2040_RUNTIME_OVERCLOCK_KHZ
#   define RP2040_RUNTIME_OVERCLOCK_KHZ      250000u
#endif

#ifndef RP2040_OVERCLOCK_STARTUP_DELAY_MS
#   define RP2040_OVERCLOCK_STARTUP_DELAY_MS 500u
#endif

#ifndef RP2040_OVERCLOCK_PLL_VCO_KHZ
#   define RP2040_OVERCLOCK_PLL_VCO_KHZ      1500000u
#endif

#ifndef RP2040_OVERCLOCK_PLL_POSTDIV1
#   define RP2040_OVERCLOCK_PLL_POSTDIV1     6u
#endif

#ifndef RP2040_OVERCLOCK_PLL_POSTDIV2
#   define RP2040_OVERCLOCK_PLL_POSTDIV2     1u
#endif

#ifndef RP2040_OVERCLOCK_VREG
#   define RP2040_OVERCLOCK_VREG             0xEu    /* 1.25 V */
#endif

#ifndef RP2040_OVERCLOCK_VREG_SETTLE_MS
#   define RP2040_OVERCLOCK_VREG_SETTLE_MS   20u
#endif

#if RP2040_RUNTIME_OVERCLOCK_KHZ
#   if RP2040_RUNTIME_OVERCLOCK_KHZ != (RP2040_OVERCLOCK_PLL_VCO_KHZ / (RP2040_OVERCLOCK_PLL_POSTDIV1 * RP2040_OVERCLOCK_PLL_POSTDIV2))
#       error RP2040_RUNTIME_OVERCLOCK_KHZ does not match the PLL VCO/post-divider settings.
#   endif
#endif

/*============================ MACROS ========================================*/
/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ LOCAL VARIABLES ===============================*/
/*============================ PROTOTYPES ====================================*/
/*============================ IMPLEMENTATION ================================*/

#if RP2040_RUNTIME_OVERCLOCK_KHZ
static void rp2040_set_vreg_voltage(uint32_t vsel)
{
    hw_write_masked(&vreg_and_chip_reset_hw->vreg,
                    vsel << VREG_AND_CHIP_RESET_VREG_VSEL_LSB,
                    VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
}

static void rp2040_set_sys_clock_overclock(void)
{
    const uint32_t wUsbClockHz = USB_CLK_KHZ * KHZ;
    const uint32_t wSysClockHz = RP2040_RUNTIME_OVERCLOCK_KHZ * KHZ;

    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    wUsbClockHz,
                    wUsbClockHz);

    pll_init(pll_sys,
             PLL_COMMON_REFDIV,
             RP2040_OVERCLOCK_PLL_VCO_KHZ * KHZ,
             RP2040_OVERCLOCK_PLL_POSTDIV1,
             RP2040_OVERCLOCK_PLL_POSTDIV2);

    clock_configure(clk_ref,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                    0,
                    XOSC_KHZ * KHZ,
                    XOSC_KHZ * KHZ);

    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    wSysClockHz,
                    wSysClockHz);

    clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    wSysClockHz,
                    wSysClockHz);
}
#endif

void SysTick_Handler(void)
{

}

void Disp0_DrawBitmap(  int16_t x, 
                        int16_t y, 
                        int16_t width, 
                        int16_t height, 
                        const uint8_t *pchBitmap)
{
    st7789_draw_bitmap(x, y, width, height, pchBitmap);
}

static platform_display_profile_t s_tDisplayProfile;
static uint32_t s_wProfileWindowUs, s_wProfileFrameStartUs;
static uint32_t s_wProfileFrames, s_wProfileFrameUs, s_wProfileTaskUs;
static volatile uint32_t s_wProfileFlushUs, s_wProfilePixels, s_wProfileBlocks;
static uint32_t s_wProfileFlushStartUs;
static uint32_t s_wProfileSectionUs[DISPLAY_PROFILE_SECTION_COUNT];

uint32_t platform_display_profile_timestamp(void)
{
    return time_us_32();
}

void platform_display_profile_section(unsigned section, uint32_t start_us)
{
    assert(section < DISPLAY_PROFILE_SECTION_COUNT);
    s_wProfileSectionUs[section] += time_us_32() - start_us;
}

void platform_display_profile_frame_begin(uint32_t now_us)
{
    s_wProfileFrameStartUs = now_us;
}

void platform_display_profile_task_time(uint32_t elapsed_us)
{
    s_wProfileTaskUs += elapsed_us;
}

void platform_display_profile_frame_end(uint32_t now_us)
{
    s_wProfileFrameUs += now_us - s_wProfileFrameStartUs;
    s_wProfileFrames++;
    if (now_us - s_wProfileWindowUs < 1000000u) {
        return;
    }
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t flush_us = s_wProfileFlushUs;
    uint32_t pixels = s_wProfilePixels;
    uint32_t blocks = s_wProfileBlocks;
    s_wProfileFlushUs = s_wProfilePixels = s_wProfileBlocks = 0;
    restore_interrupts(irq_state);

    s_tDisplayProfile.sequence++;
    s_tDisplayProfile.clock_hz = clock_get_hz(clk_sys);
    s_tDisplayProfile.frame_us = s_wProfileFrameUs / s_wProfileFrames;
    s_tDisplayProfile.task_us = s_wProfileTaskUs / s_wProfileFrames;
    s_tDisplayProfile.flush_us = flush_us / s_wProfileFrames;
    s_tDisplayProfile.pixels = pixels / s_wProfileFrames;
    s_tDisplayProfile.blocks = blocks / s_wProfileFrames;
    for (unsigned n = 0; n < DISPLAY_PROFILE_SECTION_COUNT; n++) {
        s_tDisplayProfile.section_us[n] = s_wProfileSectionUs[n] / s_wProfileFrames;
        s_wProfileSectionUs[n] = 0;
    }
    s_wProfileFrames = s_wProfileFrameUs = s_wProfileTaskUs = 0;
    s_wProfileWindowUs = now_us;
}

platform_display_profile_t platform_display_profile_get(void)
{
    return s_tDisplayProfile;
}

#if __DISP0_CFG_ENABLE_ASYNC_FLUSHING__
void __disp_adapter0_request_async_flushing(void *pTarget,
                                            bool bIsNewFrame,
                                            int16_t iX, 
                                            int16_t iY,
                                            int16_t iWidth,
                                            int16_t iHeight,
                                            const uint16_t *phwBuffer)
{
    s_wProfileFlushStartUs = time_us_32();
    s_wProfilePixels += (uint32_t)iWidth * (uint32_t)iHeight;
    s_wProfileBlocks++;
    st7789_draw_bitmap_async(iX, iY, iWidth, iHeight, (const uint8_t *)phwBuffer);
}

void st7789_insert_async_flush_cpl_evt_handler(void)
{
    /* End before the callback, which may immediately launch another block. */
    s_wProfileFlushUs += time_us_32() - s_wProfileFlushStartUs;
    disp_adapter0_insert_async_flushing_complete_event_handler();
}
#endif

void platform_init(void)
{
    clocks_init();
    extern uint32_t SystemCoreClock;

#if RP2040_RUNTIME_OVERCLOCK_KHZ
    busy_wait_ms(RP2040_OVERCLOCK_STARTUP_DELAY_MS);
    rp2040_set_vreg_voltage(RP2040_OVERCLOCK_VREG);
    busy_wait_ms(RP2040_OVERCLOCK_VREG_SETTLE_MS);
    rp2040_set_sys_clock_overclock();
#endif

    SystemCoreClock = clock_get_hz(clk_sys);
    
	
    /*! \note if you do want to use SysTick in your application, please use 
     *!       init_cycle_counter(true); 
     *!       instead of 
     *!       init_cycle_counter(false); 
     */
    init_cycle_counter(false);

#if defined(RTE_Compiler_EventRecorder) || defined(RTE_CMSIS_View_EventRecorder)
    EventRecorderInitialize(0, 1);
#endif
    stdio_init_all();

    st7789_init();
}
