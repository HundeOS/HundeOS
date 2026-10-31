# Channels and event queues

`Channel` is the application-facing bidirectional IPC abstraction. `Queue` is
its protected shared-memory transport. `hundekanal-warter` authorizes and
establishes a Channel, but it is never an intermediary for requests or replies.

This separation is a required architectural invariant:

```text
setup only
client -------- OPEN(target) --------> hundekanal-warter
client <------- direct connection ---- hundekanal-warter

steady state
client ===== request / reply ===== service
              direct Queue

hundekanal-warter is not on this path
```

Putting the warden in the steady-state path adds two process hops to every
request, makes unrelated channels contend on one service, and turns warden
scheduling latency into application I/O latency. Implementations must not relay,
copy, acknowledge, or otherwise observe ordinary Channel traffic through the
warden.

Read [queues](queues.md) for transport mappings and
[dependency scheduling](dependency-scheduling.md) for request tickets. Bulk data
uses [shared buffers](../userspace/shared-buffers.md); the inline Channel area is for bounded
protocol messages, descriptors, commands, and completions.

## IPC types

- `Channel` is a reusable client-to-service session with one outstanding request.
- `Queue` is the internal participant-only transport beneath a Channel. Service
  protocols must not expose Queue handles or invent another lifecycle around it.
- `KernelNotificationQueue` is a bounded one-way kernel-to-service mechanism for
  kernel-originated events. It is not a general Channel replacement.
- Service protocols define their own resource handles and close operations. A
  file close or buffer release does not close the Channel session.

One Channel may carry operations for many protocol resources. For example, a
process can open several file handles through one I/O Channel. Those handles are
owned and validated by the service; they are not separate Channels.

## Direct transport

A direct Channel connection contains three protected pages:

| Page | Client mapping | Server mapping | Purpose |
| --- | --- | --- | --- |
| Identity | Read-only | Read-only | Connection, endpoint, authenticated participants, origin, lifecycle, Channel authorization |
| Client-owned | Read/write | Read-only | Client messages, request bytes, and consumption of server messages |
| Server-owned | Read-only | Read/write | Server messages, reply bytes, and consumption of client messages |

Each endpoint is the sole writer of its outgoing page. The peer can read it but
cannot forge its cursors, publications, or data. The bounded inline data region
is part of each directional Queue page, so request and reply bytes do not require
a warden-owned payload page.

The sender writes bytes, publishes a control message, and rings the peer's
kernel-authenticated doorbell. The receiver reads the control message and bytes
from the opposite page. Replies use the same mechanism in the other direction.
There is no Channel-level acknowledgement message after a reply; Queue cursors
already provide transport consumption state.

`Channel__try_request` returns `CHANNEL_PENDING` when it has not published a
request. `Channel__try_poll_reply` returns an empty Optional until a reply is
available. If the supplied reply buffer is too small, the Channel retains the
consumed reply metadata so the caller can retry without repeating the request.

## Establishment

Services publish protected Queue endpoints in the kernel service directory.
Opening a userspace Channel proceeds as follows:

1. The client creates a temporary protected Queue to `hundekanal-warter`.
2. The warden accepts it and receives `OPEN(target endpoint)`. The connection,
   not a message field, authenticates the client PID.
3. The warden asks the kernel to delegate a connection from that authenticated
   client to the target endpoint.
4. The kernel derives the client from the accepted control connection, creates
   and maps a direct client-to-service Queue, and marks its read-only identity as
   authorized for Channel use.
5. The warden returns the direct connection handle. The client attaches to it and
   closes the temporary control Queue.
6. The service accepts only connections whose kernel-owned identity carries the
   Channel authorization mark. An ordinary process cannot bypass the warden by
   opening a raw Queue to the service endpoint.

The warden's complete state machine is deliberately small:

```text
WAIT_OPEN -> DELIVERING_OPEN -> WAIT_CLIENT_CLOSE -> COMPLETE
```

It records no steady-state session, target Queue, payload mapping, request,
reply, or acknowledgement. After the temporary control Queue closes, no warden
state remains for that Channel.

Kernel-originated Channels connect directly to the target service without a
warden round trip. Their transport participant is `QUEUE_KERNEL_PID`; the
read-only identity separately records the originating process. Root request
tickets use that real origin, allowing dependency scheduling to lead back to
the blocked process while preserving the reserved kernel transport identity.

