# Userspace interrupt service

## Status and scope

This architecture now has an initial implementation in `hund-unterbrict` and
the separate `virtio_blk_channelio` driver. It builds on
[Channels and event queues](channels-and-event-queues.md) and the
[queue transport](queues.md) design.

The kernel-to-manager transport is a fixed per-IRQ coalescing queue. The kernel
masks an IRQ before publishing it, so every routed IRQ has a dedicated pending
slot and queue capacity cannot discard another IRQ. Driver control and delivery
messages use persistent bidirectional `Channel` sessions; the interrupt
protocol does not define another channel or expose its internal queues.
All VirtIO disks, including the root and boot-configuration disks, now select
`virtio_blk_channelio`. Hundeschule starts the embedded channel manager and
interrupt server and waits for both before probing disks. Their startup does
not depend on disk configuration. The legacy kernel IRQ subscription and wait
APIs remain for other unmigrated callers; the old `virtio_blk` driver has been
deleted. `virtio_blk_channelio` owns the hardware implementation and uses only
its fibre runtime and interrupt-manager channel.

The objective is to keep device IRQ routing and acknowledgement policy in
userspace. Hardware interrupt entry still runs in the kernel. Registration,
authentication, and memory setup may require syscalls; routine driver
notification and acknowledgement should not.

## Responsibilities

The kernel owns exception entry, privileged GIC CPU-interface operations,
process identity, token validation, and process-exit notifications. It retains
timer interrupts and scheduling IPIs. For delegated device IRQs it masks the
source and completes the required GIC end-of-interrupt/deactivation sequence
before returning to userspace, publishing a notification to the interrupt
service's shared event queue.

`hund-unterbrict` owns device IRQ registrations, delivery to consumers,
outstanding acknowledgements, and rearming. It receives a kernel-authorized
mapping of the GIC distributor registers needed to manage delegated IRQs.
It consumes kernel IRQ events together with channel requests in one logical
event loop using event waits. Kernel notifications use a one-way
`KernelNotificationQueue`; each driver uses a separate `Channel`. The queue
transport beneath a channel is not part of the interrupt protocol.

Device drivers own their device MMIO registers. They service the device and
clear its interrupt source before acknowledging completion to the interrupt
service. They do not map the GIC themselves.

The GIC mapping makes the interrupt service highly trusted: register pages
control multiple IRQs, so page permissions cannot enforce per-IRQ ownership.
The service must not alter kernel-owned IRQs. Kernel masking and userspace
rearming must have a defined ordering; neither side should perform conflicting
read-modify-write operations on shared controller state.

`hundekanal-warter` continues to own channel allocation and lifetime. It does
not become responsible for interrupt routing or device acknowledgements.

## Registration and authentication

Processes register through a channel to `hund-unterbrict`, not through a direct
device-driver IRQ subscription syscall. Registration requests identify the IRQ
through the requesting connection. Only kernel-validated `hund` processes may
subscribe. A claimed PID, process name, or service-directory entry is not proof
of eligibility.

Connection-bound validation replaces a separate bearer token in the initial
implementation. The kernel-authenticated warden connection supplies the
driver's PID as server-side channel metadata; it is not taken from the driver
payload. The manager passes that PID to the kernel, which resolves the live
process and checks its IRQ capability. Invalid or dead clients receive an
authorization error and do not change registration state.

Each device IRQ has at most one registered consumer. After authentication, a
request for an occupied IRQ receives an explicit already-registered error on
its channel, including when the existing consumer is the same process. It must
not replace the consumer or silently steal the registration. Kernel-reserved
and unsupported IRQs also receive explicit errors. Registration and reply
delivery must preserve consistent ownership when a queue is full.

Legacy globally writable service queues and client-claimed identities are not
an authenticated transport. Registration therefore uses warden-managed
channels backed by kernel-mediated, participant-only queue mappings. Connection
setup and initial route authorization incur syscalls; subsequent IRQ deliveries
and acknowledgements remain channel exchanges in userspace.

