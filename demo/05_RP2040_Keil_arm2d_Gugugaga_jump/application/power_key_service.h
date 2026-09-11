#ifndef __POWER_KEY_SERVICE_H__
#define __POWER_KEY_SERVICE_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_KEEP_PIN                  2u
#define POWER_UP_CHECK_PIN              9u

/* Start 1 ms hardware-alarm sampling on core 0. False permits poll fallback. */
bool power_key_service_start_sampling(void);
/* Called by the sampler, or main-loop fallback; never both concurrently. */
void power_key_service_poll(uint32_t now_ms);

/* One latched, debounced press; the startup power-on press is excluded. */
bool power_key_service_consume_press(void);
bool power_key_service_is_pressed(void);
/* Cached sample for diagnostics; does not access GPIO from the draw path. */
bool power_key_service_is_raw_pressed(void);
/* Qualified hold tolerates contact bounce; confirmed release clears it in 12 ms. */
bool power_key_service_glide_ready(void);
uint16_t power_key_service_max_sample_gap_ms(void);
/* Last completed low/high intervals, saturated at 9999 ms. Poll resolution. */
void power_key_service_get_pulse_ms(uint16_t *low_ms, uint16_t *high_ms);
/* A fresh hold after startup releases POWER_KEEP_PIN after 5 seconds,
 * independently of game state. Confirmed release restarts that timer. */

#ifdef __cplusplus
}
#endif

#endif
