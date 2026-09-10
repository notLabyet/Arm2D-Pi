#include "platformer_game.h"

#include <limits.h>
#include <string.h>

#define GAME_GRAVITY_PPS2              2048
#define GAME_JUMP_SPEED_PPS             512
#define GAME_MAX_FALL_SPEED_PPS         800
#define GAME_GLIDE_HOLD_MS              300u
#define GAME_GLIDE_GRAVITY_PPS2         128
#define GAME_GLIDE_FALL_SPEED_PPS        40
#define GAME_MAX_CATCHUP_MS             100u
#define GAME_LOOKAHEAD                  400
#define GAME_RETAIN                     160
#define GAME_SEED                      UINT32_C(0x71A9B35D)

typedef struct game_route_object_t {
    int16_t iX;
    uint8_t chRise;
    uint8_t chWidth;
    uint8_t chType;
} game_route_object_t;

/* Authored jump routes, randomized as whole sections rather than scattering
 * rewards independently. The 56 px body can reach a cookie from the floor
 * whenever its rise is below 80 px; even a floor jump reaches up to 144 px.
 * Every route therefore has rewards above 144 px that need a platform jump.
 * Platforms stop at 72 px so the 64 px sprite stays on screen at jump apex. */
static const game_route_object_t c_tRoutes[5][9] = {
    {   /* Introduction: one wide landing, then jump again for the high reward. */
        {  0, 36, 112, PLATFORMER_GAME_PLATFORM},
        { 18, 100, 24, PLATFORMER_GAME_COOKIE},
        { 60, 112, 24, PLATFORMER_GAME_COOKIE},
        {112, 148, 24, PLATFORMER_GAME_COOKIE},
    },
    {   /* Ascending steps with a pair of rewards above the upper landing. */
        {  0, 40,  88, PLATFORMER_GAME_PLATFORM},
        {120, 72, 104, PLATFORMER_GAME_PLATFORM},
        { 24, 100, 24, PLATFORMER_GAME_COOKIE},
        {100, 132, 24, PLATFORMER_GAME_COOKIE},
        {144, 156, 24, PLATFORMER_GAME_COOKIE},
        {184, 156, 24, PLATFORMER_GAME_COOKIE},
        {264, 108, 24, PLATFORMER_GAME_COOKIE},
    },
    {   /* Climb, cross a short gap, then descend to another jump opportunity. */
        {  0, 36, 80, PLATFORMER_GAME_PLATFORM},
        {108, 68, 72, PLATFORMER_GAME_PLATFORM},
        {212, 44, 92, PLATFORMER_GAME_PLATFORM},
        { 20, 100, 24, PLATFORMER_GAME_COOKIE},
        {110, 148, 24, PLATFORMER_GAME_COOKIE},
        {142, 152, 24, PLATFORMER_GAME_COOKIE},
        {250, 144, 24, PLATFORMER_GAME_COOKIE},
    },
    {   /* Longer low landing followed by a tighter upper reward pair. */
        {  0, 44, 104, PLATFORMER_GAME_PLATFORM},
        {136, 72,  88, PLATFORMER_GAME_PLATFORM},
        { 24, 104, 24, PLATFORMER_GAME_COOKIE},
        { 96, 136, 24, PLATFORMER_GAME_COOKIE},
        {154, 156, 24, PLATFORMER_GAME_COOKIE},
        {190, 160, 24, PLATFORMER_GAME_COOKIE},
        {270, 112, 24, PLATFORMER_GAME_COOKIE},
    },
    {   /* Climb to 72 px, jump from the edge, then hold through the air trail.
         * 48 px / 12 px steps follow a 160 px/s cruise at 40 px/s descent;
         * cookie/body overlap also accommodates ordinary 90..180 px/s cruise. */
        {  0, 40,  88, PLATFORMER_GAME_PLATFORM},
        {120, 72, 104, PLATFORMER_GAME_PLATFORM},
        { 24, 100, 24, PLATFORMER_GAME_COOKIE},
        {240, 160, 24, PLATFORMER_GAME_COOKIE},
        {288, 148, 24, PLATFORMER_GAME_COOKIE},
        {336, 136, 24, PLATFORMER_GAME_COOKIE},
        {384, 124, 24, PLATFORMER_GAME_COOKIE},
        {432, 112, 24, PLATFORMER_GAME_COOKIE},
        {480, 100, 24, PLATFORMER_GAME_COOKIE},
    },
};
static const uint8_t c_chRouteObjectCounts[5] = {4, 7, 7, 7, 9};

