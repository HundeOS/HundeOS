#define DRIVER_LOG_TAG "EXT2FS"

#include <string.h>

#include "sys/debug.h"
#include "sys/service.h"
#include "async_io/async_io.h"
#include "types/fixed_array.h"
#include "../../user.h"
#include "../../../common/types/bstring_additional_types.h"

#ifndef EXT2FS_ENTRYPOINT
#define EXT2FS_ENTRYPOINT main
#endif

#define EXT2FS_ARG_COUNT 3
#define EXT2FS_BLOCK_SIZE 1024U
#define EXT2FS_DEVICE_BLOCK_SIZE 512U
#define EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK (EXT2FS_BLOCK_SIZE / EXT2FS_DEVICE_BLOCK_SIZE)
#define EXT2FS_SUPERBLOCK_BLOCK 1U
#define EXT2FS_ROOT_INODE 2U
#define EXT2FS_NDIR_BLOCKS 12U
#define EXT2FS_MAX_READ_SIZE (EXT2FS_BLOCK_SIZE * EXT2FS_NDIR_BLOCKS)
#define EXT2FS_MAX_GROUPS 32U
#define EXT2FS_MAX_PATH_SEGMENTS 32U

#define EXT2FS_SUPER_MAGIC 0xEF53U
#define EXT2FS_MODE_TYPE_MASK 0xF000U
#define EXT2FS_MODE_FILE 0x8000U
#define EXT2FS_MODE_DIR 0x4000U
#define EXT2FS_DIRENT_TYPE_FILE 1U
#define EXT2FS_DIRENT_TYPE_DIR 2U

typedef uint8_t Ext2fsBlock[EXT2FS_BLOCK_SIZE];
typedef Ext2fsBlock * Ext2fsBlockPtr;

typedef struct Ext2fsBlockData
{
    Ext2fsBlock data;
} Ext2fsBlockData;

ResultDef(Ext2fsBlockData);

typedef struct Ext2fsInode
{
    FileMode mode;
    uint32_t size;
    uint16_t links_count;
    uint32_t block[15];
} Ext2fsInode;

typedef Ext2fsInode * Ext2fsInodePtr;
typedef const Ext2fsInode * ConstExt2fsInodePtr;

ResultDef(Ext2fsInode);

typedef struct Ext2fsVolume
{
    BString device_path;
    uint32_t blocks_count;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t group_count;
    uint32_t first_data_block;
    uint32_t block_bitmap_block;
    uint32_t inode_bitmap_block;
    uint32_t inode_table_block[EXT2FS_MAX_GROUPS];
    uint32_t group_block_bitmap_block[EXT2FS_MAX_GROUPS];
    uint32_t group_inode_bitmap_block[EXT2FS_MAX_GROUPS];
    bool use_async_io;
} Ext2fsVolume;

typedef Ext2fsVolume * Ext2fsVolumePtr;

typedef enum Ext2fsCode
{
    EXT2FS_OK = 0,
    EXT2FS_IO_ERROR = -1,
    EXT2FS_INVALID_PATH = -2,
    EXT2FS_NOT_FOUND = -3,
    EXT2FS_NOT_A_DIRECTORY = -4,
    EXT2FS_NOT_A_FILE = -5,
    EXT2FS_ALREADY_EXISTS = -6,
    EXT2FS_NO_SPACE = -7,
    EXT2FS_UNSUPPORTED = -8,
} Ext2fsCode;

ResultDef(Ext2fsCode);
ResultDef(uint32_t);

typedef struct Ext2fsResolvedParent
{
    uint32_t parent_inode_number;
    BString leaf_name;
} Ext2fsResolvedParent;

static Ext2fsResolvedParent Ext2fsResolvedParent__new(uint32_t parent_inode_number, ConstBStringPtr leaf_name)
{
    return (Ext2fsResolvedParent){
        .parent_inode_number = parent_inode_number,
        .leaf_name = *leaf_name,
    };
}

ResultDef(Ext2fsResolvedParent);

typedef struct Ext2fsFirstChild
{
    BString name;
    uint32_t inode_number;
    Ext2fsInode inode;
} Ext2fsFirstChild;

static Ext2fsFirstChild Ext2fsFirstChild__new(ConstBStringPtr name, uint32_t inode_number, ConstExt2fsInodePtr inode)
{
    return (Ext2fsFirstChild){
        .name = *name,
        .inode_number = inode_number,
        .inode = *inode,
    };
}

ResultDef(Ext2fsFirstChild);

typedef struct Ext2fsWriteFileResult
{
    uint32_t inode_number;
    Ext2fsInode inode;
} Ext2fsWriteFileResult;

static Ext2fsWriteFileResult Ext2fsWriteFileResult__new(uint32_t inode_number, ConstExt2fsInodePtr inode)
{
    return (Ext2fsWriteFileResult){
        .inode_number = inode_number,
        .inode = *inode,
    };
}

ResultDef(Ext2fsWriteFileResult);

static Ext2fsVolume Ext2fsVolume__new(ConstBStringPtr device_path)
{
    return (Ext2fsVolume){
        .device_path = *device_path,
        .use_async_io = false,
    };
}

typedef struct Ext2fsServiceContext
{
    Ext2fsVolume volume;
    BString filesystem_type;
    BString mountpoint;
} Ext2fsServiceContext;

typedef Ext2fsServiceContext * Ext2fsServiceContextPtr;

static BString ext2fs_io_error = bstr("EXT2FS IO error");
static BString ext2fs_invalid_path = bstr("EXT2FS invalid path");
static BString ext2fs_not_found = bstr("EXT2FS not found");
static BString ext2fs_not_a_directory = bstr("EXT2FS not a directory");
static BString ext2fs_not_a_file = bstr("EXT2FS not a file");
static BString ext2fs_already_exists = bstr("EXT2FS already exists");
static BString ext2fs_no_space = bstr("EXT2FS no space");
static BString ext2fs_unsupported = bstr("EXT2FS unsupported filesystem");

static Ext2fsServiceContext Ext2fsServiceContext__new(ConstBStringPtr device_path, ConstBStringPtr mountpoint)
{
    return (Ext2fsServiceContext){
        .volume = Ext2fsVolume__new(device_path),
        .filesystem_type = bstr("ext2"),
        .mountpoint = *mountpoint,
    };
}

static uint16_t Ext2fs__read_u16(uint8_t * data, size_t offset)
{
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

static uint32_t Ext2fs__read_u32(uint8_t * data, size_t offset)
{
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) |
           ((uint32_t)data[offset + 3] << 24);
}

static void Ext2fs__write_u16(uint8_t * data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value & 0xff);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xff);
}

static void Ext2fs__write_u32(uint8_t * data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value & 0xff);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xff);
    data[offset + 2] = (uint8_t)((value >> 16) & 0xff);
    data[offset + 3] = (uint8_t)((value >> 24) & 0xff);
}

