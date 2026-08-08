#include "../../user.h"
#include "../../sys/io.h"

#define MAX_ENTRIES 32
#define PATH_BUFFER_SIZE 512

#ifndef LS_ENTRYPOINT
#define LS_ENTRYPOINT main
#endif

typedef struct LSOptions
{
    bool show_all;
    bool long_format;
    const char * path_arg;
} LSOptions;

static const char * LS__user_name(UserID user_id)
{
    switch (user_id)
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

static const char * LS__group_name(GroupID group_id)
{
    switch (group_id)
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

static void LS__mode_string(FileMode mode, char target[11])
{
    target[0] = (mode & IO_MODE_TYPE_MASK) == IO_MODE_TYPE_DIR ? 'd' : '-';
    target[1] = (mode & IO_MODE_OWNER_READ) != 0 ? 'r' : '-';
    target[2] = (mode & IO_MODE_OWNER_WRITE) != 0 ? 'w' : '-';
    target[3] = (mode & IO_MODE_OWNER_EXEC) != 0 ? 'x' : '-';
    target[4] = (mode & IO_MODE_GROUP_READ) != 0 ? 'r' : '-';
    target[5] = (mode & IO_MODE_GROUP_WRITE) != 0 ? 'w' : '-';
    target[6] = (mode & IO_MODE_GROUP_EXEC) != 0 ? 'x' : '-';
    target[7] = (mode & IO_MODE_OTHER_READ) != 0 ? 'r' : '-';
    target[8] = (mode & IO_MODE_OTHER_WRITE) != 0 ? 'w' : '-';
    target[9] = (mode & IO_MODE_OTHER_EXEC) != 0 ? 'x' : '-';
    target[10] = '\0';
}

static bool LS__is_hidden(DirectoryEntryPtr entry)
{
    return (entry->attributes & IO_DIRENT_ATTR_HIDDEN) != 0 || entry->name[0] == '.';
}

static void LS__print_entry(DirectoryEntryPtr entry, LSOptions options)
{
    if (!options.show_all && LS__is_hidden(entry))
    {
        return;
    }

    if (!options.long_format)
    {
        print(entry->name);
        print("\n");
        return;
    }

    char mode[11];
    LS__mode_string(entry->mode, mode);
    printf("%s %s %s %s\n", mode, LS__user_name(entry->user_id), LS__group_name(entry->group_id), entry->name);
}

static bool LS__matches_option(const char * arg, const char * option)
{
    uint64_t i = 0;
    while (arg[i] != '\0' && option[i] != '\0')
    {
        if (arg[i] != option[i])
        {
            return false;
        }
        i++;
    }

    return arg[i] == '\0' && option[i] == '\0';
}

static bool LS__parse_options(int argc, char ** argv, LSOptions * options)
{
    *options = (LSOptions) {
        .show_all = false,
        .long_format = false,
        .path_arg = NULL,
    };

    for (int i = 1; i < argc; i++)
    {
        const char * arg = argv[i];
        if (arg[0] == '-')
        {
            if (LS__matches_option(arg, "-a"))
            {
                options->show_all = true;
                continue;
            }

            if (LS__matches_option(arg, "-l"))
            {
                options->long_format = true;
                continue;
            }

            if (LS__matches_option(arg, "-la") || LS__matches_option(arg, "-al"))
            {
                options->show_all = true;
                options->long_format = true;
                continue;
            }

            printf("ls: unknown option %s\n", arg);
            return false;
        }

        if (options->path_arg != NULL)
        {
            printf("ls: too many paths\n");
            return false;
        }

        options->path_arg = arg;
    }

    return true;
}

static void path_from_arg(const char * arg, char * path, size_t path_size)
{
    for (size_t i = 0; i < path_size; i++)
    {
        path[i] = '\0';
    }

    if (arg == NULL || arg[0] == '\0')
    {
        path[0] = '/';
        return;
    }

    size_t position = 0;
    if (arg[0] != '/' && position < path_size - 1)
    {
        path[position++] = '/';
    }

    for (size_t i = 0; arg[i] != '\0' && position < path_size - 1; i++)
    {
        path[position++] = arg[i];
    }
}

int LS_ENTRYPOINT(int argc, char ** argv)
{
    LSOptions options;
    if (!LS__parse_options(argc, argv, &options))
    {
        return 1;
    }

    char path[PATH_BUFFER_SIZE];
    DirectoryEntry entries[MAX_ENTRIES];
    path_from_arg(options.path_arg, path, PATH_BUFFER_SIZE);
    DirectoryEntryCount count = list_dir(path, entries, MAX_ENTRIES);

    if (count < 0)
    {
        printf("ls: error %s (%i)\n", io_status_string((IOStatus) count), (int) count);
        return 1;
    }

    for (DirectoryEntryCount i = 0; i < count; i++)
    {
        LS__print_entry(&entries[i], options);
    }

    return 0;
}
