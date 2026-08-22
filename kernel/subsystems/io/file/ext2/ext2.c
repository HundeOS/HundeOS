#include "ext2.h"

#include "../../../../kalloc.h"
#include "../../../../mutex.h"

FixedArrayBodyDefine(Ext2DirectoryEntries);
MapBodyDefine(Ext2FileHandleMap);

static BString invalid_argument = BSTRING_NEW("Invalid EXT2 argument");
static BString io_error = BSTRING_NEW("EXT2 block IO failed");
static BString invalid_volume = BSTRING_NEW("Invalid EXT2 volume");
static BString not_found = BSTRING_NEW("EXT2 path not found");
static BString not_supported = BSTRING_NEW("EXT2 feature not supported");
static BString invalid_path = BSTRING_NEW("Invalid EXT2 path");
static BString invalid_handle = BSTRING_NEW("Invalid EXT2 handle");

typedef struct Ext2Inode
{
    Ext2InodeMode mode;
    Ext2FileSize size;
    uint16_t links_count;
    Ext2Block block[15];
}
Ext2Inode;

typedef struct Ext2AsyncListDirContext
{
    Ext2FilesystemIOStatePtr fs_state;
    BString path;
    DirectoryEntryPtr entries;
    size_t max_entries;
    DirectoryEntryCount count;
    BStringArray segments;
    size_t segment_index;
    Ext2InodeNumber current_inode_number;
    Ext2Inode current_inode;
    size_t current_dir_block_index;
    Ext2Block block_to_read;
    Data current_dir_block_buffer[EXT2_BLOCK_SIZE];
    size_t current_dir_block_offset;
    Ext2DirectoryEntry pending_dir_entry;
    enum
    {
        EXT2_ASYNC_LIST_READ_SUPERBLOCK = 1,
        EXT2_ASYNC_LIST_READ_GROUP_DESC = 2,
        EXT2_ASYNC_LIST_READ_INODE = 3,
        EXT2_ASYNC_LIST_READ_DIR_BLOCK_LOOKUP = 4,
        EXT2_ASYNC_LIST_READ_DIR_BLOCK_COLLECT = 5,
        EXT2_ASYNC_LIST_READ_DIRENT_INODE = 6,
    } phase;
}
Ext2AsyncListDirContext;

typedef Ext2AsyncListDirContext * Ext2AsyncListDirContextPtr;

typedef struct Ext2SyncReadContext
{
    DataPtr target;
}
Ext2SyncReadContext;

typedef struct Ext2SyncWriteContext
{
    int unused;
}
Ext2SyncWriteContext;

typedef struct Ext2AsyncReadContext
{
    Ext2FilesystemIOStatePtr fs_state;
    Ext2FileHandlePtr file_handle;
    ExternalBuffer target_buffer;
    size_t requested_size;
    size_t read_size;
    Ext2Block target_block;
}
Ext2AsyncReadContext;

typedef Ext2AsyncReadContext * Ext2AsyncReadContextPtr;

typedef enum Ext2AsyncWritePhase
{
    EXT2_ASYNC_WRITE_READ_SUPERBLOCK = 1,
    EXT2_ASYNC_WRITE_READ_GROUP_DESC = 2,
    EXT2_ASYNC_WRITE_READ_PARENT_INODE = 3,
    EXT2_ASYNC_WRITE_READ_PARENT_DIR_BLOCK = 4,
    EXT2_ASYNC_WRITE_READ_TARGET_INODE = 5,
    EXT2_ASYNC_WRITE_READ_TARGET_INODE_BLOCK = 6,
    EXT2_ASYNC_WRITE_READ_INODE_BITMAP = 7,
    EXT2_ASYNC_WRITE_READ_BLOCK_BITMAP = 8,
    EXT2_ASYNC_WRITE_READ_SUPERBLOCK_COUNTS = 9,
    EXT2_ASYNC_WRITE_READ_GROUP_DESC_COUNTS = 10,
    EXT2_ASYNC_WRITE_WRITE_DATA_BLOCK = 11,
    EXT2_ASYNC_WRITE_WRITE_TARGET_INODE = 12,
    EXT2_ASYNC_WRITE_WRITE_INODE_BITMAP = 13,
    EXT2_ASYNC_WRITE_WRITE_BLOCK_BITMAP = 14,
    EXT2_ASYNC_WRITE_WRITE_SUPERBLOCK = 15,
    EXT2_ASYNC_WRITE_WRITE_GROUP_DESC = 16,
    EXT2_ASYNC_WRITE_WRITE_PARENT_DIR_BLOCK = 17,
}
Ext2AsyncWritePhase;

typedef struct Ext2AsyncWriteContext
{
    Ext2FilesystemIOStatePtr fs_state;
    Ext2FileHandlePtr file_handle;
    BString path;
    BStringArray segments;
    size_t parent_segment_count;
    size_t segment_index;
    BString leaf_name;
    Ext2AsyncWritePhase phase;

    Ext2InodeNumber current_inode_number;
    Ext2Inode current_inode;
    size_t current_dir_block_index;

    Ext2InodeNumber parent_inode_number;
    Ext2Block parent_dir_block;
    Data parent_dir_block_buffer[EXT2_BLOCK_SIZE];
    bool found_target;
    bool creating;
    bool needs_parent_dir_write;
    size_t insert_prev_offset;
    size_t insert_offset;
    bool insert_uses_empty_slot;

    Ext2InodeNumber target_inode_number;
    Ext2Inode target_inode;
    Ext2Block target_inode_block_number;
    Data target_inode_block_buffer[EXT2_BLOCK_SIZE];

    Data inode_bitmap_buffer[EXT2_BLOCK_SIZE];
    Data block_bitmap_buffer[EXT2_BLOCK_SIZE];
    Data superblock_buffer[EXT2_BLOCK_SIZE];
    Data group_desc_buffer[EXT2_BLOCK_SIZE];
    Data data_block_buffer[EXT2_BLOCK_SIZE];
    Data write_data[EXT2_MAX_READ_SIZE];
    size_t write_size;
    size_t blocks_needed;
    size_t data_block_index;
    int32_t free_block_delta;
    int32_t free_inode_delta;
}
Ext2AsyncWriteContext;

typedef Ext2AsyncWriteContext * Ext2AsyncWriteContextPtr;

typedef enum Ext2AsyncMkdirPhase
{
    EXT2_ASYNC_MKDIR_READ_SUPERBLOCK = 1,
    EXT2_ASYNC_MKDIR_READ_GROUP_DESC = 2,
    EXT2_ASYNC_MKDIR_READ_PARENT_INODE = 3,
    EXT2_ASYNC_MKDIR_READ_PARENT_DIR_BLOCK = 4,
    EXT2_ASYNC_MKDIR_READ_INODE_BITMAP = 5,
    EXT2_ASYNC_MKDIR_READ_BLOCK_BITMAP = 6,
    EXT2_ASYNC_MKDIR_READ_SUPERBLOCK_COUNTS = 7,
    EXT2_ASYNC_MKDIR_READ_GROUP_DESC_COUNTS = 8,
    EXT2_ASYNC_MKDIR_READ_TARGET_INODE_BLOCK = 9,
    EXT2_ASYNC_MKDIR_WRITE_TARGET_DIR_BLOCK = 10,
    EXT2_ASYNC_MKDIR_WRITE_TARGET_INODE_BLOCK = 11,
    EXT2_ASYNC_MKDIR_WRITE_INODE_BITMAP = 12,
    EXT2_ASYNC_MKDIR_WRITE_BLOCK_BITMAP = 13,
    EXT2_ASYNC_MKDIR_WRITE_SUPERBLOCK = 14,
    EXT2_ASYNC_MKDIR_WRITE_GROUP_DESC = 15,
    EXT2_ASYNC_MKDIR_WRITE_PARENT_DIR_BLOCK = 16,
}
Ext2AsyncMkdirPhase;

typedef struct Ext2AsyncMkdirContext
{
    Ext2FilesystemIOStatePtr fs_state;
    BString path;
    BStringArray segments;
    size_t parent_segment_count;
    size_t segment_index;
    BString leaf_name;
    Ext2AsyncMkdirPhase phase;

    Ext2InodeNumber current_inode_number;
    Ext2Inode current_inode;
    size_t current_dir_block_index;

    Ext2InodeNumber parent_inode_number;
    Ext2Block parent_dir_block;
    Data parent_dir_block_buffer[EXT2_BLOCK_SIZE];
    bool needs_parent_dir_write;
    size_t insert_prev_offset;
    size_t insert_offset;
    bool insert_uses_empty_slot;

    Ext2InodeNumber target_inode_number;
    Ext2Inode target_inode;
    Ext2Block target_inode_block_number;
    Data target_inode_block_buffer[EXT2_BLOCK_SIZE];
    Data target_dir_block_buffer[EXT2_BLOCK_SIZE];

    Data inode_bitmap_buffer[EXT2_BLOCK_SIZE];
    Data block_bitmap_buffer[EXT2_BLOCK_SIZE];
    Data superblock_buffer[EXT2_BLOCK_SIZE];
    Data group_desc_buffer[EXT2_BLOCK_SIZE];
}
Ext2AsyncMkdirContext;

typedef Ext2AsyncMkdirContext * Ext2AsyncMkdirContextPtr;

typedef enum Ext2AsyncLookupPhase
{
    EXT2_ASYNC_LOOKUP_READ_SUPERBLOCK = 1,
    EXT2_ASYNC_LOOKUP_READ_GROUP_DESC = 2,
    EXT2_ASYNC_LOOKUP_READ_INODE = 3,
    EXT2_ASYNC_LOOKUP_READ_DIR_BLOCK = 4,
}
Ext2AsyncLookupPhase;

typedef struct Ext2AsyncOpenContext
{
    Ext2FilesystemIOStatePtr fs_state;
    BString path;
    Flags flags;
    BStringArray segments;
    size_t segment_index;
    Ext2InodeNumber current_inode_number;
    Ext2Inode current_inode;
    size_t current_dir_block_index;
    Ext2AsyncLookupPhase phase;
}
Ext2AsyncOpenContext;

typedef Ext2AsyncOpenContext * Ext2AsyncOpenContextPtr;

typedef enum Ext2AsyncRemovePhase
{
    EXT2_ASYNC_REMOVE_READ_SUPERBLOCK = 1,
    EXT2_ASYNC_REMOVE_READ_GROUP_DESC = 2,
    EXT2_ASYNC_REMOVE_READ_PARENT_INODE = 3,
    EXT2_ASYNC_REMOVE_READ_PARENT_DIR_BLOCK = 4,
    EXT2_ASYNC_REMOVE_READ_TARGET_INODE = 5,
    EXT2_ASYNC_REMOVE_VERIFY_TARGET_DIR_BLOCK = 6,
    EXT2_ASYNC_REMOVE_READ_INODE_BITMAP = 7,
    EXT2_ASYNC_REMOVE_READ_BLOCK_BITMAP = 8,
    EXT2_ASYNC_REMOVE_READ_SUPERBLOCK_COUNTS = 9,
    EXT2_ASYNC_REMOVE_READ_GROUP_DESC_COUNTS = 10,
    EXT2_ASYNC_REMOVE_WRITE_TARGET_INODE = 11,
    EXT2_ASYNC_REMOVE_WRITE_INODE_BITMAP = 12,
    EXT2_ASYNC_REMOVE_WRITE_BLOCK_BITMAP = 13,
    EXT2_ASYNC_REMOVE_WRITE_SUPERBLOCK = 14,
    EXT2_ASYNC_REMOVE_WRITE_GROUP_DESC = 15,
    EXT2_ASYNC_REMOVE_WRITE_PARENT_DIR_BLOCK = 16,
}
Ext2AsyncRemovePhase;

typedef struct Ext2AsyncRemoveContext
{
    Ext2FilesystemIOStatePtr fs_state;
    BString path;
    Flags flags;
    BStringArray segments;
    BStringArray resume_paths;
    size_t parent_segment_count;
    size_t segment_index;
    BString leaf_name;
    Ext2AsyncRemovePhase phase;

    Ext2InodeNumber current_inode_number;
    Ext2Inode current_inode;
    size_t current_dir_block_index;

    Ext2InodeNumber parent_inode_number;
    Ext2Block parent_dir_block;
    Data parent_dir_block_buffer[EXT2_BLOCK_SIZE];
    size_t parent_dirent_offset;
    size_t parent_previous_dirent_offset;
    bool parent_has_previous;

    Ext2InodeNumber target_inode_number;
    Ext2Inode target_inode;
    Ext2Block target_inode_block_number;
    Data target_inode_block_buffer[EXT2_BLOCK_SIZE];
    size_t verify_dir_block_index;

    Data inode_bitmap_buffer[EXT2_BLOCK_SIZE];
    Data block_bitmap_buffer[EXT2_BLOCK_SIZE];
    Data superblock_buffer[EXT2_BLOCK_SIZE];
    Data group_desc_buffer[EXT2_BLOCK_SIZE];
    uint32_t freed_block_count;
    bool decrement_only;
}
Ext2AsyncRemoveContext;

typedef Ext2AsyncRemoveContext * Ext2AsyncRemoveContextPtr;

typedef enum Ext2AsyncLinkPhase
{
    EXT2_ASYNC_LINK_READ_SUPERBLOCK = 1,
    EXT2_ASYNC_LINK_READ_GROUP_DESC = 2,
    EXT2_ASYNC_LINK_READ_SOURCE_INODE = 3,
    EXT2_ASYNC_LINK_READ_SOURCE_DIR_BLOCK = 4,
    EXT2_ASYNC_LINK_READ_TARGET_PARENT_INODE = 5,
    EXT2_ASYNC_LINK_READ_TARGET_PARENT_DIR_BLOCK = 6,
    EXT2_ASYNC_LINK_READ_SOURCE_INODE_BLOCK = 7,
    EXT2_ASYNC_LINK_WRITE_SOURCE_INODE_BLOCK = 8,
    EXT2_ASYNC_LINK_WRITE_TARGET_PARENT_DIR_BLOCK = 9,
}
Ext2AsyncLinkPhase;

typedef struct Ext2AsyncLinkContext
{
    Ext2FilesystemIOStatePtr fs_state;
    BString source_path;
    BString target_path;
    BStringArray source_segments;
    BStringArray target_segments;
    size_t segment_index;
    size_t target_parent_segment_count;
    BString target_leaf_name;
    bool traversing_target_parent;
    Ext2AsyncLinkPhase phase;

    Ext2InodeNumber current_inode_number;
    Ext2Inode current_inode;
    size_t current_dir_block_index;

    Ext2InodeNumber source_inode_number;
    Ext2Inode source_inode;
    Ext2Block source_inode_block_number;
    Data source_inode_block_buffer[EXT2_BLOCK_SIZE];

    Ext2InodeNumber target_parent_inode_number;
    Ext2Block target_parent_dir_block;
    Data target_parent_dir_block_buffer[EXT2_BLOCK_SIZE];
    bool needs_parent_dir_write;
    size_t insert_prev_offset;
    size_t insert_offset;
    bool insert_uses_empty_slot;
}
Ext2AsyncLinkContext;

typedef Ext2AsyncLinkContext * Ext2AsyncLinkContextPtr;

static bool Ext2Inode__is_dir(Ext2Inode * inode);
static bool Ext2Inode__is_file(Ext2Inode * inode);
static Result(Ext2Code) Ext2Volume__update_allocation_counts(
    Ext2VolumePtr self,
    int32_t block_delta,
    int32_t inode_delta,
    int32_t used_dirs_delta
);
static Result(Ext2Code) Ext2Volume__lookup_child(
    Ext2VolumePtr self, Ext2InodeNumber directory_inode, BString name, Ext2InodeNumber * child_inode
);
static Result(Ext2Code) Ext2Volume__resolve_parent(
    Ext2VolumePtr self,
    BString path,
    Ext2InodeNumber * parent_inode_number,
    BString * leaf_name
);

static Result(Anything) Ext2FileIO__complete_async_list_dir(BlockIOContextPtr block_context);
static Result(Anything) Ext2FileIO__complete_async_read_file(BlockIOContextPtr block_context);
static Result(Anything) Ext2FileIO__complete_async_open(BlockIOContextPtr block_context);
static Result(Anything) Ext2FileIO__complete_async_remove(BlockIOContextPtr block_context);
static Result(Anything) Ext2FileIO__complete_async_write_file(BlockIOContextPtr block_context);
static Result(Anything) Ext2FileIO__complete_async_mkdir(BlockIOContextPtr block_context);
static Result(Anything) Ext2FileIO__complete_async_link(BlockIOContextPtr block_context);
static Result(Anything) Ext2FileIO__read_block_async(
    Ext2AsyncListDirContextPtr context,
    Ext2Block block,
    KernelContext kernel_context,
    BlockIOContinuation continuation
);
static Result(Anything) Ext2FileIO__read_open_block_async(
    Ext2AsyncOpenContextPtr context,
    Ext2Block block,
    KernelContext kernel_context
);
static Result(IOInternalHandle) Ext2FileIO__start_async_open(
    Ext2FilesystemIOStatePtr fs_state,
    BString path,
    Flags flags
);
static Result(IOStatus) Ext2FileIO__start_async_remove(
    Ext2FilesystemIOStatePtr fs_state,
    BString path,
    Flags flags
);
static Result(IOStatus) Ext2FileIO__start_async_mkdir(
    Ext2FilesystemIOStatePtr fs_state,
    BString path
);
static Result(IOStatus) Ext2FileIO__start_async_link(
    Ext2FilesystemIOStatePtr fs_state,
    BString source_path,
    BString target_path
);
static Result(IOBufferPtr) Ext2FileIO__start_async_read(
    Ext2FilesystemIOStatePtr fs_state,
    Ext2FileHandlePtr file_handle,
    size_t size,
    ExternalBuffer target_buffer
);
static bool Ext2FilesystemIOState__uses_userspace_device(Ext2FilesystemIOStatePtr self);
static Result(Anything) Ext2Volume__complete_sync_read(BlockIOContextPtr block_context);
static Result(Anything) Ext2Volume__complete_sync_write(BlockIOContextPtr block_context);
static Result(Ext2Code) Ext2Volume__remove_inode(
    Ext2VolumePtr self,
    Ext2InodeNumber parent_inode_number,
    BString leaf_name,
    Ext2InodeNumber target_inode_number,
    Ext2Inode * target_inode,
    Flags flags
);

static uint16_t Ext2__read_u16(DataPtr data, size_t offset)
{
    return (uint16_t) data[offset] | ((uint16_t) data[offset + 1] << 8);
}

static uint32_t Ext2__read_u32(DataPtr data, size_t offset)
{
    return (uint32_t) data[offset]
        | ((uint32_t) data[offset + 1] << 8)
        | ((uint32_t) data[offset + 2] << 16)
        | ((uint32_t) data[offset + 3] << 24);
}

static void Ext2__write_u16(DataPtr data, size_t offset, uint16_t value)
{
    data[offset] = (Data) (value & 0xff);
    data[offset + 1] = (Data) ((value >> 8) & 0xff);
}

static void Ext2__write_u32(DataPtr data, size_t offset, uint32_t value)
{
    data[offset] = (Data) (value & 0xff);
    data[offset + 1] = (Data) ((value >> 8) & 0xff);
    data[offset + 2] = (Data) ((value >> 16) & 0xff);
    data[offset + 3] = (Data) ((value >> 24) & 0xff);
}

static DirectoryEntry Ext2FileIO__directory_entry_from_ext2_entry(Ext2DirectoryEntryPtr entry)
{
    DirectoryEntry dirent = (DirectoryEntry) { 0 };
    dirent.attributes = IO_DIRENT_ATTR_NONE;
    dirent.user_id = PROCESS_USER_ROOT;
    dirent.group_id = PROCESS_GROUP_ROOT;
    dirent.mode = entry->mode;
    if ((dirent.mode & IO_MODE_TYPE_MASK) == 0)
    {
        dirent.mode |= entry->file_type == EXT2_DIRENT_TYPE_DIR ? IO_MODE_TYPE_DIR : IO_MODE_TYPE_FILE;
    }

    size_t max_name = IO_DIRENT_NAME_MAX - 1;
    size_t copy_len = entry->name.len < (StringPos) max_name ? (size_t) entry->name.len : max_name;
    for (size_t i = 0; i < copy_len; i++)
    {
        dirent.name[i] = entry->name.data[i];
    }
    dirent.name[copy_len] = '\0';
    dirent.name_len = copy_len;
    return dirent;
}

bool Ext2DirectoryEntry__equals(Ext2DirectoryEntryPtr self, Ext2DirectoryEntryPtr other)
{
    return BString__equals(&self->name, &other->name)
        && self->file_type == other->file_type
        && self->inode == other->inode
        && self->mode == other->mode
        && self->size == other->size;
}

