# Channels and event queues

This describes the warden-managed echo channel implementation and its limits.
Read [atomic message queues](atomic-message-queues.md) for the underlying
kernel-mediated transport and [interrupt service](interrupt-service.md) for the
planned IRQ manager. Older kernel IO, IRQ and globally writable service-queue
paths remain available to unmigrated callers.

## Responsibilities

The kernel maintains the read-only service directory, authenticates connection
participants, maps queue and payload memory, and detaches processes on exit.
It does not interpret channel open, submit, or completion messages.

`hundekanal-warter` owns channel lifecycle in userspace. Its dispatch fibre scans
protected client connections and the corresponding connections to target
servers. Ownership, mapping addresses and protocol phases live in the warden's
private `ChannelExchanges` collection. Globally writable control slots are no
longer used to reserve or authorize these channels.

The echo service advertises a protected listening endpoint and accepts channel
dispatch connections only from the warden identified in the kernel directory.
This is transport binding, not the token-based `hund` authorization proposed
for IRQ registration.

## Discovery and setup

Both services call `service_become_service`, `atomic_message_queue_listen(name)` and
`service_publish_ready`. Directory entries advertise endpoint incarnation
handles, not globally writable queue addresses. Clients look up those handles
in shared memory without calling a readiness syscall.

The current demo uses one exchange per pair of control connections:

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
   the token and length. This is its final payload access.
7. The warden validates the completion and forwards it to the client. The client
   copies/prints the reply and closes its control connection as its final
   acknowledgement. The warden releases the payload once neither peer uses it.

Setup, acceptance, allocation and teardown require syscalls. Queue publication,
consumption and payload exchange do not require per-message send/wait syscalls.
The demo does not yet keep control connections or payload pages pooled across
separate invocations.

## Exchange states

The lifecycle is implemented by constant state objects. Each state owns an action
function that returns `Result(ConstChannelExchangeStatePtr)`. Returning the same
state means that the action is blocked; returning a different state records
progress. An error moves the exchange to server cleanup and queues a failure reply
while the client remains connected.

| State | Action | Next state |
| --- | --- | --- |
| `CHANNEL_EXCHANGE_WAIT_OPEN` | Receive and validate `OPEN` | `CHANNEL_EXCHANGE_CONNECTING` |
| `CHANNEL_EXCHANGE_CONNECTING` | Await acceptance and allocate the payload | `CHANNEL_EXCHANGE_DELIVERING_OPEN` |
| `CHANNEL_EXCHANGE_DELIVERING_OPEN` | Deliver the open response | `CHANNEL_EXCHANGE_OPEN` |
| `CHANNEL_EXCHANGE_OPEN` | Receive and validate `SUBMIT` | `CHANNEL_EXCHANGE_SUBMITTING` |
| `CHANNEL_EXCHANGE_SUBMITTING` | Retry until the server queue accepts the request | `CHANNEL_EXCHANGE_WORKING` |
| `CHANNEL_EXCHANGE_WORKING` | Validate the server reply | `CHANNEL_EXCHANGE_CLOSING_SERVER` |
| `CHANNEL_EXCHANGE_WORKING_CLIENT_CLOSED` | Wait until the server stops using the payload | `CHANNEL_EXCHANGE_CLOSING_SERVER` |
| `CHANNEL_EXCHANGE_CLOSING_SERVER` | Close and discard the server transport | Reply delivery or payload release |
| `CHANNEL_EXCHANGE_DELIVERING_REPLY` | Deliver the final response | `CHANNEL_EXCHANGE_WAIT_CLIENT_CLOSE` |
| `CHANNEL_EXCHANGE_WAIT_CLIENT_CLOSE` | Wait for and acknowledge client closure | `CHANNEL_EXCHANGE_RELEASING` |
| `CHANNEL_EXCHANGE_RELEASING` | Retry payload release | `CHANNEL_EXCHANGE_COMPLETE` |
| `CHANNEL_EXCHANGE_COMPLETE` | Mark the private record removable | Removal |

Each scan invokes one state action per exchange. A successful submission also
checks for an already queued completion in that action. Pending acceptance, full
outgoing queues, and empty incoming queues return the current state for retry.
Invalid client requests close the client. A disconnected client transitions from
`CHANNEL_EXCHANGE_WORKING` to `CHANNEL_EXCHANGE_WORKING_CLIENT_CLOSED`, retaining
the payload until the server completes or disconnects.

## Address layout and permissions

| Region | Address | Userspace access |
| --- | --- | --- |
| Service directory | `0x70000000` | Read-only |
| Legacy service queues | `0x70001000` | Legacy read/write mappings, unused by this path |
| Legacy control workspace | `0x70011000` | Still reserved/mapped, unused by this path |
| Managed payload pages | `0x71000000` onward | Read/write for the owner and two peers only |
| Protected arrival pages | `0x72000000` onward | Read-only, listener only |
| Protected connections | `0x72100000` onward | Participant-only, asymmetric permissions |

Each control connection has a kernel-written identity page read-only to both
peers, a client-written message page, and a server-written message page. Each
peer can read but cannot write the other direction. Unrelated processes receive
none of the connection or payload mappings. All mappings are non-executable.

Payload pages contain two fixed byte buffers. Length, ownership, status and
addresses are not read from writable payload metadata. Message fields are copied
into private memory before validation. A peer can corrupt its own payload, but
cannot use its length or address fields to redirect another exchange.

The kernel bounds the system to 16 protected connections and four outgoing
connections per process. Because each active exchange needs two connections,
the warden can dispatch at most four concurrent exchanges in this implementation;
other queue users also consume the system-wide limit. Admission failures are
returned to the requesting client without replacing existing exchanges.

## Closure and failure

Protected connection closure is authoritative kernel state, not a client-sent
process-exit message. The warden no longer subscribes to the legacy exit ring:
disconnect and process death both invalidate that endpoint's control connection.
A kernel-looking message kind received from a client has no special authority.

Client closure before dispatch cancels the pending exchange. After dispatch,
client closure does not release a payload while the server may still use it:
the warden waits for server completion or disconnection. Server disconnection
causes a failure reply to the client, whose close permits payload release.
Protocol violations abort the offending exchange, not unrelated clients.

`atomic_message_queue_close` commits the caller's detach: its queue mappings must not be used
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

- Protected general kernel-event delivery and reliable overflow recovery are
  still needed for the interrupt service. This migration uses connection closure,
  not a substitute process-exit event stream.
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

- `common/services/atomic_message_queue.h`: protected transport ABI and atomics.
- `kernel/service/atomic_message_queue.c`: connection mappings, detach and quarantine.
- `kernel/scheduler/scheduler.c`: managed payload ownership and exit cleanup.
- `user/lib/channel/exchange.h`: payload and protocol definitions.
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
