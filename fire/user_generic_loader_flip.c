/*
 * Copyright (c) 2009-2025 Arm Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

/*============================ INCLUDES ======================================*/
#define __GENERIC_LOADER_INHERIT__
#define __flip_sim_IMPLEMENT__

#include "user_generic_loader_flip.h"
#include "flip_rp2040_q16.h"

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
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#elif __IS_COMPILER_ARM_COMPILER_5__
#   pragma diag_suppress 64,177
#elif __IS_COMPILER_IAR__
#   pragma diag_suppress=Pa089,Pe188,Pe177,Pe174
#endif

/*============================ MACROS ========================================*/

#define FLIP_RENDER_MAX_WIDTH             240
#define FLIP_RENDER_MAX_HEIGHT            240
#define FLIP_CONTAINER_SCALE_PERCENT       82
#define FLIP_CONTAINER_BORDER_COLOUR      0x0u
#define FLIP_SURFACE_THRESHOLD_SHIFT         4u
 
#undef this
#define this    (*ptThis)

/*============================ TYPES =========================================*/

typedef struct flip_density_map_t {
    uint16_t hwIndex;
    uint8_t chFraction;
} flip_density_map_t;

/*============================ PROTOTYPES ====================================*/

ARM_NONNULL(1)
static arm_2d_err_t __flip_sim_decoder_init(arm_generic_loader_t *ptObj);

ARM_NONNULL(1, 2, 3)
static arm_2d_err_t __flip_sim_draw(arm_generic_loader_t *ptObj,
                                    arm_2d_region_t *ptROI,
                                    uint8_t *pchBuffer,
                                    uint32_t iTargetStrideInByte,
                                    uint_fast8_t chBitsPerPixel);

/*============================ LOCAL VARIABLES ===============================*/

static flip_density_map_t s_tFlipMapX[FLIP_RENDER_MAX_WIDTH];
static flip_density_map_t s_tFlipMapY[FLIP_RENDER_MAX_HEIGHT];
static int16_t s_iFlipParticleX[FLIP_MAX_PARTICLES];
static int16_t s_iFlipParticleNext[FLIP_MAX_PARTICLES];
static uint16_t s_hwFlipParticleColour[FLIP_MAX_PARTICLES];
static int16_t s_iFlipRowHead[FLIP_RENDER_MAX_HEIGHT];
static arm_2d_region_t s_tFlipContainer;
static uint32_t s_wFlipScaleXQ16;
static uint32_t s_wFlipScaleYQ16;
static uint32_t s_wCachedParticleFrameNr;
static flip_fluid_t *s_ptCachedParticleFluid;
static int16_t s_iMappedGridWidth;
static int16_t s_iMappedGridHeight;
static int16_t s_iMappedScreenWidth;
static int16_t s_iMappedScreenHeight;
static bool s_bMapValid;

/*============================ IMPLEMENTATION ================================*/

static inline void __flip_store_rgb565(uint8_t *pchTarget,
                                       uint16_t hwColour)
{
    memcpy(pchTarget, &hwColour, sizeof(hwColour));
}

static inline uint32_t __flip_mul_uq16(uint32_t wA, uint32_t wB)
{
#if defined(__ARM_ARCH_6M__)
    uint32_t wALow = wA & UINT16_MAX;
    uint32_t wBLow = wB & UINT16_MAX;
    uint32_t wAHigh = wA >> 16;
    uint32_t wBHigh = wB >> 16;

    return ((wALow * wBLow) >> 16)
         + (wALow * wBHigh)
         + (wAHigh * wBLow)
         + ((wAHigh * wBHigh) << 16);
#else
    return (uint32_t)(((uint64_t)wA * wB) >> 16);
#endif
}

static inline int16_t __flip_clamp_i16(int32_t iValue,
                                       int16_t iMinimum,
                                       int16_t iMaximum)
{
    if (iValue < iMinimum) {
        return iMinimum;
    }
    if (iValue > iMaximum) {
        return iMaximum;
    }
    return (int16_t)iValue;
}

static inline uint16_t __flip_lerp_u16(uint16_t hwA,
                                       uint16_t hwB,
                                       uint8_t chFraction)
{
    return (uint16_t)((((uint32_t)hwA * (256u - chFraction))
                     + ((uint32_t)hwB * chFraction)
                     + 128u) >> 8);
}

