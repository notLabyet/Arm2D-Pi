/*
 * Copyright (c) 2009-2025 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __USER_GENERIC_LOADER_ARC_H__
#define __USER_GENERIC_LOADER_ARC_H__

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
#ifdef __USER_GENERIC_LOADER_ARC_IMPLEMENT__
#   undef __USER_GENERIC_LOADER_ARC_IMPLEMENT__
#   define __ARM_2D_IMPL__
#elif defined(__USER_GENERIC_LOADER_ARC_INHERIT__)
#   undef __USER_GENERIC_LOADER_ARC_INHERIT__
#   define __ARM_2D_INHERIT__
#endif
#include "arm_2d_utils.h"

/*============================ TYPES =========================================*/
typedef struct user_generic_loader_arc_param_t {
    arm_2d_location_t tCenter;
    arm_2d_location_t tStartPoint;   /* direction only */
    uint16_t hwRadius;               /* outer radius in pixels */
    uint16_t hwRingWidth;            /* grows inward from hwRadius */
    uint16_t hwColour;               /* RGB565 */
    int16_t iSweepAngle;             /* degrees: +CW, -CCW */
    int16_t iSweepAngleQ10;          /* optional tenths of degrees: +CW, -CCW */
    bool bRoundCaps;                 /* round start/end caps for partial arcs */
} user_generic_loader_arc_param_t;

typedef struct user_generic_loader_arc_cfg_t {
    arm_2d_size_t tSize;
    uint16_t bUseHeapForVRES : 1;

    struct {
        const arm_loader_io_t *ptIO;
        uintptr_t pTarget;
    } ImageIO;

    arm_2d_scene_t *ptScene;
    user_generic_loader_arc_param_t tArc;
} user_generic_loader_arc_cfg_t;

typedef struct user_generic_loader_arc_t user_generic_loader_arc_t;

struct user_generic_loader_arc_t {
    union {
        arm_2d_tile_t tTile;
        inherit(arm_generic_loader_t);
    };

ARM_PRIVATE(
    user_generic_loader_arc_cfg_t tCFG;
    struct {
        int16_t iStartXQ14;
        int16_t iStartYQ14;
        int16_t iEndXQ14;
        int16_t iEndYQ14;
        int16_t iTipXQ14;
        int16_t iTipYQ14;
        uint32_t wOuterRadiusSquared;
        uint32_t wOuterBorderRadiusSquared;
        uint32_t wInnerRadiusSquared;
        uint32_t wInnerBorderRadiusSquared;
        arm_2d_region_t tBounds;
        arm_2d_region_t tDirtyRegion;
        uint16_t bVisible : 1;
        uint16_t bFullCircle : 1;
        uint16_t bWideArc : 1;
        uint16_t bParametersValid : 1;
        uint16_t bForceFullDirtyRegion : 1;
    } Runtime;
)
};

/*============================ PROTOTYPES ====================================*/
extern
ARM_NONNULL(1, 2)
arm_2d_err_t user_generic_loader_arc_init(
                                    user_generic_loader_arc_t *ptThis,
                                    user_generic_loader_arc_cfg_t *ptCFG);

extern
ARM_NONNULL(1, 2)
arm_2d_err_t user_generic_loader_arc_set(
                                    user_generic_loader_arc_t *ptThis,
                                    const user_generic_loader_arc_param_t *ptArc);

extern
ARM_NONNULL(1, 2)
arm_2d_err_t user_generic_loader_arc_get_dirty_region(
                                    const user_generic_loader_arc_t *ptThis,
                                    arm_2d_region_t *ptRegion);

extern
ARM_NONNULL(1)
void user_generic_loader_arc_depose(user_generic_loader_arc_t *ptThis);

extern
ARM_NONNULL(1)
void user_generic_loader_arc_on_load(user_generic_loader_arc_t *ptThis);

extern
ARM_NONNULL(1)
void user_generic_loader_arc_on_frame_start(user_generic_loader_arc_t *ptThis);

extern
ARM_NONNULL(1)
void user_generic_loader_arc_on_frame_complete(user_generic_loader_arc_t *ptThis);

extern
ARM_NONNULL(1)
void user_generic_loader_arc_show(user_generic_loader_arc_t *ptThis,
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

#define user_generic_loader_arc_init(...)              ARM_2D_ERR_NOT_AVAILABLE
#define user_generic_loader_arc_set(...)               ARM_2D_ERR_NOT_AVAILABLE
#define user_generic_loader_arc_get_dirty_region(...)  ARM_2D_ERR_NOT_AVAILABLE
#define user_generic_loader_arc_depose(...)
#define user_generic_loader_arc_on_load(...)
#define user_generic_loader_arc_on_frame_start(...)
#define user_generic_loader_arc_on_frame_complete(...)
#define user_generic_loader_arc_show(...)

#endif

#endif
