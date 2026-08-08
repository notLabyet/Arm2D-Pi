/*
 * Copyright (c) 2009-2025 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*============================ INCLUDES ======================================*/
#define __GENERIC_LOADER_INHERIT__
#define __USER_GENERIC_LOADER_ARC_IMPLEMENT__
#include "user_generic_loader_arc.h"

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
#endif

#if __GLCD_CFG_COLOUR_DEPTH__ != 16
#   error user_generic_loader_arc currently requires an RGB565 target
#endif

#undef this
#define this    (*ptThis)

/*============================ PROTOTYPES ====================================*/
ARM_NONNULL(1)
static
arm_2d_err_t __user_generic_loader_arc_decoder_init(
                                                arm_generic_loader_t *ptObj);

ARM_NONNULL(1, 2, 3)
static
arm_2d_err_t __user_generic_loader_arc_draw(
                                                arm_generic_loader_t *ptObj,
                                                arm_2d_region_t *ptROI,
                                                uint8_t *pchBuffer,
                                                uint32_t iTargetStrideInByte,
                                                uint_fast8_t chBitsPerPixel);

/*============================ IMPLEMENTATION ================================*/
static uint32_t __user_generic_loader_arc_sqrt_u32(uint32_t wValue)
{
    uint32_t wResult = 0;
    uint32_t wBit = 1uL << 30;

    while (wBit > wValue) {
        wBit >>= 2;
    }

    while (0 != wBit) {
        if (wValue >= wResult + wBit) {
            wValue -= wResult + wBit;
            wResult = (wResult >> 1) + wBit;
        } else {
            wResult >>= 1;
        }
        wBit >>= 2;
    }

    return wResult;
}

static uint32_t __user_generic_loader_arc_sqrt_u64(uint64_t qwValue)
{
    uint64_t qwResult = 0;
    uint64_t qwBit = (uint64_t)1u << 62;

    while (qwBit > qwValue) {
        qwBit >>= 2;
    }

    while (0 != qwBit) {
        if (qwValue >= qwResult + qwBit) {
            qwValue -= qwResult + qwBit;
            qwResult = (qwResult >> 1) + qwBit;
        } else {
            qwResult >>= 1;
        }
        qwBit >>= 2;
    }

    return (uint32_t)qwResult;
}

static uint8_t __user_generic_loader_arc_half_plane_opacity(int32_t iCrossQ14)
{
    if (iCrossQ14 >= 0) {
        return 0xFFu;
    }
    if (iCrossQ14 <= -16384) {
        return 0u;
    }

    return (uint8_t)(((uint32_t)(iCrossQ14 + 16384) * 255u) >> 14);
}

static uint8_t __user_generic_loader_arc_direction_opacity(
                                    const user_generic_loader_arc_t *ptThis,
                                    int32_t iX,
                                    int32_t iY)
{
    if (this.Runtime.bFullCircle) {
        return 0xFFu;
    }

    int32_t iStartCross =
        (int32_t)this.Runtime.iStartXQ14 * iY
      - (int32_t)this.Runtime.iStartYQ14 * iX;
    int32_t iEndCross =
        iX * (int32_t)this.Runtime.iEndYQ14
      - iY * (int32_t)this.Runtime.iEndXQ14;
    uint8_t chStartOpacity =
        __user_generic_loader_arc_half_plane_opacity(iStartCross);
    uint8_t chEndOpacity =
        __user_generic_loader_arc_half_plane_opacity(iEndCross);

    if (this.Runtime.bWideArc) {
        return MAX(chStartOpacity, chEndOpacity);
    }

    return MIN(chStartOpacity, chEndOpacity);
}

