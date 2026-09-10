#ifndef POWER_KEY_AUDIT_H
#define POWER_KEY_AUDIT_H
#include <stdint.h>
typedef struct {
    uint32_t edges;
    uint16_t age_ms;
    uint8_t levels; /* bit0=SIO input, bit1=INFROMPAD; both use electrical high=1 */
    uint8_t faults; /* latched: 1=not SIO, 2=output enabled, 4=override, 8=pad config */
    uint16_t sio_changes, pad_changes, window_ms;
    uint8_t sio_seen, pad_seen; /* bit0=low observed, bit1=high observed */
} power_key_audit_t;
/* Atomically snapshots and restarts the observation window; single consumer. */
void power_key_service_get_audit(power_key_audit_t *out);
#endif
