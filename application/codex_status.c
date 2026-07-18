#include "codex_status.h"

#include "perf_counter.h"

volatile uint8_t  g_u8CodexState = CODEX_STATE_IDLE;
volatile uint8_t  g_u8CodexPhase = CODEX_PHASE_IDLE;
volatile uint32_t g_u32CodexTokens = 0;
volatile int32_t  g_i32CodexBalance = -1;
volatile uint16_t g_u16CodexRetries = 0;
volatile uint16_t g_u16CodexSeq = 0;
volatile uint32_t g_u32CodexLastMs = 0;

static uint16_t codex_read_le16(uint8_t const *pchData)
{
    return (uint16_t)pchData[0] | ((uint16_t)pchData[1] << 8);
}

static uint32_t codex_read_le32(uint8_t const *pchData)
{
    return (uint32_t)pchData[0] |
           ((uint32_t)pchData[1] << 8) |
           ((uint32_t)pchData[2] << 16) |
           ((uint32_t)pchData[3] << 24);
}

void codex_status_hid_rx(uint8_t const *pchReport, uint16_t hwLength)
{
    uint8_t const *pchPayload = pchReport;
    uint16_t hwPayloadLength = hwLength;
    uint8_t chState;
    uint8_t chPhase = CODEX_PHASE_IDLE;

    /* Ignore short packets or packets from other HID tools. */
    if ((pchReport == 0) || (hwLength < 16u)) {
        return;
    }

    if ((pchReport[0] == 0u) &&
        (hwLength >= 17u) &&
        (pchReport[1] == CODEX_HID_MAGIC0) &&
        (pchReport[2] == CODEX_HID_MAGIC1)) {
        pchPayload = &pchReport[1];
        hwPayloadLength = (uint16_t)(hwLength - 1u);
    }

    if ((hwPayloadLength < 16u) ||
        (pchPayload[0] != CODEX_HID_MAGIC0) ||
        (pchPayload[1] != CODEX_HID_MAGIC1) ||
        (pchPayload[2] != CODEX_HID_VERSION)) {
        return;
    }

    chState = pchPayload[3];
    if (chState > CODEX_STATE_CONNECTION_ERROR) {
        chState = CODEX_STATE_IDLE;
    }

    if (hwPayloadLength > 16u) {
        chPhase = pchPayload[16];
        if (chPhase > CODEX_PHASE_ERROR) {
            chPhase = CODEX_PHASE_IDLE;
        }
    } else if (chState == CODEX_STATE_WORKING) {
        chPhase = CODEX_PHASE_THINKING;
    } else if (chState == CODEX_STATE_RETRY) {
        chPhase = CODEX_PHASE_RETRY;
    } else if (chState == CODEX_STATE_CONNECTION_ERROR) {
        chPhase = CODEX_PHASE_ERROR;
    }

    g_u8CodexState = chState;
    g_u8CodexPhase = chPhase;
    g_u32CodexTokens = codex_read_le32(&pchPayload[4]);
    g_u16CodexRetries = codex_read_le16(&pchPayload[8]);
    g_u16CodexSeq = codex_read_le16(&pchPayload[10]);
    g_i32CodexBalance = (int32_t)codex_read_le32(&pchPayload[12]);
    g_u32CodexLastMs = get_system_ms();
}
