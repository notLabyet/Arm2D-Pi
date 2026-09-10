/*
 * Copyright (c) 2009-2024 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ARM_2D_SCENE_AIR_COMBAT_H__
#define __ARM_2D_SCENE_AIR_COMBAT_H__

/*============================ INCLUDES ======================================*/

#include "arm_2d.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#include "arm_2d_helper_scene.h"

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

#define AIR_COMBAT_MAX_ENEMIES                6
#define AIR_COMBAT_MAX_PROJECTILES           20

#ifdef __USER_SCENE_AIR_COMBAT_IMPLEMENT__
#   undef __USER_SCENE_AIR_COMBAT_IMPLEMENT__
#   define __ARM_2D_IMPL__
#endif
#include "arm_2d_utils.h"

/*============================ MACROFIED FUNCTIONS ===========================*/

#define arm_2d_scene_air_combat_init(__DISP_ADAPTER_PTR, ...)                 \
            __arm_2d_scene_air_combat_init(                                  \
                (__DISP_ADAPTER_PTR),                                        \
                (NULL, ##__VA_ARGS__))

/*============================ TYPES =========================================*/

typedef enum air_combat_game_state_t {
    AIR_COMBAT_GAME_STATE_INIT = 0,
    AIR_COMBAT_GAME_STATE_RUNNING,
    AIR_COMBAT_GAME_STATE_PAUSED,
    AIR_COMBAT_GAME_STATE_GAME_OVER,
    AIR_COMBAT_GAME_STATE_COUNT,
} air_combat_game_state_t;

typedef enum air_combat_enemy_state_t {
    AIR_COMBAT_ENEMY_STATE_INACTIVE = 0,
    AIR_COMBAT_ENEMY_STATE_ENTERING,
    AIR_COMBAT_ENEMY_STATE_FORMATION,
    AIR_COMBAT_ENEMY_STATE_DIVING,
    AIR_COMBAT_ENEMY_STATE_EXPLODING,
} air_combat_enemy_state_t;

typedef enum air_combat_enemy_type_t {
    AIR_COMBAT_ENEMY_TYPE_FIGHTER = 0,
    AIR_COMBAT_ENEMY_TYPE_LASER,
} air_combat_enemy_type_t;

typedef enum air_combat_weapon_type_t {
    AIR_COMBAT_WEAPON_TYPE_BURST_GUN = 0,
    AIR_COMBAT_WEAPON_TYPE_LASER,
} air_combat_weapon_type_t;

typedef enum air_combat_weapon_state_t {
    AIR_COMBAT_WEAPON_STATE_COOLDOWN = 0,
    AIR_COMBAT_WEAPON_STATE_AIMING,
    AIR_COMBAT_WEAPON_STATE_LOCKED,
    AIR_COMBAT_WEAPON_STATE_FIRING,
} air_combat_weapon_state_t;

typedef enum air_combat_ammo_shape_t {
    AIR_COMBAT_AMMO_SHAPE_RECTANGLE = 0,
    AIR_COMBAT_AMMO_SHAPE_DIAMOND,
} air_combat_ammo_shape_t;

typedef enum air_combat_projectile_state_t {
    AIR_COMBAT_PROJECTILE_STATE_INACTIVE = 0,
    AIR_COMBAT_PROJECTILE_STATE_FLYING,
    AIR_COMBAT_PROJECTILE_STATE_IMPACT,
} air_combat_projectile_state_t;

typedef enum air_combat_projectile_owner_t {
    AIR_COMBAT_PROJECTILE_OWNER_PLAYER = 0,
    AIR_COMBAT_PROJECTILE_OWNER_ENEMY,
} air_combat_projectile_owner_t;

typedef struct air_combat_ammo_t {
    air_combat_ammo_shape_t tShape;
    uint8_t u8Width;
    uint8_t u8Height;
    uint16_t hwColour;
} air_combat_ammo_t;

typedef struct air_combat_projectile_weapon_config_t {
    air_combat_ammo_t tAmmo;
    uint8_t u8MountCount;
    uint8_t u8SpreadAngleDeg;
    uint8_t u8ProjectileSpeed;
    uint8_t u8BurstShots;
    uint8_t u8BurstIntervalTicks;
    uint16_t hwCooldownTicks;
} air_combat_projectile_weapon_config_t;

typedef struct air_combat_laser_weapon_config_t {
    uint8_t u8AimTicks;
    uint8_t u8LockDelayTicks;
    uint8_t u8FireTicks;
    uint8_t u8ScanSpread;
    uint16_t hwCooldownTicks;
} air_combat_laser_weapon_config_t;

typedef struct air_combat_weapon_t {
    arm_2d_location_t tTarget;
    air_combat_weapon_type_t tType;
    air_combat_weapon_state_t tState;
    union {
        air_combat_projectile_weapon_config_t tProjectile;
        air_combat_laser_weapon_config_t tLaser;
    } tConfig;
    uint8_t u8ShotsRemaining;
    uint16_t hwPhaseTicks;
    uint16_t hwCooldownTicks;
} air_combat_weapon_t;

typedef struct air_combat_enemy_t {
    arm_2d_location_t tLocation;
    arm_2d_size_t tSize;
    air_combat_enemy_state_t tState;
    air_combat_enemy_type_t tType;
    air_combat_weapon_t tWeapon;
    int8_t i8VelocityX;
    uint8_t u8HitPoints;
    uint8_t u8Slot;
    uint16_t hwStateTicks;
} air_combat_enemy_t;

typedef struct air_combat_projectile_t {
    arm_2d_location_t tLocation;
    air_combat_ammo_t tAmmo;
    air_combat_projectile_state_t tState;
    air_combat_projectile_owner_t tOwner;
    int8_t i8VelocityX;
    int8_t i8VelocityY;
    uint8_t u8StateTicks;
} air_combat_projectile_t;

typedef struct user_scene_air_combat_t user_scene_air_combat_t;

struct user_scene_air_combat_t {
    implement(arm_2d_scene_t);                                                //!< derived from arm_2d_scene_t

ARM_PRIVATE(
    bool bUserAllocated;
    air_combat_game_state_t tGameState;
    int8_t i8RollFrame;
    int64_t lRollFrameTimestamp;
    arm_2d_tile_t tFighterTile;
    arm_2d_tile_t tFighterMask;
    uint8_t u8ThrusterFrame;
    int64_t lThrusterTimestamp;
    bool bThrusterBoost;
    arm_2d_tile_t tThrusterTile;
    arm_2d_tile_t tThrusterMask;
    arm_2d_region_t tPlayfield;
    arm_2d_location_t tPlayerStartLocation;
    arm_2d_location_t tPlayerLocation;
    int64_t lLogicTimestamp;
    uint32_t wScore;
    uint16_t hwGameTicks;
    uint16_t hwSpawnCooldown;
    uint16_t hwGameOverTicks;
    uint8_t u8Lives;
    uint8_t u8Wave;
    uint8_t u8NextEnemySlot;
    uint8_t u8PlayerShotCooldown;
    uint8_t u8PlayerInvulnerableTicks;
    air_combat_enemy_t tEnemies[AIR_COMBAT_MAX_ENEMIES];
    air_combat_projectile_t tProjectiles[AIR_COMBAT_MAX_PROJECTILES];
)
};

/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/

ARM_NONNULL(1)
extern
user_scene_air_combat_t *__arm_2d_scene_air_combat_init(
                                        arm_2d_scene_player_t *ptDispAdapter,
                                        user_scene_air_combat_t *ptScene);

ARM_NONNULL(1)
extern
void arm_2d_scene_air_combat_set_state(
                                        user_scene_air_combat_t *ptScene,
                                        air_combat_game_state_t tState);

ARM_NONNULL(1)
extern
air_combat_game_state_t arm_2d_scene_air_combat_get_state(
                                        const user_scene_air_combat_t *ptScene);

ARM_NONNULL(1)
extern
void arm_2d_scene_air_combat_restart(user_scene_air_combat_t *ptScene);

#if defined(__clang__)
#   pragma clang diagnostic pop
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic pop
#endif

#ifdef   __cplusplus
}
#endif

#endif

#endif
