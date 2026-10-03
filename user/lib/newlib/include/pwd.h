#ifndef __USER_LIB_NEWLIB_PWD_H
#define __USER_LIB_NEWLIB_PWD_H

#include <sys/types.h>

struct passwd
{
    char * pw_name;
    uid_t pw_uid;
    gid_t pw_gid;
    char * pw_dir;
    char * pw_shell;
};

struct passwd * getpwuid(uid_t user);
char * user_from_uid(uid_t user, int nouser);

#endif
