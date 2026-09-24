# Shared Buffers: hunde-puffer

## Status

Implemented first version: `hunde-puffer` manages shared bulk-data buffers through
authorized kernel operations, including allocation, directional mappings,
generation-tagged leases, recycling, and hard freeing. Mama starts it immediately
after the Channel warden. File I/O, executable loading, and block-client transfers
use these buffers. Inline path/control fields are not bulk-transfer buffers.

The default is 2 MiB per direction, allocated lazily for I/O opens. Block describe
opens allocate both directions. There are 256 backing slots and a pool of at most
four detached allocations (8 MiB); failed frees remain retained for retry. These
are initial global limits, not per-client quotas.

Existing bidirectional Channels carry opens, buffer descriptors, transfer commands,
and completions. Bulk bytes move through separate buffers. Do not introduce
service-specific channel types or parallel request/reply rings in those buffers.

## Responsibilities and Identity

- `hunde-puffer` manages leases, authorized participants, common virtual
  addresses, the reusable pool, and housekeeping. Its management interface uses
  the existing Channel abstraction.
- The kernel owns backing allocations and enforces permissions, attachment
  lifetimes, revocation, and safe reclamation. It supplies authoritative process
  lease changes caused by process exits through a `KernelNotificationQueue` mapped
  only to the manager. The manager can consume this queue but cannot alter the
  kernel's authoritative attachment records.
- `hundekanal-warter` retains Channel establishment and session lifecycle.
  It coordinates closure with buffer cleanup, but does not become the bulk allocator.
- I/O and device services own protocol resources and in-flight work. They acquire
  buffers from `hunde-puffer`, advertise them to clients, and complete or cancel
  outstanding access before releasing leases.

Clients are identified by authenticated PID; services by `BString` service
identifier and current authenticated process attachment. Untrusted PID fields
and knowledge of an address must not grant access.

## Addresses, Sizes, and Permissions

A buffer has the same virtual address in every participating userspace process.
Unrelated processes receive no mapping. Reserve a common range without collisions
with heaps, stacks, registries, or Channel mappings. Mapping conflicts and partial
setup failures must roll back, not silently give one peer a different address.
Kernel clients resolve descriptors to validated backing; a userspace address is
never directly trusted as a kernel pointer.

The implementation reserves `0x80000000` through `0xa0000000` for 256 2 MiB
slots. ELF segments, stacks, heaps, dynamic/MMIO mappings, and shared registries
use disjoint ranges. New checked shared mappings roll back partial failures.
The manager notification page is at `0x7f000000`. A read/write pair consumes
4 MiB of backing, not 4 MiB per participant. Larger transfers use repeated
buffer-sized requests; variable-sized leases remain deferred.

Buffer capacity is distinct from valid data length and device sector size.
The size policy does not require physically contiguous memory or a particular
page-table block mapping. DMA constraints need explicit handling.

| Direction | Client mapping | Server mapping |
| --- | --- | --- |
| Read: server produces, client consumes | Read-only | Read/write |
| Write: client produces, server consumes | Read/write | Read-only |

All mappings are non-executable. Direction is relative to each protocol's
client/server roles. Mediators and downstream drivers receive only explicitly
authorized attachments and necessary permissions. Management authority need not
give every service, or even the manager, unrestricted data mappings.

## Protocol Semantics

The wire definitions are in `common/ipc/buffer.h`, `io_protocol.h`, and
`block_protocol.h`. Service names use length-delimited wire bytes and BString
operations, with a 255-byte management-protocol name limit.

1. The client sends a protocol open request on an established Channel. Opening
   a protocol resource is distinct from establishing the Channel session.
2. The service requests directional buffers from `hunde-puffer`, bound to the
   authenticated client, service, session, and protocol resource.
3. The manager asks the kernel to allocate or reuse backing and establish the
   required mappings. Open cannot succeed before mappings are ready.
4. The service returns the resource handle and descriptors on the Channel.
   Descriptors include lease identity/generation, common address, capacity, and
   direction. An address alone is not a capability.
5. A read identifies the resource/file offset, destination buffer range, and
   maximum length. The server fills the read buffer and replies with actual
   valid bytes, status, and operation identity. The client consumes that range
   before a subsequent request permits it to be overwritten: "read more".
6. A write identifies a filled write-buffer range and resource/file offset.
   The server consumes the range and replies with completion and actual bytes
   consumed. The client must not reuse it until completion: "write from buffer".

Validate generation, participant, resource/session binding, direction, and
overflow-safe ranges on every operation. Distinguish partial completion, EOF,
and errors; reject stale completions after reuse. Use release/acquire publication
for data visibility. CPU events are wakeup hints, not proof of completion.

Read-only consumer mappings do not freeze producer memory. Copy control metadata
before validation and treat client-written data as untrusted. Operations requiring
immutable bytes need explicit snapshot or ownership-transfer rules; do not assume
a malicious client obeys the instruction not to modify an in-flight write buffer.

One Channel carries multiple protocol resources. This implementation shares one
directional pair across the session, serializing access with the existing
one-outstanding-request limit. Transfers start at buffer offset zero; count is
bounded by capacity. The I/O mediator has a separate pair on each backend session
and copies bulk bytes between its upstream and downstream buffers. It must not
copy a departed client's drained completion into a revoked upstream buffer.

## Cleanup and Recycling

