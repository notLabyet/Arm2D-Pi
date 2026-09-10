#include "ir_task.h"

#include <stdbool.h>
#include <stdio.h>

#include "drv_ir.h"
#include "pico/stdlib.h"

typedef enum ir_task_state_t {
    IR_TASK_IDLE = 0,
    IR_TASK_SENDING,
    IR_TASK_WAIT_RESULT,
} ir_task_state_t;

static bool s_bInited;
static uint32_t s_wLastRunMS;
static uint32_t s_wResultStartMS;
static uint8_t s_chTxData = 0x5au;
static uint8_t s_chLastTxData;
static ir_task_state_t s_tState = IR_TASK_IDLE;

static void ir_task_print_capture_debug(const drv_ir_rx_capture_t *ptCapture)
{
    uint8_t chLimit = ptCapture->chCount;

    if (chLimit > 10u) {
        chLimit = 10u;
    }

    printf("IR cap:");
    for (uint8_t chIndex = 0; chIndex < chLimit; chIndex++) {
        printf(" %u:%lu",
               (unsigned)ptCapture->chLevel[chIndex],
               (unsigned long)ptCapture->wDurationUS[chIndex]);
    }
    printf("\r\n");
}

static void ir_task_service_send_until_done(void)
{
    while (drv_ir_is_sending()) {
        (void)drv_ir_send_task();
        sleep_us(50u);
    }
}

void ir_task_init(void)
{
    drv_ir_init(DRV_IR_CARRIER_HZ, DRV_IR_DUTY_PERMILLE);
    s_bInited = true;
    s_wLastRunMS = 0u;
    s_wResultStartMS = 0u;
    s_tState = IR_TASK_IDLE;

    printf("IR task: TX GPIO%u, RX GPIO%u, rx_idle=%u, carrier=%lu Hz, duty=%u.%u%%\r\n",
           (unsigned)DRV_IR_TX_PIN,
           (unsigned)DRV_IR_RX_PIN,
           (unsigned)drv_ir_get_rx_level(),
           (unsigned long)drv_ir_get_carrier_hz(),
           (unsigned)(drv_ir_get_duty_permille() / 10u),
           (unsigned)(drv_ir_get_duty_permille() % 10u));
}

void ir_task(uint32_t wPeriodMS)
{
    uint32_t wNowMS = to_ms_since_boot(get_absolute_time());

    if (!s_bInited) {
        ir_task_init();
    }

    if (wPeriodMS == 0u) {
        wPeriodMS = IR_TASK_SEND_INTERVAL_MS;
    }

    (void)drv_ir_send_task();

    switch (s_tState) {
        case IR_TASK_IDLE:
            if ((uint32_t)(wNowMS - s_wLastRunMS) < wPeriodMS) {
                break;
            }

            s_wLastRunMS = wNowMS;
            s_chLastTxData = s_chTxData;
            s_chTxData = (uint8_t)(s_chTxData + 0x17u);
            drv_ir_reset_rx();

            if (drv_ir_send_byte_start(s_chLastTxData)) {
                printf("IR TX: 0x%02X carrier=%luHz duty=%u.%u%%\r\n",
                       (unsigned)s_chLastTxData,
                       (unsigned long)drv_ir_get_carrier_hz(),
                       (unsigned)(drv_ir_get_duty_permille() / 10u),
                       (unsigned)(drv_ir_get_duty_permille() % 10u));
                s_tState = IR_TASK_SENDING;
            }
            break;

        case IR_TASK_SENDING:
            ir_task_service_send_until_done();
            s_wResultStartMS = to_ms_since_boot(get_absolute_time());
            s_tState = IR_TASK_WAIT_RESULT;
            break;

        case IR_TASK_WAIT_RESULT:
            if ((uint32_t)(wNowMS - s_wResultStartMS) >= IR_TASK_RESULT_DELAY_MS) {
                drv_ir_rx_capture_t tCapture;
                uint8_t chRxData = 0;

                if (drv_ir_receive_snapshot(&tCapture)) {
                    bool bDecoded = drv_ir_decode_capture(&tCapture, &chRxData);
                    bool bPass = bDecoded && (chRxData == s_chLastTxData);

                    printf("IR RX: %s tx=0x%02X rx=%s0x%02X items=%u low_pulses=%lu low_total=%luus%s\r\n",
                           bPass ? "PASS" : "FAIL",
                           (unsigned)s_chLastTxData,
                           bDecoded ? "" : "?",
                           (unsigned)chRxData,
                           (unsigned)tCapture.chCount,
                           (unsigned long)tCapture.wLowPulseCount,
                           (unsigned long)tCapture.wTotalLowUS,
                           tCapture.bOverflow ? " overflow" : "");
                    if (!bPass) {
                        ir_task_print_capture_debug(&tCapture);
                    }
                } else {
                    printf("IR RX: timeout/no frame for tx=0x%02X rx_level=%u\r\n",
                           (unsigned)s_chLastTxData,
                           (unsigned)drv_ir_get_rx_level());
                }

                s_tState = IR_TASK_IDLE;
            }
            break;

        default:
            s_tState = IR_TASK_IDLE;
            break;
    }
}
