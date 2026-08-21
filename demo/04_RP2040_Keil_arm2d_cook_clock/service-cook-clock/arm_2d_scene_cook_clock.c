/*============================ INCLUDES ======================================*/

#define __USER_SCENE_COOK_CLOCK_IMPLEMENT__
#include "arm_2d_scene_cook_clock.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#include <assert.h>
#include <string.h>
#include "pico/time.h"
#include "cook_clock_wheel_masks.h"

/*============================ MACROS ========================================*/

#define COOK_CLOCK_COUNTDOWN_SECONDS        300
#define COOK_CLOCK_WHEEL_DIAMETER            220
#define COOK_CLOCK_MICROSECONDS_PER_SECOND    1000000ULL
#define COOK_CLOCK_FULL_PROGRESS                1000
#define COOK_CLOCK_MAX_VISIBLE_PROGRESS         (COOK_CLOCK_FULL_PROGRESS - 1)

/*============================ MACROFIED FUNCTIONS ===========================*/

#undef this
#define this (*ptThis)

extern const
struct {
    implement(arm_2d_user_font_t);
    arm_2d_char_idx_t tUTF8Table;
} ARM_2D_FONT_ALARM_CLOCK_64_A4;

extern const
struct {
    implement(arm_2d_user_font_t);
    arm_2d_char_idx_t tUTF8Table;
} ARM_2D_FONT_LiberationSansRegular32_A4;

IMPL_ARM_2D_REGION_LIST(s_tCookClockDirtyRegions, static)

    ADD_LAST_REGION_TO_LIST(s_tCookClockDirtyRegions,
        .tSize = {
            .iWidth = 240,
            .iHeight = 80,
        },
    ),

END_IMPL_ARM_2D_REGION_LIST(s_tCookClockDirtyRegions)

static user_scene_cook_clock_t *s_ptCookClock;

/*============================ IMPLEMENTATION ================================*/

static void __request_wheel_clear(user_scene_cook_clock_t *ptThis)
{
    this.wSecondsRemaining = 0u;
    this.iProgress = 0;
    this.bCountdownTextDirty = true;
    this.bCountdownFinished = false;
    this.bCountdownPaused = true;
    this.chWheelClearFrames = 2u;
}

static void __initialise_countdown_wheel(user_scene_cook_clock_t *ptThis)
{
    progress_wheel_cfg_t tWheelCFG = {
        .ptileArcMask = &c_tileCookClockWheelArcMask,
        .ptileDotMask = &c_tileCookClockWheelDotMask,
        .tWheelColour = this.tDisplayColour.tValue,
        .tDotColour = GLCD_COLOR_WHITE,
        .iWheelDiameter = COOK_CLOCK_WHEEL_DIAMETER,
        .bUseDirtyRegions = true,
        .u2StartPosition = PROGRESS_WHEEL_START_POSITION_TOP,
        .u15FullLength = COOK_CLOCK_FULL_PROGRESS,
    };

    progress_wheel_init(&this.tCountdownWheel,
                        &this.use_as__arm_2d_scene_t,
                        &tWheelCFG);
    progress_wheel_on_load(&this.tCountdownWheel);
}

static void __on_scene_cook_clock_load(arm_2d_scene_t *ptScene)
{
    user_scene_cook_clock_t *ptThis = (user_scene_cook_clock_t *)ptScene;

    progress_wheel_on_load(&this.tCountdownWheel);
}

static void __on_scene_cook_clock_depose(arm_2d_scene_t *ptScene)
{
    user_scene_cook_clock_t *ptThis = (user_scene_cook_clock_t *)ptScene;

    progress_wheel_depose(&this.tCountdownWheel);
    ptScene->ptPlayer = NULL;
    if (!this.bUserAllocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, ptScene);
    }
}

