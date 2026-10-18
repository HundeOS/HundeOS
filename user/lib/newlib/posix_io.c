#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include "../../common/types/map.h"
#include "../io/client.h"
#include "../sys/io.h"
#include "../sys/process.h"
#include "../trace/io_trace.h"
#include "current_directory.h"
#include "posix_io.h"

enum
{
    POSIX_FIRST_FILE_DESCRIPTOR = 3,
    POSIX_MAX_OPEN_FILES = 64,
};

static bool PosixIO__descriptor_equals(void * left, void * right)
{
    return *(int *)left == *(int *)right;
}

OptionalDef(int);
typedef struct PosixFileDescriptor
{
    uint64_t description;
    int descriptor_flags;
} PosixFileDescriptor;
OptionalDef(PosixFileDescriptor);
MapDefine(int, PosixFileDescriptor, POSIX_MAX_OPEN_FILES, PosixFileDescriptors, PosixIO__descriptor_equals);
MapBodyDefine(PosixFileDescriptors);

static PosixFileDescriptors descriptors = EmptyMap(PosixFileDescriptors);

static bool PosixIO__description_equals(void * left, void * right)
{
    return *(uint64_t *)left == *(uint64_t *)right;
}

typedef struct PosixOpenDescription
{
    uint64_t id;
    IOHandle handle;
    BString path;
    uint64_t references;
    int status_flags;
} PosixOpenDescription;

typedef PosixOpenDescription * PosixOpenDescriptionPtr;
OptionalDef(PosixOpenDescription);
MapDefine(uint64_t, PosixOpenDescription, POSIX_MAX_OPEN_FILES, PosixOpenDescriptions, PosixIO__description_equals);
MapBodyDefine(PosixOpenDescriptions);

static PosixOpenDescriptions open_descriptions = EmptyMap(PosixOpenDescriptions);
static uint64_t next_description = 1;

MapDefine(uint64_t, IOClientInheritance, POSIX_MAX_OPEN_FILES, PosixForkInheritances, PosixIO__description_equals);
MapBodyDefine(PosixForkInheritances);
static PosixForkInheritances fork_inheritances = EmptyMap(PosixForkInheritances);

static int PosixIO__errno(IOStatus status);
static bool PosixIO__is_standard_descriptor(int descriptor);

static ExecPayloadOpenDescription exec_open_descriptions[POSIX_MAX_OPEN_FILES];
static ExecPayloadDescriptor exec_descriptors[POSIX_MAX_OPEN_FILES];
static size_t exec_open_description_count;
static size_t exec_descriptor_count;

static ExecPayloadOpenDescriptionPtr PosixIO__exec_open_description(uint64_t id)
{
    for (size_t index = 0; index < exec_open_description_count; index++)
    {
        if (exec_open_descriptions[index].id == id)
        {
            return &exec_open_descriptions[index];
        }
    }
    return NULL;
}

static void PosixIO__reset_exec_inheritance(void)
{
    exec_open_description_count = 0;
    exec_descriptor_count = 0;
}

void PosixIO__abort_exec(void)
{
    for (size_t index = 0; index < exec_open_description_count; index++)
    {
        IOClientInheritance inheritance = {
            .local = exec_open_descriptions[index].local,
            .token = exec_open_descriptions[index].token,
            .flags = exec_open_descriptions[index].inheritance_flags,
            .service_length = exec_open_descriptions[index].service_length,
            .single_transfer = exec_open_descriptions[index].single_transfer != 0,
        };
        memcpy(inheritance.service_data, exec_open_descriptions[index].service_data, sizeof(inheritance.service_data));
        IOClient__abort_inheritance(&inheritance);
    }
    PosixIO__reset_exec_inheritance();
}

