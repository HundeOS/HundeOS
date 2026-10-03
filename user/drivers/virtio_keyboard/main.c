#define DRIVER_LOG_TAG "VIRTIO-KEYBOARD"

#include <stdbool.h>
#include <stdint.h>

#include "sys/debug.h"
#include "sys/boot_info.h"
#include "io/stream_driver.h"
#include "interrupt/client.h"
#include "sys/service.h"
#include "sys/sync.h"

#ifndef VIRTIO_KEYBOARD_ENTRYPOINT
#define VIRTIO_KEYBOARD_ENTRYPOINT main
#endif

#define VIRTIO_MMIO_MAGIC 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c
#define VIRTIO_MMIO_QUEUE_PFN 0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STAT 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070

#define VIRTIO_MMIO_MAGIC_VALUE 0x74726976U
#define VIRTIO_DEVICE_ID_INPUT 18U
#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER 2U
#define VIRTIO_STATUS_DRIVER_OK 4U

#define VIRTIO_INPUT_EVENT_QUEUE_INDEX 0U
#define VIRTIO_INPUT_QUEUE_SIZE 32U
#define VIRTIO_INPUT_QUEUE_ALIGN 4096U
#define VIRTIO_INPUT_QUEUE_BYTES 8192U

#define VIRTQ_DESC_F_WRITE 2U

#define EV_KEY 1U
#define KEY_BACKSPACE 14U
#define KEY_TAB 15U
#define KEY_ENTER 28U
#define KEY_LEFTSHIFT 42U
#define KEY_RIGHTSHIFT 54U
#define KEY_SPACE 57U
#define KEY_UP 103U
#define KEY_LEFT 105U
#define KEY_RIGHT 106U
#define KEY_DOWN 108U

#define VIRTIO_KEYBOARD_PENDING_BYTES 256U

static bool KeyboardByte__equals(void * first, void * second)
{
    return *(char *)first == *(char *)second;
}

FixedArrayDefine(char, VIRTIO_KEYBOARD_PENDING_BYTES, KeyboardInput, KeyboardByte__equals);
FixedArrayBodyDefine(KeyboardInput);

typedef struct VirtqDesc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} VirtqDesc;

typedef VirtqDesc * VirtqDescPtr;

typedef struct VirtqAvail
{
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_INPUT_QUEUE_SIZE];
} VirtqAvail;

typedef VirtqAvail * VirtqAvailPtr;

typedef struct VirtqUsedElem
{
    uint32_t id;
    uint32_t len;
} VirtqUsedElem;

typedef struct VirtqUsed
{
    uint16_t flags;
    uint16_t idx;
    VirtqUsedElem ring[VIRTIO_INPUT_QUEUE_SIZE];
} VirtqUsed;

typedef VirtqUsed * VirtqUsedPtr;

typedef struct VirtioInputEvent
{
    uint16_t type;
    uint16_t code;
    int32_t value;
} VirtioInputEvent;

typedef struct VirtioKeyboard
{
    void * mmio;
    uint64_t slot;
    uint64_t irq_id;
    uint16_t queue_size;
    uint16_t last_used_idx;
    bool shift;
    KeyboardInput pending;
} VirtioKeyboard;

typedef VirtioKeyboard * VirtioKeyboardPtr;

typedef struct VirtioKeyboardDevice
{
    void * mmio;
    uint64_t slot;
    uint64_t irq_id;
} VirtioKeyboardDevice;

typedef const VirtioKeyboardDevice * ConstVirtioKeyboardDevicePtr;

OptionalDef(VirtioKeyboardDevice);

static VirtioKeyboardDevice VirtioKeyboardDevice__new(void * mmio, uint64_t slot, uint64_t irq_id)
{
    return (VirtioKeyboardDevice){
        .mmio = mmio,
        .slot = slot,
        .irq_id = irq_id,
    };
}

static VirtqDesc VirtqDesc__new(uint64_t addr, uint32_t len, uint16_t flags, uint16_t next)
{
    return (VirtqDesc){
        .addr = addr,
        .len = len,
        .flags = flags,
        .next = next,
    };
}

static VirtioKeyboard VirtioKeyboard__new(ConstVirtioKeyboardDevicePtr device)
{
    return (VirtioKeyboard){
        .mmio = device->mmio,
        .slot = device->slot,
        .irq_id = device->irq_id,
        .queue_size = 0,
        .last_used_idx = 0,
        .shift = false,
        .pending = KeyboardInput__new(),
    };
}

