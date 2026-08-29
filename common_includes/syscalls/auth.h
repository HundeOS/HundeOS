#ifndef __SYSCALL_AUTH_H
#define __SYSCALL_AUTH_H

#include <stdint.h>

#include "types/bstring.h"

typedef uint64_t AuthTicketID;
typedef int64_t AuthResult;

#define AUTH_NO_TICKET 0

typedef enum AuthResults
{
    AUTH_INVALID_CREDENTIALS = -1,
    AUTH_NO_PROCESS = -2,
    AUTH_TICKET_STORE_FULL = -3,
    AUTH_INVALID_TICKET = -4,
    AUTH_TICKET_FOR_DIFFERENT_PROCESS = -5,
    AUTH_TICKET_CONSUMED = -6,
} AuthResults;

typedef struct AuthRequest
{
    BString username;
    BString password;
} AuthRequest;

typedef AuthRequest * AuthRequestPtr;

static inline AuthRequest AuthRequest__new(ConstBStringPtr username, ConstBStringPtr password)
{
    return (AuthRequest) {
        .username = *username,
        .password = *password,
    };
}

#endif
