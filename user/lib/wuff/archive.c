#include "archive.h"

Result(EmbeddedFs) WuffArchive__mapped(void)
{
    EmbeddedFsHeaderPtr header = (EmbeddedFsHeaderPtr)EMBEDDED_FS_VADDR;
    if (header->blob_size < sizeof(EmbeddedFsHeader) || header->blob_size > EMBEDDED_FS_MAX_SIZE)
    {
        return Error(EmbeddedFs, bstr("Invalid bootstrap archive size"), WUFF_LOAD_INVALID_IMAGE);
    }
    EmbeddedFs archive = EmbeddedFs__new(EMBEDDED_FS_VADDR, EMBEDDED_FS_VADDR + header->blob_size);
    if (!EmbeddedFs__is_valid(&archive))
    {
        return Error(EmbeddedFs, bstr("Invalid bootstrap archive"), WUFF_LOAD_INVALID_IMAGE);
    }
    return Success(EmbeddedFs, archive);
}

Result(WuffImage) WuffArchive__load_image(EmbeddedFsPtr archive, ConstBStringPtr path)
{
    BString prefix = bstr("/sbin/");
    if (!BString__starts_with(path, &prefix) || !EmbeddedFs__is_valid(archive))
    {
        return Error(WuffImage, bstr("Invalid bootstrap image path or archive"), WUFF_LOAD_INVALID_IMAGE);
    }
    BString relative = BString__slice(path, BString__length(&prefix), BString__length(path));
    match_optional(
        EmbeddedFs__find(archive, &relative),
        some(file)
        {
            return Success(WuffImage, WuffImage__new(path, (const uint8_t *)file.start, file.end - file.start));
        },
        none()
        {
            return Error(WuffImage, bstr("Bootstrap image not found"), WUFF_LOAD_DEPENDENCY_NOT_FOUND);
        }
    );
    return Error(WuffImage, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static Result(WuffImage) WuffArchive__load_dependency(void * context, ConstBStringPtr name)
{
    if (BString__is_empty(name) || BString__contains_char(name, '/'))
    {
        return Error(WuffImage, bstr("Invalid bootstrap dependency name"), WUFF_LOAD_DEPENDENCY_NOT_FOUND);
    }
    BString directory = bstr("/sbin/lib/");
    if (BString__length(&directory) + BString__length(name) >= BSTRING_MAX_SIZE)
    {
        return Error(WuffImage, bstr("Bootstrap dependency name too long"), WUFF_LOAD_DEPENDENCY_NOT_FOUND);
    }
    BString path = BString__append(&directory, name);
    match_result(
        WuffArchive__load_image(context, &path),
        success(image)
        {
            return Success(WuffImage, WuffImage__new(name, image.data, image.size));
        },
        error(code, message)
        {
            return Error(WuffImage, message, code);
        }
    );
    return Error(WuffImage, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static void WuffArchive__release(void * context, ConstWuffImagePtr image)
{
}

WuffDependencyOperations WuffArchive__dependencies(EmbeddedFsPtr archive)
{
    return WuffDependencyOperations__new(archive, WuffArchive__load_dependency, WuffArchive__release);
}

Result(WuffGraph)
    WuffArchive__load_module(EmbeddedFsPtr archive, ConstBStringPtr path, ConstWuffMemoryOperationsPtr memory)
{
    match_result(
        WuffArchive__load_image(archive, path),
        success(image)
        {
            WuffDependencyOperations dependencies = WuffArchive__dependencies(archive);
            return WuffLoader__load_module_graph(&image, memory, &dependencies);
        },
        error(code, message)
        {
            return Error(WuffGraph, message, code);
        }
    );
    return Error(WuffGraph, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}
