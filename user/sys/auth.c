#include "auth.h"

#include "syscall.h"
#include "syscalls/calls.h"

AuthResult auth(const char * username, const char * password)
{
    AuthRequest request = (AuthRequest) {
        .username = username,
        .password = password,
    };
    return (AuthResult) syscall(SYSCALL_AUTH, (Anything) &request, 0, 0, 0, 0);
}
