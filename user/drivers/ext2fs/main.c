#define DRIVER_LOG_TAG "EXT2FS"

#include <string.h>

#include "../../sys/debug.h"
#include "../../sys/service.h"
#include "../../types/fixed_array.h"
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

typedef struct Ext2fsInode
{
    FileMode mode;
    uint32_t size;
    uint16_t links_count;
    uint32_t block[15];
}
Ext2fsInode;

typedef Ext2fsInode * Ext2fsInodePtr;

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
}
Ext2fsVolume;

typedef Ext2fsVolume * Ext2fsVolumePtr;

static Ext2fsVolume Ext2fsVolume__new(BString device_path)
{
    return (Ext2fsVolume) {
        .device_path = device_path,
    };
}

static uint16_t Ext2fs__read_u16(uint8_t * data, size_t offset)
{
    return (uint16_t) data[offset] | ((uint16_t) data[offset + 1] << 8);
}

static uint32_t Ext2fs__read_u32(uint8_t * data, size_t offset)
{
    return (uint32_t) data[offset]
        | ((uint32_t) data[offset + 1] << 8)
        | ((uint32_t) data[offset + 2] << 16)
        | ((uint32_t) data[offset + 3] << 24);
}

static void Ext2fs__write_u16(uint8_t * data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t) (value & 0xff);
    data[offset + 1] = (uint8_t) ((value >> 8) & 0xff);
}

static void Ext2fs__write_u32(uint8_t * data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t) (value & 0xff);
    data[offset + 1] = (uint8_t) ((value >> 8) & 0xff);
    data[offset + 2] = (uint8_t) ((value >> 16) & 0xff);
    data[offset + 3] = (uint8_t) ((value >> 24) & 0xff);
}

static bool Ext2fs__read_block(Ext2fsVolumePtr self, uint32_t block, Ext2fsBlockPtr target)
{
    ServiceBlockReadResult result = service_block_read_device(
        &self->device_path,
        (uint64_t) block * EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
        EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
        (Address) target,
        EXT2FS_BLOCK_SIZE
    );
    return result == EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK;
}

static bool Ext2fs__write_block(Ext2fsVolumePtr self, uint32_t block, Ext2fsBlockPtr source)
{
    ServiceBlockReadResult result = service_block_write_device(
        &self->device_path,
        (uint64_t) block * EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
        EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
        (Address) source,
        EXT2FS_BLOCK_SIZE
    );
    return result == EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK;
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

static bool Ext2fsVolume__mount(Ext2fsVolumePtr self)
{
    Ext2fsBlock block;
    if (!Ext2fs__read_block(self, EXT2FS_SUPERBLOCK_BLOCK, &block))
    {
        DRIVER_DEBUG("ext2fs: failed to read superblock\n");
        return false;
    }

    if (Ext2fs__read_u16(block, 56) != EXT2FS_SUPER_MAGIC)
    {
        DRIVER_DEBUG("ext2fs: invalid magic\n");
        return false;
    }

    uint32_t log_block_size = Ext2fs__read_u32(block, 24);
    if (log_block_size != 0)
    {
        DRIVER_DEBUG("ext2fs: only 1024 byte blocks are supported\n");
        return false;
    }

    self->blocks_count = Ext2fs__read_u32(block, 4);
    self->first_data_block = Ext2fs__read_u32(block, 20);
    self->blocks_per_group = Ext2fs__read_u32(block, 32);
    self->inodes_per_group = Ext2fs__read_u32(block, 40);
    self->inode_size = Ext2fs__read_u16(block, 88);
    if (self->inode_size == 0)
    {
        self->inode_size = 128;
    }

    self->group_count = (self->blocks_count + self->blocks_per_group - 1) / self->blocks_per_group;
    if (self->group_count > EXT2FS_MAX_GROUPS)
    {
        DRIVER_DEBUG("ext2fs: too many groups\n");
        return false;
    }

    if (!Ext2fs__read_block(self, 2, &block))
    {
        DRIVER_DEBUG("ext2fs: failed to read group descriptors\n");
        return false;
    }

    for (uint32_t group = 0; group < self->group_count; group++)
    {
        self->group_block_bitmap_block[group] = Ext2fs__read_u32(block, group * 32U);
        self->group_inode_bitmap_block[group] = Ext2fs__read_u32(block, group * 32U + 4U);
        self->inode_table_block[group] = Ext2fs__read_u32(block, group * 32U + 8U);
    }

    self->block_bitmap_block = self->group_block_bitmap_block[0];
    self->inode_bitmap_block = self->group_inode_bitmap_block[0];
    return true;
}

static bool Ext2fsVolume__read_inode(Ext2fsVolumePtr self, uint32_t inode_number, Ext2fsInodePtr target)
{
    if (inode_number == 0)
    {
        return false;
    }

    uint32_t block = Ext2fsVolume__inode_block(self, inode_number);
    size_t offset = Ext2fsVolume__inode_offset(self, inode_number);
    if (block == 0)
    {
        return false;
    }

    Ext2fsBlock data;
    if (!Ext2fs__read_block(self, block, &data))
    {
        return false;
    }

    target->mode = Ext2fs__read_u16(data, offset);
    target->size = Ext2fs__read_u32(data, offset + 4);
    target->links_count = Ext2fs__read_u16(data, offset + 26);
    if (target->links_count == 0)
    {
        target->links_count = 1;
    }
    for (uint32_t i = 0; i < 15; i++)
    {
        target->block[i] = Ext2fs__read_u32(data, offset + 40 + (i * 4));
    }

    return true;
}

static DirectoryEntry Ext2fs__directory_entry(BString name, uint8_t file_type, Ext2fsInodePtr inode)
{
    FileMode mode = inode->mode;
    if ((mode & EXT2FS_MODE_TYPE_MASK) == 0)
    {
        mode |= file_type == EXT2FS_DIRENT_TYPE_DIR ? IO_MODE_TYPE_DIR : IO_MODE_TYPE_FILE;
    }

    return DirectoryEntry__new(IO_DIRENT_ATTR_NONE, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT, mode, &name);
}

static bool Ext2fs__find_child_inode(
    Ext2fsVolumePtr self, Ext2fsInodePtr directory, ConstBStringPtr name, uint32_t * inode_number
)
{
    Ext2fsBlock block;
    for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS; i++)
    {
        if (directory->block[i] == 0)
        {
            continue;
        }

        if (!Ext2fs__read_block(self, directory->block[i], &block))
        {
            return false;
        }

        size_t offset = 0;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE)
        {
            uint32_t candidate_inode = Ext2fs__read_u32(block, offset);
            uint16_t rec_len = Ext2fs__read_u16(block, offset + 4);
            uint8_t name_len = block[offset + 6];

            if (rec_len == 0)
            {
                break;
            }

            if (candidate_inode != 0 && name_len > 0)
            {
                BString candidate = BString__from_chars((const char *) &block[offset + 8], name_len);
                if (BString__equals(&candidate, name))
                {
                    *inode_number = candidate_inode;
                    return true;
                }
            }

            offset += rec_len;
        }
    }

    return false;
}

