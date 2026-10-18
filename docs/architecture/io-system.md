# Userspace I/O system

The [Mama bootstrap](bootstrap-services.md) now establishes `/boot`,
`/boot/config`, and `/sbin` in userspace before configured startup. The filesystem
service starts empty and accepts authenticated Channel mount requests; it no
longer reads `fstab` itself. Boot file operations no longer use a syscall bypass.

## Status

`hund-io-system` routes sockets, streams, and filesystem paths without retaining
per-request or per-handle state. `hund-dateisystem` owns the userspace mount table and routes
filesystem paths to the format drivers. FAT32 and EXT2 no longer register with
the kernel filesystem broker or fetch/complete filesystem requests through
service syscalls. Their file handles, offsets, and buffers now live in userspace.
Console streams now use the same I/O protocol and Channels. The kernel retains
boot-level-0 embedded executable loading and independent boot/panic output, not
bootstrap filesystem mounts or console/keyboard data buffering.

The agreed next architecture consolidates this routing, namespace, and the
high-level filesystem, socket, stream, pipe, terminal, and information
components into `hunde-io-host`. Until that migration occurs, the ownership and
protocols in this document describe the running system. See
[I/O and driver hosts](io-driver-hosts.md).

## Common application interface

`hund-io-system` is the common userspace I/O front end for files, sockets,
network I/O, and streams. Applications must not have a separate filesystem-only
front end. Preserve the public userspace I/O API while moving its implementation
from I/O syscalls to a protocol over the existing `Channel`.

```text
Application
    |-- route request --> hund-io-system
    |                     `-- filesystem route --> hund-dateisystem
    |                                              `-- mount route reply
    `-- direct Channel --> final filesystem/socket/stream service
                           `-- Channel --> block driver where required
```

These are protocols over the same bidirectional IPC session abstraction, not
new kinds of channel. Follow [Channels and event queues](channels-and-event-queues.md).

Target programs may use either the native `IO__*` interface or the userspace
C/POSIX compatibility layer. Native I/O handles begin above `1 << 32`; the
compatibility layer therefore maps them to process-local `int` descriptors and
converts native EOF and errors to POSIX return values and `errno`. Fallible
native `IO__*` operations return `Result(payload)`: success values contain only
the requested handle, count, position, change, attributes, or status, while the
error branch carries a named message and the corresponding `IOStatus` as its
code. `IOStatus` remains the bounded protocol status on Channel replies; callers
must not infer failure from a negative payload or inspect a reply status outside
the I/O client boundary.
Descriptors `0`, `1`, and `2` retain their standard stream meanings. This map
and the stdio implementation are entirely userspace concerns.

The POSIX compatibility layer also owns each process's current working
directory. It resolves relative paths before sending absolute paths to
`hund-io-system`; `chdir` verifies the destination through the ordinary file
information request, and `getcwd` reports the stored normalized path. Fork
inherits this userspace state with the rest of the address space. Exec carries
the path in its validated startup payload so the replacement image retains the
same directory, while native kernel spawns begin at `/`. The kernel transports
this startup string but does not resolve paths or own filesystem namespace
state.

Pinned, unmodified OpenBSD `cat`, `cp`, `grep`, and `tail` sources are built
directly from `vendor/openbsd` and installed in the disk rootfs under
`/usr/local/bin`. The pinned, unmodified OpenBSD `kill` source is also built and
installed there. Its positive-PID and signal-zero paths use the shared signal
compatibility layer; process-group targets remain unsupported until the process
group and permission work recorded in `docs/TODO.md` is complete.
The compatibility stdio layer uses seek-aware file read buffering and
line-buffered standard output to avoid an I/O-service request for each
character. `fstat` exposes a transfer block size but not full filesystem
metadata, and `pledge` currently records compatibility rather than enforcing a
sandbox; neither limitation moves I/O policy into the kernel.

