/**
 * @file drv_QMI8658.c
 * @brief QMI8658 6-axis IMU: Pico I2C transport, register helpers, init, read, and calibration.
 *
 * This translation unit implements low-level @c iic0_* helpers on top of @c i2c_write_blocking /
 * @c i2c_read_blocking (see @c I2C_PORT in the board header), then higher-level QMI8658A_* APIs
 * for reset, WHO_AM_I check, CTRL register programming, optional self-test/COD/still calibration,
 * raw sample reads, and float conversion to g / dps.
 *
 * Optional compile-time features (default 0):
 *   @c QMI8658_STARTUP_SELF_TEST — run accelerometer and gyroscope self tests during init.
 *   @c QMI8658_STARTUP_COD — run gyro calibration-on-demand after enabling sensors.
 *   @c QMI8658_STARTUP_STILL_CALIBRATION — collect stationary samples into @c GyrCompensate.
 */

#include "drv_QMI8658.h"
#include "pico/stdlib.h"
#ifndef QMI8658_STARTUP_SELF_TEST
#define QMI8658_STARTUP_SELF_TEST 0
#endif

#ifndef QMI8658_STARTUP_COD
#define QMI8658_STARTUP_COD 0
#endif

#ifndef QMI8658_STARTUP_STILL_CALIBRATION
#define QMI8658_STARTUP_STILL_CALIBRATION 0
#endif

#ifndef QMI8658_RESET_TIMEOUT_MS
#define QMI8658_RESET_TIMEOUT_MS 50u
#endif

#ifndef QMI8658_CTRL9_TIMEOUT_MS
#define QMI8658_CTRL9_TIMEOUT_MS 100u
#endif

static uint8_t s_chQMI8658Address = Device_Address;

/**
 * @brief SMBus-style read: write register address with repeated start, then read @p len bytes.
 * @return 1 success, 0 if either I2C phase failed (matches legacy @c char success convention).
 */
char iic0_read_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len)
{
	uint8_t ret = 0;
	if( i2c_write_blocking(I2C_PORT,addr,&reg, 1,true) < 0){
		ret = 0;
	} else {
		ret = 1;
		if( i2c_read_blocking(I2C_PORT,addr,value, len,false) < 0){
			ret = 0;
		} else {
			ret = 1;
		}
	}

	return ret;
}

/**
 * @brief Write register followed by payload in one STOP-terminated transaction (max 15 data bytes).
 */
char iic0_write_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len)
{
	uint8_t ret = 0;
	uint8_t buf[16];
	if ((len + 1u) > sizeof(buf)) {
		return 0;
	}
	buf[0] = reg;
	memcpy(&buf[1],value,len);
	if( i2c_write_blocking(I2C_PORT,addr,buf, len+1,false) < 0){
		ret = 0;
	} else {
		ret = 1;
	}
	return ret;
}

/** One-byte read at @p addr from the default @c Device_Address slave. */
static unsigned char i2cread(unsigned char addr, unsigned char *Data) {
    return iic0_read_bytes(s_chQMI8658Address, addr, Data, 1);
}

/** Multi-byte read starting at @p addr. */
static unsigned char i2creads(uint8_t addr, uint8_t length, uint8_t *Data) {
    return iic0_read_bytes(s_chQMI8658Address, addr, Data, length);
}

/** Single-byte write to @p addr. */
static unsigned char i2cwrite(uint8_t addr, uint8_t Data) {
    return iic0_write_bytes(s_chQMI8658Address, addr, &Data, 1);
}

/** Multi-byte write of @p length bytes from @p Data starting at @p addr. */
static unsigned char i2cwrites(uint8_t addr, uint8_t length, const uint8_t *Data) {
    return iic0_write_bytes(s_chQMI8658Address, addr, (uint8_t *)Data, length);
}