static Result(Ext2fsBlockData) Ext2fs__read_block(Ext2fsVolumePtr self, uint32_t block)
{
    Ext2fsBlockData target;
    if (self->use_async_io)
    {
        match_result(
            IO__block_read(&self->device_path, (uint64_t)block * EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
                           EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, (Address)&target.data, EXT2FS_BLOCK_SIZE),
            success(result)
            {
                if (result != EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK)
                {
                    return Error(Ext2fsBlockData, ext2fs_io_error, EXT2FS_IO_ERROR);
                }
            },
            error(code, message) { return Error(Ext2fsBlockData, message, code); }
        );

        return Success(Ext2fsBlockData, target);
    }

    ServiceBlockReadResult result =
        service_block_read_device(&self->device_path, (uint64_t)block * EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
                                  EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, (Address)&target.data, EXT2FS_BLOCK_SIZE);
    if (result != EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK)
    {
        return Error(Ext2fsBlockData, ext2fs_io_error, EXT2FS_IO_ERROR);
    }

    return Success(Ext2fsBlockData, target);
}

static Result(Ext2fsCode) Ext2fs__write_block(Ext2fsVolumePtr self, uint32_t block, Ext2fsBlockPtr source)
{
    if (self->use_async_io)
    {
        match_result(
            IO__block_write(&self->device_path, (uint64_t)block * EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
                            EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, (Address)source, EXT2FS_BLOCK_SIZE),
            success(result)
            {
                if (result != EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK)
                {
                    return Error(Ext2fsCode, ext2fs_io_error, EXT2FS_IO_ERROR);
                }
            },
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );

        return Success(Ext2fsCode, EXT2FS_OK);
    }

    ServiceBlockReadResult result =
        service_block_write_device(&self->device_path, (uint64_t)block * EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
                                   EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, (Address)source, EXT2FS_BLOCK_SIZE);
    if (result != EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK)
    {
        return Error(Ext2fsCode, ext2fs_io_error, EXT2FS_IO_ERROR);
    }

    return Success(Ext2fsCode, EXT2FS_OK);
}

static uint32_t Ext2fsVolume__inode_block(Ext2fsVolumePtr self, uint32_t inode_number)
{
    if (inode_number == 0 || self->inodes_per_group == 0)
    {
        return 0;
    }

    uint32_t index = inode_number - 1;
    uint32_t group = index / self->inodes_per_group;
    if (group >= self->group_count)
    {
        return 0;
    }

    uint32_t group_index = index % self->inodes_per_group;
    return self->inode_table_block[group] + ((group_index * self->inode_size) / EXT2FS_BLOCK_SIZE);
}

static size_t Ext2fsVolume__inode_offset(Ext2fsVolumePtr self, uint32_t inode_number)
{
    uint32_t index = inode_number - 1;
    uint32_t group_index = self->inodes_per_group == 0 ? index : index % self->inodes_per_group;
    return (group_index * self->inode_size) % EXT2FS_BLOCK_SIZE;
}

static Result(Ext2fsCode) Ext2fsVolume__mount(Ext2fsVolumePtr self)
{
    Ext2fsBlockData block;
    match_result(
        Ext2fs__read_block(self, EXT2FS_SUPERBLOCK_BLOCK),
        success(data) { block = data; },
        error(code, message)
        {
            DRIVER_DEBUG("ext2fs: failed to read superblock\n");
            return Error(Ext2fsCode, message, code);
        }
    );

    if (Ext2fs__read_u16(block.data, 56) != EXT2FS_SUPER_MAGIC)
    {
        DRIVER_DEBUG("ext2fs: invalid magic\n");
        return Error(Ext2fsCode, ext2fs_unsupported, EXT2FS_UNSUPPORTED);
    }

    uint32_t log_block_size = Ext2fs__read_u32(block.data, 24);
    if (log_block_size != 0)
    {
        DRIVER_DEBUG("ext2fs: only 1024 byte blocks are supported\n");
        return Error(Ext2fsCode, ext2fs_unsupported, EXT2FS_UNSUPPORTED);
    }

    self->blocks_count = Ext2fs__read_u32(block.data, 4);
    self->first_data_block = Ext2fs__read_u32(block.data, 20);
    self->blocks_per_group = Ext2fs__read_u32(block.data, 32);
    self->inodes_per_group = Ext2fs__read_u32(block.data, 40);
    self->inode_size = Ext2fs__read_u16(block.data, 88);
    if (self->inode_size == 0)
    {
        self->inode_size = 128;
    }

    self->group_count = (self->blocks_count + self->blocks_per_group - 1) / self->blocks_per_group;
    if (self->group_count > EXT2FS_MAX_GROUPS)
    {
        DRIVER_DEBUG("ext2fs: too many groups\n");
        return Error(Ext2fsCode, ext2fs_unsupported, EXT2FS_UNSUPPORTED);
    }

    match_result(
        Ext2fs__read_block(self, 2),
        success(data) { block = data; },
        error(code, message)
        {
            DRIVER_DEBUG("ext2fs: failed to read group descriptors\n");
            return Error(Ext2fsCode, message, code);
        }
    );

    for (uint32_t group = 0; group < self->group_count; group++)
    {
        self->group_block_bitmap_block[group] = Ext2fs__read_u32(block.data, group * 32U);
        self->group_inode_bitmap_block[group] = Ext2fs__read_u32(block.data, group * 32U + 4U);
        self->inode_table_block[group] = Ext2fs__read_u32(block.data, group * 32U + 8U);
    }

    self->block_bitmap_block = self->group_block_bitmap_block[0];
    self->inode_bitmap_block = self->group_inode_bitmap_block[0];
    return Success(Ext2fsCode, EXT2FS_OK);
}

static Result(Ext2fsInode) Ext2fsVolume__read_inode(Ext2fsVolumePtr self, uint32_t inode_number)
{
    if (inode_number == 0)
    {
        return Error(Ext2fsInode, ext2fs_invalid_path, EXT2FS_INVALID_PATH);
    }

    uint32_t block = Ext2fsVolume__inode_block(self, inode_number);
    size_t offset = Ext2fsVolume__inode_offset(self, inode_number);
    if (block == 0)
    {
        return Error(Ext2fsInode, ext2fs_invalid_path, EXT2FS_INVALID_PATH);
    }

    Ext2fsBlockData data;
    match_result(
        Ext2fs__read_block(self, block),
        success(block_data) { data = block_data; },
        error(code, message) { return Error(Ext2fsInode, message, code); }
    );

    Ext2fsInode target;
    target.mode = Ext2fs__read_u16(data.data, offset);
    target.size = Ext2fs__read_u32(data.data, offset + 4);
    target.links_count = Ext2fs__read_u16(data.data, offset + 26);
    if (target.links_count == 0)
    {
        target.links_count = 1;
    }
    for (uint32_t i = 0; i < 15; i++)
    {
        target.block[i] = Ext2fs__read_u32(data.data, offset + 40 + (i * 4));
    }

    return Success(Ext2fsInode, target);
}

static DirectoryEntry Ext2fs__directory_entry(ConstBStringPtr name, uint8_t file_type, Ext2fsInodePtr inode)
{
    FileMode mode = inode->mode;
    if ((mode & EXT2FS_MODE_TYPE_MASK) == 0)
    {
        mode |= file_type == EXT2FS_DIRENT_TYPE_DIR ? IO_MODE_TYPE_DIR : IO_MODE_TYPE_FILE;
    }

    return DirectoryEntry__new(IO_DIRENT_ATTR_NONE, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT, mode, name);
}

