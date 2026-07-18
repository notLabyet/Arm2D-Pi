#define __USER_SCENE_SD_QOI_IMPLEMENT__
#include "arm_2d_scene_sd_qoi.h"

/*============================ INCLUDES ======================================*/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "arm_2d.h"
#include "arm_2d_disp_adapters.h"

/*============================ MACROS ========================================*/

#ifndef ARM_2D_SCENE_SD_QOI_FILE_PATH
#   define ARM_2D_SCENE_SD_QOI_FILE_PATH             "gif.qoi"
#endif

#ifndef ARM_2D_SCENE_SD_QOI_FRAME_WIDTH
#   define ARM_2D_SCENE_SD_QOI_FRAME_WIDTH           240
#endif

#ifndef ARM_2D_SCENE_SD_QOI_FRAME_HEIGHT
#   define ARM_2D_SCENE_SD_QOI_FRAME_HEIGHT          240
#endif

#ifndef ARM_2D_SCENE_SD_QOI_FRAME_PERIOD_MS
#   define ARM_2D_SCENE_SD_QOI_FRAME_PERIOD_MS       40
#endif

#undef this
#define this (*ptThis)

/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/
/*============================ LOCAL VARIABLES ===============================*/
/*============================ IMPLEMENTATION ================================*/

static void __on_scene_sd_qoi_load(arm_2d_scene_t *ptScene)
{
    user_scene_sd_qoi_t *ptThis = (user_scene_sd_qoi_t *)ptScene;

    if (this.bQOIReady) {
        arm_qoi_loader_on_load(&this.tQOI);
    }
}

static void __on_scene_sd_qoi_depose(arm_2d_scene_t *ptScene)
{
    user_scene_sd_qoi_t *ptThis = (user_scene_sd_qoi_t *)ptScene;

    if (this.bQOIReady) {
        arm_qoi_loader_depose(&this.tQOI);
    }

    ptScene->ptPlayer = NULL;
    if (!this.bUserAllocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, ptScene);
    }
}

static void __on_scene_sd_qoi_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_sd_qoi_t *ptThis = (user_scene_sd_qoi_t *)ptScene;

    if (this.bQOIReady) {
        if (this.bUseFilm) {
            if (arm_2d_helper_is_time_out(this.tFilm.hwPeriodPerFrame, &this.lTimestamp[0])) {
                arm_2d_helper_film_next_frame(&this.tFilm);
                arm_2d_helper_dirty_region_item_suspend_update(
                    &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                    false);

                this.wFPSFrames++;
                if (arm_2d_helper_is_time_out(2000, &this.lTimestamp[1])) {
                    printf("QOI fps: %lu\r\n",
                           (unsigned long)((this.wFPSFrames + 1u) / 2u));
                    this.wFPSFrames = 0;
                }
            } else {
                arm_2d_helper_dirty_region_item_suspend_update(
                    &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                    true);
            }
        } else {
            arm_2d_helper_dirty_region_item_suspend_update(
                &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                false);
        }

        arm_qoi_loader_on_frame_start(&this.tQOI);
    }
}

static void __on_scene_sd_qoi_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_sd_qoi_t *ptThis = (user_scene_sd_qoi_t *)ptScene;

    if (this.bQOIReady) {
        arm_qoi_loader_on_frame_complete(&this.tQOI);
    }
}

