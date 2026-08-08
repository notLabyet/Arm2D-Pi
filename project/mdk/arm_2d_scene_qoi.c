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

#if defined(_RTE_)
#   include "RTE_Components.h"
#endif

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#include "arm_2d.h"

#define __USER_SCENE_QOI_IMPLEMENT__
#include "arm_2d_scene_qoi.h"

#include "arm_2d_helper.h"

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
#undef this
#define this (*ptThis)

/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/

/*============================ LOCAL FUNCTIONS ==============================*/

static void __on_scene_qoi_load(arm_2d_scene_t *ptScene)
{
    user_scene_qoi_t *ptThis = (user_scene_qoi_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
}

static void __on_scene_qoi_depose(arm_2d_scene_t *ptScene)
{
    user_scene_qoi_t *ptThis = (user_scene_qoi_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

    if (NULL != this.pszImagePath) {
        free(this.pszImagePath);
        this.pszImagePath = NULL;
    }

    arm_qoi_loader_depose(&this.tQOIBackground);

    ptScene->ptPlayer = NULL;

    if (!this.bUserAllocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, ptScene);
    }
}

/*----------------------------------------------------------------------------*
 * Scene qoi                                                                    *
 *----------------------------------------------------------------------------*/

static void __on_scene_qoi_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_qoi_t *ptThis = (user_scene_qoi_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

    arm_qoi_loader_on_frame_start(&this.tQOIBackground);
}

static void __on_scene_qoi_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_qoi_t *ptThis = (user_scene_qoi_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

    arm_qoi_loader_on_frame_complete(&this.tQOIBackground);
}

static
IMPL_PFB_ON_DRAW(__pfb_draw_scene_qoi_handler)
{
    ARM_2D_PARAM(pTarget);
    ARM_2D_PARAM(ptTile);
    ARM_2D_PARAM(bIsNewFrame);

    user_scene_qoi_t *ptThis = (user_scene_qoi_t *)pTarget;
    arm_2d_size_t tScreenSize = ptTile->tRegion.tSize;

    ARM_2D_UNUSED(tScreenSize);

    arm_2d_canvas(ptTile, __top_canvas) {
    /*-----------------------draw the foreground begin-----------------------*/
    
        arm_2d_align_centre(__top_canvas, 
                            this.tQOIBackground.vres.tTile.tRegion.tSize) {

            arm_2d_tile_copy_only(&this.tQOIBackground.vres.tTile,
                                ptTile,
                                &__centre_region);
        }

        /* draw text at the top-left corner */
        arm_lcd_text_set_target_framebuffer((arm_2d_tile_t *)ptTile);
        arm_lcd_text_set_font(&ARM_2D_FONT_6x8.use_as__arm_2d_font_t);
        arm_lcd_text_set_draw_region(NULL);
        arm_lcd_text_set_colour(GLCD_COLOR_RED, GLCD_COLOR_WHITE);
        arm_lcd_text_location(0, 0);
        arm_lcd_puts("Scene QOI (SD Card)");

    /*-----------------------draw the foreground end  -----------------------*/
    }

    ARM_2D_OP_WAIT_ASYNC();

    return arm_fsm_rt_cpl;
}

ARM_NONNULL(1)
void user_scene_qoi_set_image_path(user_scene_qoi_t *ptThis, const char *pszPath)
{
    assert(NULL != ptThis);
    assert(NULL != pszPath);

    if (NULL != this.pszImagePath) {
        free(this.pszImagePath);
    }

    this.pszImagePath = (char *)malloc(strlen(pszPath) + 1);
    if (NULL != this.pszImagePath) {
        strcpy(this.pszImagePath, pszPath);
    }
}

ARM_NONNULL(1)
user_scene_qoi_t *__arm_2d_scene_qoi_init(
                                        arm_2d_scene_player_t *ptDispAdapter, 
                                        user_scene_qoi_t *ptThis)
{
    bool bUserAllocated = false;
    assert(NULL != ptDispAdapter);

    /* get the screen region */
    arm_2d_region_t tScreen
        = arm_2d_helper_pfb_get_display_area(
            &ptDispAdapter->use_as__arm_2d_helper_pfb_t);

    const arm_2d_tile_t *ptCurrentTile = NULL;

    if (NULL == ptThis) {
        ptThis = (user_scene_qoi_t *)
                    __arm_2d_allocate_scratch_memory(   sizeof(user_scene_qoi_t),
                                                        __alignof__(user_scene_qoi_t),
                                                        ARM_2D_MEM_TYPE_UNSPECIFIED);
        assert(NULL != ptThis);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
    }

    memset(ptThis, 0, sizeof(user_scene_qoi_t));

    *ptThis = (user_scene_qoi_t){
        .use_as__arm_2d_scene_t = {

            /* the canvas colour */
            .tCanvas = {GLCD_COLOR_BLACK}, 

            .fnScene        = &__pfb_draw_scene_qoi_handler,

            .fnOnLoad       = &__on_scene_qoi_load,
            .fnOnFrameStart = &__on_scene_qoi_frame_start,
            .fnOnFrameCPL   = &__on_scene_qoi_frame_complete,
            .fnDepose       = &__on_scene_qoi_depose,

            .bUseDirtyRegionHelper = true,

        },
        .bUserAllocated = bUserAllocated,
    };

    /* ------------   initialize members of user_scene_qoi_t begin ---------------*/

    /* allocate default image path */
    this.pszImagePath = (char *)malloc(strlen(ARM_2D_SCENE_QOI_IMAGE_PATH) + 1);
    if (NULL != this.pszImagePath) {
        strcpy(this.pszImagePath, ARM_2D_SCENE_QOI_IMAGE_PATH);
    }

    do {
        arm_qoi_loader_cfg_t tCFG = {
            .ptScene = (arm_2d_scene_t *)ptThis,
            .u2WorkMode = ARM_QOI_MODE_PARTIAL_DECODED,
            .tBackgroundColour.wColour = GLCD_COLOR_WHITE,
            .ImageIO = {
                .ptIO = &ARM_QOI_IO_FILE_LOADER,
                .pTarget = (uintptr_t)&this.LoaderIO.tFile,
            },
        };

        arm_qoi_io_file_loader_init(&this.LoaderIO.tFile, this.pszImagePath);
        arm_qoi_loader_init(&this.tQOIBackground, &tCFG);
    } while(0);

    ptScene = &this.use_as__arm_2d_scene_t;
    ptScene->ptPlayer = ptDispAdapter;

    return ptThis;
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic pop
#endif

#endif