static Result(uint32_t) Ext2fs__find_child_inode(Ext2fsVolumePtr self, Ext2fsInodePtr directory, ConstBStringPtr name)
{
    for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS; i++)
    {
        if (directory->block[i] == 0)
        {
            continue;
        }

        Ext2fsBlockData block;
        match_result(
            Ext2fs__read_block(self, directory->block[i]),
            success(block_data) { block = block_data; },
            error(code, message) { return Error(uint32_t, message, code); }
        );

        size_t offset = 0;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE)
        {
            uint32_t candidate_inode = Ext2fs__read_u32(block.data, offset);
            uint16_t rec_len = Ext2fs__read_u16(block.data, offset + 4);
            uint8_t name_len = block.data[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (candidate_inode != 0 && name_len > 0)
            {
                BString candidate = BString__from_chars((const char *)&block.data[offset + 8], name_len);
                if (BString__equals(&candidate, name))
                {
                    return Success(uint32_t, candidate_inode);
                }
            }

            offset += rec_len;
        }
    }

    return Error(uint32_t, ext2fs_not_found, EXT2FS_NOT_FOUND);
}

static Result(Ext2fsInode) Ext2fsVolume__inode_for_path(Ext2fsVolumePtr self, ConstBStringPtr path)
{
    Ext2fsInode current;
    match_result(
        Ext2fsVolume__read_inode(self, EXT2FS_ROOT_INODE),
        success(inode) { current = inode; },
        error(code, message) { return Error(Ext2fsInode, message, code); }
    );

    BStringArray segments = BString__split(path, '/');
    for (uint64_t i = 0; i < BStringArray__len(&segments); i++)
    {
        match_optional(
            BStringArray__at(&segments, i),
            some(segment)
            {
                if (BString__is_empty(&segment))
                {
                    continue;
                }

                match_result(
                    Ext2fs__find_child_inode(self, &current, &segment),
                    success(child_inode)
                    {
                        match_result(
                            Ext2fsVolume__read_inode(self, child_inode),
                            success(inode) { current = inode; },
                            error(code, message) { return Error(Ext2fsInode, message, code); }
                        );
                    },
                    error(code, message) { return Error(Ext2fsInode, message, code); }
                );
            },
            none_do_nothing()
        );
    }

    return Success(Ext2fsInode, current);
}

static DirectoryEntryCount Ext2fsVolume__list_dir(Ext2fsVolumePtr self, ConstBStringPtr path, DirectoryEntryPtr entries,
                                                  size_t max_entries)
{
    Ext2fsInode directory;
    match_result(
        Ext2fsVolume__inode_for_path(self, path),
        success(inode) { directory = inode; },
        error(code, message) { return IO_INVALID_PATH; }
    );

    if ((directory.mode & EXT2FS_MODE_TYPE_MASK) != EXT2FS_MODE_DIR)
    {
        return IO_INVALID_PATH;
    }

    DirectoryEntryCount count = 0;
    for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS && (size_t)count < max_entries; i++)
    {
        if (directory.block[i] == 0)
        {
            continue;
        }

        Ext2fsBlockData block;
        match_result(
            Ext2fs__read_block(self, directory.block[i]),
            success(block_data) { block = block_data; },
            error(code, message) { return IO_INVALID_PATH; }
        );

        size_t offset = 0;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE && (size_t)count < max_entries)
        {
            uint32_t inode_number = Ext2fs__read_u32(block.data, offset);
            uint16_t rec_len = Ext2fs__read_u16(block.data, offset + 4);
            uint8_t name_len = block.data[offset + 6];
            uint8_t file_type = block.data[offset + 7];

            if (rec_len == 0)
            {
                break;
            }

            if (inode_number != 0 && name_len > 0)
            {
                BString name = BString__from_chars((const char *)&block.data[offset + 8], name_len);
                if (!BString__equals(&name, &bstr(".")) && !BString__equals(&name, &bstr("..")))
                {
                    Ext2fsInode entry_inode;
                    match_result(
                        Ext2fsVolume__read_inode(self, inode_number),
                        success(inode) { entry_inode = inode; },
                        error(code, message) { return IO_INVALID_PATH; }
                    );
                    entries[count] = Ext2fs__directory_entry(&name, file_type, &entry_inode);
                    count++;
                }
            }

            offset += rec_len;
        }
    }

    return count;
}

static bool Ext2fsInode__is_dir(Ext2fsInodePtr self)
{
    return (self->mode & EXT2FS_MODE_TYPE_MASK) == EXT2FS_MODE_DIR ||
           (self->mode & IO_MODE_TYPE_MASK) == IO_MODE_TYPE_DIR;
}

static bool Ext2fsInode__is_file(Ext2fsInodePtr self)
{
    return (self->mode & EXT2FS_MODE_TYPE_MASK) == EXT2FS_MODE_FILE ||
           (self->mode & IO_MODE_TYPE_MASK) == IO_MODE_TYPE_FILE;
}

static BString Ext2fs__segment_at(BStringArrayPtr segments, size_t index)
{
    match_optional(
        BStringArray__at(segments, index),
        some(segment) { return segment; },
        none() { return empty_bstr(); }
    );
}

static Result(Ext2fsResolvedParent) Ext2fsVolume__resolve_parent(Ext2fsVolumePtr self, ConstBStringPtr path)
{
    if (BString__length(path) == 0 || BString__equals(path, &bstr("/")))
    {
        return Error(Ext2fsResolvedParent, ext2fs_invalid_path, EXT2FS_INVALID_PATH);
    }

    BStringArray segments = BString__split(path, '/');
    if (BStringArray__len(&segments) == 0)
    {
        return Error(Ext2fsResolvedParent, ext2fs_invalid_path, EXT2FS_INVALID_PATH);
    }

    Ext2fsInode current;
    match_result(
        Ext2fsVolume__read_inode(self, EXT2FS_ROOT_INODE),
        success(inode) { current = inode; },
        error(code, message) { return Error(Ext2fsResolvedParent, message, code); }
    );

    uint32_t current_inode = EXT2FS_ROOT_INODE;
    for (uint64_t i = 0; i + 1 < BStringArray__len(&segments); i++)
    {
        BString segment = Ext2fs__segment_at(&segments, i);
        match_result(
            Ext2fs__find_child_inode(self, &current, &segment),
            success(inode_number) { current_inode = inode_number; },
            error(code, message) { return Error(Ext2fsResolvedParent, message, code); }
        );

        match_result(
            Ext2fsVolume__read_inode(self, current_inode),
            success(inode) { current = inode; },
            error(code, message) { return Error(Ext2fsResolvedParent, message, code); }
        );

        if (!Ext2fsInode__is_dir(&current))
        {
            return Error(Ext2fsResolvedParent, ext2fs_not_a_directory, EXT2FS_NOT_A_DIRECTORY);
        }
    }

    BString leaf_name = Ext2fs__segment_at(&segments, BStringArray__len(&segments) - 1);
    if (BString__is_empty(&leaf_name))
    {
        return Error(Ext2fsResolvedParent, ext2fs_invalid_path, EXT2FS_INVALID_PATH);
    }

    return Success(Ext2fsResolvedParent, Ext2fsResolvedParent__new(current_inode, &leaf_name));
}

