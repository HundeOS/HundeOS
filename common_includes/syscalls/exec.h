#ifndef _COMMON_INCLUDES_SYSCALLS_EXEC_H
#define _COMMON_INCLUDES_SYSCALLS_EXEC_H

#include <stddef.h>
#include <stdint.h>

#include "ipc/io_protocol.h"
#include "io.h"

#define EXEC_PAYLOAD_MAX 65536ULL

typedef uint64_t ExecPayloadOffset;
typedef ExecPayloadOffset * ExecPayloadOffsetPtr;
typedef const ExecPayloadOffset * ConstExecPayloadOffsetPtr;

typedef struct ExecPayloadOpenDescription
{
    uint64_t id;
    int32_t status_flags;
    ExecPayloadOffset path_offset;
    IOHandle local;
    uint64_t token;
    Flags inheritance_flags;
    uint64_t service_length;
    uint8_t service_data[IO_PROTOCOL_DATA_SIZE];
    uint8_t single_transfer;
} ExecPayloadOpenDescription;

typedef ExecPayloadOpenDescription * ExecPayloadOpenDescriptionPtr;
typedef const ExecPayloadOpenDescription * ConstExecPayloadOpenDescriptionPtr;

typedef struct ExecPayloadDescriptor
{
    int32_t descriptor;
    int32_t descriptor_flags;
    uint64_t open_description;
} ExecPayloadDescriptor;

typedef ExecPayloadDescriptor * ExecPayloadDescriptorPtr;
typedef const ExecPayloadDescriptor * ConstExecPayloadDescriptorPtr;

typedef struct ExecPayload
{
    uint64_t size;
    ExecPayloadOffset path_offset;
    ExecPayloadOffset current_directory_offset;
    uint64_t argument_count;
    ExecPayloadOffset arguments_offset;
    uint64_t environment_count;
    ExecPayloadOffset environment_offset;
    uint64_t open_description_count;
    ExecPayloadOffset open_descriptions_offset;
    uint64_t descriptor_count;
    ExecPayloadOffset descriptors_offset;
    uint8_t data[];
} ExecPayload;

typedef ExecPayload * ExecPayloadPtr;
typedef const ExecPayload * ConstExecPayloadPtr;

static inline int ExecPayload__is_present(ConstExecPayloadPtr self)
{
    return (uintptr_t)self;
}

typedef enum ExecPayloadStatus
{
    EXEC_PAYLOAD_OK = 0,
    EXEC_PAYLOAD_INVALID = -1,
    EXEC_PAYLOAD_TOO_LARGE = -2,
} ExecPayloadStatus;

static inline size_t ExecPayload__size(ConstExecPayloadPtr self)
{
    return self->size;
}

static inline size_t ExecPayload__argument_count(ConstExecPayloadPtr self)
{
    return self->argument_count;
}

static inline size_t ExecPayload__environment_count(ConstExecPayloadPtr self)
{
    return self->environment_count;
}

static inline size_t ExecPayload__open_description_count(ConstExecPayloadPtr self)
{
    return self->open_description_count;
}

static inline size_t ExecPayload__descriptor_count(ConstExecPayloadPtr self)
{
    return self->descriptor_count;
}

static inline const char * ExecPayload__string_at(ConstExecPayloadPtr self, ExecPayloadOffset offset)
{
    return offset < self->size ? (const char *)self + offset : NULL;
}

static inline const char * ExecPayload__path(ConstExecPayloadPtr self)
{
    return ExecPayload__string_at(self, self->path_offset);
}

static inline const char * ExecPayload__current_directory(ConstExecPayloadPtr self)
{
    return ExecPayload__string_at(self, self->current_directory_offset);
}

static inline const char * ExecPayload__argument_at(ConstExecPayloadPtr self, size_t index)
{
    if (index >= self->argument_count || self->arguments_offset > self->size ||
        self->argument_count > (self->size - self->arguments_offset) / sizeof(ExecPayloadOffset))
    {
        return NULL;
    }
    ConstExecPayloadOffsetPtr offsets = (ConstExecPayloadOffsetPtr)((const uint8_t *)self + self->arguments_offset);
    return ExecPayload__string_at(self, offsets[index]);
}

static inline const char * ExecPayload__environment_at(ConstExecPayloadPtr self, size_t index)
{
    if (index >= self->environment_count || self->environment_offset > self->size ||
        self->environment_count > (self->size - self->environment_offset) / sizeof(ExecPayloadOffset))
    {
        return NULL;
    }
    ConstExecPayloadOffsetPtr offsets = (ConstExecPayloadOffsetPtr)((const uint8_t *)self + self->environment_offset);
    return ExecPayload__string_at(self, offsets[index]);
}

static inline ConstExecPayloadOpenDescriptionPtr ExecPayload__open_description_at(ConstExecPayloadPtr self,
                                                                                  size_t index)
{
    if (index >= self->open_description_count || self->open_descriptions_offset > self->size ||
        (self->open_descriptions_offset % _Alignof(ExecPayloadOpenDescription)) != 0 ||
        self->open_description_count >
            (self->size - self->open_descriptions_offset) / sizeof(ExecPayloadOpenDescription))
    {
        return NULL;
    }
    ConstExecPayloadOpenDescriptionPtr descriptions =
        (ConstExecPayloadOpenDescriptionPtr)((const uint8_t *)self + self->open_descriptions_offset);
    return &descriptions[index];
}

