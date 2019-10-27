#include "main.h"
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>


#include "../Hardware/avnet_mt3620_sk/inc/hw/sample_hardware.h"

#include "adc_header.h"
#include "button_helper.h"
#include "general_helper.h"
#include "i2c_helper.h"
#include "led_helper.h"
#include "applibs_versions.h"
#include "epoll_timerfd_utilities.h"

#include "build_options.h"

#include <applibs/adc.h>
#include <applibs/gpio.h>
#include <applibs/i2c.h>
#include <applibs/log.h>
#include <applibs/wificonfig.h>

#include "iothub_helper.h"

#include "lsm6dso_reg.h"

char scopeId[SCOPEID_LENGTH]; // ScopeId for the Azure IoT Central application, set in
									 // app_manifest.json, CmdArgs

//epoll timer for reading sensors
int g_read_sensors_timer_fd = -1;

//epoll timer for checking button A
extern int g_button_a_timer_fd;

//FD for GPIO of Button A
extern int g_button_a_gpio_fd;

//epoll timer for syncing with IoT Hub
extern int g_azure_iot_sync_timer_fd;

//FD for ADC0
extern int g_adc0_controller_fd;

extern IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle;

bool g_is_iothub_authenticated = false;
extern int g_iot_hub_current_poll_period_seconds;
extern const int g_iot_hub_default_poll_period_seconds;

// epoll timer FD instance 
int epollFd = -1;

lsm6dso_ctx_t dev_ctx;


sensors_state g_previous_state = {false, false, false, false};

// Application state
volatile sig_atomic_t g_is_app_exit_requested = false;
bool g_is_armed = false;
bool g_is_breach_detected = false;
bool g_is_wifi_connected = false;

//new-measured sensor data
axis3bit16_t g_data_raw_acceleration;
axis3bit16_t g_data_raw_angular_rate;
float g_acceleration_mg[3];
float g_angular_rate_dps[3];
extern int g_ambient_sample_bit_count;
extern int g_microphone_sample_bit_count;

//calibration data for angular sensor
axis3bit16_t g_raw_angular_rate_calibration;

/// <summary>
/// This will exit application
/// </summary>
void ExitApplication()
{
	// Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
	g_is_app_exit_requested = true;
}


EventData buttonEventData = { .eventHandler = &ButtonATimerEventHandler };
EventData azureEventData = { .eventHandler = &AzureTimerEventHandler };

