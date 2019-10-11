#pragma once

#include <stdbool.h>
#include "epoll_timerfd_utilities.h"

bool initButtonA(void);
void ButtonATimerEventHandler(EventData* eventData);