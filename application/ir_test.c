#include "ir_test.h"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#define IR_MARK_US                 600u
#define IR_START_MARK_US           2400u
#define IR_START_SPACE_US          600u
#define IR_ZERO_SPACE_US           600u
#define IR_ONE_SPACE_US            1600u
#define IR_FRAME_GAP_US            5000u
#define IR_RESULT_DELAY_MS         60u

static uint32_t s_wCarrierHz = IR_TEST_CARRIER_HZ;
static uint16_t s_hwDutyPermille = IR_TEST_DUTY_PERMILLE;
static uint s_wSlice;
static uint s_wChannel;

static volatile bool s_bRxReceiving;
static volatile bool s_bRxDone;
static volatile bool s_bRxOverflow;
static volatile uint8_t s_chRxCount;
static volatile uint32_t s_wLastEdgeUS;
static volatile uint8_t s_chRxLevel[IR_RX_CAPTURE_MAX];
static volatile uint32_t s_wRxDurationUS[IR_RX_CAPTURE_MAX];

static bool ir_us_in_range(uint32_t wValue, uint32_t wTarget, uint32_t wTolerance)
{
    return (wValue >= (wTarget - wTolerance)) && (wValue <= (wTarget + wTolerance));
}

static void ir_rx_reset(void)
{
    uint32_t wState = save_and_disable_interrupts();

    s_bRxReceiving = false;
    s_bRxDone = false;
    s_bRxOverflow = false;
    s_chRxCount = 0;
    s_wLastEdgeUS = time_us_32();

    restore_interrupts(wState);
}

static void ir_rx_gpio_irq(uint gpio, uint32_t events)
{
    (void)events;

    if (gpio != IR_TEST_RX_PIN) {
        return;
    }

    uint32_t wNowUS = time_us_32();
    bool bNewLevel = gpio_get(IR_TEST_RX_PIN) ? true : false;
    uint32_t wDurationUS = wNowUS - s_wLastEdgeUS;

    if (!s_bRxReceiving || (wDurationUS > IR_FRAME_GAP_US)) {
        s_bRxReceiving = true;
        s_bRxDone = false;
        s_bRxOverflow = false;
        s_chRxCount = 0;
        s_wLastEdgeUS = wNowUS;
        return;
    }

    if (!s_bRxDone) {
        uint8_t chCount = s_chRxCount;

        if (chCount < IR_RX_CAPTURE_MAX) {
            s_chRxLevel[chCount] = bNewLevel ? 0u : 1u;
            s_wRxDurationUS[chCount] = wDurationUS;
            s_chRxCount = (uint8_t)(chCount + 1u);
        } else {
            s_bRxOverflow = true;
        }
    }

    s_wLastEdgeUS = wNowUS;
}

static void ir_rx_finalize_if_idle(void)
{
    uint32_t wState = save_and_disable_interrupts();

    if (s_bRxReceiving && !s_bRxDone && ((uint32_t)(time_us_32() - s_wLastEdgeUS) > IR_FRAME_GAP_US)) {
        s_bRxReceiving = false;
        s_bRxDone = true;
    }

    restore_interrupts(wState);
}

