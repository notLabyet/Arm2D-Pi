/*
 * QOI 场景集成示例
 * 
 * 这个文件展示如何在你的应用中集成和使用新的 QOI 场景。
 * 您可以参考这个示例来添加 QOI 场景到你的应用中。
 */

#include "arm_2d_scene_qoi.h"

/* 全局场景指针（示例） */
static user_scene_qoi_t *g_ptSceneQOI = NULL;

/*
 * ============================================================================
 * 示例 1: 基础集成 - 最简单的方式
 * ============================================================================
 */

void example_qoi_scene_basic_init(arm_2d_scene_player_t *ptDispAdapter)
{
    /* 1. 创建 QOI 场景 */
    g_ptSceneQOI = arm_2d_scene_qoi_init(ptDispAdapter);
    
    if (NULL != g_ptSceneQOI) {
        /* 2. 设置 SD 卡上的图像路径 */
        user_scene_qoi_set_image_path(g_ptSceneQOI, "0:/image.qoi");
        
        /* 3. 将场景添加到播放器 */
        arm_2d_scene_player_append_scenes(
            ptDispAdapter,
            (arm_2d_scene_t *)g_ptSceneQOI,
            1);  /* 1 个场景 */
    }
}

/*
 * ============================================================================
 * 示例 2: 动态切换图像
 * ============================================================================
 */

void example_qoi_scene_switch_image(const char *pszImagePath)
{
    if (NULL != g_ptSceneQOI) {
        /* 设置新的图像路径 */
        user_scene_qoi_set_image_path(g_ptSceneQOI, pszImagePath);
        
        /* 如果需要立即重新加载，可能需要重新初始化场景加载器 */
        /* 这取决于你的应用设计 */
    }
}

/*
 * ============================================================================
 * 示例 3: 预分配内存的集成
 * ============================================================================
 */

/* 预分配的场景结构体 */
static user_scene_qoi_t g_tSceneQOI_PreAllocated;

void example_qoi_scene_with_preallocated_memory(arm_2d_scene_player_t *ptDispAdapter)
{
    /* 1. 使用预分配的内存创建场景 */
    user_scene_qoi_t *ptScene = __arm_2d_scene_qoi_init(
        ptDispAdapter,
        &g_tSceneQOI_PreAllocated);
    
    if (NULL != ptScene) {
        /* 2. 设置图像路径 */
        user_scene_qoi_set_image_path(ptScene, "0:/background.qoi");
        
        /* 3. 添加到播放器 */
        arm_2d_scene_player_append_scenes(
            ptDispAdapter,
            (arm_2d_scene_t *)ptScene,
            1);
    }
}

/*
 * ============================================================================
 * 示例 4: 支持多个 QOI 场景
 * ============================================================================
 */

typedef struct {
    user_scene_qoi_t *ptScene1;
    user_scene_qoi_t *ptScene2;
    user_scene_qoi_t *ptScene3;
} qoi_scenes_t;

static qoi_scenes_t g_tQOIScenes;

void example_qoi_scene_multiple(arm_2d_scene_player_t *ptDispAdapter)
{
    /* 创建多个 QOI 场景 */
    g_tQOIScenes.ptScene1 = arm_2d_scene_qoi_init(ptDispAdapter);
    g_tQOIScenes.ptScene2 = arm_2d_scene_qoi_init(ptDispAdapter);
    g_tQOIScenes.ptScene3 = arm_2d_scene_qoi_init(ptDispAdapter);
    
    /* 为每个场景设置不同的图像 */
    user_scene_qoi_set_image_path(g_tQOIScenes.ptScene1, "0:/image1.qoi");
    user_scene_qoi_set_image_path(g_tQOIScenes.ptScene2, "0:/image2.qoi");
    user_scene_qoi_set_image_path(g_tQOIScenes.ptScene3, "0:/image3.qoi");
    
    /* 添加所有场景到播放器 */
    arm_2d_scene_player_append_scenes(
        ptDispAdapter,
        (arm_2d_scene_t *)g_tQOIScenes.ptScene1,
        1);
    
    arm_2d_scene_player_append_scenes(
        ptDispAdapter,
        (arm_2d_scene_t *)g_tQOIScenes.ptScene2,
        1);
    
    arm_2d_scene_player_append_scenes(
        ptDispAdapter,
        (arm_2d_scene_t *)g_tQOIScenes.ptScene3,
        1);
}