`tail -f` registers file handles with `hund-io-system` and blocks on a batched
userspace I/O request containing its watched handles and last-seen generations.
Filesystem writes, removals, and recreations advance the corresponding path's
generation. The service completes the blocked request only when one generation
changes, allowing the ordinary channel wait path to deschedule `tail` without a
polling timer or a kernel filesystem watcher. The libc `kqueue` compatibility
subset translates these notifications for the unchanged OpenBSD source.

## Ownership

`hund-io-system` owns only top-level endpoint selection. It returns a bounded
redirect describing the next service, rewritten path, and whether that service
is final. `hund-dateisystem` similarly performs mount selection and path
rewriting, with one namespace-specific responsibility: directory enumeration
includes immediate child mountpoints before redirecting to the backing
filesystem. It does not manufacture file metadata. Neither routing service owns
client handles, buffers, pending requests, or backend Channels.

The application I/O client follows at most eight redirects and then opens or
reuses a direct Channel to the final service. It gives callers process-local
opaque handles and maps them to the final service's handles. The final
filesystem, socket, or stream service owns resource state, access checks, and
cleanup for its authenticated client sessions. Unsupported operations return an
explicit error.

`hund-dateisystem` owns the filesystem namespace, configured mount table, and
mount selection. Format drivers own filesystem format knowledge and open-file
state. Socket/network services own their network-specific state.
Device drivers own device operations. Moving the front end must move real
ownership out of the kernel, not introduce a service that forwards the same
I/O syscalls back into the kernel.

Anonymous pipes are owned by the `hund-rohr` userspace service. `hund-io-system`
only returns a final redirect for pipe creation. Each pipe has independently
authenticated read and write endpoint handles and a service-private 64 KiB byte
ring. Reads wait while the ring is empty and a writer remains, then return EOF
after the final writer closes. Writes of at most 4096 bytes are admitted
atomically; larger writes may make partial progress. A full ring retains the
request through the ordinary I/O server wait path. A transfer carrying
`IO_TRANSFER_NONBLOCK` instead receives `IO_WOULD_BLOCK`, which the POSIX layer
maps to `EAGAIN`; blocking descriptors continue to use the retained-request
path. A write with no readers returns `IO_BROKEN_PIPE`. Data still moves through
the session's directional
`hunde-puffer` buffers; the pipe ring is service state, not another IPC
transport.

The POSIX compatibility layer maps each descriptor to a reference-counted local
open description. `dup`, `dup2`, and `F_DUPFD` therefore share one native pipe
endpoint, its access mode and `O_NONBLOCK` status, and close it only after the
final local descriptor closes. `F_GETFL` and `F_SETFL` operate on that shared
open description, while `FD_CLOEXEC` remains local to each descriptor. These
local references do not authorize another process. Fork inheritance still
requires a child-owned Channel session and an explicit service grant to the same
open resource, as tracked in `docs/TODO.md`.

`O_APPEND` is also shared through the local open description. Libc marks each
write request with `IO_TRANSFER_APPEND`; the final filesystem service selects
the file's current size and performs the write in the same service operation.
Seeking an append descriptor therefore does not change the next write position,
and independent append handles cannot select the same stale end offset.

`hundekanal-warter` authorizes Channel establishment and delegates a direct Queue
between the authenticated client and service. It does not retain established
session state or interpret, relay, or acknowledge I/O operations. The kernel
owns mappings, connection close, and process-exit cleanup.

## Identity and handles

Identify clients by authenticated PID and services by their `BString` service
identifier. Do not trust a PID or session supplied in a request payload. The
common I/O server overwrites both fields with authenticated Channel metadata
before calling a service. Kernel-originated executable loads retain their
authenticated origin PID while following the same redirect protocol.

One client/service channel session carries multiple independent resource
handles. Opening another file or socket does not allocate another channel.
Likewise, the client library reuses each direct final-service session across resources.
Validate each handle against its owning client session; stale or foreign
handles must not access a recycled resource.

Closing an I/O handle releases that resource, not the channel. Session close
requires cleanup of all remaining resources associated with the session.
Descriptor inheritance and transfer must preserve explicit ownership and
access rights; they must not emerge accidentally from globally usable handles.

## Data and progress

