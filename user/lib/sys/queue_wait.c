#include "queue_wait.h"
#include "../types/fixed_array.h"
#include "ipc/ipc_platform.h"
#include "ipc/queue_wait_control.h"
#include "boot_info.h"
#include "syscall.h"
#include "syscalls/service.h"

typedef struct QueueWaitSource
{
    QueueWaitKind kind;
    QueueHandleID handle;
    union
    {
        Queue queue;
        QueueArrivalsPtr arrivals;
    };
} QueueWaitSource;
typedef QueueWaitSource * QueueWaitSourcePtr;
OptionalDef(QueueWaitSource);

static bool QueueWaitSource__equals(void * first, void * second)
{
    QueueWaitSourcePtr self = first;
    QueueWaitSourcePtr other = second;
    return self->kind == other->kind && self->handle == other->handle;
}

FixedArrayDefine(QueueWaitSource, QUEUE_WAIT_LIMIT, QueueWaitSources, QueueWaitSource__equals);
FixedArrayBodyDefine(QueueWaitSources);
FixedArrayDefine(QueueWaitSnapshot, QUEUE_WAIT_LIMIT, QueueWaitSnapshots, (EqualFunction)QueueWaitSnapshot__equals);
FixedArrayBodyDefine(QueueWaitSnapshots);

static QueueWaitSources sources = EmptyArray(QueueWaitSources);
static QueueWaitSnapshots snapshots = EmptyArray(QueueWaitSnapshots);
static bool prepared;
static QueueWaitWakeGeneration prepared_wake_generation;
static Optional(QueueWaitControlPtr) wait_control = None(QueueWaitControlPtr);
static bool registration_attempted;

static void QueueWait__register_control(void)
{
    if (registration_attempted)
    {
        return;
    }
    registration_attempted = true;
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            IpcPlatform__configure_wait(info.wait_mode);
        },
        error(code, message)
        {
            IpcPlatform__configure_wait(BOOT_WAIT_SPIN);
        }
    );
    SystemCallResult address = syscall(SERVICE_CALL, SERVICE_QUEUE_WAIT_CONTROL, 0, 0, 0, 0);
    if ((int64_t)address > 0)
    {
        wait_control = Some(QueueWaitControlPtr, (QueueWaitControlPtr)address);
        IODiagnostics__attach(&((QueueWaitControlPtr)address)->diagnostics);
    }
}

static void QueueWait__clear_control(void)
{
    match_optional(wait_control,
        some(control)
        {
            QueueWaitControl__clear(control);
        },
        none_do_nothing()
    );
}

static void QueueWait__complete_control(void)
{
    match_optional(
        wait_control,
        some(control)
        {
            QueueWaitControl__complete_wait(control, IpcPlatform__monotonic_usec());
        },
        none_do_nothing()
    );
}

static Result(QueueCode) QueueWait__arm_control(void)
{
    match_optional(
        wait_control,
        some(control)
        {
            QueueWaitControl__begin(control);
            for_each_fa(
                QueueWaitSnapshots,
                snapshots,
                index,
                observed,
                {
                    match_result(
                        QueueWaitControl__add(control, &observed),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(QueueCode, message, QUEUE_INVALID);
                        }
                    );
                }
            );
            QueueWaitControl__commit(control, IpcPlatform__monotonic_usec(), QUEUE_WAIT_NO_DEADLINE,
                                     prepared_wake_generation);
        },
        none_do_nothing()
    );
    return Success(QueueCode, QUEUE_OK);
}

static QueueWaitSource QueueWaitSource__new_queue(QueuePtr queue)
{
    return (QueueWaitSource){.kind = QUEUE_WAIT_CONNECTION, .handle = queue->connection, .queue = *queue};
}

static QueueWaitSource QueueWaitSource__new_arrivals(QueueArrivalsPtr arrivals)
{
    return (QueueWaitSource){.kind = QUEUE_WAIT_ARRIVALS, .handle = arrivals->endpoint, .arrivals = arrivals};
}

