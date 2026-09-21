#define DRIVER_LOG_TAG "EXT2FS"

#include <string.h>

#include "sys/debug.h"
#include "sys/service.h"
#include "channel/block_client.h"
#include "io/filesystem_driver.h"
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
    };
}

static BlockClient ext2fs_block_protocol;

static ServiceCode Ext2fs__connect_block_protocol(ConstBStringPtr device_path)
{
    match_result(
        BlockClient__open(device_path),
        success(channel)
        {
            ext2fs_block_protocol = channel;
            return SERVICE_OK;
        },
        error(code, message)
        {
            return (ServiceCode)code;
        }
    );
}

static BString ext2fs_io_error = bstr("EXT2FS IO error");
static BString ext2fs_invalid_path = bstr("EXT2FS invalid path");
static BString ext2fs_not_found = bstr("EXT2FS not found");
static BString ext2fs_not_a_directory = bstr("EXT2FS not a directory");
static BString ext2fs_not_a_file = bstr("EXT2FS not a file");
static BString ext2fs_already_exists = bstr("EXT2FS already exists");
static BString ext2fs_no_space = bstr("EXT2FS no space");
static BString ext2fs_unsupported = bstr("EXT2FS unsupported filesystem");

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
    match_result(
        BlockClient__read(&ext2fs_block_protocol, (uint64_t)block * EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
                          EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, (Address)&target.data, EXT2FS_BLOCK_SIZE),
        success(result)
        {
            if (result != EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK)
            {
                return Error(Ext2fsBlockData, ext2fs_io_error, EXT2FS_IO_ERROR);
            }
        },
        error(code, message)
        {
            return Error(Ext2fsBlockData, message, code);
        }
    );

    return Success(Ext2fsBlockData, target);
}

static Result(Ext2fsCode) Ext2fs__write_block(Ext2fsVolumePtr self, uint32_t block, Ext2fsBlockPtr source)
{
    match_result(
        BlockClient__write(&ext2fs_block_protocol, (uint64_t)block * EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
                           EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, (Address)source, EXT2FS_BLOCK_SIZE),
        success(result)
        {
            if (result != EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK)
            {
                return Error(Ext2fsCode, ext2fs_io_error, EXT2FS_IO_ERROR);
            }
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

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
        success(data)
        {
            block = data;
        },
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
        success(data)
        {
            block = data;
        },
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
        success(block_data)
        {
            data = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsInode, message, code);
        }
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
            success(block_data)
            {
                block = block_data;
            },
            error(code, message)
            {
                return Error(uint32_t, message, code);
            }
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
        success(inode)
        {
            current = inode;
        },
        error(code, message)
        {
            return Error(Ext2fsInode, message, code);
        }
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
                            success(inode)
                            {
                                current = inode;
                            },
                            error(code, message)
                            {
                                return Error(Ext2fsInode, message, code);
                            }
                        );
                    },
                    error(code, message)
                    {
                        return Error(Ext2fsInode, message, code);
                    }
                );
            },
            none_do_nothing()
        );
    }

    return Success(Ext2fsInode, current);
}

