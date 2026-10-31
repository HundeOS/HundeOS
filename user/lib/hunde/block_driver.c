#include "block_driver.h"

#include "buffer/client.h"
#include "channel/block_server.h"
#include "fiber/scheduler.h"
#include "ipc/io_diagnostics.h"
#include "ipc/ipc_platform.h"
#include "sys/service.h"
#include "sys/process_sleep.h"

typedef struct HundeBlockDriver
{
    BlockServer server;
    FiberScheduler scheduler;
    HundeBlockTransfer transfer;
    void * context;
    uint64_t irq;
    bool worked;
} HundeBlockDriver;

typedef HundeBlockDriver * HundeBlockDriverPtr;

static HundeBlockBuffer HundeBlockBuffer__new(ConstBufferDescriptorPtr descriptor)
{
    return (HundeBlockBuffer){.id = descriptor->id,
                              .address = descriptor->address,
                              .capacity = descriptor->capacity,
                              .direction = descriptor->direction};
}

static BlockProtocolStatus HundeBlockDriver__transfer(BlockServerContext context, BlockProtocolOperation operation,
                                                      BlockProtocolBlockNumber block, BlockProtocolBlockCount count,
                                                      ConstBufferDescriptorPtr buffer)
{
    HundeBlockDriverPtr self = context;
    HundeBlockBuffer description = HundeBlockBuffer__new(buffer);
    HundeStatusResult status =
        self->transfer(self->context, operation == BLOCK_PROTOCOL_WRITE, block, count, &description, self);
    return status.kind == HUNDE_ABI_SUCCESS ? BLOCK_PROTOCOL_OK : BLOCK_PROTOCOL_IO_ERROR;
}

static Result(FiberCode) HundeBlockDriver__worker(FiberPtr fiber, Anything argument)
{
    HundeBlockDriverPtr self = (HundeBlockDriverPtr)argument;
    while (true)
    {
        bool progress = BlockServer__poll(&self->server);
        self->worked = self->worked || progress;
        if (!progress)
        {
            match_result(
                FiberScheduler__await_current(&self->scheduler, FiberWaitKey__new(FIBER_WAIT_USER, 0)),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, message, code);
                }
            );
        }
    }
}

static HundeStatusResult HundeBlockDriver__wake(HundeBlockDriverPtr self, uint64_t key_id)
{
    FiberWaitKey key = FiberWaitKey__new(FIBER_WAIT_USER, key_id);
    match_result(
        FiberScheduler__wake(&self->scheduler, key, FiberWakeValue__new(&key, FIBER_OK, 0)),
        success(value)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return code == FIBER_WAIT_NOT_FOUND ? HundeStatusResult__success() : RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

RustNativeLayout hunde_block_driver_layout(void)
{
    return RustNativeLayout__new(sizeof(HundeBlockDriver), _Alignof(HundeBlockDriver));
}

HundeStatusResult hunde_block_driver_init(void * storage, HundeByteSpan name, uint64_t sectors, uint64_t irq,
                                          void * context, HundeBlockTransfer transfer)
{
    if (name.length == 0 || name.length >= BSTRING_MAX_SIZE || sectors == 0 || irq == 0)
    {
        return RustNative__error(SERVICE_INVALID_BUFFER);
    }
    HundeBlockDriverPtr self = storage;
    self->scheduler = FiberScheduler__new();
    self->transfer = transfer;
    self->context = context;
    self->irq = irq;
    BString path = BString__from_chars((const char *)name.bytes, name.length);
    ChannelCode status = BlockServer__listen_into(&self->server, &path, sectors, self, HundeBlockDriver__transfer);
    if (status != CHANNEL_OK)
    {
        return RustNative__error(status);
    }
    match_result(
        FiberScheduler__spawn(&self->scheduler, HundeBlockDriver__worker, (Anything)self),
        success(value)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

HundeStatusResult hunde_block_driver_step(void * storage)
{
    HundeBlockDriverPtr self = storage;
    self->worked = false;
    HundeStatusResult wake = HundeBlockDriver__wake(self, 0);
    if (wake.kind != HUNDE_ABI_SUCCESS)
    {
        return wake;
    }
    match_result(
        FiberScheduler__run_ready_until_blocked(&self->scheduler),
        success(value)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

uint32_t hunde_block_driver_worked(void * storage)
{
    HundeBlockDriverPtr self = storage;
    return self->worked;
}

HundeStatusResult hunde_block_driver_deliver(void * storage)
{
    HundeBlockDriverPtr self = storage;
    return HundeBlockDriver__wake(self, self->irq);
}

HundeStatusResult hunde_block_driver_await(void * storage)
{
    HundeBlockDriverPtr self = storage;
    match_result(
        FiberScheduler__await_current(&self->scheduler, FiberWaitKey__new(FIBER_WAIT_USER, self->irq)),
        success(value)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

HundeStatusResult hunde_block_driver_register(HundeByteSpan name, uint64_t sectors)
{
    if (name.length == 0 || name.length >= BSTRING_MAX_SIZE || sectors == 0)
    {
        return RustNative__error(SERVICE_INVALID_BUFFER);
    }
    BString path = BString__from_chars((const char *)name.bytes, name.length);
    ServiceCode status = service_register_block_device(&path, BLOCK_CHANNEL_SECTOR_SIZE, sectors);
    return service_ok(status) ? HundeStatusResult__success() : RustNative__error(status);
}

int64_t hunde_block_buffer_device_address(ConstHundeBlockBufferPtr buffer, uint64_t offset, uint64_t length)
{
    BufferDescriptor descriptor = BufferDescriptor__new(buffer->id, (BufferDirection)buffer->direction);
    if (descriptor.address != buffer->address || descriptor.capacity != buffer->capacity ||
        !BufferDescriptor__contains(&descriptor, offset, length) || length == 0)
    {
        return BUFFER_INVALID;
    }
    return BufferMemory__operation(MEM_BUFFER_DEVICE_ADDRESS, buffer->id, offset, length, buffer->direction);
}

uint64_t hunde_block_driver_monotonic_usec(void)
{
    return IpcPlatform__monotonic_usec();
}

void hunde_block_driver_record(uint64_t blocks, uint64_t device_usec, uint32_t succeeded)
{
    Optional(BlockDeviceTiming) timing = None(BlockDeviceTiming);
    IODiagnostics__record_block_submission(blocks, device_usec, succeeded != 0, &timing);
}

HundeStatusResult hunde_block_driver_sleep_usec(uint64_t usec)
{
    return sleep_usec(usec) == PROCESS_SLEEP_OK ? HundeStatusResult__success()
                                                : RustNative__error(SERVICE_OPERATION_NOT_SUPPORTED);
}