static IOStatus PosixIO__prepare_exec_description(PosixOpenDescriptionPtr description)
{
    if (PosixIO__exec_open_description(description->id) != NULL)
    {
        return IO_OK;
    }
    if (exec_open_description_count >= POSIX_MAX_OPEN_FILES)
    {
        return IO_REGISTER_FAILED;
    }
    match_result(
        IOClient__prepare_inheritance(description->handle),
        success(inheritance)
        {
            ExecPayloadOpenDescription record;
            record.id = description->id;
            record.status_flags = description->status_flags;
            record.path_offset = (ExecPayloadOffset)BString__cstr(&description->path);
            record.local = inheritance.local;
            record.token = inheritance.token;
            record.inheritance_flags = inheritance.flags;
            record.service_length = inheritance.service_length;
            record.single_transfer = inheritance.single_transfer ? 1 : 0;
            memcpy(record.service_data, inheritance.service_data, sizeof(record.service_data));
            exec_open_descriptions[exec_open_description_count++] = record;
            return IO_OK;
        },
        error(code, message)
        {
            return (IOStatus)code;
        }
    );
}

ExecPayloadPtr PosixIO__prepare_exec_payload(const char * path, const char * current_directory,
                                             char * const arguments[], char * const environment[])
{
    PosixIO__reset_exec_inheritance();
    for_each_map(
        PosixFileDescriptors,
        descriptors,
        index,
        descriptor,
        file,
        {
            if ((file.descriptor_flags & FD_CLOEXEC) != 0)
            {
                continue;
            }
            PosixOpenDescriptionPtr description;
            match_optional(
                PosixOpenDescriptions__get_ptr(&open_descriptions, file.description),
                some(found)
                {
                    description = found;
                },
                none()
                {
                    PosixIO__abort_exec();
                    errno = EBADF;
                    return NULL;
                }
            );
            if (description->handle < IO_SESSION_HANDLE_BASE)
            {
                continue;
            }
            IOStatus status = PosixIO__prepare_exec_description(description);
            if (status != IO_OK)
            {
                PosixIO__abort_exec();
                errno = PosixIO__errno(status);
                return NULL;
            }
            if (exec_descriptor_count >= POSIX_MAX_OPEN_FILES)
            {
                PosixIO__abort_exec();
                errno = EMFILE;
                return NULL;
            }
            exec_descriptors[exec_descriptor_count++] = (ExecPayloadDescriptor){
                .descriptor = descriptor,
                .descriptor_flags = file.descriptor_flags,
                .open_description = file.description,
            };
        }
    );
    ExecPayloadPtr payload =
        PosixExecPayload__new(path, current_directory, arguments, environment, exec_open_descriptions,
                              exec_open_description_count, exec_descriptors, exec_descriptor_count);
    if (payload == NULL)
    {
        PosixIO__abort_exec();
    }
    return payload;
}

IOStatus PosixIO__commit_exec(void)
{
    uint64_t pid = process_current_pid();
    if (pid == 0)
    {
        return IO_UNKNOWN_PID;
    }
    for (size_t index = 0; index < exec_open_description_count; index++)
    {
        IOClientInheritance inheritance = {
            .local = exec_open_descriptions[index].local,
            .token = exec_open_descriptions[index].token,
            .flags = exec_open_descriptions[index].inheritance_flags,
            .service_length = exec_open_descriptions[index].service_length,
            .single_transfer = exec_open_descriptions[index].single_transfer != 0,
        };
        memcpy(inheritance.service_data, exec_open_descriptions[index].service_data, sizeof(inheritance.service_data));
        IOStatus status = IOClient__commit_inheritance(&inheritance, pid);
        if (status != IO_OK)
        {
            return status;
        }
    }
    return IO_OK;
}

static IOClientInheritance PosixIO__exec_inheritance(ConstExecPayloadOpenDescriptionPtr description)
{
    IOClientInheritance inheritance = {
        .local = description->local,
        .token = description->token,
        .flags = description->inheritance_flags,
        .service_length = description->service_length,
        .single_transfer = description->single_transfer != 0,
    };
    memcpy(inheritance.service_data, description->service_data, sizeof(inheritance.service_data));
    return inheritance;
}