static uint8_t __user_generic_loader_arc_radius_opacity(
                                    const user_generic_loader_arc_t *ptThis,
                                    uint32_t wDistanceSquared)
{
    if (wDistanceSquared >= this.Runtime.wOuterBorderRadiusSquared) {
        return 0u;
    }

    uint8_t chOpacity = 0xFFu;
    if (wDistanceSquared > this.Runtime.wOuterRadiusSquared) {
        uint32_t wBorderWidth = this.Runtime.wOuterBorderRadiusSquared
                              - this.Runtime.wOuterRadiusSquared;
        uint32_t wDelta = wDistanceSquared
                        - this.Runtime.wOuterRadiusSquared;
        chOpacity = (uint8_t)(255u - (wDelta * 255u) / wBorderWidth);
    }

    if (0u == this.Runtime.wInnerRadiusSquared) {
        return chOpacity;
    }
    if (wDistanceSquared <= this.Runtime.wInnerBorderRadiusSquared) {
        return 0u;
    }
    if (wDistanceSquared < this.Runtime.wInnerRadiusSquared) {
        uint32_t wBorderWidth = this.Runtime.wInnerRadiusSquared
                              - this.Runtime.wInnerBorderRadiusSquared;
        uint32_t wDelta = wDistanceSquared
                        - this.Runtime.wInnerBorderRadiusSquared;
        uint8_t chInnerOpacity =
            (uint8_t)((wDelta * 255u) / wBorderWidth);
        chOpacity = MIN(chOpacity, chInnerOpacity);
    }

    return chOpacity;
}

static void __user_generic_loader_arc_update_bounds(
                                    user_generic_loader_arc_t *ptThis)
{
    int32_t iLeft = (int32_t)this.tCFG.tArc.tCenter.iX
                  - this.tCFG.tArc.hwRadius - 1;
    int32_t iTop = (int32_t)this.tCFG.tArc.tCenter.iY
                 - this.tCFG.tArc.hwRadius - 1;
    int32_t iRight = (int32_t)this.tCFG.tArc.tCenter.iX
                   + this.tCFG.tArc.hwRadius + 1;
    int32_t iBottom = (int32_t)this.tCFG.tArc.tCenter.iY
                    + this.tCFG.tArc.hwRadius + 1;

    if (iLeft < 0) {
        iLeft = 0;
    }
    if (iTop < 0) {
        iTop = 0;
    }
    if (iRight >= this.tCFG.tSize.iWidth) {
        iRight = this.tCFG.tSize.iWidth - 1;
    }
    if (iBottom >= this.tCFG.tSize.iHeight) {
        iBottom = this.tCFG.tSize.iHeight - 1;
    }

    if ((iLeft > iRight) || (iTop > iBottom)) {
        this.Runtime.bVisible = false;
        memset(&this.Runtime.tBounds, 0, sizeof(this.Runtime.tBounds));
        return;
    }

    this.Runtime.tBounds.tLocation.iX = (int16_t)iLeft;
    this.Runtime.tBounds.tLocation.iY = (int16_t)iTop;
    this.Runtime.tBounds.tSize.iWidth = (int16_t)(iRight - iLeft + 1);
    this.Runtime.tBounds.tSize.iHeight = (int16_t)(iBottom - iTop + 1);
}

static void __user_generic_loader_arc_include_region(
                                    arm_2d_region_t *ptTarget,
                                    bool *pbInitialized,
                                    const arm_2d_region_t *ptRegion)
{
    if ((ptRegion->tSize.iWidth <= 0) || (ptRegion->tSize.iHeight <= 0)) {
        return;
    }

    if (!*pbInitialized) {
        *ptTarget = *ptRegion;
        *pbInitialized = true;
        return;
    }

    arm_2d_region_get_minimal_enclosure(ptTarget, ptRegion, ptTarget);
}

