#define __USER_SCENE_SD_LMSK_IMPLEMENT__
#include "arm_2d_scene_sd_lmsk.h"

/*============================ INCLUDES ======================================*/

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm_2d.h"
#include "arm_2d_disp_adapters.h"

/*============================ MACROS ========================================*/

#ifndef ARM_2D_SCENE_SD_LMSK_FILE_PATH
#   define ARM_2D_SCENE_SD_LMSK_FILE_PATH             "bad_apple_100x75_16_a4.lmsk"
#endif

#ifndef ARM_2D_SCENE_SD_LMSK_FRAME_WIDTH
#   define ARM_2D_SCENE_SD_LMSK_FRAME_WIDTH           100
#endif

#ifndef ARM_2D_SCENE_SD_LMSK_FRAME_HEIGHT
#   define ARM_2D_SCENE_SD_LMSK_FRAME_HEIGHT          75
#endif

#ifndef ARM_2D_SCENE_SD_LMSK_FRAME_COLUMNS
#   define ARM_2D_SCENE_SD_LMSK_FRAME_COLUMNS         16
#endif

#ifndef ARM_2D_SCENE_SD_LMSK_FRAME_COUNT
#   define ARM_2D_SCENE_SD_LMSK_FRAME_COUNT           3110
#endif

#ifndef ARM_2D_SCENE_SD_LMSK_FRAME_PERIOD_MS
#   define ARM_2D_SCENE_SD_LMSK_FRAME_PERIOD_MS       33
#endif

#undef this
#define this (*ptThis)

/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/
/*============================ LOCAL VARIABLES ===============================*/

static uint32_t const c_wColourTable[] = {
    __RGB32(0xff, 0x00, 0x00),
    __RGB32(0x00, 0xff, 0x00),
    __RGB32(0x00, 0x00, 0xff),
    __RGB32(0x00, 0xff, 0xff),
    __RGB32(0xff, 0xff, 0xff),
    __RGB32(0xff, 0xff, 0x00),
    __RGB32(0xff, 0x00, 0xff),
    __RGB32(0xff, 0x80, 0x00),
};

/*============================ IMPLEMENTATION ================================*/

static void __on_scene_sd_lmsk_load(arm_2d_scene_t *ptScene)
{
    user_scene_sd_lmsk_t *ptThis = (user_scene_sd_lmsk_t *)ptScene;

    if (this.bLMSKReady) {
        arm_lmsk_loader_on_load(&this.tAnimation);
    }
}

static void __on_scene_sd_lmsk_depose(arm_2d_scene_t *ptScene)
{
    user_scene_sd_lmsk_t *ptThis = (user_scene_sd_lmsk_t *)ptScene;

    if (this.bLMSKReady) {
        arm_lmsk_loader_depose(&this.tAnimation);
    }

    ptScene->ptPlayer = NULL;
    if (!this.bUserAllocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, ptScene);
    }
}

static void __on_scene_sd_lmsk_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_sd_lmsk_t *ptThis = (user_scene_sd_lmsk_t *)ptScene;

    if (!this.bLMSKReady) {
        return;
    }

    if (arm_2d_helper_is_time_out(this.tFilm.hwPeriodPerFrame, &this.lTimestamp[0])) {
        arm_2d_helper_film_next_frame(&this.tFilm);
        arm_2d_helper_dirty_region_item_suspend_update(
            &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
            false);
    } else {
        arm_2d_helper_dirty_region_item_suspend_update(
            &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
            true);
    }

    do {
        int32_t nResult;

        if (arm_2d_helper_time_liner_slider(0, 1000, 5000, &nResult, &this.lTimestamp[1])) {
            this.lTimestamp[1] = 0;
            this.wPreviousColour = c_wColourTable[this.chColourTableIndex];
            srand((uint32_t)arm_2d_helper_get_system_timestamp());
            this.chColourTableIndex = rand() % dimof(c_wColourTable);
            nResult = 0;
        }

        this.tColour = arm_2d_pixel_from_brga8888(
            __arm_2d_helper_colour_slider(this.wPreviousColour,
                                          c_wColourTable[this.chColourTableIndex],
                                          1000,
                                          nResult));
    } while (0);

    if (0 != ptScene->ptPlayer->Benchmark.wAverage) {
        this.hwFPS = MIN((arm_2d_helper_get_reference_clock_frequency() /
                          ptScene->ptPlayer->Benchmark.wAverage),
                          999);
    }

    arm_lmsk_loader_on_frame_start(&this.tAnimation);
}

