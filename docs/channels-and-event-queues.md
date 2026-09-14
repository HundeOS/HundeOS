# Channels and event queues

The shared Channel payload and nonblocking client request/reply core live in
`common/ipc/channel*`. Userspace connection setup and waiting remain in
`user/lib/channel`. `Channel__try_request` returns `CHANNEL_PENDING` when
the request cannot yet be queued; it has not started a request in that case.
`Channel__try_poll_reply` returns an empty Optional until both the reply and
its acknowledgement complete. It retains a consumed reply under acknowledgement
backpressure, so retrying does not consume another reply or repeat the request.
Callers must retain valid queue and payload mappings throughout each call.
This shared core does not yet provide kernel endpoint establishment.

This describes the generic IPC primitives and the warden-managed channel
implementation. Read [queues](queues.md) for the underlying
kernel-mediated transport and [interrupt service](interrupt-service.md) for the
planned IRQ manager. Older kernel IO and IRQ paths remain available to
unmigrated callers; the globally writable service queue has been retired.

The [userspace I/O system plan](io-system.md) defines `hund-io-system` as the
common application-facing service for files, sockets, networking, and streams.
Its protocols and backend connections use the same channels described here.

## IPC types

- `Channel` is a bidirectional process-to-process IPC session.
  Services and clients exchange protocol requests, replies, notifications, and
  resource descriptors through a channel. Its payload and lifecycle are managed
  by `hundekanal-warter`.
- `Queue` is an internal transport used to implement channels. Service and
  driver protocols must not expose queues or build a second connection,
  request/reply, signalling, or lifecycle abstraction from them. It was
  previously named `AtomicMessageQueue`; atomic cursors are an implementation
  detail rather than an application-facing IPC model.
- `KernelNotificationQueue` is a bounded, one-way kernel-to-service delivery
  mechanism. It is the exception for kernel-originated notifications and is not
  a bidirectional process-to-process IPC alternative.
- `BlockProtocol` and `InterruptProtocol` define service-specific messages and
  resource descriptors carried by channels. They do not create or own a
  distinct transport or kind of channel.

Service protocols are layered on the channel session. Each defines its own
operations, resources, and resource-release semantics. There is no mandatory
generic protocol-level open or close operation. For example, a block protocol
could close a block context, while a filesystem protocol could close individual
file handles or release a mount. These operations travel over the channel and
can release their resources while the channel session remains open.

### One session, multiple protocol resources

Processes share one channel session for their communication. In the client/service
model, the logical session is identified by client PID and service identifier;
opening another protocol resource reuses that session. A channel is not allocated
per file, buffer, operation, or protocol state.

The protocol may maintain multiple independent resource states simultaneously
within that session. For example, a program opens several files through its one
channel to the filesystem service. The service returns a distinct file handle
for each open, and subsequent reads, writes, and closes identify the appropriate
handle in messages over the same channel. Closing one file leaves the other
files and the channel session open.

Resource handles are scoped and validated against their owning session. Protocol
state belongs to the service; the channel manager tracks the session and shared
resource attachments without interpreting file-handle operations. Session close
or client exit requires the service to clean up all remaining resource states
associated with that session.

Multiple live resources do not require multiple in-flight messages. The current
channel implementation permits one outstanding request per session; any future
support for concurrent requests must correlate replies within that same channel.

`ChannelSignal` is the low-level epoch and `SEV`/`WFE` wait primitive used by
channel internals and shared-buffer access. It carries no protocol messages and
owns no connection lifecycle, so services must not use it to construct another
IPC mechanism.

## Shared buffers

A channel payload can contain a descriptor for separately shared memory when
the data is too large or too frequent to copy through the channel payload. The
channel remains the control plane: it establishes the operation, communicates
the buffer identity, offset, size, access mode, and lifetime, and returns
completion or failure. The shared region carries only the bulk bytes described
by that exchange.

Block devices are the primary case. Clients may supply buffers for the service
to read from or write into; services may also provide shared buffers. Buffer
descriptors and their access/lifetime agreement are exchanged through the
channel in either case. Block requests and responses also travel through that channel. A
shared buffer must not contain a parallel request ring, response ring,
connection state, or independent transport close protocol.

Clients must explicitly dispose of their buffers when finished. Disposal must
coordinate with the service through the protocol so that outstanding accesses
finish before mappings or backing are released. A block-context close is one
possible protocol operation letting the service dispose of associated resources
or mark buffers recyclable once
they are no longer in use. Recyclable storage must not remain accessible through
stale client descriptors or mappings.