static void __on_scene_cook_clock_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_cook_clock_t *ptThis = (user_scene_cook_clock_t *)ptScene;

    if (this.bWheelColourDirty) {
        this.tDisplayColour = this.tPendingDisplayColour;
        this.bWheelColourDirty = false;
        this.bWheelRepaintPending = true;
        this.bCountdownTextDirty = true;
    }

    if (this.bWheelRepaintPending) {
        progress_wheel_depose(&this.tCountdownWheel);
        __initialise_countdown_wheel(ptThis);
        this.bWheelRepaintPending = false;
    }

    arm_2d_dirty_region_item_ignore_set(&s_tCookClockDirtyRegions[0],
                                        !this.bCountdownTextDirty);
    this.bCountdownTextDirty = false;
    progress_wheel_on_frame_start(&this.tCountdownWheel);
}

static void __on_scene_cook_clock_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_cook_clock_t *ptThis = (user_scene_cook_clock_t *)ptScene;

    if (0u == this.chWheelClearFrames) {
        return;
    }

    this.chWheelClearFrames--;
    if (0u != this.chWheelClearFrames) {
        return;
    }

    if (this.bCountdownStartPending) {
        this.wCountdownDuration = this.wPendingCountdownDuration;
        this.wSecondsRemaining = this.wPendingCountdownDuration;
        this.qwCountdownStartTime = time_us_64();
        this.iProgress = COOK_CLOCK_MAX_VISIBLE_PROGRESS;
        this.bCountdownStartPending = false;
        this.bWheelRepaintPending = true;
        this.bCountdownPaused = false;
        this.bCountdownTextDirty = true;
        return;
    }

    this.bCountdownFinished = true;
    this.bCountdownPaused = false;
    this.bCountdownTextDirty = true;

    if (NULL != this.fnOnCountdownFinished) {
        this.fnOnCountdownFinished(this.tCountdownFinishedEffect,
                                  this.pCountdownFinishedTarget);
    }
}

static
IMPL_PFB_ON_DRAW(__pfb_draw_scene_cook_clock_handler)
{
    user_scene_cook_clock_t *ptThis = (user_scene_cook_clock_t *)pTarget;

    ARM_2D_PARAM(bIsNewFrame);

    arm_2d_canvas(ptTile, __canvas) {
        if (!this.bCountdownFinished) {
            progress_wheel_show(&this.tCountdownWheel,
                                ptTile,
                                &__canvas,
                                this.iProgress,
                                255u,
                                bIsNewFrame);
        }

        arm_2d_align_centre(__canvas, 240, 80) {
            arm_lcd_text_set_target_framebuffer((arm_2d_tile_t *)ptTile);
            arm_lcd_text_set_draw_region(&__centre_region);
            arm_lcd_text_set_colour(this.tDisplayColour.tValue,
                                    GLCD_COLOR_BLACK);
                if (this.bCountdownFinished &&
                    (COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_BIRTHDAY ==
                     this.tCountdownFinishedEffect)) {
                    arm_lcd_text_set_font(
                        (const arm_2d_font_t *)&ARM_2D_FONT_LiberationSansRegular32_A4);
                    arm_lcd_printf_label(ARM_2D_ALIGN_CENTRE, "happy birthday");
                } else {
                arm_2d_size_t tTextSize;

                arm_lcd_text_set_font(
                    (const arm_2d_font_t *)&ARM_2D_FONT_ALARM_CLOCK_64_A4);
                tTextSize = arm_lcd_get_string_line_box(
                    "00:00", (const arm_2d_font_t *)&ARM_2D_FONT_ALARM_CLOCK_64_A4);
                arm_2d_align_centre(__centre_region, tTextSize) {
                    arm_lcd_text_set_draw_region(&__centre_region);
                    (void)arm_lcd_printf_to_buffer(
                        (const arm_2d_font_t *)&ARM_2D_FONT_ALARM_CLOCK_64_A4,
                        "%02lu:%02lu",
                        (unsigned long)(this.wSecondsRemaining / 60),
                        (unsigned long)(this.wSecondsRemaining % 60));
                    arm_lcd_printf_buffer(0);
                }
            }

            arm_lcd_text_set_target_framebuffer(NULL);
        }
    }

    ARM_2D_OP_WAIT_ASYNC();
    return arm_fsm_rt_cpl;
}