Requests, replies, and shared-buffer descriptors travel over channels. The
agreed bulk-data design uses separately shared directional buffers managed by
[`hunde-puffer`](shared-buffers.md), with common virtual addresses, validated
bounds, enforced access modes, and explicit lease lifetimes. Protocol open
responses advertise the buffers; read/write commands and completions remain on
the Channel. Do not create parallel request/reply rings in those buffers.

The first implementation uses 2 MiB directional leases advertised by the final
service on open. Routing services do not allocate or copy bulk buffers. Block commands describe
multiple sectors per request; hardware callbacks now receive the shared buffer
directly. VirtIO uses DMA into retained service-to-service backing, while SDHCI
uses multi-sector PIO. See [release rules](shared-buffers.md#current-release-rules)
for the temporary boot-lifetime retention policy.

EXT2 places a bounded 1024-sector (512 KiB) scan-resistant LRU cache above its
`BlockClient`. Reads retain contiguous miss batching. A repeated access protects
a sector, while one-off sectors remain probationary and are evicted first.
Writes reach the block service before updating sectors already resident in the
cache; uncached streaming writes are not admitted. This keeps device coherence
simple without displacing the hot read working set. Cache hits, misses, and
evictions are exported with the filesystem process's I/O diagnostics through
`/proc/<pid>/ipc`. The cache is userspace filesystem policy; it does not restore
a kernel block cache.

EXT2 also keeps a bounded 256-entry inode cache using the same generic
scan-resistant cache policy. Reads are keyed by inode number. Successful
inode-table writes update the cached value only after the block write completes,
and freeing an inode invalidates it after the bitmap write completes. Inode
cache hits, misses, and evictions are reported separately through
`/proc/<pid>/ipc`.

The current channel supports one outstanding request per session. Multiple
open handles do not change that limit. The server must continue servicing
other sessions while a backend operation waits; it must not block its entire
dispatcher on one device or client. Additional in-flight request support
requires an explicit protocol change, not extra channels per resource.

Routing requests complete synchronously with a redirect or error and therefore
need no per-request fibres. A final service may retain a request by returning no
reply while it waits for data. Its other authenticated sessions continue to be
polled by the common server loop.

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

Retain independent boot/panic output and boot-level-0 embedded executable loading.
Mama starts the Channel and I/O services without filesystem dependencies, then
establishes userspace `/boot` mounts before configuration is read.

Test routing, multiple handles over one session, permissions, stale and foreign
handles, close and exit cleanup, backend failure, and progress under blocked
requests. Verify boot ordering and actual file operations on all configured
mounts before retiring their old paths.

## Console streams

`hund-io-system` owns a userspace stream-device registry. Stream paths use
`:name:` syntax: names must be nonempty and contain no colon, slash, NUL, or
space/tab/newline characters. Each registration associates a BString path and
one access direction (read or write) with a BString backend service identifier.
Duplicate name/direction registrations are rejected. Unknown names return
`IO_NO_DEVICE_FOUND`; malformed names return `IO_INVALID_PATH`, and unsupported
access returns `IO_PERMISSION_DENIED`. Colon-prefixed opens never fall through
to the filesystem router.

Boot setup registers the terminal routes below. The registry and stream-driver
helpers validate stream syntax, not a built-in list of device names. Direct
Channels are opened lazily and reused by service identifier, including across
aliases. Open handles retain final-service routing and client ownership independently
of subsequent path lookup. Registration is local to the I/O service for now;
there is no driver-controlled registration protocol. `:keyboard:` reads route
to the `keyboard` service using this same registry and Channel protocol.

Applications open `:tty:` or `:console:` through `hund-io-system`. Reads route
to `uart_rx`; `:tty:` writes route to `uart_tx`. `:console:` writes use
`video_term` when a framebuffer is present, otherwise `uart_tx`. These drivers
use the existing I/O protocol over Channel, with multiple owned handles per
session, direction checks, close, and session cleanup. They do not fetch or
complete console buffers through kernel I/O syscalls.

`uart_rx` owns a 1024-byte input buffer and subscribes to UART interrupts through
`hund-unterbrict`. It drains the FIFO and clears device status before sending
the interrupt-protocol acknowledgement. When the input buffer fills, it retains
the pending interrupt until reads make space. `uart_tx` handles FIFO backpressure
by waiting for transmit readiness before each byte, including newline translation,
and completes the accepted write before replying. The stream driver retains an
empty read at its backend until input arrives, so the application request remains
blocked without Channel request/reply churn or retries through routing services.

The shell assigns its foreground child to the terminal through its authenticated
input handle, then waits for that child explicitly. While a foreground process is
assigned, `uart_rx` consumes the Ctrl-C byte and sends that process
`PROCESS_SIGNAL_INTERRUPT`; the byte is not returned as input. With no foreground
child, Ctrl-C reaches the interactive shell and cancels its current input line.
This is currently single-process foreground ownership. Foreground process groups
remain necessary before pipelines can receive terminal signals as a unit.

Initial standard descriptors carry terminal endpoint metadata, not another
process's channel handles. The kernel passes this metadata in the process
startup context; userspace lazily opens its own input/output/error handles.
Login and child commands therefore use independently authenticated sessions.
General channel-handle inheritance is not implemented. Closing a local standard
stream does not update the kernel's inherited endpoint metadata.

The userspace compatibility layer also derives terminal identity from that
metadata. UART descriptors report `:tty:` and keyboard/video descriptors report
the logical `:console:` terminal. `ttyname` and `isatty` preserve this identity
when a standard descriptor is duplicated, and reject closed, invalid, and
non-terminal descriptors. This identity does not yet provide a `/dev/tty` node
or a bidirectional terminal open; those remain part of controlling-terminal
support.

The kernel stores only the three startup endpoint values, not an open-file
descriptor table. Spawn may select terminal endpoint markers or the parent's
standard slots (0, 1, 2); other handle-transfer requests fail. Actual open-resource
sharing belongs in the future userspace descriptor-inheritance protocol.

Serial boot waits for both UART services before starting login. Framebuffer
login waits for both `video_term` and `keyboard`. VirtIO keyboard input uses a
bounded userspace buffer, retaining used-ring events and the outstanding IRQ
when there is insufficient space for a complete key sequence. Device interrupt
status is cleared before the completion ACK is sent to `hund-unterbrict`.
The USB keyboard stub exposes the same stream interface, but does not yet
acquire hardware input. Only one keyboard backend is configured at a time.
Kernel boot messages, panic output, and diagnostic logging retain an
independent architecture UART path and do not depend on these services.

## Socket protocol

The implemented control path is application -> `hund-io-system` redirect ->
`socket_router`; subsequent operations use the application's direct Channel to
`socket_router`. All socket buffers, connection pairing,
listener backlogs, and accept queues for this path live in userspace.
`socket://name` remains a socket-registry name, not a filesystem node. This is
local socket communication, not a TCP/IP stack.

The protocol provides open (listen or connect selected by flags), accept,
read, write, and close. Replies contain an explicit I/O status and a handle
or byte count. The client library owns local handles and translates them to
socket-service handles. The common server overwrites request identity with
authenticated Channel metadata; `socket_router` validates operation kind,
access flags, and resource ownership against that direct session.

Backend operations are nonblocking. An empty accept/read or a full write buffer
returns `IO_WAIT_FOR_DATA` (`WOULD_BLOCK`). The socket server retains an empty
accept or read without publishing an interim reply while continuing to scan
other sessions. Channel replies are retained under backpressure without
repeating completed writes or opens. One application session still supports
only one outstanding operation.

Transfers use the directional shared buffers and can return short counts.
Open sends a counted endpoint name of at most 255 bytes, reconstructed
as a `BString` by the service; overlong names are rejected rather than truncated.
Socket transport also uses the session's directional buffers. After peer close, buffered
bytes remain readable, followed by EOF (a successful zero-byte read); writes
to a closed peer fail. Closing a listener releases unaccepted connections but
leaves accepted connections alive.

Application handles start at `1 << 32`; filesystem handles use a separate range
starting at `1 << 40`. The userspace library routes these handles and socket
opens to the I/O channel. There is no kernel descriptor table or syscall fallback.
Socket handles are not currently inherited by spawned processes.
Service-restart recovery is deferred.

Each client disconnect releases the socket resources owned by that authenticated
session. Direct Queue closure wakes the service, which cancels or completes any
in-progress protocol work and releases the session resources. Backend loss fails
requests without replay or reconnection. A live client that never closes can
still retain bounded resources.

Mama starts the Channel, interrupt, filesystem, and I/O services. Hundeschule
starts the socket service later; the I/O service connects to it on demand.
Configuration entries for already-established core services do not start duplicates.
The old kernel socket implementation and broker service operations have been
deleted. Userspace socket calls use the Channel-based socket service.

Implementation: `user/lib/io` contains the protocol, redirect-following client,
and channel server loop; `user/services/hund-io-system` selects routes; socket
state lives in `user/drivers/socket_router/sockets.c`. Tests cover routing, the
real channel client/server path, ownership, backpressure, EOF, and teardown.

## Boot filesystem namespace

The filesystem control path is application -> `hund-io-system` redirect ->
`hund-dateisystem` redirect -> FAT32/EXT2 service. The application then talks
directly to that final filesystem service; its block operations use a separate
Channel to the block driver. Every arrow is an existing Channel, not a
service-specific transport. Routing services translate paths, not handles.

Mama starts `hund-dateisystem` with an empty namespace, then establishes `/sbin`,
`/boot`, and `/boot/config`. Hundeschule subsequently reads `/boot/config/fstab`
through Channels, registers the configured FAT32/EXT2 mounts, and starts the root
`humpen-wachhund`. It waits for root readiness before starting Hundeschau. Missing
or duplicate root configuration, spawn failure, or failed readiness prevents
startup from reaching login. Humpen skips root and starts the remaining
format-driver watchdogs using the same fstab and shared `MountConfig` parser.
Drivers advertise their mountpoint as their service identifier after mounting
the block device. The namespace returns that identifier in its redirect. This startup sequence applies to all targets;
there is no HVF-only mount list in the code.

The HVF configuration includes `/`, `/mnt/sd`, `/mnt/channelio`, and
`/mnt/channelio-ext2`. Other targets retain their own fstab entries. A configured
mount whose driver is unavailable returns an I/O error, never access to an
underlying bootstrap file at the same path. Backend loss is terminal for that
connection; restart/remapping remains deferred.

The QEMU Pi UEFI trace configuration mounts `/dev/virtio2` (the EXT2 boot
configuration image) at `/mnt/ext2user`. `/dev/virtio4` is the FAT EFI system
disk, not an EXT2 volume. Using it here causes repeated mount failures and
eventually exhausts the boot-lifetime retained service buffers described in
[Shared buffers](shared-buffers.md#current-release-rules).
After correcting that entry, a four-core traced QEMU run listed
`/mnt/ext2user` and completed 150 consecutive `ps` launches without mount or
command failures. Debugger snapshots near startup, midway, and after the run
showed 28 buffer slots and six detached retained buffers throughout; the
failed-mount accumulation was absent. This is a stability check, not a
throughput benchmark or proof of general service-restart reclamation.

The implemented operations are open, read, write, close, list directory, mkdir,
and remove. Routing selects the longest matching complete mount-path prefix;
root preserves the absolute path forwarded to its driver. Duplicate mountpoints
are rejected, but nested mounts are supported. Directory enumeration first
returns the immediate child mountpoints owned by the namespace, including
synthetic parents such as `/mnt`, and then redirects to the selected filesystem
driver with an adjusted paging offset. The client removes duplicate backend
names so a mounted directory is authoritative. Other metadata requests are
redirected unchanged, and backend failures still propagate as errors.
`/boot/config` and `/boot/sysinfo` use the same Channel routing as other files.
Named `:...:` streams and other paths use the I/O service;
an `IO_PATH_UNMANAGED` reply does not permit legacy syscall routing.
With `/` configured, ordinary absolute paths are managed even when their driver
fails: there is no kernel root disk mount to fall back to. A transport error is
never an unmanaged-path reply.

Format-driver write buffers are private userspace allocations, not pointers into
client memory. Pseudo filesystems may retain a bounded read snapshot, while disk
format drivers describe the file at open and read requested ranges directly into
the transport buffer. Handles have independent offsets. Writes remain bounded
and persist each chunk using the existing whole-file format routines. Closing a
handle frees any private buffer without closing the session. Client departure
releases all its resources, including an open that was pending when it exited.
Intermediaries remember deferred opens even after a `WOULD_BLOCK` transport reply
has been consumed. Pending mount operations return `WOULD_BLOCK` to release the
namespace channel while work continues, allowing another mount to progress.

Current limits are explicit: sixteen mounts including root, `/tmp`, and `/dev`, 32 open
files per format driver and 255-byte protocol paths. Transport buffers are 2 MiB.
The client library combines file chunks into the caller's requested transfer.
Pseudo-filesystem snapshots remain limited to 12 KiB. Disk reads are streamed;
EXT2 resolves direct, single-indirect, and double-indirect blocks, while FAT32
walks cluster chains. The write limits remain 512 bytes for FAT32 and 12 KiB for
EXT2. Hard-link creation in filesystem drivers, seeking,
descriptor inheritance/transfer, service recovery, and full POSIX semantics are
not implemented. Disk drivers enforce read/write access flags and session
ownership, but do not yet add Unix mode-bit authorization that the old
filesystem broker did not enforce. Memfs additionally enforces node permissions
as described below.

All application file operations now use Channels, including `link_file`. A link
request carries both paths; the namespace resolves both mounts, rejects links
across mounts, and forwards mount-relative paths to the same provider. Current
providers return `IO_OPERATION_NOT_SUPPORTED`; transporting the request does
not implement on-disk hard links. The application library never retries file
operations through kernel I/O syscalls, including unmanaged-path replies.
Directory listing includes namespace mount entries without a kernel merge.
Legacy kernel handles are rejected; standard streams still lazily open their
configured userspace stream endpoints.

Normal executable loading uses kernel -> Channel -> `hund-io-system` -> namespace
-> filesystem provider. Only Mama's `boot_spawn()` can read the embedded archive
directly, and only before the irreversible `boot_complete()` handoff. Normal
spawn has no embedded or old filesystem fallback. The loader copies validated
shared-buffer ranges into a kernel-owned image, bounded to 64 MiB, then validates
the static ELF before mapping (at most 128 MiB of segment memory). Disk providers
stream executable reads without the pseudo-filesystem snapshot limit. EXT2
requires at least one execute mode bit before accepting an executable open;
FAT32 applies the mount policy's synthesized executable mode. Per-caller Unix
owner/group mode-bit authorization is not yet implemented. Embedded files do not
have the pseudo-filesystem snapshot limit.
See [Executable loading](bootstrap-filesystem-handoff.md).

The old kernel filesystem broker, bootfs, EXT2, block cache, and memory block
device have been deleted. The kernel mount syscall and watchdog fallback are
also retired. Embedded executable loading does not depend on the root disk.
The generic kernel I/O API, root filesystem/mount router, file-handle tables,
`kernel/kio` wrappers, and file-I/O syscall handler have also been deleted.
The old `IO_CALL` number is reserved and returns an unsupported-syscall error.
Syscall handlers use a keyed map so unregistered numbers cannot dispatch null
function pointers. Process fault reports go directly to kernel diagnostics,
independently of userspace stdout/stderr and I/O services.
The kernel block broker and its request, completion, connection, and buffer
transport have been deleted. Its syscall numbers remain reserved and reject
calls. Block-device registration publishes read-only discovery metadata only;
authorization, memory mappings, shared registries, and executable-loading
Channels remain kernel mechanisms.

The generic in-kernel message broker has also been deleted. Its I/O and service
callbacks had no production publishers, and its process-exit broadcasts had no
production subscribers. Scheduler park/unpark operations remain direct calls;
process-exit handling, parent notifications, shared Channels, event queues, and
IRQ delivery do not depend on that broker.

VirtIO block I/O uses only its Channel server and managed shared buffers.
The separate `sdhci_channelio` executable replaces the old SDHCI driver in
SD boot configurations. It runs its Channel server in a fibre and retains
the existing read-only, polling PIO hardware implementation; device interrupt
signalling remains disabled and it does not subscribe directly to kernel IRQs.
Interrupt-driven SD transfers and SD write support are not implemented.
The `channel_blk`/`channel_fs` demo pair also uses the shared BlockServer and
BlockClient APIs, not private request/reply rings. Unused `IO__block_read` and
`IO__block_write` syscall helpers are deleted; general fibre support remains.
Namespace routing, client redirects, format operations, and
disconnect behavior have userspace tests with fake kernel/block boundaries.

The userspace `sysinfofs` driver exposes `/boot/sysinfo` from the typed kernel
hardware snapshot described in [Bootstrap services](bootstrap-services.md).
UART, framebuffer, PCI, SD-host, and Ethernet readers use that file after Mama
establishes `/boot`. The I/O service uses the typed syscall directly before
filesystems are ready. `/boot/config` is a nested userspace EXT2 mount. Neither
path bypasses Channels. `/proc` remains a separate userspace accounting filesystem.

### Embedded Filesystem

Mama bootstraps `/sbin/drivers/embedfs` and mounts `/sbin` before handoff.
Open, read, close, and
directory listing use the existing Channel-based I/O path. The kernel's embedded
filesystem implementation and its obsolete test fixture have been deleted.
See [Bootstrap filesystem handoff](bootstrap-filesystem-handoff.md) for archive
mapping, permissions, resource ownership, and executable-loader
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

Devfs provides `/dev/null` as a writable character-like file using the existing
stream filesystem operations. Reads immediately return end-of-file and writes
discard all supplied bytes while reporting them consumed. Device-registry
metadata entries remain read-only.

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
files. Mutation and executable opens are rejected. No kernel stream devices
remain registered at boot: `/dev/businfo` moved to the `hundebus` Channel
interface and the `/dev/hello` proof-of-concept driver was deleted.
Legacy stream recognition and `IO_PATH_UNMANAGED` handling remain compiled,
but unknown devices and registry/backend failures never trigger that route.
The unused kernel console and stream backends, their buffered UART IRQ path,
and backend-specific tests have been removed. Low-level UART output for kernel
logging and panic remains independent of the I/O subsystem. Legacy stream-server
operations and their buffer mapping/server maps are also removed. The block
registry remains for its current clients.

Bootstrap disk discovery continues to use hardware information and service
registration, not `/dev` file reads. There is no devfs dependency cycle and no
new service-specific channel or update queue. Each devfs operation reads current
registry records; service restart recovery remains deferred.

## Temporary filesystem

Hundeschule installs a deviceless `memfs` mount at `/tmp`, starts the embedded
`/sbin/drivers/memfs` program, and waits for readiness before launching
Hundeschau. A conflicting `/tmp` fstab entry or failed startup stops that sequence.
No disk configuration entry is required.

Temporary-file route discovery follows application -> `hund-io-system` ->
`hund-dateisystem` -> memfs. Operations after the redirect use the application's
direct Channel to memfs and the existing I/O protocol.
The former kernel `/tmp/memfs` mount and syscall bypass have been removed.
The old implementation exists only as a kernel-test fixture and is not linked
into the kernel.

Memfs owns dynamically allocated file contents and a linked collection of files
and directories, with no 12 KiB disk-snapshot limit or fixed file-count table.
It supports 32 open handles, independent offsets, immediate empty-file creation
and write-only-open truncation, directory listing, mkdir, file removal, and
recursive directory removal. Read/write opens preserve existing data. Operations
use shared directional buffers, up to 2 MiB per request. Allocation failures return errors
without advancing the handle offset or discarding existing data.

The backend receives authenticated client PID/session identities from its direct
Channel. Close and client release dispose of handles, not files; a client
disconnect releases only that session's handles and preserves files and other
clients' handles.
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
