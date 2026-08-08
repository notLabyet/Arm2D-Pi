#ifndef __FIRE_SIM_H__
#define __FIRE_SIM_H__
#include "stdint.h"
#include "__arm_2d_math.h"

// 
#define MAX_SWIRLS 60

 #define max(a, b)  ((q16_t)((int32_t)(a) > (int32_t)(b) ? (a) : (b)))
 #define min(a, b)  ((q16_t)((int32_t)(a) < (int32_t)(b) ? (a) : (b)))
 
 #define Q16_ONE            ((q16_t)0x00010000)
 #define Q16_TWO            ((q16_t)0x00020000)
 #define Q16_HALF           ((q16_t)0x00008000)
 #define Q16_QUATER         ((q16_t)0x00004000)

 #define Q16_ZERO           ((q16_t)0)
 #define Q16_ZERO_ONE       ((q16_t)6554)
 #define Q16_ZERO_TWO       ((q16_t)13107)
 #define Q16_ZERO_THREE     ((q16_t)19661)
 #define Q16_ZERO_FOUR      ((q16_t)26214)
 #define Q16_ZERO_SIX       ((q16_t)39322)
 #define Q16_ZERO_SEVEN     ((q16_t)45875)
 #define Q16_ZERO_EIGHT     ((q16_t)52429)
 #define Q16_ZERO_ZERO_TWO  ((q16_t)1311)
 #define Q16_ZERO_ZERO_FIVE ((q16_t)3277)
 #define Q16_FIVE           ((q16_t)0x00050000)
// 
enum {
	U_FIELD = 0,    // X
	V_FIELD,        // Y
	T_FIELD,        // 
};
// 
typedef struct {
	int16_t numX;           // X
	int16_t numY;           // Y
	int16_t numCells;       // 
	q16_t h;            // 
	q16_t hInv;         //
	q16_t hHalf;        //
	q16_t maxX;         //
	q16_t maxY;         //
	
	q16_t *u;           // X
	q16_t *v;           // Y
	q16_t *newU;        // X
	q16_t *newV;        // Y
	uint8_t *s;         //
	q16_t *t;           // 
	q16_t *newT;        // 
	
	int16_t   numSwirls;              // 
	q16_t swirlGlobalTime;       // 
	q16_t swirlX[MAX_SWIRLS];   // X
	q16_t swirlY[MAX_SWIRLS];   // Y
	q16_t swirlOmega[MAX_SWIRLS];// 
	q16_t swirlRadius[MAX_SWIRLS];// 
	q16_t swirlTime[MAX_SWIRLS]; // 
} Fluid;
// 
typedef struct {
	q16_t   gravity;           // 
	q16_t   dt;                // 
	int16_t numIters;            // 
	int16_t frameNr;             // 
	q16_t   obstacleX;         // X
	q16_t   obstacleY;         // Y
	q16_t   obstacleRadius;    // 
	int16_t burningObstacle;     // 
	int16_t burningFloor;        // 
	int16_t paused;              // 
	int16_t showObstacle;        // 
	int16_t showSwirls;          // 
	q16_t   swirlProbability;  // 
	q16_t   swirlMaxRadius;    // 
	Fluid   *fluid;            // 
    q16_t   liftX;
    q16_t   liftY;
    q16_t   motionAccelerationX;
    q16_t   motionAccelerationY;
    q16_t   motionDisturbance;
    int16_t close;
} Scene;
extern Scene scene;  // 
extern int fire_init(void) ;
extern uint16_t getFireColor_RGB565_Q16(q16_t val);
extern void fire_sim_update(void);
extern void fire_sim_set_screen_motion_q15(int16_t gravity_x_q15,
                                           int16_t gravity_y_q15,
                                           int16_t acceleration_x_q15,
                                           int16_t acceleration_y_q15,
                                           uint16_t disturbance_q15);
extern uint16_t getFireColor_RGB565_Q12_gpt_version(float val);
extern uint16_t getFireColor_BlueGas_RGB565(float val);
extern uint16_t getFireColor_LUT(float val);
extern uint16_t getFireColor_RGB565_Fireplace(float val);
extern uint16_t getFireColor_Fireplace_Smoke(float val);
extern uint16_t getFireColor_RGB565_check_list(q16_t val);
#endif
