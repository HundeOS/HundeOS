#include "auth.h"

#include "syscall.h"
#include "syscalls/calls.h"

AuthResult auth(ConstBStringPtr username, ConstBStringPtr password)
{
    AuthRequest request = AuthRequest__new(username, password);
    return (AuthResult)syscall(SYSCALL_AUTH, (Anything)&request, 0, 0, 0, 0);
}
