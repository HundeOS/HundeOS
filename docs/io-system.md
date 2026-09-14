# Userspace I/O system

## Status

`hund-io-system` mediates sockets and the boot-configured FAT32/EXT2 mounts
through channels. `hund-dateisystem` owns the userspace mount table and routes
filesystem paths to the format drivers. FAT32 and EXT2 no longer register with
the kernel filesystem broker or fetch/complete filesystem requests through
service syscalls. Their file handles, offsets, and buffers now live in userspace.
Bootstrap filesystems, console streams, and their descriptors remain in the kernel.

## Common application interface

`hund-io-system` will be the common userspace I/O front end for files, sockets,
network I/O, and streams. Applications must not have a separate filesystem-only
front end. Preserve the public userspace I/O API while moving its implementation
from I/O syscalls to a protocol over the existing `Channel`.

```text
Application
    | Channel: I/O protocol
hund-io-system
    |-- Channel --> hund-dateisystem --> Channel --> FAT32/EXT2 --> Channel --> block driver
    |-- Channel --> socket/network service
    `-- Channel --> stream/console service
```

These are protocols over the same bidirectional IPC session abstraction, not
new kinds of channel. Follow [Channels and event queues](channels-and-event-queues.md).

## Ownership

`hund-io-system` owns application-facing handles, backend and endpoint routing,
access checks, operation dispatch, and cleanup of its clients' I/O resources.
It routes common operations such as read, write, and close to the appropriate
backend. Resource-specific operations, such as directory listing or socket
accept, use that same interface without pretending all resource types support
every operation. Unsupported operations return an explicit error.

`hund-dateisystem` owns the filesystem namespace, configured mount table, and
mount selection. Format drivers own filesystem format knowledge and open-file
state. Socket/network services own their network-specific state.
Device drivers own device operations. Moving the front end must move real
ownership out of the kernel, not introduce a service that forwards the same
I/O syscalls back into the kernel.

`hundekanal-warter` remains responsible for channel establishment and session
lifecycle. It does not interpret I/O operations. The kernel retains scheduling,
memory protection, authenticated IPC primitives, and process-exit notification.

## Identity and handles

Identify clients by authenticated PID and services by their `BString` service
identifier. Do not trust a PID supplied in a request payload. A backend must
authenticate `hund-io-system` before accepting any delegated client identity;
the intermediary must not turn an unprivileged request into privileged access.

One client/service channel session carries multiple independent resource
handles. Opening another file or socket does not allocate another channel.
Likewise, `hund-io-system` reuses its backend sessions across resources.
Validate each handle against its owning client session; stale or foreign
handles must not access a recycled resource.

Closing an I/O handle releases that resource, not the channel. Session close
requires cleanup of all remaining resources associated with the session.
Descriptor inheritance and transfer must preserve explicit ownership and
access rights; they must not emerge accidentally from globally usable handles.

## Data and progress

Requests, replies, and shared-buffer descriptors travel over channels. Bulk
data may use separately shared buffers with validated bounds, access modes,
and lifetime. Do not create parallel request/reply rings in those buffers.

The current channel supports one outstanding request per session. Multiple
open handles do not change that limit. The server must continue servicing
other sessions while a backend operation waits; it must not block its entire
dispatcher on one device or client. Additional in-flight request support
requires an explicit protocol change, not extra channels per resource.

## Cleanup and failure

Well-behaved clients explicitly close handles and dispose of buffers when
finished. Cleanup must also handle clients that crash, exit without closing,
or leave operations incomplete. Reliable kernel process-exit notifications
must reach the channel manager, which informs affected services through
channels. Reconcile all resources owned by the departed PID, not only those
whose close messages arrived. Reliable exit delivery is still outstanding
work in the current channel implementation.

Coordinate cancellation or completion before releasing buffers still in use
by a backend or device. Session closure alone is not proof that DMA or other
outstanding accesses have stopped. Backend failure must produce an explicit
I/O failure; do not silently retry an operation through the old kernel path.

Service restart recovery and remapping surviving sessions and buffers are
deferred. Service identifiers provide stable logical identity, but this does
not imply that recovery is currently implemented.

## Migration

1. Sockets migrate first: application socket operations pass through
   `hund-io-system` to the userspace socket service. This path is implemented.
2. Disk-mount routing and FAT32/EXT2 open-file ownership now live in userspace;
   their request delivery uses channels. The userspace file API routes these
   operations through the I/O service. Keep a
   deliberate boundary for unmigrated handles and paths; channel errors must
   never trigger an implicit legacy fallback.
3. Migrate networking and streams into the same I/O front end.
   Implement their operations when migrating actual callers, not as unused
   protocol placeholders in the socket step.
4. Retire the corresponding kernel routing, resource tables, request brokers,
   and syscalls as their callers migrate.

Retain the kernel `/boot/config` filesystem path,
consoles, and streams. Boot dependencies must allow the embedded channel and
I/O services to start without depending on the I/O service they are bringing
up. The root disk mount now lives in userspace. Removing the remaining boot
configuration EXT2 mount is a separate migration.

Test routing, multiple handles over one session, permissions, stale and foreign
handles, close and exit cleanup, backend failure, and progress under blocked
requests. Verify boot ordering and actual file operations on all configured
mounts before retiring their old paths.

## Socket protocol

The implemented path is application -> Channel -> `hund-io-system` -> Channel
-> `socket_router`. The I/O service mediates requests and data; it does not
redirect applications to a backend. All socket buffers, connection pairing,
listener backlogs, and accept queues for this path live in userspace.
`socket://name` remains a socket-registry name, not a filesystem node. This is
local socket communication, not a TCP/IP stack.