ARM_NONNULL(1)
user_scene_cook_clock_t *__arm_2d_scene_cook_clock_init(
                                    arm_2d_scene_player_t *ptDispAdapter,
                                    user_scene_cook_clock_t *ptThis)
{
    bool bUserAllocated = false;
    assert(NULL != ptDispAdapter);

    arm_2d_region_t tScreen = arm_2d_helper_pfb_get_display_area(
                                &ptDispAdapter->use_as__arm_2d_helper_pfb_t);
    arm_2d_align_centre(tScreen,
                        s_tCookClockDirtyRegions[0].tRegion.tSize) {
        s_tCookClockDirtyRegions[0].tRegion = __centre_region;
    }

    if (NULL == ptThis) {
        ptThis = (user_scene_cook_clock_t *)
                    __arm_2d_allocate_scratch_memory(
                                    sizeof(user_scene_cook_clock_t),
                                    __alignof__(user_scene_cook_clock_t),
                                    ARM_2D_MEM_TYPE_UNSPECIFIED);
        assert(NULL != ptThis);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
        memset(ptThis, 0, sizeof(user_scene_cook_clock_t));
    }

    *ptThis = (user_scene_cook_clock_t){
        .use_as__arm_2d_scene_t = {
            .tCanvas = {GLCD_COLOR_BLACK},
            .fnOnLoad = &__on_scene_cook_clock_load,
            .fnScene = &__pfb_draw_scene_cook_clock_handler,
            .ptDirtyRegion = (arm_2d_region_list_item_t *)s_tCookClockDirtyRegions,
            .fnOnFrameStart = &__on_scene_cook_clock_frame_start,
            .fnOnFrameCPL = &__on_scene_cook_clock_frame_complete,
            .fnDepose = &__on_scene_cook_clock_depose,
            .bUseDirtyRegionHelper = true,
        },
        .bUserAllocated = bUserAllocated,
        .wSecondsRemaining = 0u,
        .wCountdownDuration = 1u,
        .wPendingCountdownDuration = 1u,
        .qwCountdownStartTime = time_us_64(),
        .iProgress = 0,
        .bCountdownTextDirty = true,
        .bCountdownFinished = true,
        .tCountdownFinishedEffect =
            COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_DEFAULT,
        .tDisplayColour = GLCD_COLOR_GREEN,
        .tPendingDisplayColour = GLCD_COLOR_GREEN,
    };

    __initialise_countdown_wheel(ptThis);

    arm_2d_scene_player_append_scenes(ptDispAdapter,
                                      &this.use_as__arm_2d_scene_t,
                                      1);

    s_ptCookClock = ptThis;

    return ptThis;
}

void __arm_2d_scene_cook_clock_set_colour(arm_2d_color_rgb565_t tColour)
{
    assert(NULL != s_ptCookClock);

    if (s_ptCookClock->tPendingDisplayColour.tValue == tColour.tValue) {
        return;
    }

    s_ptCookClock->tPendingDisplayColour = tColour;
    s_ptCookClock->bWheelColourDirty = true;
}

void __arm_2d_scene_cook_clock_set_countdown(uint32_t wDurationInSeconds)
{
    assert(NULL != s_ptCookClock);

    s_ptCookClock->wCountdownDuration = MAX(1u, wDurationInSeconds);
    s_ptCookClock->wSecondsRemaining = s_ptCookClock->wCountdownDuration;
    s_ptCookClock->qwCountdownStartTime = time_us_64();
    s_ptCookClock->iProgress = COOK_CLOCK_MAX_VISIBLE_PROGRESS;
    s_ptCookClock->bCountdownTextDirty = true;
    s_ptCookClock->bCountdownFinished = false;
    s_ptCookClock->bCountdownPaused = false;
}

