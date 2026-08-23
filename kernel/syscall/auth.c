#include "auth.h"

#include "../debug.h"
#include "../kernel_core.h"
#include "../mutex.h"

FixedArrayBodyDefine(AuthTickets);

static BString invalid_credentials_message = BSTRING_NEW("Invalid credentials");
static BString no_process_message = BSTRING_NEW("No process");
static BString ticket_store_full_message = BSTRING_NEW("Auth ticket store full");
static BString invalid_ticket_message = BSTRING_NEW("Invalid auth ticket");
static BString ticket_process_mismatch_message = BSTRING_NEW("Auth ticket belongs to different process");
static BString ticket_consumed_message = BSTRING_NEW("Auth ticket consumed");

typedef struct AuthCredential
{
    BString username;
    BString password;
    UserID user_id;
} AuthCredential;

static AuthCredential credentials[] = {
    { BSTRING_NEW("root"), BSTRING_NEW("root"), PROCESS_USER_ROOT },
    { BSTRING_NEW("hund"), BSTRING_NEW("hund"), PROCESS_USER_HUND },
    { BSTRING_NEW("user"), BSTRING_NEW("user"), PROCESS_USER_USER },
};

bool AuthTicket__equals(void * a, void * b)
{
    AuthTicketPtr left = a;
    AuthTicketPtr right = b;
    return left->id == right->id;
}

static Result(UserID) Auth__credential_user_id(BString username, BString password)
{
    uint64_t count = sizeof(credentials) / sizeof(credentials[0]);
    for (uint64_t index = 0; index < count; index++)
    {
        AuthCredential credential = credentials[index];
        if (
            BString__equals(&username, &credential.username)
            && BString__equals(&password, &credential.password)
        )
        {
            return Success(UserID, credential.user_id);
        }
    }

    return Error(UserID, invalid_credentials_message, AUTH_INVALID_CREDENTIALS);
}

static bool Auth__ticket_id_matches(AuthTicketPtr ticket, void * context)
{
    AuthTicketID ticket_id = *((AuthTicketID *) context);
    return ticket->id == ticket_id;
}

static AuthTicket AuthTicket__new(AuthTicketID ticket_id, ProcessID issued_to_pid, UserID user_id)
{
    AuthTicket ticket = {
        .id = ticket_id,
        .issued_to_pid = issued_to_pid,
        .user_id = user_id,
        .consumed = false,
    };
    return ticket;
}

Result(AuthTicketID) Auth__authenticate(AuthPtr self, ProcessPtr process, BString username, BString password)
{
    match_result(UserID, Auth__credential_user_id(username, password),
        success(user_id)
        {
            critical_section(self->lock,
            {
                AuthTicketID ticket_id = self->next_ticket_id;
                self->next_ticket_id++;

                AuthTicket ticket = AuthTicket__new(ticket_id, process->pid, user_id);

                match_result(CollectionCode, AuthTickets__push(&self->tickets, ticket),
                    success(_) { cs_return Success(AuthTicketID, ticket_id); },
                    error(code, message) { cs_return Error(AuthTicketID, ticket_store_full_message, AUTH_TICKET_STORE_FULL); }
                )
            });
        },
        error(code, message) { return Error(AuthTicketID, message, code); }
    )

    return Error(AuthTicketID, invalid_credentials_message, AUTH_INVALID_CREDENTIALS);
}

Result(UserID) Auth__consume_ticket(AuthPtr self, ProcessPtr process, AuthTicketID ticket_id)
{
    critical_section(self->lock,
    {
        match_optional(AuthTickets__FindResult, AuthTickets__find(&self->tickets, Auth__ticket_id_matches, &ticket_id),
            some(result)
            {
                AuthTicketPtr ticket = result.value;
                if (ticket->issued_to_pid != process->pid)
                {
                    cs_return Error(UserID, ticket_process_mismatch_message, AUTH_TICKET_FOR_DIFFERENT_PROCESS);
                }
                if (ticket->consumed)
                {
                    cs_return Error(UserID, ticket_consumed_message, AUTH_TICKET_CONSUMED);
                }

                ticket->consumed = true;
                cs_return Success(UserID, ticket->user_id);
            },
            none() { cs_return Error(UserID, invalid_ticket_message, AUTH_INVALID_TICKET); }
        )
    });

    return Error(UserID, invalid_ticket_message, AUTH_INVALID_TICKET);
}

Result(SystemCallResult) Auth__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_credentials_message, AUTH_INVALID_CREDENTIALS);
    }

    AuthRequestPtr request = (AuthRequestPtr) arg1;
    if (request->username == 0 || request->password == 0)
    {
        return Error(SystemCallResult, invalid_credentials_message, AUTH_INVALID_CREDENTIALS);
    }

    match_optional(ProcessPtr, proc,
        some(process)
        {
            match_result(AuthTicketID, Auth__authenticate(
                &kernel->auth,
                process,
                bstring(request->username),
                bstring(request->password)
            ),
                success(ticket_id) { return Success(SystemCallResult, ticket_id); },
                error(code, message) { return Error(SystemCallResult, message, code); }
            )
        },
        none() { return Error(SystemCallResult, no_process_message, AUTH_NO_PROCESS); }
    )

    return Error(SystemCallResult, no_process_message, AUTH_NO_PROCESS);
}
