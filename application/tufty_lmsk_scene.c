#include "tufty_lmsk_scene.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm_2d.h"
#include "arm_2d_disp_adapters.h"
#include "arm_loader_io_fatfs.h"
#include "lmsk_loader.h"

#ifndef TUFTY_LMSK_FILE_PATH
#   define TUFTY_LMSK_FILE_PATH             "bad_apple_100x75_16_a4.lmsk"
#endif

#ifndef TUFTY_LMSK_FRAME_WIDTH
#   define TUFTY_LMSK_FRAME_WIDTH           100
#endif

#ifndef TUFTY_LMSK_FRAME_HEIGHT
#   define TUFTY_LMSK_FRAME_HEIGHT          75
#endif

#ifndef TUFTY_LMSK_FRAME_COLUMNS
#   define TUFTY_LMSK_FRAME_COLUMNS         16
#endif

#ifndef TUFTY_LMSK_FRAME_COUNT
#   define TUFTY_LMSK_FRAME_COUNT           3110
#endif

#ifndef TUFTY_LMSK_FRAME_PERIOD_MS
#   define TUFTY_LMSK_FRAME_PERIOD_MS       33
#endif

#undef this
#define this (*ptThis)

typedef struct tufty_lmsk_scene_t {
    implement(arm_2d_scene_t);

    int64_t timestamp[2];
    bool user_allocated;
    bool lmsk_ready;

    uint8_t colour_table_index;
    uint16_t fps;
    uint32_t previous_colour;
    COLOUR_INT colour;

    arm_lmsk_loader_t animation;
    arm_loader_io_fatfs_t lmsk_file;
    arm_2d_helper_film_t film;
} tufty_lmsk_scene_t;

static uint32_t const c_colour_table[] = {
    __RGB32(0xff, 0x00, 0x00),
    __RGB32(0x00, 0xff, 0x00),
    __RGB32(0x00, 0x00, 0xff),
    __RGB32(0x00, 0xff, 0xff),
    __RGB32(0xff, 0xff, 0xff),
    __RGB32(0xff, 0xff, 0x00),
    __RGB32(0xff, 0x00, 0xff),
    __RGB32(0xff, 0x80, 0x00),
};

static void tufty_lmsk_scene_on_load(arm_2d_scene_t *scene)
{
    tufty_lmsk_scene_t *ptThis = (tufty_lmsk_scene_t *)scene;

    if (this.lmsk_ready) {
        arm_lmsk_loader_on_load(&this.animation);
    }
}

static void tufty_lmsk_scene_depose(arm_2d_scene_t *scene)
{
    tufty_lmsk_scene_t *ptThis = (tufty_lmsk_scene_t *)scene;

    if (this.lmsk_ready) {
        arm_lmsk_loader_depose(&this.animation);
    }

    scene->ptPlayer = NULL;
    if (!this.user_allocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, scene);
    }
}

static void tufty_lmsk_scene_frame_start(arm_2d_scene_t *scene)
{
    tufty_lmsk_scene_t *ptThis = (tufty_lmsk_scene_t *)scene;

    if (!this.lmsk_ready) {
        return;
    }

    if (arm_2d_helper_is_time_out(this.film.hwPeriodPerFrame, &this.timestamp[0])) {
        arm_2d_helper_film_next_frame(&this.film);
        arm_2d_helper_dirty_region_item_suspend_update(
            &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
            false);
    } else {
        arm_2d_helper_dirty_region_item_suspend_update(
            &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
            true);
    }

    do {
        int32_t result;

        if (arm_2d_helper_time_liner_slider(0, 1000, 5000, &result, &this.timestamp[1])) {
            this.timestamp[1] = 0;
            this.previous_colour = c_colour_table[this.colour_table_index];
            srand((uint32_t)arm_2d_helper_get_system_timestamp());
            this.colour_table_index = rand() % dimof(c_colour_table);
            result = 0;
        }

        this.colour = arm_2d_pixel_from_brga8888(
            __arm_2d_helper_colour_slider(this.previous_colour,
                                          c_colour_table[this.colour_table_index],
                                          1000,
                                          result));
    } while (0);

    if (0 != scene->ptPlayer->Benchmark.wAverage) {
        this.fps = MIN((arm_2d_helper_get_reference_clock_frequency() /
                       scene->ptPlayer->Benchmark.wAverage),
                       999);
    }

    arm_lmsk_loader_on_frame_start(&this.animation);
}

static void tufty_lmsk_scene_frame_complete(arm_2d_scene_t *scene)
{
    tufty_lmsk_scene_t *ptThis = (tufty_lmsk_scene_t *)scene;

    if (this.lmsk_ready) {
        arm_lmsk_loader_on_frame_complete(&this.animation);
    }
}