static void __on_scene_sd_lmsk_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_sd_lmsk_t *ptThis = (user_scene_sd_lmsk_t *)ptScene;

    if (this.bLMSKReady) {
        arm_lmsk_loader_on_frame_complete(&this.tAnimation);
    }
}

static IMPL_PFB_ON_DRAW(__pfb_draw_scene_sd_lmsk_handler)
{
    ARM_2D_PARAM(bIsNewFrame);
    user_scene_sd_lmsk_t *ptThis = (user_scene_sd_lmsk_t *)pTarget;

    arm_2d_canvas(ptTile, canvas) {
        arm_2d_fill_colour(ptTile, &canvas, GLCD_COLOR_BLACK);

        if (this.bLMSKReady) {
            arm_2d_size_t tBoxSize = this.tFilm.use_as__arm_2d_tile_t.tRegion.tSize;
            tBoxSize.iHeight += 10;

            arm_2d_align_centre(canvas, tBoxSize) {
                arm_2d_dock_top(__centre_region, 10) {
                    arm_2d_fill_colour(ptTile, &__top_region, GLCD_COLOR_DARK_GREY);

                    arm_2d_dock_vertical(__top_region, 8, 2) {
                        arm_lcd_text_set_font(&ARM_2D_FONT_6x8.use_as__arm_2d_font_t);
                        arm_lcd_text_set_draw_region(&__vertical_region);
                        arm_lcd_text_set_colour(GLCD_COLOR_WHITE, GLCD_COLOR_WHITE);
                        arm_lcd_printf("Frame:%04"PRId16"\tFPS:%"PRId16,
                                        arm_2d_helper_film_get_frame_index(&this.tFilm),
                                        this.hwFPS);
                    }
                }

                arm_2d_fill_colour_with_mask(ptTile,
                                             &__centre_region,
                                             (const arm_2d_tile_t *)&this.tFilm,
                                             (__arm_2d_color_t){this.tColour});

                arm_2d_helper_dirty_region_update_item(
                    &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                    (arm_2d_tile_t *)ptTile,
                    &canvas,
                    &__centre_region);
            }
        } else {
            arm_lcd_text_set_target_framebuffer((arm_2d_tile_t *)ptTile);
            arm_lcd_text_set_font(&ARM_2D_FONT_6x8.use_as__arm_2d_font_t);
            arm_lcd_text_set_draw_region(NULL);
            arm_lcd_text_set_colour(GLCD_COLOR_WHITE, GLCD_COLOR_BLACK);
            arm_lcd_text_location(1, 1);
            arm_lcd_puts("LMSK not ready");

            arm_2d_helper_dirty_region_update_item(
                &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                (arm_2d_tile_t *)ptTile,
                &canvas,
                &canvas);
        }
    }

    ARM_2D_OP_WAIT_ASYNC();

    return arm_fsm_rt_cpl;
}

