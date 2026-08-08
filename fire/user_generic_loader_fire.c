/*
 * Copyright (c) 2009-2025 Arm Limited. All rights reserved.
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
#define __GENERIC_LOADER_INHERIT__
#define __fire_sim_IMPLEMENT__

#include "user_generic_loader_fire.h"
#include "fire_sim.h"
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
#   pragma clang diagnostic ignored "-Wunused-const-variable"
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#   pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif

/*============================ MACROS ========================================*/

#if __GLCD_CFG_COLOUR_DEPTH__ == 8


#elif __GLCD_CFG_COLOUR_DEPTH__ == 16


#elif __GLCD_CFG_COLOUR_DEPTH__ == 32

#else
#   error Unsupported colour depth!
#endif

#undef this
#define this    (*ptThis)

#define FIRE_VIEW_W            60
#define FIRE_VIEW_H            60
#define FIRE_FAST_SCALE        4

#ifndef FIRE_RENDER_USE_GAUSSIAN_SMOOTHING
#   define FIRE_RENDER_USE_GAUSSIAN_SMOOTHING    1
#endif

#ifndef FIRE_RENDER_USE_BILINEAR_UPSCALE
#   define FIRE_RENDER_USE_BILINEAR_UPSCALE       0
#endif

/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/
ARM_NONNULL(1)
static
arm_2d_err_t __fire_sim_decoder_init(arm_generic_loader_t *ptObj);

ARM_NONNULL(1, 2, 3)
static
arm_2d_err_t __fire_sim_draw(  arm_generic_loader_t *ptObj,
                                    arm_2d_region_t *ptROI,
                                    uint8_t *pchBuffer,
                                    uint32_t iTargetStrideInByte,
                                    uint_fast8_t chBitsPerPixel);

/*============================ LOCAL VARIABLES ===============================*/
static uint16_t s_hwFireRGB565[FIRE_VIEW_W * FIRE_VIEW_H];
static int16_t s_nCachedFrameNr = -1;
static Fluid *s_ptCachedFluid = NULL;

/*============================ IMPLEMENTATION ================================*/

__STATIC_FORCEINLINE void __fire_sim_store_2px(uint32_t *pwTarget,
                                                uint32_t wPixels)
{
    memcpy(pwTarget, &wPixels, sizeof(wPixels));
}

#if FIRE_RENDER_USE_BILINEAR_UPSCALE
__STATIC_FORCEINLINE uint16_t __fire_sim_rgb565_average(uint16_t hwA,
                                                        uint16_t hwB)
{
    uint32_t wCommon = (uint32_t)(hwA & hwB);
    uint32_t wHalfDifference = ((uint32_t)(hwA ^ hwB) & 0xF7DEu) >> 1;

    return (uint16_t)(wCommon + wHalfDifference);
}

__STATIC_FORCEINLINE uint16_t __fire_sim_rgb565_lerp_quarter(uint16_t hwA,
                                                             uint16_t hwB,
                                                             uint_fast8_t chPhase)
{
    if (0u == chPhase) {
        return hwA;
    }

    uint16_t hwMiddle = __fire_sim_rgb565_average(hwA, hwB);
    if (2u == chPhase) {
        return hwMiddle;
    }
    if (1u == chPhase) {
        return __fire_sim_rgb565_average(hwA, hwMiddle);
    }

    return __fire_sim_rgb565_average(hwMiddle, hwB);
}

__STATIC_FORCEINLINE void __fire_sim_rgb565_expand_pair(uint16_t hwA,
                                                         uint16_t hwB,
                                                         uint16_t hwResult[4])
{
    hwResult[0] = hwA;
    hwResult[2] = __fire_sim_rgb565_average(hwA, hwB);
    hwResult[1] = __fire_sim_rgb565_average(hwA, hwResult[2]);
    hwResult[3] = __fire_sim_rgb565_average(hwResult[2], hwB);
}

__STATIC_FORCEINLINE uint32_t __fire_sim_pack_2px(uint16_t hwLeft,
                                                   uint16_t hwRight)
{
    return (uint32_t)hwLeft | ((uint32_t)hwRight << 16);
}
#endif

