#ifndef __ARM_2D_SCENE_SD_QOI_H__
#define __ARM_2D_SCENE_SD_QOI_H__

/*============================ INCLUDES ======================================*/

#include "arm_2d.h"
#include "arm_2d_helper_scene.h"
#include "arm_loader_io_fatfs.h"
#include "qoi_loader/qoi_loader.h"

#ifdef   __cplusplus
extern "C" {
#endif

/*============================ MACROS ========================================*/

/* OOC header, please DO NOT modify  */
#ifdef __USER_SCENE_SD_QOI_IMPLEMENT__
#   undef __USER_SCENE_SD_QOI_IMPLEMENT__
#   define __ARM_2D_IMPL__
#endif
#include "arm_2d_utils.h"

/*============================ MACROFIED FUNCTIONS ===========================*/

#define arm_2d_scene_sd_qoi_init(__DISP_ADAPTER_PTR, ...)                          \
            __arm_2d_scene_sd_qoi_init((__DISP_ADAPTER_PTR), (NULL, ##__VA_ARGS__))

/*============================ TYPES =========================================*/

typedef struct user_scene_sd_qoi_t user_scene_sd_qoi_t;

struct user_scene_sd_qoi_t {
    implement(arm_2d_scene_t);

ARM_PRIVATE(
    int64_t lTimestamp[2];
    bool bUserAllocated;
    bool bQOIReady;
    bool bUseFilm;

    arm_qoi_loader_t tQOI;
    arm_loader_io_fatfs_t tQOIFile;
    arm_2d_helper_film_t tFilm;

    uint32_t wFPSFrames;
)
};

/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/

ARM_NONNULL(1)
extern
user_scene_sd_qoi_t *__arm_2d_scene_sd_qoi_init( arm_2d_scene_player_t *ptDispAdapter,
                                            user_scene_sd_qoi_t *ptScene);

extern
void arm_2d_scene_sd_qoi_loader(void);

#ifdef   __cplusplus
}
#endif

#endif
