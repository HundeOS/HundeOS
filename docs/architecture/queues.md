# Queues

Kernel executable-loader clients use the same protected connection transport.
`QUEUE_KERNEL_PID` is reserved for connections created by `Queues__connect_kernel`;
userspace cannot request that identity. The kernel retains backing addresses while
the server receives its usual protected mappings. Kernel-held backing is released
only after both endpoints detach. The warden may allocate a shared payload with
this reserved peer only while it has a live kernel connection. Allocation
generations distinguish reused shared-page slots. See
[kernel Channel clients](channels-and-event-queues.md) for access checks.

`Queue` is an internal transport primitive used to implement `Channel`. It is
not a service-facing bidirectional IPC abstraction. Application protocols must
use `Channel` and must not expose queue handles, queue messages, cursors, or
queue lifecycle. New bidirectional IPC built directly on this API is an
architectural violation.

The public architectural rule and the permitted shared-buffer model are defined
in [Channels and event queues](channels-and-event-queues.md). This document
records the lower-level transport implementation and its security invariants.

## Status

The kernel-mediated queue API is described alongside channel lifecycle in
[Channels and event queues](channels-and-event-queues.md). Its API and limits
are described below. The remaining architectural sections describe the target
design. The warden and channel echo demo now use these connections and private
control records. A protected general kernel-event stream remains unimplemented;
the warden currently uses a bounded multi-connection scan around fibre/event waits.

This foundation is a prerequisite for authenticated services such as
[`hund-unterbrict`](interrupt-service.md), not an implementation of IRQ routing
or token authorization.

## Bounded Waits

Userspace captures its process doorbell generation before polling queue and
notification state, then enters `SERVICE_QUEUE_WAIT` with that generation and an
optional deadline. Under the scheduler lock, the kernel compares the live
doorbell, scans its own queue backing for unread messages, pending listener
arrivals, and peer closure, and checks live service, IRQ, buffer, and signal state
before parking. No connection handles, cursor values, listener epochs, or wait-set
snapshots cross the syscall boundary.

Every voluntary wait reschedule and timer-driven scheduler entry checks queue
waiters owned by every CPU before selecting the next process. In addition, each
successful queue push issues an authenticated data-available notification after
publishing the shared cursor and ringing the receiver's doorbell. A pop publishes
consumption and rings the sender's shared-memory doorbell, but does not make a
syscall. The kernel derives the receiver from the live connection rather than
accepting a userspace PID. If that receiver is parked on a kernel queue wait whose
doorbell or live state has changed, the kernel makes it runnable
immediately and sends its CPU a reschedule IPI. A ready or interrupted receiver
with an armed userspace wait publication is also poked, forcing an immediate
scheduling decision instead of waiting for the next timer tick. A running receiver
is not interrupted: the queue publication's architecture event already releases
its event wait. The notification decision uses the kernel's live unread-queue
state, not the userspace wait publication.
If the peer has not parked yet, the park-time generation and live-state recheck
observes the progress and refuses to sleep. Timer and voluntary-yield scheduler
entries rescan parked processes against current kernel state, covering consumption
notifications that deliberately avoid a syscall. A `QUEUE_WAIT_CHANGED` return is
itself sufficient for userspace to re-poll live queue state; it does not require a
second doorbell-generation change.

Queue listen/connect allocates the private doorbell page. The first wait
registration obtains the private shared wait-control allocation through
`SERVICE_QUEUE_WAIT_CONTROL`. The control publication contains the observed
process doorbell generation and remains available to the dependency scheduler and
diagnostics. A kernel-parked operation separately records its observed generation
in kernel process state; the userspace control page is not authoritative for it.

Each connection maps the peer process's doorbell at the connection's fixed alias
address. `Queue__push` publishes the produced cursor before incrementing the
receiver's doorbell, and `Queue__pop` publishes consumption before incrementing
the sender's doorbell. Protocol code therefore does not ring doorbells itself.
Kernel-side acceptance, closure, listener arrival, service event, and signal
publication ring the affected process's owned doorbell. A generation is only a
wake predicate: increments may be coalesced and are not an event count.

The data-available syscall does not carry message data or decide readiness; it
authenticates a connection and prompts the kernel to recheck existing shared state.
The control allocation persists until process exit and cannot be explicitly freed
or shared with another process. An incomplete or changing control publication
leaves a userspace-idle process runnable. Normal waits allocate a deferred kernel
operation when they actually park. Kernel notification producers ring the process
doorbell before issuing the architecture wake, so both a resident waiter and the
scheduler observe the same generation transition.