#if FIRE_RENDER_USE_GAUSSIAN_SMOOTHING
__STATIC_FORCEINLINE q16_t __fire_sim_smooth_temperature_3x3(
                                                    const q16_t *ptCentre,
                                                    int16_t nStride)
{
    int32_t iSum =
          (int32_t)ptCentre[-nStride - 1]
        + (int32_t)ptCentre[-nStride] * 2
        + (int32_t)ptCentre[-nStride + 1]
        + (int32_t)ptCentre[-1] * 2
        + (int32_t)ptCentre[0] * 4
        + (int32_t)ptCentre[1] * 2
        + (int32_t)ptCentre[nStride - 1]
        + (int32_t)ptCentre[nStride] * 2
        + (int32_t)ptCentre[nStride + 1];

    return (q16_t)((iSum + 8) / 16);
}
#endif

static void __fire_sim_update_rgb565_cache(Fluid *f)
{
    if (NULL == f) {
        return;
    }

    if ((s_ptCachedFluid == f) && (s_nCachedFrameNr == scene.frameNr)) {
        return;
    }

    const int16_t n = f->numY;

    for (int_fast16_t y = 0; y < FIRE_VIEW_H; y++) {
        uint16_t *phwTarget = &s_hwFireRGB565[y * FIRE_VIEW_W];
        const int16_t fluidY = FIRE_VIEW_H - y;
        const q16_t *ptSource = &f->t[n + fluidY];

        for (int_fast16_t x = 0; x < FIRE_VIEW_W; x++) {
#if FIRE_RENDER_USE_GAUSSIAN_SMOOTHING
            q16_t tRender = __fire_sim_smooth_temperature_3x3(ptSource, n);
#else
            q16_t tRender = *ptSource;
#endif
            phwTarget[x] = getFireColor_RGB565_check_list(tRender);
            ptSource += n;
        }
    }

    s_ptCachedFluid = f;
    s_nCachedFrameNr = scene.frameNr;
}

static void __fire_sim_draw_aligned_4x(arm_2d_region_t *ptROI,
                                       uint8_t *pchBuffer,
                                       uint32_t iTargetStrideInByte)
{
    const int_fast16_t xSourceStart = ptROI->tLocation.iX >> 2;
    const int_fast16_t nSourceWidth = ptROI->tSize.iWidth >> 2;
    const int_fast16_t yLimit = ptROI->tLocation.iY + ptROI->tSize.iHeight;

    for (int_fast16_t y = ptROI->tLocation.iY; y < yLimit; y += FIRE_FAST_SCALE) {
#if FIRE_RENDER_USE_BILINEAR_UPSCALE
        const int_fast16_t ySource = y >> 2;
        const uint16_t *phwTop = &s_hwFireRGB565[ySource * FIRE_VIEW_W
                                                + xSourceStart];
        const uint16_t *phwBottom = phwTop;
        if ((ySource + 1) < FIRE_VIEW_H) {
            phwBottom += FIRE_VIEW_W;
        }
#else
        const uint16_t *phwSource = &s_hwFireRGB565[(y >> 2) * FIRE_VIEW_W
                                                  + xSourceStart];
#endif
        uint32_t *pwLine0 = (uint32_t *)pchBuffer;
        uint32_t *pwLine1 = (uint32_t *)(pchBuffer + iTargetStrideInByte);
        uint32_t *pwLine2 = (uint32_t *)(pchBuffer + iTargetStrideInByte * 2u);
        uint32_t *pwLine3 = (uint32_t *)(pchBuffer + iTargetStrideInByte * 3u);

        for (int_fast16_t x = 0; x < nSourceWidth; x++) {
#if FIRE_RENDER_USE_BILINEAR_UPSCALE
            const int_fast16_t xSource = xSourceStart + x;
            uint16_t hwTop[4];
            uint16_t hwBottom[4];
            uint16_t hwLine1[4];
            uint16_t hwLine2[4];
            uint16_t hwLine3[4];
            uint16_t hwTopRight = phwTop[0];
            uint16_t hwBottomRight = phwBottom[0];

            if ((xSource + 1) < FIRE_VIEW_W) {
                hwTopRight = phwTop[1];
                hwBottomRight = phwBottom[1];
            }

            __fire_sim_rgb565_expand_pair(phwTop[0], hwTopRight, hwTop);
            __fire_sim_rgb565_expand_pair(phwBottom[0],
                                          hwBottomRight,
                                          hwBottom);

            for (uint_fast8_t chPhase = 0; chPhase < 4u; chPhase++) {
                hwLine2[chPhase] = __fire_sim_rgb565_average(
                                                        hwTop[chPhase],
                                                        hwBottom[chPhase]);
                hwLine1[chPhase] = __fire_sim_rgb565_average(
                                                        hwTop[chPhase],
                                                        hwLine2[chPhase]);
                hwLine3[chPhase] = __fire_sim_rgb565_average(
                                                        hwLine2[chPhase],
                                                        hwBottom[chPhase]);
            }

            __fire_sim_store_2px(&pwLine0[0],
                __fire_sim_pack_2px(hwTop[0], hwTop[1]));
            __fire_sim_store_2px(&pwLine0[1],
                __fire_sim_pack_2px(hwTop[2], hwTop[3]));
            __fire_sim_store_2px(&pwLine1[0],
                __fire_sim_pack_2px(hwLine1[0], hwLine1[1]));
            __fire_sim_store_2px(&pwLine1[1],
                __fire_sim_pack_2px(hwLine1[2], hwLine1[3]));
            __fire_sim_store_2px(&pwLine2[0],
                __fire_sim_pack_2px(hwLine2[0], hwLine2[1]));
            __fire_sim_store_2px(&pwLine2[1],
                __fire_sim_pack_2px(hwLine2[2], hwLine2[3]));
            __fire_sim_store_2px(&pwLine3[0],
                __fire_sim_pack_2px(hwLine3[0], hwLine3[1]));
            __fire_sim_store_2px(&pwLine3[1],
                __fire_sim_pack_2px(hwLine3[2], hwLine3[3]));

            phwTop++;
            phwBottom++;
#else
            uint32_t wPixels = *phwSource++;
            wPixels |= wPixels << 16;

            __fire_sim_store_2px(&pwLine0[0], wPixels);
            __fire_sim_store_2px(&pwLine0[1], wPixels);
            __fire_sim_store_2px(&pwLine1[0], wPixels);
            __fire_sim_store_2px(&pwLine1[1], wPixels);
            __fire_sim_store_2px(&pwLine2[0], wPixels);
            __fire_sim_store_2px(&pwLine2[1], wPixels);
            __fire_sim_store_2px(&pwLine3[0], wPixels);
            __fire_sim_store_2px(&pwLine3[1], wPixels);
#endif

            pwLine0 += 2;
            pwLine1 += 2;
            pwLine2 += 2;
            pwLine3 += 2;
        }

        pchBuffer += iTargetStrideInByte * FIRE_FAST_SCALE;
    }
}