static QueueWaitSource QueueWaitSource__new_key(QueueConnectionID connection)
{
    return (QueueWaitSource){.kind = QUEUE_WAIT_CONNECTION, .handle = connection};
}

static Result(QueueCode) QueueWaitSource__remember(QueueWaitSourcePtr self)
{
    QueueWait__register_control();
    QueueWait__clear_control();
    prepared = false;
    match_optional(
        QueueWaitSources__find_item(&sources, *self),
        some(index)
        {
            match_result(
                QueueWaitSources__set(&sources, index, *self),
                success(code)
                {
                    return Success(QueueCode, QUEUE_OK);
                },
                error(code, message)
                {
                    return Error(QueueCode, message, QUEUE_FULL);
                }
            );
        },
        none_do_nothing()
    );
    match_result(
        QueueWaitSources__push(&sources, *self),
        success(code)
        {
            return Success(QueueCode, QUEUE_OK);
        },
        error(code, message)
        {
            return Error(QueueCode, message, QUEUE_FULL);
        }
    );
    return Error(QueueCode, bstr("Queue wait registry full"), QUEUE_FULL);
}

Result(QueueCode) QueueWait__remember_queue(QueuePtr queue)
{
    QueueWaitSource source = QueueWaitSource__new_queue(queue);
    return QueueWaitSource__remember(&source);
}

Result(QueueCode) QueueWait__remember_arrivals(QueueArrivalsPtr arrivals)
{
    QueueWaitSource source = QueueWaitSource__new_arrivals(arrivals);
    return QueueWaitSource__remember(&source);
}

Result(QueueCode) QueueWait__forget(QueueConnectionID connection)
{
    QueueWait__clear_control();
    QueueWaitSource source = QueueWaitSource__new_key(connection);
    prepared = false;
    match_result(
        QueueWaitSources__remove(&sources, source),
        success(code)
        {
            return Success(QueueCode, QUEUE_OK);
        },
        error(code, message)
        {
            // A server may reject an arrival without ever attaching it locally.
            if (code == ARRAY_ITEM_NOT_FOUND)
            {
                return Success(QueueCode, QUEUE_OK);
            }
            return Error(QueueCode, message, QUEUE_INVALID);
        }
    );
    return Error(QueueCode, bstr("Unknown queue wait source"), QUEUE_INVALID);
}

static QueueWaitSnapshot QueueWaitSource__snapshot(QueueWaitSourcePtr self)
{
    return self->kind == QUEUE_WAIT_CONNECTION ? QueueWaitSnapshot__new_connection(&self->queue)
                                               : QueueWaitSnapshot__new_arrivals(self->arrivals);
}

void Queue__prepare_wait(void)
{
    QueueWait__clear_control();
    while (QueueWaitSnapshots__len(&snapshots) != 0)
    {
        match_optional(QueueWaitSnapshots__pop(&snapshots),
            some_do_nothing(),
            none_do_nothing()
        );
    }
    prepared = true;
    match_optional(
        wait_control,
        some(control)
        {
            prepared_wake_generation = QueueWaitControl__wake_generation(control);
        },
        none()
        {
            prepared_wake_generation = 0;
        }
    );
    for_each_fa(
        QueueWaitSources,
        sources,
        index,
        source,
        {
            match_result(
                QueueWaitSnapshots__push(&snapshots, QueueWaitSource__snapshot(&source)),
                success_do_nothing(),
                error(code, message)
                {
                    prepared = false;
                }
            );
        }
    );
}

static bool QueueWait__changed(void)
{
    match_optional(
        wait_control,
        some(control)
        {
            if (QueueWaitControl__woken(control, prepared_wake_generation))
            {
                return true;
            }
        },
        none_do_nothing()
    );
    for_each_fa(
        QueueWaitSources,
        sources,
        index,
        source,
        {
            QueueWaitSnapshot current = QueueWaitSource__snapshot(&source);
            match_optional(
                QueueWaitSnapshots__at(&snapshots, index),
                some(observed)
                {
                    if (!QueueWaitSnapshot__equals(&observed, &current))
                    {
                        return true;
                    }
                },
                none()
                {
                    return true;
                }
            );
        }
    );
    return false;
}

