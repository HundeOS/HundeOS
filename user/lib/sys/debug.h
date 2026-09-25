#ifndef __USER_SYS_DEBUG_H
#define __USER_SYS_DEBUG_H

#include "trace/client.h"

#ifdef DRIVER_LOG_TAG
#define DRIVER_DEBUG(format, ...) DriverTrace__writef(DRIVER_LOG_TAG, format, ##__VA_ARGS__)
#else
#define DRIVER_DEBUG(format, ...) DriverTrace__writef("DRIVER", format, ##__VA_ARGS__)
#endif

#endif