static uint8_t virtio_keyboard_queue_area[VIRTIO_INPUT_QUEUE_BYTES] __attribute__((aligned(VIRTIO_INPUT_QUEUE_ALIGN)));
static VirtioInputEvent virtio_keyboard_events[VIRTIO_INPUT_QUEUE_SIZE] __attribute__((aligned(8)));

static uint64_t VirtioKeyboard__align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static void VirtioKeyboard__zero_bytes(void * target, uint64_t size)
{
    uint8_t * bytes = (uint8_t *)target;
    for (uint64_t index = 0; index < size; index++)
    {
        bytes[index] = 0;
    }
}

static VirtqDescPtr VirtioKeyboard__descs(VirtioKeyboardPtr self)
{
    return (VirtqDescPtr)virtio_keyboard_queue_area;
}

static VirtqAvailPtr VirtioKeyboard__avail(VirtioKeyboardPtr self)
{
    return (VirtqAvailPtr)(virtio_keyboard_queue_area + sizeof(VirtqDesc) * VIRTIO_INPUT_QUEUE_SIZE);
}

static VirtqUsedPtr VirtioKeyboard__used(VirtioKeyboardPtr self)
{
    uint64_t used_offset = VirtioKeyboard__align_up(sizeof(VirtqDesc) * VIRTIO_INPUT_QUEUE_SIZE + sizeof(VirtqAvail),
                                                    VIRTIO_INPUT_QUEUE_ALIGN);
    return (VirtqUsedPtr)(virtio_keyboard_queue_area + used_offset);
}

#if !defined(VIRTIO_KEYBOARD_READ32) || !defined(VIRTIO_KEYBOARD_WRITE32)
static volatile uint32_t * VirtioKeyboard__reg32(void * base, uint64_t offset)
{
    return (volatile uint32_t *)((uint64_t)base + offset);
}
#endif

#ifndef VIRTIO_KEYBOARD_READ32
#define VIRTIO_KEYBOARD_READ32(base, offset) (*VirtioKeyboard__reg32((base), (offset)))
#endif

#ifndef VIRTIO_KEYBOARD_WRITE32
#define VIRTIO_KEYBOARD_WRITE32(base, offset, value) (*VirtioKeyboard__reg32((base), (offset)) = (uint32_t)(value))
#endif

static uint64_t VirtioKeyboard__pending_count(VirtioKeyboardPtr self)
{
    return KeyboardInput__len(&self->pending);
}

static void VirtioKeyboard__emit_byte(VirtioKeyboardPtr self, char byte)
{
    match_result(
        KeyboardInput__push(&self->pending, byte),
        success_do_nothing(),
        error(code, message)
        {
            DRIVER_DEBUG("keyboard input buffer: %s (%i)\n", BString__cstr(&message), (int)code);
        }
    );
}

static void VirtioKeyboard__emit_escape(VirtioKeyboardPtr self, char final)
{
    VirtioKeyboard__emit_byte(self, '\033');
    VirtioKeyboard__emit_byte(self, '[');
    VirtioKeyboard__emit_byte(self, final);
}

static char VirtioKeyboard__letter(uint16_t code, bool shift)
{
    char letter = '\0';
    switch (code)
    {
    case 16:
        letter = 'q';
        break;
    case 17:
        letter = 'w';
        break;
    case 18:
        letter = 'e';
        break;
    case 19:
        letter = 'r';
        break;
    case 20:
        letter = 't';
        break;
    case 21:
        letter = 'y';
        break;
    case 22:
        letter = 'u';
        break;
    case 23:
        letter = 'i';
        break;
    case 24:
        letter = 'o';
        break;
    case 25:
        letter = 'p';
        break;
    case 30:
        letter = 'a';
        break;
    case 31:
        letter = 's';
        break;
    case 32:
        letter = 'd';
        break;
    case 33:
        letter = 'f';
        break;
    case 34:
        letter = 'g';
        break;
    case 35:
        letter = 'h';
        break;
    case 36:
        letter = 'j';
        break;
    case 37:
        letter = 'k';
        break;
    case 38:
        letter = 'l';
        break;
    case 44:
        letter = 'z';
        break;
    case 45:
        letter = 'x';
        break;
    case 46:
        letter = 'c';
        break;
    case 47:
        letter = 'v';
        break;
    case 48:
        letter = 'b';
        break;
    case 49:
        letter = 'n';
        break;
    case 50:
        letter = 'm';
        break;
    default:
        return '\0';
    }

    return shift ? (char)(letter - ('a' - 'A')) : letter;
}

