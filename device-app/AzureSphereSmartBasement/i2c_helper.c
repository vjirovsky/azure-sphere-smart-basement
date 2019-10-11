/*
 *  Some of the code in this file was copied from ST Micro.  Below is their required information.
 *
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT(c) 2018 STMicroelectronics</center></h2>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "led_helper.h"

 // applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"

#include <applibs/log.h>
#include <applibs/i2c.h>

#include "mt3620_avnet_dev.h"
#include "build_options.h"

#include "i2c_helper.h"
#include "lsm6dso_reg.h"

/* Private variables ---------------------------------------------------------*/

extern float g_acceleration_mg[3];
extern float g_angular_rate_dps[3];

extern axis3bit16_t g_data_raw_acceleration;
extern axis3bit16_t g_data_raw_angular_rate; 
extern axis3bit16_t g_raw_angular_rate_calibration;

static uint8_t whoamI, rst;
int accelTimerFd;
const uint8_t lsm6dsOAddress = LSM6DSO_ADDRESS;     // Addr = 0x6A
extern lsm6dso_ctx_t dev_ctx;

//Extern variables
int i2cFd = -1;
extern int epollFd;
extern volatile sig_atomic_t g_is_app_exit_requested;

//Private functions

// Routines to read/write to the LSM6DSO device
static int32_t platform_write(int* fD, uint8_t reg, uint8_t* bufp, uint16_t len);
static int32_t platform_read(int* fD, uint8_t reg, uint8_t* bufp, uint16_t len);


/// <summary>
///     Sleep for delayTime ms
/// </summary>
void HAL_Delay(int delayTime) {
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = delayTime * 10000;
	nanosleep(&ts, NULL);
}



