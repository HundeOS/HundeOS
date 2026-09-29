#ifndef __USER_LIB_NEWLIB_SYS_EVENT_H
#define __USER_LIB_NEWLIB_SYS_EVENT_H

#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define EVFILT_READ (-1)
#define EVFILT_VNODE (-4)

#define EV_ADD 0x0001
#define EV_ENABLE 0x0004
#define EV_CLEAR 0x0020

#define NOTE_DELETE 0x0001
#define NOTE_WRITE 0x0002
#define NOTE_EXTEND 0x0004
#define NOTE_ATTRIB 0x0008
#define NOTE_LINK 0x0010
#define NOTE_RENAME 0x0020
#define NOTE_TRUNCATE 0x0080

struct kevent
{
    uintptr_t ident;
    short filter;
    unsigned short flags;
    unsigned int fflags;
    int64_t data;
    void * udata;
};

#define EV_SET(event, identifier, event_filter, event_flags, filter_flags, event_data, user_data)                     \
    (*(event) = (struct kevent){.ident = (identifier),                                                                 \
                                .filter = (event_filter),                                                              \
                                .flags = (event_flags),                                                                \
                                .fflags = (filter_flags),                                                              \
                                .data = (event_data),                                                                  \
                                .udata = (user_data)})

int kqueue(void);
int kevent(int queue, const struct kevent * changes, int change_count, struct kevent * events, int event_count,
           const struct timespec * timeout);

#endif