static Result(Ext2Code) Ext2Volume__read_blocks(
    Ext2VolumePtr self, Ext2Block block, Ext2BlockCount count, DataPtr target
)
{
    Ext2SyncReadContext sync_context = {
        .target = target,
    };

    Match(BlockDevicePtr, self->device)
        IsSome(device,
        {
            BlockIOContext block_context = { 0 };
            block_context.kernel_context = EMPTY_KERNEL_CONTEXT;
            block_context.owner_context = (Anything) &sync_context;

            ResultMatch(Anything, BlockDevice__read_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                (BlockCount) count * EXT2_BLOCK_SECTORS,
                &block_context,
                Ext2Volume__complete_sync_read
            ))
                IsSuccess(blocks,
                {
                    if ((BlockCount) blocks == (BlockCount) count * EXT2_BLOCK_SECTORS)
                    {
                        return Success(Ext2Code, EXT2_OK);
                    }
                })
                IsError(code, message, return Error(Ext2Code, io_error, EXT2_IO_ERROR))
            End
        })
        IsNone(return Error(Ext2Code, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Ext2Code, io_error, EXT2_IO_ERROR);
}

static Result(Ext2Code) Ext2Volume__write_blocks(
    Ext2VolumePtr self, Ext2Block block, Ext2BlockCount count, DataPtr source
)
{
    Ext2SyncWriteContext sync_context = { 0 };

    Match(BlockDevicePtr, self->device)
        IsSome(device,
        {
            BlockIOContext block_context = { 0 };
            block_context.kernel_context = EMPTY_KERNEL_CONTEXT;
            block_context.owner_context = (Anything) &sync_context;

            ResultMatch(Anything, BlockDevice__write_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                (BlockCount) count * EXT2_BLOCK_SECTORS,
                source,
                &block_context,
                Ext2Volume__complete_sync_write
            ))
                IsSuccess(blocks,
                {
                    if ((BlockCount) blocks == (BlockCount) count * EXT2_BLOCK_SECTORS)
                    {
                        return Success(Ext2Code, EXT2_OK);
                    }
                })
                IsError(code, message, return Error(Ext2Code, io_error, EXT2_IO_ERROR))
            End
        })
        IsNone(return Error(Ext2Code, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Ext2Code, io_error, EXT2_IO_ERROR);
}

static Result(Anything) Ext2Volume__complete_sync_read(BlockIOContextPtr block_context)
{
    Ext2SyncReadContext * context = ptr(block_context->owner_context, Ext2SyncReadContext);
    if (block_context->status != BLOCK_DEVICE_OK)
    {
        return Error(Anything, io_error, EXT2_IO_ERROR);
    }

    Kernel__memcopy(addr(block_context->buffer), addr(context->target), block_context->buffer_size);
    return Success(Anything, block_context->completed_blocks);
}

static Result(Anything) Ext2Volume__complete_sync_write(BlockIOContextPtr block_context)
{
    if (block_context->status != BLOCK_DEVICE_OK)
    {
        return Error(Anything, io_error, EXT2_IO_ERROR);
    }

    return Success(Anything, block_context->completed_blocks);
}

static Result(Ext2Code) Ext2Volume__parse_superblock(Ext2VolumePtr self, DataPtr superblock)
{
    if (Ext2__read_u16(superblock, 56) != EXT2_SUPER_MAGIC)
    {
        return Error(Ext2Code, invalid_volume, EXT2_INVALID_VOLUME);
    }

    Ext2BlockCount log_block_size = Ext2__read_u32(superblock, 24);
    if (log_block_size != 0)
    {
        return Error(Ext2Code, not_supported, EXT2_NOT_SUPPORTED);
    }

    self->blocks_count = Ext2__read_u32(superblock, 4);
    self->first_data_block = Ext2__read_u32(superblock, 20);
    self->blocks_per_group = Ext2__read_u32(superblock, 32);
    self->inodes_per_group = Ext2__read_u32(superblock, 40);
    self->inode_size = Ext2__read_u16(superblock, 88);
    if (self->inode_size == 0)
    {
        self->inode_size = 128;
    }

    if (self->inode_size != 128)
    {
        return Error(Ext2Code, not_supported, EXT2_NOT_SUPPORTED);
    }

    return Success(Ext2Code, EXT2_OK);
}

static Result(Ext2Code) Ext2Volume__parse_group_descriptor(Ext2VolumePtr self, DataPtr group_desc)
{
    if (self->blocks_per_group == 0)
    {
        return Error(Ext2Code, invalid_volume, EXT2_INVALID_VOLUME);
    }

    self->group_count =
        (self->blocks_count + self->blocks_per_group - 1U) / self->blocks_per_group;
    if (self->group_count == 0 || self->group_count > EXT2_MAX_GROUPS)
    {
        return Error(Ext2Code, not_supported, EXT2_NOT_SUPPORTED);
    }

    for (size_t group_index = 0; group_index < self->group_count; group_index++)
    {
        size_t descriptor_offset = group_index * 32U;
        Ext2Block block_bitmap_block = Ext2__read_u32(group_desc, descriptor_offset);
        Ext2Block inode_bitmap_block = Ext2__read_u32(group_desc, descriptor_offset + 4U);
        Ext2Block inode_table_block = Ext2__read_u32(group_desc, descriptor_offset + 8U);

        if (block_bitmap_block == 0 || inode_bitmap_block == 0 || inode_table_block == 0)
        {
            return Error(Ext2Code, invalid_volume, EXT2_INVALID_VOLUME);
        }

        self->group_block_bitmap_block[group_index] = block_bitmap_block;
        self->group_inode_bitmap_block[group_index] = inode_bitmap_block;
        self->group_inode_table_block[group_index] = inode_table_block;
    }

    self->block_bitmap_block = self->group_block_bitmap_block[0];
    self->inode_bitmap_block = self->group_inode_bitmap_block[0];
    self->inode_table_block = self->group_inode_table_block[0];
    if (self->block_bitmap_block == 0 || self->inode_bitmap_block == 0 || self->inode_table_block == 0)
    {
        return Error(Ext2Code, invalid_volume, EXT2_INVALID_VOLUME);
    }

    self->mounted = true;
    return Success(Ext2Code, EXT2_OK);
}

static size_t Ext2Volume__inode_group_index(Ext2VolumePtr self, Ext2InodeNumber inode_number)
{
    if (self->inodes_per_group == 0 || inode_number == 0)
    {
        return 0;
    }

    return (size_t) ((inode_number - 1U) / self->inodes_per_group);
}

static Ext2Block Ext2Volume__inode_block(Ext2VolumePtr self, Ext2InodeNumber inode_number)
{
    Ext2InodeNumber inode_index = inode_number - 1U;
    size_t group_index = Ext2Volume__inode_group_index(self, inode_number);
    if (group_index >= self->group_count)
    {
        return 0;
    }

    Ext2InodeNumber group_local_index = inode_index % self->inodes_per_group;
    return self->group_inode_table_block[group_index]
        + ((group_local_index * self->inode_size) / EXT2_BLOCK_SIZE);
}

static size_t Ext2Volume__inode_offset(Ext2VolumePtr self, Ext2InodeNumber inode_number)
{
    Ext2InodeNumber inode_index = inode_number - 1U;
    Ext2InodeNumber group_local_index = self->inodes_per_group == 0
        ? inode_index
        : (inode_index % self->inodes_per_group);
    return (group_local_index * self->inode_size) % EXT2_BLOCK_SIZE;
}

static void Ext2Inode__read_from_block(
    Ext2VolumePtr volume,
    Ext2InodeNumber inode_number,
    DataPtr block,
    Ext2Inode * target
)
{
    size_t inode_offset = Ext2Volume__inode_offset(volume, inode_number);
    target->mode = Ext2__read_u16(block, inode_offset);
    target->size = Ext2__read_u32(block, inode_offset + 4);
    target->links_count = Ext2__read_u16(block, inode_offset + 26);
    if (target->links_count == 0)
    {
        target->links_count = 1;
    }
    for (size_t i = 0; i < 15; i++)
    {
        target->block[i] = Ext2__read_u32(block, inode_offset + 40 + (i * 4));
    }
}

static Result(Ext2Code) Ext2Volume__read_inode(
    Ext2VolumePtr self, Ext2InodeNumber inode_number, Ext2Inode * target
)
{
    if (!self->mounted || inode_number == 0)
    {
        return Error(Ext2Code, invalid_argument, EXT2_INVALID_ARGUMENT);
    }

    Ext2Block inode_block = Ext2Volume__inode_block(self, inode_number);
    Data block[EXT2_BLOCK_SIZE] = { 0 };

    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, inode_block, 1, block))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    Ext2Inode__read_from_block(self, inode_number, block, target);

    return Success(Ext2Code, EXT2_OK);
}

static Result(Ext2Code) Ext2Volume__write_inode(
    Ext2VolumePtr self, Ext2InodeNumber inode_number, Ext2Inode * source
)
{
    if (!self->mounted || inode_number == 0)
    {
        return Error(Ext2Code, invalid_argument, EXT2_INVALID_ARGUMENT);
    }

    Ext2Block inode_block = Ext2Volume__inode_block(self, inode_number);
    Data block[EXT2_BLOCK_SIZE] = { 0 };

    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, inode_block, 1, block))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    size_t inode_offset = Ext2Volume__inode_offset(self, inode_number);
    Ext2__write_u16(block, inode_offset, source->mode);
    Ext2__write_u32(block, inode_offset + 4, source->size);
    Ext2__write_u16(block, inode_offset + 26, source->links_count);
    for (size_t i = 0; i < 15; i++)
    {
        Ext2__write_u32(block, inode_offset + 40 + (i * 4), source->block[i]);
    }

    return Ext2Volume__write_blocks(self, inode_block, 1, block);
}

static bool Ext2__bitmap_bit_is_set(DataPtr bitmap, uint32_t index)
{
    uint32_t byte_index = index / 8;
    uint32_t bit_index = index % 8;
    return (bitmap[byte_index] & (1U << bit_index)) != 0;
}

static void Ext2__bitmap_clear_bit(DataPtr bitmap, uint32_t index)
{
    uint32_t byte_index = index / 8;
    uint32_t bit_index = index % 8;
    bitmap[byte_index] = (Data) (bitmap[byte_index] & ~(1U << bit_index));
}

static void Ext2__bitmap_set_bit(DataPtr bitmap, uint32_t index)
{
    uint32_t byte_index = index / 8;
    uint32_t bit_index = index % 8;
    bitmap[byte_index] = (Data) (bitmap[byte_index] | (1U << bit_index));
}

static uint16_t Ext2__dirent_record_size(size_t name_len)
{
    size_t padded = (name_len + 3U) & ~3U;
    return (uint16_t) (8U + padded);
}

static Result(Ext2Code) Ext2Volume__allocate_inode_from_bitmap(
    Ext2VolumePtr self,
    DataPtr inode_bitmap,
    Ext2InodeNumber * inode_number
)
{
    for (uint32_t inode_index = 0; inode_index < self->inodes_per_group; inode_index++)
    {
        if (!Ext2__bitmap_bit_is_set(inode_bitmap, inode_index))
        {
            Ext2__bitmap_set_bit(inode_bitmap, inode_index);
            *inode_number = inode_index + 1;
            return Success(Ext2Code, EXT2_OK);
        }
    }

    return Error(Ext2Code, not_supported, EXT2_NOT_SUPPORTED);
}

static Result(Ext2Code) Ext2Volume__allocate_block_from_bitmap(
    Ext2VolumePtr self,
    DataPtr block_bitmap,
    Ext2Block * block_number
)
{
    for (uint32_t block_index = 0; block_index < self->blocks_per_group; block_index++)
    {
        if (!Ext2__bitmap_bit_is_set(block_bitmap, block_index))
        {
            Ext2__bitmap_set_bit(block_bitmap, block_index);
            *block_number = self->first_data_block + block_index;
            return Success(Ext2Code, EXT2_OK);
        }
    }

    return Error(Ext2Code, not_supported, EXT2_NOT_SUPPORTED);
}

static Result(Ext2Code) Ext2Volume__insert_dirent(
    Ext2VolumePtr self,
    Ext2InodeNumber directory_inode_number,
    BString name,
    Ext2InodeNumber child_inode_number,
    Ext2DirectoryEntryType child_type
)
{
    Ext2Inode directory_inode = { 0 };
    ResultMatch(Ext2Code, Ext2Volume__read_inode(self, directory_inode_number, &directory_inode))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    if (!Ext2Inode__is_dir(&directory_inode))
    {
        return Error(Ext2Code, invalid_path, EXT2_NOT_FOUND);
    }

    uint16_t required_len = Ext2__dirent_record_size(name.len);
    for (size_t block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        if (directory_inode.block[block_index] == 0)
        {
            continue;
        }

        Data block[EXT2_BLOCK_SIZE] = { 0 };
        ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, directory_inode.block[block_index], 1, block))
            IsError(code, message, return Error(Ext2Code, message, code))
        End

        size_t offset = 0;
        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber entry_inode = Ext2__read_u32(block, offset);
            uint16_t rec_len = Ext2__read_u16(block, offset + 4);
            uint8_t name_len = block[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (entry_inode != 0)
            {
                uint16_t actual_len = Ext2__dirent_record_size(name_len);
                if (rec_len >= actual_len + required_len)
                {
                    size_t new_offset = offset + actual_len;
                    uint16_t new_len = rec_len - actual_len;
                    Ext2__write_u16(block, offset + 4, actual_len);
                    Ext2__write_u32(block, new_offset, child_inode_number);
                    Ext2__write_u16(block, new_offset + 4, new_len);
                    block[new_offset + 6] = (Data) name.len;
                    block[new_offset + 7] = (Data) child_type;
                    for (size_t i = 0; i < name.len; i++)
                    {
                        block[new_offset + 8 + i] = (Data) name.data[i];
                    }

                    return Ext2Volume__write_blocks(self, directory_inode.block[block_index], 1, block);
                }
            }

            offset += rec_len;
        }
    }

    return Error(Ext2Code, not_supported, EXT2_NOT_SUPPORTED);
}

static void Ext2Inode__reset_file(Ext2Inode * inode, Ext2FileSize size)
{
    inode->mode = EXT2_INODE_MODE_FILE | 0644;
    inode->size = size;
    inode->links_count = 1;
    for (size_t i = 0; i < 15; i++)
    {
        inode->block[i] = 0;
    }
}

static void Ext2Inode__reset_dir(Ext2Inode * inode, Ext2Block block)
{
    inode->mode = EXT2_INODE_MODE_DIR | 0755;
    inode->size = EXT2_BLOCK_SIZE;
    inode->links_count = 2;
    for (size_t i = 0; i < 15; i++)
    {
        inode->block[i] = 0;
    }
    inode->block[0] = block;
}

static void Ext2__initialise_dir_block(
    DataPtr block,
    Ext2InodeNumber self_inode,
    Ext2InodeNumber parent_inode
)
{
    for (size_t i = 0; i < EXT2_BLOCK_SIZE; i++)
    {
        block[i] = 0;
    }

    Ext2__write_u32(block, 0, self_inode);
    Ext2__write_u16(block, 4, 12);
    block[6] = 1;
    block[7] = EXT2_DIRENT_TYPE_DIR;
    block[8] = '.';

    Ext2__write_u32(block, 12, parent_inode);
    Ext2__write_u16(block, 16, EXT2_BLOCK_SIZE - 12);
    block[18] = 2;
    block[19] = EXT2_DIRENT_TYPE_DIR;
    block[20] = '.';
    block[21] = '.';
}

static Result(Ext2Code) Ext2Volume__write_inode_data(
    Ext2VolumePtr self,
    Ext2InodeNumber inode_number,
    Ext2Inode * inode,
    DataPtr data,
    size_t size,
    bool creating
)
{
    if (size > EXT2_MAX_READ_SIZE)
    {
        return Error(Ext2Code, not_supported, EXT2_FILE_TOO_LARGE);
    }

    Data inode_bitmap[EXT2_BLOCK_SIZE] = { 0 };
    Data block_bitmap[EXT2_BLOCK_SIZE] = { 0 };
    int32_t free_block_delta = 0;
    int32_t free_inode_delta = 0;

    if (creating)
    {
        ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, self->inode_bitmap_block, 1, inode_bitmap))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
        Ext2__bitmap_set_bit(inode_bitmap, inode_number - 1);
        free_inode_delta--;
    }

    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, self->block_bitmap_block, 1, block_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
    {
        if (inode->block[i] != 0)
        {
            Ext2__bitmap_clear_bit(block_bitmap, inode->block[i] - self->first_data_block);
            free_block_delta++;
            inode->block[i] = 0;
        }
    }

    size_t blocks_needed = (size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    if (blocks_needed > EXT2_NDIR_BLOCKS)
    {
        return Error(Ext2Code, not_supported, EXT2_FILE_TOO_LARGE);
    }

    for (size_t i = 0; i < blocks_needed; i++)
    {
        ResultMatch(Ext2Code, Ext2Volume__allocate_block_from_bitmap(self, block_bitmap, &inode->block[i]))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
        free_block_delta--;
    }

    for (size_t i = 0; i < blocks_needed; i++)
    {
        Data block[EXT2_BLOCK_SIZE] = { 0 };
        size_t source_offset = i * EXT2_BLOCK_SIZE;
        size_t remaining = size - source_offset;
        size_t copy_size = remaining < EXT2_BLOCK_SIZE ? remaining : EXT2_BLOCK_SIZE;
        for (size_t j = 0; j < copy_size; j++)
        {
            block[j] = data[source_offset + j];
        }

        ResultMatch(Ext2Code, Ext2Volume__write_blocks(self, inode->block[i], 1, block))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
    }

    inode->size = (Ext2FileSize) size;

    ResultMatch(Ext2Code, Ext2Volume__write_inode(self, inode_number, inode))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    if (creating)
    {
        ResultMatch(Ext2Code, Ext2Volume__write_blocks(self, self->inode_bitmap_block, 1, inode_bitmap))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
    }

    ResultMatch(Ext2Code, Ext2Volume__write_blocks(self, self->block_bitmap_block, 1, block_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    if (free_block_delta != 0 || free_inode_delta != 0)
    {
        ResultMatch(Ext2Code, Ext2Volume__update_allocation_counts(
            self,
            free_block_delta,
            free_inode_delta,
            0
        ))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
    }

    return Success(Ext2Code, EXT2_OK);
}

static Result(Ext2Code) Ext2Volume__write_file_at_path(
    Ext2VolumePtr self,
    BString path,
    DataPtr data,
    size_t size,
    Ext2InodeNumber * inode_number,
    Ext2Inode * inode
)
{
    Ext2InodeNumber parent_inode_number = 0;
    BString leaf_name = EMPTY_BSTRING;

    ResultMatch(Ext2Code, Ext2Volume__resolve_parent(self, path, &parent_inode_number, &leaf_name))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    Ext2InodeNumber existing_inode_number = 0;
    Result(Ext2Code) lookup_result = Ext2Volume__lookup_child(self, parent_inode_number, leaf_name, &existing_inode_number);
    ResultMatch(Ext2Code, lookup_result)
        IsSuccess(code,
        {
            Ext2Inode existing_inode = { 0 };
            ResultMatch(Ext2Code, Ext2Volume__read_inode(self, existing_inode_number, &existing_inode))
                IsError(inner_code, message, return Error(Ext2Code, message, inner_code))
            End

            if (!Ext2Inode__is_file(&existing_inode))
            {
                return Error(Ext2Code, invalid_path, EXT2_NOT_FOUND);
            }

            ResultMatch(Ext2Code, Ext2Volume__write_inode_data(
                self,
                existing_inode_number,
                &existing_inode,
                data,
                size,
                false
            ))
                IsError(inner_code, message, return Error(Ext2Code, message, inner_code))
            End

            *inode_number = existing_inode_number;
            *inode = existing_inode;
            return Success(Ext2Code, EXT2_OK);
        })
        IsError(code, message,
        {
            if (code != EXT2_NOT_FOUND)
            {
                return Error(Ext2Code, message, code);
            }
        })
    End

    Data inode_bitmap[EXT2_BLOCK_SIZE] = { 0 };
    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, self->inode_bitmap_block, 1, inode_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    Ext2InodeNumber new_inode_number = 0;
    ResultMatch(Ext2Code, Ext2Volume__allocate_inode_from_bitmap(self, inode_bitmap, &new_inode_number))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    Ext2Inode new_inode = { 0 };
    Ext2Inode__reset_file(&new_inode, (Ext2FileSize) size);

    ResultMatch(Ext2Code, Ext2Volume__write_inode_data(self, new_inode_number, &new_inode, data, size, true))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    ResultMatch(Ext2Code, Ext2Volume__insert_dirent(
        self,
        parent_inode_number,
        leaf_name,
        new_inode_number,
        EXT2_DIRENT_TYPE_FILE
    ))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    *inode_number = new_inode_number;
    *inode = new_inode;
    return Success(Ext2Code, EXT2_OK);
}

static Result(Ext2Code) Ext2Volume__mkdir_at_path(Ext2VolumePtr self, BString path)
{
    Ext2InodeNumber parent_inode_number = 0;
    BString leaf_name = EMPTY_BSTRING;

    ResultMatch(Ext2Code, Ext2Volume__resolve_parent(self, path, &parent_inode_number, &leaf_name))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    Ext2InodeNumber existing_inode_number = 0;
    Result(Ext2Code) lookup_result = Ext2Volume__lookup_child(self, parent_inode_number, leaf_name, &existing_inode_number);
    ResultMatch(Ext2Code, lookup_result)
        IsSuccess(code, return Error(Ext2Code, invalid_path, EXT2_NOT_FOUND))
        IsError(code, message,
        {
            if (code != EXT2_NOT_FOUND)
            {
                return Error(Ext2Code, message, code);
            }
        })
    End

    Data inode_bitmap[EXT2_BLOCK_SIZE] = { 0 };
    Data block_bitmap[EXT2_BLOCK_SIZE] = { 0 };
    Data dir_block[EXT2_BLOCK_SIZE] = { 0 };
    Ext2InodeNumber new_inode_number = 0;
    Ext2Block new_dir_block = 0;

    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, self->inode_bitmap_block, 1, inode_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, self->block_bitmap_block, 1, block_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    ResultMatch(Ext2Code, Ext2Volume__allocate_inode_from_bitmap(self, inode_bitmap, &new_inode_number))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__allocate_block_from_bitmap(self, block_bitmap, &new_dir_block))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    Ext2Inode new_inode = { 0 };
    Ext2Inode__reset_dir(&new_inode, new_dir_block);
    Ext2__initialise_dir_block(dir_block, new_inode_number, parent_inode_number);

    ResultMatch(Ext2Code, Ext2Volume__write_blocks(self, new_dir_block, 1, dir_block))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__write_inode(self, new_inode_number, &new_inode))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__write_blocks(self, self->inode_bitmap_block, 1, inode_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__write_blocks(self, self->block_bitmap_block, 1, block_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__insert_dirent(
        self,
        parent_inode_number,
        leaf_name,
        new_inode_number,
        EXT2_DIRENT_TYPE_DIR
    ))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__update_allocation_counts(self, -1, -1, 1))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    return Success(Ext2Code, EXT2_OK);
}

