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

#define __USER_SCENE_SHOW_DATA_IMPLEMENT__
#include "arm_2d_scene_show_data.h"
#include "../service_sensor.h"

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

#define SHOW_DATA_SENSOR_COUNT          9
#define SHOW_DATA_GRID_MARGIN           12      //!< 九宫格与屏幕四边的距离，单位：像素。
#define SHOW_DATA_GRID_GAP              6       //!< 相邻行或列之间的空白距离，单位：像素。
#define SHOW_DATA_BACKGROUND_COLOUR     __RGB(4, 12, 28)
#define SHOW_DATA_PANEL_COLOUR          __RGB(14, 42, 78)
#define SHOW_DATA_TEXT_COLOUR           __RGB(174, 229, 255)

// #if __GLCD_CFG_COLOUR_DEPTH__ == 8

// #   define c_tileCMSISLogo          c_tileCMSISLogoGRAY8

// #elif __GLCD_CFG_COLOUR_DEPTH__ == 16

// #   define c_tileCMSISLogo          c_tileCMSISLogoRGB565

// #elif __GLCD_CFG_COLOUR_DEPTH__ == 32

// #   define c_tileCMSISLogo          c_tileCMSISLogoCCCA8888
// #else
// #   error Unsupported colour depth!
// #endif

/*============================ MACROFIED FUNCTIONS ===========================*/
#undef this
#define this (*ptThis)

/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/

extern const
struct {
    implement(arm_2d_user_font_t);
    arm_2d_char_idx_t tUTF8Table;
} ARM_2D_FONT_LiberationSansRegular14_A4;

/*============================ PROTOTYPES ====================================*/
/*============================ LOCAL VARIABLES ===============================*/

/*! One independently enabled dirty region for each sensor cell. */
IMPL_ARM_2D_REGION_LIST(s_tDirtyRegions, static)

    ADD_REGION_TO_LIST(s_tDirtyRegions, 0),
    ADD_REGION_TO_LIST(s_tDirtyRegions, 0),
    ADD_REGION_TO_LIST(s_tDirtyRegions, 0),
    ADD_REGION_TO_LIST(s_tDirtyRegions, 0),
    ADD_REGION_TO_LIST(s_tDirtyRegions, 0),
    ADD_REGION_TO_LIST(s_tDirtyRegions, 0),
    ADD_REGION_TO_LIST(s_tDirtyRegions, 0),
    ADD_REGION_TO_LIST(s_tDirtyRegions, 0),
    ADD_LAST_REGION_TO_LIST(s_tDirtyRegions, 0),

END_IMPL_ARM_2D_REGION_LIST(s_tDirtyRegions)

/*============================ IMPLEMENTATION ================================*/

/*! \brief 将一行 dock 区域切分为三个单元，并绑定到场景脏区域列表中连续的条目。
 *
 * dock 操作从 tRow 的左侧依次取出区域。生成的三个矩形分别复制到
 * s_tDirtyRegions[chFirstIndex..chFirstIndex + 2]；该列表通过
 * arm_2d_scene_t::ptDirtyRegion 连接到场景。
 */
static void __initialise_sensor_dirty_region_row(arm_2d_region_t tRow,
                                                 uint8_t chFirstIndex)
{
    /* 一行预留两个列间距后，剩余宽度均分给三个传感器单元。 */
    int16_t iColumnWidth = (int16_t)((tRow.tSize.iWidth
                            - (2 * SHOW_DATA_GRID_GAP)) / 3);

    /* arm_2d_layout 保存共享的布局游标，内部每个 item dock 都会消耗
     * 当前剩余区域，因此三项得到不同的横向位置。 */
    arm_2d_layout(tRow) {
        __item_line_dock_horizontal(iColumnWidth) {
            /* __item_region 已包含本格的左上角和宽高，整体赋值到脏区域。 */
            s_tDirtyRegions[chFirstIndex].tRegion = __item_region;
        }
        /* 消耗第一处列间距；不写入脏区域，因此该空白不会触发刷新。 */
        __item_line_dock_horizontal(SHOW_DATA_GRID_GAP) {
        }
        __item_line_dock_horizontal(iColumnWidth) {
            s_tDirtyRegions[chFirstIndex + 1].tRegion = __item_region;
        }
        /* 消耗第二处列间距。 */
        __item_line_dock_horizontal(SHOW_DATA_GRID_GAP) {
        }
        /* 最后一格取全部剩余宽度，包含整数除法时余下的像素。 */
        __item_line_dock_horizontal() {
            s_tDirtyRegions[chFirstIndex + 2].tRegion = __item_region;
        }
    }
}

/*! \brief 基于显示区域构建 3 x 3 的传感器脏区域布局。
 *
 * 先移除外边距，再通过 arm_2d_layout 和纵向 item dock 切分出三行。
 * 每一行都传给 __initialise_sensor_dirty_region_row()，将其三个单元绑定到
 * s_tDirtyRegions[0..8]。PFB Helper 随后通过场景的 ptDirtyRegion
 * 成员使用该链表。
 */
