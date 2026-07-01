#include "../../user.h"
#include "../../sys/io.h"

#define MAX_ENTRIES 32
#define PATH_BUFFER_SIZE 512

#ifndef LS_ENTRYPOINT
#define LS_ENTRYPOINT main
#endif

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
    char path[PATH_BUFFER_SIZE];
    DirectoryEntry entries[MAX_ENTRIES];
    const char * arg = argc > 1 ? argv[1] : NULL;
    path_from_arg(arg, path, PATH_BUFFER_SIZE);
    DirectoryEntryCount count = list_dir(path, entries, MAX_ENTRIES);

    if (count < 0)
    {
        printf("ls: error %s (%i)\n", io_status_string((IOStatus) count), (int) count);
        return 1;
    }

    for (DirectoryEntryCount i = 0; i < count; i++)
    {
        print(entries[i].name);
        print("\n");
    }

    return 0;
}
