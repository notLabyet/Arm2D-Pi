#ifndef __CODEX_STATUS_H__
#define __CODEX_STATUS_H__

#include <stdint.h>

#define CODEX_HID_REPORT_SIZE      64u
#define CODEX_HID_MAGIC0           'C'
#define CODEX_HID_MAGIC1           'X'
#define CODEX_HID_VERSION          1u

typedef enum codex_state_t {
    CODEX_STATE_IDLE = 0,
    CODEX_STATE_WORKING = 1,
    CODEX_STATE_RETRY = 2,
    CODEX_STATE_CONNECTION_ERROR = 3,
} codex_state_t;

typedef enum codex_phase_t {
    CODEX_PHASE_IDLE = 0,
    CODEX_PHASE_THINKING = 1,
    CODEX_PHASE_WRITING = 2,
    CODEX_PHASE_RETRY = 3,
    CODEX_PHASE_ERROR = 4,
} codex_phase_t;

/* HID payload fields are packed in this order:
 * 0-2 magic/version, 3 state, 4-7 tokens, 8-9 retries, 10-11 seq, 12-15 balance.
 * Byte 16 is an optional phase field used when state is CODEX_STATE_WORKING.
 */
extern volatile uint8_t  g_u8CodexState;
extern volatile uint8_t  g_u8CodexPhase;
extern volatile uint32_t g_u32CodexTokens;
extern volatile int32_t  g_i32CodexBalance;
extern volatile uint16_t g_u16CodexRetries;
extern volatile uint16_t g_u16CodexSeq;
extern volatile uint32_t g_u32CodexLastMs;

void codex_status_hid_rx(uint8_t const *pchReport, uint16_t hwLength);

#endif
