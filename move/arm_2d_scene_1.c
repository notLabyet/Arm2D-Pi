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

#define __USER_SCENE1_IMPLEMENT__
#include "arm_2d_scene_1.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#ifdef RTE_Acceleration_Arm_2D_Scene1

#include <stdlib.h>
#include <string.h>

#include "qmi8658_motion.h"

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

#if __USER_SCENE1_USE_LMSK__
#   define CMSIS_LOGO_MASK          ARM_LMSK_TILE(SCENE1_LMSK_CMSIS)
#else
#   define CMSIS_LOGO_MASK          c_tileCMSISLogoMask
#endif

#define SCENE1_BACKGROUND_VIEW_WIDTH                  240
#define SCENE1_BACKGROUND_VIEW_HEIGHT                 230
#define SCENE1_PARALLAX_Q4_SHIFT                      4
#define SCENE1_PARALLAX_SPRING_GAIN                   4
#define SCENE1_PARALLAX_DAMPING_SHIFT                 1
#define SCENE1_PARALLAX_RESPONSE_DIV                  4
#define SCENE1_GYRO_INPUT_GAIN                        5

/*============================ MACROFIED FUNCTIONS ===========================*/
#undef this
#define this (*ptThis)

/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/

extern const arm_2d_tile_t c_tileCMSISLogo;
extern const arm_2d_tile_t c_tileCMSISLogoMask;
extern const arm_2d_tile_t c_tileCMSISLogoA2Mask;
extern const arm_2d_tile_t c_tileCMSISLogoA4Mask;


extern const arm_2d_tile_t c_tilegirlMask;
extern const arm_2d_tile_t c_tilegirlRGB565;

extern const arm_2d_tile_t c_tilebackgroundRGB565;
/*============================ PROTOTYPES ====================================*/
/*============================ LOCAL VARIABLES ===============================*/

/*! define dirty regions */
IMPL_ARM_2D_REGION_LIST(s_tDirtyRegions, static)

    /* a dirty region to be specified at runtime*/
    ADD_REGION_TO_LIST(s_tDirtyRegions,
        0  /* initialize at runtime later */
    ),
    
    /* add the last region:
        * it is the top left corner for text display 
        */
    ADD_LAST_REGION_TO_LIST(s_tDirtyRegions,
        .tLocation = {
            .iX = 0,
            .iY = 0,
        },
        .tSize = {
            .iWidth = 0,
            .iHeight = 8,
        },
    ),

END_IMPL_ARM_2D_REGION_LIST(s_tDirtyRegions)

/*============================ IMPLEMENTATION ================================*/

static int16_t scene1_clamp_i16(int16_t value, int16_t min, int16_t max)
{
    if (value < min) {
        return min;
    }

    if (value > max) {
        return max;
    }

    return value;
}

static int16_t scene1_abs_i16(int16_t value)
{
    return (value >= 0) ? value : (int16_t)-value;
}

static int16_t scene1_select_horizontal_gyro_offset(int16_t gyro_offset_x,
                                                    int16_t gyro_offset_y,
                                                    int16_t gyro_offset_z)
{
    int16_t horizontal_offset = gyro_offset_z;

    if (scene1_abs_i16(gyro_offset_x) > scene1_abs_i16(horizontal_offset)) {
        horizontal_offset = gyro_offset_x;
    }

    if (scene1_abs_i16(gyro_offset_y) > scene1_abs_i16(horizontal_offset)) {
        horizontal_offset = gyro_offset_y;
    }

    return horizontal_offset;
}

static int16_t scene1_soft_curve_i16(int16_t value)
{
    int32_t const sign = (value < 0) ? -1 : 1;
    int32_t magnitude = value;

    if (magnitude < 0) {
        magnitude = -magnitude;
    }

    magnitude += (magnitude * magnitude) / 24;

    if (magnitude > 32) {
        magnitude = 32;
    }

    return (int16_t)(sign * magnitude);
}

static void scene1_update_parallax(user_scene_1_t *ptThis)
{
    int32_t const target_x_q4 =
        ((int32_t)scene1_soft_curve_i16(this.target_offset_x)
         << SCENE1_PARALLAX_Q4_SHIFT);
    int32_t const target_y_q4 =
        ((int32_t)scene1_soft_curve_i16(this.target_offset_y)
         << SCENE1_PARALLAX_Q4_SHIFT);
    int32_t const delta_x_q4 = target_x_q4 - this.parallax_offset_x_q4;
    int32_t const delta_y_q4 = target_y_q4 - this.parallax_offset_y_q4;

    this.parallax_velocity_x_q4 +=
        (delta_x_q4 * SCENE1_PARALLAX_SPRING_GAIN) /
        SCENE1_PARALLAX_RESPONSE_DIV;
    this.parallax_velocity_y_q4 +=
        (delta_y_q4 * SCENE1_PARALLAX_SPRING_GAIN) /
        SCENE1_PARALLAX_RESPONSE_DIV;

    this.parallax_velocity_x_q4 -=
        this.parallax_velocity_x_q4 >> SCENE1_PARALLAX_DAMPING_SHIFT;
    this.parallax_velocity_y_q4 -=
        this.parallax_velocity_y_q4 >> SCENE1_PARALLAX_DAMPING_SHIFT;

    this.parallax_offset_x_q4 += this.parallax_velocity_x_q4;
    this.parallax_offset_y_q4 += this.parallax_velocity_y_q4;

    this.offset_x = (int16_t)(this.parallax_offset_x_q4 >>
                              SCENE1_PARALLAX_Q4_SHIFT);
    this.offset_y = (int16_t)(this.parallax_offset_y_q4 >>
                              SCENE1_PARALLAX_Q4_SHIFT);
}