static uint8_t qmi8658_probe_address(void)
{
    uint8_t who_am_i = 0;
    uint8_t const addresses[] = {
        (uint8_t)Device_Address,
        (uint8_t)QMI8658_ADDRESS_ALT,
    };

    for (uint32_t i = 0; i < (sizeof(addresses) / sizeof(addresses[0])); i++) {
        if ((i > 0u) && (addresses[i] == addresses[0])) {
            continue;
        }

        if (iic0_read_bytes(addresses[i], WHO_AM_I, &who_am_i, 1) &&
            (who_am_i == 0x05u)) {
            s_chQMI8658Address = addresses[i];
            return 1;
        }
    }

    return 0;
}

uint8_t QMI8658A_GetActiveAddress(void)
{
    return s_chQMI8658Address;
}
/** Poll @p reg until (@p mask & value) == @p expected or @p timeout_ms elapses. */
static uint8_t qmi8658_wait_reg_bits(uint8_t reg,
                                     uint8_t mask,
                                     uint8_t expected,
                                     uint32_t timeout_ms)
{
    uint8_t data = 0;
    uint32_t start = to_ms_since_boot(get_absolute_time());

    do {
        if (!i2cread(reg, &data)) {
            sleep_ms(1);
            continue;
        }
        if ((data & mask) == expected) {
            return 1;
        }
        sleep_ms(1);
    } while ((uint32_t)(to_ms_since_boot(get_absolute_time()) - start) < timeout_ms);

    return 0;
}

/**
 * @brief Execute a CTRL9 command sequence: write cmd, wait STATUSINT bit7 set, ACK with 0, wait clear.
 */
static uint8_t qmi8658_send_ctrl9_cmd(uint8_t cmd)
{
    if (!i2cwrite(CTRL9, cmd)) {
        return 0;
    }

    if (!qmi8658_wait_reg_bits(STATUSINT, 0x80u, 0x80u, QMI8658_CTRL9_TIMEOUT_MS)) {
        return 0;
    }

    if (!i2cwrite(CTRL9, 0x00)) {
        return 0;
    }

    return qmi8658_wait_reg_bits(STATUSINT, 0x80u, 0x00u, QMI8658_CTRL9_TIMEOUT_MS);
}

/** Configure and enable the vendor Tap engine; call while CTRL7.aEN/gEN are both 0. */
int QMI8658A_EnableTapDetectionDefault(void)
{
    uint8_t ctrl8 = 0;
    uint8_t const first_set[8] = {
        (uint8_t)QMI8658_TAP_DEFAULT_PEAK_WINDOW,
        (uint8_t)(QMI8658_TAP_DEFAULT_PRIORITY & 0x07u),
        (uint8_t)(QMI8658_TAP_DEFAULT_TAP_WINDOW & 0xFFu),
        (uint8_t)((QMI8658_TAP_DEFAULT_TAP_WINDOW >> 8) & 0xFFu),
        (uint8_t)(QMI8658_TAP_DEFAULT_DTAP_WINDOW & 0xFFu),
        (uint8_t)((QMI8658_TAP_DEFAULT_DTAP_WINDOW >> 8) & 0xFFu),
        0x00u,
        0x01u,
    };
    uint8_t const second_set[8] = {
        (uint8_t)QMI8658_TAP_DEFAULT_ALPHA,
        (uint8_t)QMI8658_TAP_DEFAULT_GAMMA,
        (uint8_t)(QMI8658_TAP_DEFAULT_PEAK_MAG_THR & 0xFFu),
        (uint8_t)((QMI8658_TAP_DEFAULT_PEAK_MAG_THR >> 8) & 0xFFu),
        (uint8_t)(QMI8658_TAP_DEFAULT_UDM_THR & 0xFFu),
        (uint8_t)((QMI8658_TAP_DEFAULT_UDM_THR >> 8) & 0xFFu),
        0x00u,
        0x02u,
    };

    if (!i2cwrites(CAL1_L, sizeof(first_set), first_set)) {
        return 0;
    }
    if (!qmi8658_send_ctrl9_cmd(0x0Cu)) {
        return 0;
    }

    if (!i2cwrites(CAL1_L, sizeof(second_set), second_set)) {
        return 0;
    }
    if (!qmi8658_send_ctrl9_cmd(0x0Cu)) {
        return 0;
    }

    if (!i2cread(CTRL8, &ctrl8)) {
        return 0;
    }
    ctrl8 |= 0x01u;
    return i2cwrite(CTRL8, ctrl8);
}

