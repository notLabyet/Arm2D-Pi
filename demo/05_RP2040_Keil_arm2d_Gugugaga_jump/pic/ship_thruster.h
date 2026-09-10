#ifndef __SHIP_THRUSTER_H__
#define __SHIP_THRUSTER_H__

#include "arm_2d.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHIP_THRUSTER_FRAME_WIDTH       16
#define SHIP_THRUSTER_FRAME_HEIGHT      23
#define SHIP_THRUSTER_FRAME_COUNT       5
#define SHIP_THRUSTER_ENGINE_COUNT      2
#define SHIP_THRUSTER_ENGINE_SPACING    10
#define SHIP_THRUSTER_IDLE_ROW          0
#define SHIP_THRUSTER_BOOST_ROW         1

extern const arm_2d_tile_t c_tileShipThrusterRGB565;
extern const arm_2d_tile_t c_tileShipThrusterMask;

#ifdef __cplusplus
}
#endif

#endif
