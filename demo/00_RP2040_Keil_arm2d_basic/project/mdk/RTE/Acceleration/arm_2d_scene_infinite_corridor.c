/*
 * Copyright (c) 2009-2025 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*============================ INCLUDES ======================================*/

#define __USER_SCENE_INFINITE_CORRIDOR_IMPLEMENT__
#include "arm_2d_scene_infinite_corridor.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#include <stdlib.h>
#include <string.h>

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wunknown-warning-option"
#   pragma clang diagnostic ignored "-Wreserved-identifier"
#   pragma clang diagnostic ignored "-Wsign-conversion"
#   pragma clang diagnostic ignored "-Wpadded"
#   pragma clang diagnostic ignored "-Wcast-qual"
#   pragma clang diagnostic ignored "-Wcast-align"
#   pragma clang diagnostic ignored "-Wmissing-field-initializers"
#   pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#   pragma clang diagnostic ignored "-Wmissing-prototypes"
#   pragma clang diagnostic ignored "-Wunused-variable"
#   pragma clang diagnostic ignored "-Wgnu-statement-expression"
#   pragma clang diagnostic ignored "-Wdeclaration-after-statement"
#   pragma clang diagnostic ignored "-Wunused-function"
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#elif __IS_COMPILER_ARM_COMPILER_5__
#   pragma diag_suppress 64,177
#elif __IS_COMPILER_IAR__
#   pragma diag_suppress=Pa089,Pe188,Pe177,Pe174
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wformat="
#   pragma GCC diagnostic ignored "-Wpedantic"
#   pragma GCC diagnostic ignored "-Wunused-function"
#   pragma GCC diagnostic ignored "-Wunused-variable"
#   pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#endif

/*============================ MACROS ========================================*/
/*============================ MACROFIED FUNCTIONS ===========================*/

#undef this
#define this (*ptThis)

/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/
/*============================ LOCAL VARIABLES ===============================*/
/*============================ IMPLEMENTATION ================================*/

static void __on_scene_infinite_corridor_load(arm_2d_scene_t *ptScene)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)ptScene;

    this.lTimestamp[0] = arm_2d_helper_get_system_timestamp();
    user_generic_loader_infinite_corridor_update(&this.tCorridor, 0u);
    user_generic_loader_infinite_corridor_on_load(&this.tCorridor);
}

static void __after_scene_infinite_corridor_switching(arm_2d_scene_t *ptScene)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
}

static void __on_scene_infinite_corridor_depose(arm_2d_scene_t *ptScene)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)ptScene;

    user_generic_loader_infinite_corridor_depose(&this.tCorridor);

    arm_foreach(int64_t, this.lTimestamp, ptItem) {
        *ptItem = 0;
    }
    ptScene->ptPlayer = NULL;
    if (!this.bUserAllocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, ptScene);
    }
}

static void __on_scene_infinite_corridor_background_start(
                                                    arm_2d_scene_t *ptScene)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
}

static void __on_scene_infinite_corridor_background_complete(
                                                    arm_2d_scene_t *ptScene)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
}

static void __on_scene_infinite_corridor_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)ptScene;
    int64_t lElapsed = arm_2d_helper_get_system_timestamp()
                     - this.lTimestamp[0];
    uint32_t wElapsedInMs = (uint32_t)
        arm_2d_helper_convert_ticks_to_ms(lElapsed);

    user_generic_loader_infinite_corridor_update(&this.tCorridor,
                                                  wElapsedInMs);
    user_generic_loader_infinite_corridor_on_frame_start(&this.tCorridor);
}

static void __on_scene_infinite_corridor_frame_complete(
                                                    arm_2d_scene_t *ptScene)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)ptScene;

    user_generic_loader_infinite_corridor_on_frame_complete(&this.tCorridor);
}

static void __before_scene_infinite_corridor_switching_out(
                                                    arm_2d_scene_t *ptScene)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
}

static
IMPL_PFB_ON_DRAW(__pfb_draw_scene_infinite_corridor_background_handler)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)pTarget;
    ARM_2D_UNUSED(ptThis);
    ARM_2D_UNUSED(ptTile);
    ARM_2D_UNUSED(bIsNewFrame);

    return arm_fsm_rt_cpl;
}

static
IMPL_PFB_ON_DRAW(__pfb_draw_scene_infinite_corridor_handler)
{
    user_scene_infinite_corridor_t *ptThis =
        (user_scene_infinite_corridor_t *)pTarget;

    arm_2d_canvas(ptTile, __top_canvas) {
        arm_2d_align_centre(__top_canvas, 240, 240) {
            user_generic_loader_infinite_corridor_show(
                &this.tCorridor,
                ptTile,
                &__centre_region,
                bIsNewFrame);
        }
    }

    ARM_2D_OP_WAIT_ASYNC();
    return arm_fsm_rt_cpl;
}

ARM_NONNULL(1)
user_scene_infinite_corridor_t *__arm_2d_scene_infinite_corridor_init(
                                    arm_2d_scene_player_t *ptDispAdapter,
                                    user_scene_infinite_corridor_t *ptThis)
{
    bool bUserAllocated = false;
    assert(NULL != ptDispAdapter);

    if (NULL == ptThis) {
        ptThis = (user_scene_infinite_corridor_t *)
                    __arm_2d_allocate_scratch_memory(
                        sizeof(user_scene_infinite_corridor_t),
                        __alignof__(user_scene_infinite_corridor_t),
                        ARM_2D_MEM_TYPE_UNSPECIFIED);
        assert(NULL != ptThis);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
    }
    memset(ptThis, 0, sizeof(user_scene_infinite_corridor_t));

    *ptThis = (user_scene_infinite_corridor_t) {
        .use_as__arm_2d_scene_t = {
            .tCanvas = {GLCD_COLOR_BLACK},
            .fnOnLoad = &__on_scene_infinite_corridor_load,
            //.fnBackground =
            //    &__pfb_draw_scene_infinite_corridor_background_handler,
            .fnScene = &__pfb_draw_scene_infinite_corridor_handler,
            .fnAfterSwitch = &__after_scene_infinite_corridor_switching,
            //.fnOnBGStart = &__on_scene_infinite_corridor_background_start,
            //.fnOnBGComplete =
            //    &__on_scene_infinite_corridor_background_complete,
            .fnOnFrameStart = &__on_scene_infinite_corridor_frame_start,
            .fnBeforeSwitchOut =
                &__before_scene_infinite_corridor_switching_out,
            .fnOnFrameCPL = &__on_scene_infinite_corridor_frame_complete,
            .fnDepose = &__on_scene_infinite_corridor_depose,
            .bUseDirtyRegionHelper = false,
        },
        .bUserAllocated = bUserAllocated,
    };

    user_generic_loader_infinite_corridor_cfg_t tCFG = {
        .tSize = {
            .iWidth = 240,
            .iHeight = 240,
        },
        .ptScene = &this.use_as__arm_2d_scene_t,
    };
    arm_2d_err_t tResult = user_generic_loader_infinite_corridor_init(
                                            &this.tCorridor,
                                            &tCFG);
    assert(ARM_2D_ERR_NONE == tResult);
    ARM_2D_UNUSED(tResult);

    arm_2d_scene_player_append_scenes(ptDispAdapter,
                                      &this.use_as__arm_2d_scene_t,
                                      1);

    return ptThis;
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic pop
#endif

#endif