int QMI8658A_ReadTapStatus(qmi8658_tap_status_t *status)
{
    uint8_t status1 = 0;
    uint8_t tap_status = 0;

    if (status == NULL) {
        return 0;
    }

    status->num = QMI8658_TAP_NUM_NONE;
    status->axis = QMI8658_TAP_AXIS_NONE;
    status->polarity = QMI8658_TAP_POLARITY_POSITIVE;
    status->raw_status = 0;

    if (!i2cread(STATUS1, &status1)) {
        return 0;
    }
    if ((status1 & 0x02u) == 0u) {
        return 1;
    }

    if (!i2cread(TAP_STATUS, &tap_status)) {
        return 0;
    }

    status->raw_status = tap_status;
    status->num = (qmi8658_tap_num_t)(tap_status & 0x03u);
    status->axis = (qmi8658_tap_axis_t)((tap_status >> 4) & 0x03u);
    status->polarity = (tap_status & 0x80u) ?
                       QMI8658_TAP_POLARITY_NEGATIVE :
                       QMI8658_TAP_POLARITY_POSITIVE;

    
    if ((status->axis == QMI8658_TAP_AXIS_NONE) || ((tap_status & 0x03u) == 0x03u)) {
        status->num = QMI8658_TAP_NUM_NONE;
    }

    return 1;
}
	uint8_t reg_data__[100];

/** Table mapping @c CommandEnum indices to human-readable metadata (debug / tooling). */
CommandInfo commandInfos[] = {
    {"CTRL_CMD_ACK", 0x00, "Ctrl9", "15?"},
    {"CTRL_CMD_RST_FIFO", 0x04, "Ctrl9", "14?"},
    {"CTRL_CMD_REQ_FIFO", 0x05, "Ctrl9R", "13?"},
    {"CTRL_CMD_WRITE_WOM_SETTING", 0x08, "WCtrl9", "12?"},
    {"CTRL_CMD_ACCEL_HOST_DELTA_OFFSET", 0x09, "WCtrl9", "11?"},
    {"CTRL_CMD_GYRO_HOST_DELTA_OFFSET", 0x0A, "WCtrl9", "10?"},
    {"CTRL_CMD_CONFIGURE_TAP", 0x0C, "WCtrl9", "9?"},
    {"CTRL_CMD_CONFIGURE_PEDOMETER", 0x0D, "WCtrl9", "8?"},
    {"CTRL_CMD_CONFIGURE_MOTION", 0x0E, "WCtrl9", "7?"},
    {"CTRL_CMD_RESET_PEDOMETER", 0x0F, "WCtrl9", "6?"},
    {"CTRL_CMD_COPY_USID", 0x10, "Ctrl9R", "5?"},
    {"CTRL_CMD_SET_RPU", 0x11, "WCtrl9", "4?"},
    {"CTRL_CMD_AHB_CLOCK_GATING", 0x12, "WCtrl9", "3?"},
    {"CTRL_CMD_ON_DEMAND_CALIBRATION", 0xA2, "WCtrl9", "2?"},
    {"CTRL_CMD_APPLY_GYRO_GAINS", 0xAA, "WCtrl9", "1?"}
};

/** Lookup @c commandInfos[] by enum index (no bounds check). */
CommandInfo getCommandInfo(CommandEnum cmd) {
    return commandInfos[cmd];
}

/*
 * Example (debug):
 *   CommandInfo info = getCommandInfo(CTRL_CMD_REQ_FIFO_ENUM);
 *   printf("Command Name: %s\n", info.commandName);
 */

/**
 * @brief Run the vendor accelerometer self-test sequence (optional at startup).
 * @return 1 if self-test deltas look plausible, 0 on I2C failure or out-of-range result.
 */
