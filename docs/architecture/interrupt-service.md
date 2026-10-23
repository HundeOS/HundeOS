# Userspace interrupt service

## Status and scope

This architecture now has an initial driver-host implementation in
`hund-unterbrict`. VirtIO block driver instances execute inside that process;
other interrupt consumers still use the client Channel protocol. It builds on
[Channels and event queues](channels-and-event-queues.md) and the
[queue transport](queues.md) design.

The kernel-to-manager transport is a fixed per-IRQ coalescing queue. The kernel
masks an IRQ before publishing it, so every routed IRQ has a dedicated pending
slot and queue capacity cannot discard another IRQ. Driver control and delivery
messages use persistent bidirectional `Channel` sessions; the interrupt
protocol does not define another channel or expose its internal queues.
All VirtIO disks, including the root and boot-configuration disks, are probed
and registered by `hund-unterbrict` before it publishes readiness. The reusable
`virtio_blk_channelio` runtime owns the hardware implementation and exposes the
same block Channels as before, but hosted instances receive IRQs by direct local
dispatch. The standalone executable remains as a compatibility harness while
the hosted path is validated; normal bootstrap does not spawn it.

The objective is to keep device IRQ routing and acknowledgement policy in
userspace. Hardware interrupt entry still runs in the kernel. Registration,
authentication, and memory setup may require syscalls; routine driver
notification and acknowledgement should not.

The agreed next architecture evolves this service into
`hunde-treiber-host`, which hosts all hardware-facing drivers while preserving
the interrupt masking, device acknowledgement, rearming, and external Channel
rules in this document. High-level filesystem, socket, stream, and cache policy
belongs in `hunde-io-host`. See
[I/O and driver hosts](io-driver-hosts.md).

## Responsibilities

The kernel owns exception entry, privileged GIC CPU-interface operations,
process identity, token validation, and process-exit notifications. It retains
timer interrupts and scheduling IPIs on every CPU. For delegated device IRQs it
masks the source and completes the required GIC
end-of-interrupt/deactivation sequence
before returning to userspace, publishing a notification to the interrupt
service's shared event queue.

