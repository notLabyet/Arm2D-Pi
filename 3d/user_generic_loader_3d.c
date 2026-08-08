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
#define __ThD_sim_IMPLEMENT__

#include "user_generic_loader_3d.h"
#include "ThD_test.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_Disp_Adapter0)
#   include "arm_2d_disp_adapter_0.h"
#endif

#include "hp_oram.h"
#include "hp_oram1.h"

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


#define ROSE_VERTEX_COUNT 843
#define ROSE_TRI_COUNT 1726

#ifndef THD_HOLO_PARALLAX_X_GAIN_Q16
#   define THD_HOLO_PARALLAX_X_GAIN_Q16        Q16(-0.09f)
#endif

#ifndef THD_HOLO_PARALLAX_Y_GAIN_Q16
#   define THD_HOLO_PARALLAX_Y_GAIN_Q16        Q16(0.07f)
#endif


#ifndef THD_CFG_ENABLE_WIREFRAME
#   define THD_CFG_ENABLE_WIREFRAME            0
#endif

#ifndef THD_CFG_ENABLE_FILL
#   define THD_CFG_ENABLE_FILL                 1
#endif

#ifndef THD_CFG_ENABLE_OCCLUSION
#   define THD_CFG_ENABLE_OCCLUSION            1
#endif

#ifndef THD_CFG_ENABLE_FLAT_SHADING
#   define THD_CFG_ENABLE_FLAT_SHADING         1
#endif

#ifndef THD_CFG_ENABLE_SMOOTH_SHADING
#   define THD_CFG_ENABLE_SMOOTH_SHADING       0
#endif

#ifndef THD_CFG_ENABLE_DEPTH_FOG
#   define THD_CFG_ENABLE_DEPTH_FOG            0
#endif

#ifndef THD_MODEL_LINE_COLOUR
#   define THD_MODEL_LINE_COLOUR               GLCD_COLOR_YELLOW//GLCD_COLOR_GREEN
#endif

#ifndef THD_MODEL_FILL_COLOUR
#   define THD_MODEL_FILL_COLOUR               __RGB(   0XAA, 0X00,  0X1A)
#endif

#ifndef THD_DEPTH_FOG_COLOUR
#   define THD_DEPTH_FOG_COLOUR                __RGB(   0XD6,  0XD8,  0XE0)
#endif

#ifndef THD_NEAR_PLANE_Q16
#   define THD_NEAR_PLANE_Q16                   (1 << 10)
#endif

#define THD_LIGHT_X_Q16                        Q16(0.28f)
#define THD_LIGHT_Y_Q16                        Q16(-0.40f)
#define THD_LIGHT_Z_Q16                        Q16(0.87f)
#define THD_LIGHT_AMBIENT_Q16                  Q16(0.22f)
#define THD_LIGHT_DIFFUSE_Q16                  Q16(0.78f)

//#define THD_LIGHT_X_Q16                        Q16(0.28f)

#ifndef THD_DEPTH_FOG_START_Q16
#   define THD_DEPTH_FOG_START_Q16             (DEPTH)
#endif

#ifndef THD_CFG_DEPTH_BUFFER_SHIFT
#   define THD_CFG_DEPTH_BUFFER_SHIFT          4
#endif

#ifndef THD_CFG_Z_BUFFER_PIXEL_COUNT
#   if defined(__DISP0_CFG_PFB_BLOCK_HEIGHT__)
#       define THD_CFG_Z_BUFFER_PIXEL_COUNT    (CANVA_WIDTH * __DISP0_CFG_PFB_BLOCK_HEIGHT__)
#   else
#       define THD_CFG_Z_BUFFER_PIXEL_COUNT    (CANVA_WIDTH * 60)
#   endif
#endif

#define THD_Z_BUFFER_FAR                       0xFFFFu
#define THD_DEG_TO_Q31(__DEG)                  Q31((__DEG) / 360.0f)

/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/
typedef struct thd_model_instance_t {
    const Thd_point_t *ptVertices;
    const tri_t *ptTris;
    const int16_t (*pi16FaceNormalsQ14)[3];
    uint16_t hwVertexCount;
    uint16_t hwTriCount;
    uint16_t hwColour;
    uint16_t hwWireframeColour;
    thd_clock_hand_t tClockHand;
    q16_t q16Scale;
    Thd_point_t tOffset;
    struct {
        q31_t x;
        q31_t y;
        q31_t z;
    } tInitialAngleOffset;
} thd_model_instance_t;

typedef struct thd_projected_vertex_t {
    arm_2d_location_t tLocation;
    q16_t z;
#if THD_CFG_ENABLE_SMOOTH_SHADING
    Thd_point_t tCamera;
    Thd_point_t tNormal;
    q16_t q16Brightness;
#endif
} thd_projected_vertex_t;

typedef struct thd_face_draw_item_t {
    uint16_t hwIndex;
    uint16_t hwModelIndex;
} thd_face_draw_item_t;

/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/
ARM_NONNULL(1)
static
arm_2d_err_t __ThD_sim_decoder_init(arm_generic_loader_t *ptObj);

ARM_NONNULL(1, 2, 3)
static
arm_2d_err_t __ThD_sim_draw(  arm_generic_loader_t *ptObj,
                              arm_2d_region_t *ptROI,
                              uint8_t *pchBuffer,
                              uint32_t iTargetStrideInByte,
                              uint_fast8_t chBitsPerPixel);

ARM_NONNULL(1)
static
void __ThD_sim_prepare_frame(ThD_sim_t *ptThis);


/*============================ LOCAL VARIABLES ===============================*/
extern Thd_point_t j20_vertices[];
extern tri_t j20_tris[];
extern const tri_t david_tris[];
extern const Thd_point_t david_vertices[];
extern Thd_point_t earth_model_vertices[];

extern const Thd_point_t rose_vertices[];
extern const tri_t rose_tris[];
extern const int16_t rose_face_normals_q14[][3];

extern int16_t DATA_GY_ACC_RAW[6];
extern volatile uint32_t DATA_GY_ACC_SAMPLE_SEQ;