static uint32_t __game_random(platformer_game_t *ptGame)
{
    ptGame->wRandomState = ptGame->wRandomState * UINT32_C(1664525)
                          + UINT32_C(1013904223);
    return ptGame->wRandomState;
}

static void __game_add_object(platformer_game_t *ptGame, int32_t lX,
                              int16_t iRise, uint16_t hwWidth,
                              uint8_t chHeight, uint8_t chType)
{
    for (unsigned n = 0; n < PLATFORMER_GAME_OBJECT_COUNT; n++) {
        if (!ptGame->tObjects[n].bActive) {
            ptGame->tObjects[n] = (platformer_game_object_t){
                .lX = lX,
                .iTop = ptGame->iGroundY - iRise,
                .hwWidth = hwWidth,
                .chHeight = chHeight,
                .chType = chType,
                .bActive = true,
            };
            return;
        }
    }
}

static void __game_generate_course(platformer_game_t *ptGame)
{
    int32_t lPlayerX = ptGame->lXQ8 / PLATFORMER_GAME_Q8;
    unsigned nFree = 0;

    for (unsigned n = 0; n < PLATFORMER_GAME_OBJECT_COUNT; n++) {
        platformer_game_object_t *ptObject = &ptGame->tObjects[n];
        if (ptObject->bActive
        &&  ptObject->lX + ptObject->hwWidth < lPlayerX - GAME_RETAIN) {
            ptObject->bActive = false;
        }
        if (!ptObject->bActive) {
            nFree++;
        }
    }

    /* The longer glide section has nine objects and 720+ px spacing, allowing
     * its early platform/reward to retire before the next section is loaded. */
    while (ptGame->lNextSectionX <= lPlayerX + GAME_LOOKAHEAD && nFree >= 7u) {
        uint32_t wPreviousRandom = ptGame->wRandomState;
        uint32_t wRandom = __game_random(ptGame);
        unsigned nPattern = ptGame->wSectionIndex < 3u
                            ? (unsigned)ptGame->wSectionIndex
                            : 1u + (wRandom >> 16) % 4u;
        int32_t lX = ptGame->lNextSectionX
                     + (int32_t)((wRandom >> 8) % 3u) * 8;
        unsigned nCount = c_chRouteObjectCounts[nPattern];

        if (nFree < nCount) {
            /* Retry the same complete route once space is available. */
            ptGame->wRandomState = wPreviousRandom;
            break;
        }

        for (unsigned n = 0; n < nCount; n++) {
            const game_route_object_t *ptObject = &c_tRoutes[nPattern][n];
            __game_add_object(ptGame, lX + ptObject->iX, ptObject->chRise,
                               ptObject->chWidth,
                               ptObject->chType == PLATFORMER_GAME_PLATFORM
                                   ? 10u : PLATFORMER_GAME_COOKIE_SIZE,
                               ptObject->chType);
        }
        nFree -= nCount;
        ptGame->wSectionIndex++;
        ptGame->lNextSectionX += (nPattern == 4u ? 720 : 480)
                                + (int32_t)((wRandom >> 24) % 3u) * 32;
    }
}

static bool __game_overlaps_x(const platformer_game_t *ptGame,
                              const platformer_game_object_t *ptObject)
{
    int32_t lLeft = ptGame->lXQ8
                   + PLATFORMER_GAME_BODY_OFFSET_X * PLATFORMER_GAME_Q8;

    return lLeft < (ptObject->lX + ptObject->hwWidth) * PLATFORMER_GAME_Q8
        && lLeft + PLATFORMER_GAME_BODY_WIDTH * PLATFORMER_GAME_Q8
            > ptObject->lX * PLATFORMER_GAME_Q8;
}