static void __flip_build_density_map(flip_density_map_t *ptMap,
                                     int16_t iOutputSize,
                                     int16_t iSourceSize)
{
    uint32_t wSourceSpan = (uint32_t)(iSourceSize - 1);
    uint32_t wOutputSpan = (uint32_t)(iOutputSize - 1);

    for (int16_t i = 0; i < iOutputSize; i++) {
        uint32_t wPositionQ8 = ((uint32_t)i * wSourceSpan << 8)
                            / wOutputSpan;
        ptMap[i].hwIndex = (uint16_t)(wPositionQ8 >> 8);
        ptMap[i].chFraction = (uint8_t)wPositionQ8;
    }
}

static void __flip_prepare_density_maps(const flip_sim_t *ptThis,
                                        const flip_fluid_t *ptFluid)
{
    int32_t iScreenWidth = this.tCFG.tSize.iWidth;
    int32_t iScreenHeight = this.tCFG.tSize.iHeight;
    int32_t iGridWidth = ptFluid->numX - 2;
    int32_t iGridHeight = ptFluid->numY - 2;

    if (s_bMapValid
     && (s_iMappedGridWidth == iGridWidth)
     && (s_iMappedGridHeight == iGridHeight)
     && (s_iMappedScreenWidth == iScreenWidth)
     && (s_iMappedScreenHeight == iScreenHeight)) {
        return;
    }

    int32_t iAvailableWidth =
        (iScreenWidth * FLIP_CONTAINER_SCALE_PERCENT) / 100;
    int32_t iAvailableHeight =
        (iScreenHeight * FLIP_CONTAINER_SCALE_PERCENT) / 100;
    int32_t iContainerWidth;
    int32_t iContainerHeight;

    if ((iGridWidth * iAvailableHeight)
      > (iGridHeight * iAvailableWidth)) {
        iContainerWidth = iAvailableWidth;
        iContainerHeight = (iAvailableWidth * iGridHeight) / iGridWidth;
    } else {
        iContainerHeight = iAvailableHeight;
        iContainerWidth = (iAvailableHeight * iGridWidth) / iGridHeight;
    }

    iContainerWidth = MAX(iContainerWidth, 8);
    iContainerHeight = MAX(iContainerHeight, 8);
    s_tFlipContainer = (arm_2d_region_t) {
        .tLocation = {
            .iX = (int16_t)((iScreenWidth - iContainerWidth) >> 1),
            .iY = (int16_t)((iScreenHeight - iContainerHeight) >> 1),
        },
        .tSize = {
            .iWidth = (int16_t)iContainerWidth,
            .iHeight = (int16_t)iContainerHeight,
        },
    };

    __flip_build_density_map(s_tFlipMapX,
                             (int16_t)(iContainerWidth - 2),
                             (int16_t)iGridWidth);
    __flip_build_density_map(s_tFlipMapY,
                             (int16_t)(iContainerHeight - 2),
                             (int16_t)iGridHeight);
    s_wFlipScaleXQ16 =
        ((uint32_t)(iContainerWidth - 3) << 16) / (uint32_t)iGridWidth;
    s_wFlipScaleYQ16 =
        ((uint32_t)(iContainerHeight - 3) << 16) / (uint32_t)iGridHeight;

    s_iMappedGridWidth = (int16_t)iGridWidth;
    s_iMappedGridHeight = (int16_t)iGridHeight;
    s_iMappedScreenWidth = (int16_t)iScreenWidth;
    s_iMappedScreenHeight = (int16_t)iScreenHeight;
    s_bMapValid = true;
}

static inline uint16_t __flip_density_at(const flip_fluid_t *ptFluid,
                                         uint16_t hwX,
                                         uint16_t hwY)
{
    int16_t iGridX = (int16_t)hwX + 1;
    int16_t iGridY = (ptFluid->numY - 2) - (int16_t)hwY;

    return ptFluid->particleDensity[iGridX * ptFluid->numY + iGridY];
}