void __arm_2d_scene_cook_clock_toggle_pause(void)
{
    assert(NULL != s_ptCookClock);

    if (s_ptCookClock->bCountdownFinished) {
        return;
    }

    if (s_ptCookClock->bCountdownPaused) {
        s_ptCookClock->qwCountdownStartTime += time_us_64()
                                             - s_ptCookClock->qwCountdownPausedTime;
        s_ptCookClock->bCountdownPaused = false;
    } else {
        s_ptCookClock->qwCountdownPausedTime = time_us_64();
        s_ptCookClock->bCountdownPaused = true;
    }
}

void __arm_2d_scene_cook_clock_finish_countdown(void)
{
    assert(NULL != s_ptCookClock);

    if (s_ptCookClock->bCountdownFinished) {
        return;
    }

    s_ptCookClock->bCountdownStartPending = false;
    __request_wheel_clear(s_ptCookClock);
}

void __arm_2d_scene_cook_clock_set_countdown_finished_effect(
                                    cook_clock_countdown_finished_effect_t tEffect)
{
    assert(NULL != s_ptCookClock);

    if (COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_BIRTHDAY != tEffect) {
        tEffect = COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_DEFAULT;
    }

    s_ptCookClock->tCountdownFinishedEffect = tEffect;
}

void __arm_2d_scene_cook_clock_set_countdown_finished_handler(
                                    cook_clock_countdown_finished_handler_t fnHandler,
                                    void *pTarget)
{
    assert(NULL != s_ptCookClock);

    s_ptCookClock->fnOnCountdownFinished = fnHandler;
    s_ptCookClock->pCountdownFinishedTarget = pTarget;
}

void __arm_2d_scene_cook_clock_task(void)
{
    assert(NULL != s_ptCookClock);

    if (s_ptCookClock->bCountdownPaused ||
        s_ptCookClock->bCountdownFinished) {
        return;
    }

    uint64_t qwElapsedInMicroseconds =
        time_us_64() - s_ptCookClock->qwCountdownStartTime;
    uint64_t qwDurationInMicroseconds =
        (uint64_t)s_ptCookClock->wCountdownDuration
        * COOK_CLOCK_MICROSECONDS_PER_SECOND;
    uint32_t wSecondsRemaining;
    int16_t iProgress;

    if (qwElapsedInMicroseconds >= qwDurationInMicroseconds) {
        wSecondsRemaining = 0;
        iProgress = 0;
    } else {
        wSecondsRemaining = s_ptCookClock->wCountdownDuration
            - (uint32_t)(qwElapsedInMicroseconds
                         / COOK_CLOCK_MICROSECONDS_PER_SECOND);
        iProgress = (int16_t)(
            (((qwDurationInMicroseconds - qwElapsedInMicroseconds)
              * COOK_CLOCK_FULL_PROGRESS)
             + (qwDurationInMicroseconds / 2u)) /
            qwDurationInMicroseconds);
        iProgress = MIN(iProgress, COOK_CLOCK_MAX_VISIBLE_PROGRESS);
    }

    if ((wSecondsRemaining != s_ptCookClock->wSecondsRemaining) ||
        (iProgress != s_ptCookClock->iProgress)) {
        bool const bSecondsChanged =
            (wSecondsRemaining != s_ptCookClock->wSecondsRemaining);
        bool const bCountdownFinished = (0u == wSecondsRemaining) &&
                                      (0u != s_ptCookClock->wSecondsRemaining);

        s_ptCookClock->wSecondsRemaining = wSecondsRemaining;
        s_ptCookClock->iProgress = iProgress;
        s_ptCookClock->bCountdownTextDirty = bSecondsChanged;

        if (bCountdownFinished) {
            __request_wheel_clear(s_ptCookClock);
        }
    }
}

#endif