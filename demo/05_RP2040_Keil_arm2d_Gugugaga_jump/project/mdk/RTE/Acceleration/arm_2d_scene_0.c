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

#define __USER_SCENE0_IMPLEMENT__
#include "arm_2d_scene_0.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#ifdef RTE_Acceleration_Arm_2D_Scene0

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

#if __GLCD_CFG_COLOUR_DEPTH__ == 8

#   define c_tileCMSISLogo          c_tileCMSISLogoGRAY8

#elif __GLCD_CFG_COLOUR_DEPTH__ == 16

#   define c_tileCMSISLogo          c_tileCMSISLogoRGB565

#elif __GLCD_CFG_COLOUR_DEPTH__ == 32

#   define c_tileCMSISLogo          c_tileCMSISLogoCCCA8888
#else
#   error Unsupported colour depth!
#endif

#if __USER_SCENE0_USE_LMSK__
#   define CMSIS_LOGO_MASK          ARM_LMSK_TILE(SCENE0_LMSK_CMSIS)
#else
#   define CMSIS_LOGO_MASK          c_tileCMSISLogoMask
#endif

/*============================ MACROFIED FUNCTIONS ===========================*/
#undef this
#define this (*ptThis)

/*============================ TYPES =========================================*/

typedef struct scene0_arc_demo_cfg_t {
    arm_2d_location_t tStartPoint;
    uint16_t hwRadius;
    uint16_t hwRingWidth;
    uint16_t hwColour;
    int16_t iMinSweepAngle;
    int16_t iMaxSweepAngle;
    uint16_t hwPeriodInMs;
    uint16_t hwPhaseInMs;
    int8_t chDirection;
} scene0_arc_demo_cfg_t;

/*============================ GLOBAL VARIABLES ==============================*/

extern const arm_2d_tile_t c_tileevaMask;
#if __USER_SCENE0_ENABLE_3D__
ThD_sim_t  ThD_sim;
#endif
/*============================ PROTOTYPES ====================================*/
/*============================ LOCAL VARIABLES ===============================*/

static const scene0_arc_demo_cfg_t c_tArcDemoCFG[USER_SCENE0_ARC_COUNT] = {
    {{120,  10}, 110, 7, GLCD_COLOR_RED,      40, 330, 4200,    0,  1},
    {{216, 120},  96, 7, GLCD_COLOR_ORANGE,   70, 300, 5100,  850, -1},
    {{120, 202},  82, 7, GLCD_COLOR_YELLOW,  100, 345, 6000, 1600,  1},
    {{ 52, 120},  68, 7, GLCD_COLOR_GREEN,    50, 270, 6900, 2400, -1},
    {{158,  82},  54, 7, GLCD_COLOR_CYAN,    120, 320, 7800, 3200,  1},
    {{148, 148},  40, 7, GLCD_COLOR_BLUE,     80, 290, 8700, 4000, -1},
    {{102, 102},  26, 20, GLCD_COLOR_MAGENTA,  30, 250, 9600, 4800,  1},
};

/*============================ IMPLEMENTATION ================================*/

static int16_t __scene0_get_arc_sweep(uint_fast8_t chIndex,
                                      uint32_t wElapsedInMs)
{
    const scene0_arc_demo_cfg_t *ptCFG = &c_tArcDemoCFG[chIndex];
    uint32_t wPeriod = ptCFG->hwPeriodInMs;
    uint32_t wHalfPeriod = wPeriod >> 1;
    uint32_t wPhase = (wElapsedInMs + ptCFG->hwPhaseInMs) % wPeriod;
    uint32_t wStroke = (wPhase <= wHalfPeriod)
                     ? wPhase
                     : wPeriod - wPhase;
    int32_t iSweep = ptCFG->iMinSweepAngle
                   + ((ptCFG->iMaxSweepAngle - ptCFG->iMinSweepAngle)
                    * (int32_t)wStroke) / (int32_t)wHalfPeriod;

    return (ptCFG->chDirection < 0) ? (int16_t)-iSweep : (int16_t)iSweep;
}