static IMPL_PFB_ON_DRAW(tufty_lmsk_scene_draw)
{
    ARM_2D_PARAM(bIsNewFrame);
    tufty_lmsk_scene_t *ptThis = (tufty_lmsk_scene_t *)pTarget;

    arm_2d_canvas(ptTile, canvas) {
        arm_2d_fill_colour(ptTile, &canvas, GLCD_COLOR_BLACK);

        if (this.lmsk_ready) {
            arm_2d_size_t box_size = this.film.use_as__arm_2d_tile_t.tRegion.tSize;
            box_size.iHeight += 10;

            arm_2d_align_centre(canvas, box_size) {
                arm_2d_dock_top(__centre_region, 10) {
                    arm_2d_fill_colour(ptTile, &__top_region, GLCD_COLOR_DARK_GREY);

                    arm_2d_dock_vertical(__top_region, 8, 2) {
                        arm_lcd_text_set_font(&ARM_2D_FONT_6x8.use_as__arm_2d_font_t);
                        arm_lcd_text_set_draw_region(&__vertical_region);
                        arm_lcd_text_set_colour(GLCD_COLOR_WHITE, GLCD_COLOR_WHITE);
                        arm_lcd_printf("Frame:%04"PRId16"\tFPS:%"PRId16,
                                        arm_2d_helper_film_get_frame_index(&this.film),
                                        this.fps);
                    }
                }

                arm_2d_fill_colour_with_mask(ptTile,
                                             &__centre_region,
                                             (const arm_2d_tile_t *)&this.film,
                                             (__arm_2d_color_t){this.colour});

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

static bool tufty_lmsk_scene_init_lmsk(tufty_lmsk_scene_t *ptThis)
{
    arm_2d_err_t err;
    arm_lmsk_loader_cfg_t cfg;

    err = arm_loader_io_fatfs_init(&this.lmsk_file, TUFTY_LMSK_FILE_PATH);
    if (ARM_2D_ERR_NONE != err) {
        printf("LMSK FatFs IO init failed: %d\r\n", err);
        return false;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.ptScene = (arm_2d_scene_t *)ptThis;
#if __ARM_LMSK_USE_LOADER_IO__
    cfg.ImageIO.ptIO = &ARM_LOADER_IO_FATFS;
    cfg.ImageIO.pTarget = (uintptr_t)&this.lmsk_file;
#else
#   error TUFTY_LMSK scene requires __ARM_LMSK_USE_LOADER_IO__
#endif

    err = arm_lmsk_loader_init(&this.animation, &cfg);
    if (ARM_2D_ERR_NONE != err) {
        printf("LMSK loader init failed: %d\r\n", err);
        return false;
    }

    this.film = (arm_2d_helper_film_t)
        impl_film(this.animation,
                  TUFTY_LMSK_FRAME_WIDTH,
                  TUFTY_LMSK_FRAME_HEIGHT,
                  TUFTY_LMSK_FRAME_COLUMNS,
                  TUFTY_LMSK_FRAME_COUNT,
                  TUFTY_LMSK_FRAME_PERIOD_MS);

    this.previous_colour = c_colour_table[0];
    this.colour_table_index = 1;
    this.colour = arm_2d_pixel_from_brga8888(c_colour_table[0]);

    printf("LMSK ready: %s frame=%dx%d cols=%u count=%u period=%ums\r\n",
           TUFTY_LMSK_FILE_PATH,
           TUFTY_LMSK_FRAME_WIDTH,
           TUFTY_LMSK_FRAME_HEIGHT,
           (unsigned)TUFTY_LMSK_FRAME_COLUMNS,
           (unsigned)TUFTY_LMSK_FRAME_COUNT,
           (unsigned)TUFTY_LMSK_FRAME_PERIOD_MS);

    return true;
}

static tufty_lmsk_scene_t *tufty_lmsk_scene_init(arm_2d_scene_player_t *player,
                                                 tufty_lmsk_scene_t *ptThis)
{
    bool user_allocated = false;

    assert(NULL != player);

    if (NULL == ptThis) {
        ptThis = (tufty_lmsk_scene_t *)
            __arm_2d_allocate_scratch_memory(sizeof(tufty_lmsk_scene_t),
                                             __alignof__(tufty_lmsk_scene_t),
                                             ARM_2D_MEM_TYPE_UNSPECIFIED);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        user_allocated = true;
    }

    memset(ptThis, 0, sizeof(*ptThis));

    *ptThis = (tufty_lmsk_scene_t) {
        .use_as__arm_2d_scene_t = {
            .tCanvas = {GLCD_COLOR_BLACK},
            .fnOnLoad = tufty_lmsk_scene_on_load,
            .fnScene = tufty_lmsk_scene_draw,
            .fnOnFrameStart = tufty_lmsk_scene_frame_start,
            .fnOnFrameCPL = tufty_lmsk_scene_frame_complete,
            .fnDepose = tufty_lmsk_scene_depose,
            .bUseDirtyRegionHelper = true,
        },
        .user_allocated = user_allocated,
    };

    this.lmsk_ready = tufty_lmsk_scene_init_lmsk(ptThis);

    arm_2d_scene_player_append_scenes(player,
                                      &this.use_as__arm_2d_scene_t,
                                      1);

    return ptThis;
}

void tufty_lmsk_scene_loader(void)
{
    (void)tufty_lmsk_scene_init(&DISP0_ADAPTER, NULL);
}