## Identity and authorization

The kernel owns all authoritative connection identity:

- client PID and server PID come from live `Process` objects;
- endpoint and connection generations reject stale handles;
- the originating PID is protected connection metadata;
- the Channel authorization mark is kernel-written;
- mappings are limited to the two participants;
- a process exit cannot be disguised as a protocol message.

Services must use `ChannelServer` metadata for client identity and overwrite or
ignore identity claims carried in application payloads. A request ticket is
dependency metadata, not permission to impersonate another transport peer.

Only a process with `PROCESS_CAP_MANAGE_SHARED_PAGES` may request delegation.
The kernel verifies that the manager is the server of the accepted temporary
control connection before deriving its client. Knowledge of another connection
handle is insufficient.

## Request tickets and scheduling

Every request carries a connection handle, generation, origin PID, and optional
parent connection/generation. A direct request publication identifies the
caller and responder without a relay edge. A validated reply prioritizes the
caller and then the callers of live ancestor requests.

Tickets are scheduler hints and ancestry evidence. They do not authorize data
access, and missing a transient publication cannot lose the real Queue wakeup.
See [dependency scheduling](dependency-scheduling.md) for validation and policy.

## Closure and failure

Either endpoint closes its direct Queue through the kernel. Closure marks the
protected identity, wakes the survivor, revokes the departing endpoint's
mappings, and retains backing until the survivor detaches. Process exit follows
the same kernel-owned connection cleanup path.

The survivor's peer-doorbell alias also remains mapped until that endpoint
detaches: a message operation may already have passed its open-state check when
the other side closes. Kernel-private references retain doorbell backing beyond
process exit or image replacement, including kernel-created connections. See
[queue mapping lifetime](queues.md#initial-queue-api) for the backing rules.

There is no warden cleanup protocol for an established Channel. Duplicating
kernel lifecycle state in the warden would introduce races between observations
and authoritative close or exit state. Services remain responsible for releasing
their own protocol resources when a Channel closes or a client exits.

Queue close and process-exit cleanup must be idempotent. Backing allocations are
not recycled while either surviving participant can still access them. Failed
backing frees remain quarantined for retry.

## Shared buffers

The inline directional data area is bounded and is not the bulk-transfer design.
Large or frequent bytes move through separately shared directional buffers
managed by `hunde-puffer`. Channel messages carry buffer descriptors, offsets,
lengths, commands, and completions. Shared buffers must not contain a second
request/reply ring or independent connection protocol.

## Waiting

Queue publication invokes the authenticated data-available operation after the
shared cursor is visible. The kernel marks a parked receiver runnable and
requests a reschedule when required. On a single CPU, publication may yield at
the next scheduling decision so the woken peer can run; it does not require an
IPI to the current CPU.

User wait snapshots are hints used to describe what the process chose to wait
for. Authoritative Queue identity, cursors, close state, and doorbell generations
remain kernel-visible and determine whether work is available.

The Rust I/O host's scoped native wait adapter makes C Channel waits suspend
the calling fibre on its one shared scheduler. The outer loop retains the
generation-checked kernel wait after runnable work drains. A Channel still
allows only one outstanding request: shared cached clients must serialize the
whole connection/transaction, not just publication. Cooperative ownership gates
wake parked contenders on release, including error cleanup. This changes wait
policy, not transport authentication, mappings, or the direct request path.

## Source and tests

- `common/ipc/channel.c`: transport-independent Channel request/reply state.
- `common/ipc/queue.c`: directional Queue data and control publication.
- `kernel/ipc/queues.c`: mappings, identity, delegation, close, and exit cleanup.
- `kernel/ipc/kernel_channel.c`: direct kernel-to-service Channels.
- `user/lib/channel/channel.c`: userspace establishment and waiting.
- `user/lib/channel/server.c`: authorized direct service acceptance.
- `user/services/hundekanal-warter/lifecycle.c`: temporary open/delegation state.
- `kernel/scheduler/request_graph.c`: direct request dependency discovery.

Coverage includes authorization, authenticated client delegation, direct binary
request/reply, raw Queue rejection, kernel Channels, closure, stale handles,
request ancestry, and warden removal from the steady-state path.