static void __initialise_sensor_dirty_regions(arm_2d_region_t tScreen)
{
    /* tGrid 从完整显示区域开始，后续 dock 操作会逐步缩小该剩余区域。 */
    arm_2d_region_t tGrid = tScreen;

    /* 四边各扣除 SHOW_DATA_GRID_MARGIN，__dock_region 是九宫格的可用矩形。 */
    arm_2d_dock_with_margin(tGrid, SHOW_DATA_GRID_MARGIN) {
        tGrid = __dock_region;
    }

    /* 三行之间有两个行间距，将其扣除后把可用高度均分为三行。 */
    int16_t iRowHeight = (int16_t)((tGrid.tSize.iHeight
                        - (2 * SHOW_DATA_GRID_GAP)) / 3);

    /* 纵向 layout 与行内的横向 layout 相同，会为每项保留剩余区域游标。 */
    arm_2d_layout(tGrid) {
        __item_line_dock_vertical(iRowHeight) {
            /* 第一行绑定 S1、S2、S3，即脏区域索引 0、1、2。 */
            __initialise_sensor_dirty_region_row(__item_region, 0);
        }
        /* 消耗第一处行间距。 */
        __item_line_dock_vertical(SHOW_DATA_GRID_GAP) {
        }
        __item_line_dock_vertical(iRowHeight) {
            /* 第二行绑定 S4、S5、S6，即脏区域索引 3、4、5。 */
            __initialise_sensor_dirty_region_row(__item_region, 3);
        }
        /* 消耗第二处行间距。 */
        __item_line_dock_vertical(SHOW_DATA_GRID_GAP) {
        }
        /* 最后一行取全部剩余高度，包含整数除法时余下的像素。 */
        __item_line_dock_vertical() {
            /* 第三行绑定 S7、S8、S9，即脏区域索引 6、7、8。 */
            __initialise_sensor_dirty_region_row(__item_region, 6);
        }
    }
}

