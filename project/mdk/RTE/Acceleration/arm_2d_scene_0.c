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
#include "codex_status.h"
#include "perf_counter.h"
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
typedef enum codex_anim_id_t {
    CODEX_ANIM_IDLE = 0,
    CODEX_ANIM_THINKING,
    CODEX_ANIM_WORKING,
    CODEX_ANIM_RETRYING,
    CODEX_ANIM_DYING,
} codex_anim_id_t;

/*============================ GLOBAL VARIABLES ==============================*/
extern const arm_2d_tile_t c_tileidleRGB565;
extern const arm_2d_tile_t c_tiledyingRGB565;
extern const arm_2d_tile_t c_tileretryingRGB565;
extern const arm_2d_tile_t c_tilethinkingRGB565;
extern const arm_2d_tile_t c_tileworkingRGB565;

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

static char const *codex_state_label(uint8_t chState, uint8_t chPhase)
{
    switch (chPhase) {
        case CODEX_PHASE_THINKING:
            return "THINK";
        case CODEX_PHASE_WRITING:
            return "WRITE";
        case CODEX_PHASE_RETRY:
            return "RETRY";
        case CODEX_PHASE_ERROR:
            return "LINK";
        default:
            break;
    }

    switch (chState) {
        case CODEX_STATE_IDLE:
            return "IDLE";
        case CODEX_STATE_WORKING:
            return "THINK";
        case CODEX_STATE_RETRY:
            return "RETRY";
        case CODEX_STATE_CONNECTION_ERROR:
            return "LINK";
        default:
            return "UNKNOWN";
    }
}

static codex_anim_id_t codex_anim_from_status(uint8_t chState, uint8_t chPhase)
{
    if (CODEX_STATE_IDLE == chState) {
        return CODEX_ANIM_IDLE;
    }

    switch (chPhase) {
        case CODEX_PHASE_WRITING:
            return CODEX_ANIM_WORKING;
        case CODEX_PHASE_RETRY:
            return CODEX_ANIM_RETRYING;
        case CODEX_PHASE_ERROR:
            return CODEX_ANIM_DYING;
        case CODEX_PHASE_THINKING:
            return CODEX_ANIM_THINKING;
        default:
            break;
    }

    switch (chState) {
        case CODEX_STATE_WORKING:
            return CODEX_ANIM_THINKING;
        case CODEX_STATE_RETRY:
            return CODEX_ANIM_RETRYING;
        case CODEX_STATE_CONNECTION_ERROR:
            return CODEX_ANIM_DYING;
        case CODEX_STATE_IDLE:
        default:
            return CODEX_ANIM_IDLE;
    }
}

static uint16_t codex_random_idle_delay_ms(void)
{
    return (uint16_t)(5000u + ((uint32_t)rand() % 5001u));
}

static bool codex_anim_is_looping(codex_anim_id_t tAnim)
{
    return (tAnim == CODEX_ANIM_THINKING) || (tAnim == CODEX_ANIM_WORKING);
}

static arm_2d_helper_film_t *codex_film_from_anim(user_scene_0_t *ptThis,
                                                  codex_anim_id_t tAnim)
{
    switch (tAnim) {
        case CODEX_ANIM_IDLE:
            return &this.tFilmIdle;
        case CODEX_ANIM_WORKING:
            return &this.tFilmWorking;
        case CODEX_ANIM_RETRYING:
            return &this.tFilmRetrying;
        case CODEX_ANIM_DYING:
            return &this.tFilmDying;
        case CODEX_ANIM_THINKING:
        default:
            return &this.tFilmThinking;
    }
}

static void codex_select_anim(user_scene_0_t *ptThis,
                              uint8_t chState,
                              uint8_t chPhase)
{
    codex_anim_id_t tAnim = codex_anim_from_status(chState, chPhase);

    if (this.chLastAnim != (uint8_t)tAnim) {
        arm_2d_helper_film_t *ptFilm = codex_film_from_anim(ptThis, tAnim);

        arm_2d_helper_film_set_frame(ptFilm, 0);
        this.chActiveAnim = (uint8_t)tAnim;
        this.chLastAnim = (uint8_t)tAnim;
        this.bIdleWaiting = false;
        this.hwIdleDelayMs = 0;
        this.wIdleWaitStartMs = 0;
        this.lTimestamp[0] = 0;
        this.lTimestamp[1] = 0;
    }
}

static void codex_active_film_next_frame(user_scene_0_t *ptThis)
{
    codex_anim_id_t tAnim = (codex_anim_id_t)this.chActiveAnim;
    arm_2d_helper_film_t *ptFilm = codex_film_from_anim(ptThis, tAnim);

    if (NULL == ptFilm) {
        return;
    }

    if (CODEX_ANIM_IDLE == tAnim && this.bIdleWaiting) {
        if ((uint32_t)(get_system_ms() - this.wIdleWaitStartMs) >= this.hwIdleDelayMs) {
            this.bIdleWaiting = false;
            arm_2d_helper_film_set_frame(ptFilm, 0);
            this.lTimestamp[0] = 0;
        }
        return;
    }

    if (!arm_2d_helper_is_time_out(ptFilm->hwPeriodPerFrame, &this.lTimestamp[0])) {
        return;
    }

    if (codex_anim_is_looping(tAnim)) {
        arm_2d_helper_film_next_frame(ptFilm);
    } else {
        uint16_t hwIndex = arm_2d_helper_film_get_frame_index(ptFilm);
        uint16_t hwFrameCount = arm_2d_helper_film_get_frame_count(ptFilm);

        if (hwFrameCount == 0u) {
            return;
        }

        if (hwIndex < (uint16_t)(hwFrameCount - 1u)) {
            arm_2d_helper_film_next_frame(ptFilm);
        } else {
            arm_2d_helper_film_set_frame(ptFilm, (int16_t)(hwFrameCount - 1u));
            if (CODEX_ANIM_IDLE == tAnim) {
                this.bIdleWaiting = true;
                this.hwIdleDelayMs = codex_random_idle_delay_ms();
                this.wIdleWaitStartMs = get_system_ms();
            }
        }
    }
}

