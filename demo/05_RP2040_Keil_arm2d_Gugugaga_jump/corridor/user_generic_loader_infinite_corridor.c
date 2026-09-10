/*
 * Copyright (c) 2009-2025 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*============================ INCLUDES ======================================*/
#define __GENERIC_LOADER_INHERIT__
#define __USER_GENERIC_LOADER_INFINITE_CORRIDOR_IMPLEMENT__
#include "user_generic_loader_infinite_corridor.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB) && defined(RTE_Acceleration_Arm_2D_Extra_Loader)

#include <assert.h>
#include <string.h>

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wunknown-warning-option"
#   pragma clang diagnostic ignored "-Wreserved-identifier"
#   pragma clang diagnostic ignored "-Wdeclaration-after-statement"
#   pragma clang diagnostic ignored "-Wsign-conversion"
#   pragma clang diagnostic ignored "-Wpadded"
#   pragma clang diagnostic ignored "-Wcast-qual"
#   pragma clang diagnostic ignored "-Wcast-align"
#   pragma clang diagnostic ignored "-Wmissing-field-initializers"
#   pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#   pragma clang diagnostic ignored "-Wmissing-braces"
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#   pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#elif __IS_COMPILER_ARM_COMPILER_5__
#   pragma diag_suppress 64,177
#elif __IS_COMPILER_IAR__
#   pragma diag_suppress=Pa089,Pe188,Pe177,Pe174
#endif

#if __GLCD_CFG_COLOUR_DEPTH__ != 16
#   error user_generic_loader_infinite_corridor requires an RGB565 target
#endif

/*============================ MACROS ========================================*/

#define USER_INFINITE_CORRIDOR_ZOOM_PERIOD_MS        1800u
#define USER_INFINITE_CORRIDOR_CAMERA_PERIOD_MS      7200u
#define USER_INFINITE_CORRIDOR_ZOOM_SCALE_Q16        93061u
#define USER_INFINITE_CORRIDOR_HUE_RANGE              1536u
#define USER_INFINITE_CORRIDOR_GLOW_WIDTH                 2u
#define USER_INFINITE_CORRIDOR_PERSPECTIVE_RADIUS       180u
#define USER_INFINITE_CORRIDOR_SCALE_X_Q8               304
#define USER_INFINITE_CORRIDOR_SHEAR_Q8                  40

#undef this
#define this    (*ptThis)

/*============================ PROTOTYPES ====================================*/

ARM_NONNULL(1)
static
arm_2d_err_t __user_generic_loader_infinite_corridor_decoder_init(
                                                arm_generic_loader_t *ptObj);

ARM_NONNULL(1, 2, 3)
static
arm_2d_err_t __user_generic_loader_infinite_corridor_draw(
                                                arm_generic_loader_t *ptObj,
                                                arm_2d_region_t *ptROI,
                                                uint8_t *pchBuffer,
                                                uint32_t iTargetStrideInByte,
                                                uint_fast8_t chBitsPerPixel);

/*============================ LOCAL VARIABLES ===============================*/

static const uint16_t c_hwBaseRadius[USER_INFINITE_CORRIDOR_FRAME_COUNT] = {
      4,   6,   9,  13,  19,  27,  39,  56,  80, 114, 162,
};

/*============================ IMPLEMENTATION ================================*/

static uint16_t __user_infinite_corridor_rgb565(uint16_t hwHue,
                                                 uint8_t chIntensity)
{
    uint8_t chSegment = (uint8_t)(hwHue >> 8);
    uint8_t chOffset = (uint8_t)hwHue;
    uint8_t chRise = chOffset;
    uint8_t chFall = (uint8_t)(255u - chOffset);
    uint8_t chRed = 0;
    uint8_t chGreen = 0;
    uint8_t chBlue = 0;

    switch (chSegment) {
        case 0:
            chRed = 255;
            chGreen = chRise;
            break;
        case 1:
            chRed = chFall;
            chGreen = 255;
            break;
        case 2:
            chGreen = 255;
            chBlue = chRise;
            break;
        case 3:
            chGreen = chFall;
            chBlue = 255;
            break;
        case 4:
            chRed = chRise;
            chBlue = 255;
            break;
        default:
            chRed = 255;
            chBlue = chFall;
            break;
    }

    chRed = (uint8_t)(((uint16_t)chRed * chIntensity) >> 8);
    chGreen = (uint8_t)(((uint16_t)chGreen * chIntensity) >> 8);
    chBlue = (uint8_t)(((uint16_t)chBlue * chIntensity) >> 8);

    return (uint16_t)(((uint16_t)(chRed & 0xF8u) << 8)
                    | ((uint16_t)(chGreen & 0xFCu) << 3)
                    | ((uint16_t)chBlue >> 3));
}