The protocol provides open (listen or connect selected by flags), accept,
read, write, and close. Replies contain an explicit I/O status and a handle
or byte count. The I/O service owns application handles, validates operation
kind and read/write flags, and translates handles to backend resources. It
overwrites delegated identity with authenticated channel metadata. The socket
service accepts delegated operations only from the directory-identified I/O
service, through a warden-authenticated channel.

Backend operations are nonblocking. An empty accept/read or a full write buffer
returns `IO_WAIT_FOR_DATA` (`WOULD_BLOCK`). The mediator retains the application
request and retries while scanning other sessions. It never holds the backend
channel waiting for another client's action. Channel replies are retained under
backpressure without repeating completed writes or opens. One application
session still supports only one outstanding operation.

Transfers carry at most 256 bytes in the channel payload and can return short
counts. Open sends a counted endpoint name of at most 255 bytes, reconstructed
as a `BString` by the service; overlong names are rejected rather than truncated.
There are no separately shared socket buffers yet. After peer close, buffered
bytes remain readable, followed by EOF (a successful zero-byte read); writes
to a closed peer fail. Closing a listener releases unaccepted connections but
leaves accepted connections alive.

Application handles start at `1 << 32`, outside the kernel descriptor range.
The userspace library routes those handles to the I/O channel, and routes
`socket://` opens there directly. Filesystem handles use a separate range
starting at `1 << 40`. Kernel-owned descriptors retain their syscall route.
There is no failure-triggered fallback. Socket handles are not
currently inherited by spawned processes; kernel descriptor inheritance still
applies only to kernel-owned descriptors. Service-restart recovery is deferred.

Each client disconnect triggers a backend release-client operation. The warden
sends a channel cancellation notification if a request is in progress, retaining
the payload until the server replies or disconnects. This lets the I/O server
notice departure even when it is holding a blocked accept/read. The
mediator drains an outstanding reply first, including an open that succeeded
after the client departed, then releases all resources for that session.
Backend loss fails requests without replay or reconnection. Mediator-channel
loss clears the socket service's resources. This uses current channel closure
detection; the reliable process-exit notification work described above remains
outstanding. A live client that never closes can still retain bounded resources.

