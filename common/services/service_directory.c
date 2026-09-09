#include "service_directory.h"
#include "service_platform.h"

ServiceQueueMessage ServiceQueueMessage__new(uint64_t sender_pid, uint64_t kind)
{
    ServiceQueueMessage message = {0};
    message.sender_pid = sender_pid;
    message.kind = kind;
    return message;
}

ServiceQueueMessage ServiceQueueMessage__process_exit(uint64_t pid, uint64_t return_code)
{
    ServiceQueueMessage message = ServiceQueueMessage__new(0, SERVICE_QUEUE_MESSAGE_KERNEL_PROCESS_EXIT);
    message.arg0 = pid;
    message.arg1 = return_code;
    return message;
}

ServiceDirectoryPtr ServiceDirectory__shared(void)
{
    return (ServiceDirectoryPtr)HUND_SERVICE_DIRECTORY_VADDR;
}

ServiceQueuePtr ServiceQueue__at_index(uint64_t index)
{
    return (ServiceQueuePtr)(HUND_SERVICE_QUEUE_REGION_VADDR + (index * HUND_SERVICE_PAGE_SIZE));
}

ChannelStatePtr ServiceQueue__state(ServiceQueuePtr self)
{
    return &self->state;
}

uint64_t ServiceQueue__data_epoch(ServiceQueuePtr self)
{
    return __atomic_load_n(&self->state.data_epoch, __ATOMIC_ACQUIRE);
}

uint64_t ServiceQueue__space_epoch(ServiceQueuePtr self)
{
    return __atomic_load_n(&self->state.space_epoch, __ATOMIC_ACQUIRE);
}

void ServiceQueue__notify_data(ServiceQueuePtr self)
{
    __atomic_add_fetch(&self->state.data_epoch, 1, __ATOMIC_RELEASE);
    ServicePlatform__signal_event();
}

void ServiceQueue__notify_space(ServiceQueuePtr self)
{
    __atomic_add_fetch(&self->state.space_epoch, 1, __ATOMIC_RELEASE);
    ServicePlatform__signal_event();
}

bool ServiceQueue__has_messages(ServiceQueuePtr self)
{
    uint64_t head = __atomic_load_n(&self->head, __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&self->tail, __ATOMIC_ACQUIRE);
    return head != tail && __atomic_load_n(&self->message_ready[head], __ATOMIC_ACQUIRE) != 0;
}

ServiceQueueStatus ServiceQueue__push(ServiceQueuePtr self, ConstServiceQueueMessagePtr message)
{
    uint64_t slot = 0;
    while (true)
    {
        uint64_t tail = __atomic_load_n(&self->tail, __ATOMIC_ACQUIRE);
        uint64_t head = __atomic_load_n(&self->head, __ATOMIC_ACQUIRE);
        uint64_t next_tail = (tail + 1) % HUND_SERVICE_QUEUE_SLOTS;
        if (next_tail == head)
        {
            return SERVICE_QUEUE_FULL;
        }
        uint64_t expected = tail;
        if (__atomic_compare_exchange_n(&self->tail, &expected, next_tail, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
            slot = tail;
            break;
        }
    }
    self->messages[slot] = *message;
    __atomic_store_n(&self->message_ready[slot], 1, __ATOMIC_RELEASE);
    ServiceQueue__notify_data(self);
    return SERVICE_QUEUE_OK;
}

ServiceQueueStatus ServiceQueue__pop(ServiceQueuePtr self, ServiceQueueMessagePtr message)
{
    uint64_t head = __atomic_load_n(&self->head, __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&self->tail, __ATOMIC_ACQUIRE);
    if (head == tail || __atomic_load_n(&self->message_ready[head], __ATOMIC_ACQUIRE) == 0)
    {
        return SERVICE_QUEUE_EMPTY;
    }
    *message = self->messages[head];
    __atomic_store_n(&self->message_ready[head], 0, __ATOMIC_RELEASE);
    __atomic_store_n(&self->head, (head + 1) % HUND_SERVICE_QUEUE_SLOTS, __ATOMIC_RELEASE);
    ServiceQueue__notify_space(self);
    return SERVICE_QUEUE_OK;
}

void ServiceDirectoryName__copy_from_bstring(ServiceDirectoryNamePtr self, ConstBStringPtr name)
{
    StringPos name_length = BString__length(name);
    self->length = 0;
    for (StringPos index = 0; index < name_length && index < (StringPos)HUND_SERVICE_NAME_SIZE; index++)
    {
        self->data[index] = (uint8_t)BString__char_at_or(name, index, '\0');
        self->length++;
    }
}

bool ServiceDirectoryName__equals_bstring(ConstServiceDirectoryNamePtr self, ConstBStringPtr name)
{
    StringPos name_length = BString__length(name);
    if (name_length != self->length)
    {
        return false;
    }
    for (StringPos index = 0; index < name_length; index++)
    {
        if (self->data[index] != (uint8_t)BString__char_at_or(name, index, '\0'))
        {
            return false;
        }
    }
    return true;
}

bool ServiceDirectoryEntry__name_equals(ServiceDirectoryEntryPtr self, ConstBStringPtr name)
{
    return ServiceDirectoryName__equals_bstring(&self->name, name);
}

Optional(ServiceQueuePtr) ServiceDirectory__find_queue(ServiceDirectoryPtr self, ConstBStringPtr name)
{
    for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
    {
        ServiceDirectoryEntryPtr entry = &self->entries[index];
        if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0 && entry->queue_address != 0 &&
            ServiceDirectoryEntry__name_equals(entry, name))
        {
            return Some(ServiceQueuePtr, (ServiceQueuePtr)entry->queue_address);
        }
    }
    return None(ServiceQueuePtr);
}

OptionalDef(ServiceDirectoryEntryPtr);

static Optional(ServiceDirectoryEntryPtr)
    ServiceDirectory__find_protected(ServiceDirectoryPtr self, ConstBStringPtr name)
{
    for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
    {
        ServiceDirectoryEntryPtr entry = &self->entries[index];
        if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0 && entry->queue_address == 0 &&
            ServiceDirectoryEntry__name_equals(entry, name))
        {
            return Some(ServiceDirectoryEntryPtr, entry);
        }
    }
    return None(ServiceDirectoryEntryPtr);
}

Optional(AtomicMessageQueueEndpointID) ServiceDirectory__find_endpoint(ServiceDirectoryPtr self, ConstBStringPtr name)
{
    match_optional(
        ServiceDirectory__find_protected(self, name),
        some(entry)
        {
            return Some(AtomicMessageQueueEndpointID, __atomic_load_n(&entry->generation, __ATOMIC_ACQUIRE));
        },
        none_do_nothing()
    );
    return None(AtomicMessageQueueEndpointID);
}

Optional(uint64_t) ServiceDirectory__find_service_pid(ServiceDirectoryPtr self, ConstBStringPtr name)
{
    match_optional(
        ServiceDirectory__find_protected(self, name),
        some(entry) { return Some(uint64_t, __atomic_load_n(&entry->service_pid, __ATOMIC_ACQUIRE)); },
        none_do_nothing()
    );
    return None(uint64_t);
}