static void __on_scene_show_data_load(arm_2d_scene_t *ptScene)
{
    user_scene_show_data_t *ptThis = (user_scene_show_data_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static void __after_scene_show_data_switching(arm_2d_scene_t *ptScene)
{
    user_scene_show_data_t *ptThis = (user_scene_show_data_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static void __on_scene_show_data_depose(arm_2d_scene_t *ptScene)
{
    user_scene_show_data_t *ptThis = (user_scene_show_data_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

    /*--------------------- insert your depose code begin --------------------*/


    /*---------------------- insert your depose code end  --------------------*/

    arm_foreach(int64_t,this.lTimestamp, ptItem) {
        *ptItem = 0;
    }
    ptScene->ptPlayer = NULL;
    if (!this.bUserAllocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, ptScene);
    }
}

/*----------------------------------------------------------------------------*
 * Scene show_data                                                                    *
 *----------------------------------------------------------------------------*/
#if 0  /* deprecated */
static void __on_scene_show_data_background_start(arm_2d_scene_t *ptScene)
{
    user_scene_show_data_t *ptThis = (user_scene_show_data_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static void __on_scene_show_data_background_complete(arm_2d_scene_t *ptScene)
{
    user_scene_show_data_t *ptThis = (user_scene_show_data_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}
#endif


static void __on_scene_show_data_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_show_data_t *ptThis = (user_scene_show_data_t *)ptScene;

    /* S1 displays the battery percentage supplied by the sensor service cache. */
    arm_2d_scene_show_data_set_sensor_value(ptThis,
                                            0,
                                            g_service_sensor.battery_voltage);
    arm_2d_scene_show_data_set_sensor_value(ptThis,
                                            1,
                                            g_service_sensor.battery_percentage);

    for (uint8_t chIndex = 0; chIndex < SHOW_DATA_SENSOR_COUNT; chIndex++) {
        arm_2d_dirty_region_item_ignore_set(&s_tDirtyRegions[chIndex],
                                            !this.bSensorDirty[chIndex]);
        this.bSensorDirty[chIndex] = false;
    }

}

static void __on_scene_show_data_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_show_data_t *ptThis = (user_scene_show_data_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static void __before_scene_show_data_switching_out(arm_2d_scene_t *ptScene)
{
    user_scene_show_data_t *ptThis = (user_scene_show_data_t *)ptScene;
    ARM_2D_UNUSED(ptThis);

}

static
IMPL_PFB_ON_DRAW(__pfb_draw_scene_show_data_handler)
{
    ARM_2D_PARAM(pTarget);
    ARM_2D_PARAM(ptTile);
    ARM_2D_PARAM(bIsNewFrame);

    user_scene_show_data_t *ptThis = (user_scene_show_data_t *)pTarget;

    arm_2d_canvas(ptTile, __top_canvas) {
    /*-----------------------draw the scene begin-----------------------*/

        arm_lcd_text_set_target_framebuffer((arm_2d_tile_t *)ptTile);
        arm_lcd_text_set_font((const arm_2d_font_t *)&ARM_2D_FONT_LiberationSansRegular14_A4);

        for (uint8_t chIndex = 0; chIndex < SHOW_DATA_SENSOR_COUNT; chIndex++) {
            arm_2d_region_t *ptRegion = &s_tDirtyRegions[chIndex].tRegion;

            /* 每个完整脏区域即为文字外侧的深蓝色背景框。 */
            arm_2d_fill_colour(ptTile, ptRegion, SHOW_DATA_PANEL_COLOUR);
            arm_lcd_text_set_draw_region(ptRegion);
            arm_lcd_text_set_colour(SHOW_DATA_TEXT_COLOUR, SHOW_DATA_PANEL_COLOUR);

            switch (chIndex)
            {
            case 0:
                arm_lcd_printf_label(ARM_2D_ALIGN_CENTRE,
                                     "bat:%.2fv",
                                     (double)this.fSensorValues[chIndex]);
                break;

            case 1:
                arm_lcd_printf_label(ARM_2D_ALIGN_CENTRE,
                                     "bat: %.1f%%",
                                     (double)this.fSensorValues[chIndex]);
                break;

            default:
                arm_lcd_printf_label(ARM_2D_ALIGN_CENTRE,
                                     "S%u: %.1f",
                                     (unsigned int)(chIndex + 1),
                                     (double)this.fSensorValues[chIndex]);
                break;
            }

        }
        arm_lcd_text_set_target_framebuffer(NULL);

    /*-----------------------draw the scene end  -----------------------*/
    }
    ARM_2D_OP_WAIT_ASYNC();

    return arm_fsm_rt_cpl;
}

ARM_NONNULL(1)
user_scene_show_data_t *__arm_2d_scene_show_data_init(   arm_2d_scene_player_t *ptDispAdapter,
                                        user_scene_show_data_t *ptThis)
{
    bool bUserAllocated = false;
    assert(NULL != ptDispAdapter);

    s_tDirtyRegions[SHOW_DATA_SENSOR_COUNT - 1].ptNext = NULL;

    arm_2d_region_t tScreen
        = arm_2d_helper_pfb_get_display_area(
            &ptDispAdapter->use_as__arm_2d_helper_pfb_t);

    __initialise_sensor_dirty_regions(tScreen);

    if (NULL == ptThis) {
        ptThis = (user_scene_show_data_t *)
                    __arm_2d_allocate_scratch_memory(   sizeof(user_scene_show_data_t),
                                                        __alignof__(user_scene_show_data_t),
                                                        ARM_2D_MEM_TYPE_UNSPECIFIED);
        assert(NULL != ptThis);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
    }

    memset(ptThis, 0, sizeof(user_scene_show_data_t));

    *ptThis = (user_scene_show_data_t){
        .use_as__arm_2d_scene_t = {

            /* the canvas colour */
            .tCanvas = {SHOW_DATA_BACKGROUND_COLOUR},

            /* Please uncommon the callbacks if you need them
             */
            .fnOnLoad       = &__on_scene_show_data_load,
            .fnScene        = &__pfb_draw_scene_show_data_handler,
            .fnAfterSwitch  = &__after_scene_show_data_switching,

            .ptDirtyRegion  = (arm_2d_region_list_item_t *)s_tDirtyRegions,

            //.fnOnBGStart    = &__on_scene_show_data_background_start,        /* deprecated */
            //.fnOnBGComplete = &__on_scene_show_data_background_complete,     /* deprecated */
            .fnOnFrameStart = &__on_scene_show_data_frame_start,
            .fnBeforeSwitchOut = &__before_scene_show_data_switching_out,
            .fnOnFrameCPL   = &__on_scene_show_data_frame_complete,
            .fnDepose       = &__on_scene_show_data_depose,

            .bUseDirtyRegionHelper = true,
        },
        .bUserAllocated = bUserAllocated,
    };

    /* ------------   initialize members of user_scene_show_data_t begin ---------------*/

    for (uint8_t chIndex = 0; chIndex < SHOW_DATA_SENSOR_COUNT; chIndex++) {
        this.bSensorDirty[chIndex] = true;
    }

    /* ------------   initialize members of user_scene_show_data_t end   ---------------*/

    arm_2d_scene_player_append_scenes(  ptDispAdapter,
                                        &this.use_as__arm_2d_scene_t,
                                        1);

    return ptThis;
}

void arm_2d_scene_show_data_set_sensor_value(user_scene_show_data_t *ptScene,
                                              uint8_t chIndex,
                                              float fValue)
{
    assert(NULL != ptScene);
    if ((NULL == ptScene) || (chIndex >= SHOW_DATA_SENSOR_COUNT)) {
        return;
    }

    if (ptScene->fSensorValues[chIndex] != fValue) {
        ptScene->fSensorValues[chIndex] = fValue;
        ptScene->bSensorDirty[chIndex] = true;
    }
}


#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#endif
