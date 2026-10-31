#ifndef HUNDE_RUST_DRIVER_H
#define HUNDE_RUST_DRIVER_H

#include "native.h"
#include "hunde/boot_info.h"
#include "ipc/interrupt_protocol.h"

typedef struct RustDriverValue
{
    HundeStatusResult status;
    uint64_t value;
} RustDriverValue;

typedef struct RustDriverEvent
{
    HundeStatusResult status;
    uint64_t irq;
    uint64_t sequence;
    uint64_t kernel_at_usec;
    uint64_t manager_at_usec;
} RustDriverEvent;

typedef struct RustDriverRequest
{
    HundeStatusResult status;
    uint32_t kind;
    uint32_t reserved;
    uint64_t irq;
    uint64_t generation;
    uint64_t sequence;
} RustDriverRequest;

RustNativeLayout hunde_driver_manager_layout(void);
HundeStatusResult hunde_driver_manager_init(void * storage);
HundeStatusResult hunde_driver_route(void * storage, uint64_t pid, uint64_t irq);
HundeStatusResult hunde_driver_route_self(void * storage, uint64_t irq);
HundeStatusResult hunde_driver_rearm(void * storage, uint64_t irq);
RustDriverEvent hunde_driver_event(void * storage);
RustNativeLayout hunde_driver_listener_layout(void);
HundeStatusResult hunde_driver_listen(void * storage, HundeByteSpan name);
RustDriverValue hunde_driver_arrival(void * storage, uint64_t index);
RustNativeLayout hunde_driver_client_layout(void);
HundeStatusResult hunde_driver_accept(void * storage, uint64_t connection);
uint64_t hunde_driver_client_pid(void * storage);
RustDriverRequest hunde_driver_request(void * storage);
HundeStatusResult hunde_driver_reply(void * storage, uint32_t kind, int32_t code, uint64_t irq, uint64_t generation,
                                     uint64_t sequence, uint64_t kernel_at_usec, uint64_t manager_at_usec);

_Static_assert(sizeof(RustDriverValue) == 40, "Driver value ABI changed");
_Static_assert(sizeof(RustDriverEvent) == 64, "Driver event ABI changed");
_Static_assert(sizeof(RustDriverRequest) == 64, "Driver request ABI changed");

#endif
