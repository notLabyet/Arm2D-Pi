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

#ifndef __ARM_2D_SCENE_1_H__
#define __ARM_2D_SCENE_1_H__

/*============================ INCLUDES ======================================*/

#if defined(_RTE_)
#   include "RTE_Components.h"
#endif

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#include "arm_2d_helper.h"
#include "arm_2d_example_controls.h"
#include "arm_2d_example_loaders.h"
#include "arm_loader_io_fatfs.h"
#include "fal.h"

#ifdef RTE_Acceleration_Arm_2D_Scene1

#ifdef   __cplusplus
extern "C" {
#endif

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wunknown-warning-option"
#   pragma clang diagnostic ignored "-Wreserved-identifier"
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#   pragma clang diagnostic ignored "-Wpadded"
#elif __IS_COMPILER_ARM_COMPILER_5__
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wformat="
#   pragma GCC diagnostic ignored "-Wpedantic"
#   pragma GCC diagnostic ignored "-Wpadded"
#endif

/*============================ MACROS ========================================*/

#ifdef __USER_SCENE1_IMPLEMENT__
#   define __USER_SCENE_1_IMPLEMENT__
#endif

#ifdef __USER_SCENE1_INHERIT__
#   define __USER_SCENE_1_INHERIT__
#endif

/* OOC header, please DO NOT modify  */
#ifdef __USER_SCENE_1_IMPLEMENT__
#   define __ARM_2D_IMPL__
#endif

#ifdef __USER_SCENE_1_INHERIT__
#   define __ARM_2D_INHERIT__
#endif

#include "arm_2d_utils.h"

#ifndef __USER_SCENE_1_USE_LMSK__
#   define __USER_SCENE_1_USE_LMSK__    0
#endif

#ifndef __USER_SCENE1_USE_LMSK__
#   define __USER_SCENE1_USE_LMSK__     __USER_SCENE_1_USE_LMSK__
#endif

#ifndef __USER_SCENE_1_USE_SD_JPG__
#   define __USER_SCENE_1_USE_SD_JPG__  1
#endif


#ifndef __USER_SCENE_1_USE_ZJPGD__
#   define __USER_SCENE_1_USE_ZJPGD__   ARM_2D_DEMO_USE_ZJPGD
#endif

#if __USER_SCENE_1_USE_SD_JPG__ && __USER_SCENE_1_USE_ZJPGD__ \
 && !defined(RTE_Acceleration_Arm_2D_Extra_ZJpgDec_Loader)
#   error __USER_SCENE_1_USE_ZJPGD__ requires RTE_Acceleration_Arm_2D_Extra_ZJpgDec_Loader
#endif

#if __USER_SCENE_1_USE_SD_JPG__ && !__USER_SCENE_1_USE_ZJPGD__ \
 && !defined(RTE_Acceleration_Arm_2D_Extra_TJpgDec_Loader)
#   error __USER_SCENE_1_USE_SD_JPG__ requires RTE_Acceleration_Arm_2D_Extra_TJpgDec_Loader
#endif

/*============================ MACROFIED FUNCTIONS ===========================*/

#define arm_2d_scene_1_init(__DISP_ADAPTER_PTR, ...)                              \
            __arm_2d_scene_1_init((__DISP_ADAPTER_PTR), (NULL, ##__VA_ARGS__))

#define arm_2d_scene1_init(__DISP_ADAPTER_PTR, ...)                               \
            arm_2d_scene_1_init((__DISP_ADAPTER_PTR), ##__VA_ARGS__)

/*============================ TYPES =========================================*/

#if __USER_SCENE_1_USE_LMSK__
enum {
    SCENE_1_LMSK_CMSIS = 0,
    __SCENE_1_LMSK_COUNT,
};
#endif

typedef struct user_scene_1_t user_scene_1_t;

struct user_scene_1_t {
    implement(arm_2d_scene_t);

ARM_PRIVATE(
    int64_t lTimestamp[3];
    bool bUserAllocated;

#if __USER_SCENE_1_USE_LMSK__
    ARM_LMSK_GROUP_DEF(__SCENE_1_LMSK_COUNT);
#endif

    int16_t iTargetOffsetX;
    int16_t iTargetOffsetY;
    int16_t iOffsetX;
    int16_t iOffsetY;
    int32_t nParallaxOffsetXQ4;
    int32_t nParallaxOffsetYQ4;
    int32_t nParallaxVelocityXQ4;
    int32_t nParallaxVelocityYQ4;
    arm_2d_helper_film_t tFilm;
    arm_2d_helper_film_t tFilmMask;
#if __USER_SCENE_1_USE_SD_JPG__
    uint8_t chSaberCacheState;
    uint16_t hwSaberCacheFrameIndex;
    uint16_t hwSaberCacheStripeY;
    uint32_t wSaberCacheTotalSize;
    uint16_t *phwSaberCacheStripeBuffer;
    const struct fal_partition *ptSaberCachePartition;
    arm_2d_tile_t tSaberCacheTile;
    arm_2d_helper_film_t tSaberJPGFilm;
    bool bSaberJPGReady;
    bool bSaberCacheReady;
    int16_t iSaberFileStatus;
    uint32_t wSaberFileSize;
#if __USER_SCENE_1_USE_ZJPGD__
    arm_zjpgd_loader_t tSaberJPG;
#else
    arm_tjpgd_loader_t tSaberJPG;
#endif
    arm_loader_io_fatfs_t tSaberJPGFile;
#endif
    uint8_t chOpacity;
    bool bLightOn;
)
};

/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/

ARM_NONNULL(1)
extern
user_scene_1_t *__arm_2d_scene_1_init(arm_2d_scene_player_t *ptDispAdapter,
                                      user_scene_1_t *ptScene);

ARM_NONNULL(1)
extern
user_scene_1_t *__arm_2d_scene1_init(arm_2d_scene_player_t *ptDispAdapter,
                                     user_scene_1_t *ptScene);

#if defined(__clang__)
#   pragma clang diagnostic pop
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic pop
#endif

#undef __USER_SCENE1_IMPLEMENT__
#undef __USER_SCENE1_INHERIT__
#undef __USER_SCENE_1_IMPLEMENT__
#undef __USER_SCENE_1_INHERIT__

#ifdef   __cplusplus
}
#endif

#endif

#endif

#endif