static char VirtioKeyboard__number_or_symbol(uint16_t code, bool shift)
{
    static const char normal[] = "1234567890";
    static const char shifted[] = "!@#$%^&*()";
    if (code < 2 || code > 11)
    {
        return '\0';
    }

    uint16_t index = code - 2;
    return shift ? shifted[index] : normal[index];
}

static char VirtioKeyboard__punctuation(uint16_t code, bool shift)
{
    switch (code)
    {
    case 12:
        return shift ? '_' : '-';
    case 13:
        return shift ? '+' : '=';
    case 26:
        return shift ? '{' : '[';
    case 27:
        return shift ? '}' : ']';
    case 39:
        return shift ? ':' : ';';
    case 40:
        return shift ? '"' : '\'';
    case 41:
        return shift ? '~' : '`';
    case 43:
        return shift ? '|' : '\\';
    case 51:
        return shift ? '<' : ',';
    case 52:
        return shift ? '>' : '.';
    case 53:
        return shift ? '?' : '/';
    default:
        return '\0';
    }
}

static void VirtioKeyboard__handle_key_event(VirtioKeyboardPtr self, uint16_t code, int32_t value)
{
    bool pressed = value == 1 || value == 2;
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT)
    {
        self->shift = pressed;
        return;
    }
    if (!pressed)
    {
        return;
    }

    if (code == KEY_ENTER)
    {
        VirtioKeyboard__emit_byte(self, '\n');
        return;
    }
    if (code == KEY_BACKSPACE)
    {
        VirtioKeyboard__emit_byte(self, 0x7f);
        return;
    }
    if (code == KEY_TAB)
    {
        VirtioKeyboard__emit_byte(self, '\t');
        return;
    }
    if (code == KEY_SPACE)
    {
        VirtioKeyboard__emit_byte(self, ' ');
        return;
    }
    if (code == KEY_UP)
    {
        VirtioKeyboard__emit_escape(self, 'A');
        return;
    }
    if (code == KEY_DOWN)
    {
        VirtioKeyboard__emit_escape(self, 'B');
        return;
    }
    if (code == KEY_RIGHT)
    {
        VirtioKeyboard__emit_escape(self, 'C');
        return;
    }
    if (code == KEY_LEFT)
    {
        VirtioKeyboard__emit_escape(self, 'D');
        return;
    }

    char translated = VirtioKeyboard__letter(code, self->shift);
    if (translated == '\0')
    {
        translated = VirtioKeyboard__number_or_symbol(code, self->shift);
    }
    if (translated == '\0')
    {
        translated = VirtioKeyboard__punctuation(code, self->shift);
    }
    if (translated != '\0')
    {
        VirtioKeyboard__emit_byte(self, translated);
    }
}

static bool VirtioKeyboard__probe(void * mmio)
{
    return VIRTIO_KEYBOARD_READ32(mmio, VIRTIO_MMIO_MAGIC) == VIRTIO_MMIO_MAGIC_VALUE &&
           VIRTIO_KEYBOARD_READ32(mmio, VIRTIO_MMIO_VERSION) >= 1 &&
           VIRTIO_KEYBOARD_READ32(mmio, VIRTIO_MMIO_DEVICE_ID) == VIRTIO_DEVICE_ID_INPUT;
}

