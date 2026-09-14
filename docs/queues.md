# Queues

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
- `Queue__wait_readable/writable` recheck queue predicates around AArch64 `WFE`;
  producers and consumers issue `SEV`. These are process-level waits, not yet
  multi-connection fibre watches. A server must service its other connections
  rather than block on one client's queue. `queue_test` scans all connections
  after event wakes and bounds work per connection.
- `QueueConnection__close(connection)` is the calling endpoint's final acknowledgement:
  stop all local users of that handle before calling it. The kernel publishes
  closed state and removes only the caller's mappings. The surviving endpoint
  retains its mappings until it closes too. Once both are detached, backing is
  freed after unmapping/TLB invalidation. Process exit performs the same local
  detachment. The legacy process-exit event queue has been removed.
  A backing-free failure after detachment does not turn close into a failed
  detach: the kernel quarantines the backing for retry on a later connect and
  returns success. Callers must discard their handle after successful close.

The arrivals region starts at `0x74000000`, with three pages per listener slot
to hold 1024 connection IDs and the endpoint/epoch header. Allocation, mapping,
overlap checks, and teardown use the page-rounded `QUEUE_ARRIVALS_SIZE`.
Connections start at `0x72400000`, with three pages per connection slot; their
12 MiB address range ends at `0x73000000`. Neither region overlaps the kernel
notification queue at `0x72300000`.

| Page | Client | Server | Contents |
| --- | --- | --- | --- |
| Identity | Read-only | Read-only | Connection/endpoint handles, peer PIDs, accepted/closed state |
| Client-owned | Read/write | Read-only | Client messages and acknowledgement of server messages |
| Server-owned | Read-only | Read/write | Server messages and acknowledgement of client messages |

Unrelated processes receive none of these mappings. Slots cannot reuse backing
while a surviving endpoint retains its mappings. New handles use a monotonic
generation so stale accepts/closes cannot affect a replacement. Existing tracked
allocations overlapping the chosen virtual range cause setup to fail.

The bounds are 1024 listener slots, 1024 connections system-wide, and four active
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
tests link the no-op implementation under `common/arch/hosted`.

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
