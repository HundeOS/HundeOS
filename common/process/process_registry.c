#include "process_registry.h"

Result(OptionalProcessRecord) ProcessRegistry__at(ConstProcessRegistryPtr self, uint64_t index)
{
    if (self->version != PROCESS_REGISTRY_VERSION)
    {
        return Error(OptionalProcessRecord, bstr("Invalid process registry"), ATOMIC_RECORD_INVALID);
    }
    for (uint64_t cpu = 0; cpu < PROCESS_REGISTRY_CPUS; cpu++)
    {
        uint64_t count = unwrap_optional(ProcessRecordCounts__at(&self->counts, cpu), 0);
        if (count > PROCESS_REGISTRY_CPU_CAPACITY)
        {
            return Error(OptionalProcessRecord, bstr("Invalid process count"), ATOMIC_RECORD_INVALID);
        }
        if (index < count)
        {
            match_result(
                ProcessRecords__at(&self->records, cpu * PROCESS_REGISTRY_CPU_CAPACITY + index),
                success(record)
                {
                    return Success(OptionalProcessRecord, Some(ProcessRecord, record));
                },
                error(code, message)
                {
                    return Error(OptionalProcessRecord, message, code);
                }
            );
        }
        index -= count;
    }
    return Success(OptionalProcessRecord, None(ProcessRecord));
}

Result(OptionalProcessRecord) ProcessRegistry__find(ConstProcessRegistryPtr self, uint64_t pid)
{
    for (uint64_t index = 0; index < PROCESS_REGISTRY_CAPACITY; index++)
    {
        match_result(
            ProcessRegistry__at(self, index),
            success(found)
            {
                match_optional(
                    found,
                    some(record)
                    {
                        if (record.pid == pid)
                        {
                            return Success(OptionalProcessRecord, found);
                        }
                    },
                    none()
                    {
                        return Success(OptionalProcessRecord, found);
                    }
                );
            },
            error(code, message)
            {
                return Error(OptionalProcessRecord, message, code);
            }
        );
    }
    return Success(OptionalProcessRecord, None(ProcessRecord));
}

Result(CollectionCode)
    ProcessRegistry__set(ProcessRegistryPtr self, uint64_t cpu, uint64_t index, ConstProcessRecordPtr record)
{
    if (cpu >= PROCESS_REGISTRY_CPUS || index >= PROCESS_REGISTRY_CPU_CAPACITY)
    {
        return Error(CollectionCode, bstr("Invalid process slot"), ATOMIC_RECORD_INVALID);
    }
    return ProcessRecords__set(&self->records, cpu * PROCESS_REGISTRY_CPU_CAPACITY + index, record);
}

Result(CollectionCode) ProcessRegistry__set_count(ProcessRegistryPtr self, uint64_t cpu, uint64_t count)
{
    if (cpu >= PROCESS_REGISTRY_CPUS || count > PROCESS_REGISTRY_CPU_CAPACITY)
    {
        return Error(CollectionCode, bstr("Invalid process count"), ATOMIC_RECORD_INVALID);
    }
    ProcessRecordCounts__set(&self->counts, cpu, count);
    return Success(CollectionCode, 0);
}

ProcessInfo ProcessRecord__info(ConstProcessRecordPtr self)
{
    ProcessInfo info = ProcessInfo__new(self->pid);
    info.cpu = self->cpu;
    info.cpu_usage = self->cpu_usage;
    info.user_wait_usec = self->user_wait_usec;
    BString state = bstring(ProcessState__to_string(self->state));
    BString class_name = self->process_class == PROCESS_CLASS_REALTIME_SERVICE ? bstr("rt-service")
                         : self->process_class == PROCESS_CLASS_SERVICE        ? bstr("service")
                                                                               : bstr("normal");
    BString user_name = self->credentials.user_id == PROCESS_USER_ROOT   ? bstr("root")
                        : self->credentials.user_id == PROCESS_USER_HUND ? bstr("hund")
                        : self->credentials.user_id == PROCESS_USER_USER ? bstr("user")
                                                                         : bstr("unknown");
    BString wait = bstr("-");
    match_optional(
        self->wait_reason,
        some(reason)
        {
            wait = ParkReason__to_string(reason);
        },
        none()
        {
            match_optional(
                self->user_wait,
                some(user_wait)
                {
                    wait = BString__sprintf(&bstr("USER_WAIT %llu.%03us"), user_wait.elapsed_usec / 1000000ULL,
                                            (unsigned int)((user_wait.elapsed_usec / 1000ULL) % 1000ULL));
                },
                none_do_nothing()
            );
        }
    );
    BString__copy_to(&self->name, info.name, sizeof(info.name));
    BString__copy_to(&state, info.state, sizeof(info.state));
    BString__copy_to(&wait, info.wait_reason, sizeof(info.wait_reason));
    BString__copy_to(&class_name, info.class_name, sizeof(info.class_name));
    BString__copy_to(&user_name, info.user_name, sizeof(info.user_name));
    return info;
}
