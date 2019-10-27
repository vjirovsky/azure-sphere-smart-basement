#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>

#include <applibs/log.h>
#include <applibs/gpio.h>

#include "general_helper.h"
#include "led_helper.h"
#include "mt3620_avnet_dev.h"
#include "epoll_timerfd_utilities.h"
#include "build_options.h"

int g_button_a_gpio_fd = -1;
int g_button_a_timer_fd = -1;

extern sig_atomic_t g_is_app_exit_requested;
extern bool g_is_armed;
extern bool g_is_breach_detected;


extern sensors_state g_previous_state;

// Button state variables, initilize them to button not-pressed (High)
static GPIO_Value_Type buttonAState = GPIO_Value_High;

/// <summary>
/// Init GPIO for button A
/// </summary>
int initButtonA()
{
	g_button_a_gpio_fd = GPIO_OpenAsInput(MT3620_RDB_BUTTON_A);
	if (g_button_a_gpio_fd < 0) {
		Log_Debug("ERROR - Could not open buttonA GPIO: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	return 0;
}



/// <summary>
///     Handle button timer event: if the button is pressed, report the event to the IoT Hub.
/// </summary>
extern void ButtonATimerEventHandler(EventData* eventData)
{

	bool is_button_a_to_perform_operation = false;

	if (ConsumeTimerFdEvent(g_button_a_timer_fd) != 0) {
		g_is_app_exit_requested = true;
		return;
	}


	// Check for button A press
	GPIO_Value_Type newButtonAState;
	int result = GPIO_GetValue(g_button_a_gpio_fd, &newButtonAState);
	if (result != 0) {
		Log_Debug("ERROR - Could not read buttonA GPIO: %s (%d).\n", strerror(errno), errno);
		g_is_app_exit_requested = true;
		return;
	}

	// If the A button has just been pressed, send a telemetry message
	// The button has GPIO_Value_Low when pressed and GPIO_Value_High when released
	if (newButtonAState != buttonAState) {
		if (newButtonAState == GPIO_Value_Low) {
			if (g_is_armed) {
				// is armed already
			}
			else {
				Log_Debug("Arming room...waiting %d seconds to leave the room\n", WAIT_SECONDS_BEFORE_ARMING);
				is_button_a_to_perform_operation = true;
			}
		}

		// Update the static variable to use next time we enter this routine
		buttonAState = newButtonAState;
	}


	if (is_button_a_to_perform_operation) {
		for (int i = 0; i < WAIT_SECONDS_BEFORE_ARMING; i++) {
			setNewLEDStateRGB(false, false, true);
			sleep_for(500000000);
			setNewLEDStateRGB(false, false, false);
			sleep_for(500000000);
		}
		//reset previous alarm
		g_is_breach_detected = false;

		//reset previous values from sensor
		g_previous_state.is_acceleration_set = false;
		g_previous_state.is_angular_rate_set = false;
		g_previous_state.is_ambient_set = false;
		g_previous_state.is_microphone_set = false;

		//turn on armed state
		g_is_armed = true;

		setNewLEDStateRGB(false, false, false);

		is_button_a_to_perform_operation = false;
	}
}