static void __on_scene0_load(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
#if __USER_SCENE0_ENABLE_3D__
    ThD_sim_on_load(&ThD_sim);
#endif
    this.lTimestamp[0] = arm_2d_helper_get_system_timestamp();
    arm_2d_helper_dirty_region_add_items(
        &this.use_as__arm_2d_scene_t.tDirtyRegionHelper,
        this.tArcDirtyRegionItem,
        USER_SCENE0_ARC_COUNT);
    for (uint_fast8_t n = 0; n < USER_SCENE0_ARC_COUNT; n++) {
        arm_2d_helper_dirty_region_item_force_to_use_minimal_enclosure(
            &this.tArcDirtyRegionItem[n], true);
        user_generic_loader_arc_on_load(&this.tArcLoader[n]);
    }

#if __USER_SCENE0_USE_LMSK__ 
    ARM_LMSK_GROUP_ON_LOAD();
#endif
}

static void __after_scene0_switching(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static void __on_scene0_depose(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
    arm_2d_helper_dirty_region_remove_items(
        &this.use_as__arm_2d_scene_t.tDirtyRegionHelper,
        this.tArcDirtyRegionItem,
        USER_SCENE0_ARC_COUNT);
    for (uint_fast8_t n = 0; n < USER_SCENE0_ARC_COUNT; n++) {
        user_generic_loader_arc_depose(&this.tArcLoader[n]);
    }
#if __USER_SCENE0_ENABLE_3D__
    ThD_sim_depose(&ThD_sim);
#endif

    /*--------------------- insert your depose code begin --------------------*/
    
#if __USER_SCENE0_USE_LMSK__ 
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
 * Scene 0                                                                    *
 *----------------------------------------------------------------------------*/

#if 0 /* deprecated */
static void __on_scene0_background_start(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static void __on_scene0_background_complete(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}
#endif


static void __on_scene0_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
#if __USER_SCENE0_ENABLE_3D__
    ThD_sim_on_frame_start(&ThD_sim);
#endif

    int64_t lElapsed = arm_2d_helper_get_system_timestamp()
                     - this.lTimestamp[0];
    uint32_t wElapsedInMs = (uint32_t)
        arm_2d_helper_convert_ticks_to_ms(lElapsed);

    for (uint_fast8_t n = 0; n < USER_SCENE0_ARC_COUNT; n++) {
        this.tArcParam[n].iSweepAngle =
            __scene0_get_arc_sweep(n, wElapsedInMs);
        (void)user_generic_loader_arc_set(&this.tArcLoader[n],
                                          &this.tArcParam[n]);
        user_generic_loader_arc_on_frame_start(&this.tArcLoader[n]);
    }
#if __USER_SCENE0_USE_LMSK__ 
    ARM_LMSK_GROUP_ON_FRAME_START();
#endif
}

static void __on_scene0_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

#if __USER_SCENE0_ENABLE_3D__
    ThD_sim_on_frame_complete(&ThD_sim);
#endif
    for (uint_fast8_t n = 0; n < USER_SCENE0_ARC_COUNT; n++) {
        user_generic_loader_arc_on_frame_complete(&this.tArcLoader[n]);
    }

#if __USER_SCENE0_USE_LMSK__ 
    ARM_LMSK_GROUP_ON_FRAME_COMPLETE();
#endif
}

static void __before_scene0_switching_out(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static
IMPL_PFB_ON_DRAW(__pfb_draw_scene0_handler)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)pTarget;

    ARM_2D_UNUSED(ptTile);
    ARM_2D_UNUSED(bIsNewFrame);
    
    arm_2d_canvas(ptTile, __top_canvas) {
    /*-----------------------draw the scene begin-----------------------*/
     arm_2d_canvas(ptTile, __top_canvas) {
        arm_2d_align_centre(__top_canvas,240, 240) {
#if __USER_SCENE0_ENABLE_3D__
           ThD_sim_show(&ThD_sim,ptTile,&__centre_region,1);
#endif
           for (uint_fast8_t n = 0; n < USER_SCENE0_ARC_COUNT; n++) {
               arm_2d_region_t tDirtyRegion;
               (void)user_generic_loader_arc_get_dirty_region(
                   &this.tArcLoader[n], &tDirtyRegion);
               tDirtyRegion.tLocation.iX += __centre_region.tLocation.iX;
               tDirtyRegion.tLocation.iY += __centre_region.tLocation.iY;
               const arm_2d_region_t *ptDirtyRegion =
                   ((tDirtyRegion.tSize.iWidth > 0)
                 && (tDirtyRegion.tSize.iHeight > 0))
                 ? &tDirtyRegion
                 : NULL;
               arm_2d_helper_dirty_region_update_item(
                   &this.tArcDirtyRegionItem[n],
                   (arm_2d_tile_t *)ptTile,
                   &__centre_region,
                   ptDirtyRegion);
               user_generic_loader_arc_show(
                   &this.tArcLoader[n],
                   ptTile,
                   &__centre_region,
                   bIsNewFrame);
           }
      }
      /*-----------------------draw the scene end  -----------------------*/     
      /*-----------------------draw the scene end  -----------------------*/
    }       
        /* following code is just a demo, you can remove them */

                                    
    }
    ARM_2D_OP_WAIT_ASYNC();

    return arm_fsm_rt_cpl;
}