static void __user_generic_loader_arc_include_direction(
                                    const user_generic_loader_arc_t *ptThis,
                                    arm_2d_region_t *ptTarget,
                                    bool *pbInitialized,
                                    int16_t iXQ14,
                                    int16_t iYQ14)
{
    int32_t iOuterRadius = (int32_t)this.tCFG.tArc.hwRadius + 1;
    int32_t iInnerRadius = (int32_t)this.tCFG.tArc.hwRadius
                         - this.tCFG.tArc.hwRingWidth - 1;
    if (iInnerRadius < 0) {
        iInnerRadius = 0;
    }

    int32_t iOuterX = this.tCFG.tArc.tCenter.iX
                    + ((int32_t)iXQ14 * iOuterRadius) / 16384;
    int32_t iOuterY = this.tCFG.tArc.tCenter.iY
                    + ((int32_t)iYQ14 * iOuterRadius) / 16384;
    int32_t iInnerX = this.tCFG.tArc.tCenter.iX
                    + ((int32_t)iXQ14 * iInnerRadius) / 16384;
    int32_t iInnerY = this.tCFG.tArc.tCenter.iY
                    + ((int32_t)iYQ14 * iInnerRadius) / 16384;
    int32_t iLeft = MIN(iOuterX, iInnerX) - 2;
    int32_t iTop = MIN(iOuterY, iInnerY) - 2;
    int32_t iRight = MAX(iOuterX, iInnerX) + 2;
    int32_t iBottom = MAX(iOuterY, iInnerY) + 2;

    iLeft = MAX(iLeft, 0);
    iTop = MAX(iTop, 0);
    iRight = MIN(iRight, this.tCFG.tSize.iWidth - 1);
    iBottom = MIN(iBottom, this.tCFG.tSize.iHeight - 1);
    if ((iLeft > iRight) || (iTop > iBottom)) {
        return;
    }

    arm_2d_region_t tRegion = {
        .tLocation = {
            .iX = (int16_t)iLeft,
            .iY = (int16_t)iTop,
        },
        .tSize = {
            .iWidth = (int16_t)(iRight - iLeft + 1),
            .iHeight = (int16_t)(iBottom - iTop + 1),
        },
    };
    __user_generic_loader_arc_include_region(ptTarget,
                                             pbInitialized,
                                             &tRegion);
}

static bool __user_generic_loader_arc_direction_is_inside(
                                    int16_t iStartXQ14,
                                    int16_t iStartYQ14,
                                    int16_t iEndXQ14,
                                    int16_t iEndYQ14,
                                    bool bWideArc,
                                    int16_t iXQ14,
                                    int16_t iYQ14)
{
    int32_t iStartCross = (int32_t)iStartXQ14 * iYQ14
                        - (int32_t)iStartYQ14 * iXQ14;
    int32_t iEndCross = (int32_t)iXQ14 * iEndYQ14
                      - (int32_t)iYQ14 * iEndXQ14;

    if (bWideArc) {
        return (iStartCross >= 0) || (iEndCross >= 0);
    }
    return (iStartCross >= 0) && (iEndCross >= 0);
}

static bool __user_generic_loader_arc_same_shape(
                            const user_generic_loader_arc_param_t *ptOldArc,
                            const user_generic_loader_arc_param_t *ptNewArc)
{
    return (ptOldArc->tCenter.iX == ptNewArc->tCenter.iX)
        && (ptOldArc->tCenter.iY == ptNewArc->tCenter.iY)
        && (ptOldArc->tStartPoint.iX == ptNewArc->tStartPoint.iX)
        && (ptOldArc->tStartPoint.iY == ptNewArc->tStartPoint.iY)
        && (ptOldArc->hwRadius == ptNewArc->hwRadius)
        && (ptOldArc->hwRingWidth == ptNewArc->hwRingWidth)
        && (ptOldArc->hwColour == ptNewArc->hwColour);
}

static void __user_generic_loader_arc_clip_dirty_region(
                                    user_generic_loader_arc_t *ptThis)
{
    arm_2d_region_t *ptRegion = &this.Runtime.tDirtyRegion;
    if ((ptRegion->tSize.iWidth <= 0) || (ptRegion->tSize.iHeight <= 0)) {
        memset(ptRegion, 0, sizeof(*ptRegion));
        return;
    }

    int32_t iLeft = ptRegion->tLocation.iX;
    int32_t iTop = ptRegion->tLocation.iY;
    int32_t iRight = iLeft + ptRegion->tSize.iWidth;
    int32_t iBottom = iTop + ptRegion->tSize.iHeight;

    iLeft = MAX(iLeft, 0);
    iTop = MAX(iTop, 0);
    iRight = MIN(iRight, this.tCFG.tSize.iWidth);
    iBottom = MIN(iBottom, this.tCFG.tSize.iHeight);

    if ((iLeft >= iRight) || (iTop >= iBottom)) {
        memset(ptRegion, 0, sizeof(*ptRegion));
        return;
    }

    ptRegion->tLocation.iX = (int16_t)iLeft;
    ptRegion->tLocation.iY = (int16_t)iTop;
    ptRegion->tSize.iWidth = (int16_t)(iRight - iLeft);
    ptRegion->tSize.iHeight = (int16_t)(iBottom - iTop);
}