static Optional(VirtioKeyboardDevice) VirtioKeyboard__find_device(void)
{
    BootVirtioMmioInfo virtio_mmio;
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            match_optional(
                info.virtio_mmio,
                some(value)
                {
                    virtio_mmio = value;
                },
                none()
                {
                    DRIVER_DEBUG("virtio_keyboard: virtio mmio unavailable\n");
                    return None(VirtioKeyboardDevice);
                }
            );
        },
        error(code, message)
        {
            DRIVER_DEBUG("virtio_keyboard: boot hardware unavailable\n");
            return None(VirtioKeyboardDevice);
        }
    );
    ServiceMappedMemoryPtr mapping;
    if (!service_ok(
            service_map_mmio(virtio_mmio.physical_base, virtio_mmio.slot_size * virtio_mmio.slot_count, &mapping)))
    {
        DRIVER_DEBUG("virtio_keyboard: mmio region map failed\n");
        return None(VirtioKeyboardDevice);
    }

    for (uint64_t slot = 0; slot < virtio_mmio.slot_count; slot++)
    {
        void * mmio = driver_ptr_offset(mapping, slot * virtio_mmio.slot_size);
        if (VirtioKeyboard__probe(mmio))
        {
            VirtioKeyboardDevice device = VirtioKeyboardDevice__new(mmio, slot, virtio_mmio.first_irq + slot);
            DRIVER_DEBUG("virtio_keyboard: found slot=%i irq=%i\n", (int)slot, (int)device.irq_id);
            return Some(VirtioKeyboardDevice, device);
        }
    }

    DRIVER_DEBUG("virtio_keyboard: no virtio input device found\n");
    return None(VirtioKeyboardDevice);
}

static void VirtioKeyboard__acknowledge_driver(VirtioKeyboardPtr self)
{
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_STATUS, 0);
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
}

static bool VirtioKeyboard__setup_event_queue(VirtioKeyboardPtr self)
{
    uint64_t queue_device_address = 0;
    if (!service_ok(service_device_address(virtio_keyboard_queue_area, &queue_device_address)))
    {
        DRIVER_DEBUG("virtio_keyboard: queue address translation failed\n");
        return false;
    }
    if ((queue_device_address & (VIRTIO_INPUT_QUEUE_ALIGN - 1)) != 0)
    {
        DRIVER_DEBUG("virtio_keyboard: queue address not aligned 0x%x\n", (unsigned int)queue_device_address);
        return false;
    }

    VirtioKeyboard__zero_bytes(virtio_keyboard_queue_area, VIRTIO_INPUT_QUEUE_BYTES);
    VirtioKeyboard__zero_bytes(virtio_keyboard_events, sizeof(virtio_keyboard_events));

    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_GUEST_PAGE_SIZE, VIRTIO_INPUT_QUEUE_ALIGN);
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_INPUT_EVENT_QUEUE_INDEX);
    uint32_t queue_num_max = VIRTIO_KEYBOARD_READ32(self->mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_num_max == 0)
    {
        DRIVER_DEBUG("virtio_keyboard: event queue missing\n");
        return false;
    }

    self->queue_size = queue_num_max < VIRTIO_INPUT_QUEUE_SIZE ? (uint16_t)queue_num_max : VIRTIO_INPUT_QUEUE_SIZE;
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_NUM, self->queue_size);
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_ALIGN, VIRTIO_INPUT_QUEUE_ALIGN);
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_PFN, queue_device_address / VIRTIO_INPUT_QUEUE_ALIGN);

    VirtqDescPtr descs = VirtioKeyboard__descs(self);
    VirtqAvailPtr avail = VirtioKeyboard__avail(self);
    for (uint16_t index = 0; index < self->queue_size; index++)
    {
        uint64_t event_device_address = 0;
        if (!service_ok(service_device_address(&virtio_keyboard_events[index], &event_device_address)))
        {
            DRIVER_DEBUG("virtio_keyboard: event address translation failed\n");
            return false;
        }
        descs[index] = VirtqDesc__new(event_device_address, sizeof(VirtioInputEvent), VIRTQ_DESC_F_WRITE, 0);
        avail->ring[index] = index;
    }

    __sync_boundary();
    avail->idx = self->queue_size;
    self->last_used_idx = VirtioKeyboard__used(self)->idx;
    __sync_boundary();
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_INPUT_EVENT_QUEUE_INDEX);
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_STATUS,
                            VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    return true;
}

static void VirtioKeyboard__ack_interrupt(VirtioKeyboardPtr self)
{
    uint32_t status = VIRTIO_KEYBOARD_READ32(self->mmio, VIRTIO_MMIO_INTERRUPT_STAT);
    __sync_boundary();
    if (status != 0)
    {
        VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_INTERRUPT_ACK, status);
        __sync_boundary();
    }
}