static Result(Ext2Code) Ext2Volume__update_allocation_counts(
    Ext2VolumePtr self,
    int32_t block_delta,
    int32_t inode_delta,
    int32_t used_dirs_delta
)
{
    Data superblock[EXT2_BLOCK_SIZE] = { 0 };
    Data group_desc[EXT2_BLOCK_SIZE] = { 0 };

    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, 1, 1, superblock))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, 2, 1, group_desc))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    uint32_t free_blocks = Ext2__read_u32(superblock, 12);
    uint32_t free_inodes = Ext2__read_u32(superblock, 16);
    uint16_t group_free_blocks = Ext2__read_u16(group_desc, 12);
    uint16_t group_free_inodes = Ext2__read_u16(group_desc, 14);
    uint16_t used_dirs = Ext2__read_u16(group_desc, 16);

    Ext2__write_u32(superblock, 12, (uint32_t) ((int32_t) free_blocks + block_delta));
    Ext2__write_u32(superblock, 16, (uint32_t) ((int32_t) free_inodes + inode_delta));
    Ext2__write_u16(group_desc, 12, (uint16_t) ((int32_t) group_free_blocks + block_delta));
    Ext2__write_u16(group_desc, 14, (uint16_t) ((int32_t) group_free_inodes + inode_delta));
    Ext2__write_u16(group_desc, 16, (uint16_t) ((int32_t) used_dirs + used_dirs_delta));

    ResultMatch(Ext2Code, Ext2Volume__write_blocks(self, 1, 1, superblock))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    return Ext2Volume__write_blocks(self, 2, 1, group_desc);
}

static Result(Ext2Code) Ext2Volume__free_inode(Ext2VolumePtr self, Ext2InodeNumber inode_number)
{
    if (inode_number == 0)
    {
        return Error(Ext2Code, invalid_argument, EXT2_INVALID_ARGUMENT);
    }

    Data inode_bitmap[EXT2_BLOCK_SIZE] = { 0 };
    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, self->inode_bitmap_block, 1, inode_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    uint32_t inode_index = inode_number - 1;
    if (!Ext2__bitmap_bit_is_set(inode_bitmap, inode_index))
    {
        return Error(Ext2Code, invalid_volume, EXT2_INVALID_VOLUME);
    }

    Ext2__bitmap_clear_bit(inode_bitmap, inode_index);
    ResultMatch(Ext2Code, Ext2Volume__write_blocks(self, self->inode_bitmap_block, 1, inode_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    return Ext2Volume__update_allocation_counts(self, 0, 1, 0);
}

static Result(Ext2Code) Ext2Volume__free_block(Ext2VolumePtr self, Ext2Block block_number)
{
    if (block_number < self->first_data_block)
    {
        return Error(Ext2Code, invalid_argument, EXT2_INVALID_ARGUMENT);
    }

    Data block_bitmap[EXT2_BLOCK_SIZE] = { 0 };
    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, self->block_bitmap_block, 1, block_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    uint32_t block_index = block_number - self->first_data_block;
    if (!Ext2__bitmap_bit_is_set(block_bitmap, block_index))
    {
        return Error(Ext2Code, invalid_volume, EXT2_INVALID_VOLUME);
    }

    Ext2__bitmap_clear_bit(block_bitmap, block_index);
    ResultMatch(Ext2Code, Ext2Volume__write_blocks(self, self->block_bitmap_block, 1, block_bitmap))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    return Ext2Volume__update_allocation_counts(self, 1, 0, 0);
}

static bool Ext2Inode__is_dir(Ext2Inode * inode)
{
    return (inode->mode & EXT2_INODE_MODE_DIR) != 0;
}

static bool Ext2Inode__is_file(Ext2Inode * inode)
{
    return (inode->mode & EXT2_INODE_MODE_FILE) != 0;
}

static Result(Ext2DirectoryEntries) Ext2Volume__list_directory_inode(
    Ext2VolumePtr self, Ext2InodeNumber inode_number
)
{
    Ext2DirectoryEntries entries = Ext2DirectoryEntries__new();
    Ext2Inode inode = { 0 };

    ResultMatch(Ext2Code, Ext2Volume__read_inode(self, inode_number, &inode))
        IsError(code, message, return Error(Ext2DirectoryEntries, message, code))
    End

    if (!Ext2Inode__is_dir(&inode))
    {
        return Error(Ext2DirectoryEntries, invalid_path, EXT2_NOT_FOUND);
    }

    for (size_t block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        if (inode.block[block_index] == 0)
        {
            continue;
        }

        Data block[EXT2_BLOCK_SIZE] = { 0 };
        ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, inode.block[block_index], 1, block))
            IsError(code, message, return Error(Ext2DirectoryEntries, message, code))
        End

        size_t offset = 0;
        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber entry_inode = Ext2__read_u32(block, offset);
            uint16_t rec_len = Ext2__read_u16(block, offset + 4);
            uint8_t name_len = block[offset + 6];
            Ext2DirectoryEntryType file_type = block[offset + 7];

            if (rec_len == 0)
            {
                break;
            }

            if (entry_inode != 0 && name_len > 0)
            {
                BString name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    name.data[i] = (char) block[offset + 8 + i];
                }
                name.len = name_len;
                name.data[name.len] = '\0';

                if (!BString__equals(&name, &BSTRING_NEW(".")) && !BString__equals(&name, &BSTRING_NEW("..")))
                {
                    Ext2Inode entry_node = { 0 };
                    ResultMatch(Ext2Code, Ext2Volume__read_inode(self, entry_inode, &entry_node))
                        IsError(code, message, return Error(Ext2DirectoryEntries, message, code))
                    End

                    ResultMatch(CollectionCode, Ext2DirectoryEntries__push(&entries, ((Ext2DirectoryEntry) {
                        .name = name,
                        .file_type = file_type,
                        .inode = entry_inode,
                        .mode = entry_node.mode,
                        .size = entry_node.size,
                    })))
                        IsError(code, message, return Error(Ext2DirectoryEntries, invalid_volume, EXT2_INVALID_VOLUME))
                    End
                }
            }

            offset += rec_len;
        }
    }

    return Success(Ext2DirectoryEntries, entries);
}

static Result(Ext2Code) Ext2Volume__lookup_child(
    Ext2VolumePtr self, Ext2InodeNumber directory_inode, BString name, Ext2InodeNumber * child_inode
)
{
    ResultMatch(Ext2DirectoryEntries, Ext2Volume__list_directory_inode(self, directory_inode))
        IsSuccess(entries,
        {
            FAIterate(Ext2DirectoryEntries, entries, index, entry)
                if (BString__equals(&entry.name, &name))
                {
                    *child_inode = entry.inode;
                    return Success(Ext2Code, EXT2_OK);
                }
            End
        })
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    return Error(Ext2Code, not_found, EXT2_NOT_FOUND);
}

static Result(Ext2Code) Ext2Volume__find_inode_at_path(
    Ext2VolumePtr self, BString path, Ext2InodeNumber * inode_number
)
{
    if (BString__equals(&path, &BSTRING_NEW("/")) || path.len == 0)
    {
        *inode_number = EXT2_ROOT_INODE;
        return Success(Ext2Code, EXT2_OK);
    }

    BStringArray segments = BString__split(&path, '/');
    Ext2InodeNumber current = EXT2_ROOT_INODE;

    FAIterate(BStringArray, segments, index, segment)
        ResultMatch(Ext2Code, Ext2Volume__lookup_child(self, current, segment, &current))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
    End

    *inode_number = current;
    return Success(Ext2Code, EXT2_OK);
}

static Result(Ext2Code) Ext2Volume__resolve_parent(
    Ext2VolumePtr self,
    BString path,
    Ext2InodeNumber * parent_inode_number,
    BString * leaf_name
)
{
    if (path.len == 0 || BString__equals(&path, &BSTRING_NEW("/")))
    {
        return Error(Ext2Code, invalid_path, EXT2_NOT_FOUND);
    }

    BStringArray segments = BString__split(&path, '/');
    if (segments.len == 0)
    {
        return Error(Ext2Code, invalid_path, EXT2_NOT_FOUND);
    }

    Ext2InodeNumber current = EXT2_ROOT_INODE;
    for (size_t i = 0; i + 1 < segments.len; i++)
    {
        ResultMatch(Ext2Code, Ext2Volume__lookup_child(self, current, segments.data[i], &current))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
    }

    *parent_inode_number = current;
    *leaf_name = segments.data[segments.len - 1];
    return Success(Ext2Code, EXT2_OK);
}

static Result(Ext2Code) Ext2Volume__remove_dirent(
    Ext2VolumePtr self,
    Ext2InodeNumber directory_inode_number,
    BString name,
    Ext2InodeNumber * removed_inode_number
)
{
    Ext2Inode directory_inode = { 0 };
    ResultMatch(Ext2Code, Ext2Volume__read_inode(self, directory_inode_number, &directory_inode))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    if (!Ext2Inode__is_dir(&directory_inode))
    {
        return Error(Ext2Code, invalid_path, EXT2_NOT_FOUND);
    }

    for (size_t block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        if (directory_inode.block[block_index] == 0)
        {
            continue;
        }

        Data block[EXT2_BLOCK_SIZE] = { 0 };
        ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, directory_inode.block[block_index], 1, block))
            IsError(code, message, return Error(Ext2Code, message, code))
        End

        size_t offset = 0;
        size_t previous_offset = 0;
        bool has_previous = false;

        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber entry_inode = Ext2__read_u32(block, offset);
            uint16_t rec_len = Ext2__read_u16(block, offset + 4);
            uint8_t name_len = block[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (entry_inode != 0 && name_len > 0)
            {
                BString entry_name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    entry_name.data[i] = (char) block[offset + 8 + i];
                }
                entry_name.len = name_len;
                entry_name.data[entry_name.len] = '\0';

                if (BString__equals(&entry_name, &name))
                {
                    *removed_inode_number = entry_inode;
                    if (has_previous)
                    {
                        uint16_t previous_len = Ext2__read_u16(block, previous_offset + 4);
                        Ext2__write_u16(block, previous_offset + 4, previous_len + rec_len);
                    }
                    else
                    {
                        Ext2__write_u32(block, offset, 0);
                    }

                    return Ext2Volume__write_blocks(self, directory_inode.block[block_index], 1, block);
                }
            }

            previous_offset = offset;
            has_previous = true;
            offset += rec_len;
        }
    }

    return Error(Ext2Code, not_found, EXT2_NOT_FOUND);
}

static BString Ext2__child_path(BString parent_path, BString child_name)
{
    if (parent_path.len == 0 || BString__equals(&parent_path, &BSTRING_NEW("/")))
    {
        return child_name;
    }

    BString with_slash = BString__append(&parent_path, &BSTRING_NEW("/"));
    return BString__append(&with_slash, &child_name);
}

static void Ext2AsyncRemoveContext__reset_for_path(
    Ext2AsyncRemoveContextPtr self,
    BString path,
    Ext2AsyncRemovePhase phase
)
{
    self->path = path;
    self->segments = BString__split(&path, '/');
    self->parent_segment_count = 0;
    self->segment_index = 0;
    self->leaf_name = EMPTY_BSTRING;
    self->phase = phase;

    self->current_inode_number = EXT2_ROOT_INODE;
    self->current_inode = (Ext2Inode) { 0 };
    self->current_dir_block_index = 0;

    self->parent_inode_number = 0;
    self->parent_dir_block = 0;
    self->parent_dirent_offset = 0;
    self->parent_previous_dirent_offset = 0;
    self->parent_has_previous = false;

    self->target_inode_number = 0;
    self->target_inode = (Ext2Inode) { 0 };
    self->target_inode_block_number = 0;
    self->verify_dir_block_index = 0;
    self->freed_block_count = 0;
    self->decrement_only = false;

    if (self->segments.len > 0)
    {
        self->parent_segment_count = self->segments.len - 1;
        self->leaf_name = self->segments.data[self->segments.len - 1];
    }
}

static Result(Ext2Code) Ext2Volume__remove_inode(
    Ext2VolumePtr self,
    Ext2InodeNumber parent_inode_number,
    BString leaf_name,
    Ext2InodeNumber target_inode_number,
    Ext2Inode * target_inode,
    Flags flags
)
{
    if (Ext2Inode__is_dir(target_inode))
    {
        ResultMatch(Ext2DirectoryEntries, Ext2Volume__list_directory_inode(self, target_inode_number))
            IsSuccess(entries,
            {
                if (entries.len > 0 && (flags & IO_REMOVE_RECURSIVE) == 0)
                {
                    return Error(Ext2Code, not_supported, EXT2_NOT_SUPPORTED);
                }

                FAIterate(Ext2DirectoryEntries, entries, index, entry)
                    Ext2Inode child_inode = { 0 };
                    ResultMatch(Ext2Code, Ext2Volume__read_inode(self, entry.inode, &child_inode))
                        IsError(code, message, return Error(Ext2Code, message, code))
                    End

                    ResultMatch(Ext2Code, Ext2Volume__remove_inode(
                        self,
                        target_inode_number,
                        entry.name,
                        entry.inode,
                        &child_inode,
                        flags
                    ))
                        IsError(code, message, return Error(Ext2Code, message, code))
                    End
                End
            })
            IsError(code, message, return Error(Ext2Code, message, code))
        End

        for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
        {
            if (target_inode->block[i] != 0)
            {
                ResultMatch(Ext2Code, Ext2Volume__free_block(self, target_inode->block[i]))
                    IsError(code, message, return Error(Ext2Code, message, code))
                End
            }
        }

        *target_inode = (Ext2Inode) { 0 };
        ResultMatch(Ext2Code, Ext2Volume__write_inode(self, target_inode_number, target_inode))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
        ResultMatch(Ext2Code, Ext2Volume__free_inode(self, target_inode_number))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
        ResultMatch(Ext2Code, Ext2Volume__update_allocation_counts(self, 0, 0, -1))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
        return Ext2Volume__remove_dirent(self, parent_inode_number, leaf_name, &target_inode_number);
    }

    if (!Ext2Inode__is_file(target_inode))
    {
        return Error(Ext2Code, invalid_path, EXT2_NOT_FOUND);
    }

    if (target_inode->links_count > 1)
    {
        target_inode->links_count--;
        ResultMatch(Ext2Code, Ext2Volume__write_inode(self, target_inode_number, target_inode))
            IsError(code, message, return Error(Ext2Code, message, code))
        End
        return Ext2Volume__remove_dirent(self, parent_inode_number, leaf_name, &target_inode_number);
    }

    for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
    {
        if (target_inode->block[i] != 0)
        {
            ResultMatch(Ext2Code, Ext2Volume__free_block(self, target_inode->block[i]))
                IsError(code, message, return Error(Ext2Code, message, code))
            End
        }
    }

    *target_inode = (Ext2Inode) { 0 };
    ResultMatch(Ext2Code, Ext2Volume__write_inode(self, target_inode_number, target_inode))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__free_inode(self, target_inode_number))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    return Ext2Volume__remove_dirent(self, parent_inode_number, leaf_name, &target_inode_number);
}

static Result(Ext2Code) Ext2Volume__remove_at_path(Ext2VolumePtr self, BString path, Flags flags)
{
    Ext2InodeNumber parent_inode_number = 0;
    BString leaf_name = EMPTY_BSTRING;
    Ext2InodeNumber target_inode_number = 0;
    Ext2Inode target_inode = { 0 };

    ResultMatch(Ext2Code, Ext2Volume__resolve_parent(self, path, &parent_inode_number, &leaf_name))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    ResultMatch(Ext2Code, Ext2Volume__lookup_child(self, parent_inode_number, leaf_name, &target_inode_number))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    ResultMatch(Ext2Code, Ext2Volume__read_inode(self, target_inode_number, &target_inode))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    return Ext2Volume__remove_inode(
        self,
        parent_inode_number,
        leaf_name,
        target_inode_number,
        &target_inode,
        flags
    );
}

static Result(Ext2Code) Ext2Volume__link_at_path(Ext2VolumePtr self, BString source_path, BString target_path)
{
    Ext2InodeNumber source_inode_number = 0;
    Ext2Inode source_inode = { 0 };
    Ext2InodeNumber target_parent_inode_number = 0;
    BString target_leaf_name = EMPTY_BSTRING;
    Ext2InodeNumber existing_inode_number = 0;

    ResultMatch(Ext2Code, Ext2Volume__find_inode_at_path(self, source_path, &source_inode_number))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__read_inode(self, source_inode_number, &source_inode))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    if (!Ext2Inode__is_file(&source_inode))
    {
        return Error(Ext2Code, not_supported, EXT2_NOT_SUPPORTED);
    }

    ResultMatch(Ext2Code, Ext2Volume__resolve_parent(self, target_path, &target_parent_inode_number, &target_leaf_name))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    Result(Ext2Code) lookup_result = Ext2Volume__lookup_child(
        self,
        target_parent_inode_number,
        target_leaf_name,
        &existing_inode_number
    );
    ResultMatch(Ext2Code, lookup_result)
        IsSuccess(code, return Error(Ext2Code, invalid_path, EXT2_NOT_FOUND))
        IsError(code, message,
        {
            if (code != EXT2_NOT_FOUND)
            {
                return Error(Ext2Code, message, code);
            }
        })
    End

    source_inode.links_count++;
    ResultMatch(Ext2Code, Ext2Volume__write_inode(self, source_inode_number, &source_inode))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__insert_dirent(
        self,
        target_parent_inode_number,
        target_leaf_name,
        source_inode_number,
        EXT2_DIRENT_TYPE_FILE
    ))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    return Success(Ext2Code, EXT2_OK);
}

Ext2Volume Ext2Volume__new(Optional(BlockDevicePtr) device, BlockCount total_sectors)
{
    return (Ext2Volume) {
        .device = device,
        .total_sectors = total_sectors,
        .blocks_count = 0,
        .blocks_per_group = 0,
        .inodes_per_group = 0,
        .inode_size = 128,
        .block_bitmap_block = 0,
        .inode_bitmap_block = 0,
        .inode_table_block = 0,
        .first_data_block = 1,
        .mounted = false,
    };
}