uint8_t Acc_Self_Test()
{
    uint8_t data = 0;

    /* 1) Disable all sensors while configuring self-test. */
    if (!i2cwrite(CTRL7, 0x00)) {
        return 0;
    }

    /* 2) Accel ODR/range for self-test (vendor pattern: 0x93 ≈ ±4g, high ODR). */
    if (!i2cwrite(CTRL2, 0x93)) {
        return 0;
    }

    /* 3) Wait until STATUSINT bit0 (data ready / self-test busy) reads 1. */
    data = 0x00;
    while ((data & 0x01) == 0) {
        sleep_ms(10);
        if (!i2cread(STATUSINT, &data)) {
            return 0;
        }
    }

    /* 4) Clear CTRL2 self-test bit (bit7, aST) before reading results. */
    if (!i2cread(CTRL2, &data)) {
        return 0;
    }
    data &= 0x7F;
    if (!i2cwrite(CTRL2, data)) {
        return 0;
    }

    /* 5) Wait until STATUSINT bit0 returns to 0 (sequence complete). */
    data = 0xFF;
    while ((data & 0x01) != 0) {
        sleep_ms(10);
        if (!i2cread(STATUSINT, &data)) {
            return 0;
        }
    }

    /* 6) Fetch 6 bytes of self-test delta data from vendor offset 0x51. */
    unsigned char datas[6] = {};
    if (!i2creads(0x51, 6, datas)) {
        return 0;
    }

    int16_t dVxData = 0, dVyData = 0, dVzData = 0;
    dVxData = (datas[1] << 8) | datas[0];
    dVyData = (datas[3] << 8) | datas[2];
    dVzData = (datas[5] << 8) | datas[4];

    /* Pass if every axis magnitude (0.5 LSB scale) exceeds ~200 mg threshold. */
    if (fabs(dVxData * 0.5) < 200 || fabs(dVyData * 0.5) < 200 || fabs(dVzData * 0.5) < 200) {
        return 0;
    }
    return 1;
}


/**
 * @brief Run the vendor gyroscope self-test sequence (optional at startup).
 * @return 1 if self-test rates look plausible, 0 on I2C failure or out-of-range result.
 */
uint8_t Gyr_Self_Test()
{
    uint8_t data = 0;

    if (!i2cwrite(CTRL7, 0x00)) {
        return 0;
    }

    /* Set gyro self-test bit (gST, CTRL3 bit7). */
    if (!i2cread(CTRL3, &data)) {
        return 0;
    }
    data |= 0x80;
    if (!i2cwrite(CTRL3, data)) {
        return 0;
    }

    data = 0x00;
    while ((data & 0x01) == 0) {
        if (!i2cread(STATUSINT, &data)) {
            return 0;
        }
    }

    if (!i2cread(CTRL3, &data)) {
        return 0;
    }
    data &= 0x7F;
    if (!i2cwrite(CTRL3, data)) {
        return 0;
    }

    data = 0xFF;
    while ((data & 0x01) != 0) {
        if (!i2cread(STATUSINT, &data)) {
            return 0;
        }
    }

    unsigned char datas[6] = {};
    if (!i2creads(0x51, 6, datas)) {
        return 0;
    }

    int16_t dVxData = 0, dVyData = 0, dVzData = 0;
    dVxData = (datas[1] << 8) | datas[0];
    dVyData = (datas[3] << 8) | datas[2];
    dVzData = (datas[5] << 8) | datas[4];

    /* Vendor scaling: 62.5 mdps/LSB; require each axis > ~300 dps after scaling. */
    if (fabs(dVxData * 62.5 / 1000) < 300 || fabs(dVyData * 62.5 / 1000) < 300 || fabs(dVzData * 62.5 / 1000) < 300) {
        return 0;
    }
    return 1;
}

/**
 * @brief Gyroscope calibration-on-demand (COD) via CTRL9; keep the board still.
 * @return 1 if @c COD_STATUS reads zero after the handshake, 0 on timeout or error.
 */
