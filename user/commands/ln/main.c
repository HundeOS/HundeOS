#include "../../user.h"
#include "../../sys/io.h"

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

int main(int argc, char ** argv)
{
    if (argc != 3)
    {
        print("ln: usage ln <source> <target>\n");
        return 1;
    }

    char source_path[512];
    char target_path[512];
    path_from_arg(argv[1], source_path, sizeof(source_path));
    path_from_arg(argv[2], target_path, sizeof(target_path));

    IOStatus status = link_file(source_path, target_path);
    if (status != IO_OK)
    {
        printf("ln: error %s (%i)\n", io_status_string(status), (int) status);
        return 1;
    }

    return 0;
}