void Queue__wait_event(void)
{
    if (prepared && !QueueWait__changed() && QueueWaitSources__len(&sources) != 0)
    {
        if (unwrap_result(QueueWait__arm_control(), QUEUE_INVALID) != QUEUE_OK)
        {
            QueueWait__clear_control();
            return;
        }
        IODiagnostics__record(IO_DIAGNOSTIC_WAIT_ENTRIES, 1);
        uint64_t unchanged_wakes = 0;
        while (!QueueWait__changed())
        {
            IpcPlatform__wait_event();
            if (!QueueWait__changed())
            {
                unchanged_wakes++;
            }
        }
        if (unchanged_wakes != 0)
        {
            IODiagnostics__record(IO_DIAGNOSTIC_UNCHANGED_WAKES, unchanged_wakes);
        }
        QueueWait__complete_control();
    }
    // The next dispatch scans authoritative state after this snapshot, even for
    // callers that prepare only once before entering their event loop.
    Queue__prepare_wait();
}

static void QueueWaitSource__wait(QueueWaitSourcePtr self, ConstQueueWaitSnapshotPtr observed)
{
    QueueWaitSnapshot current = QueueWaitSource__snapshot(self);
    if (QueueWaitSnapshot__equals(observed, &current))
    {
        match_optional(
            wait_control,
            some(control)
            {
                QueueWaitControl__begin(control);
                match_result(
                    QueueWaitControl__add(control, observed),
                    success(code)
                    {
                        QueueWaitControl__commit(control, IpcPlatform__monotonic_usec(), QUEUE_WAIT_NO_DEADLINE,
                                                 QueueWaitControl__wake_generation(control));
                    },
                    error(code, message)
                    {
                        QueueWaitControl__clear(control);
                    }
                );
            },
            none_do_nothing()
        );
        IODiagnostics__record(IO_DIAGNOSTIC_WAIT_ENTRIES, 1);
        uint64_t unchanged_wakes = 0;
        while (true)
        {
            IpcPlatform__wait_event();
            QueueWaitSnapshot after = QueueWaitSource__snapshot(self);
            if (!QueueWaitSnapshot__equals(observed, &after))
            {
                break;
            }
            unchanged_wakes++;
        }
        if (unchanged_wakes != 0)
        {
            IODiagnostics__record(IO_DIAGNOSTIC_UNCHANGED_WAKES, unchanged_wakes);
        }
        QueueWait__complete_control();
    }
}

void Queue__wait_readable(QueuePtr self)
{
    QueueWaitSource source = QueueWaitSource__new_queue(self);
    while (true)
    {
        QueueWaitSnapshot observed = QueueWaitSource__snapshot(&source);
        if (observed.closed != 0 || (observed.accepted != 0 && observed.produced != self->read))
        {
            return;
        }
        QueueWaitSource__wait(&source, &observed);
    }
}

void Queue__wait_writable(QueuePtr self)
{
    QueueWaitSource source = QueueWaitSource__new_queue(self);
    while (true)
    {
        QueueWaitSnapshot observed = QueueWaitSource__snapshot(&source);
        if (observed.closed != 0 || (observed.accepted != 0 && self->written - observed.consumed != QUEUE_CAPACITY))
        {
            return;
        }
        QueueWaitSource__wait(&source, &observed);
    }
}

void QueueArrivals__wait(QueueArrivalsPtr self, QueueEpoch observed_epoch)
{
    QueueWaitSource source = QueueWaitSource__new_arrivals(self);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_arrivals(self);
    observed.produced = observed_epoch;
    QueueWaitSource__wait(&source, &observed);
}