## Interrupt and acknowledgement flow

1. The kernel accepts a delegated IRQ, masks its GIC source, and completes the
   privileged CPU-interface handling. It records and publishes the IRQ event
   to `hund-unterbrict`.
2. The service records an outstanding delivery and notifies the registered
   consumer through its channel. The notification identifies the IRQ,
   registration generation, and delivery sequence.
3. The driver services the device, including MMIO acknowledgement of its
   interrupt source and required memory/device ordering.
4. The driver sends a completion acknowledgement over its channel to
   `hund-unterbrict`, identifying that delivery.
5. The service validates the consumer and delivery identity, retires the
   outstanding acknowledgement, and unmasks the IRQ through GIC MMIO.

An acknowledgement from another process, an old registration, or an already
completed delivery must not rearm the IRQ. Shared-memory publication and
consumption use release/acquire ordering and event notification. Waiting for
completion must not hot-spin or block processing other consumers' events.

GIC acknowledgement, GIC deactivation, device acknowledgement, and the channel
completion message are separate operations. A device can remain asserted after
the kernel finishes its CPU-interface handling. Keeping its GIC source masked
until userspace completion prevents repeated delivery during that interval.

Channel backpressure must not lose an IRQ or its outstanding completion state.
Retain pending notifications for retry and leave the source masked until its
delivery can complete. The same reliability requirement applies to process-exit
events used for cleanup; current bounded-queue exit delivery is not sufficient
to guarantee this without overflow recovery.

## Consumer death and cleanup

The target protocol must define how clients release IRQ registrations while the
channel session remains open; the operation belongs to the interrupt protocol
and need not be named close. Clients separately close the channel
session when finished with it. Session close and kernel-reported client death
must also clean up registrations left behind by the client. Cleanup across these
layers must be idempotent, and orphaned IRQs must remain masked.

The required design includes reliable kernel process-exit events; this cleanup
path remains implementation work. If the process whose
acknowledgement it is waiting for dies, it completes that outstanding software
acknowledgement on the process's behalf rather than waiting forever. It removes
the process's IRQ registrations and cached authorization and invalidates their
delivery generations. Cleanup must also handle death before a notification is
delivered or while a registration response is pending.

Completing an acknowledgement on death does not mean the hardware device has
been serviced. The service must leave the orphaned IRQ masked: the dead driver
may have left a level-triggered source asserted. A replacement consumer must
initialize or quiesce the device before delivery is enabled again. Cleanup must
not blindly unmask an unowned IRQ or pretend to clear device-specific status.
Late acknowledgements from the old registration cannot affect its replacement.

Channel release remains subject to the warden's endpoint cleanup rules. Ending
an IRQ registration does not itself free channel memory still in use.

## Future acknowledgement timeouts

Later, track acknowledgement deadlines and detect live but unresponsive
consumers. Timeout handling should report the failed delivery and leave the
IRQ masked until explicit recovery. It must not manufacture successful device
completion or repeatedly rearm an asserted source. Timer sources, deadlines,
and recovery policy are deferred; do not add unused timeout machinery now.

## Implementation verification

- Valid `hund` registration, invalid tokens, cache hits, exit invalidation,
  PID reuse, and rejection of forged endpoint identity.
- Duplicate and reserved-IRQ registration errors returned on the channel.
- IRQ delivery, device-before-service acknowledgement ordering, and exactly
  one rearm for a valid completion; stale and duplicate ACK rejection.
- Channel-reply retry without lost notifications or premature rearming.
- Consumer death before delivery and while awaiting an ACK, with registration
  removal, cache invalidation, and the orphaned source remaining masked.
- Block reads on a dedicated QEMU test disk with no per-completion wait or
  rearm syscall. Kernel interfaces should be stubbed in userspace tests.

Interrupt-service failure also needs a fail-closed kernel cleanup path before
deployment: delegated sources must remain masked if their manager exits.