Normal clients explicitly release buffers and close protocol resources. A file
close or other protocol-specific release is separate from Channel session close.
Session close must clean up resources the client omitted, without waiting for exit.

`hunde-puffer` also tracks normal exit without cleanup, crashes, kills, and service
failure through authoritative kernel notifications. Reconcile every lease involving
the departed process, including incomplete opens. Exit delivery must remain
recoverable under queue saturation, and PID/incarnation checks must reject delayed
events for a replacement process. The kernel detaches the exiting participant
and publishes the affected buffer slot and lease generation. There is one durable
notification slot per possible buffer, so repeated changes coalesce without
losing an affected lease. The manager rereads authoritative kernel state. This
is lease-change reconciliation, not a general PID-exit subscription service.

Required reclamation order:

1. Mark the lease retiring and reject further submissions under its identity.
2. Complete or cancel outstanding operations and quiesce device access. Notify
   surviving services over Channels; a dead process cannot acknowledge cleanup.
3. Revoke obsolete mappings and complete translation invalidation. Kernel
   attachment and in-flight-use accounting must prevent premature reclamation.
4. Once old access is impossible, retain backing in a bounded reusable pool or
   hard free it. Sanitize before granting a new lease, with a fresh generation.

Process death removes an attachment, not permission to free memory still used by
a service or DMA. Reference counts alone do not establish DMA quiescence. Failed
revocation or uncertain quiescence must quarantine backing, never recycle it.
Duplicate release/exit/completion events must be idempotent. Former clients must
not retain mappings into pooled or reassigned buffers.

Mappings contribute to each participant's shared-memory accounting, not private
memory. A separate published pool-usage metric, pressure-driven eviction,
per-client/service quotas, and policy for unresponsive live peers remain deferred.
`hunde-puffer` failure must not cause unsafe reuse; recovery of manager records
requires explicit design. Service restart/remapping remains deferred and must
not imply replay of uncertain writes. Restart retains backing safely but does not
yet reconstruct all live manager records or remap sessions to replacement PIDs.

### Current Release Rules

- Buffers created between two kernel-registered services retain their backing
  for the rest of the boot. The kernel records this at successful allocation;
  it cannot be cleared by dropping attachments. Close and exit still detach
  mappings, but neither the kernel nor `hunde-puffer` may free, pool, sanitize,
  or reassign that backing. Failed allocation setup remains reclaimable.
  Registration here means the authenticated `SERVICE_BECOME_SERVICE` process
  class, not the presence of a filesystem endpoint during bootstrap.
  This deliberately consumes the finite buffer slots until reboot. Explicit
  unmount, hot-unplug, coordinated reclamation, and restart attachment are
  deferred; ordinary application buffers retain the cleanup policy below.
- The application I/O client releases its session buffers when its last handle
  closes. Protocol handles and the Channel itself remain distinct lifetimes.
- Block clients send a protocol close, then close the Channel. Services also
  release pairs after Channel departure, after draining any active operation.
- Kernel executable loads relinquish their attachment on success or cancellation.
- Process exit detaches only that process. A surviving service keeps its mapping
  until it finishes or cancels work and releases it. Only detached, non-retained
  backing may enter the pool; it is zeroed and assigned a fresh generation before
  reuse.
- Block servers pass the filesystem's shared block-client buffer and sector
  count directly to the hardware callback, without a sector staging copy.
  VirtIO submits up to the full 2 MiB buffer using a single data descriptor
  when contiguous. `MEM_BUFFER_DEVICE_ADDRESS` validates the retained lease,
  server attachment, direction, bounds, and physical contiguity of the entire
  requested range. Noncontiguous ranges are split into smaller submissions.
  This uses the existing physical-address device model, not an IOMMU mapping.
  Cache maintenance surrounds DMA; uncertain completion disables further
  submissions on that device. SDHCI reads multiple sectors through PIO directly
  into the shared buffer. Filesystem-local and mediator copies still remain.

## Integration and Verification

Mama must bootstrap `hunde-puffer` after Channel infrastructure and before bulk
buffers are needed. Avoid depending on an executable loader that itself needs
the buffer service. Keep core policy architecture-neutral, with mapping,
translation invalidation, and device integration behind architecture interfaces.

Migrate both file/executable I/O and filesystem-to-block-driver transfers.
`hund-io-system` remains the mediator. Forward descriptors only after authorizing
downstream attachments; do not copy every 256/512 bytes through intermediaries.
Whether a stage can share a lease end-to-end or needs another buffer depends on
its transformation and permissions. This is not a promise of universal zero-copy.

Test common addresses, asymmetric permissions, rollback, exhaustion, partial
transfers, backpressure, invalid ranges and generations, explicit release, exits
during open and transfer, DMA quiescence, sanitized reuse, and pool eviction.
Measure boot/spawn and multi-block throughput and request counts to demonstrate
removal of per-small-chunk round trips.

The first implementation has kernel permission/rollback/lifetime tests, manager
pool and identity tests, and client/server tests covering transfers larger than
one buffer, invalid descriptors, bounds, and exit during allocation. A four-core
HVF smoke run reached login, spawned shell commands, read `/sbin`, `/boot/sysinfo`,
FAT32 and EXT2 files, and wrote/read a memfs file under `/tmp`. Quantitative
throughput benchmarking remains separate work.

See [TODO](../TODO.md), [Userspace I/O system](io-system.md),
and [Channels and event queues](channels-and-event-queues.md).
