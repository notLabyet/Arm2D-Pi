/*
 * Copyright (c) 2009-2024 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ARM_2D_SCENE_PLATFORMER_H__
#define __ARM_2D_SCENE_PLATFORMER_H__

/*============================ INCLUDES ======================================*/

#include "arm_2d.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#include "arm_2d_helper_scene.h"
#include "platformer_game.h"
#include "platformer_speed_control.h"
#include "power_key_audit.h"
#include "../platform/display_profile.h"

#ifdef   __cplusplus
extern "C" {
#endif

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wunknown-warning-option"
#   pragma clang diagnostic ignored "-Wreserved-identifier"
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#   pragma clang diagnostic ignored "-Wpadded"
#elif __IS_COMPILER_ARM_COMPILER_5__
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wformat="
#   pragma GCC diagnostic ignored "-Wpedantic"
#   pragma GCC diagnostic ignored "-Wpadded"
#endif

/*============================ MACROS ========================================*/

#define PLATFORMER_FOREGROUND_ITEM_COUNT         28u
#define PLATFORMER_DIRTY_REGION_ITEM_COUNT         (7u + PLATFORMER_GAME_OBJECT_COUNT)

#ifdef __USER_SCENE_PLATFORMER_IMPLEMENT__
#   undef __USER_SCENE_PLATFORMER_IMPLEMENT__
#   define __ARM_2D_IMPL__
#endif
#include "arm_2d_utils.h"

/*============================ MACROFIED FUNCTIONS ===========================*/

#define arm_2d_scene_platformer_init(__DISP_ADAPTER_PTR, ...)                 \
            __arm_2d_scene_platformer_init(                                  \
                (__DISP_ADAPTER_PTR),                                        \
                (NULL, ##__VA_ARGS__))

/*============================ TYPES =========================================*/

typedef struct user_scene_platformer_t user_scene_platformer_t;

struct user_scene_platformer_t {
    implement(arm_2d_scene_t);                                                //!< derived from arm_2d_scene_t

ARM_PRIVATE(
    bool bUserAllocated;
    bool bWalking;
    bool bRawJumpHeld;
    bool bFrameDrawn;
    uint16_t hwFrameCount;
    uint16_t hwFPS;
    uint16_t hwDrawnFPS;
    power_key_audit_t tKeyAudit;
    uint16_t hwKeyLowMs;
    uint16_t hwKeyHighMs;
    uint16_t hwKeySampleGapMs;
    uint16_t hwCourseRegionMask;
    uint32_t wFPSWindowMs;
    platform_display_profile_t tDisplayProfile;
    uint8_t chPlayingAnimation;
    uint8_t u8AnimationFrame;
    uint8_t chComboPhase;
    uint8_t chComboBar;
    uint8_t chCloudScrollRemainder;
    uint8_t chMidFarScrollRemainder;
    int16_t iCloudScrollX;
    int16_t iMidFarScrollX;
    int64_t lAnimationTimestamp;
    platformer_game_t tGame;
    platformer_speed_control_t tSpeedControl;
    int32_t lPlayerWorldX;
    int32_t lCameraWorldX;
    int32_t lNextForegroundX;
    uint32_t wForegroundRandomState;
    uint32_t wForegroundSequence;
    arm_2d_tile_t tHeroTile;
    arm_2d_tile_t tHeroMask;
    arm_2d_helper_dirty_region_item_t
        tDirtyRegionItems[PLATFORMER_DIRTY_REGION_ITEM_COUNT];
    arm_2d_region_t tPlayfield;
    arm_2d_region_t tGround;
    arm_2d_region_t tPlayerHitBox;
    arm_2d_region_t tCourseRegions[PLATFORMER_GAME_OBJECT_COUNT];
    arm_2d_location_t tPlayerStartLocation;
    arm_2d_location_t tPlayerLocation;
    int32_t lForegroundX[PLATFORMER_FOREGROUND_ITEM_COUNT];
    int16_t iForegroundY[PLATFORMER_FOREGROUND_ITEM_COUNT];
    uint8_t chForegroundTypes[PLATFORMER_FOREGROUND_ITEM_COUNT];
    uint8_t chForegroundFrames[PLATFORMER_FOREGROUND_ITEM_COUNT];
    bool bForegroundActive[PLATFORMER_FOREGROUND_ITEM_COUNT];
)
};

/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/

ARM_NONNULL(1)
extern
user_scene_platformer_t *__arm_2d_scene_platformer_init(
                                        arm_2d_scene_player_t *ptDispAdapter,
                                        user_scene_platformer_t *ptScene);

#if defined(__clang__)
#   pragma clang diagnostic pop
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic pop
#endif

#ifdef   __cplusplus
}
#endif

#endif

#endif
