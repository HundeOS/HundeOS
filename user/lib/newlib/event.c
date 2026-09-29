#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/event.h>
#include <sys/stat.h>

#include "../sys/io.h"
#include "posix_io.h"

enum
{
    POSIX_KQUEUE_DESCRIPTOR = 1024,
    POSIX_KQUEUE_WATCH_CAPACITY = IO_FILE_WATCH_CAPACITY,
};

typedef struct PosixKqueueWatch
{
    int descriptor;
    IOHandle handle;
    uint64_t generation;
    void * read_context;
    void * vnode_context;
    Flags vnode_flags;
    bool read;
    bool vnode;
} PosixKqueueWatch;

static PosixKqueueWatch watches[POSIX_KQUEUE_WATCH_CAPACITY];
static size_t watch_count;
static bool queue_open;

static PosixKqueueWatch * PosixKqueue__find(int descriptor)
{
    for (size_t index = 0; index < watch_count; index++)
    {
        if (watches[index].descriptor == descriptor)
        {
            return &watches[index];
        }
    }
    return NULL;
}

static PosixKqueueWatch * PosixKqueue__add(int descriptor)
{
    PosixKqueueWatch * watch = PosixKqueue__find(descriptor);
    if (watch == NULL && watch_count == POSIX_KQUEUE_WATCH_CAPACITY)
    {
        errno = ENOSPC;
        return NULL;
    }
    IOHandle handle = PosixIO__native_handle(descriptor);
    if (handle < 0)
    {
        return NULL;
    }
    IOWatchResult reply = IO__watch_file(handle);
    if (reply.status != IO_OK)
    {
        errno = EIO;
        return NULL;
    }
    if (watch == NULL)
    {
        watch = &watches[watch_count++];
        *watch = (PosixKqueueWatch){.descriptor = descriptor};
    }
    watch->handle = handle;
    watch->generation = reply.change.generation;
    return watch;
}

int kqueue(void)
{
    watch_count = 0;
    queue_open = true;
    return POSIX_KQUEUE_DESCRIPTOR;
}

static int PosixKqueue__register(const struct kevent * changes, int change_count)
{
    for (int index = 0; index < change_count; index++)
    {
        if ((changes[index].flags & EV_ADD) == 0)
        {
            errno = EOPNOTSUPP;
            return -1;
        }
        PosixKqueueWatch * watch = PosixKqueue__add((int)changes[index].ident);
        if (watch == NULL)
        {
            return -1;
        }
        if (changes[index].filter == EVFILT_READ)
        {
            watch->read = true;
            watch->read_context = changes[index].udata;
        }
        else if (changes[index].filter == EVFILT_VNODE)
        {
            watch->vnode = true;
            watch->vnode_context = changes[index].udata;
            watch->vnode_flags = changes[index].fflags;
        }
        else
        {
            errno = EOPNOTSUPP;
            return -1;
        }
    }
    return 0;
}

static int PosixKqueue__wait(struct kevent * event)
{
    while (true)
    {
        IOFileWatch pending[POSIX_KQUEUE_WATCH_CAPACITY];
        for (size_t index = 0; index < watch_count; index++)
        {
            pending[index] = (IOFileWatch){.handle = watches[index].handle, .generation = watches[index].generation};
        }
        IOWatchResult reply = IO__wait_file_change(pending, watch_count);
        if (reply.status != IO_OK)
        {
            errno = reply.status == IO_INTERRUPTED ? EINTR : EIO;
            return -1;
        }
        PosixKqueueWatch * watch = NULL;
        for (size_t index = 0; index < watch_count; index++)
        {
            if (watches[index].handle == reply.change.handle)
            {
                watch = &watches[index];
                break;
            }
        }
        if (watch == NULL)
        {
            errno = EIO;
            return -1;
        }
        watch->generation = reply.change.generation;
        Flags vnode_events = reply.change.events &
                             (IO_FILE_CHANGE_DELETE | IO_FILE_CHANGE_RENAME | IO_FILE_CHANGE_TRUNCATE);
        if (vnode_events != 0 && watch->vnode)
        {
            unsigned int flags = 0;
            flags |= (vnode_events & IO_FILE_CHANGE_DELETE) != 0 ? NOTE_DELETE : 0;
            flags |= (vnode_events & IO_FILE_CHANGE_RENAME) != 0 ? NOTE_RENAME : 0;
            flags |= (vnode_events & IO_FILE_CHANGE_TRUNCATE) != 0 ? NOTE_TRUNCATE : 0;
            *event = (struct kevent){.ident = (uintptr_t)watch->descriptor,
                                    .filter = EVFILT_VNODE,
                                    .fflags = flags & watch->vnode_flags,
                                    .udata = watch->vnode_context};
            return 1;
        }
        if (watch->read)
        {
            *event = (struct kevent){.ident = (uintptr_t)watch->descriptor,
                                    .filter = EVFILT_READ,
                                    .udata = watch->read_context};
            return 1;
        }
    }
}

int kevent(int queue, const struct kevent * changes, int change_count, struct kevent * events, int event_count,
           const struct timespec * timeout)
{
    if (!queue_open || queue != POSIX_KQUEUE_DESCRIPTOR)
    {
        errno = EBADF;
        return -1;
    }
    if (change_count < 0 || event_count < 0 || (change_count > 0 && changes == NULL) ||
        (event_count > 0 && events == NULL))
    {
        errno = EINVAL;
        return -1;
    }
    if (change_count > 0 && PosixKqueue__register(changes, change_count) != 0)
    {
        return -1;
    }
    if (event_count == 0)
    {
        return 0;
    }
    if (watch_count == 0)
    {
        errno = EINVAL;
        return -1;
    }
    return PosixKqueue__wait(events);
}
