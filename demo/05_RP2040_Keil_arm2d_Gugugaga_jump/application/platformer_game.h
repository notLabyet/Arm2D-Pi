#ifndef __PLATFORMER_GAME_H__
#define __PLATFORMER_GAME_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORMER_GAME_OBJECT_COUNT          16u
#define PLATFORMER_GAME_STEP_MS               10u
#define PLATFORMER_GAME_MAX_SPEED_PPS         180
#define PLATFORMER_GAME_BUFFER_MS             100u
#define PLATFORMER_GAME_COYOTE_MS              80u
#define PLATFORMER_GAME_BODY_OFFSET_X           9
#define PLATFORMER_GAME_BODY_WIDTH             30
#define PLATFORMER_GAME_BODY_HEIGHT            56
#define PLATFORMER_GAME_Q8                    256
#define PLATFORMER_GAME_COOKIE_SIZE           24
#define PLATFORMER_GAME_COMBO_WINDOW_MS       2000u
#define PLATFORMER_GAME_COMBO_MAX             999u

enum {
    PLATFORMER_GAME_PLATFORM = 0,
    PLATFORMER_GAME_COOKIE,
};

typedef struct platformer_game_object_t {
    int32_t lX;
    int16_t iTop;
    uint16_t hwWidth;
    uint8_t chHeight;
    uint8_t chType;
    bool bActive;
} platformer_game_object_t;

/* Pure game state: no GPIO, allocation or Arm-2D operations. Y is the foot line. */
typedef struct platformer_game_t {
    int32_t lXQ8;
    int32_t lFootYQ8;
    int32_t lVelocityYQ8;
    int32_t lNextSectionX;
    int16_t iGroundY;
    uint32_t wLastUpdateMs;
    uint32_t wRandomState;
    uint32_t wSectionIndex;
    uint32_t wCookies;
    uint16_t hwCombo;
    uint16_t hwComboRemainingMs;
    uint16_t hwAccumulatorMs;
    uint16_t hwJumpAgeMs;
    uint16_t hwLandingAgeMs;
    uint16_t hwBufferMs;
    uint16_t hwCoyoteMs;
    bool bGrounded;
    bool bGlideHeld;
    bool bGlideQualified; /* Input service qualifies the held/pulsed button signal. */
    bool bGliding;
    bool bJumpFlight;
    bool bJumpArmed;
    uint16_t hwGlideHoldMs;
    platformer_game_object_t tObjects[PLATFORMER_GAME_OBJECT_COUNT];
} platformer_game_t;

void platformer_game_init(platformer_game_t *ptGame, int16_t iGroundY,
                          int16_t iStartX, uint32_t wNowMs);
void platformer_game_update(platformer_game_t *ptGame, uint32_t wNowMs,
                            int16_t iSpeedPps, bool bJumpPressed);
void platformer_game_rebase(platformer_game_t *ptGame, int32_t lOffset);
uint32_t platformer_game_score(const platformer_game_t *ptGame);
/* -1 selects the ordinary walk/idle cycle; otherwise returns jump frame 0..11. */
int8_t platformer_game_jump_frame(const platformer_game_t *ptGame);

#ifdef __cplusplus
}
#endif

#endif
