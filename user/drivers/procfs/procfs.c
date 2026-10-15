#include "procfs.h"
#include "memory.h"

typedef struct ProcPath
{
    uint64_t pid;
    BString leaf;
} ProcPath;

ResultDef(ProcPath);

typedef struct ProcfsWriter
{
    FilesystemFileDataPtr data;
    FilesystemFileSize size;
    bool failed;
} ProcfsWriter;

typedef ProcfsWriter * ProcfsWriterPtr;

static ProcfsWriter ProcfsWriter__new(FilesystemFileDataPtr data)
{
    return (ProcfsWriter){.data = data};
}

static void ProcfsWriter__line(ProcfsWriterPtr self, ConstBStringPtr key, uint64_t value)
{
    BString line = BString__sprintf(&bstr("%s=%llu\n"), cstr_p(key), value);
    StringPos size = BString__length(&line);
    if (self->size + size > FILESYSTEM_FILE_CAPACITY)
    {
        self->failed = true;
        return;
    }
    Mem__memcpy(self->data + self->size, BString__cstr(&line), size);
    self->size += size;
}

static void ProcfsWriter__latency(ProcfsWriterPtr self, ConstBStringPtr name,
                                  ConstIODiagnosticLatencyPercentilesPtr latency)
{
    BString p50_key = BString__sprintf(&bstr("block_%s_usec_p50"), cstr_p(name));
    BString p70_key = BString__sprintf(&bstr("block_%s_usec_p70"), cstr_p(name));
    BString p90_key = BString__sprintf(&bstr("block_%s_usec_p90"), cstr_p(name));
    BString p99_key = BString__sprintf(&bstr("block_%s_usec_p99"), cstr_p(name));
    ProcfsWriter__line(self, &p50_key, latency->p50_usec);
    ProcfsWriter__line(self, &p70_key, latency->p70_usec);
    ProcfsWriter__line(self, &p90_key, latency->p90_usec);
    ProcfsWriter__line(self, &p99_key, latency->p99_usec);
}

static ProcPath ProcPath__new(uint64_t pid, ConstBStringPtr leaf)
{
    return (ProcPath){.pid = pid, .leaf = *leaf};
}

