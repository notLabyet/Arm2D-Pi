#include "light_test.h"

#include <stdbool.h>
#include <stdio.h>

#include "hardware/adc.h"
#include "hardware/resets.h"
#include "pico/stdlib.h"

static bool s_bLightTestInited;
static uint32_t s_wLastPrintMS;

static uint32_t light_div_round_u64(uint64_t ullValue, uint32_t wDivisor)
{
    return (uint32_t)((ullValue + (uint64_t)(wDivisor / 2u)) / wDivisor);
}

static uint32_t light_ldr_ohm_from_mv(uint32_t wMilliVolt)
{
    if (wMilliVolt >= LIGHT_TEST_VREF_MV) {
        return UINT32_MAX;
    }

    if (wMilliVolt == 0u) {
        return 0u;
    }

    return light_div_round_u64((uint64_t)LIGHT_TEST_PULLUP_OHM * wMilliVolt,
                               LIGHT_TEST_VREF_MV - wMilliVolt);
}

static uint32_t light_pow_x100(float fBase, float fExponent)
{
    float fY = 1.0f;
    float fTerm = 1.0f;
    float fLnBase;
    float fX;

    if (fBase <= 0.0f) {
        return 0u;
    }

    /*
     * Fast enough approximation for a serial debug lux estimate. It avoids
     * pulling libm powf into this small test module.
     */
    fLnBase = 2.0f * ((fBase - 1.0f) / (fBase + 1.0f));
    {
        float fZ = (fBase - 1.0f) / (fBase + 1.0f);
        float fZ2 = fZ * fZ;
        float fZn = fZ * fZ2;

        fLnBase += 2.0f * fZn / 3.0f;
        fZn *= fZ2;
        fLnBase += 2.0f * fZn / 5.0f;
        fZn *= fZ2;
        fLnBase += 2.0f * fZn / 7.0f;
        fZn *= fZ2;
        fLnBase += 2.0f * fZn / 9.0f;
    }

    fX = fExponent * fLnBase;

    if (fX > 19.0f) {
        return UINT32_MAX;
    }

    if (fX < -19.0f) {
        return 0u;
    }

    for (uint32_t i = 1u; i <= 16u; i++) {
        fTerm *= fX / (float)i;
        fY += fTerm;
    }

    if (fY <= 0.0f) {
        return 0u;
    }

    if (fY >= 42949672.0f) {
        return UINT32_MAX;
    }

    return (uint32_t)(fY * 100.0f + 0.5f);
}

static uint32_t light_lux_x100_from_ohm(uint32_t wLdrOhm)
{
    uint32_t wRatioPowX100;

    if (wLdrOhm == UINT32_MAX) {
        return 0u;
    }

    if (wLdrOhm == 0u) {
        wLdrOhm = 1u;
    }

    wRatioPowX100 = light_pow_x100(LIGHT_TEST_GL5528_R10_OHM / (float)wLdrOhm,
                                   1.0f / LIGHT_TEST_GL5528_GAMMA);
    if (wRatioPowX100 > (UINT32_MAX / 10u)) {
        return UINT32_MAX;
    }

    return wRatioPowX100 * 10u;
}

void light_test_init(void)
{
    reset_block(RESETS_RESET_ADC_BITS);
    unreset_block_wait(RESETS_RESET_ADC_BITS);
    adc_hw->cs = ADC_CS_EN_BITS;
    while (!(adc_hw->cs & ADC_CS_READY_BITS)) {
        tight_loop_contents();
    }

    adc_gpio_init(LIGHT_TEST_ADC_GPIO);
    adc_select_input(LIGHT_TEST_ADC_INPUT);

    s_bLightTestInited = true;
    s_wLastPrintMS = 0u;

    printf("LIGHT test: GPIO%u/ADC%u, pullup=%luohm, GL5528 R10=%luohm gamma=%u.%02u\r\n",
           (unsigned)LIGHT_TEST_ADC_GPIO,
           (unsigned)LIGHT_TEST_ADC_INPUT,
           (unsigned long)LIGHT_TEST_PULLUP_OHM,
           (unsigned long)LIGHT_TEST_GL5528_R10_OHM,
           (unsigned)((uint32_t)(LIGHT_TEST_GL5528_GAMMA * 100.0f + 0.5f) / 100u),
           (unsigned)((uint32_t)(LIGHT_TEST_GL5528_GAMMA * 100.0f + 0.5f) % 100u));
}

light_test_sample_t light_test_read(void)
{
    uint32_t wSum = 0u;
    light_test_sample_t tSample;

    adc_select_input(LIGHT_TEST_ADC_INPUT);

    for (uint32_t i = 0u; i < LIGHT_TEST_SAMPLE_COUNT; i++) {
        wSum += adc_read();
        sleep_us(50u);
    }

    tSample.hwRaw = (uint16_t)((wSum + (LIGHT_TEST_SAMPLE_COUNT / 2u)) /
                               LIGHT_TEST_SAMPLE_COUNT);
    tSample.wMilliVolt = light_div_round_u64((uint64_t)tSample.hwRaw *
                                             LIGHT_TEST_VREF_MV,
                                             4095u);
    tSample.wLdrOhm = light_ldr_ohm_from_mv(tSample.wMilliVolt);
    tSample.wLuxX100 = light_lux_x100_from_ohm(tSample.wLdrOhm);

    return tSample;
}

void light_test_task(void)
{
    uint32_t wNowMS = to_ms_since_boot(get_absolute_time());
    light_test_sample_t tSample;

    if (!s_bLightTestInited) {
        light_test_init();
    }

    if ((uint32_t)(wNowMS - s_wLastPrintMS) < LIGHT_TEST_PRINT_INTERVAL_MS) {
        return;
    }

    s_wLastPrintMS = wNowMS;
    tSample = light_test_read();

    if (tSample.wLdrOhm == UINT32_MAX) {
        printf("LIGHT: raw=%u adc=%lumV r_ldr=>%luohm lux=%lu.%02lu\r\n",
               (unsigned)tSample.hwRaw,
               (unsigned long)tSample.wMilliVolt,
               (unsigned long)LIGHT_TEST_PULLUP_OHM * 1000u,
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
