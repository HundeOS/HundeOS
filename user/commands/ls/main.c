#include "../../user.h"
#include "sys/io.h"

#define MAX_ENTRIES 32

#ifndef LS_ENTRYPOINT
#define LS_ENTRYPOINT main
#endif

typedef struct LSOptions
{
    bool show_all;
    bool long_format;
    Optional(BString) path_arg;
} LSOptions;

typedef LSOptions * LSOptionsPtr;

static LSOptions LSOptions__new(void)
{
    return (LSOptions){
        .show_all = false,
        .long_format = false,
        .path_arg = None(BString),
    };
}

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

static BString LS__mode_string(FileMode mode)
{
    BString result = empty_bstr();
    BString__push_char(&result, (mode & IO_MODE_TYPE_MASK) == IO_MODE_TYPE_DIR ? 'd' : '-');
    BString__push_char(&result, (mode & IO_MODE_OWNER_READ) != 0 ? 'r' : '-');
    BString__push_char(&result, (mode & IO_MODE_OWNER_WRITE) != 0 ? 'w' : '-');
    BString__push_char(&result, (mode & IO_MODE_OWNER_EXEC) != 0 ? 'x' : '-');
    BString__push_char(&result, (mode & IO_MODE_GROUP_READ) != 0 ? 'r' : '-');
    BString__push_char(&result, (mode & IO_MODE_GROUP_WRITE) != 0 ? 'w' : '-');
    BString__push_char(&result, (mode & IO_MODE_GROUP_EXEC) != 0 ? 'x' : '-');
    BString__push_char(&result, (mode & IO_MODE_OTHER_READ) != 0 ? 'r' : '-');
    BString__push_char(&result, (mode & IO_MODE_OTHER_WRITE) != 0 ? 'w' : '-');
    BString__push_char(&result, (mode & IO_MODE_OTHER_EXEC) != 0 ? 'x' : '-');
    return result;
}

static bool LS__is_hidden(DirectoryEntryPtr entry)
{
    return (entry->attributes & IO_DIRENT_ATTR_HIDDEN) != 0 || entry->name[0] == '.';
}

static void LS__print_entry(DirectoryEntryPtr entry, LSOptionsPtr options)
{
    if (!options->show_all && LS__is_hidden(entry))
    {
        return;
    }

    if (!options->long_format)
    {
        BString name = bstring(entry->name);
        IO__print(&name);
        IO__print(&bstr("\n"));
        return;
    }

    BString mode = LS__mode_string(entry->mode);
    IO__printf(&bstr("%B %s %s %s\n"), &mode, LS__user_name(entry->user_id), LS__group_name(entry->group_id),
               entry->name);
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

static bool LS__parse_options(int argc, char ** argv, LSOptionsPtr options)
{
    *options = LSOptions__new();

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

            IO__printf(&bstr("ls: unknown option %s\n"), arg);
            return false;
        }

        match_optional(
            options->path_arg,
            some(path_arg)
            {
                IO__printf(&bstr("ls: too many paths\n"));
                return false;
            },
            none_do_nothing()
        );

        options->path_arg = Some(BString, BString__new(arg));
    }

    return true;
}

static BString LS__path_from_options(LSOptionsPtr options)
{
    match_optional(options->path_arg,
        some(path_arg)
        {
            return IO__path_from_arg(&path_arg);
        },
        none_do_nothing()
    );

    BString root = bstr("/");
    return root;
}

int LS_ENTRYPOINT(int argc, char ** argv)
{
    LSOptions options;
    if (!LS__parse_options(argc, argv, &options))
    {
        return 1;
    }

    BString path = LS__path_from_options(&options);
    DirectoryEntry entries[MAX_ENTRIES];
    DirectoryEntryCount count = IO__list_dir(&path, entries, MAX_ENTRIES);

    if (count < 0)
    {
        IO__printf(&bstr("ls: error %s (%i)\n"), io_status_string((IOStatus)count), (int)count);
        return 1;
    }

    for (DirectoryEntryCount i = 0; i < count; i++)
    {
        LS__print_entry(&entries[i], &options);
    }

    return 0;
}
