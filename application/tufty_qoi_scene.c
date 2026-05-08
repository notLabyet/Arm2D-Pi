#include "tufty_qoi_scene.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "arm_2d.h"
#include "arm_2d_disp_adapters.h"
#include "arm_loader_io_fatfs.h"
#include "qoi_loader.h"

#ifndef TUFTY_QOI_FILE_PATH
#   define TUFTY_QOI_FILE_PATH             "gif.qoi"
#endif

#ifndef TUFTY_QOI_FRAME_WIDTH
#   define TUFTY_QOI_FRAME_WIDTH           320
#endif

#ifndef TUFTY_QOI_FRAME_HEIGHT
#   define TUFTY_QOI_FRAME_HEIGHT          240
#endif

#ifndef TUFTY_QOI_FRAME_COLUMNS
#   define TUFTY_QOI_FRAME_COLUMNS         9
#endif

#ifndef TUFTY_QOI_FRAME_COUNT
#   define TUFTY_QOI_FRAME_COUNT           81
#endif

#ifndef TUFTY_QOI_FRAME_PERIOD_MS
#   define TUFTY_QOI_FRAME_PERIOD_MS       40
#endif

#undef this
#define this (*ptThis)

typedef struct tufty_qoi_scene_t {
    implement(arm_2d_scene_t);

    int64_t timestamp;
    bool user_allocated;
    bool qoi_ready;
    bool use_film;

    arm_qoi_loader_t qoi;
    arm_loader_io_fatfs_t qoi_file;
    arm_2d_helper_film_t film;
} tufty_qoi_scene_t;

static void tufty_qoi_scene_on_load(arm_2d_scene_t *scene)
{
    tufty_qoi_scene_t *ptThis = (tufty_qoi_scene_t *)scene;

    if (this.qoi_ready) {
        arm_qoi_loader_on_load(&this.qoi);
    }
}

static void tufty_qoi_scene_depose(arm_2d_scene_t *scene)
{
    tufty_qoi_scene_t *ptThis = (tufty_qoi_scene_t *)scene;

    if (this.qoi_ready) {
        arm_qoi_loader_depose(&this.qoi);
    }

    scene->ptPlayer = NULL;
    if (!this.user_allocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, scene);
    }
}

