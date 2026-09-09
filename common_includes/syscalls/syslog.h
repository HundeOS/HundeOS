#ifndef _COMMON_INCLUDES_SYSCALLS_SYSLOG_H
#define _COMMON_INCLUDES_SYSCALLS_SYSLOG_H

#include <stdint.h>

#include "types/bstring.h"

#define SYSLOG_MAX_MESSAGE 512
#define SYSLOG_MAX_TAG 32

typedef int64_t SyslogResult;

typedef enum SyslogResults
{
    SYSLOG_OK = 0,
    SYSLOG_INVALID_ARGUMENT = -1,
} SyslogResults;

typedef struct SyslogRequest
{
    BString message;
    Optional(BString) tag;
} SyslogRequest;

typedef SyslogRequest * SyslogRequestPtr;

static inline SyslogRequest SyslogRequest__new(ConstBStringPtr message, Optional(BString) tag)
{
    return (SyslogRequest){
        .message = *message,
        .tag = tag,
    };
}

#endif