static IMPL_PFB_ON_DRAW(__pfb_draw_scene_sd_qoi_handler)
{
    ARM_2D_PARAM(bIsNewFrame);
    user_scene_sd_qoi_t *ptThis = (user_scene_sd_qoi_t *)pTarget;

    arm_2d_canvas(ptTile, canvas) {
        arm_2d_fill_colour(ptTile, &canvas, GLCD_COLOR_BLACK);

        if (this.bQOIReady) {
            const arm_2d_tile_t *ptSource =
                this.bUseFilm ? (const arm_2d_tile_t *)&this.tFilm : &this.tQOI.vres.tTile;
            arm_2d_size_t tSize = ptSource->tRegion.tSize;

            if (tSize.iWidth > canvas.tSize.iWidth) {
                tSize.iWidth = canvas.tSize.iWidth;
            }
            if (tSize.iHeight > canvas.tSize.iHeight) {
                tSize.iHeight = canvas.tSize.iHeight;
            }

            arm_2d_region_t tTargetRegion = {
                .tLocation = {
                    .iX = (int16_t)((canvas.tSize.iWidth - tSize.iWidth) / 2),
                    .iY = (int16_t)((canvas.tSize.iHeight - tSize.iHeight) / 2),
                },
                .tSize = tSize,
            };

            arm_2d_tile_copy_only(ptSource, ptTile, &tTargetRegion);
            arm_2d_helper_dirty_region_update_item(
                &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                (arm_2d_tile_t *)ptTile,
                &canvas,
                &tTargetRegion);
        } else {
            arm_lcd_text_set_target_framebuffer((arm_2d_tile_t *)ptTile);
            arm_lcd_text_set_font(&ARM_2D_FONT_6x8.use_as__arm_2d_font_t);
            arm_lcd_text_set_draw_region(NULL);
            arm_lcd_text_set_colour(GLCD_COLOR_WHITE, GLCD_COLOR_BLACK);
            arm_lcd_text_location(1, 1);
            arm_lcd_puts("gif.qoi not ready");
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

static bool __arm_2d_scene_sd_qoi_init_qoi(user_scene_sd_qoi_t *ptThis)
{
    arm_2d_err_t err;
    arm_qoi_loader_cfg_t cfg;

    err = arm_loader_io_fatfs_init(&this.tQOIFile, ARM_2D_SCENE_SD_QOI_FILE_PATH);
    if (ARM_2D_ERR_NONE != err) {
        printf("QOI FatFs IO init failed: %d\r\n", err);
        return false;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.ptScene = (arm_2d_scene_t *)ptThis;
    cfg.u2WorkMode = ARM_QOI_MODE_PARTIAL_DECODED;
#if __ARM_QOI_USE_LOADER_IO__
    cfg.ImageIO.ptIO = &ARM_LOADER_IO_FATFS;
    cfg.ImageIO.pTarget = (uintptr_t)&this.tQOIFile;
#else
#   error ARM_2D_SCENE_SD_QOI requires __ARM_QOI_USE_LOADER_IO__
#endif

    err = arm_qoi_loader_init(&this.tQOI, &cfg);
    if (ARM_2D_ERR_NONE != err) {
        printf("QOI loader init failed: %d\r\n", err);
        return false;
    }

    int16_t iImageWidth = this.tQOI.vres.tTile.tRegion.tSize.iWidth;
    int16_t iImageHeight = this.tQOI.vres.tTile.tRegion.tSize.iHeight;
    uint16_t hwFrameColumns = (uint16_t)(iImageWidth / ARM_2D_SCENE_SD_QOI_FRAME_WIDTH);
    uint16_t hwFrameRows = (uint16_t)(iImageHeight / ARM_2D_SCENE_SD_QOI_FRAME_HEIGHT);
    uint16_t hwFrameCount = hwFrameColumns * hwFrameRows;

    this.bUseFilm =
        (hwFrameColumns > 0u) &&
        (hwFrameRows > 0u) &&
        (hwFrameCount > 1u);

    if (this.bUseFilm) {
        this.tFilm = (arm_2d_helper_film_t)
            impl_film(this.tQOI.vres.tTile,
                      ARM_2D_SCENE_SD_QOI_FRAME_WIDTH,
                      ARM_2D_SCENE_SD_QOI_FRAME_HEIGHT,
                      hwFrameColumns,
                      hwFrameCount,
                      ARM_2D_SCENE_SD_QOI_FRAME_PERIOD_MS);
    }

    printf("QOI ready: %s size=%dx%d film=%u frame=%dx%d cols=%u count=%u\r\n",
           ARM_2D_SCENE_SD_QOI_FILE_PATH,
           iImageWidth,
           iImageHeight,
           this.bUseFilm ? 1u : 0u,
           ARM_2D_SCENE_SD_QOI_FRAME_WIDTH,
           ARM_2D_SCENE_SD_QOI_FRAME_HEIGHT,
           hwFrameColumns,
           hwFrameCount);

    return true;
}

user_scene_sd_qoi_t *__arm_2d_scene_sd_qoi_init( arm_2d_scene_player_t *ptDispAdapter,
                                            user_scene_sd_qoi_t *ptThis)
{
    bool bUserAllocated = false;

    assert(NULL != ptDispAdapter);

    if (NULL == ptThis) {
        ptThis = (user_scene_sd_qoi_t *)
            __arm_2d_allocate_scratch_memory(sizeof(user_scene_sd_qoi_t),
                                             __alignof__(user_scene_sd_qoi_t),
                                             ARM_2D_MEM_TYPE_UNSPECIFIED);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
    }

    memset(ptThis, 0, sizeof(*ptThis));

    *ptThis = (user_scene_sd_qoi_t) {
        .use_as__arm_2d_scene_t = {
            .tCanvas = {GLCD_COLOR_BLACK},
            .fnOnLoad = __on_scene_sd_qoi_load,
            .fnScene = __pfb_draw_scene_sd_qoi_handler,
            .fnOnFrameStart = __on_scene_sd_qoi_frame_start,
            .fnOnFrameCPL = __on_scene_sd_qoi_frame_complete,
            .fnDepose = __on_scene_sd_qoi_depose,
            .bUseDirtyRegionHelper = true,
        },
        .bUserAllocated = bUserAllocated,
    };

    this.bQOIReady = __arm_2d_scene_sd_qoi_init_qoi(ptThis);

    arm_2d_scene_player_append_scenes(ptDispAdapter,
                                      &this.use_as__arm_2d_scene_t,
                                      1);

    return ptThis;
}

void arm_2d_scene_sd_qoi_loader(void)
{
    (void)__arm_2d_scene_sd_qoi_init(&DISP0_ADAPTER, NULL);
}