static DirectoryEntryCount Ext2fsVolume__list_dir_from(Ext2fsVolumePtr self, ConstBStringPtr path,
                                                       DirectoryEntryPtr entries, size_t max_entries, uint64_t skip)
{
    Ext2fsInode directory;
    match_result(
        Ext2fsVolume__inode_for_path(self, path),
        success(inode)
        {
            directory = inode;
        },
        error(code, message)
        {
            return IO_INVALID_PATH;
        }
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
            success(block_data)
            {
                block = block_data;
            },
            error(code, message)
            {
                return IO_INVALID_PATH;
            }
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
                    if (skip > 0)
                    {
                        skip--;
                        offset += rec_len;
                        continue;
                    }
                    Ext2fsInode entry_inode;
                    match_result(
                        Ext2fsVolume__read_inode(self, inode_number),
                        success(inode)
                        {
                            entry_inode = inode;
                        },
                        error(code, message)
                        {
                            return IO_INVALID_PATH;
                        }
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
        some(segment)
        {
            return segment;
        },
        none()
        {
            return empty_bstr();
        }
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
        success(inode)
        {
            current = inode;
        },
        error(code, message)
        {
            return Error(Ext2fsResolvedParent, message, code);
        }
    );

    uint32_t current_inode = EXT2FS_ROOT_INODE;
    for (uint64_t i = 0; i + 1 < BStringArray__len(&segments); i++)
    {
        BString segment = Ext2fs__segment_at(&segments, i);
        match_result(
            Ext2fs__find_child_inode(self, &current, &segment),
            success(inode_number)
            {
                current_inode = inode_number;
            },
            error(code, message)
            {
                return Error(Ext2fsResolvedParent, message, code);
            }
        );

        match_result(
            Ext2fsVolume__read_inode(self, current_inode),
            success(inode)
            {
                current = inode;
            },
            error(code, message)
            {
                return Error(Ext2fsResolvedParent, message, code);
            }
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
        success(block_data)
        {
            block = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
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
        success(block_data)
        {
            superblock = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    match_result(
        Ext2fs__read_block(self, 2),
        success(block_data)
        {
            group_desc = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
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
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
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
        success(inode)
        {
            directory_inode = inode;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
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
            success(block_data)
            {
                block = block_data;
            },
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
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
        success(inode)
        {
            directory_inode = inode;
        },
        error(code, message)
        {
            return Error(uint32_t, message, code);
        }
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
            success(block_data)
            {
                block = block_data;
            },
            error(code, message)
            {
                return Error(uint32_t, message, code);
            }
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
                        success(code)
                        {
                            return Success(uint32_t, entry_inode);
                        },
                        error(code, message)
                        {
                            return Error(uint32_t, message, code);
                        }
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
            success(block_data)
            {
                inode_bitmap = block_data;
            },
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
        );
        Ext2fs__bitmap_set_bit(inode_bitmap.data, inode_number - 1);
        free_inode_delta--;
    }

    match_result(
        Ext2fs__read_block(self, self->block_bitmap_block),
        success(block_data)
        {
            block_bitmap = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
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
            success(block_number)
            {
                inode->block[i] = block_number;
            },
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
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
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
        );
    }

    inode->size = (uint32_t)size;
    match_result(
        Ext2fsVolume__write_inode(self, inode_number, inode),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    if (creating)
    {
        match_result(
            Ext2fs__write_block(self, self->inode_bitmap_block, &inode_bitmap.data),
            success_do_nothing(),
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
        );
    }

    match_result(
        Ext2fs__write_block(self, self->block_bitmap_block, &block_bitmap.data),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
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
        success(resolved)
        {
            parent = resolved;
        },
        error(code, message)
        {
            return Error(Ext2fsWriteFileResult, message, code);
        }
    );

    Ext2fsInode parent_inode;
    match_result(
        Ext2fsVolume__read_inode(self, parent.parent_inode_number),
        success(inode)
        {
            parent_inode = inode;
        },
        error(code, message)
        {
            return Error(Ext2fsWriteFileResult, message, code);
        }
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
                success(existing_inode)
                {
                    inode = existing_inode;
                },
                error(code, message)
                {
                    return Error(Ext2fsWriteFileResult, message, code);
                }
            );

            if (!Ext2fsInode__is_file(&inode))
            {
                return Error(Ext2fsWriteFileResult, ext2fs_not_a_file, EXT2FS_NOT_A_FILE);
            }

            match_result(
                Ext2fsVolume__write_inode_data(self, existing_inode_number, &inode, data, size, false),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(Ext2fsWriteFileResult, message, code);
                }
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
        success(block_data)
        {
            inode_bitmap = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsWriteFileResult, message, code);
        }
    );

    uint32_t new_inode_number = 0;
    match_result(
        Ext2fsVolume__allocate_inode_from_bitmap(self, inode_bitmap.data),
        success(inode_number)
        {
            new_inode_number = inode_number;
        },
        error(code, message)
        {
            return Error(Ext2fsWriteFileResult, message, code);
        }
    );

    Ext2fsInode inode;
    Ext2fsInode__reset_file(&inode, (uint32_t)size);
    match_result(
        Ext2fsVolume__write_inode_data(self, new_inode_number, &inode, data, size, true),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsWriteFileResult, message, code);
        }
    );

    match_result(
        Ext2fsVolume__insert_dirent(self, parent.parent_inode_number, &parent.leaf_name, new_inode_number,
                                    EXT2FS_DIRENT_TYPE_FILE),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsWriteFileResult, message, code);
        }
    );

    return Success(Ext2fsWriteFileResult, Ext2fsWriteFileResult__new(new_inode_number, &inode));
}

static Result(Ext2fsCode) Ext2fsVolume__mkdir_at_path(Ext2fsVolumePtr self, ConstBStringPtr path)
{
    Ext2fsResolvedParent parent;
    match_result(
        Ext2fsVolume__resolve_parent(self, path),
        success(resolved)
        {
            parent = resolved;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    Ext2fsInode parent_inode;
    match_result(
        Ext2fsVolume__read_inode(self, parent.parent_inode_number),
        success(inode)
        {
            parent_inode = inode;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    if (!Ext2fsInode__is_dir(&parent_inode))
    {
        return Error(Ext2fsCode, ext2fs_not_a_directory, EXT2FS_NOT_A_DIRECTORY);
    }

    match_result(
        Ext2fs__find_child_inode(self, &parent_inode, &parent.leaf_name),
        success(existing_inode_number)
        {
            return Error(Ext2fsCode, ext2fs_already_exists, EXT2FS_ALREADY_EXISTS);
        },
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
        success(block_data)
        {
            inode_bitmap = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    match_result(
        Ext2fs__read_block(self, self->block_bitmap_block),
        success(block_data)
        {
            block_bitmap = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    uint32_t new_inode_number = 0;
    uint32_t new_dir_block = 0;
    match_result(
        Ext2fsVolume__allocate_inode_from_bitmap(self, inode_bitmap.data),
        success(inode_number)
        {
            new_inode_number = inode_number;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    match_result(
        Ext2fsVolume__allocate_block_from_bitmap(self, block_bitmap.data),
        success(block_number)
        {
            new_dir_block = block_number;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    Ext2fsInode new_inode;
    Ext2fsInode__reset_dir(&new_inode, new_dir_block);
    Ext2fs__initialise_dir_block(dir_block, new_inode_number, parent.parent_inode_number);

    match_result(
        Ext2fs__write_block(self, new_dir_block, &dir_block),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    match_result(
        Ext2fsVolume__write_inode(self, new_inode_number, &new_inode),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    match_result(
        Ext2fs__write_block(self, self->inode_bitmap_block, &inode_bitmap.data),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    match_result(
        Ext2fs__write_block(self, self->block_bitmap_block, &block_bitmap.data),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    match_result(
        Ext2fsVolume__insert_dirent(self, parent.parent_inode_number, &parent.leaf_name, new_inode_number,
                                    EXT2FS_DIRENT_TYPE_DIR),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
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
        success(block_data)
        {
            inode_bitmap = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    Ext2fs__bitmap_clear_bit(inode_bitmap.data, inode_number - 1);
    match_result(
        Ext2fs__write_block(self, self->inode_bitmap_block, &inode_bitmap.data),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
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
        success(block_data)
        {
            block_bitmap = block_data;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    Ext2fs__bitmap_clear_bit(block_bitmap.data, block_number - self->first_data_block);
    match_result(
        Ext2fs__write_block(self, self->block_bitmap_block, &block_bitmap.data),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    return Ext2fsVolume__update_allocation_counts(self, 1, 0, 0);
}

static Result(Ext2fsFirstChild) Ext2fsVolume__first_child(Ext2fsVolumePtr self, uint32_t directory_inode_number)
{
    Ext2fsInode directory_inode;
    match_result(
        Ext2fsVolume__read_inode(self, directory_inode_number),
        success(inode)
        {
            directory_inode = inode;
        },
        error(code, message)
        {
            return Error(Ext2fsFirstChild, message, code);
        }
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
            success(block_data)
            {
                block = block_data;
            },
            error(code, message)
            {
                return Error(Ext2fsFirstChild, message, code);
            }
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
                        error(code, message)
                        {
                            return Error(Ext2fsFirstChild, message, code);
                        }
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
                error(code, message)
                {
                    return Error(Ext2fsCode, message, code);
                }
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
                    error(code, message)
                    {
                        return Error(Ext2fsCode, message, code);
                    }
                );
            }
        }

        memset(target_inode, 0, sizeof(*target_inode));
        match_result(
            Ext2fsVolume__write_inode(self, target_inode_number, target_inode),
            success_do_nothing(),
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
        );
        match_result(
            Ext2fsVolume__free_inode(self, target_inode_number),
            success_do_nothing(),
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
        );
        match_result(
            Ext2fsVolume__update_allocation_counts(self, 0, 0, -1),
            success_do_nothing(),
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
        );
        match_result(
            Ext2fsVolume__remove_dirent(self, parent_inode_number, leaf_name),
            success_do_nothing(),
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
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
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
        );
        match_result(
            Ext2fsVolume__remove_dirent(self, parent_inode_number, leaf_name),
            success_do_nothing(),
            error(code, message)
            {
                return Error(Ext2fsCode, message, code);
            }
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
                error(code, message)
                {
                    return Error(Ext2fsCode, message, code);
                }
            );
        }
    }

    memset(target_inode, 0, sizeof(*target_inode));
    match_result(
        Ext2fsVolume__write_inode(self, target_inode_number, target_inode),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    match_result(
        Ext2fsVolume__free_inode(self, target_inode_number),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    match_result(
        Ext2fsVolume__remove_dirent(self, parent_inode_number, leaf_name),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );
    return Success(Ext2fsCode, EXT2FS_OK);
}

static Result(Ext2fsCode) Ext2fsVolume__remove_at_path(Ext2fsVolumePtr self, ConstBStringPtr path, Flags flags)
{
    Ext2fsResolvedParent parent;
    match_result(
        Ext2fsVolume__resolve_parent(self, path),
        success(resolved)
        {
            parent = resolved;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    Ext2fsInode parent_inode;
    Ext2fsInode target_inode;
    match_result(
        Ext2fsVolume__read_inode(self, parent.parent_inode_number),
        success(inode)
        {
            parent_inode = inode;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    uint32_t target_inode_number = 0;
    match_result(
        Ext2fs__find_child_inode(self, &parent_inode, &parent.leaf_name),
        success(inode_number)
        {
            target_inode_number = inode_number;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    match_result(
        Ext2fsVolume__read_inode(self, target_inode_number),
        success(inode)
        {
            target_inode = inode;
        },
        error(code, message)
        {
            return Error(Ext2fsCode, message, code);
        }
    );

    return Ext2fsVolume__remove_inode(self, parent.parent_inode_number, &parent.leaf_name, target_inode_number,
                                      &target_inode, flags);
}

static Result(FilesystemFileSize)
    Ext2fs__load(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data)
{
    Ext2fsVolumePtr volume = context;
    Ext2fsInode inode;
    match_result(
        Ext2fsVolume__inode_for_path(volume, path),
        success(value)
        {
            inode = value;
        },
        error(code, message)
        {
            return Error(FilesystemFileSize, message, IO_INVALID_PATH);
        }
    );
    if (!Ext2fsInode__is_file(&inode) || inode.size > FILESYSTEM_FILE_CAPACITY)
    {
        return Error(FilesystemFileSize, ext2fs_io_error, IO_OPERATION_NOT_SUPPORTED);
    }
    for (uint64_t offset = 0; offset < inode.size; offset += EXT2FS_BLOCK_SIZE)
    {
        uint64_t count = inode.size - offset;
        if (count > EXT2FS_BLOCK_SIZE)
        {
            count = EXT2FS_BLOCK_SIZE;
        }
        uint32_t number = inode.block[offset / EXT2FS_BLOCK_SIZE];
        if (number == 0)
        {
            memset(data + offset, 0, count);
            continue;
        }
        match_result(
            Ext2fs__read_block(volume, number),
            success(block)
            {
                memcpy(data + offset, block.data, count);
            },
            error(code, message)
            {
                return Error(FilesystemFileSize, message, IO_INVALID_PATH);
            }
        );
    }
    return Success(FilesystemFileSize, inode.size);
}

static IOStatus Ext2fs__store(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data,
                              FilesystemFileSize size)
{
    Ext2fsVolumePtr volume = context;
    match_result(
        Ext2fsVolume__write_file_at_path(volume, path, data, size),
        success(value)
        {
            return IO_OK;
        },
        error(code, message)
        {
            return IO_INVALID_PATH;
        }
    );
}

static Result(OptionalDirectoryEntry) Ext2fs__list(IOServiceContext context, ConstBStringPtr path, uint64_t index)
{
    DirectoryEntry entry;
    DirectoryEntryCount count = Ext2fsVolume__list_dir_from(context, path, &entry, 1, index);
    if (count < 0)
    {
        return Error(OptionalDirectoryEntry, bstr("Directory lookup failed"), count);
    }
    return Success(OptionalDirectoryEntry, count == 0 ? None(DirectoryEntry) : Some(DirectoryEntry, entry));
}

static IOStatus Ext2fs__mkdir(IOServiceContext context, ConstBStringPtr path)
{
    Ext2fsVolumePtr volume = context;
    match_result(
        Ext2fsVolume__mkdir_at_path(volume, path),
        success(value)
        {
            return IO_OK;
        },
        error(code, message)
        {
            return IO_INVALID_PATH;
        }
    );
}

static IOStatus Ext2fs__remove(IOServiceContext context, ConstBStringPtr path, Flags flags)
{
    Ext2fsVolumePtr volume = context;
    match_result(
        Ext2fsVolume__remove_at_path(volume, path, flags),
        success(value)
        {
            return IO_OK;
        },
        error(code, message)
        {
            return IO_INVALID_PATH;
        }
    );
}

static FilesystemOperations Ext2fs__operations(void)
{
    return FilesystemOperations__new(Ext2fs__load, Ext2fs__store, Ext2fs__list, Ext2fs__mkdir, Ext2fs__remove,
                                     EXT2FS_MAX_READ_SIZE);
}

int EXT2FS_ENTRYPOINT(int argc, char ** argv)
{
    if (argc != EXT2FS_ARG_COUNT || !service_ok(service_become_service()))
    {
        return 1;
    }
    BString device = BString__new(argv[1]);
    BString mountpoint = BString__new(argv[2]);
    static Ext2fsVolume volume;
    volume = Ext2fsVolume__new(&device);
    if (!service_ok(Ext2fs__connect_block_protocol(&device)))
    {
        return 1;
    }
    match_result(
        Ext2fsVolume__mount(&volume),
        success_do_nothing(),
        error(code, message)
        {
            DRIVER_DEBUG("ext2fs: mount failed: %B (%i)\n", &message, (int)code);
            BlockClient__close(&ext2fs_block_protocol);
            return 1;
        }
    );
    static FilesystemDriver driver;
    FilesystemOperations operations = Ext2fs__operations();
    driver = FilesystemDriver__new(&volume, &operations);
    int result = FilesystemDriver__run(&driver, &mountpoint);
    BlockClient__close(&ext2fs_block_protocol);
    return result;
}