static uint16_t __flip_sample_density(const flip_fluid_t *ptFluid,
                                      const flip_density_map_t *ptMapX,
                                      const flip_density_map_t *ptMapY)
{
    uint16_t hwX0 = ptMapX->hwIndex;
    uint16_t hwY0 = ptMapY->hwIndex;
    uint16_t hwXLimit = (uint16_t)(ptFluid->numX - 3);
    uint16_t hwYLimit = (uint16_t)(ptFluid->numY - 3);
    uint16_t hwX1 = (hwX0 < hwXLimit) ? hwX0 + 1u : hwXLimit;
    uint16_t hwY1 = (hwY0 < hwYLimit) ? hwY0 + 1u : hwYLimit;
    uint16_t hwTop = __flip_lerp_u16(
        __flip_density_at(ptFluid, hwX0, hwY0),
        __flip_density_at(ptFluid, hwX1, hwY0),
        ptMapX->chFraction);
    uint16_t hwBottom = __flip_lerp_u16(
        __flip_density_at(ptFluid, hwX0, hwY1),
        __flip_density_at(ptFluid, hwX1, hwY1),
        ptMapX->chFraction);

    return __flip_lerp_u16(hwTop, hwBottom, ptMapY->chFraction);
}

static inline uint16_t __flip_rgb565(uint8_t chRed,
                                     uint8_t chGreen,
                                     uint8_t chBlue)
{
    return (uint16_t)(((uint16_t)(chRed & 0xF8u) << 8)
                    | ((uint16_t)(chGreen & 0xFCu) << 3)
                    | ((uint16_t)chBlue >> 3));
}

static uint16_t __flip_density_colour(const flip_fluid_t *ptFluid,
                                      uint16_t hwDensity)
{
    if (0u == hwDensity) {
        return 0u;
    }

    uint16_t hwRestDensity = ptFluid->particleRestDensity;
    if ((0u != hwRestDensity)
     && (((uint32_t)hwDensity << FLIP_SURFACE_THRESHOLD_SHIFT)
       < hwRestDensity)) {
        return 0u;
    }

    if (0u == hwRestDensity) {
        return __flip_rgb565(150u, 32u, 210u);
    }
    if (((uint32_t)hwDensity * 10u)
      < ((uint32_t)hwRestDensity * 7u)) {
        return __flip_rgb565(88u, 16u, 142u);
    }
    if (hwDensity < hwRestDensity) {
        return __flip_rgb565(142u, 28u, 202u);
    }
    if (((uint32_t)hwDensity * 2u)
      < ((uint32_t)hwRestDensity * 3u)) {
        return __flip_rgb565(198u, 48u, 232u);
    }
    return __flip_rgb565(246u, 92u, 255u);
}

static inline uint32_t __flip_abs_q16(q16_t q16Value)
{
    uint32_t wBits = (uint32_t)q16Value;
    return (q16Value < 0) ? (~wBits + 1u) : wBits;
}

static uint16_t __flip_particle_colour(const flip_fluid_t *ptFluid,
                                       int16_t iParticle,
                                       uint32_t wFrameNr)
{
    uint32_t wVelocityX = __flip_abs_q16(
        ptFluid->particleVel[2 * iParticle]);
    uint32_t wVelocityY = __flip_abs_q16(
        ptFluid->particleVel[2 * iParticle + 1]);
    uint32_t wMaximum = MAX(wVelocityX, wVelocityY);
    uint32_t wMinimum = MIN(wVelocityX, wVelocityY);
    uint32_t wSpeed = wMaximum + (wMinimum >> 1);
    uint32_t wRedSpeed = (uint32_t)FLIP_SIM_PARTICLE_RED_SPEED_Q16;

    wSpeed = MIN(wSpeed, wRedSpeed);
    uint32_t wLevel = (wSpeed * 255u
                     + (wRedSpeed >> 1))
                    / wRedSpeed;
    uint32_t wRed;
    uint32_t wGreen;
    uint32_t wBlue;

    if (wLevel < 128u) {
        uint32_t wBlend = wLevel << 1;
        wRed = 52u + (wBlend * 98u) / 255u;
        wGreen = 10u + (wBlend * 20u) / 255u;
        wBlue = 92u + (wBlend * 118u) / 255u;
    } else {
        uint32_t wBlend = (wLevel - 128u) << 1;
        wRed = 150u + (wBlend * 96u) / 254u;
        wGreen = 30u + (wBlend * 64u) / 254u;
        wBlue = 210u + (wBlend * 45u) / 254u;
    }

    uint32_t wPhase = (wFrameNr * 3u
                     + (uint32_t)iParticle * 47u) & 0xFFu;
    uint32_t wPulse = (wPhase < 128u) ? wPhase : 255u - wPhase;
    uint32_t wGlow = wPulse >> 3;

    wRed = MIN(wRed + wGlow, 255u);
    wGreen = MIN(wGreen + (wGlow >> 1), 255u);
    wBlue = MIN(wBlue + wGlow, 255u);

    return __flip_rgb565((uint8_t)wRed,
                         (uint8_t)wGreen,
                         (uint8_t)wBlue);
}

