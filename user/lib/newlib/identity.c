#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include "../sys/process.h"

static Result(ProcessCredentials) PosixIdentity__current_credentials(void)
{
    return process_credentials(process_current_pid());
}

pid_t getpid(void)
{
    return (pid_t)process_current_pid();
}

pid_t getppid(void)
{
    match_result(
        process_identity(),
        success(identity)
        {
            return (pid_t)identity.parent_pid;
        },
        error(code, message)
        {
            errno = ESRCH;
            return (pid_t)-1;
        }
    );
}

static int PosixIdentity__group_error(ProcessGroupResult result)
{
    if (result == PROCESS_GROUP_PROCESS_NOT_FOUND)
    {
        errno = ESRCH;
    }
    else if (result == PROCESS_GROUP_PERMISSION_DENIED)
    {
        errno = EPERM;
    }
    else
    {
        errno = EINVAL;
    }
    return -1;
}

pid_t getpgid(pid_t pid)
{
    ProcessGroupRequest request = {
        .operation = PROCESS_GROUP_GET,
        .pid = pid,
    };
    ProcessGroupResult result = process_group_control(&request);
    if (result != PROCESS_GROUP_OK)
    {
        return (pid_t)PosixIdentity__group_error(result);
    }
    return (pid_t)request.result_id;
}

pid_t getpgrp(void)
{
    return getpgid(0);
}

int setpgid(pid_t pid, pid_t process_group_id)
{
    ProcessGroupRequest request = {
        .operation = PROCESS_GROUP_SET,
        .pid = pid,
        .process_group_id = process_group_id,
    };
    ProcessGroupResult result = process_group_control(&request);
    if (result != PROCESS_GROUP_OK)
    {
        return PosixIdentity__group_error(result);
    }
    return 0;
}

pid_t setsid(void)
{
    ProcessGroupRequest request = {
        .operation = PROCESS_GROUP_CREATE_SESSION,
    };
    ProcessGroupResult result = process_group_control(&request);
    if (result != PROCESS_GROUP_OK)
    {
        return (pid_t)PosixIdentity__group_error(result);
    }
    return (pid_t)request.result_id;
}

uid_t getuid(void)
{
    match_result(
        PosixIdentity__current_credentials(),
        success(credentials)
        {
            return (uid_t)credentials.user_id;
        },
        error(code, message)
        {
            errno = ESRCH;
            return (uid_t)-1;
        }
    );
}

gid_t getgid(void)
{
    match_result(
        PosixIdentity__current_credentials(),
        success(credentials)
        {
            return (gid_t)credentials.group_id;
        },
        error(code, message)
        {
            errno = ESRCH;
            return (gid_t)-1;
        }
    );
}

uid_t geteuid(void)
{
    return getuid();
}

gid_t getegid(void)
{
    return getgid();
}

int setuid(uid_t user)
{
    match_result(
        PosixIdentity__current_credentials(),
        success(credentials)
        {
            if (user == credentials.user_id)
            {
                return 0;
            }
            errno = EPERM;
            return -1;
        },
        error(code, message)
        {
            errno = ESRCH;
            return -1;
        }
    );
}

int setgid(gid_t group)
{
    match_result(
        PosixIdentity__current_credentials(),
        success(credentials)
        {
            if (group == credentials.group_id)
            {
                return 0;
            }
            errno = EPERM;
            return -1;
        },
        error(code, message)
        {
            errno = ESRCH;
            return -1;
        }
    );
}

static const char * PosixIdentity__user(uid_t user)
{
    switch (user)
    {
    case PROCESS_USER_ROOT:
        return "root";
    case PROCESS_USER_HUND:
        return "hund";
    case PROCESS_USER_USER:
        return "user";
    default:
        return "unknown";
    }
}

static const char * PosixIdentity__group(gid_t group)
{
    switch (group)
    {
    case PROCESS_GROUP_ROOT:
        return "root";
    case PROCESS_GROUP_HUND:
        return "hund";
    case PROCESS_GROUP_USER:
        return "user";
    default:
        return "unknown";
    }
}

static size_t group_iterator;
static char * empty_group_members[] = {NULL};

static struct group builtin_groups[] = {
    {.gr_name = "root", .gr_passwd = "", .gr_gid = PROCESS_GROUP_ROOT, .gr_mem = empty_group_members},
    {.gr_name = "hund", .gr_passwd = "", .gr_gid = PROCESS_GROUP_HUND, .gr_mem = empty_group_members},
    {.gr_name = "user", .gr_passwd = "", .gr_gid = PROCESS_GROUP_USER, .gr_mem = empty_group_members},
};

struct passwd * getpwuid(uid_t user)
{
    static struct passwd result;
    result = (struct passwd){
        .pw_name = (char *)PosixIdentity__user(user),
        .pw_uid = user,
        .pw_gid = user,
        .pw_dir = "/",
        .pw_shell = "/usr/local/bin/bash",
    };
    return &result;
}

struct group * getgrgid(gid_t group)
{
    static struct group result;
    result = (struct group){
        .gr_name = (char *)PosixIdentity__group(group),
        .gr_passwd = "",
        .gr_gid = group,
        .gr_mem = empty_group_members,
    };
    return &result;
}

void setgrent(void)
{
    group_iterator = 0;
}

struct group * getgrent(void)
{
    if (group_iterator >= sizeof(builtin_groups) / sizeof(builtin_groups[0]))
    {
        return NULL;
    }
    return &builtin_groups[group_iterator++];
}

void endgrent(void)
{
    group_iterator = sizeof(builtin_groups) / sizeof(builtin_groups[0]);
}

char * user_from_uid(uid_t user, int nouser)
{
    return (char *)PosixIdentity__user(user);
}

char * group_from_gid(gid_t group, int nogroup)
{
    return (char *)PosixIdentity__group(group);
}