/// <summary>
/// Get new sensor data and submit event to IoT Hub
/// </summary>
void GetAndProcessDataFromSensors(EventData* eventData)
{
	float ambient;
	uint8_t reg;

	// Consume the event.  If we don't do this we'll come right back 
	// to process the same event again
	if (ConsumeTimerFdEvent(g_read_sensors_timer_fd) != 0) {
		ExitApplication();
		return;
	}

	if (!g_is_armed) {
		setNewLEDStateRGB(false, false, false);

		//there is no reason to read data from sensors - alarm is not armed
		return;
	}

	// set up LED to constant blue
	setNewLEDStateRGB(false, false, true);


	//read data from acceleration sensor

	lsm6dso_xl_flag_data_ready_get(&dev_ctx, &reg);
	float absolute_accel = 0;
	if (reg)
	{
		g_previous_state.acceleration_mg[0] = g_acceleration_mg[0];
		g_previous_state.acceleration_mg[1] = g_acceleration_mg[1];
		g_previous_state.acceleration_mg[2] = g_acceleration_mg[2];
		// Read acceleration field data
		memset(g_data_raw_acceleration.u8bit, 0x00, 3 * sizeof(int16_t));
		lsm6dso_acceleration_raw_get(&dev_ctx, g_data_raw_acceleration.u8bit);

		g_acceleration_mg[0] = lsm6dso_from_fs4_to_mg(g_data_raw_acceleration.i16bit[0]);
		g_acceleration_mg[1] = lsm6dso_from_fs4_to_mg(g_data_raw_acceleration.i16bit[1]);
		g_acceleration_mg[2] = lsm6dso_from_fs4_to_mg(g_data_raw_acceleration.i16bit[2]);

		absolute_accel = sqrt(pow(g_previous_state.acceleration_mg[0] - g_acceleration_mg[0], 2) + pow(g_previous_state.acceleration_mg[1] - g_acceleration_mg[1], 2) + pow(g_previous_state.acceleration_mg[2] - g_acceleration_mg[2], 2));
		Log_Debug("Acceleration difference: %.4lf\n", absolute_accel);
		if (absolute_accel > ABSOLUTE_ACCELERATION_THRESHOLD && g_previous_state.is_acceleration_set) {
			g_is_breach_detected = true;
		}
		g_previous_state.is_acceleration_set = true;
	}

	//read data from angular rate sensor

	float absolute_angular = 0;
	lsm6dso_gy_flag_data_ready_get(&dev_ctx, &reg);
	if (reg)
	{
		g_previous_state.angular_rate_dps[0] = g_angular_rate_dps[0];
		g_previous_state.angular_rate_dps[1] = g_angular_rate_dps[1];
		g_previous_state.angular_rate_dps[2] = g_angular_rate_dps[2];
		// Read angular rate field data
		memset(g_data_raw_angular_rate.u8bit, 0x00, 3 * sizeof(int16_t));
		lsm6dso_angular_rate_raw_get(&dev_ctx, g_data_raw_angular_rate.u8bit);

		// Before we store the mdps values subtract the calibration data we captured at startup.
		g_angular_rate_dps[0] = (lsm6dso_from_fs2000_to_mdps(g_data_raw_angular_rate.i16bit[0] - g_raw_angular_rate_calibration.i16bit[0])) / 1000.0;
		g_angular_rate_dps[1] = (lsm6dso_from_fs2000_to_mdps(g_data_raw_angular_rate.i16bit[1] - g_raw_angular_rate_calibration.i16bit[1])) / 1000.0;
		g_angular_rate_dps[2] = (lsm6dso_from_fs2000_to_mdps(g_data_raw_angular_rate.i16bit[2] - g_raw_angular_rate_calibration.i16bit[2])) / 1000.0;

		Log_Debug("Angular rate [dps] : %4.2f, %4.2f, %4.2f\n", g_angular_rate_dps[0], g_angular_rate_dps[1], g_angular_rate_dps[2]);
		absolute_angular = sqrt(pow(g_previous_state.angular_rate_dps[0] - g_angular_rate_dps[0], 2) + pow(g_previous_state.angular_rate_dps[1] - g_angular_rate_dps[1], 2) + pow(g_previous_state.angular_rate_dps[2] - g_angular_rate_dps[2], 2));
		Log_Debug("Angular rate difference: %.4lf\n", absolute_angular);
		if (absolute_angular > ABSOLUTE_ANGULAR_THRESHOLD && g_previous_state.is_angular_rate_set) {
			g_is_breach_detected = true;
		}

		g_previous_state.is_angular_rate_set = true;
	}


	//read data from ambient light sensor

	uint32_t ambient_value;
	float absolute_ambient = 0;
	int result = ADC_Poll(g_adc0_controller_fd, MT3620_ADC_CHANNEL0, &ambient_value);
	if (result < -1) {
		Log_Debug("ADC_Poll failed with error: %s (%d)\n", strerror(errno), errno);
		ExitApplication();
		return;
	}
	else {
		float voltage = (((float)ambient_value * ALS_PT19_MAX_VOLTAGE) / (float)((1 << g_ambient_sample_bit_count) - 1));

		Log_Debug("Voltage on ambient sensor: %.10f (was %.10f)\n", voltage, g_previous_state.ambient);
		absolute_ambient = abs((g_previous_state.ambient -voltage)*1000);
		Log_Debug("Ambient difference: %4.2f\n", absolute_ambient);

		g_previous_state.ambient = voltage;

		if (absolute_ambient > ABSOLUTE_AMBIENT_THRESHOLD && g_previous_state.is_ambient_set) {
			g_is_breach_detected = true;
		}

		g_previous_state.is_ambient_set = true;
	}

	//read data from microphone

	uint32_t microphone_value;
	float absolute_microphone = 0;
	int microphone_result = ADC_Poll(g_adc0_controller_fd, MT3620_ADC_CHANNEL1, &microphone_value);
	if (microphone_result < -1) {
		Log_Debug("ADC_Poll failed with error: %s (%d)\n", strerror(errno), errno);
		ExitApplication();
		return;
	}
	else {
		float voltage = (((float)microphone_value * MICROPHONE_MAX_VOLTAGE) / (float)((1 << g_microphone_sample_bit_count) - 1));

		Log_Debug("Voltage on microphone: %.10f\n", voltage);
		absolute_microphone = abs((g_previous_state.microphone - voltage) * 1000);
		Log_Debug("Microphone difference: %4.2f\n", absolute_microphone);

		g_previous_state.microphone = voltage;

		if (absolute_microphone > ABSOLUTE_MICROPHONE_THRESHOLD && g_previous_state.is_microphone_set) {
			g_is_breach_detected = true;
		}

		g_previous_state.is_microphone_set = true;
	}


	if (g_is_breach_detected) {
		Log_Debug("------------SECURITY BREACH DETECTED!!!--------\n");
		g_is_armed = false;
		setNewLEDStateRGB(false, false, false);

		char payload_to_send[JSON_BUFFER_SIZE] = { 0 };
		const char* payload_msg_template = "{ \"_t\": \"%s\", \"aAc\": \"%4.2f\", \"aAr\": \"%4.2f\", \"aAl\": \"%4.2f\", \"aMic\": \"%4.2f\" }";
		int len = snprintf(payload_to_send, sizeof(payload_to_send), payload_msg_template, "alarm", absolute_accel, absolute_angular, absolute_ambient, absolute_microphone);
		if (len < 0)
			return;
		Log_Debug("Sending message to IoT Hub.\n");
		SendMessageToIoTHub(payload_to_send);

	}
}