static Optional(PosixOpenDescription) PosixIO__claimed_exec_description(ConstExecPayloadPtr payload,
                                                                       ConstExecPayloadOpenDescriptionPtr description)
{
    IOClientInheritance inheritance = PosixIO__exec_inheritance(description);
    IOStatus status = IOClient__claim_inheritance(&inheritance);
    if (status != IO_OK)
    {
        return None(PosixOpenDescription);
    }
    const char * path = ExecPayload__string_at(payload, description->path_offset);
    PosixOpenDescription claimed = {
        .id = description->id,
        .handle = description->local,
        .path = path == NULL ? empty_bstr() : BString__new(path),
        .references = 0,
        .status_flags = description->status_flags,
    };
    return Some(PosixOpenDescription, claimed);
}

IOStatus PosixIO__claim_exec(ConstExecPayloadPtr payload)
{
    descriptors = EmptyMap(PosixFileDescriptors);
    open_descriptions = EmptyMap(PosixOpenDescriptions);
    next_description = 1;
    IOClient__reset_after_fork();
    IO__reset_after_fork();
    IOTrace__reset_after_fork();
    for (size_t index = 0; index < ExecPayload__open_description_count(payload); index++)
    {
        ConstExecPayloadOpenDescriptionPtr description = ExecPayload__open_description_at(payload, index);
        if (description == NULL)
        {
            return IO_UNKNOWN_BUFFER;
        }
        match_optional(
            PosixIO__claimed_exec_description(payload, description),
            some(claimed)
            {
                match_result(
                    PosixOpenDescriptions__set(&open_descriptions, claimed.id, claimed),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return IO_REGISTER_FAILED;
                    }
                );
                if (claimed.id >= next_description)
                {
                    next_description = claimed.id + 1;
                }
            },
            none()
            {
                return IO_PERMISSION_DENIED;
            }
        );
    }
    for (size_t index = 0; index < ExecPayload__descriptor_count(payload); index++)
    {
        ConstExecPayloadDescriptorPtr descriptor = ExecPayload__descriptor_at(payload, index);
        if (descriptor == NULL)
        {
            return IO_UNKNOWN_BUFFER;
        }
        match_optional(
            PosixOpenDescriptions__get_ptr(&open_descriptions, descriptor->open_description),
            some(description)
            {
                match_result(
                    PosixFileDescriptors__set(&descriptors, descriptor->descriptor,
                                              (PosixFileDescriptor){.description = descriptor->open_description,
                                                                    .descriptor_flags = descriptor->descriptor_flags}),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return IO_REGISTER_FAILED;
                    }
                );
                description->references++;
            },
            none()
            {
                return IO_UNKNOWN_HANDLE;
            }
        );
    }
    return IO_OK;
}

void PosixIO__abort_fork(void)
{
    for_each_map(
        PosixForkInheritances,
        fork_inheritances,
        index,
        id,
        inheritance,
        {
            IOClient__abort_inheritance(&inheritance);
        }
    );
    fork_inheritances = EmptyMap(PosixForkInheritances);
}

IOStatus PosixIO__prepare_fork(void)
{
    fork_inheritances = EmptyMap(PosixForkInheritances);
    for_each_map(
        PosixOpenDescriptions,
        open_descriptions,
        index,
        id,
        description,
        {
            if (description.handle < IO_SESSION_HANDLE_BASE)
            {
                continue;
            }
            match_result(
                IOClient__prepare_inheritance(description.handle),
                success(inheritance)
                {
                    match_result(
                        PosixForkInheritances__set(&fork_inheritances, description.id, inheritance),
                        success_do_nothing(),
                        error(code, message)
                        {
                            PosixIO__abort_fork();
                            return IO_REGISTER_FAILED;
                        }
                    );
                },
                error(code, message)
                {
                    PosixIO__abort_fork();
                    return (IOStatus)code;
                }
            );
        }
    );
    return IO_OK;
}

IOStatus PosixIO__commit_fork(uint64_t child_pid)
{
    for_each_map_ptr(
        PosixForkInheritances,
        &fork_inheritances,
        index,
        id,
        inheritance,
        {
            IOStatus status = IOClient__commit_inheritance(inheritance, child_pid);
            if (status != IO_OK)
            {
                return status;
            }
        }
    );
    return IO_OK;
}

