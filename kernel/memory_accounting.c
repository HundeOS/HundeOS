#include "memory_accounting.h"

LinkedListBodyDefine(MemoryMappings);

MemoryMapping MemoryMapping__new(Address space, Address start, Address backing, size_t size, MemoryMappingKind kind)
{
    return (MemoryMapping){.space = space, .start = start, .backing = backing, .size = size, .kind = kind};
}

bool MemoryMapping__equals(void * first, void * second)
{
    MemoryMappingPtr self = first;
    MemoryMappingPtr other = second;
    return self->space == other->space && self->start == other->start;
}

MemoryAccounting MemoryAccounting__new(void)
{
    return (MemoryAccounting){.lock = MUTEX_INIT, .mappings = MemoryMappings__new()};
}

static void MemoryAccounting__append(MemoryAccountingPtr self, MemoryMapping mapping)
{
    match_result(
        MemoryMappings__push(&self->mappings, mapping),
        success_do_nothing(),
        error(code, message)
        {
            self->failed = true;
        }
    );
}

static bool MemoryMapping__overlaps(MemoryMappingPtr self, void * context)
{
    MemoryMappingPtr range = context;
    return self->space == range->space && self->start < range->start + range->size &&
           range->start < self->start + self->size;
}

static void MemoryAccounting__unmap_locked(MemoryAccountingPtr self, Address space, Address start, size_t size)
{
    MemoryMapping range = MemoryMapping__new(space, start, 0, size, MEMORY_PRIVATE);
    while (true)
    {
        match_find_result(
            MemoryMappings__find(&self->mappings, MemoryMapping__overlaps, &range),
            some(found)
            {
                MemoryMapping mapping = found;
                MemoryMappings__remove(&self->mappings, found);
                if (mapping.start < start)
                {
                    MemoryMapping prefix = mapping;
                    prefix.size = start - mapping.start;
                    MemoryAccounting__append(self, prefix);
                }
                Address end = start + size;
                if (mapping.start + mapping.size > end)
                {
                    MemoryMapping suffix = mapping;
                    suffix.start = end;
                    suffix.backing += end - mapping.start;
                    suffix.size -= end - mapping.start;
                    MemoryAccounting__append(self, suffix);
                }
            },
            none()
            {
                return;
            }
        );
    }
}

void MemoryAccounting__map(MemoryAccountingPtr self, ConstMemoryMappingPtr mapping)
{
    if (mapping->size == 0)
    {
        return;
    }
    critical_section(
        self->lock,
        {
            MemoryAccounting__unmap_locked(self, mapping->space, mapping->start, mapping->size);
            MemoryAccounting__append(self, *mapping);
        }
    );
}

void MemoryAccounting__unmap(MemoryAccountingPtr self, Address space, Address start, size_t size)
{
    if (size == 0)
    {
        return;
    }
    critical_section(
        self->lock,
        {
            MemoryAccounting__unmap_locked(self, space, start, size);
        }
    );
}

static bool MemoryMapping__in_space(MemoryMappingPtr self, void * context)
{
    return self->space == *(AddressPtr)context;
}

void MemoryAccounting__forget(MemoryAccountingPtr self, Address space)
{
    critical_section(
        self->lock,
        {
            while (true)
            {
                match_find_result(
                    MemoryMappings__find(&self->mappings, MemoryMapping__in_space, &space),
                    some(found)
                    {
                        MemoryMappings__remove(&self->mappings, found);
                    },
                    none()
                    {
                        cs_return;
                    }
                );
            }
        }
    );
}

static uint64_t MemoryAccounting__shared_overlap(MemoryAccountingPtr self, MemoryMapping mapping)
{
    Address cursor = mapping.backing;
    Address end = cursor + mapping.size;
    uint64_t shared = 0;
    // Sweep interval boundaries: multiple peers sharing the same page count once for its owner.
    while (cursor < end)
    {
        Address covered_end = cursor;
        Address next_start = end;
        for_each_ll(
            MemoryMappings,
            self->mappings,
            peer,
            {
                if (peer.kind != MEMORY_SHARED)
                {
                    continue;
                }
                if (peer.backing <= cursor && peer.backing + peer.size > covered_end)
                {
                    covered_end = peer.backing + peer.size;
                }
                else if (peer.backing > cursor && peer.backing < next_start)
                {
                    next_start = peer.backing;
                }
            }
        );
        if (covered_end > cursor)
        {
            if (covered_end > end)
            {
                covered_end = end;
            }
            shared += covered_end - cursor;
            cursor = covered_end;
        }
        else
        {
            cursor = next_start;
        }
    }
    return shared;
}

Optional(ProcessMemoryUsage) MemoryAccounting__usage(MemoryAccountingPtr self, Address space)
{
    critical_section(
        self->lock,
        {
            if (self->failed)
            {
                cs_return None(ProcessMemoryUsage);
            }
            ProcessMemoryUsage usage = ProcessMemoryUsage__new();
            for_each_ll(
                MemoryMappings,
                self->mappings,
                mapping,
                {
                    if (mapping.space == space)
                    {
                        if (mapping.kind == MEMORY_DEVICE)
                        {
                            usage.device_bytes += mapping.size;
                        }
                        else if (mapping.kind == MEMORY_SHARED)
                        {
                            usage.shared_bytes += mapping.size;
                        }
                        else
                        {
                            uint64_t shared = MemoryAccounting__shared_overlap(self, mapping);
                            usage.shared_bytes += shared;
                            usage.private_bytes += mapping.size - shared;
                        }
                    }
                }
            );
            cs_return Some(ProcessMemoryUsage, usage);
        }
    );
}
