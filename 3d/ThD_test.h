#ifndef __3D_TEST_H__
#define __3D_TEST_H__

#include "stdint.h"
#include "__arm_2d_math.h"
#include "arm_2d_helper.h"

#define DEG2RAD_Q16(x) ((q16_t)((x) * 1144)) // 

#define Q16_CANVA_HEIGHT    reinterpret_q16_s16(CANVA_HEIGHT)
#define Q16_HALF            reinterpret_q16_f32(0.5f)
#define Q16_ZERO_ZERO_ONE   reinterpret_q16_f32(0.01f)
#define Q16_ONE             reinterpret_q16_s16(1)
#define Q16_ZERO_ONE        reinterpret_q16_f32(0.1)
#define Q16_ZERO_FIVE       reinterpret_q16_f32(0.05)
#define Q16_ONE_FIVE        reinterpret_q16_f32(1.4)
#define Q16_TWO             reinterpret_q16_s16(2)

#define PI_Q16              reinterpret_q16_f32(3.1415f)

#ifndef  CANVA_WIDTH
#define  CANVA_WIDTH   240
#endif
#ifndef  CANVA_HEIGHT
#define  CANVA_HEIGHT  240
#endif

#ifndef  DEPTH
#define  DEPTH   Q16_ONE_FIVE
#endif

#define  Q16_ONE reinterpret_q16_f32(1.0)

#define Q16(x) ((q16_t)((x) * 65536))
#define Q31(x) ((q31_t)((x) * 2147483648.0f))

#define DAVID_VERTEX_COUNT 426u
#define DAVID_TRI_COUNT    848u

typedef struct {
    q16_t roll;   // X
    q16_t pitch;  // Y
    q16_t yaw;    // Z

    q16_t target_roll;
    q16_t target_pitch;
    q16_t target_yaw;

    q16_t gravity_x;
    q16_t gravity_y;
    q16_t gravity_z;
    q16_t gravity_magnitude;

    int32_t neutral_roll_sum;
    int32_t neutral_pitch_sum;
    int32_t gyro_z_bias_sum;
    q16_t neutral_roll;
    q16_t neutral_pitch;
    q16_t gyro_z_bias;

    uint16_t calibration_samples;
    uint8_t initialized;
} attitude_t;

typedef struct {
    q16_t w, x, y, z;
} quat_t;

typedef struct {
    uint16_t i0, i1, i2;
} tri_t;

typedef struct
{
   q16_t x;
   q16_t y;
}point_t;

typedef struct
{
   q16_t x;
   q16_t y;
   q16_t z;
}Thd_point_t;

typedef struct {
    q16_t m[3][3];
} mat3_t;

typedef struct
{
	 arm_2d_location_t locateA;
	 arm_2d_location_t locateB;
	 arm_2d_location_t locateC;	
	 
	 bool bIsShow;
}Tri_angle_t;

mat3_t build_rot_matrix(q31_t x_rad, q31_t y_rad, q31_t z_rad);
q16_t sqrt_q16(q16_t x);
Thd_point_t apply_rot(Thd_point_t p, mat3_t *R);
point_t projection(Thd_point_t p, mat3_t *R);
void  to_screen(point_t pt, arm_2d_location_t *locate);

void imu_update_euler(attitude_t *att,
                      q16_t ax, q16_t ay, q16_t az,   
                      q16_t gx, q16_t gy, q16_t gz,   
                      q16_t dt);  
void imu_holo_update_raw(attitude_t *att, const int16_t raw_data[6]);
void imu_holo_step(attitude_t *att);
q31_t imu_holo_angle_to_q31(q16_t radians);

void mahony_update(quat_t *q,
                   q16_t ax, q16_t ay, q16_t az,
                   q16_t gx, q16_t gy, q16_t gz,
                   q16_t dt);
mat3_t quat_to_matrix(quat_t *q);


extern Thd_point_t cube_vertices[];
extern tri_t cube_tris[];

#endif
