#include "queue_wait.h"

static Optional(QueueWaitAdapter) active_adapter = None(QueueWaitAdapter);

Optional(QueueWaitAdapter) QueueWait__swap_adapter(const OptionalPtr(QueueWaitAdapter) adapter)
{
    Optional(QueueWaitAdapter) previous = active_adapter;
    active_adapter = *adapter;
    return previous;
}

bool QueueWait__has_adapter(void)
{
    match_optional(active_adapter,
        some(adapter)
        {
            return true;
        },
        none()
        {
            return false;
        }
    );
    return false;
}

Result(QueueWaitCode) QueueWait__cooperate(void)
{
    match_optional(
        active_adapter,
        some(adapter)
        {
            return adapter.park(adapter.context);
        },
        none()
        {
            return Error(QueueWaitCode, bstr("No cooperative wait adapter"), QUEUE_INVALID);
        }
    );
    return Error(QueueWaitCode, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

void QueueWait__notify(void)
{
    match_optional(active_adapter,
        some(adapter)
        {
            adapter.notify(adapter.context);
        },
        none_do_nothing()
    );
}

Result(QueueWaitCode) QueueWaitGate__acquire(QueueWaitGatePtr self)
{
    while (self->occupied)
    {
        match_result(
            QueueWait__cooperate(),
            success_do_nothing(),
            error(code, message)
            {
                return Error(QueueWaitCode, message, code);
            }
        );
    }
    self->occupied = true;
    return Success(QueueWaitCode, QUEUE_WAIT_CHANGED);
}

void QueueWaitGate__release(QueueWaitGatePtr self)
{
    self->occupied = false;
    QueueWait__notify();
}