static bool Ext2fsVolume__inode_for_path(Ext2fsVolumePtr self, ConstBStringPtr path, Ext2fsInodePtr target)
{
    Ext2fsInode current;
    if (!Ext2fsVolume__read_inode(self, EXT2FS_ROOT_INODE, &current))
    {
        return false;
    }

    BStringArray segments = BString__split(path, '/');
    for (uint64_t i = 0; i < BStringArray__len(&segments); i++)
    {
        match_optional(BString, BStringArray__at(&segments, i),
            some(segment)
            {
                if (BString__is_empty(&segment))
                {
                    continue;
                }

                uint32_t child_inode = 0;
                if (!Ext2fs__find_child_inode(self, &current, &segment, &child_inode))
                {
                    return false;
                }

                if (!Ext2fsVolume__read_inode(self, child_inode, &current))
                {
                    return false;
                }
            },
            none_do_nothing()
        );
    }

    *target = current;
    return true;
}

static DirectoryEntryCount Ext2fsVolume__list_dir(
    Ext2fsVolumePtr self, ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries
)
{
    Ext2fsInode directory;
    if (!Ext2fsVolume__inode_for_path(self, path, &directory))
    {
        return IO_INVALID_PATH;
    }

    if ((directory.mode & EXT2FS_MODE_TYPE_MASK) != EXT2FS_MODE_DIR)
    {
        return IO_INVALID_PATH;
    }

    DirectoryEntryCount count = 0;
    Ext2fsBlock block;
    for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS && (size_t) count < max_entries; i++)
    {
        if (directory.block[i] == 0)
        {
            continue;
        }

        if (!Ext2fs__read_block(self, directory.block[i], &block))
        {
            return IO_INVALID_PATH;
        }

        size_t offset = 0;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE && (size_t) count < max_entries)
        {
            uint32_t inode_number = Ext2fs__read_u32(block, offset);
            uint16_t rec_len = Ext2fs__read_u16(block, offset + 4);
            uint8_t name_len = block[offset + 6];
            uint8_t file_type = block[offset + 7];

            if (rec_len == 0)
            {
                break;
            }

            if (inode_number != 0 && name_len > 0)
            {
                BString name = BString__from_chars((const char *) &block[offset + 8], name_len);
                if (!BString__equals(&name, &bstr(".")) && !BString__equals(&name, &bstr("..")))
                {
                    Ext2fsInode entry_inode;
                    if (!Ext2fsVolume__read_inode(self, inode_number, &entry_inode))
                    {
                        return IO_INVALID_PATH;
                    }
                    entries[count] = Ext2fs__directory_entry(name, file_type, &entry_inode);
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
    return (self->mode & EXT2FS_MODE_TYPE_MASK) == EXT2FS_MODE_DIR
        || (self->mode & IO_MODE_TYPE_MASK) == IO_MODE_TYPE_DIR;
}

static bool Ext2fsInode__is_file(Ext2fsInodePtr self)
{
    return (self->mode & EXT2FS_MODE_TYPE_MASK) == EXT2FS_MODE_FILE
        || (self->mode & IO_MODE_TYPE_MASK) == IO_MODE_TYPE_FILE;
}

static BString Ext2fs__segment_at(BStringArrayPtr segments, size_t index)
{
    match_optional(BString, BStringArray__at(segments, index),
        some(segment) { return segment; },
        none() { return empty_bstr(); }
    );
}

static bool Ext2fsVolume__find_inode_at_path(
    Ext2fsVolumePtr self, ConstBStringPtr path, uint32_t * inode_number, Ext2fsInodePtr target
)
{
    if (BString__equals(path, &bstr("/")) || BString__length(path) == 0)
    {
        *inode_number = EXT2FS_ROOT_INODE;
        return Ext2fsVolume__read_inode(self, EXT2FS_ROOT_INODE, target);
    }

    Ext2fsInode current;
    if (!Ext2fsVolume__read_inode(self, EXT2FS_ROOT_INODE, &current))
    {
        return false;
    }

    uint32_t current_inode = EXT2FS_ROOT_INODE;
    BStringArray segments = BString__split(path, '/');
    for (uint64_t i = 0; i < BStringArray__len(&segments); i++)
    {
        BString segment = Ext2fs__segment_at(&segments, i);
        if (BString__is_empty(&segment))
        {
            continue;
        }

        if (!Ext2fs__find_child_inode(self, &current, &segment, &current_inode))
        {
            return false;
        }

        if (!Ext2fsVolume__read_inode(self, current_inode, &current))
        {
            return false;
        }
    }

    *inode_number = current_inode;
    *target = current;
    return true;
}

static bool Ext2fsVolume__resolve_parent(
    Ext2fsVolumePtr self, ConstBStringPtr path, uint32_t * parent_inode_number, BString * leaf_name
)
{
    if (BString__length(path) == 0 || BString__equals(path, &bstr("/")))
    {
        return false;
    }

    BStringArray segments = BString__split(path, '/');
    if (BStringArray__len(&segments) == 0)
    {
        return false;
    }

    Ext2fsInode current;
    if (!Ext2fsVolume__read_inode(self, EXT2FS_ROOT_INODE, &current))
    {
        return false;
    }

    uint32_t current_inode = EXT2FS_ROOT_INODE;
    for (uint64_t i = 0; i + 1 < BStringArray__len(&segments); i++)
    {
        BString segment = Ext2fs__segment_at(&segments, i);
        if (!Ext2fs__find_child_inode(self, &current, &segment, &current_inode))
        {
            return false;
        }

        if (!Ext2fsVolume__read_inode(self, current_inode, &current) || !Ext2fsInode__is_dir(&current))
        {
            return false;
        }
    }

    *parent_inode_number = current_inode;
    *leaf_name = Ext2fs__segment_at(&segments, BStringArray__len(&segments) - 1);
    return !BString__is_empty(leaf_name);
}

static bool Ext2fsVolume__write_inode(Ext2fsVolumePtr self, uint32_t inode_number, Ext2fsInodePtr source)
{
    uint32_t inode_block = Ext2fsVolume__inode_block(self, inode_number);
    size_t inode_offset = Ext2fsVolume__inode_offset(self, inode_number);
    if (inode_block == 0)
    {
        return false;
    }

    Ext2fsBlock block;
    if (!Ext2fs__read_block(self, inode_block, &block))
    {
        return false;
    }

    Ext2fs__write_u16(block, inode_offset, source->mode);
    Ext2fs__write_u32(block, inode_offset + 4, source->size);
    Ext2fs__write_u16(block, inode_offset + 26, source->links_count);
    for (uint32_t i = 0; i < 15; i++)
    {
        Ext2fs__write_u32(block, inode_offset + 40 + (i * 4), source->block[i]);
    }

    return Ext2fs__write_block(self, inode_block, &block);
}

static bool Ext2fs__bitmap_bit_is_set(uint8_t * bitmap, uint32_t index)
{
    return (bitmap[index / 8] & (1U << (index % 8))) != 0;
}

static void Ext2fs__bitmap_set_bit(uint8_t * bitmap, uint32_t index)
{
    bitmap[index / 8] = (uint8_t) (bitmap[index / 8] | (1U << (index % 8)));
}

static void Ext2fs__bitmap_clear_bit(uint8_t * bitmap, uint32_t index)
{
    bitmap[index / 8] = (uint8_t) (bitmap[index / 8] & ~(1U << (index % 8)));
}

static bool Ext2fsVolume__allocate_inode_from_bitmap(
    Ext2fsVolumePtr self, uint8_t * inode_bitmap, uint32_t * inode_number
)
{
    for (uint32_t inode_index = 0; inode_index < self->inodes_per_group; inode_index++)
    {
        if (!Ext2fs__bitmap_bit_is_set(inode_bitmap, inode_index))
        {
            Ext2fs__bitmap_set_bit(inode_bitmap, inode_index);
            *inode_number = inode_index + 1;
            return true;
        }
    }

    return false;
}

static bool Ext2fsVolume__allocate_block_from_bitmap(
    Ext2fsVolumePtr self, uint8_t * block_bitmap, uint32_t * block_number
)
{
    for (uint32_t block_index = 0; block_index < self->blocks_per_group; block_index++)
    {
        if (!Ext2fs__bitmap_bit_is_set(block_bitmap, block_index))
        {
            Ext2fs__bitmap_set_bit(block_bitmap, block_index);
            *block_number = self->first_data_block + block_index;
            return true;
        }
    }

    return false;
}

static uint16_t Ext2fs__dirent_record_size(size_t name_len)
{
    size_t padded = (name_len + 3U) & ~3U;
    return (uint16_t) (8U + padded);
}

static bool Ext2fsVolume__update_allocation_counts(
    Ext2fsVolumePtr self, int32_t block_delta, int32_t inode_delta, int32_t used_dirs_delta
)
{
    Ext2fsBlock superblock;
    Ext2fsBlock group_desc;
    if (!Ext2fs__read_block(self, EXT2FS_SUPERBLOCK_BLOCK, &superblock) || !Ext2fs__read_block(self, 2, &group_desc))
    {
        return false;
    }

    uint32_t free_blocks = Ext2fs__read_u32(superblock, 12);
    uint32_t free_inodes = Ext2fs__read_u32(superblock, 16);
    uint16_t group_free_blocks = Ext2fs__read_u16(group_desc, 12);
    uint16_t group_free_inodes = Ext2fs__read_u16(group_desc, 14);
    uint16_t used_dirs = Ext2fs__read_u16(group_desc, 16);

    Ext2fs__write_u32(superblock, 12, (uint32_t) ((int32_t) free_blocks + block_delta));
    Ext2fs__write_u32(superblock, 16, (uint32_t) ((int32_t) free_inodes + inode_delta));
    Ext2fs__write_u16(group_desc, 12, (uint16_t) ((int32_t) group_free_blocks + block_delta));
    Ext2fs__write_u16(group_desc, 14, (uint16_t) ((int32_t) group_free_inodes + inode_delta));
    Ext2fs__write_u16(group_desc, 16, (uint16_t) ((int32_t) used_dirs + used_dirs_delta));

    return Ext2fs__write_block(self, EXT2FS_SUPERBLOCK_BLOCK, &superblock) && Ext2fs__write_block(self, 2, &group_desc);
}

static bool Ext2fsVolume__insert_dirent(
    Ext2fsVolumePtr self, uint32_t directory_inode_number, BString name, uint32_t child_inode_number, uint8_t child_type
)
{
    Ext2fsInode directory_inode;
    if (!Ext2fsVolume__read_inode(self, directory_inode_number, &directory_inode) || !Ext2fsInode__is_dir(&directory_inode))
    {
        return false;
    }

    uint16_t required_len = Ext2fs__dirent_record_size(BString__length(&name));
    for (uint32_t block_index = 0; block_index < EXT2FS_NDIR_BLOCKS; block_index++)
    {
        if (directory_inode.block[block_index] == 0)
        {
            continue;
        }

        Ext2fsBlock block;
        if (!Ext2fs__read_block(self, directory_inode.block[block_index], &block))
        {
            return false;
        }

        size_t offset = 0;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE)
        {
            uint32_t entry_inode = Ext2fs__read_u32(block, offset);
            uint16_t rec_len = Ext2fs__read_u16(block, offset + 4);
            uint8_t name_len = block[offset + 6];
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
                    Ext2fs__write_u16(block, offset + 4, actual_len);
                    Ext2fs__write_u32(block, new_offset, child_inode_number);
                    Ext2fs__write_u16(block, new_offset + 4, rec_len - actual_len);
                    block[new_offset + 6] = (uint8_t) BString__length(&name);
                    block[new_offset + 7] = child_type;
                    for (StringPos i = 0; i < BString__length(&name); i++)
                    {
                        block[new_offset + 8 + i] = (uint8_t) BString__char_at_or(&name, i, '\0');
                    }
                    return Ext2fs__write_block(self, directory_inode.block[block_index], &block);
                }
            }

            offset += rec_len;
        }
    }

    return false;
}