## Responsibilities

The kernel maintains the read-only service directory, authenticates connection
participants, maps queue and payload memory, and detaches processes on exit.
It does not interpret channel open, submit, or completion messages.

`hundekanal-warter` owns channel lifecycle in userspace. Its dispatch fibre scans
protected client connections and the corresponding connections to target
servers. Ownership, mapping addresses and protocol phases live in the warden's
private `ChannelSessions` collection. Globally writable control slots are no
longer used to reserve or authorize these channels.

The echo service advertises a protected listening endpoint and accepts dispatch
connections only from the warden identified in the kernel directory.
The interrupt manager uses the same server-side channel binding. It authorizes
the kernel-derived client PID forwarded by the authenticated warden rather than
trusting identity fields in the client payload.

## Discovery and setup

Both services call `service_become_service`, `Queue__listen(name)` and
`service_publish_ready`. Directory entries advertise endpoint incarnation
handles, not globally writable queue addresses. Clients look up those handles
in shared memory without calling a readiness syscall.

Each `Channel` uses one pair of control connections:

1. The client connects to the warden's advertised endpoint. The kernel creates
   the isolated queue pages and the warden accepts the connection.
2. The client sends `OPEN`, identifying the target endpoint handle. Its claimed
   sender PID is ignored: the protected connection supplies the actual PID.
3. The warden connects to that target. After acceptance, it obtains the server
   PID from the protected identity and allocates a payload page mapped into
   itself, that server, and the client.
4. The warden sends `OPEN` success containing the payload address and the
   client's connection handle as the exchange token.
5. The client writes request bytes and sends `SUBMIT` with the length and token.
   The warden validates phase, token and bounds, then forwards `SERVE` using its
   privately recorded payload address. A client cannot substitute an address.
6. The server copies the request into the reply area and sends `REPLIED` with
   the token and length.
7. The warden validates the completion and forwards it to the client. The client
   copies the reply and sends `ACKNOWLEDGE`; the session returns to `OPEN` for
   another request.
8. `Channel__close` closes the client control connection. The warden then closes
   the server connection and releases the payload once neither peer uses it.

Setup, acceptance, allocation and teardown require syscalls. Queue publication,
consumption and payload exchange do not require per-message send/wait syscalls.
Connections and payload pages persist across submissions to the same `Channel`.
Clients may use blocking `Channel__submit`, or pair `Channel__request` with
`Channel__poll_reply` when an event loop or fibre must continue handling other
work. Services use `ChannelServer` to accept warden connections, read typed
payload requests, and publish replies. Service protocols do not read or write
the underlying queue messages directly.

## Exchange states

The lifecycle is implemented by constant state objects. Each state owns an action
function that returns `Result(ConstChannelSessionStatePtr)`. Returning the same
state means that the action is blocked; returning a different state records
progress. An error moves the exchange to server cleanup and queues a failure reply
while the client remains connected.

| State | Action | Next state |
| --- | --- | --- |
| `CHANNEL_SESSION_WAIT_OPEN` | Receive and validate `OPEN` | `CHANNEL_SESSION_CONNECTING` |
| `CHANNEL_SESSION_CONNECTING` | Await acceptance and allocate the payload | `CHANNEL_SESSION_DELIVERING_OPEN` |
| `CHANNEL_SESSION_DELIVERING_OPEN` | Deliver the open response | `CHANNEL_SESSION_OPEN` |
| `CHANNEL_SESSION_OPEN` | Receive and validate `SUBMIT` | `CHANNEL_SESSION_SUBMITTING` |
| `CHANNEL_SESSION_SUBMITTING` | Retry until the server queue accepts the request | `CHANNEL_SESSION_WORKING` |
| `CHANNEL_SESSION_WORKING` | Validate the server reply | `CHANNEL_SESSION_CLOSING_SERVER` |
| `CHANNEL_SESSION_WORKING_CLIENT_CLOSED` | Wait until the server stops using the payload | `CHANNEL_SESSION_CLOSING_SERVER` |
| `CHANNEL_SESSION_CANCELLING` | Notify the server of client departure, retrying under backpressure | `CHANNEL_SESSION_WORKING_CLIENT_CLOSED` |
| `CHANNEL_SESSION_CLOSING_SERVER` | Close and discard the server transport | Reply delivery or payload release |
| `CHANNEL_SESSION_DELIVERING_REPLY` | Deliver the response | `CHANNEL_SESSION_WAIT_CLIENT_ACKNOWLEDGEMENT` |
| `CHANNEL_SESSION_WAIT_CLIENT_ACKNOWLEDGEMENT` | Validate `ACKNOWLEDGE` | `CHANNEL_SESSION_OPEN` |
| `CHANNEL_SESSION_WAIT_CLIENT_CLOSE` | Wait for and acknowledge client closure | `CHANNEL_SESSION_RELEASING` |
| `CHANNEL_SESSION_RELEASING` | Retry payload release | `CHANNEL_SESSION_COMPLETE` |
| `CHANNEL_SESSION_COMPLETE` | Mark the private record removable | Removal |

