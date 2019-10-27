#pragma once

#include "epoll_timerfd_utilities.h"

int main(int argc, char* argv[]);
static void ExitApplication();
static int InitPeripheralsAndHandlers(void);
static void ClosePeripheralsAndHandlers(void);

extern void AzureTimerEventHandler(EventData* eventData);