static void __flip_update_particle_cache(flip_fluid_t *ptFluid)
{
    if ((s_ptCachedParticleFluid == ptFluid)
     && (s_wCachedParticleFrameNr == flip_scene.frameNr)) {
        return;
    }

    int16_t iScreenHeight = s_iMappedScreenHeight;
    for (int16_t iY = 0; iY < iScreenHeight; iY++) {
        s_iFlipRowHead[iY] = -1;
    }

    int16_t iInnerLeft = s_tFlipContainer.tLocation.iX + 1;
    int16_t iInnerBottom = s_tFlipContainer.tLocation.iY
                         + s_tFlipContainer.tSize.iHeight - 2;
    int16_t iParticleCount = MIN(ptFluid->numParticles,
                                 FLIP_MAX_PARTICLES);

    for (int16_t iParticle = 0;
         iParticle < iParticleCount;
         iParticle++) {
        q16_t q16RelativeX = ptFluid->particlePos[2 * iParticle]
                           - ptFluid->h;
        q16_t q16RelativeY = ptFluid->particlePos[2 * iParticle + 1]
                           - ptFluid->h;
        uint32_t wCellXQ16 = (q16RelativeX > 0)
            ? __flip_mul_uq16((uint32_t)q16RelativeX,
                              (uint32_t)ptFluid->hInv)
            : 0u;
        uint32_t wCellYQ16 = (q16RelativeY > 0)
            ? __flip_mul_uq16((uint32_t)q16RelativeY,
                              (uint32_t)ptFluid->hInv)
            : 0u;
        uint32_t wPixelXQ16 = __flip_mul_uq16(wCellXQ16,
                                              s_wFlipScaleXQ16);
        uint32_t wPixelYQ16 = __flip_mul_uq16(wCellYQ16,
                                              s_wFlipScaleYQ16);
        int16_t iX = (int16_t)(iInnerLeft + (wPixelXQ16 >> 16));
        int16_t iY = (int16_t)(iInnerBottom - (wPixelYQ16 >> 16));

        iX = __flip_clamp_i16(iX,
                              s_tFlipContainer.tLocation.iX + 1,
                              s_tFlipContainer.tLocation.iX
                            + s_tFlipContainer.tSize.iWidth - 2);
        iY = __flip_clamp_i16(iY,
                              s_tFlipContainer.tLocation.iY + 1,
                              s_tFlipContainer.tLocation.iY
                            + s_tFlipContainer.tSize.iHeight - 2);

        s_iFlipParticleX[iParticle] = iX;
        s_hwFlipParticleColour[iParticle] =
            __flip_particle_colour(ptFluid,
                                   iParticle,
                                   flip_scene.frameNr);
        s_iFlipParticleNext[iParticle] = s_iFlipRowHead[iY];
        s_iFlipRowHead[iY] = iParticle;
    }

    s_ptCachedParticleFluid = ptFluid;
    s_wCachedParticleFrameNr = flip_scene.frameNr;
}