uint8_t Gyr_COD()
{
    uint8_t data = 0;

    if (!i2cwrite(CTRL7, 0x00)) {
        return 0;
    }

    if (!i2cwrite(CTRL9, 0xA2)) {
        return 0;
    }

    sleep_ms(1500);

    data = 0x00;
    while (((data >> 7) & 0x01) == 0) {
        if (!i2cread(STATUSINT, &data)) {
            return 0;
        }
    }

    if (!i2cwrite(CTRL9, 0x00)) {
        return 0;
    }

    data = 0xFF;
    while (((data >> 7) & 0x01) == 1) {
        if (!i2cread(STATUSINT, &data)) {
            return 0;
        }
    }

    if (!i2cread(COD_STATUS, &data)) {
        return 0;
    }
    if (data) {
        return 0;
    }
    if (!i2cwrite(CTRL7, 0x83)) {
        return 0;
    }

    return 1;
}


/** Gyro bias / trim values applied in @ref QMI8658A_ConvertData when still-calibration is used. */
float GyrCompensate[6];

/**
 * @brief Reset, verify WHO_AM_I, program ODR/range, optional self-test/COD/still calibration.
 * @return 1 on success, 0 if any I2C step or optional test/calibration fails.
 */
int QMI8658A_Init(void)
{
    uint8_t data = 0;

    if (!qmi8658_probe_address()) {
        return 0;
    }

    if (!i2cwrite(RESET, 0xB0)) {
        return 0;
    }

    if (!qmi8658_wait_reg_bits(dQY_L, 0x80u, 0x80u, QMI8658_RESET_TIMEOUT_MS)) {
        return 0;
    }

    if (!i2cread(WHO_AM_I, &data)) {
        return 0;
    }
    if (data != 0x05u) {
        return 0;
    }

    if (!i2cwrite(CTRL1, 0x60)) {
        return 0;
    }

#if QMI8658_STARTUP_SELF_TEST
    if (!Acc_Self_Test()) {
        return 0;
    }
    if (!Gyr_Self_Test()) {
        return 0;
    }
#endif

    if (!i2cwrite(CTRL2, QMI8658_CTRL2_VALUE)) {
        return 0;
    }

    if (!i2cwrite(CTRL3, QMI8658_CTRL3_VALUE)) {
        return 0;
    }

    if (!i2cwrite(CTRL5, 0x35)) {
        return 0;
    }

    if (!i2cwrite(CAL1_L, 0x01)) {
        return 0;
    }
    if (!qmi8658_send_ctrl9_cmd(0x12)) {
        return 0;
    }

#if QMI8658_TAP_ENABLE
    if (!QMI8658A_EnableTapDetectionDefault()) {
        return 0;
    }
    if (!i2cwrite(CTRL7, 0x03)) {
        return 0;
    }
#else
    if (!i2cwrite(CTRL7, 0x83)) {
        return 0;
    }
#endif
    sleep_ms(2);

#if QMI8658_STARTUP_COD
    if (!Gyr_COD()) {
        return 0;
    }
#endif

#if QMI8658_STARTUP_STILL_CALIBRATION
    if (!calibration_ACC_GYR(GyrCompensate)) {
        return 0;
    }
#else
    memset(GyrCompensate, 0, sizeof(GyrCompensate));
#endif

    return 1;
}


/**
 * @brief Read 12 bytes of axis data (accel then gyro) as little-endian int16 samples.
 * @param DATA Out: @c [AX,AY,AZ,GX,GY,GZ] raw counts (implementation may skip status polling).
 * @return 1 if the burst read succeeded, 0 on I2C error.
 */
int QMI8658A_ReadData(int16_t *DATA)
{
    uint8_t data = 0;

    if (DATA == NULL) {
        return 0;
    }

    (void)data;
    /* Optional: poll STATUSINT for Avail/Locked before burst read (left commented for speed). */

    uint8_t datas[12] = {};
    if (!i2creads(A_XYZ, 12, datas)) {
        return 0;
    }

    DATA[0] = (datas[1] << 8) | datas[0]; /* AX */
    DATA[1] = (datas[3] << 8) | datas[2]; /* AY */
    DATA[2] = (datas[5] << 8) | datas[4]; /* AZ */
    DATA[3] = (datas[7] << 8) | datas[6]; /* GX */
    DATA[4] = (datas[9] << 8) | datas[8]; /* GY */
    DATA[5] = (datas[11] << 8)| datas[10]; /* GZ */

    return 1;
}

