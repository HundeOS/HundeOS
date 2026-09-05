#ifndef __USER_SYS_DEBUG_H
#define __USER_SYS_DEBUG_H

#include "syslog.h"

#ifdef DRIVER_LOG_TAG
#define DRIVER_DEBUG(format, ...) kernel_syslog_tagf(DRIVER_LOG_TAG, format, ##__VA_ARGS__)
#else
#define DRIVER_DEBUG(format, ...) kernel_syslogf(format, ##__VA_ARGS__)
#endif

#endif
