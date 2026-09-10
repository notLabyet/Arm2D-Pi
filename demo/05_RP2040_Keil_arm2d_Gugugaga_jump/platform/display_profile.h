#ifndef PLATFORM_DISPLAY_PROFILE_H
#define PLATFORM_DISPLAY_PROFILE_H

#include <stdint.h>

enum {
    DISPLAY_PROFILE_CACHE,
    DISPLAY_PROFILE_CLOUD,
    DISPLAY_PROFILE_FAR,
    DISPLAY_PROFILE_SPRITES,
    DISPLAY_PROFILE_SECTION_COUNT,
};

typedef struct platform_display_profile_t {
    uint32_t sequence;
    uint32_t clock_hz;
    uint32_t frame_us;
    uint32_t task_us;
    uint32_t flush_us;
    uint32_t pixels;
    uint32_t blocks;
    uint32_t section_us[DISPLAY_PROFILE_SECTION_COUNT];
} platform_display_profile_t;

/* Main-loop only. Task time includes any interrupt time inside the call.
 * Flush and task times overlap and must not be added together. */
void platform_display_profile_frame_begin(uint32_t now_us);
void platform_display_profile_task_time(uint32_t elapsed_us);
void platform_display_profile_frame_end(uint32_t now_us);
platform_display_profile_t platform_display_profile_get(void);
uint32_t platform_display_profile_timestamp(void);
void platform_display_profile_section(unsigned section, uint32_t start_us);

#endif