static void __flip_draw_container_border(int16_t iY,
                                         const arm_2d_region_t *ptROI,
                                         uint8_t *pchBuffer)
{
    int16_t iLeft = s_tFlipContainer.tLocation.iX;
    int16_t iTop = s_tFlipContainer.tLocation.iY;
    int16_t iRight = iLeft + s_tFlipContainer.tSize.iWidth - 1;
    int16_t iBottom = iTop + s_tFlipContainer.tSize.iHeight - 1;
    int16_t iROILeft = ptROI->tLocation.iX;
    int16_t iROIRight = iROILeft + ptROI->tSize.iWidth - 1;

    if ((iY < iTop) || (iY > iBottom)) {
        return;
    }

    if ((iY == iTop) || (iY == iBottom)) {
        int16_t iStart = MAX(iLeft, iROILeft);
        int16_t iEnd = MIN(iRight, iROIRight);
        for (int16_t iX = iStart; iX <= iEnd; iX++) {
            __flip_store_rgb565(
                pchBuffer + (uint32_t)(iX - iROILeft) * sizeof(uint16_t),
                FLIP_CONTAINER_BORDER_COLOUR);
        }
        return;
    }

    if ((iLeft >= iROILeft) && (iLeft <= iROIRight)) {
        __flip_store_rgb565(
            pchBuffer + (uint32_t)(iLeft - iROILeft) * sizeof(uint16_t),
            FLIP_CONTAINER_BORDER_COLOUR);
    }
    if ((iRight >= iROILeft) && (iRight <= iROIRight)) {
        __flip_store_rgb565(
            pchBuffer + (uint32_t)(iRight - iROILeft) * sizeof(uint16_t),
            FLIP_CONTAINER_BORDER_COLOUR);
    }
}

ARM_NONNULL(1, 2)
void flip_sim_show(flip_sim_t *ptThis,
                   const arm_2d_tile_t *ptTile,
                   const arm_2d_region_t *ptRegion,
                   bool bIsNewFrame)
{
    ARM_2D_UNUSED(bIsNewFrame);
    assert(NULL != ptThis);

    if (-1 == (intptr_t)ptTile) {
        ptTile = arm_2d_get_default_frame_buffer();
    }
    arm_2d_tile_copy_only(&this.tTile, ptTile, ptRegion);
}