IOStatus PosixIO__claim_fork(void)
{
    IOClient__reset_after_fork();
    IO__reset_after_fork();
    IOTrace__reset_after_fork();
    for_each_map_ptr(
        PosixForkInheritances,
        &fork_inheritances,
        index,
        id,
        inheritance,
        {
            IOStatus status = IOClient__claim_inheritance(inheritance);
            if (status != IO_OK)
            {
                return status;
            }
        }
    );
    fork_inheritances = EmptyMap(PosixForkInheritances);
    return IO_OK;
}

void PosixIO__finish_fork(void)
{
    fork_inheritances = EmptyMap(PosixForkInheritances);
}

typedef struct PosixStandardDescriptorFlags
{
    int standard_input;
    int standard_output;
    int standard_error;
} PosixStandardDescriptorFlags;

static PosixStandardDescriptorFlags standard_descriptor_flags;

typedef struct PosixStandardStatusFlags
{
    int standard_input;
    int standard_output;
    int standard_error;
} PosixStandardStatusFlags;

static PosixStandardStatusFlags standard_status_flags = {
    .standard_input = O_RDONLY,
    .standard_output = O_WRONLY,
    .standard_error = O_WRONLY,
};
static bool standard_descriptor_open[] = {true, true, true};

static int PosixIO__errno(IOStatus status)
{
    switch (status)
    {
    case IO_NO_DEVICE_FOUND:
    case IO_INVALID_PATH:
    case IO_PATH_UNMANAGED:
        return ENOENT;
    case IO_NOT_OPEN:
    case IO_UNKNOWN_HANDLE:
        return EBADF;
    case IO_WAIT_FOR_DATA:
    case IO_WOULD_BLOCK:
        return EAGAIN;
    case IO_INTERRUPTED:
        return EINTR;
    case IO_ALREADY_EXISTS:
        return EEXIST;
    case IO_BROKEN_PIPE:
        return EPIPE;
    case IO_OPERATION_NOT_IMPLEMENTED:
    case IO_OPERATION_NOT_SUPPORTED:
        return ENOSYS;
    case IO_PERMISSION_DENIED:
        return EACCES;
    default:
        return EIO;
    }
}

static bool PosixIO__is_standard_descriptor(int descriptor)
{
    return descriptor >= STDIN_FILENO && descriptor <= STDERR_FILENO;
}

static int PosixIO__get_descriptor_flags(int descriptor)
{
    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(file)
        {
            return file.descriptor_flags;
        },
        none_do_nothing()
    );
    switch (descriptor)
    {
    case STDIN_FILENO:
        if (!standard_descriptor_open[STDIN_FILENO])
        {
            break;
        }
        return standard_descriptor_flags.standard_input;
    case STDOUT_FILENO:
        if (!standard_descriptor_open[STDOUT_FILENO])
        {
            break;
        }
        return standard_descriptor_flags.standard_output;
    case STDERR_FILENO:
        if (!standard_descriptor_open[STDERR_FILENO])
        {
            break;
        }
        return standard_descriptor_flags.standard_error;
    default:
        errno = EBADF;
        return -1;
    }
    errno = EBADF;
    return -1;
}

static int PosixIO__set_descriptor_flags(int descriptor, int flags)
{
    int descriptor_flags = flags & FD_CLOEXEC;
    match_optional(
        PosixFileDescriptors__get_ptr(&descriptors, descriptor),
        some(file)
        {
            file->descriptor_flags = descriptor_flags;
            return 0;
        },
        none_do_nothing()
    );
    switch (descriptor)
    {
    case STDIN_FILENO:
        if (!standard_descriptor_open[STDIN_FILENO])
        {
            break;
        }
        standard_descriptor_flags.standard_input = descriptor_flags;
        return 0;
    case STDOUT_FILENO:
        if (!standard_descriptor_open[STDOUT_FILENO])
        {
            break;
        }
        standard_descriptor_flags.standard_output = descriptor_flags;
        return 0;
    case STDERR_FILENO:
        if (!standard_descriptor_open[STDERR_FILENO])
        {
            break;
        }
        standard_descriptor_flags.standard_error = descriptor_flags;
        return 0;
    default:
        errno = EBADF;
        return -1;
    }
    errno = EBADF;
    return -1;
}