static const thd_model_instance_t s_tModelInstances[] = {
	
    {
        .ptVertices = rose_vertices,
        .ptTris = rose_tris,
        .pi16FaceNormalsQ14 = rose_face_normals_q14,
        .hwVertexCount = ROSE_VERTEX_COUNT,
        .hwTriCount = ROSE_TRI_COUNT,
        .hwColour = THD_MODEL_FILL_COLOUR,
        .hwWireframeColour = THD_MODEL_FILL_COLOUR,
        .q16Scale = Q16(1.0f),
        .tOffset = {
            .x = Q16(0.0f),
            .y = Q16(0.00f),
            .z = Q16(0.00f),
        },	
        .tInitialAngleOffset = {
            .x = THD_DEG_TO_Q31(105.0f),
            .y = THD_DEG_TO_Q31(0),
            .z = THD_DEG_TO_Q31(0.0f),
        },	
	}
//    {
//        .ptVertices = hp_oram_vertices,
//        .ptTris = hp_oram_tris,
//        .pi16FaceNormalsQ14 = hp_oram_vertex_normals_q14,
//        .hwVertexCount = HP_ORAM_VERTEX_COUNT,
//        .hwTriCount = HP_ORAM_TRI_COUNT,
//        .hwColour = THD_MODEL_FILL_COLOUR,
//        .hwWireframeColour = GLCD_COLOR_ORANGE,
//        .tClockHand = THD_CLOCK_HAND_HOUR,
//        .q16Scale = Q16(0.6f),
//        .tOffset = {
//            .x = Q16(0.0f),
//            .y = Q16(0.00f),
//            .z = Q16(0.00f),
//        },
//        .tInitialAngleOffset = {
//            .x = THD_DEG_TO_Q31(-90.0f),
//            .y = THD_DEG_TO_Q31(0),
//            .z = THD_DEG_TO_Q31(0.0f),
//        },
//    },
//    {
//        .ptVertices = hp_oram1_vertices,
//        .ptTris = hp_oram1_tris,
//        .pi16FaceNormalsQ14 = hp_oram1_vertex_normals_q14,
//        .hwVertexCount = HP_ORAM1_VERTEX_COUNT,
//        .hwTriCount = HP_ORAM1_TRI_COUNT,
//        .hwColour = GLCD_COLOR_SKY_BLUE,
//        .hwWireframeColour = GLCD_COLOR_SKY_BLUE,
//        .tClockHand = THD_CLOCK_HAND_MINUTE,
//        .q16Scale = Q16(0.95f),
//        .tOffset = {
//            .x = Q16(0.0f),
//            .y = Q16(0.00f),
//            .z = Q16(0.2f),
//        },
//        .tInitialAngleOffset = {
//            .x = THD_DEG_TO_Q31(-90.0f),
//            .y = THD_DEG_TO_Q31(0),
//            .z = THD_DEG_TO_Q31(0.0f),
//        },
//    },
};

#define THD_MODEL_INSTANCE_COUNT               (sizeof(s_tModelInstances) / sizeof(s_tModelInstances[0]))
#define THD_MODEL_TOTAL_VERTEX_COUNT            \
    ROSE_VERTEX_COUNT
#define THD_MODEL_TOTAL_TRI_COUNT               \
    ROSE_TRI_COUNT

#ifndef THD_MAX_PROJECTED_VERTEX_COUNT
#   define THD_MAX_PROJECTED_VERTEX_COUNT      THD_MODEL_TOTAL_VERTEX_COUNT
#endif

#ifndef THD_MAX_VISIBLE_FACE_COUNT
#   define THD_MAX_VISIBLE_FACE_COUNT          THD_MODEL_TOTAL_TRI_COUNT
#endif

static thd_projected_vertex_t s_tProjectedVertices[THD_MAX_PROJECTED_VERTEX_COUNT];
static thd_face_draw_item_t s_tFaceOrder[THD_MAX_VISIBLE_FACE_COUNT];
static uint16_t s_hwModelVertexBase[THD_MODEL_INSTANCE_COUNT];
static uint16_t s_hwModelVertexCount[THD_MODEL_INSTANCE_COUNT];
static uint16_t s_hwVisibleFaceCount;
static uint16_t s_hwProjectedVertexCount;
static Thd_point_t s_tModelLight[THD_MODEL_INSTANCE_COUNT];
static mat3_t s_tModelBaseMatrices[THD_MODEL_INSTANCE_COUNT];
static uint16_t s_hwZBuffer[THD_CFG_Z_BUFFER_PIXEL_COUNT];
/*============================ IMPLEMENTATION ================================*/

static mat3_t __ThD_sim_multiply_matrix(const mat3_t *ptLeft,
                                        const mat3_t *ptRight)
{
    mat3_t tResult;

    for (uint_fast8_t y = 0; y < 3; y++) {
        for (uint_fast8_t x = 0; x < 3; x++) {
            tResult.m[y][x] =
                mul_q16(ptLeft->m[y][0], ptRight->m[0][x])
              + mul_q16(ptLeft->m[y][1], ptRight->m[1][x])
              + mul_q16(ptLeft->m[y][2], ptRight->m[2][x]);
        }
    }

    return tResult;
}

static mat3_t __ThD_sim_rotate_matrix_z(const mat3_t *ptBase,
                                        q31_t q31Angle)
{
    if (0 == q31Angle) {
        return *ptBase;
    }

    q16_t q16Cos = arm_cos_q31(q31Angle) >> 15;
    q16_t q16Sin = arm_sin_q31(q31Angle) >> 15;
    mat3_t tResult = *ptBase;

    for (uint_fast8_t x = 0; x < 3; x++) {
        tResult.m[0][x] =
            mul_q16(q16Cos, ptBase->m[0][x])
          - mul_q16(q16Sin, ptBase->m[1][x]);
        tResult.m[1][x] =
            mul_q16(q16Sin, ptBase->m[0][x])
          + mul_q16(q16Cos, ptBase->m[1][x]);
    }

    return tResult;
}

ARM_NONNULL(1)

void draw_line_fast(uint16_t *fb, uint16_t w,
                    arm_2d_location_t start,
                    arm_2d_location_t end,
                    uint16_t color)
{
    int16_t dx = abs(end.iX - start.iX);
    int16_t sx = start.iX < end.iX ? 1 : -1;

    int16_t dy = -abs(end.iY - start.iY);
    int16_t sy = start.iY < end.iY ? 1 : -1;

    int16_t err = dx + dy;

    while (1) {
        // 画点
        fb[start.iX * w + start.iY] = color;
        if (start.iX == end.iX && start.iY == end.iY) break;

        int16_t e2 = err << 1;

        if (e2 >= dy) {
            err += dy;
            start.iX += sx;
        }

        if (e2 <= dx) {
            err += dx;
            start.iY += sy;
        }
    }
}


