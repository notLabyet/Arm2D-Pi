#include "light_task.h"

#include <stdbool.h>
#include <stdio.h>

#include "drv_light.h"
#include "pico/stdlib.h"

static bool s_bInited;
static uint32_t s_wLastRunMS;

void light_task_init(void)
{
    drv_light_init();
    s_bInited = true;
    s_wLastRunMS = 0u;

    printf("LIGHT task: GPIO%u/ADC%u, pullup=%luohm, GL5528 R10=%luohm gamma=%u.%02u\r\n",
           (unsigned)DRV_LIGHT_ADC_GPIO,
           (unsigned)DRV_LIGHT_ADC_INPUT,
           (unsigned long)DRV_LIGHT_PULLUP_OHM,
           (unsigned long)DRV_LIGHT_GL5528_R10_OHM,
           (unsigned)((uint32_t)(DRV_LIGHT_GL5528_GAMMA * 100.0f + 0.5f) / 100u),
           (unsigned)((uint32_t)(DRV_LIGHT_GL5528_GAMMA * 100.0f + 0.5f) % 100u));
}

void light_task(uint32_t wPeriodMS)
{
    uint32_t wNowMS = to_ms_since_boot(get_absolute_time());
    drv_light_sample_t tSample;

    if (!s_bInited) {
        light_task_init();
    }

    if (wPeriodMS == 0u) {
        wPeriodMS = LIGHT_TASK_INTERVAL_MS;
    }

    if ((uint32_t)(wNowMS - s_wLastRunMS) < wPeriodMS) {
        return;
    }
    s_wLastRunMS = wNowMS;

    tSample = drv_light_read(DRV_LIGHT_DEFAULT_SAMPLE_COUNT);
    if (tSample.wLdrOhm == UINT32_MAX) {
        printf("LIGHT: raw=%u adc=%lumV r_ldr=>%luohm lux=%lu.%02lu\r\n",
               (unsigned)tSample.hwRaw,
               (unsigned long)tSample.wMilliVolt,
               (unsigned long)DRV_LIGHT_PULLUP_OHM * 1000u,
               (unsigned long)(tSample.wLuxX100 / 100u),
               (unsigned long)(tSample.wLuxX100 % 100u));
    } else {
        printf("LIGHT: raw=%u adc=%lumV r_ldr=%luohm lux=%lu.%02lu\r\n",
               (unsigned)tSample.hwRaw,
               (unsigned long)tSample.wMilliVolt,
               (unsigned long)tSample.wLdrOhm,
               (unsigned long)(tSample.wLuxX100 / 100u),
               (unsigned long)(tSample.wLuxX100 % 100u));
    }
}