static Result(ProcPath) ProcPath__parse(ConstBStringPtr path)
{
    BString relative = BString__slice(path, 1, BString__length(path));
    BStringSplit split = BString__split_first_token(&relative, &bstr("/"));
    match_optional(
        split.match,
        some(pid_text)
        {
            match_optional(
                BString__parse_uint64(&pid_text),
                some(pid)
                {
                    BString leaf = split.rest;
                    BString canonical = BString__is_empty(&leaf) ? BString__sprintf(&bstr("/%llu"), pid)
                                                                 : BString__sprintf(&bstr("/%llu/%B"), pid, &leaf);
                    if (BString__equals(path, &canonical))
                    {
                        return Success(ProcPath, ProcPath__new(pid, &leaf));
                    }
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return Error(ProcPath, bstr("Invalid process path"), IO_INVALID_PATH);
}

static Result(ProcessRecord) Procfs__find(ConstProcessRegistryPtr registry, uint64_t pid)
{
    match_result(
        ProcessRegistry__find(registry, pid),
        success(found)
        {
            match_optional(
                found,
                some(record)
                {
                    return Success(ProcessRecord, record);
                },
                none()
                {
                    return Error(ProcessRecord, bstr("Process not found"), IO_INVALID_PATH);
                }
            );
        },
        error(code, message)
        {
            return Error(ProcessRecord, message, code == ATOMIC_RECORD_BUSY ? IO_WAIT_FOR_DATA : IO_NO_DEVICE_FOUND);
        }
    );
}

static Result(FilesystemFileSize)
    Procfs__load(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data)
{
    ProcfsContextPtr procfs = context;
    if (BString__equals(path, &bstr("/ktrace")))
    {
        match_result(
            procfs->load_kernel_trace(data, FILESYSTEM_FILE_CAPACITY),
            success(size)
            {
                return Success(FilesystemFileSize, size);
            },
            error(code, message)
            {
                return Error(FilesystemFileSize, message, IO_NO_DEVICE_FOUND);
            }
        );
    }
    ProcPath parsed;
    match_result(
        ProcPath__parse(path),
        success(value)
        {
            parsed = value;
        },
        error(code, message)
        {
            return Error(FilesystemFileSize, message, code);
        }
    );
    ProcessRecord record;
    match_result(
        Procfs__find(procfs->registry, parsed.pid),
        success(value)
        {
            record = value;
        },
        error(code, message)
        {
            return Error(FilesystemFileSize, message, code);
        }
    );
    BString text;
    if (BString__equals(&parsed.leaf, &bstr("status")))
    {
        ProcessInfo info = ProcessRecord__info(&record);
        text = BString__sprintf(
            &bstr("pid=%llu\nuid=%llu\ngid=%llu\nstate=%s\nwait=%s\nclass=%s\ncpu=%llu\nsampled_at_usec=%llu\nname="),
            record.pid, record.credentials.user_id, record.credentials.group_id, info.state, info.wait_reason,
            info.class_name, record.cpu, record.sampled_at_usec);
        size_t prefix = BString__length(&text);
        Mem__memcpy(data, BString__cstr(&text), prefix);
        size_t length = BString__length(&record.name);
        Mem__memcpy((char *)data + prefix, BString__cstr(&record.name), length);
        ((char *)data)[prefix + length] = '\n';
        return Success(FilesystemFileSize, prefix + length + 1);
    }
    if (BString__equals(&parsed.leaf, &bstr("cpu")))
    {
        ProcessCPUUsage usage = record.cpu_usage;
        text = BString__sprintf(
            &bstr("cpu=%llu\ntotal_usec=%llu\nCPU%% (1s)=%llu.%02llu\nCPU%% (100ms)=%llu.%02llu\n"
                  "scheduled_usec=%llu\nSCHED CPU%% (1s)=%llu.%02llu\nSCHED CPU%% (100ms)=%llu.%02llu\n"
                  "sampled_at_usec=%llu\n"),
            record.cpu, usage.total_usec, usage.long_basis_points / 100, usage.long_basis_points % 100,
            usage.short_basis_points / 100, usage.short_basis_points % 100, usage.scheduled_usec,
            usage.scheduled_long_basis_points / 100, usage.scheduled_long_basis_points % 100,
            usage.scheduled_short_basis_points / 100, usage.scheduled_short_basis_points % 100, record.sampled_at_usec);
    }
    else if (BString__equals(&parsed.leaf, &bstr("queue-wait")))
    {
        ProcessQueueWaitUsage usage = record.queue_wait_usage;
        text = BString__sprintf(
            &bstr("parks=%llu\nactivity_wakes=%llu\ntimeout_wakes=%llu\nselections=%llu\n"
                  "speculative_selections=%llu\n"
                  "parked_usec=%llu\nmax_parked_usec=%llu\nrunnable_usec=%llu\nmax_runnable_usec=%llu\n"
                  "sampled_at_usec=%llu\n"),
            usage.parks, usage.activity_wakes, usage.timeout_wakes, usage.selections, usage.speculative_selections,
            usage.parked_usec, usage.max_parked_usec, usage.runnable_usec, usage.max_runnable_usec,
            record.sampled_at_usec);
    }
    else if (BString__equals(&parsed.leaf, &bstr("ipc")))
    {
        match_optional(
            record.io_diagnostics,
            some(usage)
            {
                ProcfsWriter writer = ProcfsWriter__new(data);
                ProcfsWriter__line(&writer, &bstr("uart_bytes"), usage.uart_bytes);
                ProcfsWriter__line(&writer, &bstr("uart_irqs"), usage.uart_irqs);
                ProcfsWriter__line(&writer, &bstr("uart_reads"), usage.uart_reads);
                ProcfsWriter__line(&writer, &bstr("uart_empty"), usage.uart_empty);
                ProcfsWriter__line(&writer, &bstr("backend_reads"), usage.backend_reads);
                ProcfsWriter__line(&writer, &bstr("backend_empty"), usage.backend_empty);
                ProcfsWriter__line(&writer, &bstr("block_submissions"), usage.block_submissions);
                ProcfsWriter__line(&writer, &bstr("block_blocks"), usage.block_blocks);
                ProcfsWriter__line(&writer, &bstr("block_failures"), usage.block_failures);
                ProcfsWriter__line(&writer, &bstr("block_cache_hits"), usage.block_cache_hits);
                ProcfsWriter__line(&writer, &bstr("block_cache_misses"), usage.block_cache_misses);
                ProcfsWriter__line(&writer, &bstr("block_cache_evictions"), usage.block_cache_evictions);
                ProcfsWriter__line(&writer, &bstr("inode_cache_hits"), usage.inode_cache_hits);
                ProcfsWriter__line(&writer, &bstr("inode_cache_misses"), usage.inode_cache_misses);
                ProcfsWriter__line(&writer, &bstr("inode_cache_evictions"), usage.inode_cache_evictions);
                ProcfsWriter__latency(&writer, &bstr("device"), &usage.block_device);
                ProcfsWriter__line(&writer, &bstr("block_segmented_submissions"), usage.block_segmented_submissions);
                ProcfsWriter__latency(&writer, &bstr("device_irq"), &usage.block_device_irq);
                ProcfsWriter__latency(&writer, &bstr("manager_wake"), &usage.block_manager_wake);
                ProcfsWriter__latency(&writer, &bstr("irq_relay"), &usage.block_irq_relay);
                ProcfsWriter__latency(&writer, &bstr("fiber_wake"), &usage.block_fiber_wake);
                ProcfsWriter__latency(&writer, &bstr("completion"), &usage.block_completion);
                ProcfsWriter__line(&writer, &bstr("wait_entries"), usage.wait_entries);
                ProcfsWriter__line(&writer, &bstr("unchanged_wakes"), usage.unchanged_wakes);
                ProcfsWriter__line(&writer, &bstr("user_wait_usec"), record.user_wait_usec);
                ProcfsWriter__line(&writer, &bstr("sampled_at_usec"), record.sampled_at_usec);
                if (writer.failed)
                {
                    return Error(FilesystemFileSize, bstr("Process diagnostics exceed file capacity"),
                                 IO_REGISTER_FAILED);
                }
                return Success(FilesystemFileSize, writer.size);
            },
            none()
            {
                text = bstr("diagnostics=unavailable\n");
            }
        );
    }
    else if (BString__equals(&parsed.leaf, &bstr("memory")))
    {
        match_optional(
            record.memory,
            some(usage)
            {
                text = BString__sprintf(
                    &bstr("private_bytes=%llu\nshared_bytes=%llu\ndevice_bytes=%llu\nsampled_at_usec=%llu\n"),
                    usage.private_bytes, usage.shared_bytes, usage.device_bytes, record.sampled_at_usec);
            },
            none()
            {
                text = bstr("accounting=unavailable\n");
            }
        );
    }
    else
    {
        return Error(FilesystemFileSize, bstr("Unknown process file"), IO_INVALID_PATH);
    }
    Mem__memcpy(data, BString__cstr(&text), BString__length(&text));
    return Success(FilesystemFileSize, BString__length(&text));
}

static Result(DirectoryEntryCount) Procfs__list(IOServiceContext context, ConstBStringPtr path,
                                                DirectoryEntryPtr entries, size_t capacity, uint64_t offset)
{
    ProcfsContextPtr procfs = context;
    DirectoryEntryCount count = 0;
    if (BString__is_empty(path) || BString__equals(path, &bstr("/")))
    {
        if ((size_t)count < capacity && offset == 0)
        {
            entries[count++] = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_FILE | 0444, &bstr("ktrace"));
            offset++;
        }
        while ((size_t)count < capacity && offset <= PROCESS_REGISTRY_CAPACITY)
        {
            match_result(
                ProcessRegistry__at(procfs->registry, offset++ - 1U),
                success(found)
                {
                    match_optional(
                        found,
                        some(record)
                        {
                            BString name = BString__sprintf(&bstr("%llu"), record.pid);
                            entries[count++] =
                                DirectoryEntry__new(0, record.credentials.user_id, record.credentials.group_id,
                                                    IO_MODE_TYPE_DIR | 0555, &name);
                        },
                        none()
                        {
                            return Success(DirectoryEntryCount, count);
                        }
                    );
                },
                error(code, message)
                {
                    return Error(DirectoryEntryCount, message,
                                 code == ATOMIC_RECORD_BUSY ? IO_WAIT_FOR_DATA : IO_NO_DEVICE_FOUND);
                }
            );
        }
        return Success(DirectoryEntryCount, count);
    }
    ProcPath parsed;
    match_result(
        ProcPath__parse(path),
        success(value)
        {
            parsed = value;
        },
        error(code, message)
        {
            return Error(DirectoryEntryCount, message, code);
        }
    );
    if (!BString__is_empty(&parsed.leaf))
    {
        return Error(DirectoryEntryCount, bstr("Not a process directory"), IO_INVALID_PATH);
    }
    match_result(
        Procfs__find(procfs->registry, parsed.pid),
        success(record)
        {
            while ((size_t)count < capacity && offset < 5)
            {
                BString name = offset == 0   ? bstr("status")
                               : offset == 1 ? bstr("cpu")
                               : offset == 2 ? bstr("memory")
                               : offset == 3 ? bstr("queue-wait")
                                             : bstr("ipc");
                entries[count++] = DirectoryEntry__new(0, record.credentials.user_id, record.credentials.group_id,
                                                       IO_MODE_TYPE_FILE | 0444, &name);
                offset++;
            }
            return Success(DirectoryEntryCount, count);
        },
        error(code, message)
        {
            return Error(DirectoryEntryCount, message, code);
        }
    );
}

static IOStatus Procfs__store(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data,
                              FilesystemFileSize size)
{
    return IO_PERMISSION_DENIED;
}

static IOStatus Procfs__mkdir(IOServiceContext context, ConstBStringPtr path)
{
    return IO_PERMISSION_DENIED;
}

static IOStatus Procfs__remove(IOServiceContext context, ConstBStringPtr path, Flags flags)
{
    return IO_PERMISSION_DENIED;
}

FilesystemOperations Procfs__operations(void)
{
    return FilesystemOperations__new_snapshot(Procfs__load, Procfs__store, Procfs__list, Procfs__mkdir, Procfs__remove,
                                              FilesystemOperations__unsupported_link, 0);
}