static void ir_tx_pwm_off(void)
{
    pwm_set_enabled(s_wSlice, false);
    gpio_set_function(IR_TEST_TX_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(IR_TEST_TX_PIN, GPIO_OUT);
    gpio_put(IR_TEST_TX_PIN, IR_TEST_TX_ACTIVE_LOW ? 1 : 0);
}

static void ir_tx_pwm_on(void)
{
    gpio_set_function(IR_TEST_TX_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(s_wSlice, true);
}

static void ir_tx_mark(uint32_t wDurationUS)
{
    ir_tx_pwm_on();
    sleep_us(wDurationUS);
    ir_tx_pwm_off();
}

static void ir_tx_space(uint32_t wDurationUS)
{
    sleep_us(wDurationUS);
}

void ir_test_set_carrier(uint32_t wCarrierHz, uint16_t hwDutyPermille)
{
    uint32_t wClockHz = clock_get_hz(clk_sys);
    uint32_t wWrap;
    uint32_t wLevel;
    uint32_t wDiv16;
    pwm_config tConfig;

    if (wCarrierHz == 0u) {
        wCarrierHz = 1u;
    }

    if (hwDutyPermille > 1000u) {
        hwDutyPermille = 1000u;
    }

    wDiv16 = (uint32_t)(((uint64_t)wClockHz * 16u + ((uint64_t)wCarrierHz * 65536u - 1u))
                      / ((uint64_t)wCarrierHz * 65536u));

    if (wDiv16 < 16u) {
        wDiv16 = 16u;
    } else if (wDiv16 > 4095u) {
        wDiv16 = 4095u;
    }

    wWrap = (uint32_t)(((uint64_t)wClockHz * 16u) / ((uint64_t)wCarrierHz * wDiv16));
    if (wWrap == 0u) {
        wWrap = 1u;
    }
    wWrap -= 1u;
    if (wWrap > 65535u) {
        wWrap = 65535u;
    }

    wLevel = ((wWrap + 1u) * hwDutyPermille) / 1000u;
    if (wLevel > wWrap) {
        wLevel = wWrap;
    }

    s_wCarrierHz = wCarrierHz;
    s_hwDutyPermille = hwDutyPermille;

    tConfig = pwm_get_default_config();
    pwm_config_set_wrap(&tConfig, (uint16_t)wWrap);
    pwm_config_set_clkdiv_int_frac(&tConfig, (uint8_t)(wDiv16 >> 4), (uint8_t)(wDiv16 & 0x0fu));
    pwm_init(s_wSlice, &tConfig, false);
    pwm_set_chan_level(s_wSlice, s_wChannel, (uint16_t)wLevel);

#if IR_TEST_TX_ACTIVE_LOW
    pwm_set_output_polarity(s_wSlice,
                            s_wChannel == PWM_CHAN_A,
                            s_wChannel == PWM_CHAN_B);
#else
    pwm_set_output_polarity(s_wSlice, false, false);
#endif

    ir_tx_pwm_off();
}

void ir_test_init(uint32_t wCarrierHz, uint16_t hwDutyPermille)
{
    s_wSlice = pwm_gpio_to_slice_num(IR_TEST_TX_PIN);
    s_wChannel = pwm_gpio_to_channel(IR_TEST_TX_PIN);

    gpio_init(IR_TEST_RX_PIN);
    gpio_set_function(IR_TEST_RX_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(IR_TEST_RX_PIN, GPIO_IN);
    gpio_pull_up(IR_TEST_RX_PIN);

    ir_test_set_carrier(wCarrierHz, hwDutyPermille);
    ir_rx_reset();

    gpio_set_irq_enabled_with_callback(IR_TEST_RX_PIN,
                                       GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
                                       true,
                                       ir_rx_gpio_irq);

    printf("IR test: TX GPIO%u, RX GPIO%u, carrier=%lu Hz, duty=%u.%u%%\r\n",
           (unsigned)IR_TEST_TX_PIN,
           (unsigned)IR_TEST_RX_PIN,
           (unsigned long)s_wCarrierHz,
           (unsigned)(s_hwDutyPermille / 10u),
           (unsigned)(s_hwDutyPermille % 10u));
}

void ir_test_send_byte(uint8_t chData)
{
    ir_tx_mark(IR_START_MARK_US);
    ir_tx_space(IR_START_SPACE_US);

    for (uint8_t chBit = 0; chBit < 8u; chBit++) {
        ir_tx_mark(IR_MARK_US);
        ir_tx_space((chData & (1u << chBit)) ? IR_ONE_SPACE_US : IR_ZERO_SPACE_US);
    }

    ir_tx_mark(IR_MARK_US);
}

bool ir_test_receive_snapshot(ir_rx_capture_t *ptCapture)
{
    if (ptCapture == NULL) {
        return false;
    }

    ir_rx_finalize_if_idle();

    uint32_t wState = save_and_disable_interrupts();
    bool bDone = s_bRxDone;

    if (bDone) {
        uint8_t chCount = s_chRxCount;
        uint32_t wLowPulseCount = 0;
        uint32_t wTotalLowUS = 0;

        ptCapture->chCount = chCount;
        ptCapture->bOverflow = s_bRxOverflow;

        for (uint8_t chIndex = 0; chIndex < chCount; chIndex++) {
            uint8_t chLevel = s_chRxLevel[chIndex];
            uint32_t wDurationUS = s_wRxDurationUS[chIndex];

            ptCapture->chLevel[chIndex] = chLevel;
            ptCapture->wDurationUS[chIndex] = wDurationUS;

            if (chLevel == 0u) {
                wLowPulseCount++;
                wTotalLowUS += wDurationUS;
            }
        }

        ptCapture->wLowPulseCount = wLowPulseCount;
        ptCapture->wTotalLowUS = wTotalLowUS;
        s_bRxDone = false;
    }

    restore_interrupts(wState);

    return bDone;
}

bool ir_test_decode_capture(const ir_rx_capture_t *ptCapture, uint8_t *pchData)
{
    uint8_t chData = 0;
    uint8_t chIndex = 0;

    if ((ptCapture == NULL) || (pchData == NULL)) {
        return false;
    }

    while ((chIndex < ptCapture->chCount) && (ptCapture->chLevel[chIndex] != 0u)) {
        chIndex++;
    }

    if ((chIndex + 18u) >= ptCapture->chCount) {
        return false;
    }

    if (!ir_us_in_range(ptCapture->wDurationUS[chIndex], IR_START_MARK_US, 900u)) {
        return false;
    }
    chIndex++;

    if ((ptCapture->chLevel[chIndex] != 1u)
    ||  !ir_us_in_range(ptCapture->wDurationUS[chIndex], IR_START_SPACE_US, 350u)) {
        return false;
    }
    chIndex++;

    for (uint8_t chBit = 0; chBit < 8u; chBit++) {
        if ((ptCapture->chLevel[chIndex] != 0u)
        ||  !ir_us_in_range(ptCapture->wDurationUS[chIndex], IR_MARK_US, 350u)) {
            return false;
        }
        chIndex++;

        if (ptCapture->chLevel[chIndex] != 1u) {
            return false;
        }

        if (ptCapture->wDurationUS[chIndex] > 1000u) {
            if (!ir_us_in_range(ptCapture->wDurationUS[chIndex], IR_ONE_SPACE_US, 700u)) {
                return false;
            }
            chData |= (uint8_t)(1u << chBit);
        } else {
            if (!ir_us_in_range(ptCapture->wDurationUS[chIndex], IR_ZERO_SPACE_US, 350u)) {
                return false;
            }
        }
        chIndex++;
    }

    if ((chIndex < ptCapture->chCount)
    &&  (ptCapture->chLevel[chIndex] == 0u)
    &&  !ir_us_in_range(ptCapture->wDurationUS[chIndex], IR_MARK_US, 350u)) {
        return false;
    }

    *pchData = chData;
    return true;
}

void ir_test_task(void)
{
    static uint32_t s_wLastSendMS = 0;
    static uint32_t s_wResultMS = 0;
    static uint8_t s_chTxData = 0x5au;
    static bool s_bWaitingResult = false;
    static uint8_t s_chLastTxData = 0;

    uint32_t wNowMS = time_us_32() / 1000u;

    if (s_bWaitingResult && ((uint32_t)(wNowMS - s_wResultMS) >= IR_RESULT_DELAY_MS)) {
        ir_rx_capture_t tCapture;
        uint8_t chRxData = 0;

        if (ir_test_receive_snapshot(&tCapture)) {
            bool bDecoded = ir_test_decode_capture(&tCapture, &chRxData);
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
        } else {
            printf("IR RX: timeout/no frame for tx=0x%02X\r\n", (unsigned)s_chLastTxData);
        }

        s_bWaitingResult = false;
    }

    if (!s_bWaitingResult && ((uint32_t)(wNowMS - s_wLastSendMS) >= IR_TEST_SEND_INTERVAL_MS)) {
        s_wLastSendMS = wNowMS;
        s_chLastTxData = s_chTxData;
        s_chTxData = (uint8_t)(s_chTxData + 0x17u);

        ir_rx_reset();
        printf("IR TX: 0x%02X carrier=%luHz duty=%u.%u%%\r\n",
               (unsigned)s_chLastTxData,
               (unsigned long)s_wCarrierHz,
               (unsigned)(s_hwDutyPermille / 10u),
               (unsigned)(s_hwDutyPermille % 10u));

        ir_test_send_byte(s_chLastTxData);

        s_wResultMS = time_us_32() / 1000u;
        s_bWaitingResult = true;
    }
}
