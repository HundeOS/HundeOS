#ifndef __USER_SYS_AUTH_H
#define __USER_SYS_AUTH_H

#include "syscalls/auth.h"

AuthResult auth(ConstBStringPtr username, ConstBStringPtr password);

#endif