static void tufty_qoi_scene_frame_start(arm_2d_scene_t *scene)
{
    tufty_qoi_scene_t *ptThis = (tufty_qoi_scene_t *)scene;

    if (this.qoi_ready) {
        if (this.use_film) {
            if (arm_2d_helper_is_time_out(this.film.hwPeriodPerFrame, &this.timestamp)) {
                arm_2d_helper_film_next_frame(&this.film);
                arm_2d_helper_dirty_region_item_suspend_update(
                    &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                    false);
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

        arm_qoi_loader_on_frame_start(&this.qoi);
    }
}

static void tufty_qoi_scene_frame_complete(arm_2d_scene_t *scene)
{
    tufty_qoi_scene_t *ptThis = (tufty_qoi_scene_t *)scene;

    if (this.qoi_ready) {
        arm_qoi_loader_on_frame_complete(&this.qoi);
    }
}

static IMPL_PFB_ON_DRAW(tufty_qoi_scene_draw)
{
    ARM_2D_PARAM(bIsNewFrame);
    tufty_qoi_scene_t *ptThis = (tufty_qoi_scene_t *)pTarget;

    arm_2d_canvas(ptTile, canvas) {
        arm_2d_fill_colour(ptTile, &canvas, GLCD_COLOR_BLACK);

        if (this.qoi_ready) {
            const arm_2d_tile_t *source =
                this.use_film ? (const arm_2d_tile_t *)&this.film : &this.qoi.vres.tTile;
            arm_2d_size_t size = source->tRegion.tSize;

            if (size.iWidth > canvas.tSize.iWidth) {
                size.iWidth = canvas.tSize.iWidth;
            }
            if (size.iHeight > canvas.tSize.iHeight) {
                size.iHeight = canvas.tSize.iHeight;
            }

            arm_2d_region_t target = {
                .tLocation = {
                    .iX = (int16_t)((canvas.tSize.iWidth - size.iWidth) / 2),
                    .iY = (int16_t)((canvas.tSize.iHeight - size.iHeight) / 2),
                },
                .tSize = size,
            };

            arm_2d_tile_copy_only(source, ptTile, &target);
            arm_2d_helper_dirty_region_update_item(
                &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                (arm_2d_tile_t *)ptTile,
                &canvas,
                &target);
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

static bool tufty_qoi_scene_init_qoi(tufty_qoi_scene_t *ptThis)
{
    arm_2d_err_t err;
    arm_qoi_loader_cfg_t cfg;

    err = arm_loader_io_fatfs_init(&this.qoi_file, TUFTY_QOI_FILE_PATH);
    if (ARM_2D_ERR_NONE != err) {
        printf("QOI FatFs IO init failed: %d\r\n", err);
        return false;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.ptScene = (arm_2d_scene_t *)ptThis;
    cfg.u2WorkMode = ARM_QOI_MODE_PARTIAL_DECODED;
#if __ARM_QOI_USE_LOADER_IO__
    cfg.ImageIO.ptIO = &ARM_LOADER_IO_FATFS;
    cfg.ImageIO.pTarget = (uintptr_t)&this.qoi_file;
#else
#   error TUFTY_QOI scene requires __ARM_QOI_USE_LOADER_IO__
#endif

    err = arm_qoi_loader_init(&this.qoi, &cfg);
    if (ARM_2D_ERR_NONE != err) {
        printf("QOI loader init failed: %d\r\n", err);
        return false;
    }

    this.use_film =
        (this.qoi.vres.tTile.tRegion.tSize.iWidth >= TUFTY_QOI_FRAME_WIDTH) &&
        (this.qoi.vres.tTile.tRegion.tSize.iHeight >= TUFTY_QOI_FRAME_HEIGHT) &&
        (TUFTY_QOI_FRAME_COUNT > 1);

    if (this.use_film) {
        this.film = (arm_2d_helper_film_t)
            impl_film(this.qoi.vres.tTile,
                      TUFTY_QOI_FRAME_WIDTH,
                      TUFTY_QOI_FRAME_HEIGHT,
                      TUFTY_QOI_FRAME_COLUMNS,
                      TUFTY_QOI_FRAME_COUNT,
                      TUFTY_QOI_FRAME_PERIOD_MS);
    }

    printf("QOI ready: %s size=%dx%d film=%u frame=%dx%d count=%u\r\n",
           TUFTY_QOI_FILE_PATH,
           this.qoi.vres.tTile.tRegion.tSize.iWidth,
           this.qoi.vres.tTile.tRegion.tSize.iHeight,
           this.use_film ? 1u : 0u,
           TUFTY_QOI_FRAME_WIDTH,
           TUFTY_QOI_FRAME_HEIGHT,
           TUFTY_QOI_FRAME_COUNT);

    return true;
}

static tufty_qoi_scene_t *tufty_qoi_scene_init(arm_2d_scene_player_t *player,
                                               tufty_qoi_scene_t *ptThis)
{
    bool user_allocated = false;

    assert(NULL != player);

    if (NULL == ptThis) {
        ptThis = (tufty_qoi_scene_t *)
            __arm_2d_allocate_scratch_memory(sizeof(tufty_qoi_scene_t),
                                             __alignof__(tufty_qoi_scene_t),
                                             ARM_2D_MEM_TYPE_UNSPECIFIED);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        user_allocated = true;
    }

    memset(ptThis, 0, sizeof(*ptThis));

    *ptThis = (tufty_qoi_scene_t) {
        .use_as__arm_2d_scene_t = {
            .tCanvas = {GLCD_COLOR_BLACK},
            .fnOnLoad = tufty_qoi_scene_on_load,
            .fnScene = tufty_qoi_scene_draw,
            .fnOnFrameStart = tufty_qoi_scene_frame_start,
            .fnOnFrameCPL = tufty_qoi_scene_frame_complete,
            .fnDepose = tufty_qoi_scene_depose,
            .bUseDirtyRegionHelper = true,
        },
        .user_allocated = user_allocated,
    };

    this.qoi_ready = tufty_qoi_scene_init_qoi(ptThis);

    arm_2d_scene_player_append_scenes(player,
                                      &this.use_as__arm_2d_scene_t,
                                      1);

    return ptThis;
}

void tufty_qoi_scene_loader(void)
{
    (void)tufty_qoi_scene_init(&DISP0_ADAPTER, NULL);
}
