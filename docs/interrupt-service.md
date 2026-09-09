# Userspace interrupt service

## Status and scope

This is the proposed architecture for `hund-unterbrict`, not an implemented
service. It builds on [Channels and event queues](channels-and-event-queues.md).
Its transport should follow the
[atomic message queues](atomic-message-queues.md) design: kernel-mediated
connection establishment, isolated client queues, and protected kernel events.
The connection API and migration of the warden-managed echo channels are
implemented. Protected general kernel-event delivery, overflow recovery, and
integration of those events with the connection/fibre loop remain prerequisites.
The initial consumer will be a channel-based MMIO block driver; filesystem
integration comes later. The existing kernel IRQ subscription and wait APIs
remain in place until their callers are migrated.

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
It consumes kernel IRQ and process-exit events together with channel requests
in one logical event loop using fibre/event waits. Under the atomic-message-queue
proposal, these arrive through separate protected kernel and per-client queues,
not one globally writable ring. Wakeup aggregation remains to be designed.

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
device-driver IRQ subscription syscall. Registration requests identify the IRQ,
request correlation ID, and reply channel, and include an authentication token.
Only kernel-validated `hund` processes may subscribe. A claimed PID, process
name, or service-directory entry is not proof of eligibility.

The interrupt service calls the kernel to validate an unfamiliar token. The
validation contract must bind the token to a live process incarnation and its
`hund` authorization, and bind registration to that authenticated process's
channel. The service caches successful validation results so subsequent
requests do not each require a syscall. Invalid tokens receive an authorization
error on the requesting channel and do not change registration state.

Cache entries must be scoped to the token and process incarnation, not merely
a reusable numeric PID. Process exit invalidates the corresponding entries.
Any token expiry or authorization revocation supported by the kernel must also
invalidate cached authorization before it can be reused. The token issuance,
validation ABI, and invalidation mechanism are implementation requirements,
not existing APIs; the current login-ticket API is not assumed to satisfy them.

Each device IRQ has at most one registered consumer. After authentication, a
request for an occupied IRQ receives an explicit already-registered error on
its channel, including when the existing consumer is the same process. It must
not replace the consumer or silently steal the registration. Kernel-reserved
and unsupported IRQs also receive explicit errors. Registration and reply
delivery must preserve consistent ownership when a queue is full.

Legacy globally writable service queues and channel control descriptors
are not an authenticated transport. The warden-managed echo path no longer uses
them; its control records are private and its connections are isolated.
A token placed in globally readable memory
cannot safely be treated as a secret bearer credential. Before claiming secure
authorization, the implementation must provide protected token transport and
endpoint binding, or a validation protocol that remains safe when request
contents are observable. Validating a copied token and trusting a supplied PID
alone is insufficient. The channel demonstration now supplies isolated transport;
token issuance, validation and authorization binding are still prerequisites.

The preferred solution is kernel-mediated queue establishment with authenticated
peer descriptors and participant-only mappings, as described in
[Atomic message queues](atomic-message-queues.md). Requests for a connection
incur setup syscalls; subsequent IRQ requests and ACKs remain userspace messages.
Signing requests is an alternative for message authenticity, not a defense
against another writer destroying a shared queue. Token validation and caching
remain required for `hund` authorization even with authenticated transport.

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

Queue saturation must not lose an IRQ or its outstanding completion state.
Retain pending notifications for retry and leave the source masked until its
delivery can complete. The same reliability requirement applies to process-exit
events used for cleanup; current bounded-queue exit delivery is not sufficient
to guarantee this without overflow recovery.

## Consumer death and cleanup

The service subscribes to kernel process-exit events. If the process whose
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
- Queue-full retry without lost notifications or premature rearming.
- Consumer death before delivery and while awaiting an ACK, with registration
  removal, cache invalidation, and the orphaned source remaining masked.
- Block reads on a dedicated QEMU test disk with no per-completion wait or
  rearm syscall. Kernel interfaces should be stubbed in userspace tests.

Interrupt-service failure also needs a fail-closed kernel cleanup path before
deployment: delegated sources must remain masked if their manager exits.