static bool __game_is_supported(const platformer_game_t *ptGame)
{
    if (ptGame->lFootYQ8 == ptGame->iGroundY * PLATFORMER_GAME_Q8) {
        return true;
    }
    for (unsigned n = 0; n < PLATFORMER_GAME_OBJECT_COUNT; n++) {
        const platformer_game_object_t *ptObject = &ptGame->tObjects[n];
        if (ptObject->bActive && ptObject->chType == PLATFORMER_GAME_PLATFORM
        &&  ptGame->lFootYQ8 == ptObject->iTop * PLATFORMER_GAME_Q8
        &&  __game_overlaps_x(ptGame, ptObject)) {
            return true;
        }
    }
    return false;
}

static void __game_takeoff(platformer_game_t *ptGame)
{
    ptGame->lVelocityYQ8 = -GAME_JUMP_SPEED_PPS * PLATFORMER_GAME_Q8;
    ptGame->bGrounded = false;
    ptGame->bJumpFlight = true;
    ptGame->bGliding = false;
    ptGame->hwBufferMs = 0;
    ptGame->hwCoyoteMs = 0;
    ptGame->hwJumpAgeMs = 0;
    ptGame->hwLandingAgeMs = UINT16_MAX;
}

static uint16_t __game_count_down(uint16_t hwTime)
{
    return hwTime > PLATFORMER_GAME_STEP_MS
           ? (uint16_t)(hwTime - PLATFORMER_GAME_STEP_MS) : 0u;
}