ARM_NONNULL(1)
user_scene_0_t *__arm_2d_scene0_init(   arm_2d_scene_player_t *ptDispAdapter, 
                                        user_scene_0_t *ptThis)
{
    bool bUserAllocated = false;
    assert(NULL != ptDispAdapter);

    if (NULL == ptThis) {
        ptThis = (user_scene_0_t *)
                    __arm_2d_allocate_scratch_memory(   sizeof(user_scene_0_t),
                                                        __alignof__(user_scene_0_t),
                                                        ARM_2D_MEM_TYPE_UNSPECIFIED);
        assert(NULL != ptThis);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
    }
    memset(ptThis, 0, sizeof(user_scene_0_t));

    *ptThis = (user_scene_0_t){
        .use_as__arm_2d_scene_t = {

            /* the canvas colour */
            .tCanvas = {GLCD_COLOR_BLACK}, 

            /* Please uncommon the callbacks if you need them
             */
            .fnOnLoad       = &__on_scene0_load,
            .fnScene        = &__pfb_draw_scene0_handler,
            .fnAfterSwitch  = &__after_scene0_switching,

            /* if you want to use predefined dirty region list, please uncomment the following code */
            //.ptDirtyRegion  = (arm_2d_region_list_item_t *)s_tDirtyRegions,
            
            //.fnOnBGStart    = &__on_scene0_background_start,         /* deprecated */
            //.fnOnBGComplete = &__on_scene0_background_complete,      /* deprecated */
            .fnOnFrameStart = &__on_scene0_frame_start,
            .fnBeforeSwitchOut = &__before_scene0_switching_out,
            .fnOnFrameCPL   = &__on_scene0_frame_complete,
            .fnDepose       = &__on_scene0_depose,

            .bUseDirtyRegionHelper = true,
        },
        .bUserAllocated = bUserAllocated,
    };

    /* ------------   initialize members of user_scene_0_t begin ---------------*/
#if __USER_SCENE0_ENABLE_3D__
    ThD_sim_cfg_t  tCFG = {
		.tSize.iHeight = 240,
		.tSize.iWidth  = 240,
        .q16DepthFogStrength = Q16(1.5f),
		.ptScene = &this.use_as__arm_2d_scene_t,
    };
    ThD_sim_init(&ThD_sim,&tCFG);
#endif

    for (uint_fast8_t n = 0; n < USER_SCENE0_ARC_COUNT; n++) {
        const scene0_arc_demo_cfg_t *ptDemoCFG = &c_tArcDemoCFG[n];
        this.tArcParam[n] = (user_generic_loader_arc_param_t) {
            .tCenter = {
                .iX = 120,
                .iY = 120,
            },
            .tStartPoint = ptDemoCFG->tStartPoint,
            .hwRadius = ptDemoCFG->hwRadius,
            .hwRingWidth = ptDemoCFG->hwRingWidth,
            .hwColour = ptDemoCFG->hwColour,
            .iSweepAngle = ptDemoCFG->chDirection
                         * ptDemoCFG->iMinSweepAngle,
        };

        user_generic_loader_arc_cfg_t tArcCFG = {
            .tSize = {
                .iWidth = 240,
                .iHeight = 240,
            },
            .ptScene = &this.use_as__arm_2d_scene_t,
            .tArc = this.tArcParam[n],
        };
        arm_2d_err_t tResult = user_generic_loader_arc_init(
            &this.tArcLoader[n],
            &tArcCFG);
        assert(ARM_2D_ERR_NONE == tResult);
        ARM_2D_UNUSED(tResult);
    }

#if __USER_SCENE0_USE_LMSK__ 
    ARM_LMSK_ITEM_INIT_WITH_ROM(SCENE0_LMSK_CMSIS, c_lmskCMSISLogo, 1881);
#endif

    /* ------------   initialize members of user_scene_0_t end   ---------------*/

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