static uint16_t __user_infinite_corridor_octagon_radius(int32_t iX,
                                                         int32_t iY)
{
    uint32_t wAbsX = (uint32_t)((iX < 0) ? -iX : iX);
    uint32_t wAbsY = (uint32_t)((iY < 0) ? -iY : iY);
    uint32_t wDiagonal = ((wAbsX + wAbsY) * 181u) >> 8;
    uint32_t wRadius = MAX(wAbsX, wAbsY);

    return (uint16_t)MAX(wRadius, wDiagonal);
}

ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_update(
                        user_generic_loader_infinite_corridor_t *ptThis,
                        uint32_t wElapsedInMs)
{
    assert(NULL != ptThis);

    uint32_t wZoomPhaseQ16 =
        ((wElapsedInMs % USER_INFINITE_CORRIDOR_ZOOM_PERIOD_MS) << 16)
        / USER_INFINITE_CORRIDOR_ZOOM_PERIOD_MS;
    uint32_t wZoomScaleQ16 = 65536u
        + (uint32_t)(((uint64_t)wZoomPhaseQ16
                    * (USER_INFINITE_CORRIDOR_ZOOM_SCALE_Q16 - 65536u))
                    >> 16);
    q31_t q31CameraAngle = (q31_t)(
        ((uint64_t)(wElapsedInMs % USER_INFINITE_CORRIDOR_CAMERA_PERIOD_MS)
        * 2147483648ULL) / USER_INFINITE_CORRIDOR_CAMERA_PERIOD_MS);
    int32_t iVanishingOffsetX = -26
        + (arm_sin_q31(q31CameraAngle) >> 27);
    int32_t iVanishingOffsetY = -14
        + (arm_cos_q31(q31CameraAngle) >> 28);

    this.Runtime.iScaleXQ8 = USER_INFINITE_CORRIDOR_SCALE_X_Q8;
    this.Runtime.iShearQ8 = USER_INFINITE_CORRIDOR_SHEAR_Q8;

    for (uint_fast8_t chIndex = 0;
         chIndex < USER_INFINITE_CORRIDOR_FRAME_COUNT;
         chIndex++) {
        uint32_t wRadius = ((uint32_t)c_hwBaseRadius[chIndex]
                          * wZoomScaleQ16 + 32768u) >> 16;
        uint32_t wVisibleRadius = MIN(wRadius, 170u);
        uint8_t chIntensity = (uint8_t)(72u
            + (wVisibleRadius * 183u) / 170u);
        uint16_t hwHue = (uint16_t)(
            ((wElapsedInMs / 7u) + (uint32_t)chIndex * 137u)
            % USER_INFINITE_CORRIDOR_HUE_RANGE);
        uint32_t wPerspectiveRadius = MIN(
            wRadius, USER_INFINITE_CORRIDOR_PERSPECTIVE_RADIUS);
        int32_t iFrameOffsetX =
            (iVanishingOffsetX
           * (int32_t)(USER_INFINITE_CORRIDOR_PERSPECTIVE_RADIUS
                     - wPerspectiveRadius))
            / (int32_t)USER_INFINITE_CORRIDOR_PERSPECTIVE_RADIUS;
        int32_t iFrameOffsetY =
            (iVanishingOffsetY
           * (int32_t)(USER_INFINITE_CORRIDOR_PERSPECTIVE_RADIUS
                     - wPerspectiveRadius))
            / (int32_t)USER_INFINITE_CORRIDOR_PERSPECTIVE_RADIUS;

        this.Runtime.hwRadius[chIndex] = (uint16_t)wRadius;
        this.Runtime.chHalfWidth[chIndex] = 0u;
        this.Runtime.iFrameOffsetX[chIndex] = (int16_t)(
            (iFrameOffsetX * this.Runtime.iScaleXQ8) >> 8);
        this.Runtime.iFrameOffsetY[chIndex] = (int16_t)(
            iFrameOffsetY
          + ((iFrameOffsetX * this.Runtime.iShearQ8) >> 8));
        this.Runtime.hwColour[chIndex][0] =
            __user_infinite_corridor_rgb565(hwHue, chIntensity);
        this.Runtime.hwColour[chIndex][1] =
            __user_infinite_corridor_rgb565(hwHue,
                                             (uint8_t)(chIntensity >> 1));
        this.Runtime.hwColour[chIndex][2] =
            __user_infinite_corridor_rgb565(hwHue,
                                             (uint8_t)(chIntensity >> 2));
    }
}

