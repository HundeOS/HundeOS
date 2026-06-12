#include "embedded_fs.h"

#include <stddef.h>

static EmbeddedFsHeaderPtr EmbeddedFs__header(EmbeddedFsPtr self)
{
    return (EmbeddedFsHeaderPtr) self->start;
}

static EmbeddedFsEntryPtr EmbeddedFs__entries(EmbeddedFsPtr self)
{
    EmbeddedFsHeaderPtr header = EmbeddedFs__header(self);
    return (EmbeddedFsEntryPtr) (self->start + header->index_offset);
}

EmbeddedFs EmbeddedFs__new(Address start, Address end)
{
    return (EmbeddedFs) {
        .start = start,
        .end = end,
    };
}

bool EmbeddedFs__is_valid(EmbeddedFsPtr self)
{
    if (self->start == 0 || self->end <= self->start)
    {
        return false;
    }

    size_t size = self->end - self->start;
    if (size < sizeof(EmbeddedFsHeader))
    {
        return false;
    }

    EmbeddedFsHeaderPtr header = EmbeddedFs__header(self);
    if (header->magic != EMBEDDED_FS_MAGIC || header->version != EMBEDDED_FS_VERSION)
    {
        return false;
    }

    if (header->blob_size > size)
    {
        return false;
    }

    size_t index_end = header->index_offset + (header->entry_count * sizeof(EmbeddedFsEntry));
    if (header->index_offset < sizeof(EmbeddedFsHeader) || index_end > header->blob_size)
    {
        return false;
    }

    if (header->names_offset < index_end || header->names_offset > header->blob_size)
    {
        return false;
    }

    if (header->data_offset < header->names_offset || header->data_offset > header->blob_size)
    {
        return false;
    }

    return true;
}

static KString EmbeddedFs__entry_name(EmbeddedFsPtr self, EmbeddedFsEntryPtr entry)
{
    return KString__new((const char *) (self->start + entry->name_offset));
}

static bool EmbeddedFs__entry_is_valid(EmbeddedFsPtr self, EmbeddedFsEntryPtr entry)
{
    EmbeddedFsHeaderPtr header = EmbeddedFs__header(self);
    if (entry->name_offset < header->names_offset || entry->name_offset + entry->name_len > header->data_offset)
    {
        return false;
    }

    if (entry->data_offset < header->data_offset || entry->data_offset + entry->data_len > header->blob_size)
    {
        return false;
    }

    return true;
}

Optional(EmbeddedFsFile) EmbeddedFs__find(EmbeddedFsPtr self, KString name)
{
    if (!EmbeddedFs__is_valid(self))
    {
        return None(EmbeddedFsFile);
    }

    EmbeddedFsHeaderPtr header = EmbeddedFs__header(self);
    EmbeddedFsEntryPtr entries = EmbeddedFs__entries(self);

    for (int i = 0; i < header->entry_count; i++)
    {
        EmbeddedFsEntryPtr entry = &entries[i];
        if (!EmbeddedFs__entry_is_valid(self, entry))
        {
            continue;
        }

        KString entry_name = EmbeddedFs__entry_name(self, entry);
        if (KString__equals(&entry_name, &name))
        {
            return Some(EmbeddedFsFile, ((EmbeddedFsFile) {
                .start = self->start + entry->data_offset,
                .end = self->start + entry->data_offset + entry->data_len,
                .name = entry_name,
            }));
        }
    }

    return None(EmbeddedFsFile);
}

KStringArray EmbeddedFs__list(EmbeddedFsPtr self)
{
    KStringArray result = KStringArray__new();
    if (!EmbeddedFs__is_valid(self))
    {
        return result;
    }

    EmbeddedFsHeaderPtr header = EmbeddedFs__header(self);
    EmbeddedFsEntryPtr entries = EmbeddedFs__entries(self);

    for (int i = 0; i < header->entry_count; i++)
    {
        EmbeddedFsEntryPtr entry = &entries[i];
        if (EmbeddedFs__entry_is_valid(self, entry))
        {
            KStringArray__push(&result, EmbeddedFs__entry_name(self, entry));
        }
    }

    return result;
}