static bool Ext2fsVolume__remove_dirent(
    Ext2fsVolumePtr self, uint32_t directory_inode_number, BString name, uint32_t * removed_inode_number
)
{
    Ext2fsInode directory_inode;
    if (!Ext2fsVolume__read_inode(self, directory_inode_number, &directory_inode) || !Ext2fsInode__is_dir(&directory_inode))
    {
        return false;
    }

    for (uint32_t block_index = 0; block_index < EXT2FS_NDIR_BLOCKS; block_index++)
    {
        if (directory_inode.block[block_index] == 0)
        {
            continue;
        }

        Ext2fsBlock block;
        if (!Ext2fs__read_block(self, directory_inode.block[block_index], &block))
        {
            return false;
        }

        size_t offset = 0;
        size_t previous_offset = 0;
        bool has_previous = false;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE)
        {
            uint32_t entry_inode = Ext2fs__read_u32(block, offset);
            uint16_t rec_len = Ext2fs__read_u16(block, offset + 4);
            uint8_t name_len = block[offset + 6];
            if (rec_len == 0)
            {
                break;
            }

            if (entry_inode != 0 && name_len > 0)
            {
                BString entry_name = BString__from_chars((const char *) &block[offset + 8], name_len);
                if (BString__equals(&entry_name, &name))
                {
                    *removed_inode_number = entry_inode;
                    if (has_previous)
                    {
                        uint16_t previous_len = Ext2fs__read_u16(block, previous_offset + 4);
                        Ext2fs__write_u16(block, previous_offset + 4, previous_len + rec_len);
                    }
                    else
                    {
                        Ext2fs__write_u32(block, offset, 0);
                    }
                    return Ext2fs__write_block(self, directory_inode.block[block_index], &block);
                }
            }

            previous_offset = offset;
            has_previous = true;
            offset += rec_len;
        }
    }

    return false;
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