ARM_NONNULL(1, 2)
arm_2d_err_t user_generic_loader_infinite_corridor_init(
                        user_generic_loader_infinite_corridor_t *ptThis,
                        user_generic_loader_infinite_corridor_cfg_t *ptCFG)
{
    assert(NULL != ptThis);
    assert(NULL != ptCFG);
    memset(ptThis, 0, sizeof(user_generic_loader_infinite_corridor_t));

    this.tCFG = *ptCFG;
    if ((this.tCFG.tSize.iWidth <= 0)
     || (this.tCFG.tSize.iHeight <= 0)) {
        return ARM_2D_ERR_INVALID_PARAM;
    }

    arm_generic_loader_cfg_t tCFG = {
        .bUseHeapForVRES = this.tCFG.bUseHeapForVRES,
        .tColourInfo.chScheme = ARM_2D_COLOUR_RGB565,
        .bBlendWithBG = false,
        .ImageIO = {
            .ptIO = this.tCFG.ImageIO.ptIO,
            .pTarget = this.tCFG.ImageIO.pTarget,
        },
        .UserDecoder = {
            .fnDecoderInit =
                &__user_generic_loader_infinite_corridor_decoder_init,
            .fnDecode = &__user_generic_loader_infinite_corridor_draw,
        },
        .ptScene = this.tCFG.ptScene,
    };

    arm_2d_err_t tResult = arm_generic_loader_init(
                            &this.use_as__arm_generic_loader_t,
                            &tCFG);
    if (tResult < 0) {
        return tResult;
    }

    this.tTile.tRegion.tSize = this.tCFG.tSize;
    user_generic_loader_infinite_corridor_update(ptThis, 0u);
    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_depose(
                        user_generic_loader_infinite_corridor_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_depose(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_on_load(
                        user_generic_loader_infinite_corridor_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_on_load(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_on_frame_start(
                        user_generic_loader_infinite_corridor_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_on_frame_start(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_on_frame_complete(
                        user_generic_loader_infinite_corridor_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_on_frame_complete(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void user_generic_loader_infinite_corridor_show(
                        user_generic_loader_infinite_corridor_t *ptThis,
                        const arm_2d_tile_t *ptTile,
                        const arm_2d_region_t *ptRegion,
                        bool bIsNewFrame)
{
    assert(NULL != ptThis);
    ARM_2D_UNUSED(bIsNewFrame);

    if (-1 == (intptr_t)ptTile) {
        ptTile = arm_2d_get_default_frame_buffer();
    }

    arm_2d_tile_copy_only(&this.tTile, ptTile, ptRegion);
}

ARM_NONNULL(1)
static
arm_2d_err_t __user_generic_loader_infinite_corridor_decoder_init(
                                                arm_generic_loader_t *ptObj)
{
    assert(NULL != ptObj);
    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1, 2, 3)
static
arm_2d_err_t __user_generic_loader_infinite_corridor_draw(
                                                arm_generic_loader_t *ptObj,
                                                arm_2d_region_t *ptROI,
                                                uint8_t *pchBuffer,
                                                uint32_t iTargetStrideInByte,
                                                uint_fast8_t chBitsPerPixel)
{
    assert(NULL != ptObj);
    user_generic_loader_infinite_corridor_t *ptThis =
        (user_generic_loader_infinite_corridor_t *)ptObj;

    if (16u != chBitsPerPixel) {
        return ARM_2D_ERR_INVALID_PARAM;
    }

    int32_t iCentreX = this.tCFG.tSize.iWidth >> 1;
    int32_t iCentreY = this.tCFG.tSize.iHeight >> 1;

    for (int_fast16_t iY = 0; iY < ptROI->tSize.iHeight; iY++) {
        int32_t iScreenY = ptROI->tLocation.iY + iY;
        int32_t iDY = iScreenY - iCentreY;
        uint16_t *phwPixel = (uint16_t *)(pchBuffer
                            + (uint32_t)iY * iTargetStrideInByte);

        for (int_fast16_t iX = 0; iX < ptROI->tSize.iWidth; iX++) {
            int32_t iScreenX = ptROI->tLocation.iX + iX;
            int32_t iDX = iScreenX - iCentreX;
            int32_t iTransformedX =
                (iDX * this.Runtime.iScaleXQ8) >> 8;
            int32_t iTransformedY = iDY
                + ((iDX * this.Runtime.iShearQ8) >> 8);
            uint16_t hwColour = 0u;

            for (uint_fast8_t chLayer = USER_INFINITE_CORRIDOR_FRAME_COUNT;
                 chLayer > 0;
                 chLayer--) {
                uint_fast8_t chFrame = chLayer - 1u;
                int32_t iFrameX = iTransformedX
                    - this.Runtime.iFrameOffsetX[chFrame];
                int32_t iFrameY = iTransformedY
                    - this.Runtime.iFrameOffsetY[chFrame];
                uint16_t hwOctagonRadius =
                    __user_infinite_corridor_octagon_radius(iFrameX,
                                                            iFrameY);
                uint16_t hwFrameRadius = this.Runtime.hwRadius[chFrame];
                uint16_t hwHalfWidth = this.Runtime.chHalfWidth[chFrame];
                uint16_t hwDistance = (hwOctagonRadius > hwFrameRadius)
                    ? hwOctagonRadius - hwFrameRadius
                    : hwFrameRadius - hwOctagonRadius;

                if (hwDistance <= hwHalfWidth) {
                    hwColour = this.Runtime.hwColour[chFrame][0];
                    break;
                }

                if (hwDistance <= hwHalfWidth
                               + USER_INFINITE_CORRIDOR_GLOW_WIDTH) {
                    uint_fast8_t chShade =
                        (uint_fast8_t)(hwDistance - hwHalfWidth);
                    hwColour = this.Runtime.hwColour[chFrame][chShade];
                    break;
                }

                if (hwOctagonRadius >= hwFrameRadius) {
                    break;
                }
            }

            *phwPixel++ = hwColour;
        }
    }

    return ARM_2D_ERR_NONE;
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#endif