static Result(Ext2fsCode) Ext2fsVolume__write_inode(Ext2fsVolumePtr self, uint32_t inode_number, Ext2fsInodePtr source)
{
    uint32_t inode_block = Ext2fsVolume__inode_block(self, inode_number);
    size_t inode_offset = Ext2fsVolume__inode_offset(self, inode_number);
    if (inode_block == 0)
    {
        return Error(Ext2fsCode, ext2fs_invalid_path, EXT2FS_INVALID_PATH);
    }

    Ext2fsBlockData block;
    match_result(
        Ext2fs__read_block(self, inode_block),
        success(block_data) { block = block_data; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    Ext2fs__write_u16(block.data, inode_offset, source->mode);
    Ext2fs__write_u32(block.data, inode_offset + 4, source->size);
    Ext2fs__write_u16(block.data, inode_offset + 26, source->links_count);
    for (uint32_t i = 0; i < 15; i++)
    {
        Ext2fs__write_u32(block.data, inode_offset + 40 + (i * 4), source->block[i]);
    }

    return Ext2fs__write_block(self, inode_block, &block.data);
}

static bool Ext2fs__bitmap_bit_is_set(uint8_t * bitmap, uint32_t index)
{
    return (bitmap[index / 8] & (1U << (index % 8))) != 0;
}

static void Ext2fs__bitmap_set_bit(uint8_t * bitmap, uint32_t index)
{
    bitmap[index / 8] = (uint8_t)(bitmap[index / 8] | (1U << (index % 8)));
}

static void Ext2fs__bitmap_clear_bit(uint8_t * bitmap, uint32_t index)
{
    bitmap[index / 8] = (uint8_t)(bitmap[index / 8] & ~(1U << (index % 8)));
}

static Result(uint32_t) Ext2fsVolume__allocate_inode_from_bitmap(Ext2fsVolumePtr self, uint8_t * inode_bitmap)
{
    for (uint32_t inode_index = 0; inode_index < self->inodes_per_group; inode_index++)
    {
        if (!Ext2fs__bitmap_bit_is_set(inode_bitmap, inode_index))
        {
            Ext2fs__bitmap_set_bit(inode_bitmap, inode_index);
            return Success(uint32_t, inode_index + 1);
        }
    }

    return Error(uint32_t, ext2fs_no_space, EXT2FS_NO_SPACE);
}

static Result(uint32_t) Ext2fsVolume__allocate_block_from_bitmap(Ext2fsVolumePtr self, uint8_t * block_bitmap)
{
    for (uint32_t block_index = 0; block_index < self->blocks_per_group; block_index++)
    {
        if (!Ext2fs__bitmap_bit_is_set(block_bitmap, block_index))
        {
            Ext2fs__bitmap_set_bit(block_bitmap, block_index);
            return Success(uint32_t, self->first_data_block + block_index);
        }
    }

    return Error(uint32_t, ext2fs_no_space, EXT2FS_NO_SPACE);
}

static uint16_t Ext2fs__dirent_record_size(size_t name_len)
{
    size_t padded = (name_len + 3U) & ~3U;
    return (uint16_t)(8U + padded);
}

static Result(Ext2fsCode) Ext2fsVolume__update_allocation_counts(Ext2fsVolumePtr self, int32_t block_delta,
                                                                 int32_t inode_delta, int32_t used_dirs_delta)
{
    Ext2fsBlockData superblock;
    Ext2fsBlockData group_desc;
    match_result(
        Ext2fs__read_block(self, EXT2FS_SUPERBLOCK_BLOCK),
        success(block_data) { superblock = block_data; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    match_result(
        Ext2fs__read_block(self, 2),
        success(block_data) { group_desc = block_data; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    uint32_t free_blocks = Ext2fs__read_u32(superblock.data, 12);
    uint32_t free_inodes = Ext2fs__read_u32(superblock.data, 16);
    uint16_t group_free_blocks = Ext2fs__read_u16(group_desc.data, 12);
    uint16_t group_free_inodes = Ext2fs__read_u16(group_desc.data, 14);
    uint16_t used_dirs = Ext2fs__read_u16(group_desc.data, 16);

    Ext2fs__write_u32(superblock.data, 12, (uint32_t)((int32_t)free_blocks + block_delta));
    Ext2fs__write_u32(superblock.data, 16, (uint32_t)((int32_t)free_inodes + inode_delta));
    Ext2fs__write_u16(group_desc.data, 12, (uint16_t)((int32_t)group_free_blocks + block_delta));
    Ext2fs__write_u16(group_desc.data, 14, (uint16_t)((int32_t)group_free_inodes + inode_delta));
    Ext2fs__write_u16(group_desc.data, 16, (uint16_t)((int32_t)used_dirs + used_dirs_delta));

    match_result(
        Ext2fs__write_block(self, EXT2FS_SUPERBLOCK_BLOCK, &superblock.data),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    return Ext2fs__write_block(self, 2, &group_desc.data);
}

static Result(Ext2fsCode)
    Ext2fsVolume__insert_dirent(Ext2fsVolumePtr self, uint32_t directory_inode_number, ConstBStringPtr name,
                                uint32_t child_inode_number, uint8_t child_type)
{
    Ext2fsInode directory_inode;
    match_result(
        Ext2fsVolume__read_inode(self, directory_inode_number),
        success(inode) { directory_inode = inode; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    if (!Ext2fsInode__is_dir(&directory_inode))
    {
        return Error(Ext2fsCode, ext2fs_not_a_directory, EXT2FS_NOT_A_DIRECTORY);
    }

    uint16_t required_len = Ext2fs__dirent_record_size(BString__length(name));
    for (uint32_t block_index = 0; block_index < EXT2FS_NDIR_BLOCKS; block_index++)
    {
        if (directory_inode.block[block_index] == 0)
        {
            continue;
        }

        Ext2fsBlockData block;
        match_result(
            Ext2fs__read_block(self, directory_inode.block[block_index]),
            success(block_data) { block = block_data; },
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );

        size_t offset = 0;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE)
        {
            uint32_t entry_inode = Ext2fs__read_u32(block.data, offset);
            uint16_t rec_len = Ext2fs__read_u16(block.data, offset + 4);
            uint8_t name_len = block.data[offset + 6];
            if (rec_len == 0)
            {
                break;
            }

            if (entry_inode != 0)
            {
                uint16_t actual_len = Ext2fs__dirent_record_size(name_len);
                if (rec_len >= actual_len + required_len)
                {
                    size_t new_offset = offset + actual_len;
                    Ext2fs__write_u16(block.data, offset + 4, actual_len);
                    Ext2fs__write_u32(block.data, new_offset, child_inode_number);
                    Ext2fs__write_u16(block.data, new_offset + 4, rec_len - actual_len);
                    block.data[new_offset + 6] = (uint8_t)BString__length(name);
                    block.data[new_offset + 7] = child_type;
                    for (StringPos i = 0; i < BString__length(name); i++)
                    {
                        block.data[new_offset + 8 + i] = (uint8_t)BString__char_at_or(name, i, '\0');
                    }
                    return Ext2fs__write_block(self, directory_inode.block[block_index], &block.data);
                }
            }

            offset += rec_len;
        }
    }

    return Error(Ext2fsCode, ext2fs_no_space, EXT2FS_NO_SPACE);
}

static Result(uint32_t)
    Ext2fsVolume__remove_dirent(Ext2fsVolumePtr self, uint32_t directory_inode_number, ConstBStringPtr name)
{
    Ext2fsInode directory_inode;
    match_result(
        Ext2fsVolume__read_inode(self, directory_inode_number),
        success(inode) { directory_inode = inode; },
        error(code, message) { return Error(uint32_t, message, code); }
    );

    if (!Ext2fsInode__is_dir(&directory_inode))
    {
        return Error(uint32_t, ext2fs_not_a_directory, EXT2FS_NOT_A_DIRECTORY);
    }

    for (uint32_t block_index = 0; block_index < EXT2FS_NDIR_BLOCKS; block_index++)
    {
        if (directory_inode.block[block_index] == 0)
        {
            continue;
        }

        Ext2fsBlockData block;
        match_result(
            Ext2fs__read_block(self, directory_inode.block[block_index]),
            success(block_data) { block = block_data; },
            error(code, message) { return Error(uint32_t, message, code); }
        );

        size_t offset = 0;
        size_t previous_offset = 0;
        bool has_previous = false;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE)
        {
            uint32_t entry_inode = Ext2fs__read_u32(block.data, offset);
            uint16_t rec_len = Ext2fs__read_u16(block.data, offset + 4);
            uint8_t name_len = block.data[offset + 6];
            if (rec_len == 0)
            {
                break;
            }

            if (entry_inode != 0 && name_len > 0)
            {
                BString entry_name = BString__from_chars((const char *)&block.data[offset + 8], name_len);
                if (BString__equals(&entry_name, name))
                {
                    if (has_previous)
                    {
                        uint16_t previous_len = Ext2fs__read_u16(block.data, previous_offset + 4);
                        Ext2fs__write_u16(block.data, previous_offset + 4, previous_len + rec_len);
                    }
                    else
                    {
                        Ext2fs__write_u32(block.data, offset, 0);
                    }
                    match_result(
                        Ext2fs__write_block(self, directory_inode.block[block_index], &block.data),
                        success(code) { return Success(uint32_t, entry_inode); },
                        error(code, message) { return Error(uint32_t, message, code); }
                    );
                }
            }

            previous_offset = offset;
            has_previous = true;
            offset += rec_len;
        }
    }

    return Error(uint32_t, ext2fs_not_found, EXT2FS_NOT_FOUND);
}

static void Ext2fsInode__reset_file(Ext2fsInodePtr inode, uint32_t size)
{
    inode->mode = EXT2FS_MODE_FILE | 0644;
    inode->size = size;
    inode->links_count = 1;
    for (uint32_t i = 0; i < 15; i++)
    {
        inode->block[i] = 0;
    }
}

static void Ext2fsInode__reset_dir(Ext2fsInodePtr inode, uint32_t block)
{
    inode->mode = EXT2FS_MODE_DIR | 0755;
    inode->size = EXT2FS_BLOCK_SIZE;
    inode->links_count = 2;
    for (uint32_t i = 0; i < 15; i++)
    {
        inode->block[i] = 0;
    }
    inode->block[0] = block;
}

static void Ext2fs__initialise_dir_block(uint8_t * block, uint32_t self_inode, uint32_t parent_inode)
{
    memset(block, 0, EXT2FS_BLOCK_SIZE);

    Ext2fs__write_u32(block, 0, self_inode);
    Ext2fs__write_u16(block, 4, 12);
    block[6] = 1;
    block[7] = EXT2FS_DIRENT_TYPE_DIR;
    block[8] = '.';

    Ext2fs__write_u32(block, 12, parent_inode);
    Ext2fs__write_u16(block, 16, EXT2FS_BLOCK_SIZE - 12);
    block[18] = 2;
    block[19] = EXT2FS_DIRENT_TYPE_DIR;
    block[20] = '.';
    block[21] = '.';
}

static Result(Ext2fsCode)
    Ext2fsVolume__write_inode_data(Ext2fsVolumePtr self, uint32_t inode_number, Ext2fsInodePtr inode, uint8_t * data,
                                   size_t size, bool creating)
{
    if (size > EXT2FS_MAX_READ_SIZE)
    {
        return Error(Ext2fsCode, ext2fs_unsupported, EXT2FS_UNSUPPORTED);
    }

    Ext2fsBlockData inode_bitmap;
    Ext2fsBlockData block_bitmap;
    int32_t free_block_delta = 0;
    int32_t free_inode_delta = 0;

    if (creating)
    {
        match_result(
            Ext2fs__read_block(self, self->inode_bitmap_block),
            success(block_data) { inode_bitmap = block_data; },
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
        Ext2fs__bitmap_set_bit(inode_bitmap.data, inode_number - 1);
        free_inode_delta--;
    }

    match_result(
        Ext2fs__read_block(self, self->block_bitmap_block),
        success(block_data) { block_bitmap = block_data; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS; i++)
    {
        if (inode->block[i] != 0)
        {
            Ext2fs__bitmap_clear_bit(block_bitmap.data, inode->block[i] - self->first_data_block);
            free_block_delta++;
            inode->block[i] = 0;
        }
    }

    size_t blocks_needed = (size + EXT2FS_BLOCK_SIZE - 1) / EXT2FS_BLOCK_SIZE;
    for (uint32_t i = 0; i < blocks_needed; i++)
    {
        match_result(
            Ext2fsVolume__allocate_block_from_bitmap(self, block_bitmap.data),
            success(block_number) { inode->block[i] = block_number; },
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
        free_block_delta--;
    }

    for (uint32_t i = 0; i < blocks_needed; i++)
    {
        Ext2fsBlock block;
        memset(block, 0, sizeof(block));
        size_t source_offset = i * EXT2FS_BLOCK_SIZE;
        size_t remaining = size - source_offset;
        size_t copy_size = remaining < EXT2FS_BLOCK_SIZE ? remaining : EXT2FS_BLOCK_SIZE;
        memcpy(block, &data[source_offset], copy_size);

        match_result(
            Ext2fs__write_block(self, inode->block[i], &block),
            success_do_nothing(),
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
    }

    inode->size = (uint32_t)size;
    match_result(
        Ext2fsVolume__write_inode(self, inode_number, inode),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    if (creating)
    {
        match_result(
            Ext2fs__write_block(self, self->inode_bitmap_block, &inode_bitmap.data),
            success_do_nothing(),
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
    }

    match_result(
        Ext2fs__write_block(self, self->block_bitmap_block, &block_bitmap.data),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    if (free_block_delta != 0 || free_inode_delta != 0)
    {
        return Ext2fsVolume__update_allocation_counts(self, free_block_delta, free_inode_delta, 0);
    }

    return Success(Ext2fsCode, EXT2FS_OK);
}

static Result(Ext2fsWriteFileResult)
    Ext2fsVolume__write_file_at_path(Ext2fsVolumePtr self, ConstBStringPtr path, uint8_t * data, size_t size)
{
    Ext2fsResolvedParent parent;
    match_result(
        Ext2fsVolume__resolve_parent(self, path),
        success(resolved) { parent = resolved; },
        error(code, message) { return Error(Ext2fsWriteFileResult, message, code); }
    );

    Ext2fsInode parent_inode;
    match_result(
        Ext2fsVolume__read_inode(self, parent.parent_inode_number),
        success(inode) { parent_inode = inode; },
        error(code, message) { return Error(Ext2fsWriteFileResult, message, code); }
    );

    if (!Ext2fsInode__is_dir(&parent_inode))
    {
        return Error(Ext2fsWriteFileResult, ext2fs_not_a_directory, EXT2FS_NOT_A_DIRECTORY);
    }

    match_result(
        Ext2fs__find_child_inode(self, &parent_inode, &parent.leaf_name),
        success(existing_inode_number)
        {
            Ext2fsInode inode;
            match_result(
                Ext2fsVolume__read_inode(self, existing_inode_number),
                success(existing_inode) { inode = existing_inode; },
                error(code, message) { return Error(Ext2fsWriteFileResult, message, code); }
            );

            if (!Ext2fsInode__is_file(&inode))
            {
                return Error(Ext2fsWriteFileResult, ext2fs_not_a_file, EXT2FS_NOT_A_FILE);
            }

            match_result(
                Ext2fsVolume__write_inode_data(self, existing_inode_number, &inode, data, size, false),
                success_do_nothing(),
                error(code, message) { return Error(Ext2fsWriteFileResult, message, code); }
            );

            return Success(Ext2fsWriteFileResult, Ext2fsWriteFileResult__new(existing_inode_number, &inode));
        },
        error(code, message)
        {
            if (code != EXT2FS_NOT_FOUND)
            {
                return Error(Ext2fsWriteFileResult, message, code);
            }
        }
    );

    Ext2fsBlockData inode_bitmap;
    match_result(
        Ext2fs__read_block(self, self->inode_bitmap_block),
        success(block_data) { inode_bitmap = block_data; },
        error(code, message) { return Error(Ext2fsWriteFileResult, message, code); }
    );

    uint32_t new_inode_number = 0;
    match_result(
        Ext2fsVolume__allocate_inode_from_bitmap(self, inode_bitmap.data),
        success(inode_number) { new_inode_number = inode_number; },
        error(code, message) { return Error(Ext2fsWriteFileResult, message, code); }
    );

    Ext2fsInode inode;
    Ext2fsInode__reset_file(&inode, (uint32_t)size);
    match_result(
        Ext2fsVolume__write_inode_data(self, new_inode_number, &inode, data, size, true),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsWriteFileResult, message, code); }
    );

    match_result(
        Ext2fsVolume__insert_dirent(self, parent.parent_inode_number, &parent.leaf_name, new_inode_number,
                                    EXT2FS_DIRENT_TYPE_FILE),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsWriteFileResult, message, code); }
    );

    return Success(Ext2fsWriteFileResult, Ext2fsWriteFileResult__new(new_inode_number, &inode));
}

static Result(Ext2fsCode) Ext2fsVolume__mkdir_at_path(Ext2fsVolumePtr self, ConstBStringPtr path)
{
    Ext2fsResolvedParent parent;
    match_result(
        Ext2fsVolume__resolve_parent(self, path),
        success(resolved) { parent = resolved; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    Ext2fsInode parent_inode;
    match_result(
        Ext2fsVolume__read_inode(self, parent.parent_inode_number),
        success(inode) { parent_inode = inode; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    if (!Ext2fsInode__is_dir(&parent_inode))
    {
        return Error(Ext2fsCode, ext2fs_not_a_directory, EXT2FS_NOT_A_DIRECTORY);
    }

    match_result(
        Ext2fs__find_child_inode(self, &parent_inode, &parent.leaf_name),
        success(existing_inode_number) { return Error(Ext2fsCode, ext2fs_already_exists, EXT2FS_ALREADY_EXISTS); },
        error(code, message)
        {
            if (code != EXT2FS_NOT_FOUND)
            {
                return Error(Ext2fsCode, message, code);
            }
        }
    );

    Ext2fsBlockData inode_bitmap;
    Ext2fsBlockData block_bitmap;
    Ext2fsBlock dir_block;
    match_result(
        Ext2fs__read_block(self, self->inode_bitmap_block),
        success(block_data) { inode_bitmap = block_data; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    match_result(
        Ext2fs__read_block(self, self->block_bitmap_block),
        success(block_data) { block_bitmap = block_data; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    uint32_t new_inode_number = 0;
    uint32_t new_dir_block = 0;
    match_result(
        Ext2fsVolume__allocate_inode_from_bitmap(self, inode_bitmap.data),
        success(inode_number) { new_inode_number = inode_number; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    match_result(
        Ext2fsVolume__allocate_block_from_bitmap(self, block_bitmap.data),
        success(block_number) { new_dir_block = block_number; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    Ext2fsInode new_inode;
    Ext2fsInode__reset_dir(&new_inode, new_dir_block);
    Ext2fs__initialise_dir_block(dir_block, new_inode_number, parent.parent_inode_number);

    match_result(
        Ext2fs__write_block(self, new_dir_block, &dir_block),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    match_result(
        Ext2fsVolume__write_inode(self, new_inode_number, &new_inode),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    match_result(
        Ext2fs__write_block(self, self->inode_bitmap_block, &inode_bitmap.data),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    match_result(
        Ext2fs__write_block(self, self->block_bitmap_block, &block_bitmap.data),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    match_result(
        Ext2fsVolume__insert_dirent(self, parent.parent_inode_number, &parent.leaf_name, new_inode_number,
                                    EXT2FS_DIRENT_TYPE_DIR),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    return Ext2fsVolume__update_allocation_counts(self, -1, -1, 1);
}

static Result(Ext2fsCode) Ext2fsVolume__free_inode(Ext2fsVolumePtr self, uint32_t inode_number)
{
    if (inode_number == 0)
    {
        return Error(Ext2fsCode, ext2fs_invalid_path, EXT2FS_INVALID_PATH);
    }

    Ext2fsBlockData inode_bitmap;
    match_result(
        Ext2fs__read_block(self, self->inode_bitmap_block),
        success(block_data) { inode_bitmap = block_data; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    Ext2fs__bitmap_clear_bit(inode_bitmap.data, inode_number - 1);
    match_result(
        Ext2fs__write_block(self, self->inode_bitmap_block, &inode_bitmap.data),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    return Ext2fsVolume__update_allocation_counts(self, 0, 1, 0);
}

static Result(Ext2fsCode) Ext2fsVolume__free_block(Ext2fsVolumePtr self, uint32_t block_number)
{
    if (block_number < self->first_data_block)
    {
        return Error(Ext2fsCode, ext2fs_invalid_path, EXT2FS_INVALID_PATH);
    }

    Ext2fsBlockData block_bitmap;
    match_result(
        Ext2fs__read_block(self, self->block_bitmap_block),
        success(block_data) { block_bitmap = block_data; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    Ext2fs__bitmap_clear_bit(block_bitmap.data, block_number - self->first_data_block);
    match_result(
        Ext2fs__write_block(self, self->block_bitmap_block, &block_bitmap.data),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    return Ext2fsVolume__update_allocation_counts(self, 1, 0, 0);
}

static Result(Ext2fsFirstChild) Ext2fsVolume__first_child(Ext2fsVolumePtr self, uint32_t directory_inode_number)
{
    Ext2fsInode directory_inode;
    match_result(
        Ext2fsVolume__read_inode(self, directory_inode_number),
        success(inode) { directory_inode = inode; },
        error(code, message) { return Error(Ext2fsFirstChild, message, code); }
    );

    if (!Ext2fsInode__is_dir(&directory_inode))
    {
        return Error(Ext2fsFirstChild, ext2fs_not_a_directory, EXT2FS_NOT_A_DIRECTORY);
    }

    for (uint32_t block_index = 0; block_index < EXT2FS_NDIR_BLOCKS; block_index++)
    {
        if (directory_inode.block[block_index] == 0)
        {
            continue;
        }

        Ext2fsBlockData block;
        match_result(
            Ext2fs__read_block(self, directory_inode.block[block_index]),
            success(block_data) { block = block_data; },
            error(code, message) { return Error(Ext2fsFirstChild, message, code); }
        );

        size_t offset = 0;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE)
        {
            uint32_t entry_inode = Ext2fs__read_u32(block.data, offset);
            uint16_t rec_len = Ext2fs__read_u16(block.data, offset + 4);
            uint8_t name_len = block.data[offset + 6];
            if (rec_len == 0)
            {
                break;
            }

            if (entry_inode != 0 && name_len > 0)
            {
                BString entry_name = BString__from_chars((const char *)&block.data[offset + 8], name_len);
                if (!BString__equals(&entry_name, &bstr(".")) && !BString__equals(&entry_name, &bstr("..")))
                {
                    match_result(
                        Ext2fsVolume__read_inode(self, entry_inode),
                        success(child_inode)
                        {
                            return Success(Ext2fsFirstChild,
                                           Ext2fsFirstChild__new(&entry_name, entry_inode, &child_inode));
                        },
                        error(code, message) { return Error(Ext2fsFirstChild, message, code); }
                    );
                }
            }

            offset += rec_len;
        }
    }

    return Error(Ext2fsFirstChild, ext2fs_not_found, EXT2FS_NOT_FOUND);
}

static Result(Ext2fsCode)
    Ext2fsVolume__remove_inode(Ext2fsVolumePtr self, uint32_t parent_inode_number, ConstBStringPtr leaf_name,
                               uint32_t target_inode_number, Ext2fsInodePtr target_inode, Flags flags)
{
    if (Ext2fsInode__is_dir(target_inode))
    {
        Result(Ext2fsFirstChild) child_result = Ext2fsVolume__first_child(self, target_inode_number);
        bool has_child = false;
        BString empty_name = empty_bstr();
        Ext2fsFirstChild child = Ext2fsFirstChild__new(&empty_name, 0, target_inode);
        match_result(
            child_result,
            success(found_child)
            {
                child = found_child;
                has_child = true;
            },
            error(code, message)
            {
                if (code != EXT2FS_NOT_FOUND)
                {
                    return Error(Ext2fsCode, message, code);
                }
            }
        );

        while (has_child)
        {
            if ((flags & IO_REMOVE_RECURSIVE) == 0)
            {
                return Error(Ext2fsCode, ext2fs_not_a_directory, EXT2FS_NOT_A_DIRECTORY);
            }

            match_result(
                Ext2fsVolume__remove_inode(self, target_inode_number, &child.name, child.inode_number, &child.inode,
                                           flags),
                success_do_nothing(),
                error(code, message) { return Error(Ext2fsCode, message, code); }
            );
            child_result = Ext2fsVolume__first_child(self, target_inode_number);
            has_child = false;
            match_result(
                child_result,
                success(found_child)
                {
                    child = found_child;
                    has_child = true;
                },
                error(code, message)
                {
                    if (code != EXT2FS_NOT_FOUND)
                    {
                        return Error(Ext2fsCode, message, code);
                    }
                }
            );
        }

        for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS; i++)
        {
            if (target_inode->block[i] != 0)
            {
                match_result(
                    Ext2fsVolume__free_block(self, target_inode->block[i]),
                    success_do_nothing(),
                    error(code, message) { return Error(Ext2fsCode, message, code); }
                );
            }
        }

        memset(target_inode, 0, sizeof(*target_inode));
        match_result(
            Ext2fsVolume__write_inode(self, target_inode_number, target_inode),
            success_do_nothing(),
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
        match_result(
            Ext2fsVolume__free_inode(self, target_inode_number),
            success_do_nothing(),
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
        match_result(
            Ext2fsVolume__update_allocation_counts(self, 0, 0, -1),
            success_do_nothing(),
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
        match_result(
            Ext2fsVolume__remove_dirent(self, parent_inode_number, leaf_name),
            success_do_nothing(),
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
        return Success(Ext2fsCode, EXT2FS_OK);
    }

    if (!Ext2fsInode__is_file(target_inode))
    {
        return Error(Ext2fsCode, ext2fs_not_a_file, EXT2FS_NOT_A_FILE);
    }

    if (target_inode->links_count > 1)
    {
        target_inode->links_count--;
        match_result(
            Ext2fsVolume__write_inode(self, target_inode_number, target_inode),
            success_do_nothing(),
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
        match_result(
            Ext2fsVolume__remove_dirent(self, parent_inode_number, leaf_name),
            success_do_nothing(),
            error(code, message) { return Error(Ext2fsCode, message, code); }
        );
        return Success(Ext2fsCode, EXT2FS_OK);
    }

    for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS; i++)
    {
        if (target_inode->block[i] != 0)
        {
            match_result(
                Ext2fsVolume__free_block(self, target_inode->block[i]),
                success_do_nothing(),
                error(code, message) { return Error(Ext2fsCode, message, code); }
            );
        }
    }

    memset(target_inode, 0, sizeof(*target_inode));
    match_result(
        Ext2fsVolume__write_inode(self, target_inode_number, target_inode),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    match_result(
        Ext2fsVolume__free_inode(self, target_inode_number),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    match_result(
        Ext2fsVolume__remove_dirent(self, parent_inode_number, leaf_name),
        success_do_nothing(),
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );
    return Success(Ext2fsCode, EXT2FS_OK);
}

static Result(Ext2fsCode) Ext2fsVolume__remove_at_path(Ext2fsVolumePtr self, ConstBStringPtr path, Flags flags)
{
    Ext2fsResolvedParent parent;
    match_result(
        Ext2fsVolume__resolve_parent(self, path),
        success(resolved) { parent = resolved; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    Ext2fsInode parent_inode;
    Ext2fsInode target_inode;
    match_result(
        Ext2fsVolume__read_inode(self, parent.parent_inode_number),
        success(inode) { parent_inode = inode; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    uint32_t target_inode_number = 0;
    match_result(
        Ext2fs__find_child_inode(self, &parent_inode, &parent.leaf_name),
        success(inode_number) { target_inode_number = inode_number; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    match_result(
        Ext2fsVolume__read_inode(self, target_inode_number),
        success(inode) { target_inode = inode; },
        error(code, message) { return Error(Ext2fsCode, message, code); }
    );

    return Ext2fsVolume__remove_inode(self, parent.parent_inode_number, &parent.leaf_name, target_inode_number,
                                      &target_inode, flags);
}

static ServiceCode Ext2fs__handle_open(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    match_result(
        Ext2fsVolume__inode_for_path(volume, &request->path),
        success(inode)
        {
            if (!Ext2fsInode__is_file(&inode))
            {
                return service_filesystem_complete_open_without_blocks(request->request_id,
                                                                       SERVICE_FILESYSTEM_REQUEST_FAILED, 0, 0, 0);
            }

            return service_filesystem_complete_open(request->request_id, SERVICE_OK, 0, inode.mode, inode.size,
                                                    inode.block);
        },
        error(code, message)
        {
            if ((request->flags & IO_OPEN_WRITE) != 0 && (request->flags & IO_OPEN_READ) == 0)
            {
                uint32_t blocks[12] = {0};
                return service_filesystem_complete_open(request->request_id, SERVICE_OK, 0, EXT2FS_MODE_FILE | 0644, 0,
                                                        blocks);
            }

            return service_filesystem_complete_open_without_blocks(request->request_id,
                                                                   SERVICE_FILESYSTEM_REQUEST_FAILED, 0, 0, 0);
        }
    );
}

static ServiceCode Ext2fs__handle_read(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    if (request->block == 0 || request->block_offset >= EXT2FS_BLOCK_SIZE ||
        request->size > EXT2FS_BLOCK_SIZE - request->block_offset)
    {
        return service_filesystem_complete(request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
    }

    Ext2fsBlockData block;
    match_result(
        Ext2fs__read_block(volume, (uint32_t)request->block),
        success(block_data) { block = block_data; },
        error(code, message)
        {
            return service_filesystem_complete(request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
        }
    );

    memcpy((void *)request->buffer, &block.data[request->block_offset], request->size);
    return service_filesystem_complete(request->request_id, SERVICE_OK, (int64_t)request->size);
}

static ServiceCode Ext2fs__handle_write(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    match_result(
        Ext2fsVolume__write_file_at_path(volume, &request->path, (uint8_t *)request->buffer, request->size),
        success(result)
        {
            return service_filesystem_complete_open(request->request_id, SERVICE_OK, (int64_t)request->size,
                                                    result.inode.mode, result.inode.size, result.inode.block);
        },
        error(code, message)
        {
            return service_filesystem_complete_open_without_blocks(request->request_id,
                                                                   SERVICE_FILESYSTEM_REQUEST_FAILED, 0, 0, 0);
        }
    );
}

static ServiceCode Ext2fs__handle_mkdir(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    match_result(
        Ext2fsVolume__mkdir_at_path(volume, &request->path),
        success_do_nothing(),
        error(code, message)
        {
            return service_filesystem_complete(request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
        }
    );

    return service_filesystem_complete(request->request_id, SERVICE_OK, IO_OK);
}

static ServiceCode Ext2fs__handle_remove(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    match_result(
        Ext2fsVolume__remove_at_path(volume, &request->path, request->flags),
        success_do_nothing(),
        error(code, message)
        {
            return service_filesystem_complete(request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
        }
    );

    return service_filesystem_complete(request->request_id, SERVICE_OK, IO_OK);
}

static void Ext2fs__handle_request(Ext2fsVolumePtr volume, uint64_t request_id)
{
    ServiceFilesystemRequest request;
    ServiceCode request_status = service_filesystem_get_request(request_id, &request);
    if (!service_ok(request_status))
    {
        DRIVER_DEBUG("ext2fs: request %i fetch failed %i\n", (int)request_id, (int)request_status);
        service_filesystem_complete(request_id, request_status, 0);
        return;
    }

    if (request.operation == SERVICE_FILESYSTEM_OPEN)
    {
        Ext2fs__handle_open(volume, &request);
        return;
    }

    if (request.operation == SERVICE_FILESYSTEM_READ)
    {
        Ext2fs__handle_read(volume, &request);
        return;
    }

    if (request.operation == SERVICE_FILESYSTEM_WRITE)
    {
        Ext2fs__handle_write(volume, &request);
        return;
    }

    if (request.operation == SERVICE_FILESYSTEM_MKDIR)
    {
        Ext2fs__handle_mkdir(volume, &request);
        return;
    }

    if (request.operation == SERVICE_FILESYSTEM_REMOVE)
    {
        Ext2fs__handle_remove(volume, &request);
        return;
    }

    if (request.operation == SERVICE_FILESYSTEM_CLOSE)
    {
        service_filesystem_complete(request.request_id, SERVICE_OK, 0);
        return;
    }

    if (request.operation != SERVICE_FILESYSTEM_LIST_DIR)
    {
        service_filesystem_complete(request.request_id, SERVICE_OPERATION_NOT_SUPPORTED, 0);
        return;
    }

    DirectoryEntryCount count =
        Ext2fsVolume__list_dir(volume, &request.path, (DirectoryEntryPtr)request.entries, request.max_entries);
    if (count < 0)
    {
        service_filesystem_complete(request.request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
        return;
    }

    service_filesystem_complete(request.request_id, SERVICE_OK, count);
}

static Result(AsyncIOCode) Ext2fs__register_service(Anything service_context)
{
    Ext2fsServiceContextPtr context = (Ext2fsServiceContextPtr)service_context;

    DRIVER_DEBUG("ext2fs: starting device=%B mountpoint=%B\n", &context->volume.device_path, &context->mountpoint);

    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("ext2fs: service registration failed\n");
        return Error(AsyncIOCode, ext2fs_io_error, ASYNC_IO_REGISTER_FAILED);
    }

    match_result(
        Ext2fsVolume__mount(&context->volume),
        success_do_nothing(),
        error(code, message)
        {
            DRIVER_DEBUG("ext2fs: mount failed device=%B mountpoint=%B\n", &context->volume.device_path,
                         &context->mountpoint);
            return Error(AsyncIOCode, message, ASYNC_IO_REGISTER_FAILED);
        }
    );

    context->volume.use_async_io = true;

    DRIVER_DEBUG("ext2fs: mounted device=%B mountpoint=%B\n", &context->volume.device_path, &context->mountpoint);

    ServiceCode register_status =
        service_register_filesystem(&context->volume.device_path, &context->filesystem_type, &context->mountpoint);
    if (!service_ok(register_status))
    {
        DRIVER_DEBUG("ext2fs: filesystem registration failed device=%B mountpoint=%B status=%i\n",
                     &context->volume.device_path, &context->mountpoint, (int)register_status);
        return Error(AsyncIOCode, ext2fs_io_error, ASYNC_IO_REGISTER_FAILED);
    }

    DRIVER_DEBUG("ext2fs: registered device=%B mountpoint=%B\n", &context->volume.device_path, &context->mountpoint);

    ServiceCode ready_status = service_publish_ready(&context->mountpoint);
    if (!service_ok(ready_status))
    {
        DRIVER_DEBUG("ext2fs: ready publish failed mountpoint=%B status=%i\n", &context->mountpoint, (int)ready_status);
        return Error(AsyncIOCode, ext2fs_io_error, ASYNC_IO_REGISTER_FAILED);
    }

    DRIVER_DEBUG("ext2fs: ready mountpoint=%B\n", &context->mountpoint);
    return Success(AsyncIOCode, ASYNC_IO_OK);
}

static Result(FiberCode) Ext2fs__request_service(FiberPtr self, Anything service_context, ServiceEvent request)
{
    Ext2fsServiceContextPtr context = (Ext2fsServiceContextPtr)service_context;
    if (request.type == SERVICE_EVENT_FILESYSTEM_IO)
    {
        Ext2fs__handle_request(&context->volume, request.source);
    }

    return Success(FiberCode, FIBER_OK);
}

int EXT2FS_ENTRYPOINT(int argc, char ** argv)
{
    if (argc != EXT2FS_ARG_COUNT)
    {
        DRIVER_DEBUG("ext2fs: usage <device> <mountpoint>\n");
        return 1;
    }

    BString device_path = BString__new(argv[1]);
    BString mountpoint = BString__new(argv[2]);
    Ext2fsServiceContext context = Ext2fsServiceContext__new(&device_path, &mountpoint);
    AsyncIO async_io = AsyncIO__new((Anything)&context, Ext2fs__request_service);

    match_result(
        AsyncIO__start(&async_io, Ext2fs__register_service, (Anything)&context, Ext2fs__request_service),
        success(code) { return code == ASYNC_IO_WAIT_COMPLETE ? 0 : 1; },
        error(code, message)
        {
            DRIVER_DEBUG("ext2fs: async io failed error=%B code=%i\n", &message, (int)code);
            return 1;
        }
    );

    return 1;
}