static void __user_generic_loader_arc_update_dirty_region(
                            user_generic_loader_arc_t *ptThis,
                            const user_generic_loader_arc_param_t *ptOldArc,
                            const arm_2d_region_t *ptOldBounds,
                            int16_t iOldTipXQ14,
                            int16_t iOldTipYQ14,
                            bool bHadOldParameters)
{
    bool bInitialized = false;
    int32_t iAngleDelta = bHadOldParameters
                        ? (int32_t)this.tCFG.tArc.iSweepAngle
                            - ptOldArc->iSweepAngle
                        : 0;
    uint32_t wAngleMagnitude = (uint32_t)((iAngleDelta < 0)
                                       ? -iAngleDelta
                                       : iAngleDelta);
    uint16_t hwOldSweepMagnitude = bHadOldParameters
        ? (uint16_t)((ptOldArc->iSweepAngle < 0)
                   ? -ptOldArc->iSweepAngle
                   : ptOldArc->iSweepAngle)
        : 0u;
    uint16_t hwNewSweepMagnitude = (uint16_t)(
        (this.tCFG.tArc.iSweepAngle < 0)
        ? -this.tCFG.tArc.iSweepAngle
        : this.tCFG.tArc.iSweepAngle);

    memset(&this.Runtime.tDirtyRegion, 0,
           sizeof(this.Runtime.tDirtyRegion));

    bool bCanTrackTip = bHadOldParameters
                     && !this.Runtime.bForceFullDirtyRegion
                     && __user_generic_loader_arc_same_shape(
                                                    ptOldArc,
                                                    &this.tCFG.tArc)
                     && (hwOldSweepMagnitude > 0u)
                     && (hwOldSweepMagnitude < 360u)
                     && (hwNewSweepMagnitude > 0u)
                     && (hwNewSweepMagnitude < 360u)
                     && (wAngleMagnitude < 360u);

    if (bCanTrackTip) {
        if (0u == wAngleMagnitude) {
            return;
        }

        int16_t iStartXQ14 = iOldTipXQ14;
        int16_t iStartYQ14 = iOldTipYQ14;
        int16_t iEndXQ14 = this.Runtime.iTipXQ14;
        int16_t iEndYQ14 = this.Runtime.iTipYQ14;
        if (iAngleDelta < 0) {
            iStartXQ14 = this.Runtime.iTipXQ14;
            iStartYQ14 = this.Runtime.iTipYQ14;
            iEndXQ14 = iOldTipXQ14;
            iEndYQ14 = iOldTipYQ14;
        }

        __user_generic_loader_arc_include_direction(
            ptThis, &this.Runtime.tDirtyRegion, &bInitialized,
            iOldTipXQ14, iOldTipYQ14);
        __user_generic_loader_arc_include_direction(
            ptThis, &this.Runtime.tDirtyRegion, &bInitialized,
            this.Runtime.iTipXQ14, this.Runtime.iTipYQ14);

        static const int16_t c_iCardinalDirectionQ14[4][2] = {
            { 16384,     0},
            {     0, 16384},
            {-16384,     0},
            {     0,-16384},
        };
        bool bWideTransition = (wAngleMagnitude > 180u);
        for (uint_fast8_t n = 0; n < dimof(c_iCardinalDirectionQ14); n++) {
            int16_t iXQ14 = c_iCardinalDirectionQ14[n][0];
            int16_t iYQ14 = c_iCardinalDirectionQ14[n][1];
            if (__user_generic_loader_arc_direction_is_inside(
                    iStartXQ14, iStartYQ14,
                    iEndXQ14, iEndYQ14,
                    bWideTransition,
                    iXQ14, iYQ14)) {
                __user_generic_loader_arc_include_direction(
                    ptThis, &this.Runtime.tDirtyRegion, &bInitialized,
                    iXQ14, iYQ14);
            }
        }
    } else {
        if (bHadOldParameters) {
            __user_generic_loader_arc_include_region(
                &this.Runtime.tDirtyRegion, &bInitialized, ptOldBounds);
        }
        __user_generic_loader_arc_include_region(
            &this.Runtime.tDirtyRegion, &bInitialized,
            &this.Runtime.tBounds);
    }

    this.Runtime.bForceFullDirtyRegion = false;
    __user_generic_loader_arc_clip_dirty_region(ptThis);
}