static bool Ext2fsVolume__write_inode_data(
    Ext2fsVolumePtr self, uint32_t inode_number, Ext2fsInodePtr inode, uint8_t * data, size_t size, bool creating
)
{
    if (size > EXT2FS_MAX_READ_SIZE)
    {
        return false;
    }

    Ext2fsBlock inode_bitmap;
    Ext2fsBlock block_bitmap;
    int32_t free_block_delta = 0;
    int32_t free_inode_delta = 0;

    if (creating)
    {
        if (!Ext2fs__read_block(self, self->inode_bitmap_block, &inode_bitmap))
        {
            return false;
        }
        Ext2fs__bitmap_set_bit(inode_bitmap, inode_number - 1);
        free_inode_delta--;
    }

    if (!Ext2fs__read_block(self, self->block_bitmap_block, &block_bitmap))
    {
        return false;
    }

    for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS; i++)
    {
        if (inode->block[i] != 0)
        {
            Ext2fs__bitmap_clear_bit(block_bitmap, inode->block[i] - self->first_data_block);
            free_block_delta++;
            inode->block[i] = 0;
        }
    }

    size_t blocks_needed = (size + EXT2FS_BLOCK_SIZE - 1) / EXT2FS_BLOCK_SIZE;
    for (uint32_t i = 0; i < blocks_needed; i++)
    {
        if (!Ext2fsVolume__allocate_block_from_bitmap(self, block_bitmap, &inode->block[i]))
        {
            return false;
        }
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

        if (!Ext2fs__write_block(self, inode->block[i], &block))
        {
            return false;
        }
    }

    inode->size = (uint32_t) size;
    if (!Ext2fsVolume__write_inode(self, inode_number, inode))
    {
        return false;
    }

    if (creating && !Ext2fs__write_block(self, self->inode_bitmap_block, &inode_bitmap))
    {
        return false;
    }

    if (!Ext2fs__write_block(self, self->block_bitmap_block, &block_bitmap))
    {
        return false;
    }

    if (free_block_delta != 0 || free_inode_delta != 0)
    {
        return Ext2fsVolume__update_allocation_counts(self, free_block_delta, free_inode_delta, 0);
    }

    return true;
}