static void __fire_sim_draw_fast_4x(arm_2d_region_t *ptROI,
                                    uint8_t *pchBuffer,
                                    uint32_t iTargetStrideInByte)
{
    const int_fast16_t xStart = ptROI->tLocation.iX;
    const int_fast16_t xLimit = xStart + ptROI->tSize.iWidth;
    const int_fast16_t yStart = ptROI->tLocation.iY;
    const int_fast16_t yLimit = yStart + ptROI->tSize.iHeight;

    if ((((uintptr_t)pchBuffer | iTargetStrideInByte) & (sizeof(uint32_t) - 1u)) == 0u
     && ((xStart | xLimit | yStart | yLimit) & (FIRE_FAST_SCALE - 1)) == 0) {
        __fire_sim_draw_aligned_4x(ptROI, pchBuffer, iTargetStrideInByte);
        return;
    }

    for (int_fast16_t y = yStart; y < yLimit; y++) {
#if FIRE_RENDER_USE_BILINEAR_UPSCALE
        uint8_t *pchPixelLine = pchBuffer;
        const int_fast16_t ySource = y >> 2;
        const int_fast16_t yNext = ((ySource + 1) < FIRE_VIEW_H)
                                 ? ySource + 1
                                 : ySource;
        const uint_fast8_t chYPhase = (uint_fast8_t)(y & 3);

        for (int_fast16_t x = xStart; x < xLimit; x++) {
            const int_fast16_t xSource = x >> 2;
            const int_fast16_t xNext = ((xSource + 1) < FIRE_VIEW_W)
                                     ? xSource + 1
                                     : xSource;
            const uint_fast8_t chXPhase = (uint_fast8_t)(x & 3);
            const uint16_t hwTop = __fire_sim_rgb565_lerp_quarter(
                s_hwFireRGB565[ySource * FIRE_VIEW_W + xSource],
                s_hwFireRGB565[ySource * FIRE_VIEW_W + xNext],
                chXPhase);
            const uint16_t hwBottom = __fire_sim_rgb565_lerp_quarter(
                s_hwFireRGB565[yNext * FIRE_VIEW_W + xSource],
                s_hwFireRGB565[yNext * FIRE_VIEW_W + xNext],
                chXPhase);
            const uint16_t hwColour = __fire_sim_rgb565_lerp_quarter(
                hwTop,
                hwBottom,
                chYPhase);

            memcpy(pchPixelLine, &hwColour, sizeof(hwColour));
            pchPixelLine += sizeof(hwColour);
        }
#else
        const uint32_t nCopyBytes = (uint32_t)ptROI->tSize.iWidth
                                   * sizeof(uint16_t);
        uint16_t *phwLine = (uint16_t *)pchBuffer;
        if ((y > yStart) && ((y >> 2) == ((y - 1) >> 2))) {
            memcpy(pchBuffer, pchBuffer - iTargetStrideInByte, nCopyBytes);
        } else {
            const uint16_t *phwSource = &s_hwFireRGB565[(y >> 2) * FIRE_VIEW_W];
            int_fast16_t x = xStart;

            while (x < xLimit) {
                const uint16_t hwColour = phwSource[x >> 2];
                int_fast16_t nRun = FIRE_FAST_SCALE - (x & (FIRE_FAST_SCALE - 1));

                if ((x + nRun) > xLimit) {
                    nRun = xLimit - x;
                }

                int_fast16_t nStep = nRun;

                if (FIRE_FAST_SCALE == nRun) {
                    phwLine[0] = hwColour;
                    phwLine[1] = hwColour;
                    phwLine[2] = hwColour;
                    phwLine[3] = hwColour;
                    phwLine += FIRE_FAST_SCALE;
                } else {
                    do {
                        *phwLine++ = hwColour;
                    } while (--nRun);
                }

                x += nStep;
            }
        }
#endif

        pchBuffer += iTargetStrideInByte;
    }
}