ARM_NONNULL(1, 2)
arm_2d_err_t user_generic_loader_arc_set(
                                    user_generic_loader_arc_t *ptThis,
                                    const user_generic_loader_arc_param_t *ptArc)
{
    assert(NULL != ptThis);
    assert(NULL != ptArc);

    if ((0 == ptArc->hwRadius)
     || (ptArc->hwRadius > INT16_MAX)
     || (0 == ptArc->hwRingWidth)
     || (ptArc->hwRingWidth > ptArc->hwRadius)
     || (ptArc->iSweepAngle < -360)
     || (ptArc->iSweepAngle > 360)) {
        return ARM_2D_ERR_INVALID_PARAM;
    }

    user_generic_loader_arc_param_t tOldArc = this.tCFG.tArc;
    arm_2d_region_t tOldBounds = this.Runtime.tBounds;
    int16_t iOldTipXQ14 = this.Runtime.iTipXQ14;
    int16_t iOldTipYQ14 = this.Runtime.iTipYQ14;
    bool bHadOldParameters = this.Runtime.bParametersValid;

    int32_t iStartX = (int32_t)ptArc->tStartPoint.iX
                    - ptArc->tCenter.iX;
    int32_t iStartY = (int32_t)ptArc->tStartPoint.iY
                    - ptArc->tCenter.iY;
    uint64_t qwStartLengthSquared =
        (uint64_t)((int64_t)iStartX * iStartX)
      + (uint64_t)((int64_t)iStartY * iStartY);
    uint32_t wStartLength =
        __user_generic_loader_arc_sqrt_u64(qwStartLengthSquared);

    uint16_t hwSweepMagnitude = (uint16_t)(
        (ptArc->iSweepAngle < 0)
        ? -ptArc->iSweepAngle
        : ptArc->iSweepAngle);
    bool bFullCircle = (360u == hwSweepMagnitude);

    if (0 == wStartLength) {
        if (!bFullCircle && (0 != hwSweepMagnitude)) {
            return ARM_2D_ERR_INVALID_PARAM;
        }
        iStartX = 1;
        iStartY = 0;
        wStartLength = 1;
    }

    this.tCFG.tArc = *ptArc;
    this.Runtime.iStartXQ14 = (int16_t)(
        ((int64_t)iStartX * 16384LL) / wStartLength);
    this.Runtime.iStartYQ14 = (int16_t)(
        ((int64_t)iStartY * 16384LL) / wStartLength);
    this.Runtime.iEndXQ14 = this.Runtime.iStartXQ14;
    this.Runtime.iEndYQ14 = this.Runtime.iStartYQ14;
    this.Runtime.iTipXQ14 = this.Runtime.iStartXQ14;
    this.Runtime.iTipYQ14 = this.Runtime.iStartYQ14;
    this.Runtime.bVisible = (0 != hwSweepMagnitude);
    this.Runtime.bFullCircle = bFullCircle;
    this.Runtime.bWideArc = (hwSweepMagnitude > 180u);

    if (!bFullCircle && (0 != hwSweepMagnitude)) {
        q31_t q31SweepAngle = (q31_t)(
            ((int64_t)ptArc->iSweepAngle * 2147483648LL) / 360LL);
        int32_t iCosQ15 = arm_cos_q31(q31SweepAngle) >> 16;
        int32_t iSinQ15 = arm_sin_q31(q31SweepAngle) >> 16;
        int16_t iEndXQ14 = (int16_t)(
            (iCosQ15 * this.Runtime.iStartXQ14
           - iSinQ15 * this.Runtime.iStartYQ14) >> 15);
        int16_t iEndYQ14 = (int16_t)(
            (iSinQ15 * this.Runtime.iStartXQ14
           + iCosQ15 * this.Runtime.iStartYQ14) >> 15);
        this.Runtime.iTipXQ14 = iEndXQ14;
        this.Runtime.iTipYQ14 = iEndYQ14;

        if (ptArc->iSweepAngle < 0) {
            this.Runtime.iEndXQ14 = this.Runtime.iStartXQ14;
            this.Runtime.iEndYQ14 = this.Runtime.iStartYQ14;
            this.Runtime.iStartXQ14 = iEndXQ14;
            this.Runtime.iStartYQ14 = iEndYQ14;
        } else {
            this.Runtime.iEndXQ14 = iEndXQ14;
            this.Runtime.iEndYQ14 = iEndYQ14;
        }
    }

    uint32_t wInnerRadius = ptArc->hwRadius - ptArc->hwRingWidth;
    uint32_t wOuterBorderRadius = (uint32_t)ptArc->hwRadius + 1u;
    uint32_t wInnerBorderRadius = (wInnerRadius > 0u)
                                ? wInnerRadius - 1u
                                : 0u;
    this.Runtime.wOuterRadiusSquared =
        (uint32_t)ptArc->hwRadius * ptArc->hwRadius;
    this.Runtime.wOuterBorderRadiusSquared =
        wOuterBorderRadius * wOuterBorderRadius;
    this.Runtime.wInnerRadiusSquared = wInnerRadius * wInnerRadius;
    this.Runtime.wInnerBorderRadiusSquared =
        wInnerBorderRadius * wInnerBorderRadius;
    __user_generic_loader_arc_update_bounds(ptThis);
    this.Runtime.bParametersValid = true;
    __user_generic_loader_arc_update_dirty_region(
        ptThis,
        &tOldArc,
        &tOldBounds,
        iOldTipXQ14,
        iOldTipYQ14,
        bHadOldParameters);

    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1, 2)