Result(Ext2Code) Ext2Volume__mount(Ext2VolumePtr self)
{
    Data superblock[EXT2_BLOCK_SIZE] = { 0 };
    Data group_desc[EXT2_BLOCK_SIZE] = { 0 };

    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, 1, 1, superblock))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    ResultMatch(Ext2Code, Ext2Volume__parse_superblock(self, superblock))
        IsError(code, message, return Error(Ext2Code, message, code))
    End

    ResultMatch(Ext2Code, Ext2Volume__read_blocks(self, 2, 1, group_desc))
        IsError(code, message, return Error(Ext2Code, message, code))
    End
    return Ext2Volume__parse_group_descriptor(self, group_desc);
}

Result(Ext2DirectoryEntries) Ext2Volume__list_root(Ext2VolumePtr self)
{
    if (!self->mounted)
    {
        ResultMatch(Ext2Code, Ext2Volume__mount(self))
            IsError(code, message, return Error(Ext2DirectoryEntries, message, code))
        End
    }

    return Ext2Volume__list_directory_inode(self, EXT2_ROOT_INODE);
}

static bool Ext2FilesystemIOState__uses_userspace_device(Ext2FilesystemIOStatePtr self)
{
    Match(BlockDevicePtr, self->volume.device)
        IsSome(device, return device->userspace_service)
    End

    return false;
}

static Result(IOInternalHandle) Ext2FilesystemIOState__add_handle(
    Ext2FilesystemIOStatePtr self, BString path, Flags flags, Ext2InodeNumber inode_number, Ext2Inode * inode
)
{
    Ext2FileHandle file_handle = {
        .path = path,
        .offset = 0,
        .inode_number = inode_number,
        .mode = inode->mode,
        .size = inode->size,
        .buffer = EmptyRingBuffer(IOBuffer),
        .flags = flags,
    };
    for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
    {
        file_handle.block[i] = inode->block[i];
    }

    CriticalSection(self->state.lock,
    {
        IOInternalHandle handle = (IOInternalHandle) self->handle_count++ | self->state.filesystem->fs_code;
        Ext2FileHandlePtr handle_ptr = ptr(Kernel__alloc_kernel(sizeof(Ext2FileHandle)), Ext2FileHandle);
        *handle_ptr = file_handle;
        Ext2FileHandleMap__set(&self->handles, handle, handle_ptr);
        _cs_return Success(IOInternalHandle, handle);
    })
}

static Result(Ext2FileHandlePtr) Ext2FilesystemIOState__get_handle(Ext2FilesystemIOStatePtr self, IOInternalHandle handle)
{
    CriticalSection(self->state.lock,
    {
        Match(Ext2FileHandlePtr, Ext2FileHandleMap__get(&self->handles, handle))
            IsSome(value, _cs_return Success(Ext2FileHandlePtr, value))
            IsNone(_cs_return Error(Ext2FileHandlePtr, invalid_handle, IO_UNKNOWN_HANDLE))
        End
    })

    return Error(Ext2FileHandlePtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle) Ext2FilesystemIOState__delete_handle(
    Ext2FilesystemIOStatePtr self, IOInternalHandle handle
)
{
    CriticalSection(self->state.lock,
    {
        Match(Ext2FileHandlePtr, Ext2FileHandleMap__get(&self->handles, handle))
            IsSome(value,
            {
                Kernel__free(addr(value));
                Ext2FileHandleMap__delete(&self->handles, handle);
                _cs_return Success(IOInternalHandle, handle);
            })
            IsNone(_cs_return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE))
        End
    })

    return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(Anything) Ext2FileIO__read_open_block_async(
    Ext2AsyncOpenContextPtr context,
    Ext2Block block,
    KernelContext kernel_context
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&block_context->kernel_context);
            Result(Anything) read_result = BlockDevice__read_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_open
            );
            BlockDevice__clear_current_context();
            return read_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(Anything) Ext2FileIO__read_remove_block_async(
    Ext2AsyncRemoveContextPtr context,
    Ext2Block block,
    KernelContext kernel_context
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&block_context->kernel_context);
            Result(Anything) read_result = BlockDevice__read_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_remove
            );
            BlockDevice__clear_current_context();
            return read_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(Anything) Ext2FileIO__write_remove_block_async(
    Ext2AsyncRemoveContextPtr context,
    Ext2Block block,
    DataPtr source,
    KernelContext kernel_context
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&block_context->kernel_context);
            Result(Anything) write_result = BlockDevice__write_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                source,
                block_context,
                Ext2FileIO__complete_async_remove
            );
            BlockDevice__clear_current_context();
            return write_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(Anything) Ext2FileIO__read_write_block_async(
    Ext2AsyncWriteContextPtr context,
    Ext2Block block,
    KernelContext kernel_context
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&block_context->kernel_context);
            Result(Anything) read_result = BlockDevice__read_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_write_file
            );
            BlockDevice__clear_current_context();
            return read_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(Anything) Ext2FileIO__write_write_block_async(
    Ext2AsyncWriteContextPtr context,
    Ext2Block block,
    DataPtr source,
    KernelContext kernel_context
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&block_context->kernel_context);
            Result(Anything) write_result = BlockDevice__write_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                source,
                block_context,
                Ext2FileIO__complete_async_write_file
            );
            BlockDevice__clear_current_context();
            return write_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(Anything) Ext2FileIO__read_mkdir_block_async(
    Ext2AsyncMkdirContextPtr context,
    Ext2Block block,
    KernelContext kernel_context
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&block_context->kernel_context);
            Result(Anything) read_result = BlockDevice__read_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_mkdir
            );
            BlockDevice__clear_current_context();
            return read_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(Anything) Ext2FileIO__write_mkdir_block_async(
    Ext2AsyncMkdirContextPtr context,
    Ext2Block block,
    DataPtr source,
    KernelContext kernel_context
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&block_context->kernel_context);
            Result(Anything) write_result = BlockDevice__write_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                source,
                block_context,
                Ext2FileIO__complete_async_mkdir
            );
            BlockDevice__clear_current_context();
            return write_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(Anything) Ext2FileIO__read_link_block_async(
    Ext2AsyncLinkContextPtr context,
    Ext2Block block,
    KernelContext kernel_context
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&block_context->kernel_context);
            Result(Anything) read_result = BlockDevice__read_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_link
            );
            BlockDevice__clear_current_context();
            return read_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(Anything) Ext2FileIO__write_link_block_async(
    Ext2AsyncLinkContextPtr context,
    Ext2Block block,
    DataPtr source,
    KernelContext kernel_context
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&block_context->kernel_context);
            Result(Anything) write_result = BlockDevice__write_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                source,
                block_context,
                Ext2FileIO__complete_async_link
            );
            BlockDevice__clear_current_context();
            return write_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(ByteCount) Ext2FileIO__start_async_write(
    Ext2FilesystemIOStatePtr fs_state,
    Ext2FileHandlePtr file_handle,
    DataPtr data,
    size_t size
)
{
    if (size > EXT2_MAX_READ_SIZE)
    {
        return Error(ByteCount, not_supported, IO_OPERATION_NOT_SUPPORTED);
    }

    Ext2AsyncWriteContextPtr write_context =
        ptr(Kernel__alloc_kernel(sizeof(Ext2AsyncWriteContext)), Ext2AsyncWriteContext);
    *write_context = (Ext2AsyncWriteContext) {
        .fs_state = fs_state,
        .file_handle = file_handle,
        .path = file_handle->path,
        .segments = BString__split(&file_handle->path, '/'),
        .parent_segment_count = 0,
        .segment_index = 0,
        .leaf_name = EMPTY_BSTRING,
        .phase = fs_state->volume.mounted ? EXT2_ASYNC_WRITE_READ_PARENT_INODE : EXT2_ASYNC_WRITE_READ_SUPERBLOCK,
        .current_inode_number = EXT2_ROOT_INODE,
        .current_inode = { 0 },
        .current_dir_block_index = 0,
        .parent_inode_number = 0,
        .parent_dir_block = 0,
        .found_target = false,
        .creating = false,
        .needs_parent_dir_write = false,
        .insert_prev_offset = 0,
        .insert_offset = 0,
        .insert_uses_empty_slot = false,
        .target_inode_number = 0,
        .target_inode = { 0 },
        .target_inode_block_number = 0,
        .write_size = size,
        .blocks_needed = (size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE,
        .data_block_index = 0,
        .free_block_delta = 0,
        .free_inode_delta = 0,
    };

    if (write_context->segments.len == 0)
    {
        return Error(ByteCount, invalid_path, IO_INVALID_PATH);
    }

    write_context->parent_segment_count = write_context->segments.len - 1;
    write_context->leaf_name = write_context->segments.data[write_context->segments.len - 1];
    for (size_t i = 0; i < size; i++)
    {
        write_context->write_data[i] = data[i];
    }

    Match(BlockDevicePtr, fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = EMPTY_KERNEL_CONTEXT;
            block_context->owner_context = (Anything) write_context;

            if (!fs_state->volume.mounted)
            {
                ResultMatch(Anything, BlockDevice__read_async(
                    device,
                    EXT2_SUPERBLOCK_SECTOR,
                    EXT2_BLOCK_SECTORS,
                    block_context,
                    Ext2FileIO__complete_async_write_file
                ))
                    IsSuccess(count, return Error(ByteCount, io_error, IO_WAIT_FOR_DATA))
                    IsError(code, message, return Error(ByteCount, message, code))
                End
            }

            ResultMatch(Anything, BlockDevice__read_async(
                device,
                (BlockNumber) Ext2Volume__inode_block(&fs_state->volume, write_context->current_inode_number)
                    * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_write_file
            ))
                IsSuccess(count, return Error(ByteCount, io_error, IO_WAIT_FOR_DATA))
                IsError(code, message, return Error(ByteCount, message, code))
            End
        })
        IsNone(return Error(ByteCount, invalid_argument, IO_INVALID_PATH))
    End

    return Error(ByteCount, io_error, IO_PANIC);
}

static Result(IOInternalHandle) Ext2FileIO__start_async_open(
    Ext2FilesystemIOStatePtr fs_state,
    BString path,
    Flags flags
)
{
    Ext2AsyncOpenContextPtr open_context =
        ptr(Kernel__alloc_kernel(sizeof(Ext2AsyncOpenContext)), Ext2AsyncOpenContext);
    *open_context = (Ext2AsyncOpenContext) {
        .fs_state = fs_state,
        .path = path,
        .flags = flags,
        .segments = BString__split(&path, '/'),
        .segment_index = 0,
        .current_inode_number = EXT2_ROOT_INODE,
        .current_inode = { 0 },
        .current_dir_block_index = 0,
        .phase = fs_state->volume.mounted ? EXT2_ASYNC_LOOKUP_READ_INODE : EXT2_ASYNC_LOOKUP_READ_SUPERBLOCK,
    };

    Match(BlockDevicePtr, fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = EMPTY_KERNEL_CONTEXT;
            Match(KernelContextPtr, BlockDevice__current_context())
                IsSome(kernel_context, block_context->kernel_context = *kernel_context)
            End
            block_context->owner_context = (Anything) open_context;

            if (!fs_state->volume.mounted)
            {
                ResultMatch(Anything, BlockDevice__read_async(
                    device,
                    EXT2_SUPERBLOCK_SECTOR,
                    EXT2_BLOCK_SECTORS,
                    block_context,
                    Ext2FileIO__complete_async_open
                ))
                    IsSuccess(handle, return Success(IOInternalHandle, handle))
                    IsError(code, message, return Error(IOInternalHandle, message, code))
                End
            }

            ResultMatch(Anything, BlockDevice__read_async(
                device,
                (BlockNumber) Ext2Volume__inode_block(&fs_state->volume, open_context->current_inode_number)
                    * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_open
            ))
                IsSuccess(handle, return Success(IOInternalHandle, handle))
                IsError(code, message, return Error(IOInternalHandle, message, code))
            End
        })
        IsNone(return Error(IOInternalHandle, invalid_argument, IO_INVALID_PATH))
    End

    return Error(IOInternalHandle, io_error, IO_PANIC);
}

static Result(IOStatus) Ext2FileIO__start_async_remove(
    Ext2FilesystemIOStatePtr fs_state,
    BString path,
    Flags flags
)
{
    Ext2AsyncRemoveContextPtr remove_context =
        ptr(Kernel__alloc_kernel(sizeof(Ext2AsyncRemoveContext)), Ext2AsyncRemoveContext);
    *remove_context = (Ext2AsyncRemoveContext) {
        .fs_state = fs_state,
        .path = path,
        .flags = flags,
        .segments = EmptyArray(BStringArray),
        .resume_paths = EmptyArray(BStringArray),
    };

    Ext2AsyncRemoveContext__reset_for_path(
        remove_context,
        path,
        fs_state->volume.mounted ? EXT2_ASYNC_REMOVE_READ_PARENT_INODE : EXT2_ASYNC_REMOVE_READ_SUPERBLOCK
    );

    if (remove_context->segments.len == 0)
    {
        return Error(IOStatus, invalid_path, IO_INVALID_PATH);
    }

    Match(BlockDevicePtr, fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = EMPTY_KERNEL_CONTEXT;
            block_context->owner_context = (Anything) remove_context;

            if (!fs_state->volume.mounted)
            {
                ResultMatch(Anything, BlockDevice__read_async(
                    device,
                    EXT2_SUPERBLOCK_SECTOR,
                    EXT2_BLOCK_SECTORS,
                    block_context,
                    Ext2FileIO__complete_async_remove
                ))
                    IsSuccess(count, return Error(IOStatus, io_error, IO_WAIT_FOR_DATA))
                    IsError(code, message, return Error(IOStatus, message, code))
                End
            }

            ResultMatch(Anything, BlockDevice__read_async(
                device,
                (BlockNumber) Ext2Volume__inode_block(&fs_state->volume, remove_context->current_inode_number)
                    * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_remove
            ))
                IsSuccess(count, return Error(IOStatus, io_error, IO_WAIT_FOR_DATA))
                IsError(code, message, return Error(IOStatus, message, code))
            End
        })
        IsNone(return Error(IOStatus, invalid_argument, IO_INVALID_PATH))
    End

    return Error(IOStatus, io_error, IO_PANIC);
}

static Result(IOStatus) Ext2FileIO__start_async_mkdir(
    Ext2FilesystemIOStatePtr fs_state,
    BString path
)
{
    Ext2AsyncMkdirContextPtr mkdir_context =
        ptr(Kernel__alloc_kernel(sizeof(Ext2AsyncMkdirContext)), Ext2AsyncMkdirContext);
    *mkdir_context = (Ext2AsyncMkdirContext) {
        .fs_state = fs_state,
        .path = path,
        .segments = BString__split(&path, '/'),
        .parent_segment_count = 0,
        .segment_index = 0,
        .leaf_name = EMPTY_BSTRING,
        .phase = fs_state->volume.mounted ? EXT2_ASYNC_MKDIR_READ_PARENT_INODE : EXT2_ASYNC_MKDIR_READ_SUPERBLOCK,
        .current_inode_number = EXT2_ROOT_INODE,
        .current_inode = { 0 },
        .current_dir_block_index = 0,
        .parent_inode_number = 0,
        .parent_dir_block = 0,
        .needs_parent_dir_write = false,
        .insert_prev_offset = 0,
        .insert_offset = 0,
        .insert_uses_empty_slot = false,
        .target_inode_number = 0,
        .target_inode = { 0 },
        .target_inode_block_number = 0,
    };

    if (mkdir_context->segments.len == 0)
    {
        return Error(IOStatus, invalid_path, IO_INVALID_PATH);
    }

    mkdir_context->parent_segment_count = mkdir_context->segments.len - 1;
    mkdir_context->leaf_name = mkdir_context->segments.data[mkdir_context->segments.len - 1];

    Match(BlockDevicePtr, fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = EMPTY_KERNEL_CONTEXT;
            block_context->owner_context = (Anything) mkdir_context;

            if (!fs_state->volume.mounted)
            {
                ResultMatch(Anything, BlockDevice__read_async(
                    device,
                    EXT2_SUPERBLOCK_SECTOR,
                    EXT2_BLOCK_SECTORS,
                    block_context,
                    Ext2FileIO__complete_async_mkdir
                ))
                    IsSuccess(count, return Error(IOStatus, io_error, IO_WAIT_FOR_DATA))
                    IsError(code, message, return Error(IOStatus, message, code))
                End
            }

            ResultMatch(Anything, BlockDevice__read_async(
                device,
                (BlockNumber) Ext2Volume__inode_block(&fs_state->volume, mkdir_context->current_inode_number)
                    * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_mkdir
            ))
                IsSuccess(count, return Error(IOStatus, io_error, IO_WAIT_FOR_DATA))
                IsError(code, message, return Error(IOStatus, message, code))
            End
        })
        IsNone(return Error(IOStatus, invalid_argument, IO_INVALID_PATH))
    End

    return Error(IOStatus, io_error, IO_PANIC);
}

static Result(IOStatus) Ext2FileIO__start_async_link(
    Ext2FilesystemIOStatePtr fs_state,
    BString source_path,
    BString target_path
)
{
    Ext2AsyncLinkContextPtr link_context =
        ptr(Kernel__alloc_kernel(sizeof(Ext2AsyncLinkContext)), Ext2AsyncLinkContext);
    *link_context = (Ext2AsyncLinkContext) {
        .fs_state = fs_state,
        .source_path = source_path,
        .target_path = target_path,
        .source_segments = BString__split(&source_path, '/'),
        .target_segments = BString__split(&target_path, '/'),
        .segment_index = 0,
        .target_parent_segment_count = 0,
        .target_leaf_name = EMPTY_BSTRING,
        .traversing_target_parent = false,
        .phase = fs_state->volume.mounted ? EXT2_ASYNC_LINK_READ_SOURCE_INODE : EXT2_ASYNC_LINK_READ_SUPERBLOCK,
        .current_inode_number = EXT2_ROOT_INODE,
        .current_inode = { 0 },
        .current_dir_block_index = 0,
        .source_inode_number = 0,
        .source_inode = { 0 },
        .source_inode_block_number = 0,
        .target_parent_inode_number = 0,
        .target_parent_dir_block = 0,
        .needs_parent_dir_write = false,
        .insert_prev_offset = 0,
        .insert_offset = 0,
        .insert_uses_empty_slot = false,
    };

    if (link_context->target_segments.len == 0)
    {
        return Error(IOStatus, invalid_path, IO_INVALID_PATH);
    }

    link_context->target_parent_segment_count = link_context->target_segments.len - 1;
    link_context->target_leaf_name = link_context->target_segments.data[link_context->target_segments.len - 1];

    Match(BlockDevicePtr, fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = EMPTY_KERNEL_CONTEXT;
            Match(KernelContextPtr, BlockDevice__current_context())
                IsSome(kernel_context, block_context->kernel_context = *kernel_context)
            End
            block_context->owner_context = (Anything) link_context;

            if (!fs_state->volume.mounted)
            {
                ResultMatch(Anything, BlockDevice__read_async(
                    device,
                    EXT2_SUPERBLOCK_SECTOR,
                    EXT2_BLOCK_SECTORS,
                    block_context,
                    Ext2FileIO__complete_async_link
                ))
                    IsSuccess(count, return Error(IOStatus, io_error, IO_WAIT_FOR_DATA))
                    IsError(code, message, return Error(IOStatus, message, code))
                End
            }

            ResultMatch(Anything, BlockDevice__read_async(
                device,
                (BlockNumber) Ext2Volume__inode_block(&fs_state->volume, link_context->current_inode_number)
                    * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_link
            ))
                IsSuccess(count, return Error(IOStatus, io_error, IO_WAIT_FOR_DATA))
                IsError(code, message, return Error(IOStatus, message, code))
            End
        })
        IsNone(return Error(IOStatus, invalid_argument, IO_INVALID_PATH))
    End

    return Error(IOStatus, io_error, IO_PANIC);
}