/**
 * @brief Scale raw int16 samples to g and dps using full-scale settings; subtract gyro trims.
 * @param InData  Raw [AX,AY,AZ,GX,GY,GZ].
 * @param OutData Output [AX..GZ] in g and dps respectively.
 * @param accelRange Full-scale g setting (2, 4, 8, or 16).
 * @param gyroRange  Full-scale dps setting (16 … 2048).
 */
void QMI8658A_ConvertData(int16_t *InData, float *OutData, int accelRange, int gyroRange)
{
    float accelFactor, compensate;

    if ((InData == NULL) || (OutData == NULL)) {
        return;
    }
    
    /* Full-scale counts to g: LSB weight = (2 * FS_g) / 2^16 for 16-bit signed container. */
    switch (accelRange)
    {
        case 2: // ±2g
            accelFactor = 2.0f * 2 / 65536;
		    
            break;
        case 4: // ±4g
            accelFactor = 4.0f * 2 / 65536;
            break;
        case 8: // ±8g
            accelFactor = 8.0f * 2 / 65536;
            break;
        case 16: // ±16g
            accelFactor = 16.0f * 2 / 65536;
            break;
        default:
            accelFactor = 0;
            break;
    }
    for (int i = 0; i < 3; i++)
    {
        OutData[i] = InData[i] * accelFactor;
    }
    float gyroFactor;

    /* Same scaling pattern for angular rate in dps. */
    switch (gyroRange)
    {
        case 16: // ±16dps
            gyroFactor = 16.0f * 2 / 65536;
            break;
        case 32: // ±32dps
            gyroFactor = 32.0f * 2 / 65536;
            break;
        case 64: // ±64dps
            gyroFactor = 64.0f * 2 / 65536;
            break;
        case 128: // ±128dps
            gyroFactor = 128.0f * 2 / 65536;
            break;
        case 256: // ±256dps
            gyroFactor = 256.0f * 2 / 65536;
            break;
        case 512: // ±512dps
            gyroFactor = 512.0f * 2 / 65536;
            break;
        case 1024: // ±1024dps
            gyroFactor = 1024.0f * 2 / 65536;
            break;
        case 2048: // ±2048dps
            gyroFactor = 2048.0f * 2 / 65536;
            break;
        default:
            gyroFactor = 0;
            break;
    }

    /* Gyro axes use runtime bias terms from still calibration (GyrCompensate[3..5]). */
    for (int i = 3; i < 6; i++)
    {
        switch (i)
        {
            case 3:
                compensate = GyrCompensate[3];
                break;
            case 4:
                compensate = GyrCompensate[4];
                break;
            case 5:
                compensate = GyrCompensate[5];
                break;
            default:
                compensate = 0.0f;
                break;
        }
        OutData[i] = InData[i] * gyroFactor - compensate;
    }
}

/**
 * @brief Convenience: read + convert using board @c ACCRANGE / @c GYRRANGE from the header.
 */
void QMI8658A_Get_G_DPS(float *OutData)
{
    int16_t data[6];

    if (OutData == NULL) {
        return;
    }

    if (!QMI8658A_ReadData(data)) {
        memset(OutData, 0, 6u * sizeof(OutData[0]));
        return;
    }
    QMI8658A_ConvertData(data, OutData, ACCRANGE, GYRRANGE);
}

/** @brief Euclidean norm of the first three floats (|a| in g). */
float calculateAccelerationMagnitude(float *OutData)
{
    return (float)sqrt(OutData[0] * OutData[0] + OutData[1] * OutData[1] + OutData[2] * OutData[2]);
}

/** @brief qsort comparator for ascending float (non-stable tie-break). */
int compareFloat(const void *a, const void *b)
{
    return (*(float *)a - *(float *)b) > 0 ? 1 : -1;
}

/**
 * @brief Per-axis trimmed mean: sort each axis, average the central @c USED_DATA_COUNT samples.
 * @note Allocates temporary buffers on the heap; frees them before return.
 */
