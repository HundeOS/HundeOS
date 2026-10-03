#include "devfs.h"
#include "memory.h"

static Result(FilesystemFileSize)
    Devfs__load(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data)
{
    ConstDeviceRegistryPtr registry = context;
    BString absolute = BString__sprintf(&bstr("/dev%B"), path);
    match_result(
        DeviceRegistry__find(registry, &absolute),
        success(found)
        {
            match_optional(
                found,
                some(record)
                {
                    if (record.kind == DEVICE_KERNEL_STREAM)
                    {
                        return Error(FilesystemFileSize, bstr("Kernel stream"), IO_PATH_UNMANAGED);
                    }
                    BString text =
                        BString__sprintf(&bstr("path=%B\nblock_size=%llu\nblock_count=%llu\nuserspace_service=1\n"),
                                         &record.path, record.block_size, record.block_count);
                    Mem__memcpy(data, BString__cstr(&text), BString__length(&text));
                    return Success(FilesystemFileSize, BString__length(&text));
                },
                none()
                {
                    return Error(FilesystemFileSize, bstr("No device"), IO_INVALID_PATH);
                }
            );
        },
        error(code, message)
        {
            return Error(FilesystemFileSize, message,
                         code == DEVICE_REGISTRY_BUSY ? IO_WAIT_FOR_DATA : IO_NO_DEVICE_FOUND);
        }
    );
}

static Result(DirectoryEntryCount) Devfs__list(IOServiceContext context, ConstBStringPtr path,
                                               DirectoryEntryPtr entries, size_t capacity, uint64_t skip)
{
    if (!BString__is_empty(path) && !BString__equals(path, &bstr("/")))
    {
        return Error(DirectoryEntryCount, bstr("Invalid device directory"), IO_INVALID_PATH);
    }
    ConstDeviceRegistryPtr registry = context;
    DirectoryEntryCount count = 0;
    if (capacity == 0)
    {
        return Success(DirectoryEntryCount, count);
    }
    for (uint64_t index = 0; index < DEVICE_REGISTRY_CAPACITY; index++)
    {
        match_result(
            DeviceRegistry__at(registry, index),
            success(record)
            {
                if (record.kind == DEVICE_ABSENT)
                {
                    continue;
                }
                for (unsigned name_index = 0; name_index < 2; name_index++)
                {
                    ConstBStringPtr device_path = name_index == 0 ? &record.path : &record.alias;
                    if (BString__is_empty(device_path))
                    {
                        continue;
                    }
                    if (skip > 0)
                    {
                        skip--;
                    }
                    else
                    {
                        BString name = BString__slice(device_path, 5, BString__length(device_path));
                        entries[count++] = DirectoryEntry__new(0, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT,
                                                               IO_MODE_TYPE_FILE | 0444, &name);
                        if ((size_t)count == capacity)
                        {
                            return Success(DirectoryEntryCount, count);
                        }
                    }
                }
            },
            error(code, message)
            {
                return Error(DirectoryEntryCount, message,
                             code == DEVICE_REGISTRY_BUSY ? IO_WAIT_FOR_DATA : IO_NO_DEVICE_FOUND);
            }
        );
    }
    return Success(DirectoryEntryCount, count);
}

static IOStatus Devfs__store(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data,
                             FilesystemFileSize size)
{
    return IO_PERMISSION_DENIED;
}

static IOStatus Devfs__mkdir(IOServiceContext context, ConstBStringPtr path)
{
    return IO_PERMISSION_DENIED;
}

static IOStatus Devfs__remove(IOServiceContext context, ConstBStringPtr path, Flags flags)
{
    return IO_PERMISSION_DENIED;
}

FilesystemOperations Devfs__operations(void)
{
    return FilesystemOperations__new_snapshot(Devfs__load, Devfs__store, Devfs__list, Devfs__mkdir, Devfs__remove,
                                              FilesystemOperations__unsupported_link, 0);
}