static bool __arm_2d_scene_sd_lmsk_init_lmsk(user_scene_sd_lmsk_t *ptThis)
{
    arm_2d_err_t err;
    arm_lmsk_loader_cfg_t cfg;

    err = arm_loader_io_fatfs_init(&this.tLMSKFile, ARM_2D_SCENE_SD_LMSK_FILE_PATH);
    if (ARM_2D_ERR_NONE != err) {
        printf("LMSK FatFs IO init failed: %d\r\n", err);
        return false;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.ptScene = (arm_2d_scene_t *)ptThis;
#if __ARM_LMSK_USE_LOADER_IO__
    cfg.ImageIO.ptIO = &ARM_LOADER_IO_FATFS;
    cfg.ImageIO.pTarget = (uintptr_t)&this.tLMSKFile;
#else
#   error ARM_2D_SCENE_SD_LMSK requires __ARM_LMSK_USE_LOADER_IO__
#endif

    err = arm_lmsk_loader_init(&this.tAnimation, &cfg);
    if (ARM_2D_ERR_NONE != err) {
        printf("LMSK loader init failed: %d\r\n", err);
        return false;
    }

    this.tFilm = (arm_2d_helper_film_t)
        impl_film(this.tAnimation,
                  ARM_2D_SCENE_SD_LMSK_FRAME_WIDTH,
                  ARM_2D_SCENE_SD_LMSK_FRAME_HEIGHT,
                  ARM_2D_SCENE_SD_LMSK_FRAME_COLUMNS,
                  ARM_2D_SCENE_SD_LMSK_FRAME_COUNT,
                  ARM_2D_SCENE_SD_LMSK_FRAME_PERIOD_MS);

    this.wPreviousColour = c_wColourTable[0];
    this.chColourTableIndex = 1;
    this.tColour = arm_2d_pixel_from_brga8888(c_wColourTable[0]);

    printf("LMSK ready: %s frame=%dx%d cols=%u count=%u period=%ums\r\n",
           ARM_2D_SCENE_SD_LMSK_FILE_PATH,
           ARM_2D_SCENE_SD_LMSK_FRAME_WIDTH,
           ARM_2D_SCENE_SD_LMSK_FRAME_HEIGHT,
           (unsigned)ARM_2D_SCENE_SD_LMSK_FRAME_COLUMNS,
           (unsigned)ARM_2D_SCENE_SD_LMSK_FRAME_COUNT,
           (unsigned)ARM_2D_SCENE_SD_LMSK_FRAME_PERIOD_MS);

    return true;
}

user_scene_sd_lmsk_t *__arm_2d_scene_sd_lmsk_init( arm_2d_scene_player_t *ptDispAdapter,
                                              user_scene_sd_lmsk_t *ptThis)
{
    bool bUserAllocated = false;

    assert(NULL != ptDispAdapter);

    if (NULL == ptThis) {
        ptThis = (user_scene_sd_lmsk_t *)
            __arm_2d_allocate_scratch_memory(sizeof(user_scene_sd_lmsk_t),
                                             __alignof__(user_scene_sd_lmsk_t),
                                             ARM_2D_MEM_TYPE_UNSPECIFIED);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
    }

    memset(ptThis, 0, sizeof(*ptThis));

    *ptThis = (user_scene_sd_lmsk_t) {
        .use_as__arm_2d_scene_t = {
            .tCanvas = {GLCD_COLOR_BLACK},
            .fnOnLoad = __on_scene_sd_lmsk_load,
            .fnScene = __pfb_draw_scene_sd_lmsk_handler,
            .fnOnFrameStart = __on_scene_sd_lmsk_frame_start,
            .fnOnFrameCPL = __on_scene_sd_lmsk_frame_complete,
            .fnDepose = __on_scene_sd_lmsk_depose,
            .bUseDirtyRegionHelper = true,
        },
        .bUserAllocated = bUserAllocated,
    };

    this.bLMSKReady = __arm_2d_scene_sd_lmsk_init_lmsk(ptThis);

    arm_2d_scene_player_append_scenes(ptDispAdapter,
                                      &this.use_as__arm_2d_scene_t,
                                      1);

    return ptThis;
}

void arm_2d_scene_sd_lmsk_loader(void)
{
    (void)__arm_2d_scene_sd_lmsk_init(&DISP0_ADAPTER, NULL);
}
