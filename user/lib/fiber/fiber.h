#ifndef __USER_LIB_FIBER_FIBER_H
#define __USER_LIB_FIBER_FIBER_H

#include <stdint.h>

#include "../../user.h"
#include "../channel/signal.h"
#include "../../../common/types/address.h"
#include "../../../common/types/result.h"
#include "../../../common/types/optional.h"
#include "../../../common_includes/syscalls/types.h"

typedef struct Fiber Fiber;
typedef Fiber * FiberPtr;
typedef struct FiberScheduler FiberScheduler;
typedef FiberScheduler * FiberSchedulerPtr;

typedef enum FiberCode
{
    FIBER_OK = 0,
    FIBER_NO_READY_FIBER = -1,
    FIBER_QUEUE_FULL = -2,
    FIBER_WAIT_NOT_FOUND = -3,
    FIBER_NO_CURRENT_FIBER = -4,
    FIBER_INVALID_STATE = -5,
    FIBER_ALLOCATION_FAILED = -6,
    FIBER_INVALID_STACK_SIZE = -7,
    FIBER_LIMIT_REACHED = -8,
} FiberCode;

typedef enum FiberState
{
    FIBER_CREATED = 1,
    FIBER_READY = 2,
    FIBER_RUNNING = 3,
    FIBER_WAITING = 4,
    FIBER_DONE = 5,
} FiberState;

ResultDef(FiberCode);

#include "arch/fiber_context.h"

OptionalDef(FiberContextPtr);

typedef enum FiberWaitKind
{
    FIBER_WAIT_NONE = 0,
    FIBER_WAIT_REQUEST = 1,
    FIBER_WAIT_BLOCK_REQUEST = 1,
    FIBER_WAIT_SERVICE_EVENT = 2,
    FIBER_WAIT_USER = 3,
    FIBER_WAIT_CHANNEL = 4,
} FiberWaitKind;

typedef struct FiberWaitKey
{
    FiberWaitKind kind;
    uint64_t id;
} FiberWaitKey;

typedef FiberWaitKey * FiberWaitKeyPtr;
typedef const FiberWaitKey * ConstFiberWaitKeyPtr;

typedef struct FiberWakeValue
{
    FiberWaitKey key;
    FiberCode status;
    uint64_t value;
} FiberWakeValue;

typedef const FiberWakeValue * ConstFiberWakeValuePtr;

typedef Result(FiberCode) (*FiberEntry)(FiberPtr self, Anything argument);

OptionalDef(FiberWaitKey);
OptionalDef(FiberWakeValue);
OptionalDef(FiberPtr);
ResultDef(FiberPtr);
ResultDef(FiberWakeValue);

#define FIBER_DEFAULT_STACK_SIZE (1024 * 1024 * 10)
#define FIBER_STACK_ALIGNMENT 16

FiberWaitKey FiberWaitKey__new(FiberWaitKind kind, uint64_t id);
FiberWaitKey FiberWaitKey__none(void);
FiberWaitKey FiberWaitKey__request(uint64_t request_id);
FiberWaitKey FiberWaitKey__block_request(uint64_t request_id);
FiberWaitKey FiberWaitKey__channel_signal(ChannelSignalPtr channel);
bool FiberWaitKey__equals(FiberWaitKeyPtr self, FiberWaitKeyPtr other);

FiberWakeValue FiberWakeValue__new(ConstFiberWaitKeyPtr key, FiberCode status, uint64_t value);

Result(FiberPtr) Fiber__heap_new(FiberEntry entry, Anything argument);
Result(FiberPtr) Fiber__heap_new_with_stack_size(FiberEntry entry, Anything argument, size_t stack_size);
FiberState Fiber__state(FiberPtr self);
Address Fiber__stack_base(FiberPtr self);
Address Fiber__stack_top(FiberPtr self);
size_t Fiber__stack_size(FiberPtr self);
Optional(FiberWaitKey) Fiber__wait_key(FiberPtr self);
Optional(FiberWakeValue) Fiber__wake_value(FiberPtr self);
Result(FiberWakeValue) Fiber__take_wake_value(FiberPtr self);
Result(FiberCode) Fiber__mark_ready(FiberPtr self);
Result(FiberCode) Fiber__mark_running(FiberPtr self);
Result(FiberCode) Fiber__mark_waiting(FiberPtr self, ConstFiberWaitKeyPtr key);
Result(FiberCode) Fiber__set_wake_value(FiberPtr self, ConstFiberWakeValuePtr value);
Result(FiberCode) Fiber__mark_done(FiberPtr self);
Result(FiberCode) Fiber__resume(FiberPtr self, FiberContextPtr scheduler_context);
Result(FiberCode) Fiber__yield_to_scheduler(FiberPtr self);
void Fiber__dispose(FiberPtr self);

#endif
