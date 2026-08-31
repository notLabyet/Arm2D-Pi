/**
 * @file drv_light.h
 * @brief Ambient light sensing via ADC on a GL5528 LDR divider network.
 *
 * Schematic (Crystal_Mouse):
 *   LIGHT_SENSE -> GPIO26 (ADC0).
 *   Divider: +3V3 -- R6 10k -- LIGHT_SENSE -- R9 (GL5528 LDR) -- GND,
 *   with C20 100nF from LIGHT_SENSE to GND (low-pass / noise reduction).
 *
 * Lux is estimated from LDR resistance using a gamma law and reference values
 * for the GL5528 at 10 lx (compile-time tunables).
 */

#ifndef DRV_LIGHT_H
#define DRV_LIGHT_H

#include <stdint.h>

/** GPIO number wired to the light sense node (must match ADC input). */
#ifndef DRV_LIGHT_ADC_GPIO
#   define DRV_LIGHT_ADC_GPIO              26u
#endif

/** RP2040 ADC channel index (0 = GPIO26 on standard pinmux). */
#ifndef DRV_LIGHT_ADC_INPUT
#   define DRV_LIGHT_ADC_INPUT             0u
#endif

/** ADC reference in millivolts (board IO voltage). */
#ifndef DRV_LIGHT_VREF_MV
#   define DRV_LIGHT_VREF_MV               3300u
#endif

/** Top resistor in the divider (ohms), from 3V3 to sense node. */
#ifndef DRV_LIGHT_PULLUP_OHM
#   define DRV_LIGHT_PULLUP_OHM            10000u
#endif

/** Default number of ADC samples averaged per drv_light_read() call. */
#ifndef DRV_LIGHT_DEFAULT_SAMPLE_COUNT
#   define DRV_LIGHT_DEFAULT_SAMPLE_COUNT  32u
#endif

/** Manufacturer curve reference: LDR resistance at 10 lx (ohms), used in lux model. */
#ifndef DRV_LIGHT_GL5528_R10_OHM
#   define DRV_LIGHT_GL5528_R10_OHM        15000.0f
#endif

/** Photocell gamma for lux-from-resistance fit (typical ~0.6–0.8 for CdS). */
#ifndef DRV_LIGHT_GL5528_GAMMA
#   define DRV_LIGHT_GL5528_GAMMA          0.70f
#endif

/** One averaged reading: raw counts, derived millivolts, LDR ohms, and lux×100. */
typedef struct drv_light_sample_t {
    uint16_t hwRaw;
    uint32_t wMilliVolt;
    uint32_t wLdrOhm;
    uint32_t wLuxX100;
} drv_light_sample_t;

/** Reset ADC block, enable hardware, select the light sensor input. */
void drv_light_init(void);

/**
 * Average hwSampleCount raw readings (50 µs apart), then compute mV, LDR ohms, lux×100.
 * If hwSampleCount is 0, it is treated as 1.
 */
drv_light_sample_t drv_light_read(uint16_t hwSampleCount);

/** Map LDR resistance (ohms) to illuminance in hundredths of lux (integer math + float pow). */
uint32_t drv_light_lux_x100_from_ohm(uint32_t wLdrOhm);

/** Invert the divider: sense-node mV -> LDR resistance (0 if shorted, UINT32_MAX if open). */
uint32_t drv_light_ldr_ohm_from_mv(uint32_t wMilliVolt);

#endif
