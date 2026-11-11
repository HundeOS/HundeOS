#ifndef HUNDE_SYS_THREAD_LOCAL_H
#define HUNDE_SYS_THREAD_LOCAL_H

#include "syscalls/process.h"
#include "../../../common/types/address.h"

typedef int64_t ThreadLocalStatus;

typedef struct ThreadLocalBlock
{
    Address allocation;
    Address thread_pointer;
} ThreadLocalBlock;

typedef const ThreadLocalBlock * ConstThreadLocalBlockPtr;
ResultDef(ThreadLocalBlock);

Result(ThreadLocalBlock) ThreadLocal__new(ConstThreadLocalImagePtr image);
Result(ThreadLocalBlock) ThreadLocal__new_thread(void);
ThreadLocalStatus ThreadLocal__initialize(void);
ThreadLocalStatus ThreadLocal__release(ConstThreadLocalBlockPtr block);
ThreadLocalStatus ThreadLocal__destroy_current(void);
Address ThreadLocal__libc_state(void);

#endif
