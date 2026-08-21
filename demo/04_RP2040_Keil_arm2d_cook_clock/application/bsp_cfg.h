#ifndef BSP_CFG_H
#define BSP_CFG_H

#include "hardware/i2c.h"

/* Shared sensor bus, matching the wiring used by the sibling projects. */
#ifndef I2C_PORT
#   define I2C_PORT                     i2c0
#endif

#ifndef I2C_SDA
#   define I2C_SDA                      0u
#endif

#ifndef I2C_SCL
#   define I2C_SCL                      1u
#endif

#ifndef POWER_KEEP_PIN
#   define POWER_KEEP_PIN               2u
#endif

#ifndef POWER_UP_CHECK_PIN
#   define POWER_UP_CHECK_PIN           9u
#endif

#ifndef RIGHT_BUTTON_PIN
#   define RIGHT_BUTTON_PIN              24u
#endif

#ifndef POWER_KEY_DEBOUNCE_MS
#   define POWER_KEY_DEBOUNCE_MS        20u
#endif

#ifndef BUTTON_DEBOUNCE_MS
#   define BUTTON_DEBOUNCE_MS           10u
#endif

#ifndef BUTTON_LONG_PRESS_MS
#   define BUTTON_LONG_PRESS_MS         1000u
#endif

#ifndef POWER_KEY_LONG_PRESS_MS
#   define POWER_KEY_LONG_PRESS_MS      1500u
#endif

#endif