static int PosixIO__get_status_flags(int descriptor)
{
    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(file)
        {
            match_optional(
                PosixOpenDescriptions__get(&open_descriptions, file.description),
                some(description)
                {
                    return description.status_flags;
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    switch (descriptor)
    {
    case STDIN_FILENO:
        if (standard_descriptor_open[STDIN_FILENO])
        {
            return standard_status_flags.standard_input;
        }
        break;
    case STDOUT_FILENO:
        if (standard_descriptor_open[STDOUT_FILENO])
        {
            return standard_status_flags.standard_output;
        }
        break;
    case STDERR_FILENO:
        if (standard_descriptor_open[STDERR_FILENO])
        {
            return standard_status_flags.standard_error;
        }
        break;
    }
    errno = EBADF;
    return -1;
}

static int PosixIO__set_status_flags(int descriptor, int flags)
{
    match_optional(
        PosixFileDescriptors__get_ptr(&descriptors, descriptor),
        some(file)
        {
            match_optional(
                PosixOpenDescriptions__get_ptr(&open_descriptions, file->description),
                some(description)
                {
                    description->status_flags =
                        (description->status_flags & O_ACCMODE) | (flags & (O_NONBLOCK | O_APPEND));
                    return 0;
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    int mutable_flags = flags & (O_NONBLOCK | O_APPEND);
    switch (descriptor)
    {
    case STDIN_FILENO:
        if (standard_descriptor_open[STDIN_FILENO])
        {
            standard_status_flags.standard_input = O_RDONLY | mutable_flags;
            return 0;
        }
        break;
    case STDOUT_FILENO:
        if (standard_descriptor_open[STDOUT_FILENO])
        {
            standard_status_flags.standard_output = O_WRONLY | mutable_flags;
            return 0;
        }
        break;
    case STDERR_FILENO:
        if (standard_descriptor_open[STDERR_FILENO])
        {
            standard_status_flags.standard_error = O_WRONLY | mutable_flags;
            return 0;
        }
        break;
    }
    errno = EBADF;
    return -1;
}

static Flags PosixIO__transfer_flags(int descriptor, bool writing)
{
    int status_flags = PosixIO__get_status_flags(descriptor);
    Flags transfer_flags = (status_flags & O_NONBLOCK) != 0 ? IO_TRANSFER_NONBLOCK : 0;
    if (writing && (status_flags & O_APPEND) != 0)
    {
        transfer_flags |= IO_TRANSFER_APPEND;
    }
    return transfer_flags;
}

IOHandle PosixIO__native_handle(int descriptor)
{
    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(file)
        {
            match_optional(
                PosixOpenDescriptions__get(&open_descriptions, file.description),
                some(description)
                {
                    return description.handle;
                },
                none_do_nothing()
            );
            errno = EBADF;
            return IO_UNKNOWN_HANDLE;
        },
        none_do_nothing()
    );

    if (PosixIO__is_standard_descriptor(descriptor) && standard_descriptor_open[descriptor])
    {
        return descriptor;
    }

    errno = EBADF;
    return IO_UNKNOWN_HANDLE;
}

Optional(BString) PosixIO__path(int descriptor)
{
    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(file)
        {
            match_optional(
                PosixOpenDescriptions__get(&open_descriptions, file.description),
                some(description)
                {
                    return Some(BString, description.path);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return None(BString);
}

static bool PosixIO__is_terminal_path(ConstBStringPtr path)
{
    return BString__equals(path, &bstr(":tty:")) || BString__equals(path, &bstr(":console:"));
}

Optional(BString) PosixIO__terminal_path(int descriptor)
{
    if (PosixIO__native_handle(descriptor) < 0)
    {
        return None(BString);
    }
    match_optional(
        PosixIO__path(descriptor),
        some(path)
        {
            if (PosixIO__is_terminal_path(&path))
            {
                return Some(BString, path);
            }
            errno = ENOTTY;
            return None(BString);
        },
        none_do_nothing()
    );
    match_optional(
        IO__terminal_path(descriptor),
        some(path)
        {
            return Some(BString, path);
        },
        none()
        {
            errno = ENOTTY;
            return None(BString);
        }
    );
}

static bool PosixIO__descriptor_is_open(int descriptor)
{
    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(handle)
        {
            return true;
        },
        none_do_nothing()
    );
    return PosixIO__is_standard_descriptor(descriptor) && standard_descriptor_open[descriptor];
}

static Optional(uint64_t) PosixIO__create_description(IOHandle handle, ConstBStringPtr path, int status_flags)
{
    if (next_description == UINT64_MAX)
    {
        return None(uint64_t);
    }
    PosixOpenDescription description = {
        .id = next_description++,
        .handle = handle,
        .path = *path,
        .references = 1,
        .status_flags = status_flags,
    };
    match_result(
        PosixOpenDescriptions__set(&open_descriptions, description.id, description),
        success(index)
        {
            return Some(uint64_t, description.id);
        },
        error(code, message)
        {
            return None(uint64_t);
        }
    );
}

static int PosixIO__allocate_description_descriptor(uint64_t description, int first_descriptor)
{
    int descriptor = first_descriptor;
    while (PosixIO__descriptor_is_open(descriptor))
    {
        descriptor++;
    }

    match_result(
        PosixFileDescriptors__set(&descriptors, descriptor, (PosixFileDescriptor){.description = description}),
        success(index)
        {
            return descriptor;
        },
        error(code, message)
        {
            errno = EMFILE;
            return -1;
        }
    );
}

static int PosixIO__allocate_descriptor(IOHandle handle, ConstBStringPtr path, int status_flags)
{
    match_optional(
        PosixIO__create_description(handle, path, status_flags),
        some(description)
        {
            int descriptor = PosixIO__allocate_description_descriptor(description, POSIX_FIRST_FILE_DESCRIPTOR);
            if (descriptor < 0)
            {
                PosixOpenDescriptions__delete(&open_descriptions, description);
            }
            return descriptor;
        },
        none()
        {
            errno = EMFILE;
            return -1;
        }
    );
}

static int PosixIO__duplicate_to(int source, int target);

static Optional(uint64_t) PosixIO__description_for_dup(int descriptor)
{
    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(file)
        {
            return Some(uint64_t, file.description);
        },
        none_do_nothing()
    );
    if (!PosixIO__is_standard_descriptor(descriptor) || !standard_descriptor_open[descriptor])
    {
        return None(uint64_t);
    }
    BString path = empty_bstr();
    match_optional(
        IO__terminal_path(descriptor),
        some(terminal_path)
        {
            path = terminal_path;
        },
        none_do_nothing()
    );
    match_optional(
        PosixIO__create_description(descriptor, &path, PosixIO__get_status_flags(descriptor)),
        some(description)
        {
            match_result(
                PosixFileDescriptors__set(&descriptors, descriptor,
                                          (PosixFileDescriptor){.description = description,
                                                                .descriptor_flags = PosixIO__get_descriptor_flags(
                                                                    descriptor)}),
                success(index)
                {
                    return Some(uint64_t, description);
                },
                error(code, message)
                {
                    PosixOpenDescriptions__delete(&open_descriptions, description);
                    return None(uint64_t);
                }
            );
        },
        none()
        {
            return None(uint64_t);
        }
    );
}

int pipe(int pipe_descriptors[2])
{
    if (pipe_descriptors == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    if (PosixFileDescriptors__len(&descriptors) > PosixFileDescriptors__max_size - 2)
    {
        errno = EMFILE;
        return -1;
    }

    IOPipe handles;
    match_result(
        IO__pipe(),
        success(value)
        {
            handles = value;
        },
        error(code, message)
        {
            errno = PosixIO__errno((IOStatus)code);
            return -1;
        }
    );

    int reader = PosixIO__allocate_descriptor(handles.read, &empty_bstr(), O_RDONLY);
    int writer = PosixIO__allocate_descriptor(handles.write, &empty_bstr(), O_WRONLY);
    if (reader < 0 || writer < 0)
    {
        if (reader >= 0)
        {
            close(reader);
        }
        else
        {
            IO__close(handles.read);
        }
        IO__close(handles.write);
        errno = EMFILE;
        return -1;
    }
    pipe_descriptors[0] = reader;
    pipe_descriptors[1] = writer;
    return 0;
}

int open(const char * path, int flags, ...)
{
    Flags io_flags;
    switch (flags & O_ACCMODE)
    {
    case O_RDONLY:
        io_flags = IO_OPEN_READ;
        break;
    case O_WRONLY:
        io_flags = IO_OPEN_WRITE;
        break;
    case O_RDWR:
        io_flags = IO_OPEN_READ | IO_OPEN_WRITE;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    if ((flags & O_CREAT) != 0)
    {
        io_flags |= IO_OPEN_CREATE;
        if ((flags & O_EXCL) != 0)
        {
            io_flags |= IO_OPEN_EXCLUSIVE;
        }
    }

    BString absolute_path;
    if (PosixPath__absolute(path, &absolute_path) != 0)
    {
        return -1;
    }
    if ((flags & O_TRUNC) != 0 && (io_flags & IO_OPEN_WRITE) != 0)
    {
        match_result(
            IO__remove_file(&absolute_path),
            success_do_nothing(),
            error(code, message)
            {
                if (code != IO_INVALID_PATH)
                {
                    errno = PosixIO__errno((IOStatus)code);
                    return -1;
                }
            }
        );
    }
    IOHandle handle;
    match_result(
        IO__open(&absolute_path, io_flags),
        success(opened)
        {
            handle = opened;
        },
        error(code, message)
        {
            errno = PosixIO__errno((IOStatus)code);
            return -1;
        }
    );

    int status_flags = flags & (O_ACCMODE | O_NONBLOCK | O_APPEND);
    int descriptor = PosixIO__allocate_descriptor(handle, &absolute_path, status_flags);
    if (descriptor < 0)
    {
        IO__close(handle);
    }
    return descriptor;
}

int fcntl(int descriptor, int command, ...)
{
    if (command == F_DUPFD)
    {
        va_list arguments;
        va_start(arguments, command);
        int minimum = va_arg(arguments, int);
        va_end(arguments);
        if (minimum < 0)
        {
            errno = EINVAL;
            return -1;
        }
        int target = minimum;
        while (PosixIO__descriptor_is_open(target))
        {
            target++;
        }
        return PosixIO__duplicate_to(descriptor, target);
    }
    if (command == F_GETFD)
    {
        return PosixIO__get_descriptor_flags(descriptor);
    }
    if (command == F_SETFD)
    {
        va_list arguments;
        va_start(arguments, command);
        int flags = va_arg(arguments, int);
        va_end(arguments);
        return PosixIO__set_descriptor_flags(descriptor, flags);
    }
    if (command == F_GETFL)
    {
        return PosixIO__get_status_flags(descriptor);
    }
    if (command == F_SETFL)
    {
        va_list arguments;
        va_start(arguments, command);
        int flags = va_arg(arguments, int);
        va_end(arguments);
        return PosixIO__set_status_flags(descriptor, flags);
    }

    errno = EINVAL;
    return -1;
}

ssize_t read(int descriptor, void * buffer, size_t size)
{
    if (buffer == NULL && size != 0)
    {
        errno = EFAULT;
        return -1;
    }

    IOHandle handle = PosixIO__native_handle(descriptor);
    if (handle < 0)
    {
        return -1;
    }

    match_result(
        IO__read_bytes_with_flags(handle, buffer, size, PosixIO__transfer_flags(descriptor, false)),
        success(count)
        {
            return count;
        },
        error(code, message)
        {
            if (code == IO_EOF)
            {
                return 0;
            }
            errno = PosixIO__errno((IOStatus)code);
            return -1;
        }
    );
    errno = EIO;
    return -1;
}

ssize_t write(int descriptor, const void * buffer, size_t size)
{
    if (buffer == NULL && size != 0)
    {
        errno = EFAULT;
        return -1;
    }

    IOHandle handle = PosixIO__native_handle(descriptor);
    if (handle < 0)
    {
        return -1;
    }

    match_result(
        IO__write_bytes_with_flags(handle, buffer, size, PosixIO__transfer_flags(descriptor, true)),
        success(count)
        {
            return count;
        },
        error(code, message)
        {
            errno = PosixIO__errno((IOStatus)code);
            return -1;
        }
    );
    errno = EIO;
    return -1;
}

int close(int descriptor)
{
    IOHandle handle = PosixIO__native_handle(descriptor);
    if (handle < 0)
    {
        return -1;
    }

    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(file)
        {
            PosixOpenDescriptionPtr description;
            match_optional(
                PosixOpenDescriptions__get_ptr(&open_descriptions, file.description),
                some(found)
                {
                    description = found;
                },
                none()
                {
                    errno = EBADF;
                    return -1;
                }
            );
            if (description->references == 1)
            {
                match_result(
                    IO__close(handle),
                    success_do_nothing(),
                    error(code, message)
                    {
                        errno = PosixIO__errno((IOStatus)code);
                        return -1;
                    }
                );
                PosixOpenDescriptions__delete(&open_descriptions, file.description);
            }
            else
            {
                description->references--;
            }
            match_result(
                PosixFileDescriptors__delete(&descriptors, descriptor),
                success_do_nothing(),
                error(code, message)
                {
                    errno = EIO;
                    return -1;
                }
            );
            if (PosixIO__is_standard_descriptor(descriptor))
            {
                standard_descriptor_open[descriptor] = false;
            }
            return 0;
        },
        none_do_nothing()
    );

    match_result(
        IO__close(handle),
        success_do_nothing(),
        error(code, message)
        {
            errno = PosixIO__errno((IOStatus)code);
            return -1;
        }
    );
    standard_descriptor_open[descriptor] = false;
    return 0;
}

static int PosixIO__duplicate_to(int source, int target)
{
    uint64_t description_id;
    match_optional(
        PosixIO__description_for_dup(source),
        some(found)
        {
            description_id = found;
        },
        none()
        {
            errno = EBADF;
            return -1;
        }
    );
    if (source == target)
    {
        return target;
    }
    if (target < 0)
    {
        errno = EBADF;
        return -1;
    }
    if (PosixIO__descriptor_is_open(target) && close(target) < 0)
    {
        return -1;
    }
    match_result(
        PosixFileDescriptors__set(&descriptors, target, (PosixFileDescriptor){.description = description_id}),
        success(index)
        {
            match_optional(
                PosixOpenDescriptions__get_ptr(&open_descriptions, description_id),
                some(description)
                {
                    description->references++;
                    if (PosixIO__is_standard_descriptor(target))
                    {
                        standard_descriptor_open[target] = true;
                    }
                    return target;
                },
                none_do_nothing()
            );
        },
        error(code, message)
        {
            errno = EMFILE;
            return -1;
        }
    );
    errno = EIO;
    return -1;
}

int dup(int descriptor)
{
    int target = POSIX_FIRST_FILE_DESCRIPTOR;
    while (PosixIO__descriptor_is_open(target))
    {
        target++;
    }
    return PosixIO__duplicate_to(descriptor, target);
}

int dup2(int descriptor, int target)
{
    return PosixIO__duplicate_to(descriptor, target);
}

int fsync(int descriptor)
{
    if (PosixIO__native_handle(descriptor) < 0)
    {
        return -1;
    }
    return 0;
}

int pledge(const char * promises, const char * paths)
{
    return 0;
}
