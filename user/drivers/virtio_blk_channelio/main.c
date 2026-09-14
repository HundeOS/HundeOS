#include "channelio.h"
#include "channel/protocol.h"
#include "channel/block_server.h"
#include "fiber/scheduler.h"
#include "types/fixed_array.h"

#include "device.h"
#define DRIVER_LOG_TAG "VIRTIO-BLK"
#include "sys/debug.h"

#define VIRTIO_BLK_FIBER_WORK_CAPACITY 32
typedef uint64_t VirtioBlkKernelRequestID;
OptionalDef(VirtioBlkKernelRequestID);
static bool VirtioBlkKernelRequestID__equals(void * first, void * second)
{
    return *(VirtioBlkKernelRequestID *)first == *(VirtioBlkKernelRequestID *)second;
}
FixedArrayDefine(VirtioBlkKernelRequestID, VIRTIO_BLK_FIBER_WORK_CAPACITY, VirtioBlkKernelRequests,
                 VirtioBlkKernelRequestID__equals);
FixedArrayBodyDefine(VirtioBlkKernelRequests);

typedef struct VirtioBlkFiberRuntime
{
    VirtioBlkDevicePtr device;
    FiberScheduler scheduler;
    BlockServerPtr server;
    VirtioBlkKernelRequests work;
    ChannelSignalState work_state;
    ChannelSignal work_available;
} VirtioBlkFiberRuntime;
typedef VirtioBlkFiberRuntime * VirtioBlkFiberRuntimePtr;
OptionalDef(VirtioBlkFiberRuntimePtr);

static Optional(VirtioBlkFiberRuntimePtr) virtio_blk_fiber_runtime = None(VirtioBlkFiberRuntimePtr);

static VirtioBlkFiberRuntime VirtioBlkFiberRuntime__new(VirtioBlkDevicePtr device, BlockServerPtr server)
{
    VirtioBlkFiberRuntime self = {
        .device = device,
        .scheduler = FiberScheduler__new(),
        .server = server,
        .work = VirtioBlkKernelRequests__new(),
        .work_state = ChannelSignalState__new(),
    };
    self.work_available = ChannelSignal__new(&self.work_state);
    return self;
}

ServiceCode VirtioBlkRuntime__await_irq(ServiceEventPtr event)
{
    match_optional(
        virtio_blk_fiber_runtime,
        some(runtime)
        {
            FiberWaitKey key = FiberWaitKey__new(FIBER_WAIT_USER, runtime->device->irq_id);
            match_result(
                FiberScheduler__await_current(&runtime->scheduler, key),
                success(code)
                {
                    *event = ServiceEvent__new(SERVICE_EVENT_IRQ, runtime->device->irq_id, 0);
                    return SERVICE_OK;
                },
                error(code, message) { return SERVICE_ABORT; }
            );
        },
        none() { return SERVICE_ABORT; }
    );
    return SERVICE_ABORT;
}

static Result(FiberCode) VirtioBlkFiberRuntime__worker(FiberPtr fiber, Anything argument)
{
    VirtioBlkFiberRuntimePtr self = (VirtioBlkFiberRuntimePtr)argument;
    while (true)
    {
        match_optional(
            VirtioBlkKernelRequests__pop_front(&self->work),
            some(request_id) { VirtioBlk__handle_block_request(self->device, request_id); },
            none_do_nothing()
        );
        if (!BlockServer__poll(self->server) && VirtioBlkKernelRequests__len(&self->work) == 0)
        {
            match_result(
                FiberScheduler__await_channel_signal(&self->scheduler, &self->work_available),
                success_do_nothing(),
                error(code, message) { return Error(FiberCode, message, code); }
            );
        }
    }
}

static bool VirtioBlkFiberRuntime__wake(FiberSchedulerPtr scheduler, FiberWaitKey key, uint64_t value)
{
    FiberWakeValue wake = FiberWakeValue__new(&key, FIBER_OK, value);
    bool woken = false;
    match_result(
        FiberScheduler__wake(scheduler, key, wake),
        success(code) { woken = true; },
        error(code, message)
        {
            if (code != FIBER_WAIT_NOT_FOUND)
            {
                DRIVER_DEBUG("virtio_blk: fibre wake failed code=%i error=%B\n", (int)code, &message);
            }
        }
    );
    return woken;
}

static bool VirtioBlkFiberRuntime__queue_work(VirtioBlkFiberRuntimePtr self, VirtioBlkKernelRequestID request_id)
{
    bool queued = false;
    match_result(
        VirtioBlkKernelRequests__push(&self->work, request_id),
        success(code) { queued = true; },
        error(code, message)
        {
            DRIVER_DEBUG("virtio_blk: fibre work queue full code=%i error=%B\n", (int)code, &message);
        }
    );
    if (queued)
    {
        ChannelSignal__notify_data(&self->work_available);
        VirtioBlkFiberRuntime__wake(&self->scheduler, FiberWaitKey__channel_signal(&self->work_available), 0);
    }
    return queued;
}

static bool VirtioBlkFiberRuntime__handle_event(VirtioBlkFiberRuntimePtr self, ServiceEventPtr event)
{
    if (event->type == SERVICE_EVENT_IRQ && event->source == self->device->irq_id)
    {
        FiberWaitKey key = FiberWaitKey__new(FIBER_WAIT_USER, self->device->irq_id);
        if (!VirtioBlkFiberRuntime__wake(&self->scheduler, key, event->data))
        {
            VirtioBlk__ack_interrupt(self->device->mmio);
            VirtioBlkChannelIO__acknowledge(self->device->irq_id);
        }
        return true;
    }
    if (event->type == SERVICE_EVENT_BLOCK_IO)
    {
        return VirtioBlkFiberRuntime__queue_work(self, event->source);
    }
    return false;
}

int VirtioBlkRuntime__run(VirtioBlkDevicePtr device, BlockServerPtr server)
{
    VirtioBlkFiberRuntime runtime = VirtioBlkFiberRuntime__new(device, server);
    virtio_blk_fiber_runtime = Some(VirtioBlkFiberRuntimePtr, &runtime);
    match_result(
        FiberScheduler__spawn(&runtime.scheduler, VirtioBlkFiberRuntime__worker, (Anything)&runtime),
        success_do_nothing(),
        error(code, message) { return 1; }
    );

    while (true)
    {
        Channel__prepare_wait();
        bool progress = false;
        ServiceEvent event;
        for (ServiceCode status = VirtioBlkChannelIO__poll(&event); status != SERVICE_EVENT_QUEUE_EMPTY;
             status = VirtioBlkChannelIO__poll(&event))
        {
            if (status == SERVICE_COMPLETE)
            {
                return 0;
            }
            if (!service_ok(status))
            {
                return 1;
            }
            progress = VirtioBlkFiberRuntime__handle_event(&runtime, &event) || progress;
        }

        VirtioBlkFiberRuntime__wake(&runtime.scheduler, FiberWaitKey__channel_signal(&runtime.work_available), 0);
        match_result(
            FiberScheduler__run_ready_until_blocked(&runtime.scheduler),
            success_do_nothing(),
            error(code, message)
            {
                DRIVER_DEBUG("virtio_blk: fibre scheduler failed code=%i error=%B\n", (int)code, &message);
                return 1;
            }
        );
        if (!progress && FiberScheduler__ready_count(&runtime.scheduler) == 0)
        {
            Channel__wait_event();
        }
    }
}

int main(int argc, char ** argv)
{
    if (argc < 3)
    {
        return 1;
    }
    BString slot = BString__new(argv[1]);
    BString path = BString__new(argv[2]);
    return VirtioBlk__run(&slot, &path);
}