void calculateAverages(float DATA[MIN_COLLECTION_COUNT][6], float *OutData) {
    float **sortedData = (float **)malloc(6 * sizeof(float *));
    if (sortedData == NULL) {
        return;
    }

    for (int j = 0; j < 6; j++) {
        sortedData[j] = (float *)malloc(MIN_COLLECTION_COUNT * sizeof(float));
        if (sortedData[j] == NULL) {
            for (int k = 0; k < j; k++) {
                free(sortedData[k]);
            }
            free(sortedData);
            return;
        }
    }

    for (int i = 0; i < MIN_COLLECTION_COUNT; i++) {
        for (int j = 0; j < 6; j++) {
            sortedData[j][i] = DATA[i][j];
        }
    }

    for (int j = 0; j < 6; j++) {
        qsort(sortedData[j], MIN_COLLECTION_COUNT, sizeof(float), compareFloat);
    }

    int startIndex = (MIN_COLLECTION_COUNT - USED_DATA_COUNT) / 2;

    for (int j = 0; j < 6; j++) {
        float sum = 0;
        for (int i = startIndex; i < startIndex + USED_DATA_COUNT; i++) {
            sum += sortedData[j][i];
        }
        OutData[j] = sum / USED_DATA_COUNT;
    }

    for (int j = 0; j < 6; j++) {
        free(sortedData[j]);
    }
    free(sortedData);
}


/**
 * @brief Collect @c MIN_COLLECTION_COUNT “stationary” samples (|Δ|a| small) then trimmed average.
 * @param OutData Mean vector [AX..GZ] written for use as @c GyrCompensate-style offsets.
 * @return 1 when calibration completes, 0 on malloc failure, bad pointer, or read errors.
 */
uint8_t calibration_ACC_GYR(float *OutData) {
    if (OutData == NULL) {
        return 0;
    }

    int16_t rawData[6];
    float convertedData[6], AM = 0;
    float OldAM = 0;
    float (*DATA)[6] = (float (*)[6])malloc(MIN_COLLECTION_COUNT * sizeof(float[6]));
    if (DATA == NULL) {
        return 0;
    }
    int i = 0;

    while (i < MIN_COLLECTION_COUNT) {
        if (!QMI8658A_ReadData(rawData)) {
            free(DATA);
            return 0;
        }

        QMI8658A_ConvertData(rawData, convertedData, ACCRANGE, GYRRANGE);

        AM = calculateAccelerationMagnitude(convertedData);

        if (fabs(AM - OldAM) < STATIONARY_THRESHOLD) {
            DATA[i][0] = convertedData[0];
            DATA[i][1] = convertedData[1];
            DATA[i][2] = convertedData[2];
            DATA[i][3] = convertedData[3];
            DATA[i][4] = convertedData[4];
            DATA[i][5] = convertedData[5];
            i++;
        } else {
            OldAM = AM;
        }
    }

    calculateAverages(DATA, OutData);

    free(DATA);

    return 1;
}


/**
 * @brief Periodic read/convert hook (logging placeholders commented out); throttles every 100 calls.
 */
void QMI8658A_ReadConvertAndPrint() {
    
    static int i=0;
    
    int16_t rawData[6];
    float convertedData[6],AM=0;

    if (!QMI8658A_ReadData(rawData)) {
        return;
    }

    QMI8658A_ConvertData(rawData, convertedData, ACCRANGE, GYRRANGE);


    AM= calculateAccelerationMagnitude(convertedData);


    /* Placeholder for printf/ESP_LOG style trace (commented). */
    // ESP_LOGE(TAG, " (g): AX=%d, AY=%d, AZ=%d (dps): GX=%d, GY=%d, GZ=%d", 
    //  rawData[0], rawData[1], rawData[2],rawData[3], rawData[4], rawData[5]);
    if(i==100)
    {
        i=0;
//    ESP_LOGE(TAG, " (g): AX=%.3f, AY=%.3f, AZ=%.3f, AM=%.3f (dps): GX=%.3f, GY=%.3f, GZ=%.3f",
//              (float)convertedData[0], (float)convertedData[1], (float)convertedData[2],AM,(float)convertedData[3], (float)convertedData[4], (float)convertedData[5]);
    }
               i++;
}


