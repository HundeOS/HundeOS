#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

#include "../sys/io.h"

enum
{
    POSIX_DIRECTORY_ENTRY_CAPACITY = 128,
};

struct DIR
{
    DirectoryEntry entries[POSIX_DIRECTORY_ENTRY_CAPACITY];
    DirectoryEntryCount count;
    DirectoryEntryCount index;
    struct dirent current;
};

DIR * opendir(const char * path)
{
    DIR * directory = malloc(sizeof(*directory));
    if (directory == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }
    BString argument = BString__new(path);
    BString absolute = IO__path_from_arg(&argument);
    directory->count = IO__list_dir(&absolute, directory->entries, POSIX_DIRECTORY_ENTRY_CAPACITY);
    directory->index = 0;
    if (directory->count < 0)
    {
        free(directory);
        errno = ENOENT;
        return NULL;
    }
    return directory;
}

struct dirent * readdir(DIR * directory)
{
    if (directory == NULL || directory->index >= directory->count)
    {
        return NULL;
    }
    DirectoryEntry * entry = &directory->entries[directory->index++];
    directory->current = (struct dirent){
        .d_ino = directory->index,
        .d_type = (entry->mode & IO_MODE_TYPE_MASK) == IO_MODE_TYPE_DIR ? DT_DIR : DT_REG,
    };
    BString name = BString__from_chars(entry->name, (StringPos)entry->name_len);
    BString__copy_to(&name, directory->current.d_name, sizeof(directory->current.d_name));
    return &directory->current;
}

int closedir(DIR * directory)
{
    if (directory == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    free(directory);
    return 0;
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

struct passwd * getpwuid(uid_t user)
{
    static struct passwd result;
    result = (struct passwd){.pw_name = (char *)PosixIdentity__user(user), .pw_uid = user, .pw_gid = user};
    return &result;
}

struct group * getgrgid(gid_t group)
{
    static struct group result;
    result = (struct group){.gr_name = (char *)PosixIdentity__group(group), .gr_gid = group};
    return &result;
}

const char * user_from_uid(uid_t user, int nouser)
{
    return PosixIdentity__user(user);
}

const char * group_from_gid(gid_t group, int nogroup)
{
    return PosixIdentity__group(group);
}

char * getbsize(int * header_length, long * block_size)
{
    static char header[] = "512-blocks";
    if (header_length != NULL)
    {
        *header_length = 10;
    }
    if (block_size != NULL)
    {
        *block_size = 512;
    }
    return header;
}

void strmode(mode_t mode, char * output)
{
    output[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : S_ISCHR(mode) ? 'c' : S_ISBLK(mode) ? 'b' : '-';
    const mode_t bits[9] = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};
    const char labels[9] = {'r', 'w', 'x', 'r', 'w', 'x', 'r', 'w', 'x'};
    for (size_t index = 0; index < 9; index++)
    {
        output[index + 1] = (mode & bits[index]) != 0 ? labels[index] : '-';
    }
    output[10] = ' ';
    output[11] = '\0';
}

char * fflagstostr(unsigned long flags)
{
    char * result = malloc(1);
    if (result != NULL)
    {
        result[0] = '\0';
    }
    return result;
}

int fmt_scaled(long long number, char * output)
{
    return snprintf(output, FMT_SCALED_STRSIZE, "%lld", number) < 0 ? -1 : 0;
}

int ioctl(int descriptor, unsigned long request, ...)
{
    errno = ENOSYS;
    return -1;
}

char * setlocale(int category, const char * locale)
{
    static char name[] = "C";
    return name;
}

time_t time(time_t * value)
{
    time_t now = 0;
    if (value != NULL)
    {
        *value = now;
    }
    return now;
}

struct tm * localtime(const time_t * value)
{
    static struct tm result = {.tm_mday = 1, .tm_year = 70};
    return &result;
}

size_t strftime(char * buffer, size_t size, const char * format, const struct tm * value)
{
    static const char timestamp[] = "Jan  1  1970";
    return strlcpy(buffer, timestamp, size) < size ? sizeof(timestamp) - 1 : 0;
}

int statfs(const char * path, struct statfs * status)
{
    if (status == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    status->f_iosize = 1024;
    strlcpy(status->f_mntonname, "/", sizeof(status->f_mntonname));
    return 0;
}

int fstatfs(int descriptor, struct statfs * status)
{
    struct stat file_status;
    if (fstat(descriptor, &file_status) != 0)
    {
        return -1;
    }
    return statfs("/", status);
}