static Result(IOInternalHandle) Ext2FileIO__open(IOStatePtr self, BString path, Flags flags)
{
    Ext2FilesystemIOStatePtr fs_state = (Ext2FilesystemIOStatePtr) self;
    Ext2InodeNumber inode_number = 0;
    Ext2Inode inode = { 0 };

    if (Ext2FilesystemIOState__uses_userspace_device(fs_state))
    {
        if ((flags & IO_OPEN_WRITE) != 0 && (flags & IO_OPEN_READ) == 0)
        {
            Ext2Inode__reset_file(&inode, 0);
            return Ext2FilesystemIOState__add_handle(fs_state, path, flags, 0, &inode);
        }

        return Ext2FileIO__start_async_open(fs_state, path, flags);
    }

    if ((flags & IO_OPEN_WRITE) != 0)
    {
        if (!fs_state->volume.mounted)
        {
            ResultMatch(Ext2Code, Ext2Volume__mount(&fs_state->volume))
                IsError(code, message, return Error(IOInternalHandle, message, IO_INVALID_PATH))
            End
        }

        Result(Ext2Code) find_result = Ext2Volume__find_inode_at_path(&fs_state->volume, path, &inode_number);
        ResultMatch(Ext2Code, find_result)
            IsSuccess(code,
            {
                ResultMatch(Ext2Code, Ext2Volume__read_inode(&fs_state->volume, inode_number, &inode))
                    IsError(inner_code, message, return Error(IOInternalHandle, message, IO_INVALID_PATH))
                End
            })
            IsError(code, message,
            {
                if (code != EXT2_NOT_FOUND)
                {
                    return Error(IOInternalHandle, message, IO_INVALID_PATH);
                }

                if ((flags & IO_OPEN_READ) != 0)
                {
                    return Error(IOInternalHandle, message, IO_INVALID_PATH);
                }

                ResultMatch(Ext2Code, Ext2Volume__resolve_parent(&fs_state->volume, path, &inode_number, &path))
                    IsError(inner_code, inner_message, return Error(IOInternalHandle, inner_message, IO_INVALID_PATH))
                End

                Ext2Inode__reset_file(&inode, 0);
                inode_number = 0;
            })
        End

        return Ext2FilesystemIOState__add_handle(fs_state, path, flags, inode_number, &inode);
    }

    if (!fs_state->volume.mounted)
    {
        ResultMatch(Ext2Code, Ext2Volume__mount(&fs_state->volume))
            IsError(code, message, return Error(IOInternalHandle, message, IO_INVALID_PATH))
        End
    }

        ResultMatch(Ext2Code, Ext2Volume__find_inode_at_path(&fs_state->volume, path, &inode_number))
        IsError(code, message, return Error(IOInternalHandle, message, IO_INVALID_PATH))
    End

    ResultMatch(Ext2Code, Ext2Volume__read_inode(&fs_state->volume, inode_number, &inode))
        IsError(code, message, return Error(IOInternalHandle, message, IO_INVALID_PATH))
    End

    return Ext2FilesystemIOState__add_handle(fs_state, path, flags, inode_number, &inode);
}

static Result(IOInternalHandle) Ext2FileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    return Ext2FilesystemIOState__delete_handle((Ext2FilesystemIOStatePtr) self, handle);
}

