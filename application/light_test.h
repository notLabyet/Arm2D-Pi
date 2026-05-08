#ifndef __LIGHT_TEST_H__
#define __LIGHT_TEST_H__

#include <stdint.h>

/*
 * Schematic: LIGHT_SENSE -> GPIO26_ADC0.
 * Divider: +3V3 -- R6 10k -- LIGHT_SENSE -- R9 GL5528 -- GND,
 * with C20 100nF on LIGHT_SENSE.
 */
#ifndef LIGHT_TEST_ADC_GPIO
#   define LIGHT_TEST_ADC_GPIO              26u
#endif

#ifndef LIGHT_TEST_ADC_INPUT
#   define LIGHT_TEST_ADC_INPUT             0u
#endif

#ifndef LIGHT_TEST_VREF_MV
#   define LIGHT_TEST_VREF_MV               3300u
#endif

#ifndef LIGHT_TEST_PULLUP_OHM
#   define LIGHT_TEST_PULLUP_OHM            10000u
#endif

#ifndef LIGHT_TEST_SAMPLE_COUNT
#   define LIGHT_TEST_SAMPLE_COUNT          32u
#endif

#ifndef LIGHT_TEST_PRINT_INTERVAL_MS
#   define LIGHT_TEST_PRINT_INTERVAL_MS     500u
#endif

/*
 * Common GL5528 bins are roughly 10k-20k at 10 lux. Start with the
 * midpoint, then calibrate this macro with your measured reference lux.
 */
#ifndef LIGHT_TEST_GL5528_R10_OHM
#   define LIGHT_TEST_GL5528_R10_OHM        15000.0f
#endif

#ifndef LIGHT_TEST_GL5528_GAMMA
#   define LIGHT_TEST_GL5528_GAMMA          0.70f
#endif

typedef struct light_test_sample_t {
    uint16_t hwRaw;
    uint32_t wMilliVolt;
    uint32_t wLdrOhm;
    uint32_t wLuxX100;
} light_test_sample_t;

void light_test_init(void);
light_test_sample_t light_test_read(void);
void light_test_task(void);

#endif
