#include "filesystem.h"

#include "sys/io.h"
#include "sys/mem.h"

static BString dependency_not_found = bstr("Shared object dependency not found");
static BString dependency_invalid = bstr("Invalid shared object dependency name");
static BString dependency_too_large = bstr("Shared object dependency is too large");
static BString dependency_read_failed = bstr("Unable to read shared object dependency");

static Result(WuffImage)
    WuffFilesystem__read_opened(ConstBStringPtr name, IOHandle handle, uint8_t * data, uint64_t size)
{
    size_t read = 0;
    while (read < (size_t)size)
    {
        match_result(
            IO__read_bytes(handle, (char *)data + read, (size_t)size - read),
            success(count)
            {
                if (count <= 0 || (uint64_t)count > size - read)
                {
                    IO__close(handle);
                    free(data);
                    return Error(WuffImage, dependency_read_failed, WUFF_LOAD_IO_FAILED);
                }
                read += (size_t)count;
            },
            error(code, message)
            {
                IO__close(handle);
                free(data);
                return Error(WuffImage, dependency_read_failed, WUFF_LOAD_IO_FAILED);
            }
        );
    }
    match_result(
        IO__close(handle),
        success(status)
        {
            return Success(WuffImage, WuffImage__new(name, data, read));
        },
        error(code, message)
        {
            free(data);
            return Error(WuffImage, dependency_read_failed, WUFF_LOAD_IO_FAILED);
        }
    );
    free(data);
    return Error(WuffImage, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static Result(WuffImage) WuffFilesystem__read(ConstBStringPtr path, ConstBStringPtr name)
{
    match_result(
        IO__file_info(path),
        success(info)
        {
            if ((info.mode & IO_MODE_TYPE_MASK) != IO_MODE_TYPE_FILE || info.size == 0 ||
                info.size > WUFF_MAX_IMAGE_SIZE || info.size > SIZE_MAX)
            {
                return Error(WuffImage, dependency_too_large, WUFF_LOAD_INVALID_IMAGE);
            }
            match_result(
                mem_alloc_bytes((size_t)info.size),
                success(data_address)
                {
                    match_result(
                        IO__open(path, IO_OPEN_READ),
                        success(handle)
                        {
                            return WuffFilesystem__read_opened(name, handle, (uint8_t *)data_address, info.size);
                        },
                        error(code, message)
                        {
                            free((uint8_t *)data_address);
                            return Error(WuffImage, dependency_not_found, WUFF_LOAD_DEPENDENCY_NOT_FOUND);
                        }
                    );
                    free((uint8_t *)data_address);
                    return Error(WuffImage, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
                },
                error(code, message)
                {
                    return Error(WuffImage, dependency_read_failed, WUFF_LOAD_NO_MEMORY);
                }
            );
            return Error(WuffImage, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        error(code, message)
        {
            return Error(WuffImage, dependency_not_found, WUFF_LOAD_DEPENDENCY_NOT_FOUND);
        }
    );
    return Error(WuffImage, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
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

Result(WuffGraph) WuffFilesystem__load_module(ConstBStringPtr path, ConstWuffMemoryOperationsPtr memory)
{
    match_result(
        WuffFilesystem__load_image(path),
        success(image)
        {
            WuffDependencyOperations dependencies = WuffFilesystem__dependencies();
            Result(WuffGraph) loaded = WuffLoader__load_module_graph(&image, memory, &dependencies);
            WuffFilesystem__release_image(&image);
            return loaded;
        },
        error(code, message)
        {
            return Error(WuffGraph, message, code);
        }
    );
    return Error(WuffGraph, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

void WuffFilesystem__release_image(ConstWuffImagePtr image)
{
    free((void *)image->data);
}

WuffDependencyOperations WuffFilesystem__dependencies(void)
{
    return WuffDependencyOperations__new(0, WuffFilesystem__load, WuffFilesystem__release);
}