Hundeschule starts the socket service and `hund-io-system` after the channel
and interrupt services, waits for readiness, and then probes disks. Disk
configuration entries for these bootstrap services do not start duplicates.
The old kernel socket implementation and service syscalls remain compiled for
now, but the new socket service no longer registers with that broker and normal
userspace socket calls no longer enter it.

Implementation: `user/lib/io` contains the protocol, client, shared mediation,
and channel server loop; `user/services/hund-io-system` selects backends; socket state lives
in `user/drivers/socket_router/sockets.c`. Tests cover mediation with an
asynchronous fake backend transport, the real channel client/server path,
ownership, backpressure, EOF, and teardown during an outstanding request.

## Boot filesystem namespace

The filesystem path is application -> `hund-io-system` -> `hund-dateisystem`
-> FAT32/EXT2 service -> block driver. Every arrow is an existing Channel,
not a service-specific transport. Each intermediary translates resource handles
and validates them against the delegated client PID and channel session.
`hund-dateisystem` accepts delegated identities only from the authenticated I/O
service; each format driver accepts them only from the filesystem service.

Hundeschule starts `hund-dateisystem` after disk discovery/configuration and
before Hundeschau. It reads `/boot/config/fstab` and installs the configured
FAT32/EXT2 mount paths. Every target configures `/dev/virtio1 ext2 /`, replacing
the former kernel root mount. The filesystem service starts the root
`humpen-wachhund` from that entry and waits for the `/` driver to publish
readiness before publishing its own readiness. Missing or duplicate root
configuration, spawn failure, or failed readiness prevents boot from proceeding
to login. Humpen skips root and starts the remaining format-driver watchdogs
using that same fstab and the shared `MountConfig` parser.
Drivers advertise their mountpoint as their service identifier after
mounting the block device. The namespace connects on demand and retains one
channel per format-driver process. This startup sequence applies to all targets;
there is no HVF-only mount list in the code.

The HVF configuration includes `/`, `/mnt/sd`, `/mnt/channelio`, and
`/mnt/channelio-ext2`. Other targets retain their own fstab entries. A configured
mount whose driver is unavailable returns an I/O error, never access to an
underlying bootstrap file at the same path. Backend loss is terminal for that
connection; restart/remapping remains deferred.

The implemented operations are open, read, write, close, list directory, mkdir,
and remove. Routing selects the longest matching complete mount-path prefix;
root preserves the absolute path forwarded to its driver. Duplicate mountpoints
are rejected, but nested mounts are supported. Directory entries are paged over
the channel: synthetic mount-parent entries precede backing filesystem entries.
The library merges duplicate names, preserving the earlier overlay entry while
advancing the transport offset independently of the output entry count. This
keeps `/`, `/mnt`, `/boot`, and `/tmp` discoverable, even when an intermediate
directory does not exist on disk. Backend failures still propagate as errors.
The bootstrap `/boot/config`, `/boot/sysinfo`, and named
`:...:` streams retain direct syscall routing. Other paths use the I/O service;
only an explicit `IO_PATH_UNMANAGED` reply permits legacy syscall routing.
With `/` configured, ordinary absolute paths are managed even when their driver
fails: there is no kernel root disk mount to fall back to. A transport error is
never an unmanaged-path reply.

Format-driver file buffers are private userspace allocations, not pointers into
client memory. Read opens load a bounded file snapshot; handles have independent
offsets. Writes persist each chunk using the existing whole-file format routines.
Closing a handle frees its buffer without closing the session. Client departure
releases all its resources, including an open that was pending when it exited.
Intermediaries remember deferred opens even after a `WOULD_BLOCK` transport reply
has been consumed. Pending mount operations return `WOULD_BLOCK` to release the
namespace channel while work continues, allowing another mount to progress.

