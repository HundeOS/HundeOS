#include "filesystem.h"

#include "sys/io.h"
#include "sys/mem.h"

static BString dependency_not_found = bstr("Shared object dependency not found");
static BString dependency_invalid = bstr("Invalid shared object dependency name");
static BString dependency_too_large = bstr("Shared object dependency is too large");
static BString dependency_read_failed = bstr("Unable to read shared object dependency");

static Result(WuffImage) WuffFilesystem__read(ConstBStringPtr path, ConstBStringPtr name)
{
    IOFileInfo info;
    match_result(
        IO__file_info(path),
        success(value)
        {
            info = value;
        },
        error(code, message)
        {
            return Error(WuffImage, dependency_not_found, WUFF_LOAD_DEPENDENCY_NOT_FOUND);
        }
    );
    if ((info.mode & IO_MODE_TYPE_MASK) != IO_MODE_TYPE_FILE || info.size == 0 || info.size > WUFF_MAX_IMAGE_SIZE ||
        info.size > SIZE_MAX)
    {
        return Error(WuffImage, dependency_too_large, WUFF_LOAD_INVALID_IMAGE);
    }
    uint8_t * data;
    match_result(
        mem_alloc_bytes((size_t)info.size),
        success(address)
        {
            data = (uint8_t *)address;
        },
        error(code, message)
        {
            return Error(WuffImage, dependency_read_failed, WUFF_LOAD_NO_MEMORY);
        }
    );
    IOHandle handle = IO__open(path, IO_OPEN_READ);
    if (handle < 0)
    {
        free(data);
        return Error(WuffImage, dependency_not_found, WUFF_LOAD_DEPENDENCY_NOT_FOUND);
    }
    size_t read = 0;
    bool failed = false;
    while (read < (size_t)info.size && !failed)
    {
        ByteCount count = IO__read_bytes(handle, (char *)data + read, (size_t)info.size - read);
        if (count <= 0 || (uint64_t)count > info.size - read)
        {
            failed = true;
        }
        else
        {
            read += (size_t)count;
        }
    }
    if (IO__close(handle) != IO_OK)
    {
        failed = true;
    }
    if (failed)
    {
        free(data);
        return Error(WuffImage, dependency_read_failed, WUFF_LOAD_IO_FAILED);
    }
    return Success(WuffImage, WuffImage__new(name, data, read));
}

static void WuffFilesystem__release(void * context, ConstWuffImagePtr image)
{
    WuffFilesystem__release_image(image);
}

static Result(WuffImage) WuffFilesystem__load(void * context, ConstBStringPtr name)
{
    if (BString__is_empty(name) || BString__contains_char(name, '/'))
    {
        return Error(WuffImage, dependency_invalid, WUFF_LOAD_DEPENDENCY_NOT_FOUND);
    }
    BString directory = bstr(WUFF_LIBRARY_DIRECTORY);
    if ((size_t)BString__length(&directory) + (size_t)BString__length(name) >= BSTRING_MAX_SIZE)
    {
        return Error(WuffImage, dependency_invalid, WUFF_LOAD_DEPENDENCY_NOT_FOUND);
    }
    BString path = BString__append(&directory, name);
    return WuffFilesystem__read(&path, name);
}

Result(WuffImage) WuffFilesystem__load_image(ConstBStringPtr path)
{
    if (BString__is_empty(path))
    {
        return Error(WuffImage, dependency_invalid, WUFF_LOAD_INVALID_IMAGE);
    }
    return WuffFilesystem__read(path, path);
}

void WuffFilesystem__release_image(ConstWuffImagePtr image)
{
    free((void *)image->data);
}

WuffDependencyOperations WuffFilesystem__dependencies(void)
{
    return WuffDependencyOperations__new(0, WuffFilesystem__load, WuffFilesystem__release);
}