Each scan invokes one state action per exchange. A successful submission also
checks for an already queued completion in that action. Pending acceptance, full
outgoing queues, and empty incoming queues return the current state for retry.
Invalid client requests close the client. A disconnected client transitions from
`CHANNEL_SESSION_WORKING` through `CHANNEL_SESSION_CANCELLING` to
`CHANNEL_SESSION_WORKING_CLIENT_CLOSED`. Cancellation carries the session token;
the payload stays mapped until the server completes or disconnects. A server
with deferred operations polls its `ChannelServer` while working to observe
cancellation, stop using the payload, and close its connection. This allows
blocked I/O operations to be cleaned up when the client departs.

## Address layout and permissions

| Region | Address | Userspace access |
| --- | --- | --- |
| Service directory | `0x70000000` (size rounded to pages) | Read-only |
| Device registry | `0x70800000` (size rounded to pages) | Read-only |
| Process registry | `0x78000000` (size rounded to pages) | Read-only |
| Managed payload pages | `0x71000000` onward | Read/write for the owner and two peers only |
| Protected arrival pages | `0x74000000` to `0x74c00000` (exclusive) | Read-only, listener only |
| Protected connections | `0x72400000` to `0x73000000` (exclusive) | Participant-only, asymmetric permissions |

The service directory stores service names as `BString` values. Its mapping size
is derived from the directory structure and rounded to whole pages. The directory
and listener table each accommodate 1024 services; this is separate from the
1024 system-wide queue connections and 32 messages per queue direction.

Each control connection has a kernel-written identity page read-only to both
peers, a client-written message page, and a server-written message page. Each
peer can read but cannot write the other direction. Unrelated processes receive
none of the connection or payload mappings. All mappings are non-executable.

Payload pages contain two fixed byte buffers. Length, ownership, status and
addresses are not read from writable payload metadata. Message fields are copied
into private memory before validation. A peer can corrupt its own payload, but
cannot use its length or address fields to redirect another exchange.

The kernel bounds the system to 1024 protected connections and four outgoing
connections per ordinary process. Services with shared-page management capability
may broker outgoing connections up to the global limit. Each channel needs two
transport connections; other queue users also consume the system-wide limit. Admission failures are
returned to the requesting client without replacing existing exchanges.

## Closure and failure

### Required lifecycle contract

A well-behaved client explicitly disposes of buffers and other protocol resources
using the operations defined by that protocol, and closes the channel session
when it is no longer needed. Resource release makes buffers eligible for disposal
or recycling after outstanding accesses finish. The session can remain open
after individual protocol resources are released. Session close ends the channel
itself and triggers cleanup of any protocol resources the client left behind.
Neither layer waits for process exit to perform normal cleanup.
Clients are nevertheless assumed to be unreliable: they may crash, be killed,
fault, or exit normally without closing channels or releasing buffers.

The kernel must report every process exit to the channel manager through a
protected `KernelNotificationQueue`, regardless of how the process terminated.
Exit records must remain recoverable under queue saturation. Buffer disposal,
protocol resource release, session close, and kernel-reported exit must converge on
idempotent resource cleanup, so a later close or exit cannot double-free resources
already released at another layer. The manager checks all
resources still associated with the exiting process, including partially opened
channels and incomplete buffer releases.

The manager informs affected services through channels, allowing each service
to schedule cleanup of its client resources. Client death removes the client's
mapping but must not free backing still used by the server or an in-flight
device operation. Cleanup must finish or cancel those operations before releasing
the remaining attachments. A dead or unresponsive server cannot be relied on to
acknowledge cleanup; device access must be quiesced before such backing is reused.
Process-exit delivery catches missed cleanup on exit; detecting abandoned
resources held by a still-live process requires a separate policy.

