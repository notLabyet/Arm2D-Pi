"""Compile production profiling arithmetic with clock/interrupt boundaries."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / '_compile_check/profile_test'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'platform/platform.c').read_text()
start = source.index('static platform_display_profile_t s_tDisplayProfile;')
end = source.index('void platform_init(void)', start)
code = source[start:end].replace('#if __DISP0_CFG_ENABLE_ASYNC_FLUSHING__', '').replace('#endif', '')
stub = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include "display_profile.h"
static uint32_t now;
static bool masked, copied;
#define clk_sys 0
static uint32_t time_us_32(void) { return now; }
static uint32_t clock_get_hz(int clock) { (void)clock; return 250000000; }
static uint32_t save_and_disable_interrupts(void) { assert(!masked); masked=true; return 7; }
static void restore_interrupts(uint32_t state) { assert(masked && state==7);masked=false; }
static void st7789_draw_bitmap_async(int16_t x,int16_t y,int16_t w,int16_t h,const uint8_t *p) {
    (void)x;(void)y;(void)w;(void)h;(void)p;now+=100;
}
static void disp_adapter0_insert_async_flushing_complete_event_handler(void);
'''
test = r'''
static void disp_adapter0_insert_async_flushing_complete_event_handler(void) {
    assert(s_wProfileFlushUs==400);copied=true;
    /* A completion callback may start another transfer immediately. */
    __disp_adapter0_request_async_flushing(NULL,false,0,0,10,10,NULL);
}
int main(void) {
    now=UINT32_MAX-100;
    __disp_adapter0_request_async_flushing(NULL,true,0,0,320,60,NULL);
    now+=300;
    st7789_insert_async_flush_cpl_evt_handler();
    assert(copied && s_wProfilePixels==19300 && s_wProfileBlocks==2);
    s_wProfileFlushUs=8000;
    s_wProfilePixels=76800;
    s_wProfileBlocks=8;
    s_wProfileWindowUs=1000;
    platform_display_profile_frame_begin(990000);
    now=1000;
    assert(platform_display_profile_timestamp()==1000);
    platform_display_profile_section(DISPLAY_PROFILE_CACHE,600);
    platform_display_profile_section(DISPLAY_PROFILE_CLOUD,400);
    platform_display_profile_task_time(2000);
    platform_display_profile_task_time(3000);
    platform_display_profile_frame_end(1000000);
    assert(platform_display_profile_get().sequence==0);
    platform_display_profile_frame_begin(1010000);
    platform_display_profile_task_time(7000);
    platform_display_profile_frame_end(1020000);
    platform_display_profile_t result=platform_display_profile_get();
    assert(result.sequence==1 && result.clock_hz==250000000);
    assert(result.frame_us==10000 && result.task_us==6000 && result.flush_us==4000);
    assert(result.section_us[DISPLAY_PROFILE_CACHE]==200);
    assert(result.section_us[DISPLAY_PROFILE_CLOUD]==300);
    assert(result.section_us[DISPLAY_PROFILE_FAR]==0 && s_wProfileSectionUs[0]==0);
    assert(result.pixels==38400 && result.blocks==4 && !masked);
    assert(!s_wProfileFrames && !s_wProfileTaskUs && !s_wProfilePixels && !s_wProfileFlushUs);
    s_wProfileWindowUs=UINT32_MAX-999999;
    platform_display_profile_frame_begin(UINT32_MAX-49);
    platform_display_profile_frame_end(50);
    assert(platform_display_profile_get().frame_us==100);
    assert(platform_display_profile_get().sequence==2);
    puts("PASS: production profile averages, snapshot/reset, IRQ completion before chained callback, microsecond wrap");
}
'''
p = out / 'profile.c'
p.write_text(stub + code + test)
exe = out / 'profile.exe'
subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                '-I',str(root/'platform'),str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
