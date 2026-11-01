# Userspace I/O system

## Overview

The Rust `hunde-io-host` process serves the common application-facing
`hund-io-system` endpoint. It owns namespace routing, mount policy, file handles,
and dispatch to filesystem, socket, pipe, and logical-console components.
Hardware-facing services communicate through authenticated Channels; bulk data
uses directional shared buffers managed by `hunde-puffer`. Channels carry
commands, buffer descriptors, and completions. See
[shared buffers](shared-buffers.md) and [I/O and driver hosts](io-driver-hosts.md)
for transport lifecycle and component ownership.

The host mounts initial filesystems from `config/bootstrap.fstab`, packaged as
`/sbin/config/bootstrap.fstab`. Each row names a loadable Wuff. Configuration and
components are read directly from the mapped archive before any mount exists.
The initial namespace includes `embedfs` at `/sbin`, `sysinfofs` at `/boot`,
`devfs` at `/dev`, `memfs` at `/tmp`, and `procfs` at `/proc`.
`config/io-host.conf` supplies approved service images and block-filesystem
factories, including native Rust EXT2 and FAT32 components.

[Mama](bootstrap-services.md) establishes the initial namespace and boot
configuration before `hundeschule` reads startup configuration. Platform fstab
files select additional mounts, and boot proceeds to login after required mounts
are ready. Standard QEMU configurations select hosted `ext2.wuff` for `/`,
`/mnt/channelio-ext2`, and `/mnt/hosted-ext2`, and `fat32.wuff` for configured
FAT32 volumes. `/boot/config` uses the hosted EXT2 component. Nested mounts
override paths in the root filesystem.

`sysinfofs` renders the kernel hardware snapshot; `devfs` presents the read-only
device registry through ordinary filesystem requests. Memfs supplies temporary
files with creator UID/GID and permission checks. Procfs renders process and
diagnostic snapshots; see [Process registry and procfs](process-registry-and-procfs.md).
Console and UART traffic uses the normal I/O protocol to reach userspace terminal
and UART services. Kernel boot, panic, and diagnostic output remains independent
of those services and the root filesystem.

Normal executable reads also use userspace I/O and shared buffers. The kernel
retains ELF validation and process creation. Mama's `boot_spawn()` reads the
mapped archive directly during bootstrap; `boot_complete()` disables this path
once the basic mounts are established. See
[bootstrap filesystem handoff](bootstrap-filesystem-handoff.md).

To exercise local sockets from the shell:

```sh
socket_test listen &
socket_test connect
```

### FAT32 hosting preparation

The standalone C FAT32 provider and backend have been retired. Configured FAT32
volumes are served by the I/O host's native Rust `fat32.wuff`, not a C wrapper.
`user/drivers/fat32fs` contains the `hunde-fat32fs` crate: a safe `no_std` engine over the existing
`hunde::block::Device`. Each `Volume::mount(device)` owns a separate device.
It validates volume-relative FAT32 geometry, FAT capacity and active-FAT
selection, cluster bounds, reserved/free/bad links, and cyclic chains. Reads
support fragmented files, sector/cluster boundaries, arbitrary offsets, and EOF;
directory listing supports bounded pagination. Traversal uses constant-memory
cycle detection and a geometry-derived step bound.

The Rust engine supports 512-byte sectors, power-of-two clusters up to
128 sectors, one or two FATs, and ASCII 8.3 aliases with case-insensitive lookup.
VFAT long names are validated by ordinal sequence, checksum, type, padding,
and UTF-16 decoding, including sequences spanning sectors or fragmented directory
clusters. Lookup accepts both the display name and its short alias. Malformed
long names, or names whose UTF-8 representation exceeds the directory protocol's
255-byte limit, fall back to the alias. Deleted entries, volume labels, and dot
entries are skipped. OEM short-name decoding is not implemented. FAT12/16 and
nonzero FAT32 format versions are unsupported. Sector zero must be the volume
boot sector; partition-table discovery is not part of this engine.

