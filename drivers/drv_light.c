/**
 * @file drv_light.c
 * @brief ADC sampling, voltage divider math, and GL5528-based lux estimation.
 */

#include "drv_light.h"

#include "hardware/adc.h"
#include "hardware/resets.h"
#include "pico/stdlib.h"

/** Unsigned divide with half-up rounding to reduce systematic bias in small divisors. */
static uint32_t drv_light_div_round_u64(uint64_t ullValue, uint32_t wDivisor)
{
    return (uint32_t)((ullValue + (uint64_t)(wDivisor / 2u)) / wDivisor);
}

uint32_t drv_light_ldr_ohm_from_mv(uint32_t wMilliVolt)
{
    if (wMilliVolt >= DRV_LIGHT_VREF_MV) {
        return UINT32_MAX;
    }

    if (wMilliVolt == 0u) {
        return 0u;
    }

    /* Vout = Vref * Rldr / (Rpull + Rldr)  =>  Rldr = Rpull * Vout / (Vref - Vout) */
    return drv_light_div_round_u64((uint64_t)DRV_LIGHT_PULLUP_OHM * wMilliVolt,
                                   DRV_LIGHT_VREF_MV - wMilliVolt);
}

/**
 * Approximate base^exponent * 100 using a Taylor series for exp(x) with x = exponent * ln(base).
 * ln(base) is approximated by an atan-style series for stability without libm log.
 * Clamps to [0, UINT32_MAX] on overflow/underflow.
 */
static uint32_t drv_light_pow_x100(float fBase, float fExponent)
{
    float fY = 1.0f;
    float fTerm = 1.0f;
    float fLnBase;
    float fX;

    if (fBase <= 0.0f) {
        return 0u;
    }

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

uint32_t drv_light_lux_x100_from_ohm(uint32_t wLdrOhm)
{
    uint32_t wRatioPowX100;

    if (wLdrOhm == UINT32_MAX) {
        return 0u;
    }

    if (wLdrOhm == 0u) {
        wLdrOhm = 1u;
    }

    /* Lux ~ (R10 / Rldr)^(1/gamma) with R10 = LDR at 10 lx; scale to hundredths of lux. */
    wRatioPowX100 = drv_light_pow_x100(DRV_LIGHT_GL5528_R10_OHM /
                                       (float)wLdrOhm,
                                       1.0f / DRV_LIGHT_GL5528_GAMMA);
    if (wRatioPowX100 > (UINT32_MAX / 10u)) {
        return UINT32_MAX;
    }

    return wRatioPowX100 * 10u;
}

void drv_light_init(void)
{
    reset_block(RESETS_RESET_ADC_BITS);
    unreset_block_wait(RESETS_RESET_ADC_BITS);
    adc_hw->cs = ADC_CS_EN_BITS;
    while (!(adc_hw->cs & ADC_CS_READY_BITS)) {
        tight_loop_contents();
    }

    adc_gpio_init(DRV_LIGHT_ADC_GPIO);
    adc_select_input(DRV_LIGHT_ADC_INPUT);
}

drv_light_sample_t drv_light_read(uint16_t hwSampleCount)
{
    uint32_t wSum = 0u;
    drv_light_sample_t tSample;

    if (hwSampleCount == 0u) {
        hwSampleCount = 1u;
    }

    adc_select_input(DRV_LIGHT_ADC_INPUT);

    for (uint32_t i = 0u; i < hwSampleCount; i++) {
        wSum += adc_read();
        sleep_us(50u);
    }

    tSample.hwRaw = (uint16_t)((wSum + ((uint32_t)hwSampleCount / 2u)) /
                               (uint32_t)hwSampleCount);
    tSample.wMilliVolt = drv_light_div_round_u64((uint64_t)tSample.hwRaw *
                                                 DRV_LIGHT_VREF_MV,
                                                 4095u);
    tSample.wLdrOhm = drv_light_ldr_ohm_from_mv(tSample.wMilliVolt);
    tSample.wLuxX100 = drv_light_lux_x100_from_ohm(tSample.wLdrOhm);

    return tSample;
}