static void __on_scene1_load(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
    this.opacity = 0;
    this.lighton = false;
    this.target_offset_x = 0;
    this.target_offset_y = 0;
    this.offset_x = 0;
    this.offset_y = 0;
    this.parallax_offset_x_q4 = 0;
    this.parallax_offset_y_q4 = 0;
    this.parallax_velocity_x_q4 = 0;
    this.parallax_velocity_y_q4 = 0;
#if __USER_SCENE1_USE_LMSK__ 
    ARM_LMSK_GROUP_ON_LOAD();
#endif
}

static void __after_scene1_switching(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static void __on_scene1_depose(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
    
    /*--------------------- insert your depose code begin --------------------*/
    
#if __USER_SCENE1_USE_LMSK__ 
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
 * Scene 1                                                                    *
 *----------------------------------------------------------------------------*/

#if 0 /* deprecated */
static void __on_scene1_background_start(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static void __on_scene1_background_complete(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}
#endif


static void __on_scene1_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
	
	int32_t result;
	int16_t gyro_offset_x;
	int16_t gyro_offset_y;
	int16_t gyro_offset_z;
	
    (void)qmi8658_motion_get_gyro_offset_xyz(&gyro_offset_x,
                                             &gyro_offset_y,
                                             &gyro_offset_z);
    this.target_offset_x =
        (int16_t)(scene1_select_horizontal_gyro_offset(gyro_offset_x,
                                                       gyro_offset_y,
                                                       gyro_offset_z) *
                  SCENE1_GYRO_INPUT_GAIN);
    this.target_offset_y = 0;
    scene1_update_parallax(ptThis);
    if(arm_2d_helper_is_time_out(100,&this.lTimestamp[0])){
		arm_2d_helper_film_next_frame(&this.tFilm);
		arm_2d_helper_film_next_frame(&this.tFilm_mask);
		this.lTimestamp[0] = 0;
	}
	
	if(qmi8658_motion_consume_z_tap() && !this.lighton){
	   this.lighton = true;
	}else{
		
	}
	if(this.lighton){
	   if(arm_2d_helper_time_half_cos_slider(0,255,1000,&result,&this.lTimestamp[1])){
		   this.opacity = 255;
	   }else{
		   this.opacity = result;
	   }
	}
	
	
#if __USER_SCENE1_USE_LMSK__ 
    ARM_LMSK_GROUP_ON_FRAME_START();
#endif
}

static void __on_scene1_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

#if __USER_SCENE1_USE_LMSK__ 
    ARM_LMSK_GROUP_ON_FRAME_COMPLETE();
#endif
}

static void __before_scene1_switching_out(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}




static
IMPL_PFB_ON_DRAW(__pfb_draw_scene1_handler)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)pTarget;

    ARM_2D_UNUSED(ptTile);
    ARM_2D_UNUSED(bIsNewFrame);
    
    arm_2d_canvas(ptTile, __top_canvas) {
    /*-----------------------draw the scene begin-----------------------*/
		arm_2d_align_centre(__top_canvas,180,180){
            int16_t const background_view_width = SCENE1_BACKGROUND_VIEW_WIDTH;
            int16_t const background_view_height = SCENE1_BACKGROUND_VIEW_HEIGHT;
            int16_t background_max_x =
                c_tilebackgroundRGB565.tRegion.tSize.iWidth - background_view_width;
            int16_t background_max_y =
                c_tilebackgroundRGB565.tRegion.tSize.iHeight - background_view_height;
            int16_t background_center_x;
            int16_t background_center_y;
            int16_t background_src_x;
            int16_t background_src_y;
            
            if (background_max_x < 0) {
                background_max_x = 0;
            }
            
            if (background_max_y < 0) {
                background_max_y = 0;
            }
            
            background_center_x = background_max_x / 2;
            background_center_y = background_max_y / 2;
            background_src_x =
                scene1_clamp_i16(background_center_x + this.offset_x,
                                 0,
                                 background_max_x);
            background_src_y =
                scene1_clamp_i16(background_center_y ,//+ this.offset_y,
                                 0,
                                 background_max_y);
			
			
            arm_2d_tile_t tBackgroundView =
                impl_child_tile(
                    c_tilebackgroundRGB565,
                    background_src_x,
                    background_src_y,
                    background_view_width,
                    background_view_height
                );
			
            arm_2d_region_t background_region = {
				.tLocation.iX =  40,
		        .tLocation.iY = -20,
				.tSize.iWidth  = background_view_width,
				.tSize.iHeight = background_view_height
			};
			

            arm_2d_tile_copy_only(&tBackgroundView,
                                   ptTile,
                                   &background_region);
			
			__centre_region.tLocation.iX = __centre_region.tLocation.iX + 30;
			arm_2d_rgb565_tile_fill_with_src_mask_only(
				(const arm_2d_tile_t *)&this.tFilm,
				(const arm_2d_tile_t *)&this.tFilm_mask,
				ptTile,
				&__centre_region
			);												
		}
//                arm_2d_fill_colour_with_mask(   ptTile, 
//                                        &__top_canvas, 
//                                        (const arm_2d_tile_t *)&this.tFilm_mask, 
//                                        (__arm_2d_color_t){GLCD_COLOR_RED});		
    /*-----------------------draw the scene end  -----------------------*/
    }
    ARM_2D_OP_WAIT_ASYNC();

    return arm_fsm_rt_cpl;
}