### Identity and future resume

The client side is identified by its kernel-authenticated PID. The service side
is identified by a stable `BString` service identifier; the serving PID is its
current process attachment. PID reuse must not associate old resources or delayed
exit records with a new client.

On a future service resume, the channel manager will bind the authenticated
replacement process to that service identifier and ask the kernel to remap its
surviving channels and shared buffers. Service death must therefore be distinct
from client close. Recovery of these logical resources does not imply replay of
in-flight operations: their result may be indeterminate. Resume and remapping
are deferred implementation work.

### Current implementation

Protected connection closure is authoritative kernel state. The current warden
uses it to detect endpoint departure and no longer subscribes to the legacy exit
ring. This is an implementation gap relative to the required process-exit queue
and resource reconciliation above. A kernel-looking message kind received from
a client has no special authority.

Client closure before dispatch cancels the pending exchange. After dispatch,
client closure does not release a payload while the server may still use it:
the warden waits for server completion or disconnection. Server disconnection
causes a failure reply to the client, whose close permits payload release.
Protocol violations abort the offending exchange, not unrelated clients.

`QueueConnection__close` commits the caller's detach: its queue mappings must not be used
again. The surviving endpoint retains its queue mappings until its own close.
If final backing reclamation fails after detachment, the kernel quarantines the
allocation for later retry and still reports the detach as successful.

The warden retains a private record if `mem_release_shared_page` fails, retrying
on later housekeeping passes. It never recycles that record while it still owns
the payload. Only private control records are reused; normal payload completion
currently frees memory instead of pooling it.

Full reply queues retain their pending message for retry. A scan performs bounded
work per connection, so one slow client cannot block progress on another queue.
Connection generations and per-connection protocol state reject stale endpoint
handles, submissions and completions.

## Waiting

The warden uses the existing fibre scheduler. Its dispatch fibre yields after a
bounded scan; the outer loop waits with `WFE` when no progress was made, then
wakes the fibre to recheck all arrival, message, closure and retry state.
`SEVL` initializes the event latch once; transport publication and consumption
issue `SEV`. The echo server uses the same scan-and-event-wait pattern without
fibres. No shared aggregate wake word is used as an authority or queue cursor.

`WFE` is not a kernel scheduler park. Timer preemption still schedules ordinary
services at the same priority as normal processes. Pending termination can be
delivered on the owning CPU's timer/context-switch path without another syscall.

## Memory API and remaining work

`MEM_ALLOC_SHARED_PAGE(first_peer_pid, second_peer_pid)` requires a service with
`PROCESS_CAP_MANAGE_SHARED_PAGES`. It allocates and zeroes one page and maps it
at a common reserved virtual address for the owner and both live, distinct peers.
`MEM_RELEASE_SHARED_PAGE(address)` is owner-only and unmaps remaining participants,
completes TLB invalidation, then frees backing. These operations and exit
detachment serialize under the scheduler lock without switching context inside it.

Remaining limitations:

- Reliable kernel process-exit delivery and resource reconciliation are needed
  by the channel manager and interrupt service. Current connection closure
  detection does not fulfill that requirement.
- A live peer that never completes or disconnects can retain an exchange. There
  is no timeout or forced-revocation policy for such peers.
- If the warden dies, the current kernel memory-owner exit rule revokes all its
  payload mappings. A peer concurrently accessing a payload may fault. Recovery
  of in-flight channels across warden restart is not implemented; new connections
  use the replacement endpoint incarnation.
- Kernel page-table allocation failures are not fully exposed by `PageTable__map`.
  Failed backing frees are quarantined, reducing available capacity until retry
  succeeds. The underlying allocator failure observed during QEMU cleanup is
  not diagnosed by this transport change.
- Services with unrestricted physical MMIO access remain highly trusted. This
  transport does not enforce the future IRQ token authorization policy.

## Source and tests

### Block-device protocol

The target block protocol uses a `Channel` for connection lifecycle, block
requests, block responses, and shared-buffer descriptors. Sector bytes may move
through separately shared buffers, but a client learns the buffer mapping and
valid range from a channel message. Completion and buffer release are also
channel messages.

