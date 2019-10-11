#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>

#include "general_helper.h"

/// <summary>
///     Sleep for given time
/// </summary>
void sleep_for(int delayTime) {
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = delayTime * 10000;
	nanosleep(&ts, NULL);
}

/// <summary>
///     Sleep for seconds
/// </summary>
void sleep_for_seconds(int seconds, int nano_seconds) {
	struct timespec ts;
	ts.tv_sec = seconds;
	ts.tv_nsec = nano_seconds;
	nanosleep(&ts, NULL);
}