#include "drv_battery.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"

static uint32_t drv_battery_now_ms;
/* 对外电压单位为 V，例如 3.85 表示 3.85V。 */
static float drv_battery_voltage;
/* 对外电量范围为 0.0 到 100.0。 */
static float drv_battery_percentage;

/* 一个曲线采样点：电压单位为 mV，及该电压对应的估算 SoC。 */
typedef struct drv_battery_soc_point_t {
    uint16_t hwVoltageMV;
    uint8_t chPercentage;
} drv_battery_soc_point_t;

/*
 * 单节 3.7V 锂离子/LiPo 电池的典型静置电压曲线。
 * 253450 的 400mAh 容量影响续航时间，不改变此电压-SoC 映射。
 * 负载、温度和老化会影响端电压，量产前应按实测电池校准各电压点。
 */
static const drv_battery_soc_point_t c_tBatterySocCurve[] = {
    {4200u, 100u},
    {4150u,  95u},
    {4110u,  90u},
    {4080u,  85u},
    {4020u,  80u},
    {3980u,  75u},
    {3950u,  70u},
    {3910u,  65u},
    {3870u,  60u},
    {3850u,  55u},
    {3840u,  50u},
    {3820u,  45u},
    {3800u,  40u},
    {3790u,  35u},
    {3770u,  30u},
    {3750u,  25u},
    {3730u,  20u},
    {3710u,  15u},
    {3690u,  10u},
    {3610u,   5u},
    {3270u,   0u},
};

#define DRV_BATTERY_SOC_CURVE_POINT_COUNT \
    (sizeof(c_tBatterySocCurve) / sizeof(c_tBatterySocCurve[0]))

static float __drv_battery_voltage_to_percentage(float fVoltageMV)
{
    /*
     * 曲线从高电压到低电压排列，首项是满电，末项是耗尽。
     * 电压落在两个表项之间时，计算公式为：
     * SoC = SoC下限 + (V当前 - V下限) / (V上限 - V下限)
     *                 * (SoC上限 - SoC下限)。
     */

    /* 高于满充电压时钳位为 100%，避免充电时显示超过 100%。 */
    if (fVoltageMV >= c_tBatterySocCurve[0].hwVoltageMV) {
        return 100.0f;
    }

    for (uint32_t wIndex = 0u;
         (wIndex + 1u) < DRV_BATTERY_SOC_CURVE_POINT_COUNT;
         wIndex++) {
        /* 仅使用曲线中相邻的 wIndex 和 wIndex + 1 两个标定点。 */
        if ((fVoltageMV <= c_tBatterySocCurve[wIndex].hwVoltageMV)
        && (fVoltageMV >= c_tBatterySocCurve[wIndex + 1u].hwVoltageMV)) {
            /*
             * 线性插值：
             * SoC = P下一点 + (V当前 - V下一点)
             *     * (P当前点 - P下一点) / (V当前点 - V下一点)。
             * 例如 3.93V 只使用相邻的 3.95V(70%)、3.91V(65%) 两点。
             */
            return c_tBatterySocCurve[wIndex + 1u].chPercentage
                 + (fVoltageMV
                    - c_tBatterySocCurve[wIndex + 1u].hwVoltageMV)
                 * (c_tBatterySocCurve[wIndex].chPercentage
                    - c_tBatterySocCurve[wIndex + 1u].chPercentage)
                 / (c_tBatterySocCurve[wIndex].hwVoltageMV
                    - c_tBatterySocCurve[wIndex + 1u].hwVoltageMV);
        }
    }

    /* 低于曲线最低点时钳位为 0%，避免出现负数百分比。 */
    return 0.0f;
}

void drv_battery_init(void)
{
    /* GPIO27 对应 RP2040 的 ADC1，并连接到 BAT_VOLTAGE 分压节点。 */
    adc_init();
    adc_gpio_init(27);
    adc_select_input(1);
}

float drv_battery_get_voltage(void)
{
    return drv_battery_voltage;
}

float drv_battery_get_percentage(void)
{
    return drv_battery_percentage;
}

void drv_battery_task(void)
{
    float fVoltageMV;
    uint16_t hwRawADC;

    /* 每秒采样一次，避免频繁 ADC 读取造成显示数字抖动。 */
    if ((uint32_t)(to_ms_since_boot(get_absolute_time()) - drv_battery_now_ms)
        >= 1000u) {
        drv_battery_now_ms = to_ms_since_boot(get_absolute_time());
        hwRawADC = adc_read();

        /*
         * R27/R30 均为 510kΩ，因此 ADC 节点为电池电压的一半：
         * Vbat(mV) = ADC原始值 / 4095 * 3300mV * 2。
         */
        fVoltageMV = (float)hwRawADC * DRV_BATTERY_ADC_REFERENCE_MV
                   * DRV_BATTERY_DIVIDER_RATIO / DRV_BATTERY_ADC_MAX;
        /* 显示接口使用 V，曲线查表使用 mV，分别保存以避免单位混用。 */
        drv_battery_voltage = fVoltageMV / 1000.0f;
        drv_battery_percentage =
            __drv_battery_voltage_to_percentage(fVoltageMV);
    }
}
