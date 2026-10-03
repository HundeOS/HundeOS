#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "current_directory.h"
#include "exec_payload.h"
#include "posix_io.h"
#include "runtime.h"
#include "../sys/syscall.h"
#include "syscalls/spawn.h"

#define EXECVP_DEFAULT_PATH "/usr/local/bin"

static int Execve__errno(SpawnResult result)
{
    switch (result)
    {
    case SPAWN_NO_MEM:
        return ENOMEM;
    case SPAWN_NO_FILE:
    case SPAWN_NO_PATH:
        return ENOENT;
    case SPAWN_ARG_TOO_LONG:
        return E2BIG;
    default:
        return ENOEXEC;
    }
}

int execve(const char * path, char * const arguments[], char * const environment[])
{
    BString absolute;
    char executable[PATH_MAX];
    char current_directory[PATH_MAX];
    if (PosixPath__absolute(path, &absolute) != 0)
    {
        return -1;
    }
    if (BString__length(&absolute) >= PATH_MAX)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    BString__copy_to(&absolute, executable, PATH_MAX);
    if (getcwd(current_directory, sizeof(current_directory)) == NULL)
    {
        return -1;
    }
    ExecPayloadPtr payload = PosixIO__prepare_exec_payload(executable, current_directory, arguments, environment);
    if (payload == NULL)
    {
        return -1;
    }
    IOStatus inheritance_status = PosixIO__commit_exec();
    if (inheritance_status != IO_OK)
    {
        PosixIO__abort_exec();
        PosixExecPayload__dispose(payload);
        errno = EIO;
        return -1;
    }
    SpawnRequest request = SpawnRequest__overlay(payload);
    SpawnResult result = syscall(SYSCALL_EXEC_REQUEST, (Anything)&request, 0, 0, 0, 0);
    PosixIO__abort_exec();
    PosixExecPayload__dispose(payload);
    errno = Execve__errno(result);
    return -1;
}

static int Execvp__try_path_entry(const char * entry, size_t entry_length, const char * file, char * const arguments[])
{
    size_t file_length = strlen(file);
    if (entry_length == 0)
    {
        entry = ".";
        entry_length = 1;
    }
    if (entry_length > PATH_MAX || file_length >= PATH_MAX || entry_length + file_length + 2 > PATH_MAX)
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    char path[PATH_MAX];
    memcpy(path, entry, entry_length);
    path[entry_length] = '/';
    memcpy(path + entry_length + 1, file, file_length + 1);
    return execve(path, arguments, environ);
}

int execvp(const char * file, char * const arguments[])
{
    if (file == NULL || file[0] == '\0')
    {
        errno = ENOENT;
        return -1;
    }
    if (strchr(file, '/') != NULL)
    {
        return execve(file, arguments, environ);
    }

    const char * path = LibC__getenv("PATH");
    if (path == NULL)
    {
        path = EXECVP_DEFAULT_PATH;
    }
    int saved_error = ENOENT;
    const char * entry = path;
    while (true)
    {
        const char * separator = strchr(entry, ':');
        size_t entry_length = separator == NULL ? strlen(entry) : (size_t)(separator - entry);
        if (Execvp__try_path_entry(entry, entry_length, file, arguments) == -1 && errno != ENOENT)
        {
            saved_error = errno;
        }
        if (separator == NULL)
        {
            errno = saved_error;
            return -1;
        }
        entry = separator + 1;
    }
}
