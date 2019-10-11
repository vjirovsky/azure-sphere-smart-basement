#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>

#include <applibs/gpio.h>

#include "mt3620_avnet_dev.h"

extern GPIO_Value_Type userLEDRedState = GPIO_Value_Low;
extern GPIO_Value_Type userLEDGreenState = GPIO_Value_Low;
extern GPIO_Value_Type userLEDBlueState = GPIO_Value_Low;


extern int userLEDRedFd = -1;
extern int userLEDGreenFd = -1;
extern int userLEDBlueFd = -1;

/// <summary>
/// Init GPIOs for LED
/// </summary>
int initUserLED()
{
	userLEDRedFd = GPIO_OpenAsOutput(MT3620_GPIO8, GPIO_OutputMode_PushPull, userLEDRedState);
	userLEDGreenFd = GPIO_OpenAsOutput(MT3620_GPIO9, GPIO_OutputMode_PushPull, userLEDGreenState);
	userLEDBlueFd = GPIO_OpenAsOutput(MT3620_GPIO10, GPIO_OutputMode_PushPull, userLEDBlueState);

	return 0;
}

/// <summary>
///     Set the Device Control LED's status.
/// </summary>
/// <param name="state">The LED status to be set.</param>
void setNewLEDStateRGB(bool isRedOn, bool isGreenOn, bool isBlueOn)
{
	GPIO_SetValue(userLEDRedFd, (isRedOn ? GPIO_Value_Low : GPIO_Value_High));
	GPIO_SetValue(userLEDGreenFd, (isGreenOn ? GPIO_Value_Low : GPIO_Value_High));
	GPIO_SetValue(userLEDBlueFd, (isBlueOn ? GPIO_Value_Low : GPIO_Value_High));
}