static void __game_step(platformer_game_t *ptGame, int16_t iSpeedPps)
{
    ptGame->hwComboRemainingMs = __game_count_down(ptGame->hwComboRemainingMs);
    if (!ptGame->hwComboRemainingMs) {
        ptGame->hwCombo = 0;
    }
    if (ptGame->bGlideHeld) {
        if (ptGame->hwGlideHoldMs < GAME_GLIDE_HOLD_MS) {
            ptGame->hwGlideHoldMs += PLATFORMER_GAME_STEP_MS;
        }
    } else {
        ptGame->hwGlideHoldMs = 0;
    }
    ptGame->bGliding = false;
    int32_t lAdvanceQ8 = iSpeedPps * PLATFORMER_GAME_Q8
                        * (int32_t)PLATFORMER_GAME_STEP_MS / 1000;
    int32_t lOldFootQ8 = ptGame->lFootYQ8;

    ptGame->lXQ8 += lAdvanceQ8;
    if (ptGame->bGrounded && !__game_is_supported(ptGame)) {
        ptGame->bGrounded = false;
        ptGame->hwJumpAgeMs = 260u;
        ptGame->hwLandingAgeMs = UINT16_MAX;
    }
    if (ptGame->bGrounded) {
        ptGame->hwCoyoteMs = PLATFORMER_GAME_COYOTE_MS;
    }
    if (ptGame->hwBufferMs && (ptGame->bGrounded || ptGame->hwCoyoteMs)) {
        __game_takeoff(ptGame);
    }

    if (!ptGame->bGrounded) {
        ptGame->bGliding = ptGame->bJumpFlight && ptGame->bGlideHeld
                       && ptGame->bGlideQualified
                       && ptGame->hwGlideHoldMs >= GAME_GLIDE_HOLD_MS
                       && ptGame->lVelocityYQ8 >= 0;
        if (ptGame->bGliding && ptGame->lVelocityYQ8 > GAME_GLIDE_FALL_SPEED_PPS * PLATFORMER_GAME_Q8) {
            ptGame->lVelocityYQ8 = GAME_GLIDE_FALL_SPEED_PPS * PLATFORMER_GAME_Q8;
        }
        int32_t lOldVelocityQ8 = ptGame->lVelocityYQ8;
        int32_t lLandingQ8 = ptGame->iGroundY * PLATFORMER_GAME_Q8;

        ptGame->lVelocityYQ8 += (ptGame->bGliding ? GAME_GLIDE_GRAVITY_PPS2 : GAME_GRAVITY_PPS2) * PLATFORMER_GAME_Q8
                               * (int32_t)PLATFORMER_GAME_STEP_MS / 1000;
        if (ptGame->lVelocityYQ8 > GAME_MAX_FALL_SPEED_PPS * PLATFORMER_GAME_Q8) {
            ptGame->lVelocityYQ8 = GAME_MAX_FALL_SPEED_PPS * PLATFORMER_GAME_Q8;
        }
        if (ptGame->bGliding && ptGame->lVelocityYQ8 > GAME_GLIDE_FALL_SPEED_PPS * PLATFORMER_GAME_Q8) {
            ptGame->lVelocityYQ8 = GAME_GLIDE_FALL_SPEED_PPS * PLATFORMER_GAME_Q8;
        }
        ptGame->lFootYQ8 += (lOldVelocityQ8 + ptGame->lVelocityYQ8)
                           * (int32_t)PLATFORMER_GAME_STEP_MS / 2000;
        if (ptGame->hwJumpAgeMs < 10000u) {
            ptGame->hwJumpAgeMs += PLATFORMER_GAME_STEP_MS;
        }

        if (ptGame->lVelocityYQ8 >= 0) {
            for (unsigned n = 0; n < PLATFORMER_GAME_OBJECT_COUNT; n++) {
                const platformer_game_object_t *ptObject = &ptGame->tObjects[n];
                int32_t lTopQ8 = ptObject->iTop * PLATFORMER_GAME_Q8;

                /* One-way surfaces: only the descending foot crossing lands. */
                if (ptObject->bActive
                &&  ptObject->chType == PLATFORMER_GAME_PLATFORM
                &&  lOldFootQ8 <= lTopQ8 && ptGame->lFootYQ8 >= lTopQ8
                &&  lTopQ8 < lLandingQ8 && __game_overlaps_x(ptGame, ptObject)) {
                    lLandingQ8 = lTopQ8;
                }
            }
            if (ptGame->lFootYQ8 >= lLandingQ8) {
                ptGame->lFootYQ8 = lLandingQ8;
                ptGame->lVelocityYQ8 = 0;
                ptGame->bGrounded = true;
                ptGame->bGliding = false;
                ptGame->bJumpFlight = false;
                ptGame->hwCoyoteMs = PLATFORMER_GAME_COYOTE_MS;
                ptGame->hwLandingAgeMs = 0;
                if (ptGame->hwBufferMs) {
                    __game_takeoff(ptGame);
                }
            }
        }
    } else if (ptGame->hwLandingAgeMs < 400u) {
        ptGame->hwLandingAgeMs += PLATFORMER_GAME_STEP_MS;
    }

    if (!ptGame->bGrounded) {
        ptGame->hwCoyoteMs = __game_count_down(ptGame->hwCoyoteMs);
    }
    ptGame->hwBufferMs = __game_count_down(ptGame->hwBufferMs);

    for (unsigned n = 0; n < PLATFORMER_GAME_OBJECT_COUNT; n++) {
        platformer_game_object_t *ptObject = &ptGame->tObjects[n];
        if (ptObject->bActive && ptObject->chType == PLATFORMER_GAME_COOKIE
        &&  __game_overlaps_x(ptGame, ptObject)
        &&  ptGame->lFootYQ8 > ptObject->iTop * PLATFORMER_GAME_Q8
        &&  ptGame->lFootYQ8 - PLATFORMER_GAME_BODY_HEIGHT * PLATFORMER_GAME_Q8
            < (ptObject->iTop + ptObject->chHeight) * PLATFORMER_GAME_Q8) {
            ptObject->bActive = false;
            if (ptGame->hwCombo < PLATFORMER_GAME_COMBO_MAX) {
                ptGame->hwCombo++;
            }
            ptGame->hwComboRemainingMs = PLATFORMER_GAME_COMBO_WINDOW_MS;
            if (ptGame->wCookies < 999999u) {
                ptGame->wCookies++;
            }
        }
    }
}