This gates each transport hop on its actual queue progress. Explicit propagated
IRQ tokens, hop numbers, stage acknowledgements, and operation-timeout results
remain to be implemented; this is not a complete implementation of that protocol.

### Receiver Scheduling Priority

The [ticket-aware preferred run queue](dependency-scheduling.md) extends this
policy with authenticated parent links, cross-CPU FIFO selection, and residency.
The readiness checks below also support fallback selection.

At process selection, runnable processes with unread incoming messages rank above
all runnable processes without messages, including realtime-class processes.
The preferred FIFO is selected first; class and longest-waiting ordering applies
to fallback selection. Preferred processing can continue after dequeue, up to
its budget or next shared wait. A ticketed predecessor may retain its CPU through
a short warm wait. Kernel-blocked processes do not become runnable merely because
a message exists.

The kernel checks its existing queue backing mappings, comparing the sender's
published cursor with the receiver's acknowledgement on the opposite page.
Only live, accepted connections with a pending count within queue capacity count.
The check applies symmetrically to requests, replies, and warden relay messages,
without interpreting message kinds. Pending service events and the IRQ/buffer
manager notification queues also qualify. Unread transport on any connection
owned by the process overrides an unchanged userspace wait publication. The
publication describes process-wide idleness, not a userspace-selected subset of
connections. This prevents a newly attached or otherwise omitted connection from
leaving runnable work asleep. Consuming the last message removes unread-message
eligibility but does not end an already dispatched process's preferred budget.

This is strict receiver priority: sustained unread traffic can starve processes
without messages. Shared cursor values remain untrusted scheduling hints, not
proof of useful work. Queue progress can introduce a reschedule interrupt when it
makes a parked peer runnable. Preferred selection now supports cross-CPU
migration. Tests cover receiver direction, consumption, cursor bounds/wraparound,
shared waits, timer retention, yielding, and blocked-process eligibility,
including unread work arriving on a connection absent from the preceding
userspace scan.

On AArch64, publication completes with `DSB ISHST` before `SEV`; a waiter compares
the armed generation and uses `WFE`. An event arriving between the check and
`WFE` remains pending. On RISC-V, the waiter establishes an `LR` reservation on
the generation and uses `WRS.NTO`; the peer's atomic generation update invalidates
that reservation. The RISC-V QEMU targets enable the `Zawrs` extension explicitly.
Event and arrival waits return when the process doorbell changes, then recheck
their authoritative local predicates. Synchronous queue waits use the same
process doorbell. `ChannelSignal` waits directly on its data or space epoch because each
such wait has a single authoritative address. All waits recheck until ready or
closed. Wakeups may be coalesced or spurious: they are never a message count and
do not by themselves cause another dispatch scan.

Device IRQ dispatch broadcasts an event after the handler publishes state.
Timer preemption remains enabled, without a global broadcast on every tick.
Context restoration sets the local event latch with `SEVL`, allowing resumed
waiters to recheck even if another process consumed an earlier event on that CPU.
Ready fibres run before queue waits. `WFE` is not a scheduler yield and does not
make kernel-parked processes runnable. Normal queue waits now enter the kernel
immediately and increment park counters; they carry no timeout. Architecture
event waits remain available to lower-level signal primitives whose process is
still resident.

On AArch64, boot hardware information selects whether the wait primitive uses
`WFE` or an active `YIELD` recheck. Hardware and the single-threaded TCG icount
trace target use `WFE`; HVF and ordinary MTTCG use the spin mode because their
event-wait behavior does not provide the required sleep semantics. RISC-V uses
`WRS.NTO` on the doorbell reservation. The
`qemu-virt-tcg-icount-trace` target exposes four guest CPUs but serializes them
on one host TCG thread, so it is suitable for wait behavior and scheduler traces,
not parallel-throughput measurements.

The stable publication includes a userspace monotonic timestamp taken when the
wait is committed. Process snapshots report `USER_WAIT` and its elapsed duration
only while the publication is stable and its observed doorbell generation still
matches live kernel-visible state. The timestamp is
diagnostic-only and does not influence selection or CPU accounting.

The retired snapshot design is described below.

### Isolated Timeout Probe