arm_2d_err_t user_generic_loader_arc_get_dirty_region(
                                    const user_generic_loader_arc_t *ptThis,
                                    arm_2d_region_t *ptRegion)
{
    assert(NULL != ptThis);
    assert(NULL != ptRegion);

    *ptRegion = this.Runtime.tDirtyRegion;
    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1, 2)
arm_2d_err_t user_generic_loader_arc_init(
                                    user_generic_loader_arc_t *ptThis,
                                    user_generic_loader_arc_cfg_t *ptCFG)
{
    assert(NULL != ptThis);
    assert(NULL != ptCFG);
    memset(ptThis, 0, sizeof(user_generic_loader_arc_t));

    this.tCFG = *ptCFG;
    if ((this.tCFG.tSize.iWidth <= 0)
     || (this.tCFG.tSize.iHeight <= 0)) {
        return ARM_2D_ERR_INVALID_PARAM;
    }

    arm_2d_err_t tResult = user_generic_loader_arc_set(ptThis, &ptCFG->tArc);
    if (tResult < 0) {
        return tResult;
    }

    arm_generic_loader_cfg_t tCFG = {
        .bUseHeapForVRES = this.tCFG.bUseHeapForVRES,
        .tColourInfo.chScheme = ARM_2D_COLOUR_MASK_A8,
        .bBlendWithBG = true,
        .tBackgroundColour.chColour = 0,
        .ImageIO = {
            .ptIO = this.tCFG.ImageIO.ptIO,
            .pTarget = this.tCFG.ImageIO.pTarget,
        },
        .UserDecoder = {
            .fnDecoderInit = &__user_generic_loader_arc_decoder_init,
            .fnDecode = &__user_generic_loader_arc_draw,
        },
        .ptScene = this.tCFG.ptScene,
    };

    tResult = arm_generic_loader_init(
                            &this.use_as__arm_generic_loader_t,
                            &tCFG);
    if (tResult < 0) {
        return tResult;
    }

    this.tTile.tRegion.tSize = this.tCFG.tSize;
    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1)
