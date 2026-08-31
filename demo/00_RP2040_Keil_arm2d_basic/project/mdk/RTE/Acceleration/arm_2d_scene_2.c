/*
 * Copyright (c) 2009-2024 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*============================ INCLUDES ======================================*/

#define __USER_SCENE2_IMPLEMENT__
#include "arm_2d_scene_2.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#ifdef RTE_Acceleration_Arm_2D_Scene2

#include <stdlib.h>
#include <string.h>
#include "fire_sim.h"
#include "qmi8658_motion.h"
#include "user_generic_loader_fire.h"

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
#   pragma clang diagnostic ignored "-Wimplicit-int-conversion" 
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
static fire_sim_t s_tFireSim;

/*============================ IMPLEMENTATION ================================*/

static void __on_scene2_load(arm_2d_scene_t *ptScene)
{
    user_scene_2_t *ptThis = (user_scene_2_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

    fire_sim_on_load(&s_tFireSim);

#if __USER_SCENE2_USE_LMSK__ 
    ARM_LMSK_GROUP_ON_LOAD();
#endif
}

static void __after_scene2_switching(arm_2d_scene_t *ptScene)
{
    user_scene_2_t *ptThis = (user_scene_2_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static void __on_scene2_depose(arm_2d_scene_t *ptScene)
{
    user_scene_2_t *ptThis = (user_scene_2_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
    
    /*--------------------- insert your depose code begin --------------------*/
    fire_sim_depose(&s_tFireSim);

#if FIRE_USE_ARM2D_BLUR
    ARM_2D_OP_DEPOSE(this.tBlurOP);
#endif

#if __USER_SCENE2_USE_LMSK__ 
    ARM_LMSK_GROUP_DEPOSE();
#endif

    /*---------------------- insert your depose code end  --------------------*/

    /* reset timestamp */
    arm_foreach(int64_t,this.lTimestamp, ptItem) {
        *ptItem = 0;
    }
    ptScene->ptPlayer = NULL;
    if (!this.bUserAllocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, ptScene);
    }
}

/*----------------------------------------------------------------------------*
 * Scene 2                                                                    *
 *----------------------------------------------------------------------------*/

#if 0 /* deprecated */
static void __on_scene2_background_start(arm_2d_scene_t *ptScene)
{
    user_scene_2_t *ptThis = (user_scene_2_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

    fire_sim_on_frame_start(&s_tFireSim);
    fire_sim_update();

}

static void __on_scene2_background_complete(arm_2d_scene_t *ptScene)
{
    user_scene_2_t *ptThis = (user_scene_2_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

    fire_sim_on_frame_complete(&s_tFireSim);

}
#endif


static void __on_scene2_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_2_t *ptThis = (user_scene_2_t *)ptScene;
    qmi8658_fire_motion_t tMotion;
    ARM_2D_UNUSED(ptThis);

    fire_sim_on_frame_start(&s_tFireSim);
    if (qmi8658_motion_get_fire_motion(&tMotion)) {
        fire_sim_set_screen_motion_q15(tMotion.gravity_x_q15,
                                       tMotion.gravity_y_q15,
                                       tMotion.acceleration_x_q15,
                                       tMotion.acceleration_y_q15,
                                       tMotion.disturbance_q15);
    }
    fire_sim_update();

#if __USER_SCENE2_USE_LMSK__ 
    ARM_LMSK_GROUP_ON_FRAME_START();
#endif
}

static void __on_scene2_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_2_t *ptThis = (user_scene_2_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

#if __USER_SCENE2_USE_LMSK__ 
    ARM_LMSK_GROUP_ON_FRAME_COMPLETE();
#endif

    fire_sim_on_frame_complete(&s_tFireSim);

#if FIRE_USE_ARM2D_BLUR
    arm_2dp_filter_iir_blur_depose(&this.tBlurOP);
#endif
}

static void __before_scene2_switching_out(arm_2d_scene_t *ptScene)
{
    user_scene_2_t *ptThis = (user_scene_2_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static
IMPL_PFB_ON_DRAW(__pfb_draw_scene2_handler)
{
    user_scene_2_t *ptThis = (user_scene_2_t *)pTarget;

    arm_2d_canvas(ptTile, __top_canvas) {
        arm_2d_align_centre(__top_canvas, 240, 240) {
            fire_sim_show(&s_tFireSim,
                          ptTile,
                          &__centre_region,
                          bIsNewFrame);

#if FIRE_USE_ARM2D_BLUR
            ARM_2D_OP_WAIT_ASYNC();
            arm_2dp_filter_iir_blur(&this.tBlurOP,
                                    ptTile,
                                    &__centre_region,
                                    FIRE_ARM2D_BLUR_DEGREE);
#endif
        }
    }
    ARM_2D_OP_WAIT_ASYNC();

    return arm_fsm_rt_cpl;
}

ARM_NONNULL(1)
user_scene_2_t *__arm_2d_scene2_init(   arm_2d_scene_player_t *ptDispAdapter, 
                                        user_scene_2_t *ptThis)
{
    bool bUserAllocated = false;
    assert(NULL != ptDispAdapter);

    if (NULL == ptThis) {
        ptThis = (user_scene_2_t *)
                    __arm_2d_allocate_scratch_memory(   sizeof(user_scene_2_t),
                                                        __alignof__(user_scene_2_t),
                                                        ARM_2D_MEM_TYPE_UNSPECIFIED);
        assert(NULL != ptThis);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
    }
    memset(ptThis, 0, sizeof(user_scene_2_t));

    *ptThis = (user_scene_2_t){
        .use_as__arm_2d_scene_t = {

            /* the canvas colour */
            .tCanvas = {GLCD_COLOR_BLACK}, 

            /* Please uncommon the callbacks if you need them
             */
            .fnOnLoad       = &__on_scene2_load,
            .fnScene        = &__pfb_draw_scene2_handler,
            .fnAfterSwitch  = &__after_scene2_switching,

            /* if you want to use predefined dirty region list, please uncomment the following code */
            //.ptDirtyRegion  = (arm_2d_region_list_item_t *)s_tDirtyRegions,
            
            //.fnOnBGStart    = &__on_scene2_background_start,         /* deprecated */
            //.fnOnBGComplete = &__on_scene2_background_complete,      /* deprecated */
            .fnOnFrameStart = &__on_scene2_frame_start,
            .fnBeforeSwitchOut = &__before_scene2_switching_out,
            .fnOnFrameCPL   = &__on_scene2_frame_complete,
            .fnDepose       = &__on_scene2_depose,

            .bUseDirtyRegionHelper = false,
        },
        .bUserAllocated = bUserAllocated,
    };

    /* ------------   initialize members of user_scene_2_t begin ---------------*/

    fire_sim_cfg_t tFireCFG = {
        .tSize = {
            .iWidth = 240,
            .iHeight = 240,
        },
        .ptScene = &this.use_as__arm_2d_scene_t,
    };
    (void)fire_init();
    arm_2d_err_t tResult = fire_sim_init(&s_tFireSim, &tFireCFG);
    assert(ARM_2D_ERR_NONE == tResult);
    ARM_2D_UNUSED(tResult);

#if FIRE_USE_ARM2D_BLUR
    ARM_2D_OP_INIT(this.tBlurOP);
#endif

#if __USER_SCENE2_USE_LMSK__ 
    ARM_LMSK_ITEM_INIT_WITH_ROM(SCENE2_LMSK_CMSIS, c_lmskCMSISLogo, 1881);
#endif

    /* ------------   initialize members of user_scene_2_t end   ---------------*/

    arm_2d_scene_player_append_scenes(  ptDispAdapter, 
                                        &this.use_as__arm_2d_scene_t, 
                                        1);

    return ptThis; 
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#endif

#endif
