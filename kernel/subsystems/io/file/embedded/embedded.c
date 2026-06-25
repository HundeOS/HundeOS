#include "embedded.h"

#include "../file_io.h"
#include "../../../../bin/embedded_binaries.h"
#include "../../../../utils/math.h"
#include "../../../../kalloc.h"

static KString invalid_path_format = KSTRING_NEW("Invalid path %s");
static KString invalid_file_handle = KSTRING_NEW("Invalid file handle");
static KString not_implemented = KSTRING_NEW("Operation not implemented");
static KString end_of_file = KSTRING_NEW("End pf file");

Result(IOInternalHandle) EmbeddedFileIO__open(IOStatePtr self, KString path, Flags flags)
{
    EmbeddedFilesystemIOStatePtr fs_state = (EmbeddedFilesystemIOStatePtr) self;
    KStringArray path_segments = KString__split(&path, '/');

    Optional(KString) top_level_opt = KStringArray__pop_front(&path_segments);

    Match(KString, top_level_opt)
        IsNone(return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, cstr(path)), IO_INVALID_PATH));
        IsSome(file,
        {
            KStringArray file_list = EmbeddedElf__list();
            __d_printf(FILESYSTEM, "File = %s\n", cstr(file))
            Optional(ArrayIndex) index_opt = KStringArray__find_item(&file_list, file);
            Match(ArrayIndex, index_opt)
                IsNone(return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, cstr(path)), IO_INVALID_PATH));
                IsSome(index,
                {
                    CriticalSection(fs_state->state.lock,
                    {
                        IOInternalHandle handle = (IOInternalHandle) fs_state->handle_count++ | fs_state->state.filesystem->fs_code;
                        FileHandleTargetMap__set(&fs_state->state.handles, handle, FileHandleTarget__heap_new(path));
                        _cs_return Success(IOInternalHandle, handle);
                    })
                });
            End
        });
    End

    return Error(IOInternalHandle, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(IOInternalHandle) EmbeddedFileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    return Error(IOInternalHandle, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(IOBufferPtr) EmbeddedFileIO__read(IOStatePtr self, IOInternalHandle handle, size_t size)
{
    EmbeddedFilesystemIOStatePtr fs_state = (EmbeddedFilesystemIOStatePtr) self;

    Optional(FileHandleTargetPtr) target;

    CriticalSection(fs_state->state.lock,
    {
        target = FileHandleTargetMap__get(&fs_state->state.handles, handle);
    });
    Match(FileHandleTargetPtr, target)
        IsNone(
        {
            return Error(IOBufferPtr, invalid_file_handle, IO_UNKNOWN_HANDLE);
        })
        IsSome(target_ptr,
        {
            if (IOBuffer__item_count(&target_ptr->buffer) < size)
            {
                size_t free_space = IOBuffer__free_space(&target_ptr->buffer);

                Match(Elf, EmbeddedElf__load(target_ptr->path))
                    IsSome(elf,
                    {
                        CharPtr bytes = (CharPtr) elf.start;
                        size_t elf_size = elf.end - elf.start;

                        if (target_ptr->offset >= elf_size)
                        {
                            return Error(IOBufferPtr, end_of_file, IO_EOF);
                        }

                        size_t read_size = min(elf_size, free_space);

                        IOBuffer__push_all(&target_ptr->buffer, &bytes[target_ptr->offset], read_size);
                        target_ptr->offset += read_size;
                    })
                    IsNone(
                    {
                        return Error(IOBufferPtr,
                                     KString__sprintf(&invalid_path_format, cstr(target_ptr->path)),
                                     IO_INVALID_PATH);
                    })
                End

            }

            return Success(IOBufferPtr, &target_ptr->buffer);
        })
    End

}

Result(ByteCount) EmbeddedFileIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    return Error(ByteCount, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(ParkStatus) EmbeddedFileIO__park(IOStatePtr self, IOContextPtr parked_context)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(ParkStatus) EmbeddedFileIO__unpark(IOStatePtr self)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(FilesystemNodes) EmbeddedFileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, KString path)
{
    EmbeddedFilesystemIOStatePtr fs_state = (EmbeddedFilesystemIOStatePtr) self;
    FilesystemNodes result = EmptyArray(FilesystemNodes);
    FAIterate(KStringArray, EmbeddedElf__list(), index, entry)
        FilesystemNodePtr node = FilesystemNode__heap_new(fs_state->state.filesystem, entry, 0);
        FilesystemNodes__push(&result, node);
    End

    return Success(FilesystemNodes, result);
}

Result(IOStatus) EmbeddedFileIO__mkdir(IOStatePtr self, KString path)
{
    return Error(IOStatus, not_implemented, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOStatus) EmbeddedFileIO__remove(IOStatePtr self, KString path, Flags flags)
{
    return Error(IOStatus, not_implemented, IO_OPERATION_NOT_SUPPORTED);
}

FileIO EmbeddedFilesystemIO__new()
{
    FileIO embedded_file_io = (FileIO) {
        .base_io = (IO) {
            .read = EmbeddedFileIO__read,
            .write = EmbeddedFileIO__write,
            .open = EmbeddedFileIO__open,
            .close = EmbeddedFileIO__close,
            .park = EmbeddedFileIO__park,
            .unpark = EmbeddedFileIO__unpark,
        },
        .list_nodes = EmbeddedFileIO__list_nodes,
        .mkdir = EmbeddedFileIO__mkdir,
        .remove = EmbeddedFileIO__remove,
    };
    return embedded_file_io;
}

EmbeddedFilesystemIOStatePtr EmbeddedFilesystemIOState__heap_new(FilesystemPtr fs)
{
    EmbeddedFilesystemIOStatePtr io_state = ptr(
            Kernel__alloc_kernel(sizeof(EmbeddedFilesystemIOState)), EmbeddedFilesystemIOState
        );

    *io_state = (EmbeddedFilesystemIOState) {
        .state = FilesystemIOState__new(fs),
        .handle_count = 0,
    };

    fs->state = &io_state->state;

    return io_state;
}
