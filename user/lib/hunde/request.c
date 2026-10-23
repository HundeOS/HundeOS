#include "native.h"
#include "sys/queue_wait.h"

static QueueWaitGate local_io_gate = QUEUE_WAIT_GATE_INITIALIZER;

static IOReply RustIOCallbacks__await_local(ConstRustIOCallbacksPtr self, uint64_t pid, IORequestPtr request)
{
    while (true)
    {
        match_optional(
            RustIOCallbacks__execute((IOServiceContext)self, HUNDE_LOCAL_IO_SESSION, pid, request),
            some(reply)
            {
                return reply;
            },
            none_do_nothing()
        );
        QueueWait__notify();
        match_result(
            QueueWait__cooperate(),
            success_do_nothing(),
            error(code, message)
            {
                RustIOCallbacks__disconnect((IOServiceContext)self, HUNDE_LOCAL_IO_SESSION, pid);
                return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
            }
        );
    }
}

IOReply RustIOCallbacks__execute_local(ConstRustIOCallbacksPtr self, uint64_t pid, IORequestPtr request)
{
    match_result(
        QueueWaitGate__acquire(&local_io_gate),
        success(code)
        {
            IOReply reply = RustIOCallbacks__await_local(self, pid, request);
            QueueWaitGate__release(&local_io_gate);
            return reply;
        },
        error(code, message)
        {
            return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
        }
    );
    return IOReply__new(IO_PANIC, 0);
}

_Static_assert(sizeof(((HundeIODirectoryEntry *)0)->name) == IO_DIRENT_NAME_MAX, "Directory name ABI mismatch");
_Static_assert(sizeof(((HundeIOCompletion *)0)->directory) / sizeof(HundeIODirectoryEntry) ==
                   IO_DIRECTORY_BATCH_CAPACITY,
               "Directory batch ABI mismatch");

static HundeIORequestV6 HundeIORequestV6__new(IORequestPtr request, QueueConnectionID session, uint64_t pid,
                                              IOReplyPtr reply)
{
    return (HundeIORequestV6){.operation = request->operation,
                              .session = session,
                              .pid = pid,
                              .handle = request->handle,
                              .flags = request->flags,
                              .count = request->count,
                              .read_data = IORequest__read_target(request, reply),
                              .write_data = IORequest__write_source(request),
                              .capacity = IORequest__capacity(request),
                              .offset = request->offset,
                              .inheritance_token = request->inheritance_token,
                              .inheritance_pid = request->inheritance_pid,
                              .buffered = request->buffered};
}

Optional(IOReply)
    RustIOCallbacks__execute(IOServiceContext self, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    RustIOCallbacksPtr callbacks = self;
    IOReply reply = IOReply__new(IO_OK, 0);
    if (request->count > IORequest__capacity(request))
    {
        return Some(IOReply, IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0));
    }
    HundeIORequestV6 input = HundeIORequestV6__new(request, session, pid, &reply);
    uint8_t link_paths[2 * IO_PROTOCOL_DATA_SIZE];
    if (request->operation == IO_REQUEST_LINK)
    {
        if (request->flags != 0 || request->count == 0 || request->count >= IO_PROTOCOL_DATA_SIZE ||
            request->target_path_length == 0 || request->target_path_length >= IO_PROTOCOL_DATA_SIZE)
        {
            return Some(IOReply, IOReply__new(IO_INVALID_PATH, 0));
        }
        BString source = IORequest__path(request);
        BString target = IORequest__target_path(request);
        if (BString__contains_char(&source, '\0') || BString__contains_char(&target, '\0'))
        {
            return Some(IOReply, IOReply__new(IO_INVALID_PATH, 0));
        }
        input.offset = BString__copy_to(&source, (char *)link_paths, sizeof(link_paths));
        input.count = input.offset +
                      BString__copy_to(&target, (char *)link_paths + input.offset, sizeof(link_paths) - input.offset);
        input.capacity = sizeof(link_paths);
        input.write_data = link_paths;
    }
    RustServiceCompletion output = callbacks->execute(callbacks->context, &input);
    HundeIOCompletion completion = output.completion;
    if (output.redirect != 0)
    {
        if (completion.kind != HUNDE_ABI_SUCCESS || output.redirect != 1 || output.service_length == 0 ||
            output.service_length >= IO_PROTOCOL_DATA_SIZE || output.path_length >= IO_PROTOCOL_DATA_SIZE ||
            output.target_length >= IO_PROTOCOL_DATA_SIZE)
        {
            return Some(IOReply, IOReply__new(IO_PANIC, 0));
        }
        BString service = BString__from_chars((const char *)output.service, output.service_length);
        BString path = BString__from_chars((const char *)output.path, output.path_length);
        BString target = BString__from_chars((const char *)output.target, output.target_length);
        if (BString__contains_char(&service, '\0') || BString__contains_char(&path, '\0') ||
            BString__contains_char(&target, '\0'))
        {
            return Some(IOReply, IOReply__new(IO_PANIC, 0));
        }
        IOReply redirected = IOReply__redirect(&service, &path, &target, true);
        redirected.value = output.offset;
        return Some(IOReply, redirected);
    }
    switch (completion.kind)
    {
    case HUNDE_ABI_SUCCESS:
        if (request->operation == IO_REQUEST_FILE_INFO)
        {
            if (completion.file_info.mode > UINT16_MAX)
            {
                return Some(IOReply, IOReply__new(IO_PANIC, 0));
            }
            reply.file_info = IOFileInfo__new((FileMode)completion.file_info.mode, completion.file_info.user_id,
                                              completion.file_info.group_id, completion.file_info.size);
            reply.file_info.identity = completion.file_info.identity;
        }
        if (request->operation == IO_REQUEST_LIST_DIR)
        {
            if (completion.directory_count > IO_DIRECTORY_BATCH_CAPACITY)
            {
                return Some(IOReply, IOReply__new(IO_PANIC, 0));
            }
            for (uint64_t index = 0; index < completion.directory_count; index++)
            {
                HundeIODirectoryEntry * entry = &completion.directory[index];
                if (entry->name_length >= IO_DIRENT_NAME_MAX || entry->info.mode > UINT16_MAX)
                {
                    return Some(IOReply, IOReply__new(IO_PANIC, 0));
                }
                BString name = BString__from_chars((const char *)entry->name, entry->name_length);
                reply.directory.entries[index] = DirectoryEntry__new(
                    IO_DIRENT_ATTR_NONE, entry->info.user_id, entry->info.group_id, (FileMode)entry->info.mode, &name);
            }
            reply.directory.count = completion.directory_count;
        }
        reply.value = completion.value;
        reply.pipe = IOPipe__new(completion.pipe_read, completion.pipe_write);
        reply.inheritance_flags = completion.inheritance_flags;
        if (request->operation == IO_REQUEST_WATCH_FILE || request->operation == IO_REQUEST_WAIT_FILE_CHANGE)
        {
            reply.file_change = completion.file_change;
        }
        return Some(IOReply, reply);
    case HUNDE_IO_PENDING:
        return None(IOReply);
    case HUNDE_ABI_ERROR:
        return Some(IOReply, IOReply__new(completion.error.code, 0));
    default:
        return Some(IOReply, IOReply__new(IO_PANIC, 0));
    }
}

bool RustIOCallbacks__disconnect(IOServiceContext self, QueueConnectionID session, uint64_t pid)
{
    RustIOCallbacksPtr callbacks = self;
    HundeStatusResult result = callbacks->disconnect(callbacks->context, session);
    return result.kind == HUNDE_ABI_SUCCESS;
}
