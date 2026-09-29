#include "runtime.h"

#include "ipc/ipc_platform.h"
#include "sys/syslog.h"

#define DRIVER_LOG_TAG "VIRTIO-BLK"
#include "sys/debug.h"

static Result(FiberCode) VirtioBlkRuntime__worker(FiberPtr fiber, Anything argument)
{
    VirtioBlkRuntimePtr self = (VirtioBlkRuntimePtr)argument;
    while (true)
    {
        if (!BlockServer__poll(&self->server))
        {
            match_result(
                FiberScheduler__await_channel_signal(&self->scheduler, &self->work_available),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, message, code);
                }
            );
        }
    }
}

static bool VirtioBlkRuntime__wake(FiberSchedulerPtr scheduler, FiberWaitKey key, uint64_t value)
{
    FiberWakeValue wake = FiberWakeValue__new(&key, FIBER_OK, value);
    match_result(
        FiberScheduler__wake(scheduler, key, wake),
        success(code)
        {
            return true;
        },
        error(code, message)
        {
            if (code != FIBER_WAIT_NOT_FOUND)
            {
                DRIVER_DEBUG("virtio_blk: fibre wake failed code=%i error=%B\n", (int)code, &message);
            }
        }
    );
    return false;
}

VirtioBlkInterruptBackend VirtioBlkInterruptBackend__new(Anything context, VirtioBlkInterruptSubscribe subscribe,
                                                         VirtioBlkInterruptRearm rearm)
{
    return (VirtioBlkInterruptBackend){
        .context = context,
        .subscribe = subscribe,
        .rearm = rearm,
    };
}

static VirtioBlkRuntime VirtioBlkRuntime__new(ConstVirtioBlkDevicePtr device,
                                              ConstVirtioBlkInterruptBackendPtr interrupts)
{
    return (VirtioBlkRuntime){
        .device = *device,
        .scheduler = FiberScheduler__new(),
        .work_state = ChannelSignalState__new(),
        .interrupts = *interrupts,
    };
}

ServiceCode VirtioBlkRuntime__start(VirtioBlkRuntimePtr self, uint64_t slot, ConstBStringPtr path,
                                    ConstVirtioBlkInterruptBackendPtr interrupts)
{
    VirtioBlkDevice device;
    match_result(
        VirtioBlk__start(slot, path),
        success(started_device)
        {
            device = started_device;
        },
        error(code, message)
        {
            kernel_syslog_tagf(DRIVER_LOG_TAG, "hosted start failed path=%B error=%B status=%i", path, &message,
                               (int)code);
            return code;
        }
    );
    *self = VirtioBlkRuntime__new(&device, interrupts);
    self->device.runtime = self;
    self->work_available = ChannelSignal__new(&self->work_state);

    ChannelCode listen_status = VirtioBlk__listen(&self->device, &self->server);
    if (listen_status != CHANNEL_OK)
    {
        kernel_syslog_tagf(DRIVER_LOG_TAG, "hosted listener failed path=%B status=%i", path, (int)listen_status);
        return SERVICE_BLOCK_REQUEST_FAILED;
    }
    ServiceCode subscribe_status = self->interrupts.subscribe(self->interrupts.context, self->device.irq_id);
    if (!service_ok(subscribe_status))
    {
        kernel_syslog_tagf(DRIVER_LOG_TAG, "hosted IRQ route failed path=%B irq=%i status=%i", path,
                           (int)self->device.irq_id, (int)subscribe_status);
        return SERVICE_BLOCK_REQUEST_FAILED;
    }
    ServiceCode ready_status = service_publish_ready(&self->device.path);
    if (!service_ok(ready_status))
    {
        kernel_syslog_tagf(DRIVER_LOG_TAG, "hosted readiness failed path=%B status=%i", path, (int)ready_status);
        return SERVICE_BLOCK_REQUEST_FAILED;
    }

    match_result(
        FiberScheduler__spawn(&self->scheduler, VirtioBlkRuntime__worker, (Anything)self),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf(DRIVER_LOG_TAG, "hosted worker failed path=%B error=%B status=%i", path, &message,
                               (int)code);
            return SERVICE_BLOCK_REQUEST_FAILED;
        }
    );
    return SERVICE_OK;
}

ServiceCode VirtioBlkRuntime__await_irq(VirtioBlkDevicePtr device, ServiceEventPtr event)
{
    VirtioBlkRuntimePtr self = device->runtime;
    FiberWaitKey key = FiberWaitKey__new(FIBER_WAIT_USER, device->irq_id);
    match_result(
        FiberScheduler__await_current(&self->scheduler, key),
        success(code)
        {
            device->fiber_resumed_at_usec = IpcPlatform__monotonic_usec();
            *event = ServiceEvent__new(SERVICE_EVENT_IRQ, device->irq_id, 0);
            return SERVICE_OK;
        },
        error(code, message)
        {
            return SERVICE_ABORT;
        }
    );
    return SERVICE_ABORT;
}

ServiceCode VirtioBlkRuntime__rearm_irq(VirtioBlkDevicePtr device)
{
    VirtioBlkRuntimePtr self = device->runtime;
    return self->interrupts.rearm(self->interrupts.context, device->irq_id);
}

bool VirtioBlkRuntime__handle_irq(VirtioBlkRuntimePtr self, ConstKernelNotificationPtr event,
                                  uint64_t manager_received_at_usec)
{
    if (event->id != self->device.irq_id)
    {
        return false;
    }

    self->device.irq_timing =
        Some(InterruptDeliveryTiming, InterruptDeliveryTiming__new(event->sequence, event->occurred_at_usec,
                                                                   manager_received_at_usec, manager_received_at_usec));
    FiberWaitKey key = FiberWaitKey__new(FIBER_WAIT_USER, self->device.irq_id);
    if (!VirtioBlkRuntime__wake(&self->scheduler, key, event->sequence))
    {
        VirtioBlk__ack_interrupt(self->device.mmio);
        self->interrupts.rearm(self->interrupts.context, self->device.irq_id);
    }
    return true;
}

Result(FiberCode) VirtioBlkRuntime__step(VirtioBlkRuntimePtr self)
{
    VirtioBlkRuntime__wake(&self->scheduler, FiberWaitKey__channel_signal(&self->work_available), 0);
    return FiberScheduler__run_ready_until_blocked(&self->scheduler);
}
