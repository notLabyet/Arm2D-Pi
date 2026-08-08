/*
 * Copyright (c) 2009-2025 Arm Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __USER_GENERIC_LOADER_FLIP_H__
#define __USER_GENERIC_LOADER_FLIP_H__

#if defined(_RTE_)
#   include "RTE_Components.h"
#endif

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB) && defined(RTE_Acceleration_Arm_2D_Extra_Loader)

#include "arm_2d_helper.h"
#include "arm_2d_example_controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#   pragma clang diagnostic ignored "-Wmicrosoft-anon-tag"
#   pragma clang diagnostic ignored "-Wpadded"
#endif

#ifdef __flip_sim_IMPLEMENT__
#   undef   __flip_sim_IMPLEMENT__
#   define  __ARM_2D_IMPL__
#elif defined(__flip_sim_INHERIT__)
#   undef   __flip_sim_INHERIT__
#   define __ARM_2D_INHERIT__
#endif
#include "arm_2d_utils.h"

#ifndef FLIP_SIM_DEFAULT_RENDER_MODE
#   define FLIP_SIM_DEFAULT_RENDER_MODE    FLIP_SIM_RENDER_PARTICLE
#endif

#ifndef FLIP_SIM_PARTICLE_RED_SPEED_Q16
#   define FLIP_SIM_PARTICLE_RED_SPEED_Q16 131072u     /* 2.0 */
#endif

#if FLIP_SIM_PARTICLE_RED_SPEED_Q16 == 0
#   error FLIP_SIM_PARTICLE_RED_SPEED_Q16 must be greater than zero
#endif

typedef enum flip_sim_render_mode_t {
    FLIP_SIM_RENDER_PARTICLE = 0,
    FLIP_SIM_RENDER_SURFACE,
} flip_sim_render_mode_t;

typedef struct flip_sim_cfg_t {
    arm_2d_size_t tSize;

    uint16_t bUseHeapForVRES        : 1;
    uint16_t bAntiAlias             : 1;

    uint8_t  Sreen_X;
    uint8_t  Sreen_Y;

    struct {
        const arm_loader_io_t *ptIO;
        uintptr_t pTarget;
    } ImageIO;

    arm_2d_scene_t *ptScene;
} flip_sim_cfg_t;

typedef struct flip_sim_t flip_sim_t;

struct flip_sim_t {
    union {
        arm_2d_tile_t tTile;
        inherit(arm_generic_loader_t);
    };

ARM_PRIVATE(
    flip_sim_cfg_t tCFG;
    flip_sim_render_mode_t tRenderMode;
)
};

extern
ARM_NONNULL(1, 2)
arm_2d_err_t flip_sim_init(flip_sim_t *ptThis, flip_sim_cfg_t *ptCFG);

extern
ARM_NONNULL(1)
void flip_sim_depose(flip_sim_t *ptThis);

extern
ARM_NONNULL(1)
void flip_sim_on_load(flip_sim_t *ptThis);

extern
ARM_NONNULL(1)
void flip_sim_on_frame_start(flip_sim_t *ptThis);

extern
ARM_NONNULL(1)
void flip_sim_on_frame_complete(flip_sim_t *ptThis);

extern
ARM_NONNULL(1)
void flip_sim_set_render_mode(flip_sim_t *ptThis,
                              flip_sim_render_mode_t tMode);

extern
ARM_NONNULL(1)
flip_sim_render_mode_t flip_sim_get_render_mode(const flip_sim_t *ptThis);

extern
ARM_NONNULL(1)
void flip_sim_toggle_render_mode(flip_sim_t *ptThis);

extern
ARM_NONNULL(1)
void flip_sim_show(flip_sim_t *ptThis,
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

#define flip_sim_init(...)                 ARM_2D_ERR_NOT_AVAILABLE
#define flip_sim_depose(...)
#define flip_sim_on_load(...)
#define flip_sim_on_frame_start(...)
#define flip_sim_on_frame_complete(...)
#define flip_sim_set_render_mode(...)
#define flip_sim_get_render_mode(...)      0
#define flip_sim_toggle_render_mode(...)

#endif

#endif