Current limits are explicit: eight mounts including root, `/tmp`, and `/dev`, 32 open
files per format driver, 255-byte protocol paths, and 256-byte transfer chunks.
The client library combines file chunks into the caller's requested transfer.
The existing format limits remain: 12 KiB read snapshots, 512-byte FAT32 writes,
and 12 KiB EXT2 writes. Hard links through this protocol, seeking,
descriptor inheritance/transfer, service recovery, and full POSIX semantics are
not implemented. Disk drivers enforce read/write access flags and session
ownership, but do not yet add Unix mode-bit authorization that the old
filesystem broker did not enforce. Memfs additionally enforces node permissions
as described below.

Embedded executable loading reads the immutable archive directly in the kernel,
without a kernel filesystem mount. Embedded `/sbin` programs continue to boot
and execute, but spawning an ELF from a channel-backed
mount, including the root disk, is not supported until executable loading is
migrated. Moving the root mount does not add a kernel-to-userspace file broker.

The old kernel filesystem broker remains compiled for unmigrated callers, but
the boot FAT32/EXT2 drivers no longer use it. Kernel EXT2 is retained only for
`/boot/config`; embedded executable loading does not depend on the root disk.
Namespace, mediation, routing, format operations, and
disconnect behavior have userspace tests with fake kernel/block boundaries.

The kernel boot-information provider (`kernel/subsystems/io/file/bootfs`) exposes
`/boot/sysinfo`, previously `/proc/sysinfo`. Its hardware data and live statistics
are unchanged. UART, framebuffer, PCI, SD-host, and Ethernet configuration readers
use the new path before the userspace I/O services are ready. `/boot/config`
remains a separate EXT2 mount beneath `/boot`. The userspace namespace lists
both entries and marks `sysinfo` as a read-only file, not a directory.
Only the exact `/boot/sysinfo` file bypasses channel routing; other `/boot` paths
remain subject to normal routing unless covered by `/boot/config`. There is no
kernel `/proc` mount or compatibility alias. A separate userspace procfs is now
mounted there for process accounting; it does not relocate `/boot/sysinfo`.

### Embedded Filesystem

`hund-dateisystem` starts `/sbin/drivers/embedfs` after procfs and waits for
`/sbin` before publishing readiness on every target. Open, read, close, and
directory listing use the existing Channel-based I/O path. The kernel's embedded
filesystem implementation is retired to a test fixture.
See [Bootstrap filesystem handoff](bootstrap-filesystem-handoff.md) for archive
mapping, permissions, resource ownership, and the remaining executable-loader
boundary.

### Process Filesystem

`hund-dateisystem` starts `/sbin/drivers/procfs` after devfs and waits for `/proc`
before publishing namespace readiness. It uses the existing filesystem driver
and bidirectional Channel protocol, with no additional transport. The kernel
publishes read-only shared process records; procfs formats them and `ps` reads
them directly. See [Process registry and procfs](process-registry-and-procfs.md)
for CPU, wait-state, memory-accounting, consistency, and ownership semantics.

## Device filesystem

`hund-dateisystem` starts `/sbin/drivers/devfs` and mounts it at `/dev` on every
target, after root and `/tmp` and before publishing readiness. Devfs uses the
existing filesystem driver, Channel, and I/O protocol. It owns directory
representation, metadata text, open handles, read offsets, and session cleanup.
There is no kernel devfs mount or blanket `/dev` syscall bypass.

The kernel publishes authoritative device records in a read-only, non-executable
mapping at `0x70800000`, available to every process independently of devfs startup.
The mapping size is derived from the registry layout and rounded to pages.
It accommodates 512 devices, covering both existing 256-device tables; aliases
share their device's record. Records contain
the device path, alias, kind, current owner PID, block geometry, and a `BString`
block-service identifier. The block path is the existing service identifier.
Registration still requires the existing service capabilities. Neither registry
visibility nor opening metadata grants MMIO, IRQ, or block-I/O authority.