static Result(IOBufferPtr) Ext2FileIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    Ext2FilesystemIOStatePtr fs_state = (Ext2FilesystemIOStatePtr) self;

    if (Ext2FilesystemIOState__uses_userspace_device(fs_state))
    {
        ResultMatch(Ext2FileHandlePtr, Ext2FilesystemIOState__get_handle(fs_state, handle))
            IsSuccess(target,
            {
                if ((target->flags & IO_OPEN_READ) == 0)
                {
                    return Error(IOBufferPtr, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                if (!Ext2Inode__is_file(&(Ext2Inode) {
                    .mode = target->mode,
                    .size = target->size,
                }))
                {
                    return Error(IOBufferPtr, invalid_path, IO_INVALID_PATH);
                }

                return Ext2FileIO__start_async_read(fs_state, target, size, target_buffer);
            })
            IsError(code, message, return Error(IOBufferPtr, message, code))
        End
    }

    ResultMatch(Ext2FileHandlePtr, Ext2FilesystemIOState__get_handle(fs_state, handle))
        IsSuccess(target,
        {
            if ((target->flags & IO_OPEN_READ) == 0)
            {
                return Error(IOBufferPtr, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
            }

            if (!Ext2Inode__is_file(&(Ext2Inode) {
                .mode = target->mode,
                .size = target->size,
            }))
            {
                return Error(IOBufferPtr, invalid_path, IO_INVALID_PATH);
            }

            if (target->offset >= target->size)
            {
                return Error(IOBufferPtr, not_found, IO_EOF);
            }

            size_t block_index = target->offset / EXT2_BLOCK_SIZE;
            if (block_index >= EXT2_NDIR_BLOCKS || target->block[block_index] == 0)
            {
                return Error(IOBufferPtr, invalid_path, IO_INVALID_PATH);
            }

            Data block[EXT2_BLOCK_SIZE] = { 0 };
            ResultMatch(Ext2Code, Ext2Volume__read_blocks(&fs_state->volume, target->block[block_index], 1, block))
                IsError(code, message, return Error(IOBufferPtr, message, IO_INVALID_PATH))
            End

            size_t block_offset = target->offset % EXT2_BLOCK_SIZE;
            size_t remaining = target->size - target->offset;
            size_t remaining_in_block = EXT2_BLOCK_SIZE - block_offset;
            size_t read_size = remaining < remaining_in_block ? remaining : remaining_in_block;
            if (read_size > size)
            {
                read_size = size;
            }

            IOBuffer__push_all(&target->buffer, &block[block_offset], read_size);
            target->offset += read_size;
            return Success(IOBufferPtr, &target->buffer);
        })
        IsError(code, message, return Error(IOBufferPtr, message, code))
    End

    return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOBufferPtr) Ext2FileIO__start_async_read(
    Ext2FilesystemIOStatePtr fs_state,
    Ext2FileHandlePtr file_handle,
    size_t size,
    ExternalBuffer target_buffer
)
{
    if (file_handle->offset >= file_handle->size)
    {
        return Error(IOBufferPtr, not_found, IO_EOF);
    }

    size_t block_index = file_handle->offset / EXT2_BLOCK_SIZE;
    if (block_index >= EXT2_NDIR_BLOCKS || file_handle->block[block_index] == 0)
    {
        return Error(IOBufferPtr, invalid_path, IO_INVALID_PATH);
    }

    size_t block_offset = file_handle->offset % EXT2_BLOCK_SIZE;
    size_t remaining_in_file = file_handle->size - file_handle->offset;
    size_t remaining_in_block = EXT2_BLOCK_SIZE - block_offset;
    size_t read_size = remaining_in_file < remaining_in_block ? remaining_in_file : remaining_in_block;
    if (read_size > size)
    {
        read_size = size;
    }
    if (read_size > target_buffer.len)
    {
        read_size = target_buffer.len;
    }

    Ext2AsyncReadContextPtr read_context =
        ptr(Kernel__alloc_kernel(sizeof(Ext2AsyncReadContext)), Ext2AsyncReadContext);
    *read_context = (Ext2AsyncReadContext) {
        .fs_state = fs_state,
        .file_handle = file_handle,
        .target_buffer = target_buffer,
        .requested_size = size,
        .read_size = read_size,
        .target_block = file_handle->block[block_index],
    };

    Match(BlockDevicePtr, fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = EMPTY_KERNEL_CONTEXT;
            block_context->owner_context = (Anything) read_context;

            ResultMatch(Anything, BlockDevice__read_async(
                device,
                (BlockNumber) read_context->target_block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                Ext2FileIO__complete_async_read_file
            ))
                IsSuccess(count, return Error(IOBufferPtr, io_error, IO_WAIT_FOR_DATA))
                IsError(code, message, return Error(IOBufferPtr, message, code))
            End
        })
        IsNone(return Error(IOBufferPtr, invalid_argument, IO_INVALID_PATH))
    End

    return Error(IOBufferPtr, io_error, IO_PANIC);
}

static Result(Anything) Ext2FileIO__complete_async_read_file(BlockIOContextPtr block_context)
{
    Ext2AsyncReadContextPtr context = ptr(block_context->owner_context, Ext2AsyncReadContext);
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    size_t offset = context->file_handle->offset % EXT2_BLOCK_SIZE;
    if (offset + context->read_size > block_context->buffer_size)
    {
        return Error(Anything, invalid_path, IO_INVALID_PATH);
    }

    for (size_t i = 0; i < context->read_size; i++)
    {
        context->target_buffer.data[i] = block_context->buffer[offset + i];
    }
    context->file_handle->offset += context->read_size;
    return Success(Anything, context->read_size);
}

static Result(Anything) Ext2FileIO__complete_async_open(BlockIOContextPtr block_context)
{
    Ext2AsyncOpenContextPtr context = ptr(block_context->owner_context, Ext2AsyncOpenContext);

    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    if (context->phase == EXT2_ASYNC_LOOKUP_READ_SUPERBLOCK)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_superblock(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_LOOKUP_READ_GROUP_DESC;
        return Ext2FileIO__read_open_block_async(context, 2, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_LOOKUP_READ_GROUP_DESC)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_group_descriptor(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_LOOKUP_READ_INODE;
        return Ext2FileIO__read_open_block_async(
            context,
            Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_LOOKUP_READ_INODE)
    {
        Ext2Inode__read_from_block(
            &context->fs_state->volume,
            context->current_inode_number,
            block_context->buffer,
            &context->current_inode
        );

        if (context->segment_index >= context->segments.len)
        {
            ResultMatch(IOInternalHandle, Ext2FilesystemIOState__add_handle(
                context->fs_state,
                context->path,
                context->flags,
                context->current_inode_number,
                &context->current_inode
            ))
                IsSuccess(handle, return Success(Anything, handle))
                IsError(code, message, return Error(Anything, message, code))
            End
        }

        if (!Ext2Inode__is_dir(&context->current_inode))
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->current_dir_block_index = 0;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, not_found, IO_INVALID_PATH);
        }

        context->phase = EXT2_ASYNC_LOOKUP_READ_DIR_BLOCK;
        return Ext2FileIO__read_open_block_async(
            context,
            context->current_inode.block[context->current_dir_block_index],
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_LOOKUP_READ_DIR_BLOCK)
    {
        BString target_name = context->segments.data[context->segment_index];
        size_t offset = 0;

        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
            uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
            uint8_t name_len = block_context->buffer[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (inode != 0 && name_len > 0)
            {
                BString entry_name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                }
                entry_name.len = name_len;
                entry_name.data[entry_name.len] = '\0';

                if (BString__equals(&entry_name, &target_name))
                {
                    context->current_inode_number = inode;
                    context->segment_index++;
                    context->phase = EXT2_ASYNC_LOOKUP_READ_INODE;
                    return Ext2FileIO__read_open_block_async(
                        context,
                        Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
                        block_context->kernel_context
                    );
                }
            }

            offset += rec_len;
        }

        context->current_dir_block_index++;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, not_found, IO_INVALID_PATH);
        }

        return Ext2FileIO__read_open_block_async(
            context,
            context->current_inode.block[context->current_dir_block_index],
            block_context->kernel_context
        );
    }

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Ext2FileIO__complete_async_link(BlockIOContextPtr block_context)
{
    Ext2AsyncLinkContextPtr context = ptr(block_context->owner_context, Ext2AsyncLinkContext);

    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    if (context->phase == EXT2_ASYNC_LINK_READ_SUPERBLOCK)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_superblock(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_LINK_READ_GROUP_DESC;
        return Ext2FileIO__read_link_block_async(context, 2, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_LINK_READ_GROUP_DESC)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_group_descriptor(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_LINK_READ_SOURCE_INODE;
        return Ext2FileIO__read_link_block_async(
            context,
            Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_LINK_READ_SOURCE_INODE || context->phase == EXT2_ASYNC_LINK_READ_TARGET_PARENT_INODE)
    {
        Ext2Inode__read_from_block(
            &context->fs_state->volume,
            context->current_inode_number,
            block_context->buffer,
            &context->current_inode
        );

        bool lookup_complete = context->traversing_target_parent
            ? context->segment_index >= context->target_parent_segment_count
            : context->segment_index >= context->source_segments.len;

        if (lookup_complete)
        {
            if (context->traversing_target_parent)
            {
                if (!Ext2Inode__is_dir(&context->current_inode))
                {
                    return Error(Anything, invalid_path, IO_INVALID_PATH);
                }

                context->current_dir_block_index = 0;
                while (
                    context->current_dir_block_index < EXT2_NDIR_BLOCKS
                    && context->current_inode.block[context->current_dir_block_index] == 0
                )
                {
                    context->current_dir_block_index++;
                }

                if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
                {
                    return Error(Anything, invalid_path, IO_INVALID_PATH);
                }

                context->phase = EXT2_ASYNC_LINK_READ_TARGET_PARENT_DIR_BLOCK;
                return Ext2FileIO__read_link_block_async(
                    context,
                    context->current_inode.block[context->current_dir_block_index],
                    block_context->kernel_context
                );
            }

            context->source_inode_number = context->current_inode_number;
            context->source_inode = context->current_inode;
            context->source_inode_block_number =
                Ext2Volume__inode_block(&context->fs_state->volume, context->source_inode_number);

            if (!Ext2Inode__is_file(&context->source_inode))
            {
                return Error(Anything, not_supported, IO_OPERATION_NOT_SUPPORTED);
            }

            context->traversing_target_parent = true;
            context->segment_index = 0;
            context->current_inode_number = EXT2_ROOT_INODE;
            context->current_inode = (Ext2Inode) { 0 };
            context->current_dir_block_index = 0;
            context->phase = EXT2_ASYNC_LINK_READ_TARGET_PARENT_INODE;
            return Ext2FileIO__read_link_block_async(
                context,
                Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
                block_context->kernel_context
            );
        }

        if (!Ext2Inode__is_dir(&context->current_inode))
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->current_dir_block_index = 0;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->phase = context->traversing_target_parent
            ? EXT2_ASYNC_LINK_READ_TARGET_PARENT_DIR_BLOCK
            : EXT2_ASYNC_LINK_READ_SOURCE_DIR_BLOCK;
        return Ext2FileIO__read_link_block_async(
            context,
            context->current_inode.block[context->current_dir_block_index],
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_LINK_READ_SOURCE_DIR_BLOCK || context->phase == EXT2_ASYNC_LINK_READ_TARGET_PARENT_DIR_BLOCK)
    {
        size_t offset = 0;
        bool traversing_target = context->phase == EXT2_ASYNC_LINK_READ_TARGET_PARENT_DIR_BLOCK;

        if (traversing_target && context->segment_index >= context->target_parent_segment_count)
        {
            uint16_t required_len = Ext2__dirent_record_size(context->target_leaf_name.len);
            while (offset + 8 <= EXT2_BLOCK_SIZE)
            {
                Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
                uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
                uint8_t name_len = block_context->buffer[offset + 6];

                if (rec_len == 0)
                {
                    break;
                }

                if (inode != 0 && name_len > 0)
                {
                    BString entry_name = EMPTY_BSTRING;
                    for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                    {
                        entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                    }
                    entry_name.len = name_len;
                    entry_name.data[entry_name.len] = '\0';

                    if (BString__equals(&entry_name, &context->target_leaf_name))
                    {
                        return Error(Anything, invalid_path, IO_INVALID_PATH);
                    }

                    uint16_t actual_len = Ext2__dirent_record_size(name_len);
                    if (!context->needs_parent_dir_write && rec_len >= actual_len + required_len)
                    {
                        context->needs_parent_dir_write = true;
                        context->insert_uses_empty_slot = false;
                        context->insert_prev_offset = offset;
                        context->insert_offset = offset + actual_len;
                        context->target_parent_inode_number = context->current_inode_number;
                        context->target_parent_dir_block = context->current_inode.block[context->current_dir_block_index];
                        Kernel__memcopy(
                            addr(block_context->buffer),
                            addr(context->target_parent_dir_block_buffer),
                            EXT2_BLOCK_SIZE
                        );
                    }
                }
                else if (!context->needs_parent_dir_write && rec_len >= required_len)
                {
                    context->needs_parent_dir_write = true;
                    context->insert_uses_empty_slot = true;
                    context->insert_prev_offset = 0;
                    context->insert_offset = offset;
                    context->target_parent_inode_number = context->current_inode_number;
                    context->target_parent_dir_block = context->current_inode.block[context->current_dir_block_index];
                    Kernel__memcopy(
                        addr(block_context->buffer),
                        addr(context->target_parent_dir_block_buffer),
                        EXT2_BLOCK_SIZE
                    );
                }

                offset += rec_len;
            }

            context->current_dir_block_index++;
            while (
                context->current_dir_block_index < EXT2_NDIR_BLOCKS
                && context->current_inode.block[context->current_dir_block_index] == 0
            )
            {
                context->current_dir_block_index++;
            }

            if (context->current_dir_block_index < EXT2_NDIR_BLOCKS)
            {
                return Ext2FileIO__read_link_block_async(
                    context,
                    context->current_inode.block[context->current_dir_block_index],
                    block_context->kernel_context
                );
            }

            if (!context->needs_parent_dir_write)
            {
                return Error(Anything, not_supported, IO_OPERATION_NOT_SUPPORTED);
            }

            context->phase = EXT2_ASYNC_LINK_READ_SOURCE_INODE_BLOCK;
            return Ext2FileIO__read_link_block_async(
                context,
                context->source_inode_block_number,
                block_context->kernel_context
            );
        }

        BString target_name = traversing_target
            ? context->target_segments.data[context->segment_index]
            : context->source_segments.data[context->segment_index];

        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
            uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
            uint8_t name_len = block_context->buffer[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (inode != 0 && name_len > 0)
            {
                BString entry_name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                }
                entry_name.len = name_len;
                entry_name.data[entry_name.len] = '\0';

                if (BString__equals(&entry_name, &target_name))
                {
                    context->current_inode_number = inode;
                    context->segment_index++;
                    context->phase = traversing_target
                        ? EXT2_ASYNC_LINK_READ_TARGET_PARENT_INODE
                        : EXT2_ASYNC_LINK_READ_SOURCE_INODE;
                    return Ext2FileIO__read_link_block_async(
                        context,
                        Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
                        block_context->kernel_context
                    );
                }
            }

            offset += rec_len;
        }

        context->current_dir_block_index++;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        return Ext2FileIO__read_link_block_async(
            context,
            context->current_inode.block[context->current_dir_block_index],
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_LINK_READ_SOURCE_INODE_BLOCK)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->source_inode_block_buffer), EXT2_BLOCK_SIZE);
        context->source_inode.links_count++;

        size_t inode_offset = Ext2Volume__inode_offset(&context->fs_state->volume, context->source_inode_number);
        Ext2__write_u16(context->source_inode_block_buffer, inode_offset + 26, context->source_inode.links_count);

        context->phase = EXT2_ASYNC_LINK_WRITE_SOURCE_INODE_BLOCK;
        return Ext2FileIO__write_link_block_async(
            context,
            context->source_inode_block_number,
            context->source_inode_block_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_LINK_WRITE_SOURCE_INODE_BLOCK)
    {
        if (context->insert_uses_empty_slot)
        {
            uint16_t rec_len = Ext2__read_u16(context->target_parent_dir_block_buffer, context->insert_offset + 4);
            Ext2__write_u32(
                context->target_parent_dir_block_buffer,
                context->insert_offset,
                context->source_inode_number
            );
            Ext2__write_u16(context->target_parent_dir_block_buffer, context->insert_offset + 4, rec_len);
        }
        else
        {
            uint16_t old_len = Ext2__read_u16(context->target_parent_dir_block_buffer, context->insert_prev_offset + 4);
            uint8_t prev_name_len = context->target_parent_dir_block_buffer[context->insert_prev_offset + 6];
            uint16_t actual_len = Ext2__dirent_record_size(prev_name_len);
            uint16_t new_len = old_len - actual_len;
            Ext2__write_u16(context->target_parent_dir_block_buffer, context->insert_prev_offset + 4, actual_len);
            Ext2__write_u32(context->target_parent_dir_block_buffer, context->insert_offset, context->source_inode_number);
            Ext2__write_u16(context->target_parent_dir_block_buffer, context->insert_offset + 4, new_len);
        }

        context->target_parent_dir_block_buffer[context->insert_offset + 6] = (Data) context->target_leaf_name.len;
        context->target_parent_dir_block_buffer[context->insert_offset + 7] = (Data) EXT2_DIRENT_TYPE_FILE;
        for (size_t i = 0; i < context->target_leaf_name.len; i++)
        {
            context->target_parent_dir_block_buffer[context->insert_offset + 8 + i] =
                (Data) context->target_leaf_name.data[i];
        }

        context->phase = EXT2_ASYNC_LINK_WRITE_TARGET_PARENT_DIR_BLOCK;
        return Ext2FileIO__write_link_block_async(
            context,
            context->target_parent_dir_block,
            context->target_parent_dir_block_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_LINK_WRITE_TARGET_PARENT_DIR_BLOCK)
    {
        return Success(Anything, IO_OK);
    }

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Ext2FileIO__complete_async_write_file(BlockIOContextPtr block_context)
{
    Ext2AsyncWriteContextPtr context = ptr(block_context->owner_context, Ext2AsyncWriteContext);

    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_SUPERBLOCK)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_superblock(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_WRITE_READ_GROUP_DESC;
        return Ext2FileIO__read_write_block_async(context, 2, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_GROUP_DESC)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_group_descriptor(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_WRITE_READ_PARENT_INODE;
        return Ext2FileIO__read_write_block_async(
            context,
            Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_PARENT_INODE)
    {
        Ext2Inode__read_from_block(
            &context->fs_state->volume,
            context->current_inode_number,
            block_context->buffer,
            &context->current_inode
        );

        if (!Ext2Inode__is_dir(&context->current_inode))
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->current_dir_block_index = 0;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->phase = EXT2_ASYNC_WRITE_READ_PARENT_DIR_BLOCK;
        return Ext2FileIO__read_write_block_async(
            context,
            context->current_inode.block[context->current_dir_block_index],
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_PARENT_DIR_BLOCK)
    {
        size_t offset = 0;

        if (context->segment_index < context->parent_segment_count)
        {
            BString target_name = context->segments.data[context->segment_index];
            while (offset + 8 <= EXT2_BLOCK_SIZE)
            {
                Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
                uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
                uint8_t name_len = block_context->buffer[offset + 6];

                if (rec_len == 0)
                {
                    break;
                }

                if (inode != 0 && name_len > 0)
                {
                    BString entry_name = EMPTY_BSTRING;
                    for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                    {
                        entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                    }
                    entry_name.len = name_len;
                    entry_name.data[entry_name.len] = '\0';

                    if (BString__equals(&entry_name, &target_name))
                    {
                        context->current_inode_number = inode;
                        context->segment_index++;
                        context->phase = EXT2_ASYNC_WRITE_READ_PARENT_INODE;
                        return Ext2FileIO__read_write_block_async(
                            context,
                            Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
                            block_context->kernel_context
                        );
                    }
                }

                offset += rec_len;
            }

            context->current_dir_block_index++;
            while (
                context->current_dir_block_index < EXT2_NDIR_BLOCKS
                && context->current_inode.block[context->current_dir_block_index] == 0
            )
            {
                context->current_dir_block_index++;
            }

            if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
            {
                return Error(Anything, invalid_path, IO_INVALID_PATH);
            }

            return Ext2FileIO__read_write_block_async(
                context,
                context->current_inode.block[context->current_dir_block_index],
                block_context->kernel_context
            );
        }

        uint16_t required_len = Ext2__dirent_record_size(context->leaf_name.len);
        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
            uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
            uint8_t name_len = block_context->buffer[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (inode != 0 && name_len > 0)
            {
                BString entry_name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                }
                entry_name.len = name_len;
                entry_name.data[entry_name.len] = '\0';

                if (BString__equals(&entry_name, &context->leaf_name))
                {
                    context->found_target = true;
                    context->creating = false;
                    context->target_inode_number = inode;
                    Kernel__memcopy(addr(block_context->buffer), addr(context->parent_dir_block_buffer), EXT2_BLOCK_SIZE);
                    context->parent_inode_number = context->current_inode_number;
                    context->parent_dir_block = context->current_inode.block[context->current_dir_block_index];
                    context->phase = EXT2_ASYNC_WRITE_READ_TARGET_INODE;
                    return Ext2FileIO__read_write_block_async(
                        context,
                        Ext2Volume__inode_block(&context->fs_state->volume, context->target_inode_number),
                        block_context->kernel_context
                    );
                }

                uint16_t actual_len = Ext2__dirent_record_size(name_len);
                if (!context->needs_parent_dir_write && rec_len >= actual_len + required_len)
                {
                    context->needs_parent_dir_write = true;
                    context->insert_uses_empty_slot = false;
                    context->insert_prev_offset = offset;
                    context->insert_offset = offset + actual_len;
                    context->parent_inode_number = context->current_inode_number;
                    context->parent_dir_block = context->current_inode.block[context->current_dir_block_index];
                    Kernel__memcopy(addr(block_context->buffer), addr(context->parent_dir_block_buffer), EXT2_BLOCK_SIZE);
                }
            }
            else if (!context->needs_parent_dir_write && rec_len >= required_len)
            {
                context->needs_parent_dir_write = true;
                context->insert_uses_empty_slot = true;
                context->insert_prev_offset = 0;
                context->insert_offset = offset;
                context->parent_inode_number = context->current_inode_number;
                context->parent_dir_block = context->current_inode.block[context->current_dir_block_index];
                Kernel__memcopy(addr(block_context->buffer), addr(context->parent_dir_block_buffer), EXT2_BLOCK_SIZE);
            }

            offset += rec_len;
        }

        context->current_dir_block_index++;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index < EXT2_NDIR_BLOCKS)
        {
            return Ext2FileIO__read_write_block_async(
                context,
                context->current_inode.block[context->current_dir_block_index],
                block_context->kernel_context
            );
        }

        if (!context->needs_parent_dir_write)
        {
            return Error(Anything, not_supported, IO_OPERATION_NOT_SUPPORTED);
        }

        context->creating = true;
        context->phase = EXT2_ASYNC_WRITE_READ_INODE_BITMAP;
        return Ext2FileIO__read_write_block_async(
            context,
            context->fs_state->volume.inode_bitmap_block,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_TARGET_INODE)
    {
        context->target_inode_block_number =
            Ext2Volume__inode_block(&context->fs_state->volume, context->target_inode_number);
        Kernel__memcopy(addr(block_context->buffer), addr(context->target_inode_block_buffer), EXT2_BLOCK_SIZE);
        Ext2Inode__read_from_block(
            &context->fs_state->volume,
            context->target_inode_number,
            block_context->buffer,
            &context->target_inode
        );

        if (!Ext2Inode__is_file(&context->target_inode))
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->phase = EXT2_ASYNC_WRITE_READ_INODE_BITMAP;
        return Ext2FileIO__read_write_block_async(
            context,
            context->fs_state->volume.inode_bitmap_block,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_INODE_BITMAP)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->inode_bitmap_buffer), EXT2_BLOCK_SIZE);
        context->phase = EXT2_ASYNC_WRITE_READ_BLOCK_BITMAP;
        return Ext2FileIO__read_write_block_async(
            context,
            context->fs_state->volume.block_bitmap_block,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_BLOCK_BITMAP)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->block_bitmap_buffer), EXT2_BLOCK_SIZE);
        context->phase = EXT2_ASYNC_WRITE_READ_SUPERBLOCK_COUNTS;
        return Ext2FileIO__read_write_block_async(context, 1, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_SUPERBLOCK_COUNTS)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->superblock_buffer), EXT2_BLOCK_SIZE);
        context->phase = EXT2_ASYNC_WRITE_READ_GROUP_DESC_COUNTS;
        return Ext2FileIO__read_write_block_async(context, 2, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_GROUP_DESC_COUNTS)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->group_desc_buffer), EXT2_BLOCK_SIZE);
        context->free_block_delta = 0;
        context->free_inode_delta = 0;

        if (context->creating)
        {
            ResultMatch(Ext2Code, Ext2Volume__allocate_inode_from_bitmap(
                &context->fs_state->volume,
                context->inode_bitmap_buffer,
                &context->target_inode_number
            ))
                IsError(code, message, return Error(Anything, message, IO_OPERATION_NOT_SUPPORTED))
            End
            context->free_inode_delta = -1;
            Ext2Inode__reset_file(&context->target_inode, (Ext2FileSize) context->write_size);
            context->target_inode_block_number =
                Ext2Volume__inode_block(&context->fs_state->volume, context->target_inode_number);
        }
        else
        {
            for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
            {
                if (context->target_inode.block[i] != 0)
                {
                    Ext2__bitmap_clear_bit(
                        context->block_bitmap_buffer,
                        context->target_inode.block[i] - context->fs_state->volume.first_data_block
                    );
                    context->free_block_delta++;
                    context->target_inode.block[i] = 0;
                }
            }
        }

        if (context->blocks_needed > EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, not_supported, IO_OPERATION_NOT_SUPPORTED);
        }

        for (size_t i = 0; i < context->blocks_needed; i++)
        {
            ResultMatch(Ext2Code, Ext2Volume__allocate_block_from_bitmap(
                &context->fs_state->volume,
                context->block_bitmap_buffer,
                &context->target_inode.block[i]
            ))
                IsError(code, message, return Error(Anything, message, IO_OPERATION_NOT_SUPPORTED))
            End
            context->free_block_delta--;
        }
        context->target_inode.size = (Ext2FileSize) context->write_size;

        uint32_t free_blocks = Ext2__read_u32(context->superblock_buffer, 12);
        uint32_t free_inodes = Ext2__read_u32(context->superblock_buffer, 16);
        uint16_t group_free_blocks = Ext2__read_u16(context->group_desc_buffer, 12);
        uint16_t group_free_inodes = Ext2__read_u16(context->group_desc_buffer, 14);
        Ext2__write_u32(context->superblock_buffer, 12, (uint32_t) ((int32_t) free_blocks + context->free_block_delta));
        Ext2__write_u32(context->superblock_buffer, 16, (uint32_t) ((int32_t) free_inodes + context->free_inode_delta));
        Ext2__write_u16(context->group_desc_buffer, 12, (uint16_t) ((int32_t) group_free_blocks + context->free_block_delta));
        Ext2__write_u16(context->group_desc_buffer, 14, (uint16_t) ((int32_t) group_free_inodes + context->free_inode_delta));

        if (context->creating)
        {
            context->phase = EXT2_ASYNC_WRITE_READ_TARGET_INODE_BLOCK;
            return Ext2FileIO__read_write_block_async(
                context,
                context->target_inode_block_number,
                block_context->kernel_context
            );
        }

        context->phase =
            context->blocks_needed > 0 ? EXT2_ASYNC_WRITE_WRITE_DATA_BLOCK : EXT2_ASYNC_WRITE_WRITE_TARGET_INODE;
        context->data_block_index = 0;
    }

    if (context->phase == EXT2_ASYNC_WRITE_READ_TARGET_INODE_BLOCK)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->target_inode_block_buffer), EXT2_BLOCK_SIZE);
        context->phase =
            context->blocks_needed > 0 ? EXT2_ASYNC_WRITE_WRITE_DATA_BLOCK : EXT2_ASYNC_WRITE_WRITE_TARGET_INODE;
        context->data_block_index = 0;
    }

    if (context->phase == EXT2_ASYNC_WRITE_WRITE_DATA_BLOCK)
    {
        if (context->data_block_index >= context->blocks_needed)
        {
            context->phase = EXT2_ASYNC_WRITE_WRITE_TARGET_INODE;
        }
        else
        {
            for (size_t i = 0; i < EXT2_BLOCK_SIZE; i++)
            {
                context->data_block_buffer[i] = 0;
            }

            size_t source_offset = context->data_block_index * EXT2_BLOCK_SIZE;
            size_t remaining = context->write_size - source_offset;
            size_t copy_size = remaining < EXT2_BLOCK_SIZE ? remaining : EXT2_BLOCK_SIZE;
            for (size_t i = 0; i < copy_size; i++)
            {
                context->data_block_buffer[i] = context->write_data[source_offset + i];
            }

            Ext2Block target_block = context->target_inode.block[context->data_block_index];
            context->data_block_index++;
            return Ext2FileIO__write_write_block_async(
                context,
                target_block,
                context->data_block_buffer,
                block_context->kernel_context
            );
        }
    }

    if (context->phase == EXT2_ASYNC_WRITE_WRITE_TARGET_INODE)
    {
        size_t inode_offset =
            Ext2Volume__inode_offset(&context->fs_state->volume, context->target_inode_number);
        Ext2__write_u16(context->target_inode_block_buffer, inode_offset, context->target_inode.mode);
        Ext2__write_u32(context->target_inode_block_buffer, inode_offset + 4, context->target_inode.size);
        for (size_t i = 0; i < 15; i++)
        {
            Ext2__write_u32(
                context->target_inode_block_buffer,
                inode_offset + 40 + (i * 4),
                context->target_inode.block[i]
            );
        }

        context->phase =
            context->creating ? EXT2_ASYNC_WRITE_WRITE_INODE_BITMAP : EXT2_ASYNC_WRITE_WRITE_BLOCK_BITMAP;
        return Ext2FileIO__write_write_block_async(
            context,
            context->target_inode_block_number,
            context->target_inode_block_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_WRITE_WRITE_INODE_BITMAP)
    {
        context->phase = EXT2_ASYNC_WRITE_WRITE_BLOCK_BITMAP;
        return Ext2FileIO__write_write_block_async(
            context,
            context->fs_state->volume.inode_bitmap_block,
            context->inode_bitmap_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_WRITE_WRITE_BLOCK_BITMAP)
    {
        context->phase = EXT2_ASYNC_WRITE_WRITE_SUPERBLOCK;
        return Ext2FileIO__write_write_block_async(
            context,
            context->fs_state->volume.block_bitmap_block,
            context->block_bitmap_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_WRITE_WRITE_SUPERBLOCK)
    {
        context->phase = EXT2_ASYNC_WRITE_WRITE_GROUP_DESC;
        return Ext2FileIO__write_write_block_async(context, 1, context->superblock_buffer, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_WRITE_WRITE_GROUP_DESC)
    {
        if (context->creating)
        {
            if (context->insert_uses_empty_slot)
            {
                uint16_t rec_len = Ext2__read_u16(context->parent_dir_block_buffer, context->insert_offset + 4);
                Ext2__write_u32(context->parent_dir_block_buffer, context->insert_offset, context->target_inode_number);
                Ext2__write_u16(context->parent_dir_block_buffer, context->insert_offset + 4, rec_len);
                context->parent_dir_block_buffer[context->insert_offset + 6] = (Data) context->leaf_name.len;
                context->parent_dir_block_buffer[context->insert_offset + 7] = (Data) EXT2_DIRENT_TYPE_FILE;
                for (size_t i = 0; i < context->leaf_name.len; i++)
                {
                    context->parent_dir_block_buffer[context->insert_offset + 8 + i] = (Data) context->leaf_name.data[i];
                }
            }
            else
            {
                uint16_t old_len = Ext2__read_u16(context->parent_dir_block_buffer, context->insert_prev_offset + 4);
                uint8_t prev_name_len = context->parent_dir_block_buffer[context->insert_prev_offset + 6];
                uint16_t actual_len = Ext2__dirent_record_size(prev_name_len);
                uint16_t new_len = old_len - actual_len;
                Ext2__write_u16(context->parent_dir_block_buffer, context->insert_prev_offset + 4, actual_len);
                Ext2__write_u32(context->parent_dir_block_buffer, context->insert_offset, context->target_inode_number);
                Ext2__write_u16(context->parent_dir_block_buffer, context->insert_offset + 4, new_len);
                context->parent_dir_block_buffer[context->insert_offset + 6] = (Data) context->leaf_name.len;
                context->parent_dir_block_buffer[context->insert_offset + 7] = (Data) EXT2_DIRENT_TYPE_FILE;
                for (size_t i = 0; i < context->leaf_name.len; i++)
                {
                    context->parent_dir_block_buffer[context->insert_offset + 8 + i] = (Data) context->leaf_name.data[i];
                }
            }
            context->phase = EXT2_ASYNC_WRITE_WRITE_PARENT_DIR_BLOCK;
            return Ext2FileIO__write_write_block_async(
                context,
                context->parent_dir_block,
                context->parent_dir_block_buffer,
                block_context->kernel_context
            );
        }

        context->file_handle->inode_number = context->target_inode_number;
        context->file_handle->mode = context->target_inode.mode;
        context->file_handle->size = context->target_inode.size;
        for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
        {
            context->file_handle->block[i] = context->target_inode.block[i];
        }
        context->file_handle->offset = context->write_size;
        return Success(Anything, context->write_size);
    }

    if (context->phase == EXT2_ASYNC_WRITE_WRITE_PARENT_DIR_BLOCK)
    {
        context->file_handle->inode_number = context->target_inode_number;
        context->file_handle->mode = context->target_inode.mode;
        context->file_handle->size = context->target_inode.size;
        for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
        {
            context->file_handle->block[i] = context->target_inode.block[i];
        }
        context->file_handle->offset = context->write_size;
        return Success(Anything, context->write_size);
    }

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Ext2FileIO__complete_async_mkdir(BlockIOContextPtr block_context)
{
    Ext2AsyncMkdirContextPtr context = ptr(block_context->owner_context, Ext2AsyncMkdirContext);

    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    if (context->phase == EXT2_ASYNC_MKDIR_READ_SUPERBLOCK)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_superblock(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_MKDIR_READ_GROUP_DESC;
        return Ext2FileIO__read_mkdir_block_async(context, 2, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_MKDIR_READ_GROUP_DESC)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_group_descriptor(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_MKDIR_READ_PARENT_INODE;
        return Ext2FileIO__read_mkdir_block_async(
            context,
            Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_READ_PARENT_INODE)
    {
        Ext2Inode__read_from_block(
            &context->fs_state->volume,
            context->current_inode_number,
            block_context->buffer,
            &context->current_inode
        );

        if (!Ext2Inode__is_dir(&context->current_inode))
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->current_dir_block_index = 0;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->phase = EXT2_ASYNC_MKDIR_READ_PARENT_DIR_BLOCK;
        return Ext2FileIO__read_mkdir_block_async(
            context,
            context->current_inode.block[context->current_dir_block_index],
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_READ_PARENT_DIR_BLOCK)
    {
        size_t offset = 0;

        if (context->segment_index < context->parent_segment_count)
        {
            BString target_name = context->segments.data[context->segment_index];
            while (offset + 8 <= EXT2_BLOCK_SIZE)
            {
                Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
                uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
                uint8_t name_len = block_context->buffer[offset + 6];

                if (rec_len == 0)
                {
                    break;
                }

                if (inode != 0 && name_len > 0)
                {
                    BString entry_name = EMPTY_BSTRING;
                    for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                    {
                        entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                    }
                    entry_name.len = name_len;
                    entry_name.data[entry_name.len] = '\0';

                    if (BString__equals(&entry_name, &target_name))
                    {
                        context->current_inode_number = inode;
                        context->segment_index++;
                        context->phase = EXT2_ASYNC_MKDIR_READ_PARENT_INODE;
                        return Ext2FileIO__read_mkdir_block_async(
                            context,
                            Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
                            block_context->kernel_context
                        );
                    }
                }

                offset += rec_len;
            }

            context->current_dir_block_index++;
            while (
                context->current_dir_block_index < EXT2_NDIR_BLOCKS
                && context->current_inode.block[context->current_dir_block_index] == 0
            )
            {
                context->current_dir_block_index++;
            }

            if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
            {
                return Error(Anything, invalid_path, IO_INVALID_PATH);
            }

            return Ext2FileIO__read_mkdir_block_async(
                context,
                context->current_inode.block[context->current_dir_block_index],
                block_context->kernel_context
            );
        }

        uint16_t required_len = Ext2__dirent_record_size(context->leaf_name.len);
        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
            uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
            uint8_t name_len = block_context->buffer[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (inode != 0 && name_len > 0)
            {
                BString entry_name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                }
                entry_name.len = name_len;
                entry_name.data[entry_name.len] = '\0';

                if (BString__equals(&entry_name, &context->leaf_name))
                {
                    return Error(Anything, invalid_path, IO_INVALID_PATH);
                }

                uint16_t actual_len = Ext2__dirent_record_size(name_len);
                if (!context->needs_parent_dir_write && rec_len >= actual_len + required_len)
                {
                    context->needs_parent_dir_write = true;
                    context->insert_uses_empty_slot = false;
                    context->insert_prev_offset = offset;
                    context->insert_offset = offset + actual_len;
                    context->parent_inode_number = context->current_inode_number;
                    context->parent_dir_block = context->current_inode.block[context->current_dir_block_index];
                    Kernel__memcopy(addr(block_context->buffer), addr(context->parent_dir_block_buffer), EXT2_BLOCK_SIZE);
                }
            }
            else if (!context->needs_parent_dir_write && rec_len >= required_len)
            {
                context->needs_parent_dir_write = true;
                context->insert_uses_empty_slot = true;
                context->insert_prev_offset = 0;
                context->insert_offset = offset;
                context->parent_inode_number = context->current_inode_number;
                context->parent_dir_block = context->current_inode.block[context->current_dir_block_index];
                Kernel__memcopy(addr(block_context->buffer), addr(context->parent_dir_block_buffer), EXT2_BLOCK_SIZE);
            }

            offset += rec_len;
        }

        context->current_dir_block_index++;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index < EXT2_NDIR_BLOCKS)
        {
            return Ext2FileIO__read_mkdir_block_async(
                context,
                context->current_inode.block[context->current_dir_block_index],
                block_context->kernel_context
            );
        }

        if (!context->needs_parent_dir_write)
        {
            return Error(Anything, not_supported, IO_OPERATION_NOT_SUPPORTED);
        }

        context->phase = EXT2_ASYNC_MKDIR_READ_INODE_BITMAP;
        return Ext2FileIO__read_mkdir_block_async(
            context,
            context->fs_state->volume.inode_bitmap_block,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_READ_INODE_BITMAP)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->inode_bitmap_buffer), EXT2_BLOCK_SIZE);
        context->phase = EXT2_ASYNC_MKDIR_READ_BLOCK_BITMAP;
        return Ext2FileIO__read_mkdir_block_async(
            context,
            context->fs_state->volume.block_bitmap_block,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_READ_BLOCK_BITMAP)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->block_bitmap_buffer), EXT2_BLOCK_SIZE);
        context->phase = EXT2_ASYNC_MKDIR_READ_SUPERBLOCK_COUNTS;
        return Ext2FileIO__read_mkdir_block_async(context, 1, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_MKDIR_READ_SUPERBLOCK_COUNTS)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->superblock_buffer), EXT2_BLOCK_SIZE);
        context->phase = EXT2_ASYNC_MKDIR_READ_GROUP_DESC_COUNTS;
        return Ext2FileIO__read_mkdir_block_async(context, 2, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_MKDIR_READ_GROUP_DESC_COUNTS)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->group_desc_buffer), EXT2_BLOCK_SIZE);

        ResultMatch(Ext2Code, Ext2Volume__allocate_inode_from_bitmap(
            &context->fs_state->volume,
            context->inode_bitmap_buffer,
            &context->target_inode_number
        ))
            IsError(code, message, return Error(Anything, message, IO_OPERATION_NOT_SUPPORTED))
        End
        ResultMatch(Ext2Code, Ext2Volume__allocate_block_from_bitmap(
            &context->fs_state->volume,
            context->block_bitmap_buffer,
            &context->target_inode.block[0]
        ))
            IsError(code, message, return Error(Anything, message, IO_OPERATION_NOT_SUPPORTED))
        End

        Ext2Inode__reset_dir(&context->target_inode, context->target_inode.block[0]);
        context->target_inode_block_number =
            Ext2Volume__inode_block(&context->fs_state->volume, context->target_inode_number);

        uint32_t free_blocks = Ext2__read_u32(context->superblock_buffer, 12);
        uint32_t free_inodes = Ext2__read_u32(context->superblock_buffer, 16);
        uint16_t group_free_blocks = Ext2__read_u16(context->group_desc_buffer, 12);
        uint16_t group_free_inodes = Ext2__read_u16(context->group_desc_buffer, 14);
        uint16_t used_dirs = Ext2__read_u16(context->group_desc_buffer, 16);
        Ext2__write_u32(context->superblock_buffer, 12, free_blocks - 1);
        Ext2__write_u32(context->superblock_buffer, 16, free_inodes - 1);
        Ext2__write_u16(context->group_desc_buffer, 12, group_free_blocks - 1);
        Ext2__write_u16(context->group_desc_buffer, 14, group_free_inodes - 1);
        Ext2__write_u16(context->group_desc_buffer, 16, used_dirs + 1);

        context->phase = EXT2_ASYNC_MKDIR_READ_TARGET_INODE_BLOCK;
        return Ext2FileIO__read_mkdir_block_async(
            context,
            context->target_inode_block_number,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_READ_TARGET_INODE_BLOCK)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->target_inode_block_buffer), EXT2_BLOCK_SIZE);
        Ext2__initialise_dir_block(
            context->target_dir_block_buffer,
            context->target_inode_number,
            context->parent_inode_number
        );
        context->phase = EXT2_ASYNC_MKDIR_WRITE_TARGET_DIR_BLOCK;
        return Ext2FileIO__write_mkdir_block_async(
            context,
            context->target_inode.block[0],
            context->target_dir_block_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_WRITE_TARGET_DIR_BLOCK)
    {
        size_t inode_offset =
            Ext2Volume__inode_offset(&context->fs_state->volume, context->target_inode_number);
        Ext2__write_u16(context->target_inode_block_buffer, inode_offset, context->target_inode.mode);
        Ext2__write_u32(context->target_inode_block_buffer, inode_offset + 4, context->target_inode.size);
        for (size_t i = 0; i < 15; i++)
        {
            Ext2__write_u32(
                context->target_inode_block_buffer,
                inode_offset + 40 + (i * 4),
                context->target_inode.block[i]
            );
        }

        context->phase = EXT2_ASYNC_MKDIR_WRITE_TARGET_INODE_BLOCK;
        return Ext2FileIO__write_mkdir_block_async(
            context,
            context->target_inode_block_number,
            context->target_inode_block_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_WRITE_TARGET_INODE_BLOCK)
    {
        context->phase = EXT2_ASYNC_MKDIR_WRITE_INODE_BITMAP;
        return Ext2FileIO__write_mkdir_block_async(
            context,
            context->fs_state->volume.inode_bitmap_block,
            context->inode_bitmap_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_WRITE_INODE_BITMAP)
    {
        context->phase = EXT2_ASYNC_MKDIR_WRITE_BLOCK_BITMAP;
        return Ext2FileIO__write_mkdir_block_async(
            context,
            context->fs_state->volume.block_bitmap_block,
            context->block_bitmap_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_WRITE_BLOCK_BITMAP)
    {
        context->phase = EXT2_ASYNC_MKDIR_WRITE_SUPERBLOCK;
        return Ext2FileIO__write_mkdir_block_async(context, 1, context->superblock_buffer, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_MKDIR_WRITE_SUPERBLOCK)
    {
        context->phase = EXT2_ASYNC_MKDIR_WRITE_GROUP_DESC;
        return Ext2FileIO__write_mkdir_block_async(context, 2, context->group_desc_buffer, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_MKDIR_WRITE_GROUP_DESC)
    {
        if (context->insert_uses_empty_slot)
        {
            uint16_t rec_len = Ext2__read_u16(context->parent_dir_block_buffer, context->insert_offset + 4);
            Ext2__write_u32(context->parent_dir_block_buffer, context->insert_offset, context->target_inode_number);
            Ext2__write_u16(context->parent_dir_block_buffer, context->insert_offset + 4, rec_len);
            context->parent_dir_block_buffer[context->insert_offset + 6] = (Data) context->leaf_name.len;
            context->parent_dir_block_buffer[context->insert_offset + 7] = (Data) EXT2_DIRENT_TYPE_DIR;
            for (size_t i = 0; i < context->leaf_name.len; i++)
            {
                context->parent_dir_block_buffer[context->insert_offset + 8 + i] = (Data) context->leaf_name.data[i];
            }
        }
        else
        {
            uint16_t old_len = Ext2__read_u16(context->parent_dir_block_buffer, context->insert_prev_offset + 4);
            uint8_t prev_name_len = context->parent_dir_block_buffer[context->insert_prev_offset + 6];
            uint16_t actual_len = Ext2__dirent_record_size(prev_name_len);
            uint16_t new_len = old_len - actual_len;
            Ext2__write_u16(context->parent_dir_block_buffer, context->insert_prev_offset + 4, actual_len);
            Ext2__write_u32(context->parent_dir_block_buffer, context->insert_offset, context->target_inode_number);
            Ext2__write_u16(context->parent_dir_block_buffer, context->insert_offset + 4, new_len);
            context->parent_dir_block_buffer[context->insert_offset + 6] = (Data) context->leaf_name.len;
            context->parent_dir_block_buffer[context->insert_offset + 7] = (Data) EXT2_DIRENT_TYPE_DIR;
            for (size_t i = 0; i < context->leaf_name.len; i++)
            {
                context->parent_dir_block_buffer[context->insert_offset + 8 + i] = (Data) context->leaf_name.data[i];
            }
        }

        context->phase = EXT2_ASYNC_MKDIR_WRITE_PARENT_DIR_BLOCK;
        return Ext2FileIO__write_mkdir_block_async(
            context,
            context->parent_dir_block,
            context->parent_dir_block_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_MKDIR_WRITE_PARENT_DIR_BLOCK)
    {
        return Success(Anything, IO_OK);
    }

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Ext2FileIO__complete_async_remove(BlockIOContextPtr block_context)
{
    Ext2AsyncRemoveContextPtr context = ptr(block_context->owner_context, Ext2AsyncRemoveContext);

    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    if (context->phase == EXT2_ASYNC_REMOVE_READ_SUPERBLOCK)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_superblock(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_REMOVE_READ_GROUP_DESC;
        return Ext2FileIO__read_remove_block_async(context, 2, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_REMOVE_READ_GROUP_DESC)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_group_descriptor(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_REMOVE_READ_PARENT_INODE;
        return Ext2FileIO__read_remove_block_async(
            context,
            Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_READ_PARENT_INODE)
    {
        Ext2Inode__read_from_block(
            &context->fs_state->volume,
            context->current_inode_number,
            block_context->buffer,
            &context->current_inode
        );

        if (!Ext2Inode__is_dir(&context->current_inode))
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->current_dir_block_index = 0;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, not_found, IO_INVALID_PATH);
        }

        context->phase = EXT2_ASYNC_REMOVE_READ_PARENT_DIR_BLOCK;
        return Ext2FileIO__read_remove_block_async(
            context,
            context->current_inode.block[context->current_dir_block_index],
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_READ_PARENT_DIR_BLOCK)
    {
        size_t offset = 0;

        if (context->segment_index < context->parent_segment_count)
        {
            BString target_name = context->segments.data[context->segment_index];
            while (offset + 8 <= EXT2_BLOCK_SIZE)
            {
                Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
                uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
                uint8_t name_len = block_context->buffer[offset + 6];

                if (rec_len == 0)
                {
                    break;
                }

                if (inode != 0 && name_len > 0)
                {
                    BString entry_name = EMPTY_BSTRING;
                    for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                    {
                        entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                    }
                    entry_name.len = name_len;
                    entry_name.data[entry_name.len] = '\0';

                    if (BString__equals(&entry_name, &target_name))
                    {
                        context->current_inode_number = inode;
                        context->segment_index++;
                        context->phase = EXT2_ASYNC_REMOVE_READ_PARENT_INODE;
                        return Ext2FileIO__read_remove_block_async(
                            context,
                            Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
                            block_context->kernel_context
                        );
                    }
                }

                offset += rec_len;
            }

            context->current_dir_block_index++;
            while (
                context->current_dir_block_index < EXT2_NDIR_BLOCKS
                && context->current_inode.block[context->current_dir_block_index] == 0
            )
            {
                context->current_dir_block_index++;
            }

            if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
            {
                return Error(Anything, not_found, IO_INVALID_PATH);
            }

            return Ext2FileIO__read_remove_block_async(
                context,
                context->current_inode.block[context->current_dir_block_index],
                block_context->kernel_context
            );
        }

        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
            uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
            uint8_t name_len = block_context->buffer[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (inode != 0 && name_len > 0)
            {
                BString entry_name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                }
                entry_name.len = name_len;
                entry_name.data[entry_name.len] = '\0';

                if (BString__equals(&entry_name, &context->leaf_name))
                {
                    context->parent_inode_number = context->current_inode_number;
                    context->parent_dir_block = context->current_inode.block[context->current_dir_block_index];
                    context->parent_dirent_offset = offset;
                    Kernel__memcopy(addr(block_context->buffer), addr(context->parent_dir_block_buffer), EXT2_BLOCK_SIZE);

                    size_t previous_offset = 0;
                    bool has_previous = false;
                    size_t scan_offset = 0;
                    while (scan_offset < offset)
                    {
                        uint16_t scan_rec_len = Ext2__read_u16(block_context->buffer, scan_offset + 4);
                        if (scan_rec_len == 0 || scan_offset + scan_rec_len > offset)
                        {
                            break;
                        }
                        previous_offset = scan_offset;
                        has_previous = true;
                        scan_offset += scan_rec_len;
                    }
                    context->parent_previous_dirent_offset = previous_offset;
                    context->parent_has_previous = has_previous;
                    context->target_inode_number = inode;
                    context->phase = EXT2_ASYNC_REMOVE_READ_TARGET_INODE;
                    return Ext2FileIO__read_remove_block_async(
                        context,
                        Ext2Volume__inode_block(&context->fs_state->volume, context->target_inode_number),
                        block_context->kernel_context
                    );
                }
            }

            offset += rec_len;
        }

        context->current_dir_block_index++;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, not_found, IO_INVALID_PATH);
        }

        return Ext2FileIO__read_remove_block_async(
            context,
            context->current_inode.block[context->current_dir_block_index],
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_READ_TARGET_INODE)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->target_inode_block_buffer), EXT2_BLOCK_SIZE);
        context->target_inode_block_number =
            Ext2Volume__inode_block(&context->fs_state->volume, context->target_inode_number);

        Ext2Inode__read_from_block(
            &context->fs_state->volume,
            context->target_inode_number,
            block_context->buffer,
            &context->target_inode
        );

        if (
            context->target_inode.block[EXT2_NDIR_BLOCKS] != 0
            || context->target_inode.block[EXT2_NDIR_BLOCKS + 1] != 0
            || context->target_inode.block[EXT2_NDIR_BLOCKS + 2] != 0
        )
        {
            return Error(Anything, not_supported, IO_OPERATION_NOT_SUPPORTED);
        }

        if (Ext2Inode__is_dir(&context->target_inode))
        {
            context->verify_dir_block_index = 0;
            context->phase = EXT2_ASYNC_REMOVE_VERIFY_TARGET_DIR_BLOCK;
            while (
                context->verify_dir_block_index < EXT2_NDIR_BLOCKS
                && context->target_inode.block[context->verify_dir_block_index] == 0
            )
            {
                context->verify_dir_block_index++;
            }

            if (context->verify_dir_block_index >= EXT2_NDIR_BLOCKS)
            {
                context->phase = EXT2_ASYNC_REMOVE_READ_INODE_BITMAP;
                return Ext2FileIO__read_remove_block_async(
                    context,
                    context->fs_state->volume.inode_bitmap_block,
                    block_context->kernel_context
                );
            }

            return Ext2FileIO__read_remove_block_async(
                context,
                context->target_inode.block[context->verify_dir_block_index],
                block_context->kernel_context
            );
        }

        if (!Ext2Inode__is_file(&context->target_inode))
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        if (context->target_inode.links_count > 1)
        {
            context->decrement_only = true;
            context->target_inode.links_count--;
            size_t inode_offset =
                Ext2Volume__inode_offset(&context->fs_state->volume, context->target_inode_number);
            Ext2__write_u16(context->target_inode_block_buffer, inode_offset, context->target_inode.mode);
            Ext2__write_u32(context->target_inode_block_buffer, inode_offset + 4, context->target_inode.size);
            Ext2__write_u16(context->target_inode_block_buffer, inode_offset + 26, context->target_inode.links_count);
            for (size_t i = 0; i < 15; i++)
            {
                Ext2__write_u32(
                    context->target_inode_block_buffer,
                    inode_offset + 40 + (i * 4),
                    context->target_inode.block[i]
                );
            }

            if (context->parent_has_previous)
            {
                uint16_t previous_len = Ext2__read_u16(
                    context->parent_dir_block_buffer,
                    context->parent_previous_dirent_offset + 4
                );
                uint16_t target_len = Ext2__read_u16(
                    context->parent_dir_block_buffer,
                    context->parent_dirent_offset + 4
                );
                Ext2__write_u16(
                    context->parent_dir_block_buffer,
                    context->parent_previous_dirent_offset + 4,
                    previous_len + target_len
                );
            }
            else
            {
                Ext2__write_u32(context->parent_dir_block_buffer, context->parent_dirent_offset, 0);
            }

            context->phase = EXT2_ASYNC_REMOVE_WRITE_TARGET_INODE;
            return Ext2FileIO__write_remove_block_async(
                context,
                context->target_inode_block_number,
                context->target_inode_block_buffer,
                block_context->kernel_context
            );
        }

        context->phase = EXT2_ASYNC_REMOVE_READ_INODE_BITMAP;
        return Ext2FileIO__read_remove_block_async(
            context,
            context->fs_state->volume.inode_bitmap_block,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_VERIFY_TARGET_DIR_BLOCK)
    {
        size_t offset = 0;
        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
            uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
            uint8_t name_len = block_context->buffer[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (inode != 0 && name_len > 0)
            {
                BString entry_name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    entry_name.data[i] = (char) block_context->buffer[offset + 8 + i];
                }
                entry_name.len = name_len;
                entry_name.data[entry_name.len] = '\0';

                if (!BString__equals(&entry_name, &BSTRING_NEW(".")) && !BString__equals(&entry_name, &BSTRING_NEW("..")))
                {
                    if ((context->flags & IO_REMOVE_RECURSIVE) == 0)
                    {
                        return Error(Anything, not_supported, IO_OPERATION_NOT_SUPPORTED);
                    }

                    BString child_path = Ext2__child_path(context->path, entry_name);
                    ResultMatch(CollectionCode, BStringArray__push(&context->resume_paths, context->path))
                        IsError(code, message, return Error(Anything, not_supported, IO_OPERATION_NOT_SUPPORTED))
                    End

                    Ext2AsyncRemoveContext__reset_for_path(
                        context,
                        child_path,
                        EXT2_ASYNC_REMOVE_READ_PARENT_INODE
                    );
                    return Ext2FileIO__read_remove_block_async(
                        context,
                        Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
                        block_context->kernel_context
                    );
                }
            }

            offset += rec_len;
        }

        context->verify_dir_block_index++;
        while (
            context->verify_dir_block_index < EXT2_NDIR_BLOCKS
            && context->target_inode.block[context->verify_dir_block_index] == 0
        )
        {
            context->verify_dir_block_index++;
        }

        if (context->verify_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            context->phase = EXT2_ASYNC_REMOVE_READ_INODE_BITMAP;
            return Ext2FileIO__read_remove_block_async(
                context,
                context->fs_state->volume.inode_bitmap_block,
                block_context->kernel_context
            );
        }

        return Ext2FileIO__read_remove_block_async(
            context,
            context->target_inode.block[context->verify_dir_block_index],
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_READ_INODE_BITMAP)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->inode_bitmap_buffer), EXT2_BLOCK_SIZE);
        context->phase = EXT2_ASYNC_REMOVE_READ_BLOCK_BITMAP;
        return Ext2FileIO__read_remove_block_async(
            context,
            context->fs_state->volume.block_bitmap_block,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_READ_BLOCK_BITMAP)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->block_bitmap_buffer), EXT2_BLOCK_SIZE);
        context->phase = EXT2_ASYNC_REMOVE_READ_SUPERBLOCK_COUNTS;
        return Ext2FileIO__read_remove_block_async(context, 1, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_REMOVE_READ_SUPERBLOCK_COUNTS)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->superblock_buffer), EXT2_BLOCK_SIZE);
        context->phase = EXT2_ASYNC_REMOVE_READ_GROUP_DESC_COUNTS;
        return Ext2FileIO__read_remove_block_async(context, 2, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_REMOVE_READ_GROUP_DESC_COUNTS)
    {
        Kernel__memcopy(addr(block_context->buffer), addr(context->group_desc_buffer), EXT2_BLOCK_SIZE);

        context->freed_block_count = 0;
        for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
        {
            if (context->target_inode.block[i] != 0)
            {
                Ext2__bitmap_clear_bit(
                    context->block_bitmap_buffer,
                    context->target_inode.block[i] - context->fs_state->volume.first_data_block
                );
                context->freed_block_count++;
            }
        }

        Ext2__bitmap_clear_bit(context->inode_bitmap_buffer, context->target_inode_number - 1);

        size_t inode_offset =
            Ext2Volume__inode_offset(&context->fs_state->volume, context->target_inode_number);
        for (size_t i = 0; i < context->fs_state->volume.inode_size; i++)
        {
            context->target_inode_block_buffer[inode_offset + i] = 0;
        }

        if (context->parent_has_previous)
        {
            uint16_t previous_len = Ext2__read_u16(
                context->parent_dir_block_buffer,
                context->parent_previous_dirent_offset + 4
            );
            uint16_t target_len = Ext2__read_u16(context->parent_dir_block_buffer, context->parent_dirent_offset + 4);
            Ext2__write_u16(
                context->parent_dir_block_buffer,
                context->parent_previous_dirent_offset + 4,
                previous_len + target_len
            );
        }
        else
        {
            Ext2__write_u32(context->parent_dir_block_buffer, context->parent_dirent_offset, 0);
        }

        uint32_t free_blocks = Ext2__read_u32(context->superblock_buffer, 12);
        uint32_t free_inodes = Ext2__read_u32(context->superblock_buffer, 16);
        uint16_t group_free_blocks = Ext2__read_u16(context->group_desc_buffer, 12);
        uint16_t group_free_inodes = Ext2__read_u16(context->group_desc_buffer, 14);
        uint16_t used_dirs = Ext2__read_u16(context->group_desc_buffer, 16);

        Ext2__write_u32(context->superblock_buffer, 12, free_blocks + context->freed_block_count);
        Ext2__write_u32(context->superblock_buffer, 16, free_inodes + 1);
        Ext2__write_u16(context->group_desc_buffer, 12, group_free_blocks + context->freed_block_count);
        Ext2__write_u16(context->group_desc_buffer, 14, group_free_inodes + 1);
        if (Ext2Inode__is_dir(&context->target_inode))
        {
            Ext2__write_u16(context->group_desc_buffer, 16, used_dirs - 1);
        }

        context->phase = EXT2_ASYNC_REMOVE_WRITE_TARGET_INODE;
        return Ext2FileIO__write_remove_block_async(
            context,
            context->target_inode_block_number,
            context->target_inode_block_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_WRITE_TARGET_INODE)
    {
        if (context->decrement_only)
        {
            context->phase = EXT2_ASYNC_REMOVE_WRITE_PARENT_DIR_BLOCK;
            return Ext2FileIO__write_remove_block_async(
                context,
                context->parent_dir_block,
                context->parent_dir_block_buffer,
                block_context->kernel_context
            );
        }

        context->phase = EXT2_ASYNC_REMOVE_WRITE_INODE_BITMAP;
        return Ext2FileIO__write_remove_block_async(
            context,
            context->fs_state->volume.inode_bitmap_block,
            context->inode_bitmap_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_WRITE_INODE_BITMAP)
    {
        context->phase = EXT2_ASYNC_REMOVE_WRITE_BLOCK_BITMAP;
        return Ext2FileIO__write_remove_block_async(
            context,
            context->fs_state->volume.block_bitmap_block,
            context->block_bitmap_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_WRITE_BLOCK_BITMAP)
    {
        context->phase = EXT2_ASYNC_REMOVE_WRITE_SUPERBLOCK;
        return Ext2FileIO__write_remove_block_async(context, 1, context->superblock_buffer, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_REMOVE_WRITE_SUPERBLOCK)
    {
        context->phase = EXT2_ASYNC_REMOVE_WRITE_GROUP_DESC;
        return Ext2FileIO__write_remove_block_async(context, 2, context->group_desc_buffer, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_REMOVE_WRITE_GROUP_DESC)
    {
        context->phase = EXT2_ASYNC_REMOVE_WRITE_PARENT_DIR_BLOCK;
        return Ext2FileIO__write_remove_block_async(
            context,
            context->parent_dir_block,
            context->parent_dir_block_buffer,
            block_context->kernel_context
        );
    }

    if (context->phase == EXT2_ASYNC_REMOVE_WRITE_PARENT_DIR_BLOCK)
    {
        Match(BStringArray__optional, BStringArray__pop(&context->resume_paths))
            IsSome(resume_path,
            {
                Ext2AsyncRemoveContext__reset_for_path(
                    context,
                    resume_path,
                    EXT2_ASYNC_REMOVE_READ_PARENT_INODE
                );
                return Ext2FileIO__read_remove_block_async(
                    context,
                    Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number),
                    block_context->kernel_context
                );
            })
        End
        return Success(Anything, IO_OK);
    }

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(ByteCount) Ext2FileIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    Ext2FilesystemIOStatePtr fs_state = (Ext2FilesystemIOStatePtr) self;

    if (Ext2FilesystemIOState__uses_userspace_device(fs_state))
    {
        ResultMatch(Ext2FileHandlePtr, Ext2FilesystemIOState__get_handle(fs_state, handle))
            IsSuccess(target,
            {
                if ((target->flags & IO_OPEN_WRITE) == 0 || target->offset != 0)
                {
                    return Error(ByteCount, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                return Ext2FileIO__start_async_write(fs_state, target, data, size);
            })
            IsError(code, message, return Error(ByteCount, message, code))
        End
    }

    ResultMatch(Ext2FileHandlePtr, Ext2FilesystemIOState__get_handle(fs_state, handle))
        IsSuccess(target,
        {
            if ((target->flags & IO_OPEN_WRITE) == 0 || target->offset != 0)
            {
                return Error(ByteCount, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
            }

            if (!fs_state->volume.mounted)
            {
                ResultMatch(Ext2Code, Ext2Volume__mount(&fs_state->volume))
                    IsError(code, message, return Error(ByteCount, message, IO_INVALID_PATH))
                End
            }

            Ext2InodeNumber inode_number = 0;
            Ext2Inode inode = { 0 };
            ResultMatch(Ext2Code, Ext2Volume__write_file_at_path(
                &fs_state->volume,
                target->path,
                data,
                size,
                &inode_number,
                &inode
            ))
                IsSuccess(code,
                {
                    target->inode_number = inode_number;
                    target->mode = inode.mode;
                    target->size = inode.size;
                    for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
                    {
                        target->block[i] = inode.block[i];
                    }
                    target->offset = size;
                    return Success(ByteCount, size);
                })
                IsError(code, message, return Error(ByteCount, message, IO_INVALID_PATH))
            End
        })
        IsError(code, message, return Error(ByteCount, message, code))
    End

    return Error(ByteCount, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ParkStatus) Ext2FileIO__park(IOStatePtr self, IOContextPtr parked_context)
{
    return Error(ParkStatus, not_supported, IO_OPERATION_NOT_IMPLEMENTED);
}

static Result(ParkStatus) Ext2FileIO__unpark(IOStatePtr self)
{
    return Error(ParkStatus, not_supported, IO_OPERATION_NOT_IMPLEMENTED);
}

static Result(Anything) Ext2FileIO__read_root_dir_async(
    Ext2AsyncListDirContextPtr context,
    KernelContext kernel_context
)
{
    return Ext2FileIO__read_block_async(
        context,
        context->block_to_read,
        kernel_context,
        Ext2FileIO__complete_async_list_dir
    );
}

static Result(Anything) Ext2FileIO__read_block_async(
    Ext2AsyncListDirContextPtr context,
    Ext2Block block,
    KernelContext kernel_context,
    BlockIOContinuation continuation
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = kernel_context;
            block_context->owner_context = (Anything) context;

            bool install_context =
                block_context->kernel_context.pid.present
                || block_context->kernel_context.scheduler.present
                || block_context->kernel_context.result_mapper.present
                || block_context->kernel_context.next_context.present;

            if (install_context)
            {
                BlockDevice__set_current_context(&block_context->kernel_context);
            }
            Result(Anything) read_result = BlockDevice__read_async(
                device,
                (BlockNumber) block * EXT2_BLOCK_SECTORS,
                EXT2_BLOCK_SECTORS,
                block_context,
                continuation
            );
            if (install_context)
            {
                BlockDevice__clear_current_context();
            }
            return read_result;
        })
        IsNone(return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT))
    End

    return Error(Anything, invalid_argument, EXT2_INVALID_ARGUMENT);
}

static Result(Anything) Ext2FileIO__complete_async_list_dir(BlockIOContextPtr block_context)
{
    Ext2AsyncListDirContextPtr context = ptr(block_context->owner_context, Ext2AsyncListDirContext);

    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    if (context->phase == EXT2_ASYNC_LIST_READ_SUPERBLOCK)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_superblock(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_LIST_READ_GROUP_DESC;
        context->block_to_read = 2;
        return Ext2FileIO__read_root_dir_async(context, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_LIST_READ_GROUP_DESC)
    {
        ResultMatch(Ext2Code, Ext2Volume__parse_group_descriptor(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, IO_INVALID_PATH))
        End
        context->phase = EXT2_ASYNC_LIST_READ_INODE;
        context->block_to_read = Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number);
        return Ext2FileIO__read_root_dir_async(context, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_LIST_READ_INODE)
    {
        Ext2Inode__read_from_block(
            &context->fs_state->volume,
            context->current_inode_number,
            block_context->buffer,
            &context->current_inode
        );

        if (!Ext2Inode__is_dir(&context->current_inode))
        {
            return Error(Anything, invalid_path, IO_INVALID_PATH);
        }

        context->current_dir_block_index = 0;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Success(Anything, 0);
        }

        if (context->segment_index >= context->segments.len)
        {
            context->count = 0;
            context->phase = EXT2_ASYNC_LIST_READ_DIR_BLOCK_COLLECT;
        }
        else
        {
            context->phase = EXT2_ASYNC_LIST_READ_DIR_BLOCK_LOOKUP;
        }

        context->block_to_read = context->current_inode.block[context->current_dir_block_index];
        return Ext2FileIO__read_root_dir_async(context, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_LIST_READ_DIR_BLOCK_LOOKUP)
    {
        size_t offset = 0;
        while (offset + 8 <= EXT2_BLOCK_SIZE)
        {
            Ext2InodeNumber inode = Ext2__read_u32(block_context->buffer, offset);
            uint16_t rec_len = Ext2__read_u16(block_context->buffer, offset + 4);
            uint8_t name_len = block_context->buffer[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (inode != 0 && name_len > 0)
            {
                BString name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    name.data[i] = (char) block_context->buffer[offset + 8 + i];
                }
                name.len = name_len;
                name.data[name.len] = '\0';

                if (BString__equals(&name, &context->segments.data[context->segment_index]))
                {
                    context->current_inode_number = inode;
                    context->segment_index++;
                    context->phase = EXT2_ASYNC_LIST_READ_INODE;
                    context->block_to_read =
                        Ext2Volume__inode_block(&context->fs_state->volume, context->current_inode_number);
                    return Ext2FileIO__read_root_dir_async(context, block_context->kernel_context);
                }
            }

            offset += rec_len;
        }

        context->current_dir_block_index++;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS)
        {
            return Error(Anything, not_found, IO_INVALID_PATH);
        }

        context->block_to_read = context->current_inode.block[context->current_dir_block_index];
        return Ext2FileIO__read_root_dir_async(context, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_LIST_READ_DIR_BLOCK_COLLECT)
    {
        if (context->current_dir_block_offset == 0)
        {
            for (size_t i = 0; i < EXT2_BLOCK_SIZE; i++)
            {
                context->current_dir_block_buffer[i] = block_context->buffer[i];
            }
        }

        size_t offset = context->current_dir_block_offset;
        while (offset + 8 <= EXT2_BLOCK_SIZE && context->count < (DirectoryEntryCount) context->max_entries)
        {
            Ext2InodeNumber inode = Ext2__read_u32(context->current_dir_block_buffer, offset);
            uint16_t rec_len = Ext2__read_u16(context->current_dir_block_buffer, offset + 4);
            uint8_t name_len = context->current_dir_block_buffer[offset + 6];
            Ext2DirectoryEntryType file_type = context->current_dir_block_buffer[offset + 7];

            if (rec_len == 0)
            {
                break;
            }

            if (inode != 0 && name_len > 0)
            {
                BString name = EMPTY_BSTRING;
                for (size_t i = 0; i < name_len && i < BSTRING_MAX_SIZE - 1; i++)
                {
                    name.data[i] = (char) context->current_dir_block_buffer[offset + 8 + i];
                }
                name.len = name_len;
                name.data[name.len] = '\0';

                if (!BString__equals(&name, &BSTRING_NEW(".")) && !BString__equals(&name, &BSTRING_NEW("..")))
                {
                    context->pending_dir_entry = (Ext2DirectoryEntry) {
                        .name = name,
                        .file_type = file_type,
                        .inode = inode,
                        .mode = 0,
                        .size = 0,
                    };
                    context->current_dir_block_offset = offset + rec_len;
                    context->phase = EXT2_ASYNC_LIST_READ_DIRENT_INODE;
                    context->block_to_read = Ext2Volume__inode_block(&context->fs_state->volume, inode);
                    return Ext2FileIO__read_root_dir_async(context, block_context->kernel_context);
                }
            }

            offset += rec_len;
        }

        context->current_dir_block_index++;
        context->current_dir_block_offset = 0;
        while (
            context->current_dir_block_index < EXT2_NDIR_BLOCKS
            && context->current_inode.block[context->current_dir_block_index] == 0
        )
        {
            context->current_dir_block_index++;
        }

        if (context->current_dir_block_index >= EXT2_NDIR_BLOCKS || context->count >= (DirectoryEntryCount) context->max_entries)
        {
            return Success(Anything, context->count);
        }

        context->block_to_read = context->current_inode.block[context->current_dir_block_index];
        return Ext2FileIO__read_root_dir_async(context, block_context->kernel_context);
    }

    if (context->phase == EXT2_ASYNC_LIST_READ_DIRENT_INODE)
    {
        Ext2Inode inode = { 0 };
        Ext2Inode__read_from_block(
            &context->fs_state->volume,
            context->pending_dir_entry.inode,
            block_context->buffer,
            &inode
        );

        context->pending_dir_entry.mode = inode.mode;
        context->pending_dir_entry.size = inode.size;
        context->entries[context->count] =
            Ext2FileIO__directory_entry_from_ext2_entry(&context->pending_dir_entry);
        context->count++;
        context->phase = EXT2_ASYNC_LIST_READ_DIR_BLOCK_COLLECT;
        return Ext2FileIO__complete_async_list_dir(block_context);
    }

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(DirectoryEntryCount) Ext2FileIO__copy_entries_to_dirents(
    Ext2DirectoryEntries entries, DirectoryEntryPtr output, size_t max_entries
)
{
    DirectoryEntryCount count = 0;
    FAIterate(Ext2DirectoryEntries, entries, index, entry)
        if ((size_t) count >= max_entries)
        {
            return Success(DirectoryEntryCount, count);
        }
        output[count] = Ext2FileIO__directory_entry_from_ext2_entry(&entry);
        count++;
    End

    return Success(DirectoryEntryCount, count);
}

static Result(DirectoryEntryCount) Ext2FileIO__list_dir(
    IOStatePtr self, BString path, DirectoryEntryPtr entries, size_t max_entries
)
{
    Ext2FilesystemIOStatePtr fs_state = (Ext2FilesystemIOStatePtr) self;

    if (Ext2FilesystemIOState__uses_userspace_device(fs_state))
    {
        Ext2AsyncListDirContextPtr context =
            ptr(Kernel__alloc_kernel(sizeof(Ext2AsyncListDirContext)), Ext2AsyncListDirContext);
        *context = (Ext2AsyncListDirContext) {
            .fs_state = fs_state,
            .path = path,
            .entries = entries,
            .max_entries = max_entries,
            .count = 0,
            .segments = BString__split(&path, '/'),
            .segment_index = 0,
            .current_inode_number = EXT2_ROOT_INODE,
            .current_dir_block_index = 0,
            .block_to_read = fs_state->volume.mounted
                ? Ext2Volume__inode_block(&fs_state->volume, EXT2_ROOT_INODE)
                : 1,
            .phase = fs_state->volume.mounted ? EXT2_ASYNC_LIST_READ_INODE : EXT2_ASYNC_LIST_READ_SUPERBLOCK,
        };

        ResultMatch(Anything, Ext2FileIO__read_root_dir_async(context, EMPTY_KERNEL_CONTEXT))
            IsSuccess(count, return Success(DirectoryEntryCount, count))
            IsError(code, message, return Error(DirectoryEntryCount, message, code))
        End
    }

    if (!fs_state->volume.mounted)
    {
        ResultMatch(Ext2Code, Ext2Volume__mount(&fs_state->volume))
            IsError(code, message, return Error(DirectoryEntryCount, message, IO_INVALID_PATH))
        End
    }

    Ext2InodeNumber inode_number = 0;
    ResultMatch(Ext2Code, Ext2Volume__find_inode_at_path(&fs_state->volume, path, &inode_number))
        IsError(code, message, return Error(DirectoryEntryCount, message, IO_INVALID_PATH))
    End
    ResultMatch(Ext2DirectoryEntries, Ext2Volume__list_directory_inode(&fs_state->volume, inode_number))
        IsSuccess(dir_entries, return Ext2FileIO__copy_entries_to_dirents(dir_entries, entries, max_entries))
        IsError(code, message, return Error(DirectoryEntryCount, message, code))
    End

    return Error(DirectoryEntryCount, invalid_path, IO_INVALID_PATH);
}

static Result(IOStatus) Ext2FileIO__mkdir(IOStatePtr self, BString path)
{
    Ext2FilesystemIOStatePtr fs_state = (Ext2FilesystemIOStatePtr) self;

    if (Ext2FilesystemIOState__uses_userspace_device(fs_state))
    {
        return Ext2FileIO__start_async_mkdir(fs_state, path);
    }

    if (!fs_state->volume.mounted)
    {
        ResultMatch(Ext2Code, Ext2Volume__mount(&fs_state->volume))
            IsError(code, message, return Error(IOStatus, message, IO_INVALID_PATH))
        End
    }

    CriticalSection(fs_state->state.lock,
    {
        ResultMatch(Ext2Code, Ext2Volume__mkdir_at_path(&fs_state->volume, path))
            IsSuccess(_, _cs_return Success(IOStatus, IO_OK))
            IsError(code, message, _cs_return Error(IOStatus, message, IO_INVALID_PATH))
        End
    })

    return Error(IOStatus, invalid_path, IO_INVALID_PATH);
}

static Result(IOStatus) Ext2FileIO__remove(IOStatePtr self, BString path, Flags flags)
{
    Ext2FilesystemIOStatePtr fs_state = (Ext2FilesystemIOStatePtr) self;
    if (Ext2FilesystemIOState__uses_userspace_device(fs_state))
    {
        return Ext2FileIO__start_async_remove(fs_state, path, flags);
    }

    if (!fs_state->volume.mounted)
    {
        ResultMatch(Ext2Code, Ext2Volume__mount(&fs_state->volume))
            IsError(code, message, return Error(IOStatus, message, IO_INVALID_PATH))
        End
    }

    CriticalSection(fs_state->state.lock,
    {
        ResultMatch(Ext2Code, Ext2Volume__remove_at_path(&fs_state->volume, path, flags))
            IsSuccess(_, _cs_return Success(IOStatus, IO_OK))
            IsError(code, message, _cs_return Error(IOStatus, message, IO_INVALID_PATH))
        End
    })

    return Error(IOStatus, invalid_path, IO_INVALID_PATH);
}

static Result(IOStatus) Ext2FileIO__link(IOStatePtr self, BString source_path, BString target_path)
{
    Ext2FilesystemIOStatePtr fs_state = (Ext2FilesystemIOStatePtr) self;
    if (Ext2FilesystemIOState__uses_userspace_device(fs_state))
    {
        return Ext2FileIO__start_async_link(fs_state, source_path, target_path);
    }

    if (!fs_state->volume.mounted)
    {
        ResultMatch(Ext2Code, Ext2Volume__mount(&fs_state->volume))
            IsError(code, message, return Error(IOStatus, message, IO_INVALID_PATH))
        End
    }

    CriticalSection(fs_state->state.lock,
    {
        ResultMatch(Ext2Code, Ext2Volume__link_at_path(&fs_state->volume, source_path, target_path))
            IsSuccess(_, _cs_return Success(IOStatus, IO_OK))
            IsError(code, message, _cs_return Error(IOStatus, message, IO_INVALID_PATH))
        End
    })

    return Error(IOStatus, invalid_path, IO_INVALID_PATH);
}

FileIO Ext2FilesystemIO__new()
{
    return (FileIO) {
        .base_io = (IO) {
            .read = Ext2FileIO__read,
            .write = Ext2FileIO__write,
            .open = Ext2FileIO__open,
            .close = Ext2FileIO__close,
            .park = Ext2FileIO__park,
            .unpark = Ext2FileIO__unpark,
        },
        .list_nodes = NULL,
        .list_dir = Ext2FileIO__list_dir,
        .mkdir = Ext2FileIO__mkdir,
        .remove = Ext2FileIO__remove,
        .link = Ext2FileIO__link,
    };
}

bool Ext2FilesystemIO__available()
{
    return true;
}

Ext2FilesystemIOStatePtr Ext2FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, BlockCount total_sectors
)
{
    Ext2FilesystemIOStatePtr io_state =
        ptr(Kernel__alloc_kernel(sizeof(Ext2FilesystemIOState)), Ext2FilesystemIOState);

    *io_state = (Ext2FilesystemIOState) {
        .state = {
            .state = NEW_IO_STATE,
            .filesystem = fs,
            .handles = FileHandleTargetMap__new(),
            .lock = MUTEX_INIT,
        },
        .volume = Ext2Volume__new(Some(BlockDevicePtr, device), total_sectors),
        .handles = Ext2FileHandleMap__new(),
        .handle_count = 1,
    };
    fs->state = &io_state->state;

    return io_state;
}
