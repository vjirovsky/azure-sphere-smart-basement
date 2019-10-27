#include "adc_header.h"
#include "build_options.h"
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>


#include <applibs/adc.h>
#include <applibs/log.h>

#include <azureiot/iothub_client_core_common.h>
#include <azureiot/iothub_device_client_ll.h>
#include <azureiot/iothub_client_options.h>
#include <azureiot/iothubtransportmqtt.h>
#include <azureiot/iothub.h>
#include <azureiot/azure_sphere_provisioning.h>
#include "epoll_timerfd_utilities.h"
#include <hw\avnet_mt3620_sk.h>


int g_adc0_controller_fd = -1;
int g_ambient_sample_bit_count = -1;

int g_microphone_adc_fd = -1;
int g_microphone_sample_bit_count = -1;

/// <summary>
/// Init ADC controller&channel for ambient light sensor
/// </summary>
int InitAmbientSensor()
{
	if (g_adc0_controller_fd == -1) {
		g_adc0_controller_fd = ADC_Open(AVNET_MT3620_SK_ADC_CONTROLLER0);
		if (g_adc0_controller_fd < 0) {
			Log_Debug("ERROR - Failed init ambient sensor (open) with error: %s (%d)\n", strerror(errno), errno);
			return -1;
		}
	}

	g_ambient_sample_bit_count = ADC_GetSampleBitCount(g_adc0_controller_fd, MT3620_ADC_CHANNEL0);
	if (g_ambient_sample_bit_count == -1) {
		Log_Debug("ERROR - Failed init ambient sensor (samplebit) with error: %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	if (g_ambient_sample_bit_count == 0) {
		Log_Debug("ERROR - Failed init ambient sensor (samplebit == 0).\n");
		return -1;
	}

	int result = ADC_SetReferenceVoltage(g_adc0_controller_fd, MT3620_ADC_CHANNEL0, ALS_PT19_MAX_VOLTAGE);
	if (result < 0) {
		Log_Debug("ERROR - Failed init ambient sensor (setreferencevoltage) with error : %s (%d)\n", strerror(errno), errno);
		return -1;
	}
}


/// <summary>
/// Init ADC controller&channel for microphone
/// </summary>
int InitMicrophone()
{
	if (g_adc0_controller_fd == -1) {
		g_adc0_controller_fd = ADC_Open(AVNET_MT3620_SK_ADC_CONTROLLER0);
		if (g_adc0_controller_fd < 0) {
			Log_Debug("ERROR - Failed init microphone sensor (open) with error: %s (%d)\n", strerror(errno), errno);
			return -1;
		}
	}

	g_microphone_sample_bit_count = ADC_GetSampleBitCount(g_adc0_controller_fd, MT3620_ADC_CHANNEL1);
	if (g_microphone_sample_bit_count == -1) {
		Log_Debug("ERROR - Failed init microphone (samplebit) with error: %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	if (g_microphone_sample_bit_count == 0) {
		Log_Debug("ERROR - Failed init microphone (samplebit == 0).\n");
		return -1;
	}

	int result = ADC_SetReferenceVoltage(g_adc0_controller_fd, MT3620_ADC_CHANNEL1, MICROPHONE_MAX_VOLTAGE);
	if (result < 0) {
		Log_Debug("ERROR - Failed init ambient sensor (setreferencevoltage) with error : %s (%d)\n", strerror(errno), errno);
		return -1;
	}
}