`BlockClient` now contains a real `Channel`. Opening it connects to the block
service's device-path identifier and obtains its sector count with a describe
request. Reads and writes reuse the session; transfers larger than 512 bytes
are split into sector requests. Sector bytes currently travel in the shared
channel payload itself. Separate advertised bulk buffers and their disposal
operations remain future work; this path creates no separate block mappings.

VirtIO and SDHCI serve these requests through `BlockServer` and `ChannelServer`.
The server authenticates the warden and checks the forwarded client's block IO
capability with the kernel once per session. It copies each request before
device access and retains completed replies under backpressure without repeating
the operation. SDHCI retains its existing read-only behavior.

`BlockClient__close` releases the channel session. FAT32 and EXT2 close it when
their service loop ends, including startup failure. Transport closure handles
departed clients; the reliable process-exit stream described above remains work
to implement. The old `service_block_connect` syscall and its remaining legacy
callers use `legacy_block_transport.h`; FAT32, EXT2, VirtIO, and SDHCI no longer
exchange block IO through those rings.

The `fat32fs` and `ext2fs` binaries use channel block sessions exclusively. The
legacy syscall-backed filesystem binaries and transport-specific filesystem
names have been retired. `virtio_blk_channelio` dispatches
service events and channel notifications through a `FiberScheduler`. Its worker
fibre yields while a VirtIO request waits for an IRQ, allowing the dispatcher to
continue collecting channel work. The worker serializes hardware requests
because the current VirtIO queue has one shared request header and status slot.
Configured FAT32 and EXT2 mounts use userspace filesystem and block drivers on
all targets, with block requests carried by channels. The kernel retains EXT2
temporarily under the internal
`bootstrap-ext2` type solely to mount `/boot/config` before userspace
services start; kernel FAT support has been removed.

All VirtIO disks use `virtio_blk_channelio`, including those backing the
bootstrap mounts. Hundeschule starts the embedded channel manager and interrupt
server before disk probing and waits for their readiness. Disk configuration
cannot select the legacy `virtio_blk` driver, which has been deleted. Its
hardware implementation now belongs to `virtio_blk_channelio/device.c`;
the active driver links it with the fibre runtime and interrupt-channel client.
The kernel's remaining bootstrap filesystem implementation is still required.

VirtIO block drivers claim their mapped device before enabling its queue. On
normal or abnormal process exit, the kernel resets that device before releasing
IO requests, channel mappings, or process memory. The architecture implementation
waits for reset completion before permitting DMA backing to be reclaimed. Claimed
MMIO cannot be explicitly unmapped until this reset has happened. This is a
trusted-driver cleanup contract, not an IOMMU isolation boundary.

Bootstrap filesystem mounts retain their block-device object. Driver exit marks
that object unavailable and removes it from discovery; reads through an existing
mount return an IO error. A replacement registration does not revive the old
mount's handle.

- `common/ipc/queue.h`: protected transport ABI and atomics.
- `kernel/ipc/queues.c`: connection mappings, detach and quarantine.
- `kernel/scheduler/scheduler.c`: managed payload ownership and exit cleanup.
- `user/lib/channel/protocol.h`: payload and protocol definitions.
- `user/services/hundekanal-warter/lifecycle.h`: private exchange state machine.
- `user/services/hundekanal-warter/main.c`: listening and fibre dispatch.
- `user/services/hundekanal-test-echo/main.c`: protected dispatch and echo replies.
- `user/programs/channel_echo_client/main.c`: end-to-end client.
- `test/user/services/channel_fixture.c`: integration fixture using real queue
  operations with stubbed kernel establishment/mapping calls.
- `test/user/services/hundekanal_warter` and `hundekanal_test_echo`: exchange,
  peer closure, bounds, impersonation attempts, backpressure and stale handles.

Boot the services through `hundeschule`, then run `channel_echo_client "hello"`.
The warden no longer prints unrelated processes' exit events.

Four-core QEMU verification completed ten sequential exchanges without cleanup
faults after the close-contract correction. Killing the echo service produced
a clean unavailable-service result. Using a temporary boot configuration with
`respawn`, new exchanges succeeded after separately restarting the echo service
and the idle warden. The repository's normal `once` configuration is unchanged.
An intermittent early-boot instruction-abort panic was also observed before the
services started; reset allowed the verification to proceed. This is not a
claim that startup or in-flight warden recovery has been fixed.