Run `queue_wait_probe 1` and `queue_wait_probe 10` to exercise only the calling
process's kernel wait, leaving all services in userspace event-wait mode. The optional argument
is a timeout in scheduler ticks (1 through 100; default 10). Each run makes 16
wait syscalls with the process's observed doorbell generation and prints one
summary afterward.
Timeout samples are timed from userspace immediately before the syscall to
userspace immediately after it returns; the minimum, average, and maximum are
in microseconds. Activity returns and failures are counted separately and do not
contribute to timeout durations. There is no per-sample console output.

This measures the complete timeout-call path, including entry, parking,
scheduling, and deferred completion. It does not measure message publication to
wake detection, nor reproduce every service parking concurrently. Keep the same
QEMU target and tracing settings when comparing runs.

After the timed calls, the probe waits up to five seconds for a fresh read-only
process-registry sample and prints its kernel park and runnable totals. This
sampling wait is userspace polling and excluded from call durations. Verify
`kernel_parks`, `kernel_timeouts`, and `kernel_selections` equal 16 and activity
is zero before comparing these totals with the timeout-call total. Subtraction
gives time outside the recorded parked/runnable intervals, not an attribution to
one kernel function; it includes entry/completion work and possible preemption
around the userspace timing boundaries.

#### SDHCI Trace Measurement

On 2026-09-18, `qemu-pi-uefi-firmware-trace` with all services polling and only
the probe parking produced the following averages over 16 calls. Both probe
processes were assigned CPU 0; every call timed out, with no activity or errors.

| Requested ticks | Full call (ms) | Parked (ms) | Runnable (ms) | Outside counters (ms) |
| --- | --- | --- | --- | --- |
| 1 | 20.376 | 2.063 | 0.324 | 17.988 |
| 10 | 42.164 | 22.643 | 0.328 | 19.194 |

The one-tick totals were 326010 call, 33010 parked, and 5188 runnable microseconds.
The ten-tick totals were 674622 call, 362284 parked, and 5240 runnable microseconds.
The existing trace shows each park allocating 72-, 104-, and 24-byte objects,
each backed by one page, and freeing them on completion. Allocation, trace output,
and entry/completion paths warrant separate measurement; these results do not
prove which dominates. Queue-publication-to-detection latency remains unmeasured.
This is not an all-services-parking comparison or a claim about untraced hardware.

### Retired Per-Queue Snapshot Design

This records the retired design, not the current wait path.

The former implementation registered each userspace queue, snapshotted cursors,
accepted/closed flags, and listener epochs, then passed a selected snapshot array
to `SERVICE_QUEUE_WAIT`. Correctness therefore depended on userspace registration
remaining complete as connections changed.

The snapshot array and all kernel per-connection/per-listener stored observations
have been removed. The syscall now carries one process doorbell generation. The
kernel records it only after checking current kernel-owned state under the
scheduler lock and never retains a userspace pointer.

Initially, the owning CPU's scheduler tick checks parked waits. A changed cursor,
connection state, or listener epoch makes the process runnable. Pending service
events, IRQ-manager notifications, and buffer-manager notifications also wake it.
This keeps publication syscall-free but adds up to one scheduler tick of detection
latency, plus normal scheduling delay. `SEV` alone does not wake a parked process.

Every wait also expires after N scheduler ticks, even if no observation changed.
The userspace default is 10 ticks; the syscall accepts 1 through 100. A timeout is
not an I/O failure: control returns to the userspace loop to recheck other work.
The former polling budget used the architecture monotonic counter and a nonblocking
relax instruction. Only the retained syscall tick bounds remain in `common/ipc/queue_wait.h`.
No performance improvement is assumed without measurement.

## Initial queue API

- `Queue__listen(name)` requires a service with the become-service capability.
  It returns a read-only arrivals region mapped only into the server. One listener
  per process is supported; its lifetime currently ends on process exit.
- `ServiceDirectory__find_endpoint(directory, name)` returns an optional endpoint
  handle without a syscall. Protected entries have no public queue address;
  their generation is the endpoint handle. Legacy queue lookup skips these entries.
- `Queue__connect(endpoint)` calls the kernel once to establish a pending
  connection. The kernel supplies the caller identity and validates the endpoint
  incarnation; numeric handles do not grant access to an existing connection.