static void __on_scene0_load(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);


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

#if __USER_SCENE0_USE_LMSK__
    ARM_LMSK_GROUP_ON_FRAME_START();
#endif

    codex_select_anim(ptThis, g_u8CodexState, g_u8CodexPhase);
    codex_active_film_next_frame(ptThis);
	
}

static void __on_scene0_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_0_t *ptThis = (user_scene_0_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

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

        uint8_t chState = g_u8CodexState;
        uint8_t chPhase = g_u8CodexPhase;
        arm_2d_helper_film_t *ptFilm =
            codex_film_from_anim(ptThis, (codex_anim_id_t)this.chActiveAnim);

        arm_2d_fill_colour(ptTile, &__top_canvas, GLCD_COLOR_BLACK);

		arm_2d_region_t  __film_region = {
			.tLocation.iX  = 40,
			.tLocation.iY  = 60,
			.tSize.iWidth  = 240,
			.tSize.iHeight = 180
		};

        if (NULL != ptFilm) {
            arm_2d_tile_copy_only(
                (const arm_2d_tile_t *)ptFilm,
                ptTile,
                &__film_region);
        }

		arm_lcd_text_set_target_framebuffer((arm_2d_tile_t *)ptTile);
		arm_lcd_text_set_font(&ARM_2D_FONT_6x8.use_as__arm_2d_font_t);
		arm_lcd_text_set_draw_region(NULL);
		arm_lcd_text_set_colour(GLCD_COLOR_RED, GLCD_COLOR_BLACK);
		arm_lcd_text_location(4, 4);
		arm_lcd_printf("Codex %s T%lu R%u",
                        codex_state_label(chState, chPhase),
                       (unsigned long)g_u32CodexTokens,
                       (unsigned)g_u16CodexRetries);
        
      /*-----------------------draw the scene end  -----------------------*/

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

    s_tDirtyRegions[dimof(s_tDirtyRegions)-1].ptNext = NULL;

    /* get the screen region */
    arm_2d_region_t __top_canvas
        = arm_2d_helper_pfb_get_display_area(
            &ptDispAdapter->use_as__arm_2d_helper_pfb_t);

    /* initialise dirty region 0 at runtime
     * this demo shows that we create a region in the centre of a screen(320*240)
     * for a image stored in the tile c_tileCMSISLogoMask
     */


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

            .bUseDirtyRegionHelper = false,
        },
        .bUserAllocated = bUserAllocated,
    };

    /* ------------   initialize members of user_scene_0_t begin ---------------*/


#if __USER_SCENE0_USE_LMSK__
    ARM_LMSK_ITEM_INIT_WITH_ROM(SCENE0_LMSK_CMSIS, c_lmskCMSISLogo, 1881);
#endif

    /* ------------   initialize members of user_scene_0_t end   ---------------*/
    this.tFilmIdle = (arm_2d_helper_film_t)
                    impl_film(c_tileidleRGB565,
                              240,
                              180,
                              20,
                              25,
                              200);
    this.tFilmThinking = (arm_2d_helper_film_t)
                    impl_film(c_tilethinkingRGB565,
                              240,
                              180,
                              20,
                              20,
                              200);
    this.tFilmWorking = (arm_2d_helper_film_t)
                    impl_film(c_tileworkingRGB565,
                              240,
                              180,
                              20,
                              20,
                              200);
    this.tFilmRetrying = (arm_2d_helper_film_t)
                    impl_film(c_tileretryingRGB565,
                              240,
                              180,
                              20,
                              10,
                              200);
    this.tFilmDying = (arm_2d_helper_film_t)
                    impl_film(c_tiledyingRGB565,
                              240,
                              180,
                              20,
                              20,
                              200);
    srand((uint32_t)arm_2d_helper_get_system_timestamp());
    this.chActiveAnim = CODEX_ANIM_IDLE;
    this.chLastAnim = 0xFFu;
    this.bIdleWaiting = false;
    this.hwIdleDelayMs = 0;
    this.wIdleWaitStartMs = 0;
    arm_2d_helper_film_set_frame(&this.tFilmIdle, 0);
    arm_2d_helper_film_set_frame(&this.tFilmThinking, 0);
    arm_2d_helper_film_set_frame(&this.tFilmWorking, 0);
    arm_2d_helper_film_set_frame(&this.tFilmRetrying, 0);
    arm_2d_helper_film_set_frame(&this.tFilmDying, 0);

    arm_2d_scene_player_append_scenes(ptDispAdapter,
                                      &this.use_as__arm_2d_scene_t,
                                      1);

    return ptThis;
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#endif

#endif