static bool Ext2fsVolume__write_file_at_path(
    Ext2fsVolumePtr self, ConstBStringPtr path, uint8_t * data, size_t size, uint32_t * inode_number, Ext2fsInodePtr inode
)
{
    uint32_t parent_inode_number = 0;
    BString leaf_name = empty_bstr();
    if (!Ext2fsVolume__resolve_parent(self, path, &parent_inode_number, &leaf_name))
    {
        return false;
    }

    Ext2fsInode parent_inode;
    uint32_t existing_inode_number = 0;
    if (!Ext2fsVolume__read_inode(self, parent_inode_number, &parent_inode) || !Ext2fsInode__is_dir(&parent_inode))
    {
        return false;
    }

    if (Ext2fs__find_child_inode(self, &parent_inode, &leaf_name, &existing_inode_number))
    {
        if (!Ext2fsVolume__read_inode(self, existing_inode_number, inode) || !Ext2fsInode__is_file(inode))
        {
            return false;
        }

        if (!Ext2fsVolume__write_inode_data(self, existing_inode_number, inode, data, size, false))
        {
            return false;
        }

        *inode_number = existing_inode_number;
        return true;
    }

    Ext2fsBlock inode_bitmap;
    if (!Ext2fs__read_block(self, self->inode_bitmap_block, &inode_bitmap))
    {
        return false;
    }

    uint32_t new_inode_number = 0;
    if (!Ext2fsVolume__allocate_inode_from_bitmap(self, inode_bitmap, &new_inode_number))
    {
        return false;
    }

    Ext2fsInode__reset_file(inode, (uint32_t) size);
    if (!Ext2fsVolume__write_inode_data(self, new_inode_number, inode, data, size, true))
    {
        return false;
    }

    if (!Ext2fsVolume__insert_dirent(self, parent_inode_number, leaf_name, new_inode_number, EXT2FS_DIRENT_TYPE_FILE))
    {
        return false;
    }

    *inode_number = new_inode_number;
    return true;
}

