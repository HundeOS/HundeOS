#include "current_directory.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../sys/io.h"

static BString current_directory = BSTRING_NEW("/");

int PosixPath__absolute(const char * path, BStringPtr result)
{
    if (path == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0)
    {
        errno = ENOENT;
        return -1;
    }

    BString argument = BString__new(path);
    if (path[0] == '/')
    {
        if (length >= BSTRING_MAX_SIZE)
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        *result = IO__path_from_arg(&argument);
        return 0;
    }

    if ((size_t)BString__length(&current_directory) + length + 1 >= BSTRING_MAX_SIZE)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    BString joined = IO__path_join(&current_directory, &argument);
    *result = IO__path_from_arg(&joined);
    return 0;
}

void LibC__set_current_directory(const char * path)
{
    current_directory = path != NULL && path[0] == '/' ? BString__new(path) : bstr("/");
}

int chdir(const char * path)
{
    BString absolute;
    if (PosixPath__absolute(path, &absolute) != 0)
    {
        return -1;
    }

    match_result(
        IO__file_info(&absolute),
        success(info)
        {
            if ((info.mode & IO_MODE_TYPE_MASK) != IO_MODE_TYPE_DIR)
            {
                errno = ENOTDIR;
                return -1;
            }
            current_directory = absolute;
            return 0;
        },
        error(code, message)
        {
            errno = code == IO_PERMISSION_DENIED ? EACCES : ENOENT;
            return -1;
        }
    );
}

char * getcwd(char * buffer, size_t size)
{
    size_t length = (size_t)BString__length(&current_directory);
    if (buffer == NULL)
    {
        if (size != 0)
        {
            errno = EFAULT;
            return NULL;
        }
        buffer = malloc(length + 1);
        if (buffer == NULL)
        {
            errno = ENOMEM;
            return NULL;
        }
        size = length + 1;
    }
    if (size <= length)
    {
        errno = ERANGE;
        return NULL;
    }
    BString__copy_to(&current_directory, buffer, (StringPos)size);
    return buffer;
}