/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
int InitPeripheralsAndHandlers(void)
{
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = ExitApplication;
	sigaction(SIGTERM, &action, NULL);

	epollFd = CreateEpollFd();
	if (epollFd < 0) {
		return -1;
	}

	if (initI2c() == -1 || initUserLED() == -1 || initButtonA() == -1 || InitAmbientSensor() == -1 || InitMicrophone() == -1) {
		return -1;
	}
	
	//turn off the user LED
	setNewLEDStateRGB(false, false, false);

	// Check every x milliseconds button status
	struct timespec buttonPressCheckPeriod = { 0, 100000000 };
	g_button_a_timer_fd =
		CreateTimerFdAndAddToEpoll(epollFd, &buttonPressCheckPeriod, &buttonEventData, EPOLLIN);
	if (g_button_a_timer_fd < 0) {
		return -1;
	}

	struct timespec sensorsReadPeriod = { .tv_sec = READ_SENSORS_EVERY_SECONDS,.tv_nsec = READ_SENSORS_EVERY_NANO_SECONDS };
	static EventData sensorsEventData = { .eventHandler = &GetAndProcessDataFromSensors };

	g_read_sensors_timer_fd = CreateTimerFdAndAddToEpoll(epollFd, &sensorsReadPeriod, &sensorsEventData, EPOLLIN);
	if (g_read_sensors_timer_fd < 0) {
		return -1;
	}

	g_iot_hub_current_poll_period_seconds = g_iot_hub_default_poll_period_seconds;
	struct timespec azureTelemetryPeriod = { g_iot_hub_current_poll_period_seconds, 0 };

	g_azure_iot_sync_timer_fd = CreateTimerFdAndAddToEpoll(epollFd, &azureTelemetryPeriod, &azureEventData, EPOLLIN);
	if (g_azure_iot_sync_timer_fd < 0) {
		return -1;
	}

	return 0;
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
	Log_Debug("Closing file descriptors.\n");

	closeI2c();
	CloseFdAndPrintError(epollFd, "Epoll");
	CloseFdAndPrintError(g_button_a_timer_fd, "buttonPoll");
	CloseFdAndPrintError(g_button_a_gpio_fd, "buttonA");

}

/// <summary>
/// Main entry point for this application.
/// </summary>
int main(int argc, char* argv[])
{
	char ssid[128];
	uint32_t frequency;
	char bssid[20];

	// Clear the ssid array
	memset(ssid, 0, 128);

	if (argc == 2) {
		strncpy(scopeId, argv[1], SCOPEID_LENGTH);
	}
	else {
		Log_Debug("----- Set up the ScopeId in CmdArgs in app_manifest.json. -----\n\n\n");
		return -1;
	}

	// if init of some peripheral failed, exit app
	if (InitPeripheralsAndHandlers() != 0) {
		ExitApplication();
	}

	// Use epoll to wait for events and trigger handlers, until an error or SIGTERM happens
	while (!g_is_app_exit_requested) {
		if (WaitForEventAndCallHandler(epollFd) != 0) {
			ExitApplication();
		}

		WifiConfig_ConnectedNetwork network;
		int result = WifiConfig_GetCurrentNetwork(&network);
		if (result < 0) {
			Log_Debug("Not connected to WiFi network.\n");
		}
		else {

			frequency = network.frequencyMHz;
			snprintf(bssid, JSON_BUFFER_SIZE, "%02x:%02x:%02x:%02x:%02x:%02x",
				network.bssid[0], network.bssid[1], network.bssid[2],
				network.bssid[3], network.bssid[4], network.bssid[5]);

			if ((strncmp(ssid, (char*)&network.ssid, network.ssidLength) != 0) || !g_is_wifi_connected) {

				memset(ssid, 0, 128);
				strncpy(ssid, network.ssid, network.ssidLength);
				Log_Debug("Connected to Wi-Fi network '%s'\n", ssid);
				g_is_wifi_connected = true;
			}

		}
	}
	//program failed, set up LED to permanent red color
	setNewLEDStateRGB(true, false, false);

	ClosePeripheralsAndHandlers();
	Log_Debug("Application exiting.\n");
	return 0;
}
