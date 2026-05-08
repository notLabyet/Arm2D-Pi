/*
 * drv_paj7620.h
 * C language driver for PAJ7620 gesture sensor
 *
 * Copyright (c) 2015 seeed technology inc.
 * Modified by OpenClaw for C compatibility, 2026
 *
 * The MIT License (MIT)
 */
#ifndef __DRV_PAJ7620_H__
#define __DRV_PAJ7620_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIT(x)  (1 << (x))

// REGISTER DESCRIPTION
#define PAJ7620_VAL(val, maskbit)        ((val) << (maskbit))
#define PAJ7620_ADDR_BASE                0x00

// REGISTER BANK SELECT
#define PAJ7620_REGITER_BANK_SEL        (PAJ7620_ADDR_BASE + 0xEF)    //W

// DEVICE ID
#define PAJ7620_ID  0x73

// REGISTER BANK 0
#define PAJ7620_ADDR_SUSPEND_CMD        (PAJ7620_ADDR_BASE + 0x3)    //W
#define PAJ7620_ADDR_GES_PS_DET_MASK_0        (PAJ7620_ADDR_BASE + 0x41)    //RW
#define PAJ7620_ADDR_GES_PS_DET_MASK_1        (PAJ7620_ADDR_BASE + 0x42)    //RW
#define PAJ7620_ADDR_GES_PS_DET_FLAG_0        (PAJ7620_ADDR_BASE + 0x43)    //R
#define PAJ7620_ADDR_GES_PS_DET_FLAG_1        (PAJ7620_ADDR_BASE + 0x44)    //R
#define PAJ7620_ADDR_STATE_INDICATOR    (PAJ7620_ADDR_BASE + 0x45)    //R
#define PAJ7620_ADDR_PS_HIGH_THRESHOLD    (PAJ7620_ADDR_BASE + 0x69)    //RW
#define PAJ7620_ADDR_PS_LOW_THRESHOLD    (PAJ7620_ADDR_BASE + 0x6A)    //RW
#define PAJ7620_ADDR_PS_APPROACH_STATE    (PAJ7620_ADDR_BASE + 0x6B)    //R
#define PAJ7620_ADDR_PS_RAW_DATA        (PAJ7620_ADDR_BASE + 0x6C)    //R

// REGISTER BANK 1
#define PAJ7620_ADDR_PS_GAIN            (PAJ7620_ADDR_BASE + 0x44)    //RW
#define PAJ7620_ADDR_IDLE_S1_STEP_0        (PAJ7620_ADDR_BASE + 0x67)    //RW
#define PAJ7620_ADDR_IDLE_S1_STEP_1        (PAJ7620_ADDR_BASE + 0x68)    //RW
#define PAJ7620_ADDR_IDLE_S2_STEP_0        (PAJ7620_ADDR_BASE + 0x69)    //RW
#define PAJ7620_ADDR_IDLE_S2_STEP_1        (PAJ7620_ADDR_BASE + 0x6A)    //RW
#define PAJ7620_ADDR_OP_TO_S1_STEP_0    (PAJ7620_ADDR_BASE + 0x6B)    //RW
#define PAJ7620_ADDR_OP_TO_S1_STEP_1    (PAJ7620_ADDR_BASE + 0x6C)    //RW
#define PAJ7620_ADDR_OP_TO_S2_STEP_0    (PAJ7620_ADDR_BASE + 0x6D)    //RW
#define PAJ7620_ADDR_OP_TO_S2_STEP_1    (PAJ7620_ADDR_BASE + 0x6E)    //RW
#define PAJ7620_ADDR_OPERATION_ENABLE    (PAJ7620_ADDR_BASE + 0x72)    //RW

// PAJ7620_REGITER_BANK_SEL
#define PAJ7620_BANK0        PAJ7620_VAL(0,0)
#define PAJ7620_BANK1        PAJ7620_VAL(1,0)

// PAJ7620_ADDR_SUSPEND_CMD
#define PAJ7620_I2C_WAKEUP    PAJ7620_VAL(1,0)
#define PAJ7620_I2C_SUSPEND    PAJ7620_VAL(0,0)

// PAJ7620_ADDR_OPERATION_ENABLE
#define PAJ7620_ENABLE        PAJ7620_VAL(1,0)
#define PAJ7620_DISABLE        PAJ7620_VAL(0,0)

typedef enum {
    BANK0 = 0,
    BANK1,        
} bank_e;
typedef enum {
    GES_RIGHT = 1,
    GES_LEFT = 1<<1,     
	GES_UP= 1<<2,
	GES_DOWN= 1<<3,
	GES_FORWARD= 1<<4,
	GES_BACKWARD= 1<<5,
	GES_CLOCKWISE= 1<<6,
	GES_COUNT_CLOCKWISE= 1<<7,
} GES_enum;
#define GES_RIGHT_FLAG                PAJ7620_VAL(1,0) //0x01
#define GES_LEFT_FLAG                PAJ7620_VAL(1,1)//0x02
#define GES_UP_FLAG                    PAJ7620_VAL(1,2)//0x04
#define GES_DOWN_FLAG                PAJ7620_VAL(1,3)//0x08
#define GES_FORWARD_FLAG            PAJ7620_VAL(1,4)//0x10
#define GES_BACKWARD_FLAG            PAJ7620_VAL(1,5)//0x20
#define GES_CLOCKWISE_FLAG            PAJ7620_VAL(1,6)//0x40
#define GES_COUNT_CLOCKWISE_FLAG    PAJ7620_VAL(1,7)//0x80
#define GES_WAVE_FLAG                PAJ7620_VAL(1,0)

#define INIT_REG_ARRAY_SIZE (sizeof(initRegisterArray)/sizeof(initRegisterArray[0]))

/**
 * @brief Initialize PAJ7620 sensor
 * @return 0: success, other: error code
 */
uint8_t paj7620Init(void);

/**
 * @brief Write register to PAJ7620
 * @param addr: register address
 * @param cmd: data to write
 * @return 0: success, other: error code
 */
uint8_t paj7620WriteReg(uint8_t addr, uint8_t cmd);

/**
 * @brief Read registers from PAJ7620
 * @param addr: start register address
 * @param qty: number of registers to read
 * @param data: buffer to store read data
 * @return 0: success, other: error code
 */
uint8_t paj7620ReadReg(uint8_t addr, uint8_t qty, uint8_t data[]);

/**
 * @brief Select register bank
 * @param bank: BANK0 or BANK1
 */
void paj7620SelectBank(bank_e bank);

#ifdef __cplusplus
}
#endif

#endif