ARM_NONNULL(1,2)

void fire_sim_show(fire_sim_t *ptThis,
                        const arm_2d_tile_t *ptTile,
                        const arm_2d_region_t *ptRegion,
                        bool bIsNewFrame)
{
    ARM_2D_UNUSED(bIsNewFrame);

    assert(NULL!= ptThis);
    if (-1 == (intptr_t)ptTile) {
        ptTile = arm_2d_get_default_frame_buffer();
    }	
	
	arm_2d_tile_copy_only(  &this.tTile, 
					   	    ptTile, 
						    ptRegion);
	
}


arm_2d_err_t fire_sim_init(fire_sim_t *ptThis,
                                fire_sim_cfg_t *ptCFG)
{
    assert(NULL != ptThis);
    assert(NULL != ptCFG);
    memset(ptThis, 0, sizeof(fire_sim_t));
    s_ptCachedFluid = NULL;
    s_nCachedFrameNr = -1;

    //if (NULL != ptCFG) {
        this.tCFG = *ptCFG;
    //}

    arm_2d_err_t tResult = ARM_2D_ERR_NONE;

    do {
    #if 0 /* Please make the following code avaiable when the IO is used. */
        if (NULL == this.tCFG.ImageIO.ptIO) {
            this.use_as__arm_generic_loader_t.bErrorDetected = true;
            tResult = ARM_2D_ERR_IO_ERROR;
            break;
        }
    #endif

        arm_generic_loader_cfg_t tCFG = {
            .bUseHeapForVRES = this.tCFG.bUseHeapForVRES,
            .tColourInfo.chScheme = ARM_2D_COLOUR,
            .bBlendWithBG = false,
            .ImageIO = {
                .ptIO = this.tCFG.ImageIO.ptIO,
                .pTarget = this.tCFG.ImageIO.pTarget,
            },

            .UserDecoder = {
                .fnDecoderInit = &__fire_sim_decoder_init,
                .fnDecode = &__fire_sim_draw,
            },

            .ptScene = this.tCFG.ptScene,
        };

        tResult = arm_generic_loader_init(  &this.use_as__arm_generic_loader_t,
                                            &tCFG);

        if (tResult < 0) {
            break;
        }

        this.tTile.tRegion.tSize = this.tCFG.tSize;
        if ((0 == this.tTile.tRegion.tSize.iWidth)
         || (0 == this.tTile.tRegion.tSize.iHeight)) {
            tResult = ARM_2D_ERR_INVALID_PARAM;
            break;
        }

    } while(0);

    return tResult;

}