Source-local tests use sparse disk-image fixtures with FAT32-sized geometry,
nested and chained directories, fragmented files, multiple volumes, and malformed
metadata. They also exercise device failures and assert that reading never writes.
The on-disk layout follows the
[Microsoft FAT specification](https://www.scs.stanford.edu/~zyedidia/docs/_other/fat.pdf).

#### Rust FAT32 mutations

The native engine exposes `create_file`, `create_directory`, `write`, `truncate`,
`remove_file`, and `remove_directory`. Creation accepts ASCII 8.3 names and stores
uppercase aliases. Writes preserve unaffected bytes, extend fragmented chains,
and zero newly allocated storage and gaps beyond EOF. Truncation clears length
and first-cluster fields in the existing directory slot, freeing the old chain.
Directories grow by allocating clusters; new directories contain valid dot and
parent entries. Only empty directories can be removed, and root removal is denied.
Read-only attributes are enforced. Alias removal also validates and deletes its
associated contiguous long-name slots. Creation still accepts only 8.3 names,
as did the retired C provider; existing long-name files can be read, written,
truncated, and removed.

FAT updates preserve reserved high bits, update mirrored copies or only the
selected active FAT, and reject mismatched copies before mutation. Known valid
FSInfo free counts are adjusted in the primary and backup records; unknown or
invalid counts remain unknown, and allocation does not trust these hints. Next-free
hints are invalidated. File chains are checked before mutation, including capacity
and overlap with the root or parent directory. This is not a complete filesystem
ownership/cross-link checker.

Mutations stage sector changes with fallible allocation before writing anything.
Logical validation, no-space, allocation, or staging-read errors therefore leave
the device untouched. A reported device-write failure restores every attempted
sector, including the failed write's sector in case it partially completed. If
any restoration fails, the volume returns `RecoveryRequired` and rejects further
public reads and mutations; it must not be automatically remounted as repaired.
This is runtime error rollback, not journaling, power-loss atomicity, or recovery
from external writers. The block interface does not provide a durability barrier.

Tests cover growth, truncation/reuse, directory expansion, FAT mirroring and
FSInfo, malformed metadata, and partial failures at each write boundary for
creation, growth, truncation, and removal. Native mutation probes additionally
verify remounts and filesystem consistency on disposable copies of the generated
FAT32 image, without modifying the project images.

#### Rust FAT32 request adapter

`Fat32::mount(device)` wraps the engine in the shared safe `IoComponent` contract.
Each open description owns its canonical case-insensitive path, access flags,
offset, and authenticated process/session owners. Independent opens have separate
offsets; inherited owners share a description and offset. The shared `Grants`
implementation authenticates prepare/commit/claim/abort, preserves committed
grants across source disconnect, and prevents claims from elevating access.
Close and disconnect remove owners and collect descriptions only after retained
grants are released. Disposal reports busy while descriptions or grants remain.

The adapter handles open/create/exclusive/truncate, read/write/append, seek,
metadata, paginated directories, mkdir, and file/directory removal, including
recursive removal with cycle checks and bounded batches.
Failed writes leave offsets unchanged. FAT32 has no Unix ownership metadata:
entries report root ownership, files and directories mode 0755, with write
bits removed for read-only entries. All users may read; only root may mutate,
and root does not bypass on-disk read-only attributes. Explicit executable opens
require read access, never create missing files, and preserve execution access
through inheritance. Hard links and watches are unsupported.

Removal reports busy if a live or grant-retained description refers to the file.
Directory-slot identity prevents using a long name or alias to bypass that check.
This avoids freeing/reusing directory slots under path-backed handles; POSIX
unlink-while-open is not implemented. The retired C provider also used path-backed
handles and did not provide POSIX unlink-while-open semantics.
Native adapter tests cover authorization, offsets, append, create/truncate,
permissions, metadata, inheritance, cleanup, handle exhaustion, and write errors.

#### FAT32 Wuff integration

`fat32.wuff` is packaged in `/sbin/components` alongside `ext2fs.wuff`.
Both export only `hunde_filesystem_component_entry_v1` and receive the existing
versioned host operations and host-owned block capability. The shared safe loader
accepts `BundledFilesystem::{Ext2,Fat32}`, not arbitrary library paths, and retains
the block client and component mappings for the mounted instance's lifetime.
Each format is built and checked for the same permitted relocations, RELRO,
export policy, and absence of unsupported dependencies/TLS/initializers.

Mount control accepts `device /dev/<device> fat32.wuff /<mountpoint>`. Boot mount
setup waits for host initialization before publishing the namespace mount, just
as for EXT2. The host preserves the selected driver through pending requests,
reports format-specific readiness, and runs each volume on the shared scheduler.
Reservations compare device identity independently of driver selection, so a
device cannot be mounted again merely by changing its format. Failed mounts
release their reservation; successful mounts retain it after control disconnect.

`test_qemu_hosted_fat32_wuff` exercises both architectures with a temporary boot
configuration and disposable FAT32 image at `/mnt/fat`. It covers dynamic loading,
namespace routing, creation, writes, append, truncation, inherited reads, copying
from EXT2, removal, and shutdown. Normal platform configurations are unchanged.

FAT32 acceptance and the broader root-filesystem smoke suite pass on AArch64
and RISC-V, including shutdown. The disposable FAT32 images pass filesystem
consistency checks afterward. `humpen` excludes hosted FAT32 mounts from legacy
watchdog spawning. Trace validation checks I/O readiness and rejects process
faults without depending on verbose-only kernel messages.

Integration exposed a shared loader defect, not physical-memory exhaustion:
choosing a load bias from only the first segment allowed later fixed-address
segments to overlap existing allocations. LLDB confirmed AArch64's second FAT32
segment overlapped a live dynamic allocation, correctly rejected by the kernel.
The loader now allocates the complete image span once, gives individual segments
their final permissions, and leaves gaps inaccessible. Native regression tests
exercise the fragmented-address-space case and single-owner cleanup.

The QEMU `/mnt/sd` and `/mnt/channelio` mounts and the Pi UEFI MMC FAT32
configurations now select `fat32.wuff`. The old `fat32` boot-configuration spelling
is normalized to the hosted format, without launching a legacy watchdog.
Boot setup waits for each block provider's published readiness before requesting
the hosted mount; this also covers asynchronously initialized SDHCI/MMC devices.
Readiness failure prevents component initialization and namespace publication.
The C backend, launcher, and obsolete C unit suite have been removed; the Rust
suite covers their retained functionality. Boot-archive dependencies include
every packaged Rust component, so source edits rebuild the packaged Wuff rather
than silently embedding a stale image.

## Common application interface

Write access alone preserves file contents. `IO_OPEN_TRUNCATE` is an explicit
open action requiring write access; libc translates `O_TRUNC` to that action
without deleting the path. Truncation preserves inode identity, hard links,
existing open descriptions, and file-watch continuity. Creation and exclusive
creation remain separate flags.

The host's `hund-io-system` endpoint is the common userspace I/O front end for files, sockets,
network I/O, and streams. Applications must not have a separate filesystem-only
front end. Preserve the public userspace I/O API while moving its implementation
from I/O syscalls to a protocol over the existing `Channel`.

```text
Application
    |-- route request --> hunde-io-host (hund-io-system endpoint)
    |                     `-- local namespace / host handles --> component worker
    `-- redirected Channel --> external bootstrap/device/legacy provider
                           `-- Channel --> block driver where required
```

These are protocols over the same bidirectional IPC session abstraction, not
new kinds of channel. Follow [Channels and event queues](../kernel/channels-and-event-queues.md).

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

The Rust application endpoint owns component selection, host handle identities,
inheritance-token translation, and pending-request dispatch for hosted components.
It calls the filesystem namespace locally for mount selection and path
rewriting, with one namespace-specific responsibility: directory enumeration
includes immediate child mountpoints before redirecting to the backing
filesystem. It does not manufacture file metadata. Hosted requests execute on
component workers and reply on the application's original host connection;
external providers still receive bounded redirects during migration. The retained
`hund-dateisystem` endpoint also exposes this same namespace for bootstrap mount
control and compatibility; applications no longer need a second routing exchange.

The application I/O client follows at most eight redirects for external providers.
Hosted opens return host handles directly through the application endpoint. The
client still gives callers process-local opaque handles and maps them to the
owning service's handles. The final
filesystem, socket, or stream service owns resource state, access checks, and
cleanup for its authenticated client sessions. Unsupported operations return an
explicit error.

### Process-local host I/O

The host's existing local-router registration also serves local opens and their
read/write/close operations. Local handles use the same Rust dispatcher and
component workers as application requests, but do not establish a Channel back
into the host or acquire shared-buffer leases for memory already in the process.
Local transfers borrow the caller's buffer, bounded by the normal transfer
limit, until the synchronous library call completes. External I/O continues to
use authenticated Channels and directional `hunde-puffer` buffers.

The compatibility adapter assigns one nonzero process-local session, paired with
the actual current PID. A cooperative gate serializes requests on that session
across fibres, so handles remain usable across fibres without overlapping
pending requests for the same dispatcher owner. Pending replies notify the
single host scheduler and park only the caller's fibre. The Rust router's
mutable borrow is released between polls; component workers complete the queued
work before the adapter retries. No kernel wait or external interrupt is needed
for an entirely local completion. Failure to cooperate cancels the local session
and returns an error rather than opening a self-Channel or spinning.

The current callers are native module-loader metadata queries and file reads.
Local pipe creation and watch aggregation are not added by this slice.

The Rust host owns the filesystem namespace, configured mount table, and
mount selection. Format drivers own filesystem format knowledge and open-file
state. The host maps its handles to those component-local descriptions while
preserving authenticated PID/session ownership. Socket/network services own their network-specific state.
Device drivers own device operations. Moving the front end must move real
ownership out of the kernel, not introduce a service that forwards the same
I/O syscalls back into the kernel.

Anonymous pipes are owned by Rust `pipe.wuff` inside `hunde-io-host`, exposed
through the retained `hund-rohr` Channel endpoint. There is no standalone
`hund-rohr` executable. Mama starts the host; `hundeschule` waits for endpoint
readiness before launching the login service. `hund-io-system`
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
local references do not authorize another process. Fork and exec inheritance
use prepare, child-PID-bound commit, single-use claim, and abort grants.
The child claims the same endpoint through its own authenticated Channel.
Committed grants retain endpoints across source close/disconnect until claim;
uncommitted grants are released on source close/disconnect. EOF and broken-pipe
decisions account for these retained endpoints and all authenticated owners.
Cleanup of committed grants for children that die before claiming remains open,
as for the other resource-owning services. The client handle allocator reserves
restored local identities before issuing new handles.

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

The hosted Rust EXT2 provider reuses the same C block cache through its native
per-device client, caching metadata blocks as well as data. It does not yet keep
a separate decoded-inode cache. Cache diagnostics are attributed to the I/O host;
each device client releases its cache-growth chunks on close. See
[hosted EXT2 caching](io-driver-hosts.md#hosted-ext2-caching) for ownership and
write/rollback behavior. This does not provide coherence between independently
opened writable filesystem instances.

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

## Remaining Integration

Namespace, filesystem, socket, pipe, and logical-console routing already live
in `hunde-io-host`. Hardware UART, terminal, keyboard, SDHCI, and other external
device services retain their existing protocols while driver-host consolidation
continues. See [I/O and driver hosts](io-driver-hosts.md) and [TODO](../../TODO.md).
Channel errors must never trigger an implicit kernel filesystem fallback.

Retain independent boot/panic output and boot-level-0 embedded executable loading.
Mama starts the Channel and I/O services without filesystem dependencies, then
establishes userspace `/boot` mounts before configuration is read.

Test routing, multiple handles over one session, permissions, stale and foreign
handles, close and exit cleanup, backend failure, and progress under blocked
requests. Verify boot ordering and actual file operations on all configured
mounts when changing their ownership or transport.

## Console streams

The host's Rust application router owns stream-device selection. Stream paths use
`:name:` syntax: names must be nonempty and contain no colon, slash, NUL, or
space/tab/newline characters. The built-in aliases select a backend by exact
path and access direction (read or write). Unknown names return
`IO_NO_DEVICE_FOUND`; malformed names return `IO_INVALID_PATH`, and unsupported
access returns `IO_PERMISSION_DENIED`. Malformed colon-prefixed paths fail
namespace validation rather than selecting a mounted filesystem.

Boot hardware information selects serial or framebuffer console output using
the terminal routes below. Stream drivers retain their own syntax checks. Direct
Channels are opened lazily and reused by service identifier, including across
aliases. Open handles retain final-service routing and client ownership independently
of subsequent path lookup. Selection is local to the I/O host;
there is no driver-controlled registration protocol. `:keyboard:` reads route
to the `keyboard` service using the same routing and Channel protocol.

### Configured consoles

The Rust host's `console` subsystem owns bidirectional terminal descriptions,
authenticated owners, inheritance grants, terminal attributes, foreground state,
and bounded input queues. Hardware profiles configure instances through `fstab`:

```text
device uart_rx,uart_tx console /dev/tty0
device /dev/tty0 console :console:
```

Framebuffer profiles instead use:

```text
device keyboard,video_term console /dev/vtty0
device /dev/vtty0 console :console:
```

Numbered nodes are character devices in the host filesystem namespace. Mount the
instance before its default alias. The authenticated host mount-control endpoint
constructs the component; Hundeschule publishes the namespace node only after
successful initialization. `:console:` selects an already registered instance,
not a boot-hardware-dependent redirect. Serial instances also serve the legacy
`:tty:` spelling; video instances serve legacy `:keyboard:` reads. Aliases share
the same component and terminal state. Standard descriptors and explicit
`/dev/*ttyN` opens use host-owned handles, not backend handles.

Each console has an input-pump fibre and a dispatch worker on the host's one
scheduler. The pump reads only available queue capacity, stops consuming when
the queue fills, and wakes pending reads without blocking output dispatch.
Nonblocking empty reads return `IO_WOULD_BLOCK`. Closing and disconnecting release
owners; committed inheritance grants retain descriptions until claimed or aborted.
UART and keyboard Ctrl-C handling for hosted consoles belongs to the host, which
signals the configured foreground process. Other terminal-mode processing,
controlling terminals, foreground process groups, window-size reporting, and
virtual-terminal switching remain separate work. Only one console can consume
each currently supported backend pair.

Hardware services remain external. The shared `hunde::terminal::Stream` adapter
opens the explicitly configured service through the existing authenticated
Channel client and directional managed buffers, without resolving `:console:`
back into the host. Both backends must be ready and open before the mount
completes. Framebuffer profiles start `video_term` alongside keyboard in
Hundeschule, before configured mounts; Hundeschau only waits for these services
before login. A backend failure fails initialization, not a fallback to a
different device. Kernel boot and
panic output is unchanged. Unconfigured legacy stream opens retain their previous
redirects for bootstrap and compatibility; configured console paths do not.

The hosted UART drivers
use the existing I/O protocol over Channel, with multiple owned handles per
session, direction checks, close, and session cleanup. They do not fetch or
complete console buffers through kernel I/O syscalls.

`uart_rx` owns a 1024-byte input buffer and receives UART interrupts inside
`hunde-treiber-host`. It drains the FIFO and clears device status before the host
rearms the IRQ. When the input buffer fills, it retains the pending interrupt
until reads make space. `uart_tx` handles FIFO backpressure one byte per host
poll, including newline translation, and completes the accepted write before
replying. The stream driver retains an
empty read at its backend until input arrives, so the application request remains
blocked without Channel request/reply churn or retries through routing services.

The shell assigns its foreground child to the terminal through its authenticated
input handle, then waits for that child explicitly. While a foreground process is
assigned, the hosted console consumes the Ctrl-C byte and sends that process
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

## Bootstrap component configuration

`config/io-host.conf` is embedded as `/sbin/config/io-host.conf`. Each non-comment
line has three fields:

```text
service socket_router /sbin/components/socket.wuff
filesystem ext2.wuff /sbin/components/ext2fs.wuff
filesystem boot-config /sbin/components/ext2fs.wuff
```

`service` initializes a resident I/O Wuff and registers its configured endpoint.
`filesystem` registers a block-filesystem factory by format
name, without mounting a device or initializing an instance. Existing fstab
entries select these format names, devices, and mount points. Console remains
host infrastructure, not a block-filesystem format. Initial filesystem instances
belong in `config/bootstrap.fstab`, also embedded under `/sbin/config`:

```text
device - /sbin/components/embedfs.wuff /sbin
device - /sbin/components/devfs.wuff /dev
```

The host reads both files directly from the mapped archive, not through a mounted
provider. It loads the initial Wuffs, registers their dispatch workers and
namespace mounts, then publishes mount-control readiness. Native backend Wuffs
resolve their libc dependency from the archive's `lib/` directory. No standalone
embedfs process is needed, and no builtin driver branch remains.

The host validates the complete configuration before loading components, then
uses one generic worker collection for registration, dispatch, polling, and cache
reclamation. Filesystem aliases are resolved against the configured registry,
not a per-format Rust enum. The namespace, application front door, and mount
control remain fixed infrastructure. Component images remain resident.

The `hunde` build generates approved image identities and their service/filesystem
ABI kinds from both manifests. Runtime configuration cannot make an arbitrary
path safe to load: only generated identities are accepted by the shared loader,
and ABI descriptors are checked before use. Services remain free of unsafe code.
To add a component, package its trusted Wuff, add the manifest entry, and rebuild
the image; no per-component startup or polling edits are required. This manifest
is bootstrap configuration, not a runtime hot-reload or unload interface.

## Socket protocol

Socket requests are dispatched by `hunde-io-host` to its loaded Rust
`socket.wuff` component. The host retains the `socket_router` endpoint for
existing clients; the standalone C server, its binary, and its configuration
entries have been removed. All socket buffers, connection pairing,
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
and channel server loop; `user/services/hunde-io-host/src/host/router.rs` selects
routes; socket state lives in `user/drivers/socket/src`. Tests cover routing, the
real channel client/server path, ownership, backpressure, EOF, and teardown.

## Boot filesystem namespace

The filesystem control path is application -> host application router -> local
filesystem namespace -> final mount redirect. The application then talks
directly to that final filesystem service; its block operations use a separate
Channel to the block driver. Cross-process transfers use existing Channels;
application-to-namespace routing inside the host is a function call, not IPC.
Routing translates paths, not handles.

Mama starts `hunde-io-host` with an empty filesystem namespace, then establishes `/sbin`,
`/boot`, and `/boot/config`. Hundeschule subsequently reads `/boot/config/fstab`
through Channels and registers the configured FAT32/EXT2 mounts. A hosted root
uses the host's mount worker; a legacy root starts `humpen-wachhund`.
It waits for root readiness before starting Hundeschau. Missing
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

Limits belong to their owning layers. The host dispatcher admits up to 64
requests and 512 handles (`host/dispatch.rs`); native Rust EXT2 and FAT32 each
limit open descriptions and owners to 64. Transport buffers are 2 MiB and the
client combines chunks into the requested transfer. Disk reads are streamed;
snapshot limits apply to components such as procfs rather than every filesystem.
Native Rust disk components support seek, mutation, and authenticated descriptor
inheritance. EXT2 also implements hard links and Unix owner/group/other
authorization; FAT32 uses synthesized permissions with root-only mutation.
Service recovery and full POSIX semantics remain unfinished.

All application file operations now use Channels, including `link_file`. A link
request carries both paths; the namespace resolves both mounts, rejects links
across mounts, and forwards mount-relative paths to the same provider.
EXT2 implements hard links; providers without that capability return
`IO_OPERATION_NOT_SUPPORTED`. The application library never retries file
operations through kernel I/O syscalls, including unmanaged-path replies.
Directory listing includes namespace mount entries without a kernel merge.
Legacy kernel handles are rejected; standard streams still lazily open their
configured userspace stream endpoints.

Normal executable loading uses kernel -> Channel -> `hund-io-system` -> namespace
-> filesystem provider. Only Mama's `boot_spawn()` can read the embedded archive
directly, and only before the irreversible `boot_complete()` handoff. Normal
spawn has no embedded or old filesystem fallback. The loader copies validated
shared-buffer ranges into a kernel-owned image, bounded to 64 MiB, then validates
the ELF and interpreter contract before mapping (at most 128 MiB of segment memory). Disk providers
stream executable reads without the pseudo-filesystem snapshot limit. EXT2
requires at least one execute mode bit before accepting an executable open;
FAT32 applies synthesized executable permissions. Native Rust EXT2 additionally
checks authenticated caller credentials and directory traversal permissions.
Embedded files do not
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
QEMU PCI SDHCI runs as a read-only Rust `sdhci.wuff` inside the driver host,
using local IRQ delivery for transfers and the existing Channel block protocol.
The standalone C SDHCI executable is retired. Supported SD boot configurations
use the Rust Wuff in the driver host; physical Pi EMMC2 is not supported.
SD write support is not implemented.
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

The bootstrap fstab loads `embedfs.wuff` into the host and registers `/sbin`.
There is no standalone archive process or handoff.
Open, read, close, and
directory listing use the existing Channel-based I/O path. The kernel's embedded
filesystem implementation and its obsolete test fixture have been deleted.
See [Bootstrap filesystem handoff](bootstrap-filesystem-handoff.md) for archive
mapping, permissions, resource ownership, and executable-loader
boundary.

### Process Filesystem

`hunde-io-host` loads Rust `/sbin/components/procfs.wuff` and publishes `/proc`.
The bootstrap fstab registers the mount before mount-control readiness;
Hundeschule does not remount it or spawn a standalone procfs process. The existing bidirectional Channel protocol,
namespace redirects, and managed transfer buffers remain unchanged. The kernel
publishes read-only shared process records; procfs formats coherent snapshots
through the shared `hunde` boundary. `ps` independently uses `process_list`.
See [Process registry and procfs](process-registry-and-procfs.md)
for CPU, wait-state, memory-accounting, consistency, and ownership semantics.

## Device filesystem

The bootstrap fstab loads `devfs.wuff` and registers `/dev` before mount-control
readiness. There is no standalone devfs
executable. Application handles use the common host dispatcher and the existing
filesystem Channel protocol and directional buffers.

The safe `hunde::devfs::Filesystem` adapter owns the existing C
`FilesystemDriver` and live registry-backed implementation in
`user/lib/fs/devfs.c`. This is an in-process C backend, not a native Rust rewrite
but runs behind `devfs.wuff`, with its native backend supplied by the
archive-backed `libc.wuff.1` dependency. The shared native filesystem codec is also used by embedfs;
it bounds caller-owned buffers, translates metadata and paginated listings,
and preserves inheritance tokens, access flags and shared offsets. The adapter
retains each authenticated session's PID so disconnect releases its C backend
owners without guessing an identity. Committed grants survive source disconnect.
No unsafe code is added to the Rust service.

Devfs describes `/` as a `0755` directory before listing. This fixes the old
root-metadata error that made `ls /dev` report `ENOENT` before enumeration.
Configured `/dev/ttyN` and `/dev/vttyN` console mounts appear alongside registry
entries in namespace listings. There is no kernel devfs mount or blanket `/dev`
syscall bypass.

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
departed owner's records after device quiescence. Reads through an existing
handle still consult the live registry and reject removed devices; metadata
is not a grant to access a device that has since exited.

Devfs lists device paths and `hdN` aliases and exposes block metadata as read-only
files. Registry aliases are stored as absolute `/dev/hdN` paths, including those
generated during block registration, so lookup and enumeration use the same
representation. Mutation and executable opens are rejected. No kernel stream devices
remain registered at boot: `/dev/businfo` moved to the `hunde-bus` Channel
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

The Rust I/O host loads `/sbin/components/memfs.wuff` and publishes the retained
`/tmp` endpoint. Hundeschule waits for its readiness and installs the deviceless
`memfs` mount before launching Hundeschau; it no longer spawns a memfs process.
A conflicting `/tmp` fstab entry or failed startup stops that sequence.
No disk configuration entry is required.

Temporary-file operations use application -> `hund-io-system` on the Rust I/O
host, which selects the mount and dispatches to memfs locally. Handles stay on
that application-to-host Channel; no separate namespace or memfs process is
involved.
The former kernel `/tmp/memfs` mount and syscall bypass have been removed.
The old implementation exists only as a kernel-test fixture and is not linked
into the kernel.

Rust memfs owns dynamically allocated file contents and a collection of files
and directories, with no 12 KiB disk-snapshot limit or fixed file-count table.
It supports 32 open descriptions, independent offsets between separate opens, immediate empty-file creation
and write-only-open truncation, directory listing, mkdir, file removal, and
recursive directory removal. Read/write opens preserve existing data. Operations
use shared directional buffers, up to 2 MiB per request. Allocation failures return errors
without advancing the handle offset or discarding existing data.

The backend receives authenticated client PID/session identities from its direct
Channel. Close and client release dispose of handles, not files; a client
disconnect releases only that session's handles and preserves files and other
clients' handles.
Fork and exec use the shared `hunde::inheritance` grant registry, also used by
pipes. Claims are authenticated, PID-bound, single-use, and preserve the same
open description and offset. Closing one owner does not close another's handle.
Committed grants retain descriptions across source close/disconnect; uncommitted
grants are discarded. Orphan cleanup for children that die before claiming
remains deferred. Removing a node invalidates its descriptions and grants.
Removal frees storage and invalidates handles to the removed node; recreating
the same path cannot revive them. Nonrecursive directory removal remains
unsupported, matching the previous memfs behavior. Contents are volatile and
are lost when the I/O host exits. Service restart recovery and reliable exit
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
