#include "../../user.h"
#include "sys/io.h"

#define DIR_ENTRY_BUFFER_SIZE 32

static IOStatus Mv__move_path(ConstBStringPtr source_path, ConstBStringPtr target_path);

static IOStatus Mv__move_file(IOHandle source_handle, ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    IOHandle target_handle = IO__open(target_path, IO_OPEN_WRITE);
    if (target_handle < 0)
    {
        close(source_handle);
        return (IOStatus)target_handle;
    }

    while (true)
    {
        BString buffer = empty_bstr();
        ByteCount read_bytes = IO__read(source_handle, &buffer);
        if (read_bytes == IO_EOF || read_bytes == 0)
        {
            break;
        }
        if (read_bytes < 0)
        {
            close(target_handle);
            close(source_handle);
            return (IOStatus)read_bytes;
        }

        ByteCount written = IO__write(target_handle, &buffer);
        if (written < 0)
        {
            close(target_handle);
            close(source_handle);
            return (IOStatus)written;
        }
    }

    close(target_handle);
    close(source_handle);
    return IO__remove_file(source_path);
}

static IOStatus Mv__move_directory_entries(ConstBStringPtr source_path, ConstBStringPtr target_path,
                                           DirectoryEntryPtr entries, DirectoryEntryCount count)
{
    IOStatus mkdir_status = IO__make_dir(target_path);
    if (mkdir_status != IO_OK)
    {
        return mkdir_status;
    }

    for (DirectoryEntryCount i = 0; i < count; i++)
    {
        BString child_name = BString__new(entries[i].name);
        BString child_source = IO__path_join(source_path, &child_name);
        BString child_target = IO__path_join(target_path, &child_name);

        IOStatus child_status = Mv__move_path(&child_source, &child_target);
        if (child_status != IO_OK)
        {
            return child_status;
        }
    }

    return IO__remove_file_with_flags(source_path, IO_REMOVE_RECURSIVE);
}

static IOStatus Mv__move_path(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    IOHandle source_handle = IO__open(source_path, IO_OPEN_READ);
    if (source_handle >= 0)
    {
        return Mv__move_file(source_handle, source_path, target_path);
    }

    DirectoryEntry entries[DIR_ENTRY_BUFFER_SIZE];
    DirectoryEntryCount count = IO__list_dir(source_path, entries, DIR_ENTRY_BUFFER_SIZE);
    if (count >= 0)
    {
        return Mv__move_directory_entries(source_path, target_path, entries, count);
    }

    return (IOStatus)source_handle;
}

int main(int argc, char ** argv)
{
    if (argc != 3)
    {
        IO__print(&bstr("mv: usage mv <source> <target>\n"));
        return 1;
    }

    BString source_arg = BString__new(argv[1]);
    BString target_arg = BString__new(argv[2]);
    BString source_path = IO__path_from_arg(&source_arg);
    BString target_path = IO__path_from_arg(&target_arg);

    IOStatus status = Mv__move_path(&source_path, &target_path);
    if (status != IO_OK)
    {
        IO__printf(&bstr("mv: error %s (%i)\n"), io_status_string(status), (int)status);
        return 1;
    }

    return 0;
}