ARM_NONNULL(1)
void fire_sim_depose( fire_sim_t *ptThis)
{
    assert(NULL != ptThis);

    arm_generic_loader_depose(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void fire_sim_on_load( fire_sim_t *ptThis)
{
    assert(NULL != ptThis);
    
    arm_generic_loader_on_load(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void fire_sim_on_frame_start( fire_sim_t *ptThis)
{
    assert(NULL != ptThis);
    
    arm_generic_loader_on_frame_start(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void fire_sim_on_frame_complete( fire_sim_t *ptThis)
{
    assert(NULL != ptThis);

    arm_generic_loader_on_frame_complete(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
static
arm_2d_err_t __fire_sim_decoder_init(arm_generic_loader_t *ptObj)
{
    assert(NULL != ptObj);

    fire_sim_t *ptThis = (fire_sim_t *)ptObj;
    ARM_2D_UNUSED(ptThis);

    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1, 2, 3)
static
arm_2d_err_t __fire_sim_draw(  arm_generic_loader_t *ptObj,
                                    arm_2d_region_t *ptROI,
                                    uint8_t *pchBuffer,
                                    uint32_t iTargetStrideInByte,
                                    uint_fast8_t chBitsPerPixel)
{
    assert(NULL != ptObj);
    fire_sim_t *ptThis = (fire_sim_t *)ptObj;
    ARM_2D_UNUSED(ptThis);
	
	#define SCREEN_W this.tCFG.tSize.iWidth
	#define SCREEN_H this.tCFG.tSize.iHeight

    if (16 != chBitsPerPixel) {
        return ARM_2D_ERR_NONE;
    }
	
    int_fast16_t iXLimit = ptROI->tSize.iWidth + ptROI->tLocation.iX; 
    int_fast16_t iYLimit = ptROI->tSize.iHeight + ptROI->tLocation.iY; 

    uint_fast8_t chBytesPerPixel = chBitsPerPixel >> 3;
	
	Fluid *f = scene.fluid;
    if (NULL == f) {
        return ARM_2D_ERR_NONE;
    }

    __fire_sim_update_rgb565_cache(f);

    if ((SCREEN_W == (FIRE_VIEW_W * FIRE_FAST_SCALE))
     && (SCREEN_H == (FIRE_VIEW_H * FIRE_FAST_SCALE))
     && (ptROI->tLocation.iX >= 0)
     && (ptROI->tLocation.iY >= 0)
     && (iXLimit <= SCREEN_W)
     && (iYLimit <= SCREEN_H)) {
        __fire_sim_draw_fast_4x(ptROI, pchBuffer, iTargetStrideInByte);
        return ARM_2D_ERR_NONE;
    }

	uint16_t drawW = FIRE_VIEW_W;
	uint16_t drawH = FIRE_VIEW_H;
	const int32_t fxStep = ((int32_t)drawW << 16) / SCREEN_W;
	const int32_t fyStep = ((int32_t)drawH << 16) / SCREEN_H;
	int32_t fyAcc = (int32_t)ptROI->tLocation.iY * fyStep;

	for (int_fast16_t iY = ptROI->tLocation.iY; iY < iYLimit; iY++) {

    uint8_t *pchPixelLine = pchBuffer;
    int fy = fyAcc >> 16;
    if (fy < 0) fy = 0;
    if (fy >= drawH) fy = drawH - 1;
    int32_t fxAcc = (int32_t)ptROI->tLocation.iX * fxStep;

    for (int_fast16_t iX = ptROI->tLocation.iX; iX < iXLimit; iX++) {

        int fx = fxAcc >> 16;

        if (fx < 0) fx = 0;
        if (fx >= drawW) fx = drawW - 1;

        uint16_t fluidIdx = fy * drawW + fx;

        *((uint16_t *)pchPixelLine) =  s_hwFireRGB565[fluidIdx];
        pchPixelLine += chBytesPerPixel;
        fxAcc += fxStep;
    }

    pchBuffer += iTargetStrideInByte;
    fyAcc += fyStep;
}

    return ARM_2D_ERR_NONE;
}




#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#endif
