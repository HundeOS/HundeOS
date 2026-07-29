#include "../../user.h"
#include "../../sys/io.h"

#define PATH_BUFFER_SIZE 512
#define COPY_BUFFER_SIZE 512
#define DIR_ENTRY_BUFFER_SIZE 32

static void clear_buffer(char * buffer, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        buffer[i] = '\0';
    }
}

static size_t string_len(const char * value)
{
    size_t length = 0;
    while (value[length] != '\0')
    {
        length++;
    }

    return length;
}

static void path_from_arg(const char * arg, char * path, size_t path_size)
{
    clear_buffer(path, path_size);

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

static bool path_join(const char * base, const char * child, char * output, size_t output_size)
{
    clear_buffer(output, output_size);
    size_t position = 0;

    for (size_t i = 0; base[i] != '\0' && position < output_size - 1; i++)
    {
        output[position++] = base[i];
    }

    if (position == 0)
    {
        output[position++] = '/';
    }
    else if (output[position - 1] != '/' && position < output_size - 1)
    {
        output[position++] = '/';
    }

    for (size_t i = 0; child[i] != '\0' && position < output_size - 1; i++)
    {
        output[position++] = child[i];
    }

    output[position] = '\0';
    return child[0] == '\0' || position < output_size - 1;
}

static IOStatus Mv__move_path(const char * source_path, const char * target_path);

static IOStatus Mv__move_file(IOHandle source_handle, const char * source_path, const char * target_path)
{
    IOHandle target_handle = open(target_path, IO_OPEN_WRITE);
    if (target_handle < 0)
    {
        close(source_handle);
        return (IOStatus) target_handle;
    }

    char buffer[COPY_BUFFER_SIZE];
    while (true)
    {
        ByteCount read_bytes = read(source_handle, buffer, sizeof(buffer));
        if (read_bytes == IO_EOF || read_bytes == 0)
        {
            break;
        }
        if (read_bytes < 0)
        {
            close(target_handle);
            close(source_handle);
            return (IOStatus) read_bytes;
        }

        ByteCount written = write(target_handle, buffer, (size_t) read_bytes);
        if (written < 0)
        {
            close(target_handle);
            close(source_handle);
            return (IOStatus) written;
        }
    }

    close(target_handle);
    close(source_handle);
    return remove_file(source_path);
}

static IOStatus Mv__move_directory_entries(
    const char * source_path,
    const char * target_path,
    DirectoryEntryPtr entries,
    DirectoryEntryCount count
)
{
    IOStatus mkdir_status = make_dir(target_path);
    if (mkdir_status != IO_OK)
    {
        return mkdir_status;
    }

    for (DirectoryEntryCount i = 0; i < count; i++)
    {
        char child_source[PATH_BUFFER_SIZE];
        char child_target[PATH_BUFFER_SIZE];
        if (!path_join(source_path, entries[i].name, child_source, sizeof(child_source))
            || !path_join(target_path, entries[i].name, child_target, sizeof(child_target)))
        {
            return IO_INVALID_PATH;
        }

        IOStatus child_status = Mv__move_path(child_source, child_target);
        if (child_status != IO_OK)
        {
            return child_status;
        }
    }

    return remove_file_with_flags(source_path, IO_REMOVE_RECURSIVE);
}

static IOStatus Mv__move_path(const char * source_path, const char * target_path)
{
    IOHandle source_handle = open(source_path, IO_OPEN_READ);
    if (source_handle >= 0)
    {
        return Mv__move_file(source_handle, source_path, target_path);
    }

    DirectoryEntry entries[DIR_ENTRY_BUFFER_SIZE];
    DirectoryEntryCount count = list_dir(source_path, entries, DIR_ENTRY_BUFFER_SIZE);
    if (count >= 0)
    {
        return Mv__move_directory_entries(source_path, target_path, entries, count);
    }

    return (IOStatus) source_handle;
}

int main(int argc, char ** argv)
{
    if (argc != 3)
    {
        print("mv: usage mv <source> <target>\n");
        return 1;
    }

    char source_path[PATH_BUFFER_SIZE];
    char target_path[PATH_BUFFER_SIZE];
    path_from_arg(argv[1], source_path, sizeof(source_path));
    path_from_arg(argv[2], target_path, sizeof(target_path));

    if (string_len(source_path) == 0 || string_len(target_path) == 0)
    {
        print("mv: usage mv <source> <target>\n");
        return 1;
    }

    IOStatus status = Mv__move_path(source_path, target_path);
    if (status != IO_OK)
    {
        printf("mv: error %s (%i)\n", io_status_string(status), (int) status);
        return 1;
    }

    return 0;
}