ARM_NONNULL(1)
user_scene_1_t *__arm_2d_scene1_init(   arm_2d_scene_player_t *ptDispAdapter, 
                                        user_scene_1_t *ptThis)
{
    bool bUserAllocated = false;
    assert(NULL != ptDispAdapter);

    s_tDirtyRegions[dimof(s_tDirtyRegions)-1].ptNext = NULL;

    /* get the screen region */
    arm_2d_region_t __top_canvas
        = arm_2d_helper_pfb_get_display_area(
            &ptDispAdapter->use_as__arm_2d_helper_pfb_t);

    /* initialise dirty region 0 at runtime
     * this demo shows that we create a region in the centre of a screen(320*240)
     * for a image stored in the tile c_tileCMSISLogoMask
     */
    arm_2d_align_centre(__top_canvas, c_tileCMSISLogoMask.tRegion.tSize) {
        s_tDirtyRegions[0].tRegion = __centre_region;
    }

    s_tDirtyRegions[dimof(s_tDirtyRegions)-1].tRegion.tSize.iWidth 
                                                        = __top_canvas.tSize.iWidth;

    if (NULL == ptThis) {
        ptThis = (user_scene_1_t *)
                    __arm_2d_allocate_scratch_memory(   sizeof(user_scene_1_t),
                                                        __alignof__(user_scene_1_t),
                                                        ARM_2D_MEM_TYPE_UNSPECIFIED);
        assert(NULL != ptThis);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
    }
    memset(ptThis, 0, sizeof(user_scene_1_t));

    *ptThis = (user_scene_1_t){
        .use_as__arm_2d_scene_t = {

            /* the canvas colour */
            .tCanvas = {GLCD_COLOR_BLACK}, 

            /* Please uncommon the callbacks if you need them
             */
            .fnOnLoad       = &__on_scene1_load,
            .fnScene        = &__pfb_draw_scene1_handler,
            .fnAfterSwitch  = &__after_scene1_switching,

            /* if you want to use predefined dirty region list, please uncomment the following code */
            //.ptDirtyRegion  = (arm_2d_region_list_item_t *)s_tDirtyRegions,
            
            //.fnOnBGStart    = &__on_scene1_background_start,         /* deprecated */
            //.fnOnBGComplete = &__on_scene1_background_complete,      /* deprecated */
            .fnOnFrameStart = &__on_scene1_frame_start,
            .fnBeforeSwitchOut = &__before_scene1_switching_out,
            .fnOnFrameCPL   = &__on_scene1_frame_complete,
            .fnDepose       = &__on_scene1_depose,

            .bUseDirtyRegionHelper = false,
        },
        .bUserAllocated = bUserAllocated,
    };

    /* ------------   initialize members of user_scene_1_t begin ---------------*/

#if __USER_SCENE1_USE_LMSK__ 
    ARM_LMSK_ITEM_INIT_WITH_ROM(SCENE1_LMSK_CMSIS, c_lmskCMSISLogo, 1881);
#endif
	    this.tFilm = (arm_2d_helper_film_t)
                    impl_film(c_tilegirlRGB565, 
                        180, 
                        180, 
                        12, 
                        100, 
                        500);
		
		this.tFilm_mask = (arm_2d_helper_film_t)
				impl_film(c_tilegirlMask, 
					180, 
					180, 
					12, 
					100, 
					500);	
		
    /* ------------   initialize members of user_scene_1_t end   ---------------*/

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
