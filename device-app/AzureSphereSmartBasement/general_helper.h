#pragma once

#include <stdbool.h>

void sleep_for(int delayTime);
void sleep_for_seconds(int seconds, int nano_seconds);


// previous states acceleration, angular rate, ambient light
typedef struct {
	bool is_acceleration_set;
	bool is_angular_rate_set;
	bool is_ambient_set;
	bool is_microphone_set;

	float acceleration_mg[3];
	float angular_rate_dps[3];
	float ambient;
	float microphone;

} sensors_state;