void draw_line_fast_rgb565(uint8_t *pchBuffer,
                          int strideByte,
                          int width,
                          int height,
                          arm_2d_location_t start,
                          arm_2d_location_t end,
                          arm_2d_location_t pROI,
                          uint16_t color)
{
    int16_t x0 = start.iX - pROI.iX;
    int16_t y0 = start.iY - pROI.iY;
    int16_t x1 = end.iX - pROI.iX;
    int16_t y1 = end.iY - pROI.iY;

    int16_t dx = abs(x1 - x0);
    int16_t sx = x0 < x1 ? 1 : -1;

    int16_t dy = -abs(y1 - y0);
    int16_t sy = y0 < y1 ? 1 : -1;

    int16_t err = dx + dy;

    while (1) {

        if (x0 >= 0 && x0 < width &&
            y0 >= 0 && y0 < height) {

            uint8_t *row = pchBuffer + y0 * strideByte;
            *((uint16_t *)(row + x0 * 2)) = color;
        }

        if (x0 == x1 && y0 == y1) break;

        int16_t e2 = err << 1;

        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

   const uint16_t BLUE_DEPTH_COLORMAP[32] = {
       0x65BF, 0x619E, 0x5D7E, 0x595D, 0x553D, 0x511C, 0x4CFC, 0x48DB,
       0x44BB, 0x409A, 0x3C7A, 0x3859, 0x3439, 0x3018, 0x2BF8, 0x27D7,
       0x23B7, 0x1F96, 0x1B76, 0x1755, 0x1335, 0x0F14, 0x0AF4, 0x0AD3,
       0x08B3, 0x0892, 0x0872, 0x0851, 0x0831, 0x0810, 0x07F0, 0x07CF
   };
   
   
   
   uint16_t get_color_by_z_q16(int32_t z_q16) {

       int idx = (z_q16 + 65536) * 31 / 131072;
       // 边界保护
       if(idx < 0) idx = 0;
       if(idx >= 32) idx = 31;
       return BLUE_DEPTH_COLORMAP[idx];
   }

void draw_point_rgb565(uint8_t *pchBuffer,
                          int strideByte,
                          int width,
                          int height,
                          arm_2d_location_t point,
                          arm_2d_location_t pROI,
                          uint16_t color)
{
    int16_t x0 = point.iX - pROI.iX;
    int16_t y0 = point.iY - pROI.iY;

    if (x0 >= 0 && x0 < width &&
        y0 >= 0 && y0 < height) {

        uint8_t *row = pchBuffer + y0 * strideByte;
        *((uint16_t *)(row + x0 * 2)) = color;
    }
}

static int32_t __ThD_sim_edge(  int16_t x0, int16_t y0,
                                int16_t x1, int16_t y1,
                                int16_t x,  int16_t y)
{
    return ((int32_t)x1 - x0) * ((int32_t)y - y0)
         - ((int32_t)y1 - y0) * ((int32_t)x - x0);
}

static int16_t __ThD_sim_min3_s16(int16_t a, int16_t b, int16_t c)
{
    int16_t min = (a < b) ? a : b;
    return (min < c) ? min : c;
}

static int16_t __ThD_sim_max3_s16(int16_t a, int16_t b, int16_t c)
{
    int16_t max = (a > b) ? a : b;
    return (max > c) ? max : c;
}

static uint16_t __ThD_sim_scale_rgb565(uint16_t hwColour, q16_t q16Brightness)
{
    uint32_t r = (hwColour >> 11) & 0x1Fu;
    uint32_t g = (hwColour >> 5) & 0x3Fu;
    uint32_t b = hwColour & 0x1Fu;

    if (q16Brightness < 0) {
        q16Brightness = 0;
    }
    if (q16Brightness > Q16_ONE) {
        q16Brightness = Q16_ONE;
    }

    r = (r * (uint32_t)q16Brightness) >> 16;
    g = (g * (uint32_t)q16Brightness) >> 16;
    b = (b * (uint32_t)q16Brightness) >> 16;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

#if THD_CFG_ENABLE_DEPTH_FOG
static uint16_t __ThD_sim_lerp_rgb565(uint16_t hwNearColour,
                                      uint16_t hwFarColour,
                                      q16_t q16NearWeight)
{
    if (q16NearWeight <= 0) {
        return hwFarColour;
    }

    if (q16NearWeight >= Q16_ONE) {
        return hwNearColour;
    }

    uint32_t q16FarWeight = (uint32_t)(Q16_ONE - q16NearWeight);

    uint32_t rNear = (hwNearColour >> 11) & 0x1Fu;
    uint32_t gNear = (hwNearColour >> 5) & 0x3Fu;
    uint32_t bNear = hwNearColour & 0x1Fu;

    uint32_t rFar = (hwFarColour >> 11) & 0x1Fu;
    uint32_t gFar = (hwFarColour >> 5) & 0x3Fu;
    uint32_t bFar = hwFarColour & 0x1Fu;

    uint32_t r = (rNear * (uint32_t)q16NearWeight + rFar * q16FarWeight) >> 16;
    uint32_t g = (gNear * (uint32_t)q16NearWeight + gFar * q16FarWeight) >> 16;
    uint32_t b = (bNear * (uint32_t)q16NearWeight + bFar * q16FarWeight) >> 16;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static q16_t __ThD_sim_depth_fog_factor(q16_t z, q16_t q16Strength)
{
    q16_t q16Distance = z - THD_DEPTH_FOG_START_Q16;

    if ((q16Strength <= 0) || (q16Distance <= 0)) {
        return Q16_ONE;
    }

    q16_t q16Fog = mul_q16(q16Distance, q16Strength);

    if (q16Fog >= Q16_ONE) {
        return 0;
    }

    return Q16_ONE - q16Fog;
}
#endif

static q16_t __ThD_sim_unit_normal_brightness(
                                        q16_t nx,
                                        q16_t ny,
                                        q16_t nz,
                                        const Thd_point_t *ptLight)
{
    q16_t diffuse = mul_q16(nx, ptLight->x)
                  + mul_q16(ny, ptLight->y)
                  + mul_q16(nz, ptLight->z);

    if (diffuse < 0) {
        diffuse = -diffuse;
    }

    return THD_LIGHT_AMBIENT_Q16 + mul_q16(THD_LIGHT_DIFFUSE_Q16, diffuse);
}

static q16_t __ThD_sim_normal_brightness(q16_t nx, q16_t ny, q16_t nz)
{
    q16_t len = sqrt_q16(mul_q16(nx, nx)
                       + mul_q16(ny, ny)
                       + mul_q16(nz, nz));

    if (0 == len) {
        return THD_LIGHT_AMBIENT_Q16;
    }

    nx = div_q16(nx, len);
    ny = div_q16(ny, len);
    nz = div_q16(nz, len);

    const Thd_point_t tLight = {
        .x = THD_LIGHT_X_Q16,
        .y = THD_LIGHT_Y_Q16,
        .z = THD_LIGHT_Z_Q16,
    };

    return __ThD_sim_unit_normal_brightness(nx, ny, nz, &tLight);
}

static q16_t __ThD_sim_q14_normal_brightness(
                                        const int16_t *pi16Normal,
                                        const Thd_point_t *ptLight)
{
    int32_t iDiffuse = (int32_t)pi16Normal[0] * ptLight->x
                     + (int32_t)pi16Normal[1] * ptLight->y
                     + (int32_t)pi16Normal[2] * ptLight->z;

    iDiffuse >>= 14;
    if (iDiffuse < 0) {
        iDiffuse = -iDiffuse;
    }

    return THD_LIGHT_AMBIENT_Q16
         + mul_q16(THD_LIGHT_DIFFUSE_Q16, (q16_t)iDiffuse);
}

static Thd_point_t __ThD_sim_transform_light_to_model(const mat3_t *ptMatrix)
{
    const Thd_point_t tLight = {
        .x = THD_LIGHT_X_Q16,
        .y = THD_LIGHT_Y_Q16,
        .z = THD_LIGHT_Z_Q16,
    };
    Thd_point_t tModelLight;

    tModelLight.x = mul_q16(ptMatrix->m[0][0], tLight.x)
                  + mul_q16(ptMatrix->m[1][0], tLight.y)
                  + mul_q16(ptMatrix->m[2][0], tLight.z);
    tModelLight.y = mul_q16(ptMatrix->m[0][1], tLight.x)
                  + mul_q16(ptMatrix->m[1][1], tLight.y)
                  + mul_q16(ptMatrix->m[2][1], tLight.z);
    tModelLight.z = mul_q16(ptMatrix->m[0][2], tLight.x)
                  + mul_q16(ptMatrix->m[1][2], tLight.y)
                  + mul_q16(ptMatrix->m[2][2], tLight.z);

    return tModelLight;
}

static bool __ThD_sim_face_normal(const Thd_point_t *ptA,
                                  const Thd_point_t *ptB,
                                  const Thd_point_t *ptC,
                                  Thd_point_t *ptNormal)
{
    q16_t ux = ptB->x - ptA->x;
    q16_t uy = ptB->y - ptA->y;
    q16_t uz = ptB->z - ptA->z;
    q16_t vx = ptC->x - ptA->x;
    q16_t vy = ptC->y - ptA->y;
    q16_t vz = ptC->z - ptA->z;

    ptNormal->x = mul_q16(uy, vz) - mul_q16(uz, vy);
    ptNormal->y = mul_q16(uz, vx) - mul_q16(ux, vz);
    ptNormal->z = mul_q16(ux, vy) - mul_q16(uy, vx);

    q16_t len = sqrt_q16(mul_q16(ptNormal->x, ptNormal->x)
                       + mul_q16(ptNormal->y, ptNormal->y)
                       + mul_q16(ptNormal->z, ptNormal->z));

    if (0 == len) {
        return false;
    }

    ptNormal->x = div_q16(ptNormal->x, len);
    ptNormal->y = div_q16(ptNormal->y, len);
    ptNormal->z = div_q16(ptNormal->z, len);

    return true;
}

static q16_t __ThD_sim_model_face_brightness(
                                        const thd_model_instance_t *ptModel,
                                        uint16_t hwFaceIndex,
                                        const Thd_point_t *ptModelLight)
{
    if (NULL != ptModel->pi16FaceNormalsQ14) {
        return __ThD_sim_q14_normal_brightness(
                    ptModel->pi16FaceNormalsQ14[hwFaceIndex], ptModelLight);
    }

    const tri_t *ptTri = &ptModel->ptTris[hwFaceIndex];
    Thd_point_t tNormal;

    if (!__ThD_sim_face_normal(&ptModel->ptVertices[ptTri->i0],
                               &ptModel->ptVertices[ptTri->i1],
                               &ptModel->ptVertices[ptTri->i2],
                               &tNormal)) {
        return THD_LIGHT_AMBIENT_Q16;
    }

    return __ThD_sim_unit_normal_brightness(
            tNormal.x, tNormal.y, tNormal.z, ptModelLight);
}

static bool __ThD_sim_face_intersects_roi( const arm_2d_location_t *ptA,
                                           const arm_2d_location_t *ptB,
                                           const arm_2d_location_t *ptC,
                                           const arm_2d_region_t *ptROI)
{
    int16_t xMin = __ThD_sim_min3_s16(ptA->iX, ptB->iX, ptC->iX);
    int16_t xMax = __ThD_sim_max3_s16(ptA->iX, ptB->iX, ptC->iX);
    int16_t yMin = __ThD_sim_min3_s16(ptA->iY, ptB->iY, ptC->iY);
    int16_t yMax = __ThD_sim_max3_s16(ptA->iY, ptB->iY, ptC->iY);

    int_fast16_t xLimit = ptROI->tLocation.iX + ptROI->tSize.iWidth;
    int_fast16_t yLimit = ptROI->tLocation.iY + ptROI->tSize.iHeight;

    return !(xMax < ptROI->tLocation.iX
          || xMin >= xLimit
          || yMax < ptROI->tLocation.iY
          || yMin >= yLimit);
}

static uint16_t __ThD_sim_pack_depth(q16_t z)
{
    if (z <= 0) {
        return 0;
    }

    uint32_t wDepth = (uint32_t)z >> THD_CFG_DEPTH_BUFFER_SHIFT;

    if (wDepth >= THD_Z_BUFFER_FAR) {
        return THD_Z_BUFFER_FAR - 1;
    }

    return (uint16_t)wDepth;
}

static uint16_t *__ThD_sim_clear_z_buffer(int16_t iWidth, int16_t iHeight)
{
    if ((iWidth <= 0) || (iHeight <= 0)) {
        return NULL;
    }

    uint32_t wPixelCount = (uint32_t)iWidth * (uint32_t)iHeight;

    if (wPixelCount > THD_CFG_Z_BUFFER_PIXEL_COUNT) {
        return NULL;
    }

    memset(s_hwZBuffer, 0xFF, wPixelCount * sizeof(s_hwZBuffer[0]));
    return s_hwZBuffer;
}

static void fill_triangle_rgb565(uint8_t *pchBuffer,
                                 int strideByte,
                                 int width,
                                 int height,
                                 arm_2d_location_t pt0,
                                 arm_2d_location_t pt1,
                                 arm_2d_location_t pt2,
                                 arm_2d_location_t pROI,
                                 uint16_t color)
{
    if ((width <= 0) || (height <= 0)) {
        return;
    }

    int16_t x0 = pt0.iX - pROI.iX;
    int16_t y0 = pt0.iY - pROI.iY;
    int16_t x1 = pt1.iX - pROI.iX;
    int16_t y1 = pt1.iY - pROI.iY;
    int16_t x2 = pt2.iX - pROI.iX;
    int16_t y2 = pt2.iY - pROI.iY;

    int16_t xMin = __ThD_sim_min3_s16(x0, x1, x2);
    int16_t xMax = __ThD_sim_max3_s16(x0, x1, x2);
    int16_t yMin = __ThD_sim_min3_s16(y0, y1, y2);
    int16_t yMax = __ThD_sim_max3_s16(y0, y1, y2);

    if ((xMax < 0) || (yMax < 0) || (xMin >= width) || (yMin >= height)) {
        return;
    }

    if (xMin < 0) {
        xMin = 0;
    }
    if (yMin < 0) {
        yMin = 0;
    }
    if (xMax >= width) {
        xMax = width - 1;
    }
    if (yMax >= height) {
        yMax = height - 1;
    }

    int32_t w0Row = __ThD_sim_edge(x1, y1, x2, y2, xMin, yMin);
    int32_t w1Row = __ThD_sim_edge(x2, y2, x0, y0, xMin, yMin);
    int32_t w2Row = __ThD_sim_edge(x0, y0, x1, y1, xMin, yMin);

    int32_t w0StepX = y1 - y2;
    int32_t w1StepX = y2 - y0;
    int32_t w2StepX = y0 - y1;

    int32_t w0StepY = x2 - x1;
    int32_t w1StepY = x0 - x2;
    int32_t w2StepY = x1 - x0;

    for (int16_t y = yMin; y <= yMax; y++) {
        int32_t w0 = w0Row;
        int32_t w1 = w1Row;
        int32_t w2 = w2Row;
        uint8_t *row = pchBuffer + y * strideByte + xMin * 2;

       for (int16_t x = xMin; x <= xMax; x++) {
            if ((w0 | w1 | w2) >= 0) {
                *((uint16_t *)row) = color;
            }
            row += 2;
            w0 += w0StepX;
            w1 += w1StepX;
            w2 += w2StepX;
        }

        w0Row += w0StepY;
        w1Row += w1StepY;
        w2Row += w2StepY;
    }
}

static void fill_triangle_z_rgb565(uint8_t *pchBuffer,
                                   int strideByte,
                                   int width,
                                   int height,
                                   arm_2d_location_t pt0,
                                   arm_2d_location_t pt1,
                                   arm_2d_location_t pt2,
                                   q16_t z0,
                                   q16_t z1,
                                   q16_t z2,
                                   arm_2d_location_t pROI,
                                   uint16_t *phwZBuffer,
                                   uint16_t color)
{
    if (NULL == phwZBuffer) {
        fill_triangle_rgb565(   pchBuffer,
                                strideByte,
                                width,
                                height,
                                pt0,
                                pt1,
                                pt2,
                                pROI,
                                color);
        return;
    }

    if ((width <= 0) || (height <= 0)) {
        return;
    }

    int16_t x0 = pt0.iX - pROI.iX;
    int16_t y0 = pt0.iY - pROI.iY;
    int16_t x1 = pt1.iX - pROI.iX;
    int16_t y1 = pt1.iY - pROI.iY;
    int16_t x2 = pt2.iX - pROI.iX;
    int16_t y2 = pt2.iY - pROI.iY;

    int16_t xMin = __ThD_sim_min3_s16(x0, x1, x2);
    int16_t xMax = __ThD_sim_max3_s16(x0, x1, x2);
    int16_t yMin = __ThD_sim_min3_s16(y0, y1, y2);
    int16_t yMax = __ThD_sim_max3_s16(y0, y1, y2);

    if ((xMax < 0) || (yMax < 0) || (xMin >= width) || (yMin >= height)) {
        return;
    }

    if (xMin < 0) {
        xMin = 0;
    }
    if (yMin < 0) {
        yMin = 0;
    }
    if (xMax >= width) {
        xMax = width - 1;
    }
    if (yMax >= height) {
        yMax = height - 1;
    }

    int32_t area = __ThD_sim_edge(x0, y0, x1, y1, x2, y2);

    if (area <= 0) {
        return;
    }

    int32_t w0Row = __ThD_sim_edge(x1, y1, x2, y2, xMin, yMin);
    int32_t w1Row = __ThD_sim_edge(x2, y2, x0, y0, xMin, yMin);
    int32_t w2Row = __ThD_sim_edge(x0, y0, x1, y1, xMin, yMin);

    int32_t w0StepX = y1 - y2;
    int32_t w1StepX = y2 - y0;
    int32_t w2StepX = y0 - y1;

    int32_t w0StepY = x2 - x1;
    int32_t w1StepY = x0 - x2;
    int32_t w2StepY = x1 - x0;

    int64_t lInverseAreaQ30 = ((int64_t)1 << 30) / area;
    int64_t lZRowNumerator = (int64_t)w0Row * z0
                           + (int64_t)w1Row * z1
                           + (int64_t)w2Row * z2;
    int64_t lZStepXNumerator = (int64_t)w0StepX * z0
                             + (int64_t)w1StepX * z1
                             + (int64_t)w2StepX * z2;
    int64_t lZStepYNumerator = (int64_t)w0StepY * z0
                             + (int64_t)w1StepY * z1
                             + (int64_t)w2StepY * z2;
    q16_t zRow = (q16_t)((lZRowNumerator * lInverseAreaQ30) >> 30);
    q16_t zStepX = (q16_t)((lZStepXNumerator * lInverseAreaQ30) >> 30);
    q16_t zStepY = (q16_t)((lZStepYNumerator * lInverseAreaQ30) >> 30);

    for (int16_t y = yMin; y <= yMax; y++) {
        int32_t w0 = w0Row;
        int32_t w1 = w1Row;
        int32_t w2 = w2Row;
        q16_t z = zRow;
        uint8_t *row = pchBuffer + y * strideByte + xMin * 2;
        uint16_t *phwZ = phwZBuffer + (uint32_t)y * (uint32_t)width + xMin;

        for (int16_t x = xMin; x <= xMax; x++) {
            if ((w0 | w1 | w2) >= 0) {
                uint16_t hwDepth = __ThD_sim_pack_depth(z);

                if (hwDepth < *phwZ) {
                    *phwZ = hwDepth;
                    *((uint16_t *)row) = color;
                }
            }
            row += 2;
            phwZ++;
            z += zStepX;
            w0 += w0StepX;
            w1 += w1StepX;
            w2 += w2StepX;
        }

        zRow += zStepY;
        w0Row += w0StepY;
        w1Row += w1StepY;
        w2Row += w2StepY;
    }
}

#if THD_CFG_ENABLE_SMOOTH_SHADING
static void fill_triangle_smooth_rgb565(uint8_t *pchBuffer,
                                        int strideByte,
                                        int width,
                                        int height,
                                        arm_2d_location_t pt0,
                                        arm_2d_location_t pt1,
                                        arm_2d_location_t pt2,
                                        arm_2d_location_t pROI,
                                        uint16_t color,
                                        q16_t q16Brightness0,
                                        q16_t q16Brightness1,
                                        q16_t q16Brightness2)
{
    if ((width <= 0) || (height <= 0)) {
        return;
    }

    int16_t x0 = pt0.iX - pROI.iX;
    int16_t y0 = pt0.iY - pROI.iY;
    int16_t x1 = pt1.iX - pROI.iX;
    int16_t y1 = pt1.iY - pROI.iY;
    int16_t x2 = pt2.iX - pROI.iX;
    int16_t y2 = pt2.iY - pROI.iY;

    int16_t xMin = __ThD_sim_min3_s16(x0, x1, x2);
    int16_t xMax = __ThD_sim_max3_s16(x0, x1, x2);
    int16_t yMin = __ThD_sim_min3_s16(y0, y1, y2);
    int16_t yMax = __ThD_sim_max3_s16(y0, y1, y2);

    if ((xMax < 0) || (yMax < 0) || (xMin >= width) || (yMin >= height)) {
        return;
    }

    if (xMin < 0) {
        xMin = 0;
    }
    if (yMin < 0) {
        yMin = 0;
    }
    if (xMax >= width) {
        xMax = width - 1;
    }
    if (yMax >= height) {
        yMax = height - 1;
    }

    int32_t area = __ThD_sim_edge(x0, y0, x1, y1, x2, y2);

    if (0 == area) {
        return;
    }

    int32_t w0Row = __ThD_sim_edge(x1, y1, x2, y2, xMin, yMin);
    int32_t w1Row = __ThD_sim_edge(x2, y2, x0, y0, xMin, yMin);
    int32_t w2Row = __ThD_sim_edge(x0, y0, x1, y1, xMin, yMin);

    int32_t w0StepX = y1 - y2;
    int32_t w1StepX = y2 - y0;
    int32_t w2StepX = y0 - y1;

    int32_t w0StepY = x2 - x1;
    int32_t w1StepY = x0 - x2;
    int32_t w2StepY = x1 - x0;

    for (int16_t y = yMin; y <= yMax; y++) {
        int32_t w0 = w0Row;
        int32_t w1 = w1Row;
        int32_t w2 = w2Row;
        uint8_t *row = pchBuffer + y * strideByte + xMin * 2;

        for (int16_t x = xMin; x <= xMax; x++) {
            if ((w0 | w1 | w2) >= 0) {
                int64_t iBrightness = (int64_t)w0 * q16Brightness0
                                    + (int64_t)w1 * q16Brightness1
                                    + (int64_t)w2 * q16Brightness2;
                q16_t q16Brightness = (q16_t)(iBrightness / area);
                *((uint16_t *)row) = __ThD_sim_scale_rgb565(color,
                                                            q16Brightness);
            }
            row += 2;
            w0 += w0StepX;
            w1 += w1StepX;
            w2 += w2StepX;
        }

        w0Row += w0StepY;
        w1Row += w1StepY;
        w2Row += w2StepY;
    }
}
#endif

#if THD_CFG_ENABLE_SMOOTH_SHADING
static void __ThD_sim_prepare_vertex_lighting(void)
{
    for (uint16_t i = 0; i < s_hwProjectedVertexCount; i++) {
        s_tProjectedVertices[i].tNormal.x = 0;
        s_tProjectedVertices[i].tNormal.y = 0;
        s_tProjectedVertices[i].tNormal.z = 0;
        s_tProjectedVertices[i].q16Brightness = THD_LIGHT_AMBIENT_Q16;
    }

    for (uint16_t hwModelIndex = 0;
         hwModelIndex < THD_MODEL_INSTANCE_COUNT;
         hwModelIndex++) {
        const thd_model_instance_t *ptModel = &s_tModelInstances[hwModelIndex];
        uint16_t hwBase = s_hwModelVertexBase[hwModelIndex];
        uint16_t hwVertexCount = s_hwModelVertexCount[hwModelIndex];

        for (uint16_t i = 0; i < ptModel->hwTriCount; i++) {
            const tri_t *ptTri = &ptModel->ptTris[i];

            if ((ptTri->i0 >= hwVertexCount)
             || (ptTri->i1 >= hwVertexCount)
             || (ptTri->i2 >= hwVertexCount)) {
                continue;
            }

            uint16_t hwI0 = hwBase + ptTri->i0;
            uint16_t hwI1 = hwBase + ptTri->i1;
            uint16_t hwI2 = hwBase + ptTri->i2;
            Thd_point_t tNormal;

            if (!__ThD_sim_face_normal(
                    &s_tProjectedVertices[hwI0].tCamera,
                    &s_tProjectedVertices[hwI1].tCamera,
                    &s_tProjectedVertices[hwI2].tCamera,
                    &tNormal)) {
                continue;
            }

            s_tProjectedVertices[hwI0].tNormal.x += tNormal.x;
            s_tProjectedVertices[hwI0].tNormal.y += tNormal.y;
            s_tProjectedVertices[hwI0].tNormal.z += tNormal.z;

            s_tProjectedVertices[hwI1].tNormal.x += tNormal.x;
            s_tProjectedVertices[hwI1].tNormal.y += tNormal.y;
            s_tProjectedVertices[hwI1].tNormal.z += tNormal.z;

            s_tProjectedVertices[hwI2].tNormal.x += tNormal.x;
            s_tProjectedVertices[hwI2].tNormal.y += tNormal.y;
            s_tProjectedVertices[hwI2].tNormal.z += tNormal.z;
        }
    }

    for (uint16_t i = 0; i < s_hwProjectedVertexCount; i++) {
        const Thd_point_t *ptNormal = &s_tProjectedVertices[i].tNormal;
        s_tProjectedVertices[i].q16Brightness =
            __ThD_sim_normal_brightness(ptNormal->x,
                                        ptNormal->y,
                                        ptNormal->z);
    }
}
#endif

ARM_NONNULL(1)
static
void __ThD_sim_prepare_frame(ThD_sim_t *ptThis)
{
    uint16_t hwProjectedVertexCount = 0;

    for (uint16_t hwModelIndex = 0;
         hwModelIndex < THD_MODEL_INSTANCE_COUNT;
         hwModelIndex++) {
        const thd_model_instance_t *ptModel = &s_tModelInstances[hwModelIndex];

        s_hwModelVertexBase[hwModelIndex] = hwProjectedVertexCount;
        s_hwModelVertexCount[hwModelIndex] = 0;

        if ((NULL == ptModel->ptVertices) || (NULL == ptModel->ptTris)) {
            continue;
        }

        q31_t q31ClockAngle = thd_clock_hands_get_angle(
            &this.tClockHands,
            ptModel->tClockHand);
        mat3_t tAnimatedBaseMatrix = __ThD_sim_rotate_matrix_z(
            &s_tModelBaseMatrices[hwModelIndex],
            q31ClockAngle);
        mat3_t tModelRotationMatrix = __ThD_sim_multiply_matrix(
            &this.mat3,
            &tAnimatedBaseMatrix);
        mat3_t tModelMatrix = tModelRotationMatrix;
        for (uint_fast8_t y = 0; y < 3; y++) {
            for (uint_fast8_t x = 0; x < 3; x++) {
                tModelMatrix.m[y][x] =
                    mul_q16(tModelMatrix.m[y][x], ptModel->q16Scale);
            }
        }
        s_tModelLight[hwModelIndex] =
            __ThD_sim_transform_light_to_model(&tModelRotationMatrix);

        for (uint16_t i = 0;
             (i < ptModel->hwVertexCount)
          && (hwProjectedVertexCount < THD_MAX_PROJECTED_VERTEX_COUNT);
             i++) {
            Thd_point_t tVertex = ptModel->ptVertices[i];
            Thd_point_t tPoint = apply_rot(tVertex, &tModelMatrix);

            tPoint.x += ptModel->tOffset.x;
            tPoint.y += ptModel->tOffset.y;
            tPoint.z += ptModel->tOffset.z;

            q16_t z = tPoint.z + DEPTH;

            if (z < THD_NEAR_PLANE_Q16) {
                z = THD_NEAR_PLANE_Q16;
            }

            q16_t q16InverseZ = div_q16(Q16_ONE, z);
            point_t tProjected = {
                .x = mul_q16(tPoint.x, q16InverseZ) + this.q16ParallaxX,
                .y = mul_q16(tPoint.y, q16InverseZ) + this.q16ParallaxY,
            };

            to_screen(tProjected,
                      &s_tProjectedVertices[hwProjectedVertexCount].tLocation);
            s_tProjectedVertices[hwProjectedVertexCount].z = z;
#if THD_CFG_ENABLE_SMOOTH_SHADING
            s_tProjectedVertices[hwProjectedVertexCount].tCamera = tPoint;
#endif

            s_hwModelVertexCount[hwModelIndex]++;
            hwProjectedVertexCount++;
        }
    }

    s_hwProjectedVertexCount = hwProjectedVertexCount;

#if THD_CFG_ENABLE_SMOOTH_SHADING
    __ThD_sim_prepare_vertex_lighting();
#endif

    uint16_t hwFaceCount = 0;

    for (uint16_t hwModelIndex = 0;
         (hwModelIndex < THD_MODEL_INSTANCE_COUNT)
      && (hwFaceCount < THD_MAX_VISIBLE_FACE_COUNT);
         hwModelIndex++) {
        const thd_model_instance_t *ptModel = &s_tModelInstances[hwModelIndex];
        uint16_t hwBase = s_hwModelVertexBase[hwModelIndex];
        uint16_t hwVertexCount = s_hwModelVertexCount[hwModelIndex];

        for (uint16_t i = 0;
             (i < ptModel->hwTriCount)
          && (hwFaceCount < THD_MAX_VISIBLE_FACE_COUNT);
             i++) {
            const tri_t *ptTri = &ptModel->ptTris[i];

            if ((ptTri->i0 >= hwVertexCount)
             || (ptTri->i1 >= hwVertexCount)
             || (ptTri->i2 >= hwVertexCount)) {
                continue;
            }

            uint16_t hwI0 = hwBase + ptTri->i0;
            uint16_t hwI1 = hwBase + ptTri->i1;
            uint16_t hwI2 = hwBase + ptTri->i2;
            const arm_2d_location_t *ptA =
                &s_tProjectedVertices[hwI0].tLocation;
            const arm_2d_location_t *ptB =
                &s_tProjectedVertices[hwI1].tLocation;
            const arm_2d_location_t *ptC =
                &s_tProjectedVertices[hwI2].tLocation;

            int32_t cross =
                ((int32_t)ptB->iX - ptA->iX) * ((int32_t)ptC->iY - ptA->iY)
              - ((int32_t)ptB->iY - ptA->iY) * ((int32_t)ptC->iX - ptA->iX);

            if (cross <= 0) {
                continue;
            }

            s_tFaceOrder[hwFaceCount].hwIndex = i;
            s_tFaceOrder[hwFaceCount].hwModelIndex = hwModelIndex;
            hwFaceCount++;
        }
    }

    s_hwVisibleFaceCount = hwFaceCount;
}

//void draw_point_3d_mask(uint8_t *pchBuffer,
//                          int strideByte,
//                          int width,
//                          int height,
//                          arm_2d_tile_t    *Mask,
//                          arm_2d_location_t point,
//                          arm_2d_location_t pROI,
//                          uint16_t X,
//                          uint16_t Y)
//{
//    int16_t x0 = point.iX - pROI.iX;
//    int16_t y0 = point.iY - pROI.iY;

//    if (x0 >= 0 && x0 < width &&
//        y0 >= 0 && y0 < height) {

//        uint8_t *row = pchBuffer + y0 * strideByte;
//       *((uint16_t *)(row + x0 * 2)) = Mask->phwBuffer[X + Y * 240];
//    }
//}


void ThD_sim_show(ThD_sim_t *ptThis,
                        const arm_2d_tile_t *ptTile,
                        const arm_2d_region_t *ptRegion,
                        bool bIsNewFrame)
{
    ARM_2D_UNUSED(bIsNewFrame);

    assert(NULL!= ptThis);
    if (-1 == (intptr_t)ptTile) {
        ptTile = arm_2d_get_default_frame_buffer();
    }	
    // arm_2d_rgb565_fill_colour_with_mask(
    //    (arm_2d_tile_t *)ptTile,   
    //    ptRegion,
    //    (arm_2d_tile_t *)&ptThis->tTile,   
    //    (__arm_2d_color_t){GLCD_COLOR_RED}
    //);
	arm_2d_tile_copy_only(  &this.tTile, 
                                     ptTile, 
                                     ptRegion);
	
}


arm_2d_err_t ThD_sim_init(ThD_sim_t *ptThis,
                                ThD_sim_cfg_t *ptCFG)
{
    assert(NULL != ptThis);
    assert(NULL != ptCFG);
    memset(ptThis, 0, sizeof(ThD_sim_t));

    this.tCFG = *ptCFG;
    thd_clock_hands_init(&this.tClockHands);
    for (uint16_t i = 0; i < THD_MODEL_INSTANCE_COUNT; i++) {
        s_tModelBaseMatrices[i] = build_rot_matrix(
            s_tModelInstances[i].tInitialAngleOffset.x,
            s_tModelInstances[i].tInitialAngleOffset.y,
            s_tModelInstances[i].tInitialAngleOffset.z);
    }
    this.mat3 = build_rot_matrix(0, 0, 0);

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
            .tColourInfo.chScheme = ARM_2D_COLOUR_RGB565,
            .bBlendWithBG = true,
            .ImageIO = {
                .ptIO = this.tCFG.ImageIO.ptIO,
                .pTarget = this.tCFG.ImageIO.pTarget,
            },

            .UserDecoder = {
                .fnDecoderInit = &__ThD_sim_decoder_init,
                .fnDecode = &__ThD_sim_draw,
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
void ThD_sim_depose( ThD_sim_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_depose(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void ThD_sim_on_load( ThD_sim_t *ptThis)
{
    assert(NULL != ptThis);

    __ThD_sim_prepare_frame(ptThis);
    arm_generic_loader_on_load(&this.use_as__arm_generic_loader_t);
}

quat_t quat;
ARM_NONNULL(1)
void ThD_sim_on_frame_start( ThD_sim_t *ptThis)
{
    assert(NULL != ptThis);

    uint32_t wTimestampMs = (uint32_t)arm_2d_helper_convert_ticks_to_ms(
        arm_2d_helper_get_system_timestamp());
    thd_clock_hands_update(&this.tClockHands, wTimestampMs);

	uint32_t wSampleSeq = DATA_GY_ACC_SAMPLE_SEQ;
    if (wSampleSeq != this.wLastIMUSampleSeq) {
        this.wLastIMUSampleSeq = wSampleSeq;
        imu_holo_update_raw(&this.attitude, DATA_GY_ACC_RAW);
    }

    imu_holo_step(&this.attitude);

    /* yz/xz/xy are rotations around model-view X/Y/Z respectively. */
    this.yz_rad = -imu_holo_angle_to_q31(this.attitude.roll);
    this.xz_rad = -imu_holo_angle_to_q31(this.attitude.pitch);
    this.xy_rad = -imu_holo_angle_to_q31(this.attitude.yaw);
    this.q16ParallaxX = mul_q16(this.attitude.pitch,
                                THD_HOLO_PARALLAX_X_GAIN_Q16);
    this.q16ParallaxY = mul_q16(this.attitude.roll,
                                THD_HOLO_PARALLAX_Y_GAIN_Q16);
    mat3_t tInteractionMatrix =
        build_rot_matrix(this.yz_rad, this.xz_rad, this.xy_rad);
    this.mat3 = tInteractionMatrix;
    __ThD_sim_prepare_frame(ptThis);
	
    arm_generic_loader_on_frame_start(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void ThD_sim_on_frame_complete( ThD_sim_t *ptThis)
{
    assert(NULL != ptThis);
    arm_generic_loader_on_frame_complete(&this.use_as__arm_generic_loader_t);
}

ARM_NONNULL(1)
void ThD_sim_set_depth_fog_strength(ThD_sim_t *ptThis,
                                    q16_t q16Strength)
{
    assert(NULL != ptThis);

    this.tCFG.q16DepthFogStrength = q16Strength;
    __ThD_sim_prepare_frame(ptThis);
}

ARM_NONNULL(1)
static
arm_2d_err_t __ThD_sim_decoder_init(arm_generic_loader_t *ptObj)
{
    assert(NULL != ptObj);

    ThD_sim_t *ptThis = (ThD_sim_t *)ptObj;
    ARM_2D_UNUSED(ptThis);

    return ARM_2D_ERR_NONE;
}

ARM_NONNULL(1, 2, 3)
static
arm_2d_err_t __ThD_sim_draw(  arm_generic_loader_t *ptObj,
                                    arm_2d_region_t *ptROI,
                                    uint8_t *pchBuffer,
                                    uint32_t iTargetStrideInByte,
                                    uint_fast8_t chBitsPerPixel)
{
    assert(NULL != ptObj);
    ThD_sim_t *ptThis = (ThD_sim_t *)ptObj;
    ARM_2D_UNUSED(ptThis);
    ARM_2D_UNUSED(chBitsPerPixel);	

    uint16_t *phwZBuffer =
        __ThD_sim_clear_z_buffer(ptROI->tSize.iWidth, ptROI->tSize.iHeight);

    for(uint16_t i = 0; i < s_hwVisibleFaceCount; i++) {
        uint16_t hwModelIndex = s_tFaceOrder[i].hwModelIndex;

        if (hwModelIndex >= THD_MODEL_INSTANCE_COUNT) {
            continue;
        }

        const thd_model_instance_t *ptModel = &s_tModelInstances[hwModelIndex];
        uint16_t hwBase = s_hwModelVertexBase[hwModelIndex];
        uint16_t hwVertexCount = s_hwModelVertexCount[hwModelIndex];
        uint16_t hwFaceIndex = s_tFaceOrder[i].hwIndex;
        const tri_t *ptTri = &ptModel->ptTris[hwFaceIndex];

        if ((ptTri->i0 >= hwVertexCount)
         || (ptTri->i1 >= hwVertexCount)
         || (ptTri->i2 >= hwVertexCount)) {
            continue;
        }

        uint16_t hwI0 = hwBase + ptTri->i0;
        uint16_t hwI1 = hwBase + ptTri->i1;
        uint16_t hwI2 = hwBase + ptTri->i2;
        const arm_2d_location_t *ptA =
            &s_tProjectedVertices[hwI0].tLocation;
        const arm_2d_location_t *ptB =
            &s_tProjectedVertices[hwI1].tLocation;
        const arm_2d_location_t *ptC =
            &s_tProjectedVertices[hwI2].tLocation;

        if (!__ThD_sim_face_intersects_roi(ptA, ptB, ptC, ptROI)) {
            continue;
        }

        uint16_t hwColour = ptModel->hwColour;
#if !THD_CFG_ENABLE_SMOOTH_SHADING && THD_CFG_ENABLE_FLAT_SHADING
        q16_t q16Brightness = __ThD_sim_model_face_brightness(
                                        ptModel,
                                        hwFaceIndex,
                                        &s_tModelLight[hwModelIndex]);
        hwColour = __ThD_sim_scale_rgb565(hwColour, q16Brightness);
#endif
#if THD_CFG_ENABLE_DEPTH_FOG
        q16_t z = (s_tProjectedVertices[hwI0].z
                 + s_tProjectedVertices[hwI1].z
                 + s_tProjectedVertices[hwI2].z) / 3;
        q16_t q16FogFactor = __ThD_sim_depth_fog_factor(
                                        z,
                                        this.tCFG.q16DepthFogStrength);
        hwColour = __ThD_sim_lerp_rgb565(hwColour,
                                         THD_DEPTH_FOG_COLOUR,
                                         q16FogFactor);
#endif

#if THD_CFG_ENABLE_FILL || (THD_CFG_ENABLE_WIREFRAME && THD_CFG_ENABLE_OCCLUSION)
#if THD_CFG_ENABLE_SMOOTH_SHADING
        fill_triangle_smooth_rgb565(
            pchBuffer,
            iTargetStrideInByte,
            ptROI->tSize.iWidth,
            ptROI->tSize.iHeight,
            *ptA,
            *ptB,
            *ptC,
            ptROI->tLocation,
            hwColour,
            s_tProjectedVertices[hwI0].q16Brightness,
            s_tProjectedVertices[hwI1].q16Brightness,
            s_tProjectedVertices[hwI2].q16Brightness
        );
#else
        fill_triangle_z_rgb565(
            pchBuffer,
            iTargetStrideInByte,
            ptROI->tSize.iWidth,
            ptROI->tSize.iHeight,
            *ptA,
            *ptB,
            *ptC,
            s_tProjectedVertices[hwI0].z,
            s_tProjectedVertices[hwI1].z,
            s_tProjectedVertices[hwI2].z,
            ptROI->tLocation,
            phwZBuffer,
            hwColour
        );
#endif
#endif

#if THD_CFG_ENABLE_WIREFRAME
        draw_line_fast_rgb565(
            pchBuffer,
            iTargetStrideInByte,
            ptROI->tSize.iWidth,
            ptROI->tSize.iHeight,
            *ptA,
            *ptB,
            ptROI->tLocation,
            ptModel->hwWireframeColour
        );

        draw_line_fast_rgb565(
            pchBuffer,
            iTargetStrideInByte,
            ptROI->tSize.iWidth,
            ptROI->tSize.iHeight,
            *ptB,
            *ptC,
            ptROI->tLocation,
            ptModel->hwWireframeColour
        );

        draw_line_fast_rgb565(
            pchBuffer,
            iTargetStrideInByte,
            ptROI->tSize.iWidth,
            ptROI->tSize.iHeight,
            *ptC,
            *ptA,
            ptROI->tLocation,
            ptModel->hwWireframeColour
        );
#endif
    }

    return ARM_2D_ERR_NONE;
}




#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#endif