static bool Ext2fsVolume__mkdir_at_path(Ext2fsVolumePtr self, ConstBStringPtr path)
{
    uint32_t parent_inode_number = 0;
    BString leaf_name = empty_bstr();
    if (!Ext2fsVolume__resolve_parent(self, path, &parent_inode_number, &leaf_name))
    {
        return false;
    }

    Ext2fsInode parent_inode;
    uint32_t existing_inode_number = 0;
    if (!Ext2fsVolume__read_inode(self, parent_inode_number, &parent_inode) || !Ext2fsInode__is_dir(&parent_inode))
    {
        return false;
    }

    if (Ext2fs__find_child_inode(self, &parent_inode, &leaf_name, &existing_inode_number))
    {
        return false;
    }

    Ext2fsBlock inode_bitmap;
    Ext2fsBlock block_bitmap;
    Ext2fsBlock dir_block;
    if (!Ext2fs__read_block(self, self->inode_bitmap_block, &inode_bitmap)
        || !Ext2fs__read_block(self, self->block_bitmap_block, &block_bitmap))
    {
        return false;
    }

    uint32_t new_inode_number = 0;
    uint32_t new_dir_block = 0;
    if (!Ext2fsVolume__allocate_inode_from_bitmap(self, inode_bitmap, &new_inode_number)
        || !Ext2fsVolume__allocate_block_from_bitmap(self, block_bitmap, &new_dir_block))
    {
        return false;
    }

    Ext2fsInode new_inode;
    Ext2fsInode__reset_dir(&new_inode, new_dir_block);
    Ext2fs__initialise_dir_block(dir_block, new_inode_number, parent_inode_number);

    return Ext2fs__write_block(self, new_dir_block, &dir_block)
        && Ext2fsVolume__write_inode(self, new_inode_number, &new_inode)
        && Ext2fs__write_block(self, self->inode_bitmap_block, &inode_bitmap)
        && Ext2fs__write_block(self, self->block_bitmap_block, &block_bitmap)
        && Ext2fsVolume__insert_dirent(self, parent_inode_number, leaf_name, new_inode_number, EXT2FS_DIRENT_TYPE_DIR)
        && Ext2fsVolume__update_allocation_counts(self, -1, -1, 1);
}

static bool Ext2fsVolume__free_inode(Ext2fsVolumePtr self, uint32_t inode_number)
{
    if (inode_number == 0)
    {
        return false;
    }

    Ext2fsBlock inode_bitmap;
    if (!Ext2fs__read_block(self, self->inode_bitmap_block, &inode_bitmap))
    {
        return false;
    }

    Ext2fs__bitmap_clear_bit(inode_bitmap, inode_number - 1);
    return Ext2fs__write_block(self, self->inode_bitmap_block, &inode_bitmap)
        && Ext2fsVolume__update_allocation_counts(self, 0, 1, 0);
}

static bool Ext2fsVolume__free_block(Ext2fsVolumePtr self, uint32_t block_number)
{
    if (block_number < self->first_data_block)
    {
        return false;
    }

    Ext2fsBlock block_bitmap;
    if (!Ext2fs__read_block(self, self->block_bitmap_block, &block_bitmap))
    {
        return false;
    }

    Ext2fs__bitmap_clear_bit(block_bitmap, block_number - self->first_data_block);
    return Ext2fs__write_block(self, self->block_bitmap_block, &block_bitmap)
        && Ext2fsVolume__update_allocation_counts(self, 1, 0, 0);
}

