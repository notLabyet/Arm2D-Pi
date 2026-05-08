#ifndef __IR_TEST_H__
#define __IR_TEST_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * Crystal_Mouse schematic:
 *   INFRARED_PWM  -> GPIO28_ADC2
 *   INFRARED_SIGN -> GPIO22
 *
 * XGIRM-H3238T is a demodulating IR receiver. Its OUT pin is idle high and
 * pulls low while it sees a valid carrier burst near 38 kHz.
 */
#ifndef IR_TEST_TX_PIN
#   define IR_TEST_TX_PIN              28u
#endif

#ifndef IR_TEST_RX_PIN
#   define IR_TEST_RX_PIN              22u
#endif

#ifndef IR_TEST_CARRIER_HZ
#   define IR_TEST_CARRIER_HZ          38000u
#endif

#ifndef IR_TEST_DUTY_PERMILLE
#   define IR_TEST_DUTY_PERMILLE       333u
#endif

#ifndef IR_TEST_SEND_INTERVAL_MS
#   define IR_TEST_SEND_INTERVAL_MS    1000u
#endif

#ifndef IR_TEST_TX_ACTIVE_LOW
#   define IR_TEST_TX_ACTIVE_LOW       0
#endif

#define IR_RX_CAPTURE_MAX              40u

typedef struct ir_rx_capture_t {
    uint8_t chCount;
    uint8_t chLevel[IR_RX_CAPTURE_MAX];
    uint32_t wDurationUS[IR_RX_CAPTURE_MAX];
    uint32_t wLowPulseCount;
    uint32_t wTotalLowUS;
    bool bOverflow;
} ir_rx_capture_t;

void ir_test_init(uint32_t wCarrierHz, uint16_t hwDutyPermille);
void ir_test_set_carrier(uint32_t wCarrierHz, uint16_t hwDutyPermille);
void ir_test_send_byte(uint8_t chData);
bool ir_test_receive_snapshot(ir_rx_capture_t *ptCapture);
bool ir_test_decode_capture(const ir_rx_capture_t *ptCapture, uint8_t *pchData);
void ir_test_task(void);

#endif
