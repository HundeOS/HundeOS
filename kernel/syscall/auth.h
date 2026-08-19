#ifndef __KERNEL_SYSCALL_AUTH_H
#define __KERNEL_SYSCALL_AUTH_H

#include "syscall_dispatcher.h"
#include "syscalls/auth.h"
#include "../types/fixed_array.h"

#define MAX_AUTH_TICKETS 64

typedef struct AuthTicket
{
    AuthTicketID id;
    ProcessID issued_to_pid;
    UserID user_id;
    bool consumed;
} AuthTicket;

typedef AuthTicket * AuthTicketPtr;

OptionalDef(AuthTicket);
bool AuthTicket__equals(void * a, void * b);

FixedArrayDefine(AuthTicket, MAX_AUTH_TICKETS, AuthTickets, &AuthTicket__equals);

typedef struct Auth
{
    mutex_t lock;
    AuthTicketID next_ticket_id;
    AuthTickets tickets;
} Auth;

typedef Auth * AuthPtr;

ResultDef(AuthTicketID);
ResultDef(UserID);

#define NEW_AUTH \
    (Auth) { \
        .lock = MUTEX_INIT, \
        .next_ticket_id = 1, \
        .tickets = EmptyArray(AuthTickets), \
    } \

Result(AuthTicketID) Auth__authenticate(AuthPtr self, ProcessPtr process, BString username, BString password);
Result(UserID) Auth__consume_ticket(AuthPtr self, ProcessPtr process, AuthTicketID ticket_id);

Result(SystemCallResult) Auth__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
);

#endif