Notification publication completes before the interrupt manager is made
runnable. If the manager is parked in a queue wait, the kernel completes that
wait and immediately reschedules its owning CPU, using an IPI when the IRQ was
handled on another CPU. `SEV` remains a hint for resident userspace waiters and
is not an IRQ acknowledgement. See [queue waits](queues.md#bounded-waits).

`hund-unterbrict` owns device IRQ registrations, hosted hardware drivers,
delivery to external consumers, outstanding acknowledgements, and rearming. It receives a kernel-authorized
mapping of the GIC distributor registers needed to manage delegated IRQs.
It consumes kernel IRQ events together with channel requests in one logical
event loop using event waits. Kernel notifications use a one-way
`KernelNotificationQueue`; each driver uses a separate `Channel`. The queue
transport beneath a channel is not part of the interrupt protocol.

Hosted driver instances own their device MMIO registers and use per-instance
DMA rings and fibre schedulers. An IRQ notification is matched to the owning
instance and wakes its fibre without a Channel exchange or another process
scheduling decision. External device drivers retain the Channel flow below.

Device drivers service the device and
clear its interrupt source before acknowledging completion to the interrupt
service. They do not map the GIC themselves.

The GIC mapping makes the interrupt service highly trusted: register pages
control multiple IRQs, so page permissions cannot enforce per-IRQ ownership.
The service must not alter kernel-owned IRQs. Kernel masking and userspace
rearming must have a defined ordering; neither side should perform conflicting
read-modify-write operations on shared controller state.

`hundekanal-warter` authorizes setup, then delegates a direct Channel between the
driver and interrupt service. The kernel owns that connection's lifetime. The
warden does not route interrupt messages or device acknowledgements.

## Registration and authentication

Processes register through a channel to `hund-unterbrict`, not through a direct
device-driver IRQ subscription syscall. Registration requests identify the IRQ
through the requesting connection. Only kernel-validated `hund` processes may
subscribe. A claimed PID, process name, or service-directory entry is not proof
of eligibility.

Connection-bound validation replaces a separate bearer token in the initial
implementation. The kernel-authenticated direct connection supplies the
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
an authenticated transport. Registration therefore uses warden-authorized,
kernel-delegated Channels backed by participant-only Queue mappings. Connection
setup and initial route authorization incur syscalls; subsequent IRQ deliveries
and acknowledgements travel directly between the driver and interrupt service.

## Interrupt and acknowledgement flow

After successful privileged interrupt-manager registration, the kernel promotes
`hund-unterbrict` to the realtime-service process class. This class has the
highest runnable priority but does not reserve a CPU or disable scheduling
ticks. The manager drains IRQ and hosted-driver work, then parks through its
normal Channel wait. Other work can use the CPU while the manager is parked.

1. The kernel accepts a delegated IRQ, masks its GIC source, and completes the
   privileged CPU-interface handling. It timestamps and publishes the IRQ event
   to `hund-unterbrict`.
2. The service records an outstanding delivery and notifies the registered
   consumer through its channel. The notification identifies the IRQ,
   registration generation, delivery sequence, kernel IRQ timestamp, and the
   time at which the manager consumed the kernel notification. The shared
   interrupt client records when the driver receives that delivery.
3. The driver services the device, including MMIO acknowledgement of its
   interrupt source and required memory/device ordering.
4. The driver sends a completion acknowledgement over its channel to
   `hund-unterbrict`, identifying that delivery.
5. The service validates the consumer and delivery identity, retires the
   outstanding acknowledgement, and unmasks the IRQ through GIC MMIO.

On RISC-V PLIC targets, context-enable bitmap words are kernel-owned routing
configuration, updated when an IRQ is registered. Runtime masking writes zero
to that IRQ's priority register; rearming writes priority one. Kernel and
userspace use the same per-source register layout in
`common/arch/riscv64/plic.h`, with I/O fences after writes. Mask-before-publish
and device-acknowledge-before-rearm ordering still apply. Only the registered
IRQ owner rearms its source, and the other source priorities remain unchanged.
Interrupt-manager registration maps only the 4 KiB priority page and returns
its unshifted base, regardless of the physical target hart. The old enable-word
interface biased the returned address by hart ID; retaining that bias would
write another IRQ's priority on nonzero bootstrap harts. Context-enable words
are outside the manager's supplied PLIC mapping.

Neither side may read-modify-write a shared context-enable word for runtime
mask/rearm. Concurrent updates for different IRQs can restore stale bits and
silently disable a source, leaving a completed disk request awaiting an
interrupt forever. Per-source priority writes avoid that race without adding
interrupt acknowledgement syscalls. Hosted register tests check neighboring
IRQs and enable-word preservation; live acceptance repeatedly creates, lists,
and removes directories while terminal IRQ traffic is active.

On RISC-V systems where the interconnect rejects a U-mode access to otherwise
permitted MMIO, the architecture exception handler transparently repeats the
faulting integer load or store through the physical device's supervisor mapping.
The existing readable or writable userspace MMIO mapping is the authorization;
the handler does not add interrupt-controller policy. It resumes the process
only when the decoded access range is contained by that mapping and its
translated physical address matches the hardware fault address. Keeping the
transaction off the user PTE is required on the CV181x, where an S-mode store
through a user mapping can retire but fail to complete at the following I/O
fence. This also covers the load half of compiler-generated read-modify-write
sequences. The CV181x may report a rejected store as load-access cause 5 rather
than store-access cause 7, so a load-access fault is decoded as a load first and
then as the platform's store quirk.

For a hosted driver, steps 2, 4, and 5 are local calls: the manager records the
kernel and manager timestamps, wakes the matching driver fibre, and the driver
requests rearm directly after acknowledging its device. No interrupt-protocol
Channel is created for a hosted instance.

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

The timestamps carried with the existing delivery are advisory diagnostics;
they do not add another message or acknowledgement. VirtIO block uses them,
together with timestamps at fibre resumption and used-ring observation, to
separate emulator/device latency from manager wakeup, userspace delivery, and
driver scheduling latency. A completion observed without an IRQ still counts
in overall block latency but cannot produce a segmented sample.

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

Channel release follows the kernel's endpoint detachment rules. Ending an IRQ
registration does not itself free Queue memory still mapped by a participant.

## Future acknowledgement timeouts

Later, track acknowledgement deadlines and detect live but unresponsive
consumers. Timeout handling should report the failed delivery and leave the
IRQ masked until explicit recovery. It must not manufacture successful device
completion or repeatedly rearm an asserted source. Timer sources, deadlines,
and recovery policy are deferred; do not add unused timeout machinery now.

## Shared driver client

`user/lib/interrupt/client.c` provides the common Channel-based subscribe,
poll/wait, and acknowledge client used by VirtIO block, VirtIO keyboard, and UART receive drivers.
The driver clears its device-specific interrupt source before acknowledging
the delivery to `hund-unterbrict`; only the interrupt manager requests kernel
rearm. UART receive retains a pending delivery while its userspace input buffer
is full, rather than rearming an undrained level-triggered source.
VirtIO keyboard likewise retains its delivery and unconsumed used-ring events
until stream reads make space for complete terminal key sequences.

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