/*
 * ============================================================================
 * 示例 5: 与其他场景混合使用
 * ============================================================================
 */

void example_qoi_scene_with_other_scenes(
    arm_2d_scene_player_t *ptDispAdapter,
    user_scene_histogram_t *ptHistogramScene,
    user_scene_meter_t *ptMeterScene)
{
    /* 创建 QOI 场景 */
    user_scene_qoi_t *ptQOIScene = arm_2d_scene_qoi_init(ptDispAdapter);
    user_scene_qoi_set_image_path(ptQOIScene, "0:/overlay.qoi");
    
    /* 按顺序添加所有场景到播放器，形成完整的场景播放列表 */
    arm_2d_scene_player_append_scenes(
        ptDispAdapter,
        (arm_2d_scene_t *)ptHistogramScene,
        1);
    
    arm_2d_scene_player_append_scenes(
        ptDispAdapter,
        (arm_2d_scene_t *)ptMeterScene,
        1);
    
    arm_2d_scene_player_append_scenes(
        ptDispAdapter,
        (arm_2d_scene_t *)ptQOIScene,
        1);
}

/*
 * ============================================================================
 * 示例 6: 带错误处理的集成
 * ============================================================================
 */

typedef enum {
    QOI_SCENE_RESULT_SUCCESS = 0,
    QOI_SCENE_RESULT_INIT_FAILED,
    QOI_SCENE_RESULT_PATH_INVALID,
    QOI_SCENE_RESULT_FILE_NOT_FOUND,
} qoi_scene_result_t;

qoi_scene_result_t example_qoi_scene_with_error_handling(
    arm_2d_scene_player_t *ptDispAdapter,
    const char *pszImagePath)
{
    if (NULL == ptDispAdapter) {
        return QOI_SCENE_RESULT_INIT_FAILED;
    }
    
    if (NULL == pszImagePath) {
        return QOI_SCENE_RESULT_PATH_INVALID;
    }
    
    /* 创建场景 */
    g_ptSceneQOI = arm_2d_scene_qoi_init(ptDispAdapter);
    if (NULL == g_ptSceneQOI) {
        return QOI_SCENE_RESULT_INIT_FAILED;
    }
    
    /* 设置图像路径 */
    user_scene_qoi_set_image_path(g_ptSceneQOI, pszImagePath);
    
    /* 添加到播放器 */
    arm_2d_scene_player_append_scenes(
        ptDispAdapter,
        (arm_2d_scene_t *)g_ptSceneQOI,
        1);
    
    return QOI_SCENE_RESULT_SUCCESS;
}

/*
 * ============================================================================
 * 使用提示
 * ============================================================================
 * 
 * 1. SD 卡文件路径格式:
 *    - "0:/image.qoi"           - SD 卡根目录
 *    - "0:/images/background.qoi" - SD 卡子文件夹
 *    - "/image.qoi"             - 相对路径
 *
 * 2. 推荐的应用层集成流程:
 *    a) 在主函数或初始化函数中调用 example_qoi_scene_basic_init()
 *    b) 确保 SD 卡文件系统已初始化
 *    c) 将 QOI 文件复制到 SD 卡
 *    d) 启动场景播放器
 *
 * 3. 性能优化:
 *    - 使用分部解码模式以降低内存峰值
 *    - QOI 解码速度比 JPG 快 2-3 倍
 *    - 脏区域优化自动启用，无需手动配置
 *
 * 4. 调试建议:
 *    - 检查 g_ptSceneQOI->pszImagePath 确认路径正确
 *    - 检查 SD 卡文件系统是否正常工作
 *    - 使用调试器查看场景状态
 */