static void VirtioKeyboard__requeue_event(VirtioKeyboardPtr self, uint16_t descriptor_id)
{
    VirtqAvailPtr avail = VirtioKeyboard__avail(self);
    avail->ring[avail->idx % self->queue_size] = descriptor_id;
    __sync_boundary();
    avail->idx++;
    __sync_boundary();
    VIRTIO_KEYBOARD_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_INPUT_EVENT_QUEUE_INDEX);
}

static void VirtioKeyboard__drain_used_events(VirtioKeyboardPtr self)
{
    VirtqUsedPtr used = VirtioKeyboard__used(self);
    while (self->last_used_idx != used->idx)
    {
        // Reserve a complete arrow-key escape sequence before consuming an event.
        if (VirtioKeyboard__pending_count(self) > VIRTIO_KEYBOARD_PENDING_BYTES - 3)
        {
            return;
        }
        __sync_boundary();
        VirtqUsedElem used_event = used->ring[self->last_used_idx % self->queue_size];
        if (used_event.id < self->queue_size && used_event.len >= sizeof(VirtioInputEvent))
        {
            VirtioInputEvent event = virtio_keyboard_events[used_event.id];
            if (event.type == EV_KEY)
            {
                VirtioKeyboard__handle_key_event(self, event.code, event.value);
            }
            VirtioKeyboard__requeue_event(self, (uint16_t)used_event.id);
        }
        self->last_used_idx++;
    }
}

static IOReply VirtioKeyboard__read(IOServiceContext context, IORequestPtr request)
{
    VirtioKeyboardPtr self = context;
    IOReply reply = IOReply__new(IO_OK, 0);
    while ((uint64_t)reply.value < request->count)
    {
        match_optional(
            KeyboardInput__pop_front(&self->pending),
            some(value)
            {
                IORequest__read_target(request, &reply)[reply.value++] = value;
            },
            none()
            {
                break;
            }
        );
    }
    if (reply.value == 0 && request->count != 0)
    {
        reply.status = IO_WAIT_FOR_DATA;
    }
    return reply;
}

static bool VirtioKeyboard__init(VirtioKeyboardPtr self)
{
    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("virtio_keyboard: service registration failed\n");
        return false;
    }
    match_optional(
        VirtioKeyboard__find_device(),
        some(device)
        {
            *self = VirtioKeyboard__new(&device);
        },
        none()
        {
            return false;
        }
    );

    VirtioKeyboard__acknowledge_driver(self);
    if (!VirtioKeyboard__setup_event_queue(self))
    {
        return false;
    }
    if (!service_ok(InterruptClient__subscribe(self->irq_id)))
    {
        DRIVER_DEBUG("virtio_keyboard: irq subscribe failed\n");
        return false;
    }

    return true;
}

int VIRTIO_KEYBOARD_ENTRYPOINT(void)
{
    static VirtioKeyboard keyboard;
    if (!VirtioKeyboard__init(&keyboard))
    {
        return 1;
    }

    static IOStreamDriver driver;
    static IOService service;
    driver = IOStreamDriver__new_terminal(IO_OPEN_READ, &keyboard, VirtioKeyboard__read);
    if (IOService__listen_into(&service, &bstr(IO_KEYBOARD_SERVICE), &driver, IOStreamDriver__execute,
                               IOStreamDriver__disconnect) != IO_OK)
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&bstr(IO_KEYBOARD_SERVICE))))
    {
        return 1;
    }
    bool pending_irq = false;
    Channel__prepare_wait();
    while (true)
    {
        bool progress = IOService__poll(&service);
        ServiceEvent event;
        ServiceCode status = InterruptClient__poll(&event);
        if (status == SERVICE_OK)
        {
            VirtioKeyboard__ack_interrupt(&keyboard);
            pending_irq = true;
            progress = true;
        }
        else if (status != SERVICE_EVENT_QUEUE_EMPTY)
        {
            return 1;
        }
        if (pending_irq)
        {
            VirtioKeyboard__drain_used_events(&keyboard);
            if (keyboard.last_used_idx == VirtioKeyboard__used(&keyboard)->idx)
            {
                if (!service_ok(InterruptClient__acknowledge(keyboard.irq_id)))
                {
                    return 1;
                }
                pending_irq = false;
                progress = true;
            }
        }
        if (!progress)
        {
            Channel__wait_event();
        }
    }
}
