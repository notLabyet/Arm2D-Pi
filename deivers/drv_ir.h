/**
 * @file drv_ir.h
 * @brief Infrared TX/RX driver: 38 kHz PWM carrier on TX, GPIO edge capture on RX.
 *
 * Board signals (Crystal_Mouse schematic):
 *   INFRARED_PWM  -> GPIO28 (ADC2 capable; used here as PWM TX)
 *   INFRARED_SIGN -> GPIO22 (digital input from demodulated IR receiver)
 *
 * The XGIRM-H3238T (or similar) outputs idle high and pulls low when it sees a valid
 * ~38 kHz burst. This driver implements a simple proprietary single-byte frame on TX
 * and a matching decoder for captured edge timings on RX.
 */

#ifndef DRV_IR_H
#define DRV_IR_H

#include <stdbool.h>
#include <stdint.h>

/** GPIO for IR LED / transistor drive (PWM modulated carrier). */
#ifndef DRV_IR_TX_PIN
#   define DRV_IR_TX_PIN              28u
#endif

/** GPIO from IR demodulator module output. */
#ifndef DRV_IR_RX_PIN
#   define DRV_IR_RX_PIN              22u
#endif

/** Default carrier frequency (Hz); 38000 is typical for IR remote modules. */
#ifndef DRV_IR_CARRIER_HZ
#   define DRV_IR_CARRIER_HZ          38000u
#endif

/** TX PWM duty in parts per thousand (limits LED current / range). */
#ifndef DRV_IR_DUTY_PERMILLE
#   define DRV_IR_DUTY_PERMILLE       333u
#endif

/**
 * If 1, PWM polarity is inverted so “on” drives the GPIO low (common NPN low-side).
 * If 0, normal PWM polarity is used.
 */
#ifndef DRV_IR_TX_ACTIVE_LOW
#   define DRV_IR_TX_ACTIVE_LOW       0
#endif

/** Maximum number of RX edge segments stored per frame (level + duration pairs). */
#define DRV_IR_RX_CAPTURE_MAX         40u

/**
 * Snapshot of one received frame after demodulation: each slot is the logical level
 * after an edge and the time (µs) since the previous edge. Statistics on low pulses
 * are aggregated for debugging; bOverflow if more than DRV_IR_RX_CAPTURE_MAX edges.
 */
typedef struct drv_ir_rx_capture_t {
    uint8_t chCount;
    uint8_t chLevel[DRV_IR_RX_CAPTURE_MAX];
    uint32_t wDurationUS[DRV_IR_RX_CAPTURE_MAX];
    uint32_t wLowPulseCount;
    uint32_t wTotalLowUS;
    bool bOverflow;
} drv_ir_rx_capture_t;

/** Configure TX PWM carrier and RX GPIO + IRQ; resets RX state machine. */
void drv_ir_init(uint32_t wCarrierHz, uint16_t hwDutyPermille);

/** Recompute PWM wrap/divider for a new carrier; TX is left off until a send marks active. */
void drv_ir_set_carrier(uint32_t wCarrierHz, uint16_t hwDutyPermille);

/** Clear RX buffers and flags (call when changing environment or after a bad frame). */
void drv_ir_reset_rx(void);

/**
 * Arm a non-blocking transmit of one byte using the built-in timing table.
 * Returns false if a transmission is already in progress.
 */
bool drv_ir_send_byte_start(uint8_t chData);

/**
 * Service TX state machine; call from the main loop as often as practical.
 * Uses time_us_32(); returns false when the frame is complete.
 */
bool drv_ir_send_task(void);

/** True between drv_ir_send_byte_start() and end of frame. */
bool drv_ir_is_sending(void);

/**
 * If a full frame has been captured, copy edge list into ptCapture and clear “done”.
 * Returns false if no complete frame yet or ptCapture is NULL.
 */
bool drv_ir_receive_snapshot(drv_ir_rx_capture_t *ptCapture);

/**
 * Parse a capture buffer into an 8-bit payload using the same encoding as TX.
 * Returns false if timing/levels do not match the expected start + 8 data bits + stop.
 */
bool drv_ir_decode_capture(const drv_ir_rx_capture_t *ptCapture, uint8_t *pchData);

uint32_t drv_ir_get_carrier_hz(void);
uint16_t drv_ir_get_duty_permille(void);

#endif