/// <summary>
///     Initializes the I2C interface.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
int initI2c(void) {

	// Begin MT3620 I2C init 

	i2cFd = I2CMaster_Open(MT3620_RDB_HEADER4_ISU2_I2C);
	if (i2cFd < 0) {
		Log_Debug("ERROR: I2CMaster_Open: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	int result = I2CMaster_SetBusSpeed(i2cFd, I2C_BUS_SPEED_STANDARD);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetBusSpeed: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	result = I2CMaster_SetTimeout(i2cFd, 100);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetTimeout: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	// Start lsm6dso specific init

	// Initialize lsm6dso mems driver interface
	dev_ctx.write_reg = platform_write;
	dev_ctx.read_reg = platform_read;
	dev_ctx.handle = &i2cFd;

	// Check device ID
	lsm6dso_device_id_get(&dev_ctx, &whoamI);
	if (whoamI != LSM6DSO_ID) {
		Log_Debug("LSM6DSO not found!\n");
		return -1;
	}
	else {
		Log_Debug("LSM6DSO Found!\n");
	}

	// Restore default configuration
	lsm6dso_reset_set(&dev_ctx, PROPERTY_ENABLE);
	do {
		lsm6dso_reset_get(&dev_ctx, &rst);
	} while (rst);

	// Disable I3C interface
	lsm6dso_i3c_disable_set(&dev_ctx, LSM6DSO_I3C_DISABLE);

	// Enable Block Data Update
	lsm6dso_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);

	// Set Output Data Rate
	lsm6dso_xl_data_rate_set(&dev_ctx, LSM6DSO_XL_ODR_12Hz5);
	lsm6dso_gy_data_rate_set(&dev_ctx, LSM6DSO_GY_ODR_12Hz5);

	// Set full scale
	lsm6dso_xl_full_scale_set(&dev_ctx, LSM6DSO_4g);
	lsm6dso_gy_full_scale_set(&dev_ctx, LSM6DSO_2000dps);

	// Configure filtering chain(No aux interface)
   // Accelerometer - LPF1 + LPF2 path	
	lsm6dso_xl_hp_path_on_out_set(&dev_ctx, LSM6DSO_LP_ODR_DIV_100);
	lsm6dso_xl_filter_lp2_set(&dev_ctx, PROPERTY_ENABLE);


	// Read the raw angular rate data from the device to use as offsets.  We're making the assumption that the device
	// is stationary.

	uint8_t reg;

	Log_Debug("LSM6DSO: Calibrating angular rate . . .\n");
	Log_Debug("LSM6DSO: Please make sure the device is stationary.\n");

	do {

		// Delay and read the device until we have data!
		do {
			// Read the calibration values
			HAL_Delay(5000);
			lsm6dso_gy_flag_data_ready_get(&dev_ctx, &reg);
		} while (!reg);

		if (reg)
		{
			// Read angular rate field data to use for calibration offsets
			memset(g_data_raw_angular_rate.u8bit, 0x00, 3 * sizeof(int16_t));
			lsm6dso_angular_rate_raw_get(&dev_ctx, g_raw_angular_rate_calibration.u8bit);
		}

		// Delay and read the device until we have data!
		do {
			// Read the calibration values
			HAL_Delay(5000);
			lsm6dso_gy_flag_data_ready_get(&dev_ctx, &reg);
		} while (!reg);

		// Read the angular data rate again and verify that after applying the calibration, we have 0 angular rate in all directions
		if (reg)
		{

			// Read angular rate field data
			memset(g_data_raw_angular_rate.u8bit, 0x00, 3 * sizeof(int16_t));
			lsm6dso_angular_rate_raw_get(&dev_ctx, g_data_raw_angular_rate.u8bit);

			// Before we store the mdps values subtract the calibration data we captured at startup.
			g_angular_rate_dps[0] = lsm6dso_from_fs2000_to_mdps(g_data_raw_angular_rate.i16bit[0] - g_raw_angular_rate_calibration.i16bit[0]);
			g_angular_rate_dps[1] = lsm6dso_from_fs2000_to_mdps(g_data_raw_angular_rate.i16bit[1] - g_raw_angular_rate_calibration.i16bit[1]);
			g_angular_rate_dps[2] = lsm6dso_from_fs2000_to_mdps(g_data_raw_angular_rate.i16bit[2] - g_raw_angular_rate_calibration.i16bit[2]);

		}

		// If the angular values after applying the offset are not all 0.0s, then do it again!
	} while ((g_angular_rate_dps[0] != 0.0) || (g_angular_rate_dps[1] != 0.0) || (g_angular_rate_dps[2] != 0.0));

	Log_Debug("LSM6DSO: Calibrating angular rate complete!\n");

	

	return 0;
}

/// <summary>
///     Closes the I2C interface File Descriptors.
/// </summary>
void closeI2c(void) {

	CloseFdAndPrintError(i2cFd, "i2c");
	CloseFdAndPrintError(accelTimerFd, "accelTimer");
}

/// <summary>
///     Writes data to the lsm6dso i2c device
/// </summary>
/// <returns>0</returns>

static int32_t platform_write(int* fD, uint8_t reg, uint8_t* bufp,
	uint16_t len) {

	// Construct a new command buffer that contains the register to write to, then the data to write
	uint8_t cmdBuffer[len + 1];
	cmdBuffer[0] = reg;
	for (int i = 0; i < len; i++) {
		cmdBuffer[i + 1] = bufp[i];
	}


	// Write the data to the device
	int32_t retVal = I2CMaster_Write(*fD, lsm6dsOAddress, cmdBuffer, (size_t)len + 1);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_write: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	return 0;
}

/// <summary>
///     Reads generic device register from the i2c interface
/// </summary>
/// <returns>0</returns>

/*
 * @brief  Read generic device register (platform dependent)
 *
 * @param  handle    customizable argument. In this examples is used in
 *                   order to select the correct sensor bus handler.
 * @param  reg       register to read
 * @param  bufp      pointer to buffer that store the data read
 * @param  len       number of consecutive register to read
 *
 */
static int32_t platform_read(int* fD, uint8_t reg, uint8_t* bufp,
	uint16_t len)
{
	// Set the register address to read
	int32_t retVal = I2CMaster_Write(i2cFd, lsm6dsOAddress, &reg, 1);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_read(write step): errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	// Read the data into the provided buffer
	retVal = I2CMaster_Read(i2cFd, lsm6dsOAddress, bufp, len);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_read(read step): errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	return 0;
}