static bool Ext2fsVolume__first_child(
    Ext2fsVolumePtr self, uint32_t directory_inode_number, BString * name, uint32_t * inode_number, Ext2fsInodePtr inode
)
{
    Ext2fsInode directory_inode;
    if (!Ext2fsVolume__read_inode(self, directory_inode_number, &directory_inode) || !Ext2fsInode__is_dir(&directory_inode))
    {
        return false;
    }

    for (uint32_t block_index = 0; block_index < EXT2FS_NDIR_BLOCKS; block_index++)
    {
        if (directory_inode.block[block_index] == 0)
        {
            continue;
        }

        Ext2fsBlock block;
        if (!Ext2fs__read_block(self, directory_inode.block[block_index], &block))
        {
            return false;
        }

        size_t offset = 0;
        while (offset + 8 <= EXT2FS_BLOCK_SIZE)
        {
            uint32_t entry_inode = Ext2fs__read_u32(block, offset);
            uint16_t rec_len = Ext2fs__read_u16(block, offset + 4);
            uint8_t name_len = block[offset + 6];
            if (rec_len == 0)
            {
                break;
            }

            if (entry_inode != 0 && name_len > 0)
            {
                BString entry_name = BString__from_chars((const char *) &block[offset + 8], name_len);
                if (!BString__equals(&entry_name, &bstr(".")) && !BString__equals(&entry_name, &bstr("..")))
                {
                    *name = entry_name;
                    *inode_number = entry_inode;
                    return Ext2fsVolume__read_inode(self, entry_inode, inode);
                }
            }

            offset += rec_len;
        }
    }

    return false;
}

static bool Ext2fsVolume__remove_inode(
    Ext2fsVolumePtr self, uint32_t parent_inode_number, BString leaf_name, uint32_t target_inode_number,
    Ext2fsInodePtr target_inode, Flags flags
)
{
    if (Ext2fsInode__is_dir(target_inode))
    {
        BString child_name = empty_bstr();
        uint32_t child_inode_number = 0;
        Ext2fsInode child_inode;
        while (Ext2fsVolume__first_child(self, target_inode_number, &child_name, &child_inode_number, &child_inode))
        {
            if ((flags & IO_REMOVE_RECURSIVE) == 0)
            {
                return false;
            }

            if (!Ext2fsVolume__remove_inode(self, target_inode_number, child_name, child_inode_number, &child_inode, flags))
            {
                return false;
            }
        }

        for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS; i++)
        {
            if (target_inode->block[i] != 0 && !Ext2fsVolume__free_block(self, target_inode->block[i]))
            {
                return false;
            }
        }

        memset(target_inode, 0, sizeof(*target_inode));
        return Ext2fsVolume__write_inode(self, target_inode_number, target_inode)
            && Ext2fsVolume__free_inode(self, target_inode_number)
            && Ext2fsVolume__update_allocation_counts(self, 0, 0, -1)
            && Ext2fsVolume__remove_dirent(self, parent_inode_number, leaf_name, &target_inode_number);
    }

    if (!Ext2fsInode__is_file(target_inode))
    {
        return false;
    }

    if (target_inode->links_count > 1)
    {
        target_inode->links_count--;
        return Ext2fsVolume__write_inode(self, target_inode_number, target_inode)
            && Ext2fsVolume__remove_dirent(self, parent_inode_number, leaf_name, &target_inode_number);
    }

    for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS; i++)
    {
        if (target_inode->block[i] != 0 && !Ext2fsVolume__free_block(self, target_inode->block[i]))
        {
            return false;
        }
    }

    memset(target_inode, 0, sizeof(*target_inode));
    return Ext2fsVolume__write_inode(self, target_inode_number, target_inode)
        && Ext2fsVolume__free_inode(self, target_inode_number)
        && Ext2fsVolume__remove_dirent(self, parent_inode_number, leaf_name, &target_inode_number);
}

static bool Ext2fsVolume__remove_at_path(Ext2fsVolumePtr self, ConstBStringPtr path, Flags flags)
{
    uint32_t parent_inode_number = 0;
    BString leaf_name = empty_bstr();
    if (!Ext2fsVolume__resolve_parent(self, path, &parent_inode_number, &leaf_name))
    {
        return false;
    }

    Ext2fsInode parent_inode;
    uint32_t target_inode_number = 0;
    Ext2fsInode target_inode;
    if (!Ext2fsVolume__read_inode(self, parent_inode_number, &parent_inode)
        || !Ext2fs__find_child_inode(self, &parent_inode, &leaf_name, &target_inode_number)
        || !Ext2fsVolume__read_inode(self, target_inode_number, &target_inode))
    {
        return false;
    }

    return Ext2fsVolume__remove_inode(self, parent_inode_number, leaf_name, target_inode_number, &target_inode, flags);
}

static ServiceCode Ext2fs__handle_open(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    Ext2fsInode inode;
    if (!Ext2fsVolume__inode_for_path(volume, &request->path, &inode) || !Ext2fsInode__is_file(&inode))
    {
        if ((request->flags & IO_OPEN_WRITE) != 0 && (request->flags & IO_OPEN_READ) == 0)
        {
            uint32_t blocks[12] = { 0 };
            return service_filesystem_complete_open(
                request->request_id, SERVICE_OK, 0, EXT2FS_MODE_FILE | 0644, 0, blocks
            );
        }

        return service_filesystem_complete_open(
            request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0, 0, 0, 0
        );
    }

    return service_filesystem_complete_open(
        request->request_id,
        SERVICE_OK,
        0,
        inode.mode,
        inode.size,
        inode.block
    );
}

