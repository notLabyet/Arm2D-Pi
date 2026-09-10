/*
 * Copyright (c) 2009-2025 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __USER_GENERIC_LOADER_INFINITE_CORRIDOR_H__
#define __USER_GENERIC_LOADER_INFINITE_CORRIDOR_H__

/*============================ INCLUDES ======================================*/
#if defined(_RTE_)
#   include "RTE_Components.h"
#endif

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB) && defined(RTE_Acceleration_Arm_2D_Extra_Loader)

#include "arm_2d_helper.h"
#include "arm_2d_example_loaders.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#   pragma clang diagnostic ignored "-Wmicrosoft-anon-tag"
#   pragma clang diagnostic ignored "-Wpadded"
#endif

/* OOC header, please DO NOT modify */
#ifdef __USER_GENERIC_LOADER_INFINITE_CORRIDOR_IMPLEMENT__
#   undef __USER_GENERIC_LOADER_INFINITE_CORRIDOR_IMPLEMENT__
#   define __ARM_2D_IMPL__
#elif defined(__USER_GENERIC_LOADER_INFINITE_CORRIDOR_INHERIT__)
#   undef __USER_GENERIC_LOADER_INFINITE_CORRIDOR_INHERIT__
#   define __ARM_2D_INHERIT__
#endif
#include "arm_2d_utils.h"

/*============================ MACROS ========================================*/

#define USER_INFINITE_CORRIDOR_FRAME_COUNT       11
#define USER_INFINITE_CORRIDOR_SHADE_COUNT        3

/*============================ TYPES =========================================*/

typedef struct user_generic_loader_infinite_corridor_cfg_t {
    arm_2d_size_t tSize;
    uint16_t bUseHeapForVRES : 1;

    struct {
        const arm_loader_io_t *ptIO;
        uintptr_t pTarget;
    } ImageIO;

    arm_2d_scene_t *ptScene;
} user_generic_loader_infinite_corridor_cfg_t;

typedef struct user_generic_loader_infinite_corridor_t
    user_generic_loader_infinite_corridor_t;

struct user_generic_loader_infinite_corridor_t {
    union {
        arm_2d_tile_t tTile;
        inherit(arm_generic_loader_t);
    };

ARM_PRIVATE(
    user_generic_loader_infinite_corridor_cfg_t tCFG;
    struct {
        int16_t iScaleXQ8;
        int16_t iShearQ8;
        int16_t iFrameOffsetX[USER_INFINITE_CORRIDOR_FRAME_COUNT];
        int16_t iFrameOffsetY[USER_INFINITE_CORRIDOR_FRAME_COUNT];
        uint16_t hwRadius[USER_INFINITE_CORRIDOR_FRAME_COUNT];
        uint8_t chHalfWidth[USER_INFINITE_CORRIDOR_FRAME_COUNT];
        uint16_t hwColour
            [USER_INFINITE_CORRIDOR_FRAME_COUNT]
            [USER_INFINITE_CORRIDOR_SHADE_COUNT];
    } Runtime;
)
};

/*============================ PROTOTYPES ====================================*/

extern
ARM_NONNULL(1, 2)
arm_2d_err_t user_generic_loader_infinite_corridor_init(
                        user_generic_loader_infinite_corridor_t *ptThis,
                        user_generic_loader_infinite_corridor_cfg_t *ptCFG);

extern
ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_depose(
                        user_generic_loader_infinite_corridor_t *ptThis);

extern
ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_on_load(
                        user_generic_loader_infinite_corridor_t *ptThis);

extern
ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_on_frame_start(
                        user_generic_loader_infinite_corridor_t *ptThis);

extern
ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_on_frame_complete(
                        user_generic_loader_infinite_corridor_t *ptThis);

extern
ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_update(
                        user_generic_loader_infinite_corridor_t *ptThis,
                        uint32_t wElapsedInMs);

extern
ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_show(
                        user_generic_loader_infinite_corridor_t *ptThis,
                        const arm_2d_tile_t *ptTile,
                        const arm_2d_region_t *ptRegion,
                        bool bIsNewFrame);

#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#ifdef __cplusplus
}
#endif

#else

#define user_generic_loader_infinite_corridor_init(...)               ARM_2D_ERR_NOT_AVAILABLE
#define user_generic_loader_infinite_corridor_depose(...)
#define user_generic_loader_infinite_corridor_on_load(...)
#define user_generic_loader_infinite_corridor_on_frame_start(...)
#define user_generic_loader_infinite_corridor_on_frame_complete(...)
#define user_generic_loader_infinite_corridor_update(...)
#define user_generic_loader_infinite_corridor_show(...)

#endif

#endif