void user_generic_loader_arc_depose(user_generic_loader_arc_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_depose(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void user_generic_loader_arc_on_load(user_generic_loader_arc_t *ptThis)
{
    assert(NULL != ptThis);
    this.Runtime.tDirtyRegion = this.Runtime.tBounds;
    this.Runtime.bForceFullDirtyRegion = true;
    arm_generic_loader_on_load(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void user_generic_loader_arc_on_frame_start(user_generic_loader_arc_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_on_frame_start(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void user_generic_loader_arc_on_frame_complete(user_generic_loader_arc_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_on_frame_complete(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void user_generic_loader_arc_show(user_generic_loader_arc_t *ptThis,
                                  const arm_2d_tile_t *ptTile,
                                  const arm_2d_region_t *ptRegion,
                                  bool bIsNewFrame)
{
    assert(NULL != ptThis);
    ARM_2D_UNUSED(bIsNewFrame);

    if (-1 == (intptr_t)ptTile) {
        ptTile = arm_2d_get_default_frame_buffer();
    }

    arm_2d_fill_colour_with_mask(
        ptTile,
        ptRegion,
        &this.tTile,
        (__arm_2d_color_t){this.tCFG.tArc.hwColour});
}

ARM_NONNULL(1)
static
arm_2d_err_t __user_generic_loader_arc_decoder_init(
                                                arm_generic_loader_t *ptObj)
{
    assert(NULL != ptObj);
    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1, 2, 3)
static
arm_2d_err_t __user_generic_loader_arc_draw(
                                                arm_generic_loader_t *ptObj,
                                                arm_2d_region_t *ptROI,
                                                uint8_t *pchBuffer,
                                                uint32_t iTargetStrideInByte,
                                                uint_fast8_t chBitsPerPixel)
{
    assert(NULL != ptObj);
    user_generic_loader_arc_t *ptThis =
        (user_generic_loader_arc_t *)ptObj;

    if (8u != chBitsPerPixel) {
        return ARM_2D_ERR_INVALID_PARAM;
    }

    for (int_fast16_t iY = 0; iY < ptROI->tSize.iHeight; iY++) {
        memset(pchBuffer + iY * iTargetStrideInByte,
               0,
               ptROI->tSize.iWidth);
    }

    if (!this.Runtime.bVisible) {
        return ARM_2D_ERR_NONE;
    }

    int32_t iLeft = ptROI->tLocation.iX;
    int32_t iTop = ptROI->tLocation.iY;
    int32_t iRight = iLeft + ptROI->tSize.iWidth;
    int32_t iBottom = iTop + ptROI->tSize.iHeight;
    int32_t iBoundsRight = this.Runtime.tBounds.tLocation.iX
                         + this.Runtime.tBounds.tSize.iWidth;
    int32_t iBoundsBottom = this.Runtime.tBounds.tLocation.iY
                          + this.Runtime.tBounds.tSize.iHeight;

    if (iLeft < this.Runtime.tBounds.tLocation.iX) {
        iLeft = this.Runtime.tBounds.tLocation.iX;
    }
    if (iTop < this.Runtime.tBounds.tLocation.iY) {
        iTop = this.Runtime.tBounds.tLocation.iY;
    }
    if (iRight > iBoundsRight) {
        iRight = iBoundsRight;
    }
    if (iBottom > iBoundsBottom) {
        iBottom = iBoundsBottom;
    }
    if ((iLeft >= iRight) || (iTop >= iBottom)) {
        return ARM_2D_ERR_NONE;
    }

    for (int32_t iY = iTop; iY < iBottom; iY++) {
        int32_t iDY = iY - this.tCFG.tArc.tCenter.iY;
        uint32_t wDY2 = (uint32_t)(iDY * iDY);
        if (wDY2 >= this.Runtime.wOuterBorderRadiusSquared) {
            continue;
        }

        int32_t iHalfWidth = (int32_t)__user_generic_loader_arc_sqrt_u32(
            this.Runtime.wOuterBorderRadiusSquared - wDY2);
        int32_t iXStart = this.tCFG.tArc.tCenter.iX - iHalfWidth;
        int32_t iXEnd = this.tCFG.tArc.tCenter.iX + iHalfWidth + 1;
        if (iXStart < iLeft) {
            iXStart = iLeft;
        }
        if (iXEnd > iRight) {
            iXEnd = iRight;
        }

        uint8_t *pchPixel = pchBuffer
            + (iY - ptROI->tLocation.iY) * iTargetStrideInByte
            + (iXStart - ptROI->tLocation.iX);

        for (int32_t iX = iXStart; iX < iXEnd; iX++, pchPixel++) {
            int32_t iDX = iX - this.tCFG.tArc.tCenter.iX;
            uint32_t wDistanceSquared = (uint32_t)(iDX * iDX) + wDY2;
            uint8_t chOpacity = __user_generic_loader_arc_radius_opacity(
                                                ptThis, wDistanceSquared);
            if (0u == chOpacity) {
                continue;
            }

            uint8_t chDirectionOpacity =
                __user_generic_loader_arc_direction_opacity(
                                                    ptThis, iDX, iDY);
            if (0u == chDirectionOpacity) {
                continue;
            }
            *pchPixel = MIN(chOpacity, chDirectionOpacity);
        }
    }

    return ARM_2D_ERR_NONE;
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#endif
