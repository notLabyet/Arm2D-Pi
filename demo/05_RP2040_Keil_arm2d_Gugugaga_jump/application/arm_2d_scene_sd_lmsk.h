#ifndef __ARM_2D_SCENE_SD_LMSK_H__
#define __ARM_2D_SCENE_SD_LMSK_H__

/*============================ INCLUDES ======================================*/

#include "arm_2d.h"
#include "arm_2d_helper_scene.h"
#include "arm_loader_io_fatfs.h"
#include "lmsk_loader/lmsk_loader.h"

#ifdef   __cplusplus
extern "C" {
#endif

/*============================ MACROS ========================================*/

/* OOC header, please DO NOT modify  */
#ifdef __USER_SCENE_SD_LMSK_IMPLEMENT__
#   undef __USER_SCENE_SD_LMSK_IMPLEMENT__
#   define __ARM_2D_IMPL__
#endif
#include "arm_2d_utils.h"

/*============================ MACROFIED FUNCTIONS ===========================*/

#define arm_2d_scene_sd_lmsk_init(__DISP_ADAPTER_PTR, ...)                         \
            __arm_2d_scene_sd_lmsk_init((__DISP_ADAPTER_PTR), (NULL, ##__VA_ARGS__))

/*============================ TYPES =========================================*/

typedef struct user_scene_sd_lmsk_t user_scene_sd_lmsk_t;

struct user_scene_sd_lmsk_t {
    implement(arm_2d_scene_t);

ARM_PRIVATE(
    int64_t lTimestamp[2];
    bool bUserAllocated;
    bool bLMSKReady;

    uint8_t chColourTableIndex;
    uint16_t hwFPS;
    uint32_t wPreviousColour;
    COLOUR_INT tColour;

    arm_lmsk_loader_t tAnimation;
    arm_loader_io_fatfs_t tLMSKFile;
    arm_2d_helper_film_t tFilm;
)
};

/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/

ARM_NONNULL(1)
extern
user_scene_sd_lmsk_t *__arm_2d_scene_sd_lmsk_init( arm_2d_scene_player_t *ptDispAdapter,
                                              user_scene_sd_lmsk_t *ptScene);

extern
void arm_2d_scene_sd_lmsk_loader(void);

#ifdef   __cplusplus
}
#endif

#endif