static inline ConstExecPayloadDescriptorPtr ExecPayload__descriptor_at(ConstExecPayloadPtr self, size_t index)
{
    if (index >= self->descriptor_count || self->descriptors_offset > self->size ||
        (self->descriptors_offset % _Alignof(ExecPayloadDescriptor)) != 0 ||
        self->descriptor_count > (self->size - self->descriptors_offset) / sizeof(ExecPayloadDescriptor))
    {
        return NULL;
    }
    ConstExecPayloadDescriptorPtr descriptors =
        (ConstExecPayloadDescriptorPtr)((const uint8_t *)self + self->descriptors_offset);
    return &descriptors[index];
}

static inline size_t ExecPayload__bounded_string_length(ConstExecPayloadPtr self, ExecPayloadOffset offset)
{
    if (offset >= self->size)
    {
        return SIZE_MAX;
    }
    const uint8_t * bytes = (const uint8_t *)self;
    for (size_t index = offset; index < self->size; index++)
    {
        if (bytes[index] == 0)
        {
            return index - offset;
        }
    }
    return SIZE_MAX;
}

static inline int ExecPayload__description_exists(ConstExecPayloadPtr self, uint64_t id)
{
    for (size_t index = 0; index < self->open_description_count; index++)
    {
        ConstExecPayloadOpenDescriptionPtr description = ExecPayload__open_description_at(self, index);
        if (description == NULL)
        {
            return 0;
        }
        if (description->id == id)
        {
            return 1;
        }
    }
    return 0;
}

static inline ExecPayloadStatus ExecPayload__validate(ConstExecPayloadPtr self, size_t copied_size)
{
    if (copied_size < sizeof(ExecPayload) || copied_size > EXEC_PAYLOAD_MAX || self->size != copied_size)
    {
        return copied_size > EXEC_PAYLOAD_MAX ? EXEC_PAYLOAD_TOO_LARGE : EXEC_PAYLOAD_INVALID;
    }
    if ((self->arguments_offset % sizeof(ExecPayloadOffset)) != 0 ||
        (self->environment_offset % sizeof(ExecPayloadOffset)) != 0 || self->arguments_offset > self->size ||
        self->environment_offset > self->size ||
        self->argument_count > (self->size - self->arguments_offset) / sizeof(ExecPayloadOffset) ||
        self->environment_count > (self->size - self->environment_offset) / sizeof(ExecPayloadOffset) ||
        self->open_descriptions_offset > self->size || self->descriptors_offset > self->size ||
        (self->open_descriptions_offset % _Alignof(ExecPayloadOpenDescription)) != 0 ||
        (self->descriptors_offset % _Alignof(ExecPayloadDescriptor)) != 0 ||
        self->open_description_count >
            (self->size - self->open_descriptions_offset) / sizeof(ExecPayloadOpenDescription) ||
        self->descriptor_count > (self->size - self->descriptors_offset) / sizeof(ExecPayloadDescriptor))
    {
        return EXEC_PAYLOAD_INVALID;
    }
    size_t path_length = ExecPayload__bounded_string_length(self, self->path_offset);
    if (path_length == 0 || path_length == SIZE_MAX)
    {
        return EXEC_PAYLOAD_INVALID;
    }
    const char * current_directory = ExecPayload__current_directory(self);
    if (current_directory == NULL || current_directory[0] != '/' ||
        ExecPayload__bounded_string_length(self, (const uint8_t *)current_directory - (const uint8_t *)self) ==
            SIZE_MAX)
    {
        return EXEC_PAYLOAD_INVALID;
    }
    for (size_t index = 0; index < self->argument_count; index++)
    {
        const char * argument = ExecPayload__argument_at(self, index);
        if (argument == NULL ||
            ExecPayload__bounded_string_length(self, (const uint8_t *)argument - (const uint8_t *)self) == SIZE_MAX)
        {
            return EXEC_PAYLOAD_INVALID;
        }
    }
    for (size_t index = 0; index < self->environment_count; index++)
    {
        const char * environment = ExecPayload__environment_at(self, index);
        if (environment == NULL ||
            ExecPayload__bounded_string_length(self, (const uint8_t *)environment - (const uint8_t *)self) == SIZE_MAX)
        {
            return EXEC_PAYLOAD_INVALID;
        }
    }
    for (size_t index = 0; index < self->open_description_count; index++)
    {
        ConstExecPayloadOpenDescriptionPtr description = ExecPayload__open_description_at(self, index);
        if (description == NULL || description->service_length >= IO_PROTOCOL_DATA_SIZE || description->token == 0 ||
            description->local < IO_SESSION_HANDLE_BASE ||
            ExecPayload__bounded_string_length(self, description->path_offset) == SIZE_MAX)
        {
            return EXEC_PAYLOAD_INVALID;
        }
    }
    for (size_t index = 0; index < self->descriptor_count; index++)
    {
        ConstExecPayloadDescriptorPtr descriptor = ExecPayload__descriptor_at(self, index);
        if (descriptor == NULL || descriptor->descriptor < 0 ||
            !ExecPayload__description_exists(self, descriptor->open_description))
        {
            return EXEC_PAYLOAD_INVALID;
        }
    }
    return EXEC_PAYLOAD_OK;
}

#endif
