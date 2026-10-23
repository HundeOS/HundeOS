#include <errno.h>
#include <unistd.h>

#include "../sys/process.h"
#include "posix_io.h"

static int Fork__set_errno(ProcessForkResult result)
{
    switch (result)
    {
    case PROCESS_FORK_NO_MEMORY:
        errno = ENOMEM;
        break;
    case PROCESS_FORK_UNSUPPORTED_STATE:
        errno = EOPNOTSUPP;
        break;
    case PROCESS_FORK_INVALID_CALLER:
        errno = ESRCH;
        break;
    default:
        errno = EAGAIN;
        break;
    }
    return -1;
}

pid_t fork(void)
{
    IOStatus inheritance_status = PosixIO__prepare_fork();
    if (inheritance_status != IO_OK)
    {
        errno = inheritance_status == IO_OPERATION_NOT_SUPPORTED ? EOPNOTSUPP : EIO;
        return (pid_t)-1;
    }
    ProcessForkResult result = process_fork();
    if (result < 0)
    {
        PosixIO__abort_fork();
        return (pid_t)Fork__set_errno(result);
    }
    if (result == 0)
    {
        match_result(
            process_identity(),
            success(identity)
            {
                process_set_startup_pid(identity.pid);
                if (PosixIO__claim_fork() != IO_OK)
                {
                    _exit(127);
                }
                return 0;
            },
            error(code, message)
            {
                _exit(127);
            }
        );
        _exit(127);
    }
    if (PosixIO__commit_fork((uint64_t)result) != IO_OK)
    {
        process_fork_abort((uint64_t)result);
        PosixIO__abort_fork();
        errno = EIO;
        return (pid_t)-1;
    }
    ProcessForkResult resumed = process_fork_resume((uint64_t)result);
    if (resumed < 0)
    {
        process_fork_abort((uint64_t)result);
        PosixIO__abort_fork();
        return (pid_t)Fork__set_errno(resumed);
    }
    PosixIO__finish_fork();
    return (pid_t)result;
}