void platformer_game_init(platformer_game_t *ptGame, int16_t iGroundY,
                          int16_t iStartX, uint32_t wNowMs)
{
    memset(ptGame, 0, sizeof(*ptGame));
    ptGame->iGroundY = iGroundY;
    ptGame->lXQ8 = iStartX * PLATFORMER_GAME_Q8;
    ptGame->lFootYQ8 = iGroundY * PLATFORMER_GAME_Q8;
    ptGame->wLastUpdateMs = wNowMs;
    ptGame->lNextSectionX = iStartX + 180;
    ptGame->wRandomState = GAME_SEED ^ wNowMs;
    ptGame->bGrounded = true;
    ptGame->hwCoyoteMs = PLATFORMER_GAME_COYOTE_MS;
    ptGame->bJumpArmed = true;
    ptGame->hwLandingAgeMs = UINT16_MAX;
    __game_generate_course(ptGame);
}

void platformer_game_update(platformer_game_t *ptGame, uint32_t wNowMs,
                            int16_t iSpeedPps, bool bJumpPressed)
{
    uint32_t wElapsedMs = (uint32_t)(wNowMs - ptGame->wLastUpdateMs);
    ptGame->wLastUpdateMs = wNowMs;
    if (iSpeedPps < 0) {
        iSpeedPps = 0;
    } else if (iSpeedPps > PLATFORMER_GAME_MAX_SPEED_PPS) {
        iSpeedPps = PLATFORMER_GAME_MAX_SPEED_PPS;
    }
    /* Require a released sample between accepted presses. A repeated event
     * while held must not refill the landing buffer and auto-jump again. */
    if (!ptGame->bGlideHeld && !bJumpPressed) {
        ptGame->bJumpArmed = true;
    }
    if (bJumpPressed && ptGame->bJumpArmed) {
        ptGame->bJumpArmed = false;
        ptGame->hwBufferMs = PLATFORMER_GAME_BUFFER_MS;
    }
    if (wElapsedMs > GAME_MAX_CATCHUP_MS) {
        wElapsedMs = GAME_MAX_CATCHUP_MS;
    }
    ptGame->hwAccumulatorMs += (uint16_t)wElapsedMs;
    while (ptGame->hwAccumulatorMs >= PLATFORMER_GAME_STEP_MS) {
        ptGame->hwAccumulatorMs -= PLATFORMER_GAME_STEP_MS;
        __game_step(ptGame, iSpeedPps);
    }
    /* Recycle/generate when the lookahead is consumed, not at every 10 ms step. */
    if (ptGame->lNextSectionX <= ptGame->lXQ8 / PLATFORMER_GAME_Q8 + GAME_LOOKAHEAD) {
        __game_generate_course(ptGame);
    }
}

void platformer_game_rebase(platformer_game_t *ptGame, int32_t lOffset)
{
    ptGame->lXQ8 -= lOffset * PLATFORMER_GAME_Q8;
    ptGame->lNextSectionX -= lOffset;
    for (unsigned n = 0; n < PLATFORMER_GAME_OBJECT_COUNT; n++) {
        if (ptGame->tObjects[n].bActive) {
            ptGame->tObjects[n].lX -= lOffset;
        }
    }
}

uint32_t platformer_game_score(const platformer_game_t *ptGame)
{
    return ptGame->wCookies;
}

int8_t platformer_game_jump_frame(const platformer_game_t *ptGame)
{
    if (!ptGame->bGrounded) {
        if (ptGame->hwJumpAgeMs < 30u) {
            return 0;
        }
        if (ptGame->lVelocityYQ8 < -80 * PLATFORMER_GAME_Q8) {
            unsigned nFrame = 1u + (ptGame->hwJumpAgeMs - 30u) / 60u;
            return (int8_t)(nFrame > 3u ? 3u : nFrame);
        }
        if (ptGame->lVelocityYQ8 < 80 * PLATFORMER_GAME_Q8) {
            return 4;
        }
        return ptGame->lVelocityYQ8 < 300 * PLATFORMER_GAME_Q8 ? 5 : 6;
    }
    if (ptGame->hwLandingAgeMs < 400u) {
        return (int8_t)(7u + ptGame->hwLandingAgeMs / 80u);
    }
    return -1;
}
