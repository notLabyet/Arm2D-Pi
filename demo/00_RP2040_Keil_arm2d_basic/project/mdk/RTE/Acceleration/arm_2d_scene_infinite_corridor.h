/*
 * Copyright (c) 2009-2025 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ARM_2D_SCENE_INFINITE_CORRIDOR_H__
#define __ARM_2D_SCENE_INFINITE_CORRIDOR_H__

/*============================ INCLUDES ======================================*/

#if defined(_RTE_)
#   include "RTE_Components.h"
#endif

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#include "arm_2d_helper.h"
#include "arm_2d_example_controls.h"
#include "user_generic_loader_infinite_corridor.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wunknown-warning-option"
#   pragma clang diagnostic ignored "-Wreserved-identifier"
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#   pragma clang diagnostic ignored "-Wpadded"
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wformat="
#   pragma GCC diagnostic ignored "-Wpedantic"
#   pragma GCC diagnostic ignored "-Wpadded"
#endif

/*============================ MACROS ========================================*/

/* OOC header, please DO NOT modify */
#ifdef __USER_SCENE_INFINITE_CORRIDOR_IMPLEMENT__
#   define __ARM_2D_IMPL__
#endif
#ifdef __USER_SCENE_INFINITE_CORRIDOR_INHERIT__
#   define __ARM_2D_INHERIT__
#endif
#include "arm_2d_utils.h"

/*============================ MACROFIED FUNCTIONS ===========================*/

/*!
 * \brief initialize the infinite corridor scene and append it to a scene player
 * \param[in] __DISP_ADAPTER_PTR the target display adapter
 * \param[in] ... optional user-managed user_scene_infinite_corridor_t instance
 * \return user_scene_infinite_corridor_t* the initialized scene instance
 */
#define arm_2d_scene_infinite_corridor_init(__DISP_ADAPTER_PTR, ...)           \
            __arm_2d_scene_infinite_corridor_init(                            \
                (__DISP_ADAPTER_PTR), (NULL, ##__VA_ARGS__))

/*============================ TYPES =========================================*/

typedef struct user_scene_infinite_corridor_t
    user_scene_infinite_corridor_t;

struct user_scene_infinite_corridor_t {
    implement(arm_2d_scene_t);                                                  //! derived from class: arm_2d_scene_t

ARM_PRIVATE(
    int64_t lTimestamp[1];
    bool bUserAllocated;
    user_generic_loader_infinite_corridor_t tCorridor;
)
};

/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/

ARM_NONNULL(1)
extern
user_scene_infinite_corridor_t *__arm_2d_scene_infinite_corridor_init(
                                    arm_2d_scene_player_t *ptDispAdapter,
                                    user_scene_infinite_corridor_t *ptScene);

#if defined(__clang__)
#   pragma clang diagnostic pop
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic pop
#endif

#undef __USER_SCENE_INFINITE_CORRIDOR_IMPLEMENT__
#undef __USER_SCENE_INFINITE_CORRIDOR_INHERIT__

#ifdef __cplusplus
}
#endif

#endif

#endif