static ServiceCode Ext2fs__handle_read(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    if (request->block == 0 || request->block_offset >= EXT2FS_BLOCK_SIZE
        || request->size > EXT2FS_BLOCK_SIZE - request->block_offset)
    {
        return service_filesystem_complete(request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
    }

    Ext2fsBlock block;
    if (!Ext2fs__read_block(volume, (uint32_t) request->block, &block))
    {
        return service_filesystem_complete(request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
    }

    memcpy((void *) request->buffer, &block[request->block_offset], request->size);
    return service_filesystem_complete(request->request_id, SERVICE_OK, (int64_t) request->size);
}

static ServiceCode Ext2fs__handle_write(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    Ext2fsInode inode;
    uint32_t inode_number = 0;
    if (!Ext2fsVolume__write_file_at_path(
        volume, &request->path, (uint8_t *) request->buffer, request->size, &inode_number, &inode
    ))
    {
        return service_filesystem_complete_open(
            request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0, 0, 0, 0
        );
    }

    return service_filesystem_complete_open(
        request->request_id,
        SERVICE_OK,
        (int64_t) request->size,
        inode.mode,
        inode.size,
        inode.block
    );
}

static ServiceCode Ext2fs__handle_mkdir(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    if (!Ext2fsVolume__mkdir_at_path(volume, &request->path))
    {
        return service_filesystem_complete(request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
    }

    return service_filesystem_complete(request->request_id, SERVICE_OK, IO_OK);
}

static ServiceCode Ext2fs__handle_remove(Ext2fsVolumePtr volume, ServiceFilesystemRequestPtr request)
{
    if (!Ext2fsVolume__remove_at_path(volume, &request->path, request->flags))
    {
        return service_filesystem_complete(request->request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
    }

    return service_filesystem_complete(request->request_id, SERVICE_OK, IO_OK);
}

static void Ext2fs__handle_request(Ext2fsVolumePtr volume, uint64_t request_id)
{
    ServiceFilesystemRequest request;
    ServiceCode request_status = service_filesystem_get_request(request_id, &request);
    if (!service_ok(request_status))
    {
        DRIVER_DEBUG("ext2fs: request %i fetch failed %i\n", (int) request_id, (int) request_status);
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

    DirectoryEntryCount count = Ext2fsVolume__list_dir(
        volume,
        &request.path,
        (DirectoryEntryPtr) request.entries,
        request.max_entries
    );
    if (count < 0)
    {
        service_filesystem_complete(request.request_id, SERVICE_FILESYSTEM_REQUEST_FAILED, 0);
        return;
    }

    service_filesystem_complete(request.request_id, SERVICE_OK, count);
}

int EXT2FS_ENTRYPOINT(int argc, char ** argv)
{
    if (argc != EXT2FS_ARG_COUNT)
    {
        DRIVER_DEBUG("ext2fs: usage <device> <mountpoint>\n");
        return 1;
    }

    BString filesystem_type = bstr("ext2");
    Ext2fsVolume volume = Ext2fsVolume__new(BString__new(argv[1]));
    BString mountpoint = BString__new(argv[2]);

    DRIVER_DEBUG("ext2fs: starting device=%B mountpoint=%B\n", &volume.device_path, &mountpoint);

    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("ext2fs: service registration failed\n");
        return 1;
    }

    if (!Ext2fsVolume__mount(&volume))
    {
        DRIVER_DEBUG("ext2fs: mount failed device=%B mountpoint=%B\n", &volume.device_path, &mountpoint);
        return 1;
    }

    DRIVER_DEBUG("ext2fs: mounted device=%B mountpoint=%B\n", &volume.device_path, &mountpoint);

    ServiceCode register_status = service_register_filesystem(&volume.device_path, &filesystem_type, &mountpoint);
    if (!service_ok(register_status))
    {
        DRIVER_DEBUG(
            "ext2fs: filesystem registration failed device=%B mountpoint=%B status=%i\n",
            &volume.device_path,
            &mountpoint,
            (int) register_status
        );
        return 1;
    }

    DRIVER_DEBUG("ext2fs: registered device=%B mountpoint=%B\n", &volume.device_path, &mountpoint);

    ServiceCode ready_status = service_publish_ready(&mountpoint);
    if (!service_ok(ready_status))
    {
        DRIVER_DEBUG(
            "ext2fs: ready publish failed mountpoint=%B status=%i\n",
            &mountpoint,
            (int) ready_status
        );
        return 1;
    }

    DRIVER_DEBUG("ext2fs: ready mountpoint=%B\n", &mountpoint);

    ServiceEvent event;
    for (ServiceCode status = service_wait(&event); service_alive(status); status = service_wait(&event))
    {
        if (service_ok(status) && event.type == SERVICE_EVENT_FILESYSTEM_IO)
        {
            Ext2fs__handle_request(&volume, event.source);
        }
    }

    return 0;
}