- The server scans the arrival slots and calls
  `Queue__accept(connection)` to activate a connection, or `QueueConnection__close()` to
  reject it. Arrival slots remain present until the server closes the connection.
  They are durable connection records, not a lossy notification ring.
  The slots use the generic `AtomicFixedArray` type and
  `for_each_atomic_fa_ptr` for traversal. Kernel writes use release stores;
  each visited slot uses an acquire load. Clearing a slot never moves another
  handle. The slots retain their offset after the endpoint/epoch header; the
  expanded table is backed by a page-rounded arrivals region.
- `Queue__push/pop` return Result values and exchange fixed-size
  `QueueMessage` values entirely in userspace. Each direction has 32
  slots. Full, empty, closed, pending, and corrupt states are errors rather than
  success/status values. `pop` replaces the supplied
  sender PID with the kernel-established peer PID. Message kinds are still peer
  input: a kernel-event-looking kind does not authenticate a kernel event.
- `Queue__new` is an infallible constructor for already resolved queue
  pages and peer identity. `Queue__attach` validates a shared identity,
  determines the endpoint direction, and returns a Result.
- `Queue__wait_readable/writable` recheck state after `WFE`, without kernel parking.
  Event loops watch locally registered connections and listeners and return to
  dispatch on any event. Ready fibres run before waiting. See [bounded waits](#bounded-waits).
- `QueueConnection__close(connection)` is the calling endpoint's final acknowledgement:
  stop all local users of that handle before calling it. The kernel publishes
  closed state and removes the caller's connection mappings. It also removes the
  survivor's alias to the departing process's doorbell before that doorbell can
  be freed. The surviving endpoint retains the three connection pages until it
  closes too. Once both are detached, connection backing is freed after
  unmapping/TLB invalidation. Process exit performs the same detachment. The
  legacy process-exit event queue has been removed.
  A backing-free failure after detachment does not turn close into a failed
  detach: the kernel quarantines the backing for retry on a later connect and
  returns success. Callers must discard their handle after successful close.

The arrivals region starts at `0x74000000`, with three pages per listener slot
to hold 1024 connection IDs and the endpoint/epoch header. Allocation, mapping,
overlap checks, and teardown use the page-rounded `QUEUE_ARRIVALS_SIZE`.
Connections start at `0x72400000`, with three pages per connection slot; their
12 MiB address range ends at `0x73000000`. Neither region overlaps the kernel
notification queue at `0x72300000`.

Each process owns a private doorbell page at `0x722ff000`. Connection-specific
aliases to peer doorbells occupy `0x73000000` through `0x73400000` (exclusive),
one page per connection slot. The aliases are mapped only into the two
participants and are removed as their corresponding endpoints detach.

| Page | Client | Server | Contents |
| --- | --- | --- | --- |
| Identity | Read-only | Read-only | Connection/endpoint handles, peer PIDs, accepted/closed state |
| Client-owned | Read/write | Read-only | Client messages and acknowledgement of server messages |
| Server-owned | Read-only | Read/write | Server messages and acknowledgement of client messages |

Unrelated processes receive none of these mappings. Slots cannot reuse backing
while a surviving endpoint retains its mappings. New handles use a monotonic
generation so stale accepts/closes cannot affect a replacement. Existing tracked
allocations overlapping the chosen virtual range cause setup to fail.

The bounds are 1024 listener slots, 1024 connections system-wide, and 64 active
client connections per ordinary process. Services with shared-page management
capability may broker connections up to the system-wide limit. Listeners share the 1024-entry service
directory. Admission limits are fixed, not yet configurable. Each
direction is single-producer/single-consumer; callers must serialize fibres and
retain their private cursor state instead of attaching twice to a live stream.
Peer-owned cursor values are checked before indexing or accepting progress.
`QUEUE_LIMIT` bounds connections, not queued messages; `QUEUE_CAPACITY` remains
32 messages per direction. Listener slots use the separate `QUEUE_LISTENER_LIMIT`.

Kernel operations serialize with process exit under the scheduler lock; none
context-switch while holding it. Connection setup allocates backing before
publishing arrivals. The underlying `PageTable__map` API still does not report
internal page-table allocation failure, so fully recoverable mapping rollback
remains a limitation. Authorization against a service with unrestricted physical
MMIO mapping powers is also outside this page-isolation guarantee.

Run `queue_test server &` from the root shell, then `queue_test` for 128 echo
round trips on one connection. No boot configuration is changed automatically.
The program is a transport test, not the warden-managed channel echo service.
`queue_test probe-identity` and `queue_test probe-peer` deliberately attempt
writes to protected pages and should terminate with process permission faults.
Run a normal client afterwards to verify the server continues after cleanup.

The shared ABI and declarations live in `common/ipc/queue.h` and
`queue_layout.h`. Architecture-neutral implementations are compiled
from `common/ipc/queue.c` and `queue_layout.c` into separate kernel and
userspace objects. Service discovery remains in `common/services/service_directory.c`.
Event signalling is supplied to both targets by `common/arch/aarch64/ipc_platform.c`; hosted
tests link a condition-variable event implementation under `common/arch/hosted`.

Queue ABI scalars use the semantic types declared in `queue_types.h`: endpoint
and connection IDs, participant IDs, epochs, sequences, slot indices, and state
flags. Queue mapping locations use `Address`. Typed kernel queue operations retain
those result types until the scheduler converts them to the syscall result word.

Sources also include `kernel/ipc/queues.c`, `user/lib/sys/queue.c`,
and `user/programs/queue_test/main.c`. Coverage lives in
`test/kernel/scheduler/test_queues.c` and
`test/user/programs/queue_test/test_queue_test.c`.

QEMU verification covers repeated/concurrent clients, real write-permission
faults on identity and peer-owned pages, and successful clients after fault
cleanup. Four-core QEMU also passed five server kill/restart cycles, each with
128 successful round trips before termination, without a kernel panic. This is
smoke coverage, not an exhaustive SMP race test. The earlier `User exception
without a current process` panic was not deterministically reproduced; the
unsafe cross-CPU teardown path identified during investigation has been removed
from signal delivery.

## Process termination and queue teardown

`kill` requests termination; it does not synchronously destroy the target.
`Scheduler__queue_signal` resolves the PID and queues the signal while holding
the scheduler lock, so lookup cannot race process removal. Pending signals use
atomic operations because the target CPU consumes them outside that lock.
The requesting CPU does not revoke mappings or change the target's run state.

Only the target's owning CPU delivers termination, either at a syscall boundary
or before a selected context is resumed. A timer interrupt also takes this path
when the target is the only runnable process, allowing a userspace `WFE` loop to
terminate without another syscall. Waiting, sleeping, and stopped targets with
pending termination are eligible for selection. Their deferred continuations
are not executed: signal delivery precedes deferred syscall finalisation.
Pending termination overrides the legacy active-IRQ timer-preemption exemption.
No scheduler mutex is held across cleanup or the subsequent context switch.

This matters because a target marked interrupted can still be referenced by its
CPU's in-progress context switch. Destroying it from the signalling CPU based
on that state alone is not safe. Selection for cleanup on the owning CPU keeps
those references alive until that CPU has stopped using them.

Owner-CPU teardown removes listeners, marks protected connections closed and
unmaps the exiting endpoint. A surviving endpoint retains its queue pages until
it closes its handle, so it can observe the closed flag without accessing freed
storage. Only then can the backing be freed or its slot reused. These are the
protected transport queue rules; warden-managed channel buffers have their own
lifecycle described in [channels and event queues](channels-and-event-queues.md).

Scheduler tests cover queued remote requests, timer-only delivery, blocked and
sleeping targets, a signal arriving during context selection, pending IRQ
handling, and skipped deferred continuations. The atomic-message-queue regression
checks that a signal request alone leaves the connection mapped and open, then
owner-CPU teardown closes it and the survivor's close releases the backing.

## Why protect the transport

A process that can write a shared service queue can forge requests, overwrite
other clients' messages, change indices, or reserve a slot and never publish it.
Protecting the directory alone does not protect the queues it points to.

Signed requests or message authentication codes could authenticate message
contents if keys, endpoint binding, and replay prevention are sound. Encryption
alone does not establish authenticity; confidentiality would require an
authenticated encryption scheme. Neither approach prevents a process with
write access from corrupting ring metadata or destroying legitimate messages.
Cryptography therefore does not fix the availability problem of a globally
writable queue. Do not add a bespoke cryptographic protocol as a substitute for
memory isolation.

## Registry and connection establishment

The read-only kernel-maintained registry should advertise services that accept
connections, rather than publish a globally writable request queue. Entries
identify a registered endpoint and its incarnation, so a lookup cannot silently
connect to a different process after a service restart or PID reuse.

The target connection flow is:

1. A service registers an accepting endpoint with the kernel and configures
   bounded connection admission. This does not grant every caller application
   privileges such as permission to subscribe to IRQs.
2. A client discovers the endpoint in the registry and requests a connection
   through a kernel operation. The kernel derives the caller identity from the
   calling process, not from a PID supplied in a request message.
3. The kernel validates the endpoint incarnation and admission limits, creates
   or authorizes the queue memory, and maps it only into the authorized
   participants. Clients never gain access to other clients' queues.
4. The kernel publishes a connection descriptor with authenticated peer
   identity in a protected server-side arrival structure. Clients cannot write
   these descriptors or forge arrivals. The kernel-owned descriptor region is
   read-only to the server; server consumption state lives separately.
5. The server imports the connection into its private collection of client
   queues and accepts or rejects it. Rejection or failure during setup releases
   partial mappings and reservations; connection attempts must not grow memory
   without bound.
6. Channel internals exchange control messages through their mapped queues with
   userspace atomics and event notification. Service protocols exchange requests
   and replies through `Channel`, not through this transport API. Reusing a
   channel does not require a syscall for each message.

The server-side collection is where all client queues land logically; it is
not another shared ring into which all clients can write. A client may corrupt
its own connection, but must not corrupt the server's collection or a different
connection. Separate producer- and consumer-owned state onto different pages
where asymmetric write permissions are needed. Validate queue indices, lengths,
and offsets even for authenticated peers, and copy requests into private memory
before interpreting fields that a peer could change concurrently.

## Events and waiting

Kernel IRQ, process-exit, and connection-arrival records must have protected
provenance. Their producer state and contents must not be writable by clients;
a kernel-looking message in a client queue is still an untrusted client message.

Normal clients dispose of buffers and other resources through the operations
defined by their service protocol. A generic protocol close is not required.
They separately close the channel session when it is no longer needed.
Protocol resource release does not require session closure; session closure
must also clean up resources omitted by the client. A kernel-originated
process-exit queue additionally informs the channel manager of every normal or
abnormal process termination, including exits that omit resource cleanup.
The manager reconciles remaining PID-owned resources and notifies affected
services through channels. Queue saturation must not silently discard exits;
pending records must remain recoverable. Close and exit processing must be
idempotent. This required delivery is not yet provided by the current warden's
transport-closure scan.

Logical channels identify clients by PID and services by `BString` service
identifier. Transport peer PIDs identify current mappings; they do not replace
the stable service identity needed for future resume and remapping.

This revises the earlier idea of mixing all kernel events and client requests
in one universally writable ring. The service can still have one logical event
loop over its protected kernel-event stream and its private collection of
client connections.

The wakeup design must cover all these sources without missed notifications.
An aggregate wake word, if used, is only a hint to inspect authoritative queues,
never proof of sender identity or event type. It must not reintroduce shared
write access to queue metadata. The exact aggregation mechanism remains open;
per-connection epochs and fibre watches are inputs to that design, not a claim
that the current scheduler already supports this arrangement.

## Ownership and authorization

Kernel mediation is for establishing identity, access rights, and mappings,
not for copying or routing every message. `hundekanal-warter` remains responsible
for higher-level channel lifecycle, reuse policy, and close housekeeping. Its
own control connection must use the protected establishment path too; securing
only payload queues would leave channel allocation requests spoofable.

The split between kernel-created bootstrap queues and warden-managed channels
must be made explicit in the implementation. Connection establishment must not
depend on first sending an unauthenticated request through the old global queue.
Any manager access to connection memory must be explicit, not a mapping granted
to every service. Existing globally writable channel descriptors must also be
replaced or treated as untrusted rather than used to authorize mappings.

Transport identity does not imply application authorization. IRQ registration
still includes an auth token that `hund-unterbrict` validates with the kernel
and caches, binding the result to the authenticated connection and process
incarnation. Tokens must not be exposed through the old global queue.

On exit or disconnect, revoke mappings before recycling backing pages for new
participants. Keep pending exit/arrival records recoverable under backpressure,
and use generations to reject stale descriptors and acknowledgements. Resource
limits and fair servicing are still required: isolated queues prevent one
client from overwriting another's messages, not all forms of denial of service.

## Verification and implementation order

Implement protected establishment and event provenance before claiming token
security or building privileged IRQ registration on top of this transport.
Test that an unrelated process cannot read or write queue pages, forge arrival
or kernel-event records, redirect a reply, or reuse a stale endpoint identity.
Also test a client corrupting its own queue while other clients continue, server
exit during connection setup, bounded admission, rollback, queue-full recovery,
and repeated message exchange without per-message syscalls.