ARM_NONNULL(1, 2)
arm_2d_err_t flip_sim_init(flip_sim_t *ptThis, flip_sim_cfg_t *ptCFG)
{
    assert(NULL != ptThis);
    assert(NULL != ptCFG);

    if ((ptCFG->tSize.iWidth <= 0)
     || (ptCFG->tSize.iHeight <= 0)
     || (ptCFG->tSize.iWidth > FLIP_RENDER_MAX_WIDTH)
     || (ptCFG->tSize.iHeight > FLIP_RENDER_MAX_HEIGHT)) {
        return ARM_2D_ERR_INVALID_PARAM;
    }

    memset(ptThis, 0, sizeof(flip_sim_t));
    s_bMapValid = false;
    s_ptCachedParticleFluid = NULL;
    s_wCachedParticleFrameNr = 0;
    this.tCFG = *ptCFG;
    this.tRenderMode = FLIP_SIM_DEFAULT_RENDER_MODE;

    arm_generic_loader_cfg_t tCFG = {
        .bUseHeapForVRES = this.tCFG.bUseHeapForVRES,
        .tColourInfo.chScheme = ARM_2D_COLOUR_RGB565,
        .bBlendWithBG = false,
        .ImageIO = {
            .ptIO = this.tCFG.ImageIO.ptIO,
            .pTarget = this.tCFG.ImageIO.pTarget,
        },
        .UserDecoder = {
            .fnDecoderInit = &__flip_sim_decoder_init,
            .fnDecode = &__flip_sim_draw,
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
    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1)
void flip_sim_depose(flip_sim_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_depose(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void flip_sim_on_load(flip_sim_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_on_load(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void flip_sim_on_frame_start(flip_sim_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_on_frame_start(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void flip_sim_on_frame_complete(flip_sim_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_on_frame_complete(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void flip_sim_set_render_mode(flip_sim_t *ptThis,
                              flip_sim_render_mode_t tMode)
{
    assert(NULL != ptThis);
    if ((FLIP_SIM_RENDER_PARTICLE != tMode)
     && (FLIP_SIM_RENDER_SURFACE != tMode)) {
        return;
    }

    this.tRenderMode = tMode;
}

ARM_NONNULL(1)
flip_sim_render_mode_t flip_sim_get_render_mode(const flip_sim_t *ptThis)
{
    assert(NULL != ptThis);
    return this.tRenderMode;
}

ARM_NONNULL(1)
void flip_sim_toggle_render_mode(flip_sim_t *ptThis)
{
    assert(NULL != ptThis);
    this.tRenderMode = (FLIP_SIM_RENDER_SURFACE == this.tRenderMode)
                     ? FLIP_SIM_RENDER_PARTICLE
                     : FLIP_SIM_RENDER_SURFACE;
}

ARM_NONNULL(1)
static arm_2d_err_t __flip_sim_decoder_init(arm_generic_loader_t *ptObj)
{
    assert(NULL != ptObj);
    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1, 2, 3)
static arm_2d_err_t __flip_sim_draw(arm_generic_loader_t *ptObj,
                                    arm_2d_region_t *ptROI,
                                    uint8_t *pchBuffer,
                                    uint32_t iTargetStrideInByte,
                                    uint_fast8_t chBitsPerPixel)
{
    assert(NULL != ptObj);
    flip_sim_t *ptThis = (flip_sim_t *)ptObj;

    if (16u != chBitsPerPixel) {
        return ARM_2D_ERR_INVALID_PARAM;
    }

    flip_fluid_t *ptFluid = flip_scene.fluid;
    if (NULL != ptFluid) {
        __flip_prepare_density_maps(ptThis, ptFluid);
        if (FLIP_SIM_RENDER_PARTICLE == this.tRenderMode) {
            __flip_update_particle_cache(ptFluid);
        }
    }

    int16_t iXStart = ptROI->tLocation.iX;
    int16_t iXLimit = iXStart + ptROI->tSize.iWidth;
    int16_t iYStart = ptROI->tLocation.iY;
    int16_t iYLimit = iYStart + ptROI->tSize.iHeight;
    uint32_t wLineBytes =
        (uint32_t)ptROI->tSize.iWidth * sizeof(uint16_t);

    for (int16_t iY = iYStart; iY < iYLimit; iY++) {
        memset(pchBuffer, 0, wLineBytes);

        if (NULL != ptFluid) {
            __flip_draw_container_border(iY, ptROI, pchBuffer);

            int16_t iInnerLeft = s_tFlipContainer.tLocation.iX + 1;
            int16_t iInnerTop = s_tFlipContainer.tLocation.iY + 1;
            int16_t iInnerRight = s_tFlipContainer.tLocation.iX
                                + s_tFlipContainer.tSize.iWidth - 2;
            int16_t iInnerBottom = s_tFlipContainer.tLocation.iY
                                 + s_tFlipContainer.tSize.iHeight - 2;

            if ((FLIP_SIM_RENDER_SURFACE == this.tRenderMode)
             && (iY >= iInnerTop) && (iY <= iInnerBottom)) {
                int16_t iDrawStart = MAX(iXStart, iInnerLeft);
                int16_t iDrawLimit = MIN(iXLimit, iInnerRight + 1);
                const flip_density_map_t *ptMapY =
                    &s_tFlipMapY[iY - iInnerTop];

                for (int16_t iX = iDrawStart; iX < iDrawLimit; iX++) {
                    uint16_t hwDensity = __flip_sample_density(
                        ptFluid,
                        &s_tFlipMapX[iX - iInnerLeft],
                        ptMapY);
                    uint16_t hwColour = __flip_density_colour(ptFluid,
                                                              hwDensity);
                    __flip_store_rgb565(
                        pchBuffer
                      + (uint32_t)(iX - iXStart) * sizeof(uint16_t),
                        hwColour);
                }
            } else if (FLIP_SIM_RENDER_PARTICLE == this.tRenderMode) {
                for (int16_t iParticle = s_iFlipRowHead[iY];
                     iParticle >= 0;
                     iParticle = s_iFlipParticleNext[iParticle]) {
                    int16_t iX = s_iFlipParticleX[iParticle];
                    if ((iX >= iXStart) && (iX < iXLimit)) {
                        __flip_store_rgb565(
                            pchBuffer
                          + (uint32_t)(iX - iXStart) * sizeof(uint16_t),
                            s_hwFlipParticleColour[iParticle]);
                    }
                }
            }
        }

        pchBuffer += iTargetStrideInByte;
    }

    return ARM_2D_ERR_NONE;
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#endif
