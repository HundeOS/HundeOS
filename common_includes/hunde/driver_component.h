#ifndef HUNDE_DRIVER_COMPONENT_H
#define HUNDE_DRIVER_COMPONENT_H

#include "io_component.h"

#define HUNDE_DRIVER_COMPONENT_ENTRY "hunde_driver_component_entry_v1"
#define HUNDE_DRIVER_COMPONENT_VERSION 1U
#define HUNDE_DRIVER_PROGRESS 1U
#define HUNDE_DRIVER_REARM 2U

typedef struct HundeDriverStepResult
{
    HundeStatusResult status;
    uint64_t flags;
} HundeDriverStepResult;

typedef HundeDriverStepResult (*HundeDriverStep)(HundeComponentContext context);
typedef void (*HundeDriverDeliver)(HundeComponentContext context);

typedef struct HundeDriverComponentV1
{
    HundeAbiHeader header;
    HundeComponentContext context;
    uint64_t irq;
    HundeDriverStep step;
    HundeDriverDeliver deliver;
} HundeDriverComponentV1;

typedef struct HundeDriverLoadResult
{
    HundeAbiResultKind kind;
    uint32_t reserved;
    HundeDriverComponentV1 component;
    HundeAbiError error;
} HundeDriverLoadResult;

typedef HundeDriverLoadResult (*HundeDriverComponentEntryV1)(ConstHundeIOHostOperationsV6Ptr host,
                                                             HundeByteSpan configuration);

static inline HundeDriverStepResult HundeDriverStepResult__new(uint64_t flags)
{
    return (HundeDriverStepResult){.status = HundeStatusResult__success(), .flags = flags};
}

static inline HundeDriverComponentV1 HundeDriverComponentV1__new(HundeComponentContext context, uint64_t irq,
                                                                 HundeDriverStep step, HundeDriverDeliver deliver)
{
    return (HundeDriverComponentV1){
        .header = HundeAbiHeader__new(sizeof(HundeDriverComponentV1), HUNDE_DRIVER_COMPONENT_VERSION),
        .context = context,
        .irq = irq,
        .step = step,
        .deliver = deliver};
}

static inline HundeDriverLoadResult HundeDriverLoadResult__success(HundeDriverComponentV1 component)
{
    return (HundeDriverLoadResult){.kind = HUNDE_ABI_SUCCESS, .component = component};
}

static inline HundeDriverLoadResult HundeDriverLoadResult__error(int32_t code, HundeByteSpan message)
{
    return (HundeDriverLoadResult){.kind = HUNDE_ABI_ERROR, .error = HundeAbiError__new(code, message)};
}

_Static_assert(sizeof(HundeDriverStepResult) == 40, "Driver step ABI changed");
_Static_assert(sizeof(HundeDriverComponentV1) == 40, "Driver component ABI changed");
_Static_assert(sizeof(HundeDriverLoadResult) == 72, "Driver load ABI changed");

#endif
