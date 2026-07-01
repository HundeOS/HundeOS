#ifndef __USER_SYS_SYSLOG_H
#define __USER_SYS_SYSLOG_H

#include "syscalls/syslog.h"

SyslogResult kernel_syslog(const char * message);
SyslogResult kernel_syslogf(const char * format, ...);
SyslogResult kernel_syslog_tag(const char * tag, const char * message);
SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...);

#endif
