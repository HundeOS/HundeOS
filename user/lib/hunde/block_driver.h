#ifndef HUNDE_RUST_BLOCK_DRIVER_H
#define HUNDE_RUST_BLOCK_DRIVER_H

#include "native.h"

typedef struct HundeBlockBuffer
{
    uint64_t id;
    uint64_t address;
    uint64_t capacity;
    uint32_t direction;
    uint32_t reserved;
} HundeBlockBuffer;

typedef const HundeBlockBuffer * ConstHundeBlockBufferPtr;

typedef HundeStatusResult (*HundeBlockTransfer)(void * context, uint32_t writing, uint64_t block, uint64_t count,
                                                ConstHundeBlockBufferPtr buffer, void * runtime);

RustNativeLayout hunde_block_driver_layout(void);
HundeStatusResult hunde_block_driver_init(void * storage, HundeByteSpan name, uint64_t sectors, uint64_t irq,
                                          void * context, HundeBlockTransfer transfer);
HundeStatusResult hunde_block_driver_step(void * storage);
uint32_t hunde_block_driver_worked(void * storage);
HundeStatusResult hunde_block_driver_deliver(void * storage);
HundeStatusResult hunde_block_driver_sleep_usec(uint64_t usec);
HundeStatusResult hunde_block_driver_await(void * storage);
HundeStatusResult hunde_block_driver_register(HundeByteSpan name, uint64_t sectors);
int64_t hunde_block_buffer_device_address(ConstHundeBlockBufferPtr buffer, uint64_t offset, uint64_t length);
uint64_t hunde_block_driver_monotonic_usec(void);
void hunde_block_driver_record(uint64_t blocks, uint64_t device_usec, uint32_t succeeded);

#endif