Kernel writers serialize updates. Atomic byte storage and per-record odd/even generations
provide coherent record reads without making readers write a lock. Readers make
bounded attempts and retry a busy publication through `IO_WAIT_FOR_DATA`.
Paged directory listings observe live records rather than promising one snapshot
across separate requests. Block registration publishes its record before returning
success; publication failure rolls registration back. Process exit removes the
departed owner's records after device quiescence. Metadata already opened is a
private snapshot, not a grant to access a device that has since exited.

Devfs lists device paths and `hdN` aliases and exposes block metadata as read-only
files. Mutation and executable opens are rejected. Legacy kernel-backed streams
such as `/dev/businfo` are registered in the same directory but return explicit
`IO_PATH_UNMANAGED` on open, retaining their existing kernel stream descriptors.
Unknown devices and registry/backend failures never trigger that route. Moving
stream data transport remains a separate step; the kernel device/stream brokers
and bootstrap block registry are not retired by moving devfs.

Bootstrap disk discovery continues to use hardware information and service
registration, not `/dev` file reads. There is no devfs dependency cycle and no
new service-specific channel or update queue. Each devfs operation reads current
registry records; service restart recovery remains deferred.

## Temporary filesystem

`hund-dateisystem` installs a deviceless `memfs` mount at `/tmp` on every target,
starts the embedded `/sbin/drivers/memfs` program, and waits for its readiness
before publishing namespace readiness. A conflicting `/tmp` fstab entry or
failed startup prevents publication. No disk configuration entry is required.

Temporary-file operations follow application -> `hund-io-system` ->
`hund-dateisystem` -> memfs, over the existing Channel and I/O protocol.
The former kernel `/tmp/memfs` mount and syscall bypass have been removed.
The old implementation exists only as a kernel-test fixture and is not linked
into the kernel.

Memfs owns dynamically allocated file contents and a linked collection of files
and directories, with no 12 KiB disk-snapshot limit or fixed file-count table.
It supports 32 open handles, independent offsets, immediate empty-file creation
and write-only-open truncation, directory listing, mkdir, file removal, and
recursive directory removal. Read/write opens preserve existing data. Operations
use the existing 256-byte protocol chunks. Allocation failures return errors
without advancing the handle offset or discarding existing data.

The backend authenticates the filesystem service before accepting delegated
client PID/session identities. Close and client release dispose of handles,
not files; namespace disconnect releases all handles but preserves files.
Removal frees storage and invalidates handles to the removed node; recreating
the same path cannot revive them. Nonrecursive directory removal remains
unsupported, matching the previous memfs behavior. Contents are volatile and
are lost when the driver exits. Service restart recovery and reliable exit
notification have the same outstanding limitations described above.

Memfs records the creating client's UID/GID on each node, with `0644` files and
`0755` directories. It resolves credentials using `process_credentials(pid)`,
which returns a typed kernel snapshot copied while the scheduler lock is held.
The PID comes from the authenticated I/O/namespace delegation chain, not a
client-supplied UID or GID. Unknown or exited PIDs fail path operations closed;
there is no fallback to UID 0. Credential metadata is queryable like the existing
process listing; the lookup does not grant permission to act as that process.

Open enforces owner/group/other read, write, and execute bits before truncation
or allocating a handle. Parent directories require search permission; creation
and removal require parent write/search permission; listing requires read/search.
The mount root permits shared creation with sticky `/tmp` removal semantics:
only an entry's owner or the existing root identity may remove a top-level entry.
Recursive removal checks the whole affected tree before making changes. Root
has its existing privileged identity, but cannot execute a regular file with no
execute bits. Supplementary groups, chmod/chown, and umask are not implemented.
Open handles retain their granted access and session ownership; close and
release-client remain usable without querying a departed PID.

The current account model contains built-in root (UID/GID 0), hund (1), and
user (2) identities and hard-coded test logins in `kernel/syscall/auth.c`.
`ls` displays these existing names; a memfs file created by user now reports
`user user`, not the filesystem service's identity or a hard-coded root owner.
