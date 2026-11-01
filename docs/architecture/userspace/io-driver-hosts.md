# I/O and driver hosts

## Status and target

This document defines the agreed consolidation of normal I/O execution into two
trusted userspace processes:

- `hunde-io-host` owns application-facing I/O policy and high-level components.
- `hunde-treiber-host` owns hardware-facing mechanisms and device drivers.

The target is partially implemented. The Rust I/O host owns the application
router, filesystem namespace, console aggregation, and hosted socket, pipe,
memfs, procfs, sysinfofs, EXT2 and FAT32 components. Embedfs and devfs use safe
Rust adapters over shared C backends inside the host. The Rust
`hunde-treiber-host` replaces the C `hund-unterbrict` executable and hosts
native Rust `virtio_blk.wuff`, `virtio_keyboard.wuff`, `uart_rx.wuff`, `uart_tx.wuff`, and
QEMU PCI and raspi4b `sdhci.wuff` components. VirtIO block, VirtIO keyboard,
UART receive, and both QEMU SDHCI paths use local IRQ dispatch; UART transmit is polled. Other
hardware terminal/device services remain external.
Migration must preserve working service protocols until their ownership moves
into a host.

This is consolidation of I/O execution, not consolidation of every userspace
service. `hunde-mama`, `hundekanal-warter`, `hunde-puffer`, tracing, and other
system coordination services retain their existing responsibilities. Kernel
boot, panic, and diagnostic output remains independent of both hosts.

## Topology

```text
applications
    |
    | authenticated Channel
    v
hunde-io-host
    namespace, mounts, handles, filesystems, sockets, streams, caches
    |
    | authenticated Channel and managed shared buffers
    v
hunde-treiber-host
    IRQ routing, MMIO, DMA, block, network, input, and console devices
    |
    v
hardware
```

An application operation crosses into `hunde-io-host` once. High-level routing
and protocol composition within that host use local calls, not Channels between
modules. An operation that reaches hardware crosses once more into
`hunde-treiber-host`. Replies follow the same two process boundaries. Cache hits
and operations implemented entirely by the I/O host do not enter the driver
host.

Existing routing redirects occur mainly during resource open, and current file
data already travels directly between applications, filesystem services, and
block drivers. The consolidation must therefore be evaluated with measurements
rather than assuming every path loses a process boundary. Record open latency,
cache-hit latency, cache-miss latency, Channel publications, scheduling
transitions, and representative network paths before and after migration.

## `hunde-io-host` responsibilities

`hunde-io-host` owns:

- the common application-facing I/O endpoint and authenticated session state;
- namespaces, mount routing, paths, open resources, and handle ownership;
- filesystem, socket, stream, pipe, terminal, and system-information components;
- filesystem metadata and data caches, network protocol state, and any shared
  raw-block cache introduced for current callers; and
- coordination of buffer leases and outstanding high-level operations.

Filesystem and protocol components run in the host address space and receive a
host-created request context derived from authenticated Channel metadata. A
module-provided PID, session, or handle is never authoritative. Local dispatch
must retain the existing ownership, access, stale-handle, cleanup, and progress
rules described in [Userspace I/O system](io-system.md).

Caching remains I/O policy. The driver host may retain device queue state and
hardware-specific metadata, but it does not own a general filesystem or block
cache. A shared raw-block cache, if required by measured callers, belongs in the
I/O host so write ordering, invalidation, flush, discard, and filesystem
coherence remain above the hardware boundary.

## `hunde-treiber-host` responsibilities

`hunde-treiber-host` evolves from `hund-unterbrict` and owns:

- delegated device IRQ receipt, dispatch, completion validation, and rearming;
- device discovery, claims, MMIO mappings, DMA rings, and hardware reset;
- block, network-interface, input, UART, display, and other hardware drivers;
- per-device fibres and progress while other devices or clients are blocked; and
- fail-closed cleanup of device claims and IRQ routes if the host exits.

Hosted IRQ delivery and acknowledgement use local calls as already implemented
for VirtIO block. Hardware drivers do not use a Channel to communicate with an
interrupt component in the same host. Device interrupt sources are still
cleared before the host rearms them, and kernel-owned interrupt entry, masking,
authentication, and privileged controller operations remain in the kernel.

The driver host exposes stable device operations to the I/O host over existing
Channel protocols. It contains hardware mechanism, not filesystem, socket, or
application policy.

## Buffers and data movement

Bulk bytes continue to use directional leases managed by `hunde-puffer`.
Channels carry requests, descriptors, commands, and completions. Host
consolidation must not introduce private message rings, inline bulk payloads, or
unmanaged shared mappings.

The I/O and driver hosts receive only the attachments and permissions required
for the current operation. When an application buffer can safely participate in
the complete path, the driver host may perform DMA against that managed lease.
Transforming components may require a separate lease or copy; the architecture
does not promise universal zero-copy operation. Completion, cancellation, and
DMA quiescence must precede lease release or recycling.

See [Shared buffers: hunde-puffer](shared-buffers.md).

## Components and failure domains

Dynamically loaded components share all memory and authority of their host.
Language memory safety reduces accidental corruption but is not a security
boundary. A component can still deadlock the event loop, panic, misuse an
authorized hardware operation, or leave DMA active. The two processes are the
isolation and failure boundaries; components inside a host are trusted.

Host-facing component interfaces must:

- use a versioned C ABI and fixed-width, explicitly laid-out values;
- receive host operations explicitly rather than resolving private host state;
- keep language-specific objects and ABIs behind the module boundary;
- support asynchronous completion without blocking the whole host; and
- make resource ownership and cleanup explicit.

The loading and language contract is defined in
[Dynamic linking and host modules](dynamic-linking.md).

## Migration

The following component sections retain the staged implementation details.
Early slice descriptions and ABI-version introductions are historical context;
the status above and the current dispatch section describe the running topology.
The current I/O component ABI is v6; lifecycle components use ABI v1.

### Rust driver host: VirtIO block

`user/services/hunde-treiber-host` supplies the Rust executable entry point.
Rust owns startup, reverse-slot device enumeration and `/dev/virtioN` naming,
device lifetimes, authenticated external IRQ registrations, generation and
sequence validation, pending replies, local IRQ dispatch, and the polling/wait
loop. Bootstrap launches `/sbin/services/hunde-treiber-host`; external drivers
retain the `hund-unterbrict` Channel endpoint and wire protocol.

`user/lib/hunde/driver.c` and `hunde::driver` adapt native Channel,
notification, and interrupt-controller operations. The host crate forbids unsafe
Rust. Each `virtio_blk.wuff` instance owns its device registers, DMA queue,
transfer state, and acknowledgement. Shared `hunde` MMIO/DMA wrappers and the
native block-server/fibre adapter provide the unsafe and C ABI boundary.
The standalone C block engine and Hundeschule's device selection fallback are
removed; the host publishes each block endpoint before reporting readiness.

Loaded driver modules and native runtime storage are retained for the process
lifetime: there is no safe unload or DMA-quiescence operation. A startup or
device scheduler error exits
the host. External reply backpressure retains the exact response and prevents
another request from replacing it. Dead-client registration cleanup and host
crash/DMA recovery remain unresolved; this slice does not enable safe automatic
restart. The recorded RISC-V interrupt-manager crash remains under investigation.

Source-local Rust tests cover authentication routing, hosted ownership, stale
and duplicate acknowledgements, denied registration, failed rearming, local
device dispatch, and reply retention. Native adapter tests cover authenticated
PID access, wire responses, notification timestamps, and controller validation.
`make rust-drivers` builds the selected target's executable.

### Hosted UART receive

On serial-console targets the driver host loads the typed boot UART description,
routes the UART IRQ to itself, and loads the approved `uart_rx.wuff` from the
mapped archive. The Wuff maps UART registers and serves the existing `uart_rx`
stream endpoint. Its Rust receiver retains the bounded input buffer,
foreground Ctrl-C signaling, and stream protocol. Safe MMIO access and the
versioned driver component entry live in the shared `hunde` library; the Rust
host invokes the entry's `step` and `deliver` callbacks and
rearms the IRQ only after the Wuff reports that it cleared the device source.
The standalone C executable has been removed. Older driver config rows naming
it are ignored by Hundeschule.

The same host loads `uart_tx.wuff` against the boot UART description. Its
`uart_tx` endpoint preserves terminal stream writes and LF-to-CR output, but
advances a pending write at most one register byte per poll so it cannot block
the receive or block-driver loop. It does not claim an IRQ. The standalone C
transmitter has also been removed.

When a VirtIO input device is present, the driver host probes its
MMIO slot and loads `virtio_keyboard.wuff` from the mapped archive. The Rust Wuff
owns the DMA event ring, translates key events, and serves the existing `keyboard`
read-stream endpoint. It holds an IRQ delivery while its bounded input queue is
full, then clears the device source before the host rearms the IRQ. The standalone
C executable has been retired.
The kernel's VirtIO claim check accepts block and input device IDs and tracks
both for reset before host memory is released.

The typed boot description avoids a dependency on `/boot/sysinfo` while the
driver host starts. The Wuff image and receiver storage remain resident for the
host lifetime because the current IOService/IRQ adapter has no safe unload path.

On QEMU's PCI SD host, AArch64 boot discovery supplies the BAR and routes PCI
INTx to the GIC IRQ described in the typed SD-host snapshot. The driver host
loads `sdhci.wuff`, publishes `/dev/mmc0`, and locally dispatches its IRQ. The
Rust Wuff retains read-only PIO and the existing block Channel protocol. Card
initialization uses bounded polling before IRQ signalling is enabled; transfers
wait for command, buffer-ready, and completion events, clear device status before
rearm, and use timed wakeups for missing-IRQ recovery. QEMU acceptance reads the
FAT32 SD card and checks an actual routed IRQ. QEMU raspi4b uses its legacy
BCM2835 SDHCI controller at `0xfe300000`, with GIC ID 158; mama starts the
partition services after the hosted `/dev/mmc0` becomes ready. Its boot smoke
test reaches login, reads `/boot/config`, and checks IRQ delivery. The physical
Duo S AArch64 GIC path selects `sdhci.wuff` with CV181x vendor setup. On the
board, the hosted `/dev/mmc0` reports the card capacity, partition two supplies
`/boot/config/fstab`, partition three supplies the ext2 root and an interactive
Bash session, and `/proc/ktrace` records `Rust SDHCI IRQ delivered`. `ps` shows
the Rust driver host and partition services without the standalone C SDHCI
process. The RISC-V CV181x PLIC path uses the same Rust Wuff and passes physical
boot, ext2 read, and IRQ-delivery checks. The standalone C SDHCI driver is
retired; physical Pi EMMC2 remains unsupported.

### First Rust socket slice

`/sbin/services/hunde-io-host` has a Rust `no_std` `main` entry point in
`user/services/hunde-io-host/src/lib.rs`, linked with the existing C CRT. Rust owns service
startup, the fibre scheduler loop, loaded component lifetimes, endpoint
registration, readiness publication, callback dispatch, and Future polling.
There is no host-specific C launcher or dispatcher. During startup it loads
`/sbin/components/socket.wuff` through the
existing filesystem Wuff loader and resolves `hunde_io_component_entry_v6`.
The trusted Rust module owns socket names, listeners, connection handles,
bounded byte queues, permissions, backpressure, EOF, and session cleanup.
It remains resident until the host exits; runtime unloading is not supported.

### Hosted Rust pipes

The same Rust host loads `/sbin/components/pipe.wuff`, implemented under
`user/services/hund-rohr`, and publishes the retained `hund-rohr` endpoint.
Both endpoints are polled in one fibre; neither service can monopolize progress
by draining its own work before the other is polled. The standalone pipe process
and C implementation are removed. Pipe creation still routes through
`hund-io-system`; subsequent I/O uses the client's direct Channel to the host.

The component preserves the 64 KiB bounded ring, 4096-byte atomic-write limit,
nonblocking errors, EOF, broken-pipe errors, and directional managed buffers.
Authenticated fork/exec grants retain endpoint ownership across fresh child
sessions without exposing globally usable handles. See
[Userspace I/O system](io-system.md).

I/O component ABI version 2 adds buffered creation metadata, inheritance tokens
and target PIDs, paired pipe handles, and inheritance-rights replies. Typed Rust
`Reply` values are flattened only in `hunde`; no Rust enum or collection
crosses the C ABI. The entry point and host/descriptor versions all advance to
v2 for that slice. Filesystem support added v3; procfs advances the current ABI to v4.
Lifecycle smoke remains ABI v1.

### Hosted Rust memfs

`user/drivers/memfs` implements safe Rust `memfs.wuff`, loaded by the same
host and polled alongside pipes and sockets in the existing fibre. It owns
`/tmp` contents, metadata, permissions, open descriptions, offsets, and
authenticated inheritance. The old C implementation and standalone executable
are removed; mount discovery and the external `/tmp` Channel endpoint remain.

ABI v3 adds filesystem path operations, seek offsets, file metadata and bounded
directory batches, plus a host callback for kernel credential snapshots.
The shared `hunde` crate validates/translates the C boundary; service code
contains no unsafe Rust. Credentials come from the authenticated request PID,
not caller-supplied UID/GID fields. Existing mode and sticky-root checks remain.
Pipes and memfs reuse one Rust grant registry. Memfs grants share descriptions
and offsets rather than reopening paths, supporting redirected external commands.

### Hosted Rust procfs

`user/drivers/procfs` implements safe Rust `procfs.wuff`, loaded and polled
alongside sockets, pipes, and memfs. It owns process-file formatting, read-only
metadata, snapshot handles, offsets, and authenticated inheritance. Hundeschule
waits for the hosted `/proc` service instead of launching a driver process.
Namespace routing and `ps` remain unchanged.

I/O ABI v4 adds process-accounting and kernel-trace callbacks to the host table.
Only `hunde` translates native registry records into checked, pointer-free ABI
snapshots. Services never access raw shared mappings or use unsafe Rust.
I/O ABI v5 added a dedicated file-change completion record without changing
the v4 request fields. The current ABI is v6: it adds a checked boot-information
snapshot callback to the host table for the cached Rust `sysinfofs.wuff`.
The resident components all export `hunde_io_component_entry_v6`; older I/O
descriptors are rejected rather than interpreting a different host layout.
See [Process registry and procfs](process-registry-and-procfs.md).

The single shared Rust compatibility crate is `user/lib/hunde`. It owns C ABI
layouts/imports, request and Result translation, typed component callback
adapters, runtime setup, and the existing service/Wuff/fibre bridge. The socket
component implements the safe `IoComponent` trait and registers through
`hunde::io_component!`; the host uses `hunde::executable!`. The lifecycle smoke also
uses this crate, with allocation disabled. Consumers contain domain behavior,
not independent copies of unsafe C translations. Each image still has its own
host binding and allocator state; no Rust objects cross the C ABI.

Services, drivers, and component crates inherit the workspace's forbidden-unsafe
lint for all targets and tests. Only `hunde` contains unsafe Rust and defines the
ABI/bootstrap macros. The host resolves manifest entries with
`BundledComponent::find` or `BundledFilesystem::find`, then uses the safe loader.
The library's registry is generated from both bootstrap manifests and records
approved image paths and ABI kinds. Image bytes come from the mapped immutable
archive. Arbitrary native loading stays private and unsafe; descriptor checks
alone do not establish arbitrary callbacks' memory safety.
See [Rust userspace](rust-userspace.md#unsafe-boundary).

The versioned layout in `common_includes/hunde/io_component.h` passes host
allocation/free/fatal operations, authenticated request identity, borrowed
buffer spans, and tagged success/error/pending completions. No Rust collection,
Future, reference, or language-specific Result crosses the ABI. Initialization
checks the host table; the host checks the returned descriptor before serving.
Allocation uses HundeOS libc's aligned allocator. Panics terminate the host;
there is no unwinding, TLS, OS thread support, or garbage collector.

Shared C adapters under `user/lib/hunde` translate the existing library layouts
and protocol records only. Rust allocates stable, correctly aligned storage for
the opaque C values using C-reported layouts. The shared library's approved
bundle registry resolves image paths and exports; subsystems choose components,
service names, and readiness metadata. Rust resolves and validates
the component descriptor and forwards callbacks to it. A service borrows its
loaded component exclusively, preventing the component from moving or being
released while the service can dispatch to it. On teardown, component disposal
precedes unmapping the Wuff graph; failed cleanup terminates the process.
The current transport supports process-lifetime services, not general dynamic
service replacement. The shared scheduler now supports multiple fibres; see
[one scheduler, multiple fibres](rust-userspace.md#one-scheduler-multiple-fibres).
The Rust host is organized into directory modules for `host`, `filesystem`,
and `console`. Socket and pipe services are configured components rather than
dedicated host modules. A shared subsystem contract reuses typed `IoComponent`
dispatch; the Channel adapter now calls the subsystem rather than bypassing it
to reach a Wuff directly. See [host subsystems](rust-userspace.md#host-subsystems).

Existing C `IOService` retains protected Channel establishment, authenticated
PID/session metadata, directional `hunde-puffer` attachments, pending requests,
and disconnect notifications. The adapter borrows each request's receiver-local
buffer only for the module callback. Pending callbacks retain socket state, not
borrowed pointers. Queues remain the implementation of existing Channels, not a
new parallel service protocol or public raw-ring transport.

The Rust host polls this service as a Future inside an existing stackful fibre.
When it becomes pending, the executor parks that fibre; the outer scheduler
waits for the Channel queue event and resumes parked Rust fibres to poll again. Synchronous
Future wakeups avoid sleeping, and the existing queue generation protocol closes
the poll-to-sleep race. Spawned work shares this one scheduler and exposes
joinable task results; completion wakes parked joiners without needing a Channel
event. This is not a general-purpose Rust runtime: arbitrary external-thread Wakers, timers, and a broad
Rust Channel client API are not implemented by this slice.

In the original socket slice, `hundeschule` started this host instead of the
standalone C socket router, retaining the `socket_router` endpoint and the C
application router. The later [application router migration](#application-router-ownership)
moves that selection into the host and Mama starts the host during bootstrap.

Rust sources and tests live in the Cargo workspace under `user`; see its
[test conventions](rust-userspace.md). Unit tests use source-local
`#[cfg(test)] mod tests`; integration tests belong in each crate's `tests/`.
`make rust-test` runs native tests and is included in `make test`. Native tests
cover safe socket behavior and Future/fibre scheduling with small fakes; C
adapter tests cover identity, bounded borrowed buffers, and tagged completions.
Cross-target builds and QEMU socket clients verify the actual loaded ABI.

`make ARCH=aarch64 rust-io` and `make ARCH=riscv64 rust-io` build the service and
module with the pinned stable toolchain. The module uses a `no_main` link target
and full LTO so bundled `core`/`alloc` are regenerated as PIC, rather than linking
their statically compiled bare-metal archive objects into a shared library.
ELF checks reject text relocations, dependencies, TLS, constructors, unsupported
relocations, and unexpected exports, and require RELRO.

### Rust EXT2 migration: volume-reader slice

`user/drivers/ext2fs` now also contains the safe Rust `hunde-ext2fs` crate.
This is a new filesystem implementation, not a wrapper around the C EXT2
implementation. The existing C executable remains responsible for every active
EXT2 mount, including rootfs and `/boot/config`; no Rust EXT2 Wuff or endpoint
is installed by this slice.

The first slice mounts 1 KiB-block EXT2 volumes, checks superblock and inode
geometry and supported feature bits, reads inode metadata, resolves paths,
enumerates directories, and reads direct, single-indirect, double-indirect, and
sparse file blocks. Journaled volumes and unsupported layouts fail explicitly.
It uses the same 32-group and direct/single/double-indirection limits as the
existing implementation. Existing-file mutation is now implemented below;
file/directory creation, removal, and hard links are implemented below. Remaining
metadata operations, and caching remain unimplemented.

The shared `hunde::block` module defines a typed `Device` interface and an owned
`Client` adapter. The adapter keeps native C storage private, validates sector
alignment and device bounds, rejects short completions, and closes on drop;
explicit close reports cleanup errors. Its C boundary reuses `BlockClient`,
including existing bidirectional Channels and `hunde-puffer` buffers. There is
no second bulk-data transport. These are native executable adapters, not new
callbacks in I/O component ABI v4. The separate versioned block capability
described below now supplies block access to filesystem Wuffs.

Source-local tests use in-memory devices, including malformed geometry,
directory records, out-of-range pointers, sparse files, and multiple block
groups. `tests/image.rs` builds and reads a real image with a 320 KiB file,
exercising double indirection, writes, sparse growth, and a fresh remount. It
requires `genext2fs` or `mke2fs` on PATH.
C boundary tests live in `test/user/lib/hunde_block`.

The reader/native-boundary slice passed the Rust workspace, five native
block-boundary tests, 23 existing C EXT2 regressions, cross-target Clippy, and
hosted-I/O full-system smoke on AArch64 and RISC-V. Mutation validation uses
the tests below. Standard QEMU disk mounts now use the hosted Rust EXT2
component; the reader slice's original validation was only the first stage.

#### Existing-file mutation

`Volume::write(inode, offset, bytes)` overwrites or grows an existing regular
file without truncating it. It updates direct, single-indirect, and
double-indirect pointers, allocates zero-filled data/pointer blocks from the
correct group's bitmap, updates primary superblock/group free-block counts,
and counts both data and pointer blocks in the inode's 512-byte sector count.
Unwritten gaps read as zero, including old EOF tails and preallocated blocks.
Allocation protects bitmap blocks, inode tables, primary/backup superblocks,
group descriptor blocks, and reserved GDT blocks. Unsupported inode flags,
fragment sizes, and writable feature layouts fail before disk writes.

Each operation stages changed blocks and their original bytes in a temporary
`BTreeMap`. Read, validation, or allocation failures during staging do not
write anything. A failed commit attempts to restore every attempted block,
including the failing block because a device may have written part of it.
If any restore fails, the volume returns `RecoveryRequired` and rejects
subsequent reads and writes. Successful rollback preserves the original
device error and permits retry. The temporary memory cost is proportional to
the number of changed blocks; this is not a persistent block cache.

This is synchronous I/O-error rollback, not an on-disk transaction log:
power loss or process exit during commit cannot be rolled back by this
in-memory mechanism. Journaling, durable crash recovery, mount dirty-state
management, and device flush semantics are not provided by this slice.

Source-local mutation tests cover boundary crossings, the last supported
double-indirect slot, multi-group allocation, preallocated data, space
exhaustion, corrupt metadata, unsupported layouts, every staging-read and
commit-write failure position, partial device writes, retry, and failed
rollback. The generated-image test checks persistence after remount and
independently verifies the primary free-block counter.
The mutation slice passes all 83 Rust workspace tests, native and cross-target
Clippy, and release-library builds for AArch64 and RISC-V. Active mounts and
bootstrap are unchanged; no new full-system Rust EXT2 acceptance is claimed.

#### File and directory creation

`Volume::create_file(parent, name, permissions, user, group)` creates an empty
regular file; `Volume::mkdir(...)` creates a directory containing `.` and `..`.
Both return the new inode. These are low-level volume operations: callers supply
the parent inode, a single byte-string name, permission bits, and 16-bit owner
IDs. They do not perform access checks, umask/default-ACL policy, or timestamp
management. Those remain part of service/metadata parity before rootfs migration.

Creation allocates a non-reserved inode from a group bitmap, clears the whole
inode record, and updates primary superblock/group free-inode counts. `mkdir`
also increments the group's used-directory count and the parent's link count,
rejecting link-count overflow. Existing directory record slack or deleted
records are reused; full directories grow through the existing block allocator
and indirect-pointer machinery. Legacy 16-bit name-length records and typed
directory entries are supported. Invalid names, duplicate names, unsupported
parent flags, corrupt records, and exhausted space return explicit errors.

The new inode, directory data, parent metadata, allocation bitmaps, and counters
share one staged mutation. The same synchronous rollback and fail-closed rules
as existing-file writes apply, not crash durability. Tests cover group-boundary
allocation, zeroing stale inode bytes, legacy records, 255-byte names, directory
growth into single indirection, allocation exhaustion, every staging-read and
commit-write failure with and without directory growth, partial writes, retry,
and failed rollback. The generated-image test creates a nested directory and
files, writes a new file, remounts, and checks data, ownership, link counts,
free-inode/free-block counts, and used-directory accounting.

Rust EXT2 now runs as a loaded I/O-host component, with QEMU root integration
described below. Bootstrap filesystem providers remain unchanged.

#### Removal and reclamation

`Volume::unlink(parent, name)` removes a regular-file name. Existing files with
multiple links retain their inode and data until the final link is removed.
`Volume::rmdir(parent, name)` removes only an empty directory, validates `.` and
`..`, and decrements the parent's link count and the inode group's used-directory
count. Neither operation accepts `.` or `..`; unlinking directories is rejected.
Directory records become reusable free slots. Parent directory blocks are not
compacted or shrunk, including indirect allocations retained after removals.

Final reclamation traverses all direct, single-indirect, and double-indirect
allocations, including sparse/preallocated layouts, and releases data and
pointer blocks through their owning group bitmaps and primary free-block
counters. It clears the inode record and allocation bit, restoring primary
free-inode counts. Reallocation still zeroes data before exposing it; removal
does not securely erase old file contents on disk.

Before reclamation the engine validates allocation bits, sector accounting,
duplicate/cyclic block pointers, metadata protection, and disjointness from the
parent directory's blocks. Unsupported inode flags, triple indirection,
external attribute blocks, and fragment addresses are rejected rather than
silently leaked or freed. This is not a whole-volume consistency checker:
cross-file block ownership corruption still requires filesystem repair.

Namespace removal, inode/link changes, bitmaps, and counters are committed in
one staged mutation with the existing synchronous rollback rules, not crash
durability. Source-local tests cover existing hard links, legacy directories,
multi-group block/inode reclamation, large inode records, indirect parent
directories, corrupt pointers/accounting, every staging-read/commit-write
failure, partial writes, retry, and failed rollback. The real-image test removes
new and existing files (including double-indirect data), removes a directory,
remounts, and independently checks freed allocation counts and cleared inodes.

Final unlink now retains inodes pinned by the volume's open descriptions as
described below. Without an open description it still reclaims immediately.
Permissions, timestamps, crash recovery, and authenticated service integration
remain incomplete. Active rootfs and bootstrap mounts continue using C EXT2.

#### Hard-link creation

`Volume::link(parent, name, target)` adds a regular-file directory entry pointing
to an existing inode on the same volume. Directory insertion, any parent growth,
and the target link-count increment share one staged mutation and rollback.
No inode or file-data allocation is needed; a full directory may need additional
directory/pointer blocks. Linking into existing space works without free blocks
or inodes. The operation rejects directory links, duplicate or invalid names,
zero/overflowed link counts, reserved/unallocated targets, unsupported inode
layouts/flags, and the allocation/pointer corruption checked during removal.

Both names resolve to the same inode and share writes. Unlinking one name keeps
the inode/data until its final link is removed. Source-local tests cover shared
metadata/data, legacy records, growth, space exhaustion, overflow, every staging
read and partial commit-write failure, retry, and failed rollback. The real-image
test creates a cross-directory link, remounts, checks the on-disk link count,
removes the original name, remounts again, reads through the surviving alias,
and verifies reclamation after final unlink. These are still low-level volume
operations, not access-control or service integration.

#### Open files and deferred reclamation

`Volume::open_file(owner, path, access)` returns a `hunde::io::Handle` for a
regular file. `owner` reuses `hunde::inheritance::Owner` (session and process),
not another ABI record. `read_file`, `write_file`, `seek_file`, and `close_file`
require that exact owner. `Access` provides read-only, write-only, and read-write
capabilities; attempts to use an excluded operation fail. Each open has an
independent offset; seeking sets an absolute offset within the supported file
range. Successful reads/writes advance it; failed operations do not. Handles
increase monotonically within a mounted volume, never recycle, and fail before
overflow. At most 64 descriptions may be open at once.

These APIs check ownership, not identity authenticity or Unix permissions.
The eventual service adapter must supply owners from authenticated Channel
sessions and perform credential/path checks; it must not trust caller-supplied
session/process numbers. Raw inode APIs remain internal volume primitives, not
an application-facing authorization boundary. Inheritance now shares descriptions
and offsets as described below.

Final unlink removes the name and writes link count zero but retains its inode
and blocks while any description is open. Existing handles can still read,
write, and grow it. The inode cannot be reallocated or hard-linked back into
the namespace. Last close, once no committed grant retains the description,
reclaims its current data/pointer allocations and
inode in one staged mutation. A failed close leaves the handle and inode pin
intact for retry after successful rollback; failed rollback remains fail-closed.
Closing a still-linked inode leaves its allocation unchanged.

`release_files(session)` is an authenticated-service cleanup primitive, not an
application request accepting arbitrary session IDs. It closes only that
session's descriptions. Cleanup may make partial progress; an I/O failure
leaves the failing and remaining handles available for retry, and other
sessions' descriptions keep the inode pinned. There is no fallible cleanup in
`Drop`: callers must explicitly close or release sessions before discarding a
volume. A crash or dropped volume with open, unlinked inodes can leak allocations;
on-disk orphan tracking/recovery is not provided by the in-memory pin registry.

Source-local tests cover owner/session isolation, access modes, offsets, handle
limits/exhaustion, stale handles, inode reuse, unlinked-file growth through
double indirection, final-close read/partial-write failures, retry, cleanup,
and failed rollback. The real-image test retains a large unlinked file through
two opens, writes and reads it, closes both descriptions, checks freed blocks,
and remounts to verify reclamation.

#### Inherited open descriptions

`prepare_inherit`, `commit_inherit`, `claim_inherit`, and `abort_inherit` reuse
the shared `hunde::inheritance::Grants` registry and existing token/reply types.
Prepared flags come from the authoritative C `IO_OPEN_READ`/`IO_OPEN_WRITE`
constants, mapped from the description's access mode. Commit is bound to the
source owner and a nonzero child PID. Claim requires the committed target PID
and exact flags; it consumes the token once and attaches the child's owner to
the same handle, access mode, and offset. Repeated grants to an existing owner
do not create extra owner references; libc manages local descriptor aliases.
Independent opens still have independent offsets.

Close/release removes only the matching owner. Uncommitted source grants are
discarded on close/release. Committed grants retain the description and inode
even with no owners until claim or source-authorized abort, including after
final unlink. A child may claim after the source closes or disconnects. Limits
are 64 descriptions per volume, 64 owners per description, and the shared
registry's 64 grants. A capacity-rejected claim leaves its token available for
retry without adding an owner.

Aborting the final committed grant of an ownerless, unlinked description may
need disk I/O. The shared registry exposes `abort_candidate` to validate and
preview the grant without consuming it, and `retains_except` to determine
whether another committed grant still pins the handle. EXT2 reclaims first,
then consumes the grant and removes the description only after success. Read
errors or successfully rolled-back write errors leave both token and pin
available for retry. Failed rollback remains fail-closed. Other owners, other
committed grants, and independent descriptions still prevent reclamation.

The existing authenticated-service requirement still applies: these low-level
methods trust the owner/PID identities supplied by the eventual Channel adapter.
Cleanup for a child that dies before claiming a committed grant remains deferred,
as with existing services; such grants can retain orphaned allocations. Neither
grants nor open descriptions survive service crashes or remounts.

Tests cover shared reads/writes/seeks, access preservation, wrong source/PID/
flags, replay, source close/disconnect, uncommitted cleanup, multiple grants,
owner/grant limits, final-abort failure injection and rollback. Shared-registry
tests verify preview authorization and retention counting without mutation.
The real-image test commits a grant for an unlinked large file, closes the
source and other open, claims from the child, verifies the preserved offset,
and checks final reclamation after child close and remount.

#### EXT2 request adapter

`Ext2::mount(device)` constructs an explicitly mounted request backend. It
implements the existing `IoComponent` dispatch/disconnect interface without
requiring a fake default filesystem or hard-coded device path. `IoComponent`
itself no longer requires `Default`; the existing default-entry loader still
does. Socket/pipe/memfs/procfs still use the default-entry loader; their current
entries use ABI v6, retaining the file-change completion record introduced in v5.

The adapter consumes the shared `Request`, `Operation`, `Reply`, `FileInfo`,
directory-entry, owner, and grant types. Path operations obtain credentials
through the host callback for the request PID. Hosted tests inject that callback
explicitly. The future Channel adapter must authenticate the supplied PID/session;
passing arbitrary `Request` identities directly is not an authentication boundary.
Handle operations use the already-granted access mode and exact owner rather
than repeating path permission checks after an open.

Supported requests are read/write/execute and create/exclusive opens, reads,
writes, absolute/current/end seeks, metadata, paginated directory listings, `mkdir`,
regular-file hard links, recursive/nonrecursive removal, inheritance, close, and
session release. Directory
listings include the volume's `.`/`..` records and use the existing batch limit.
Every traversed directory requires search permission, including before `..`;
root traversal cannot escape the mount. Paths retain the volume's length,
component-length, and depth bounds, including creation paths.

Owner/group/other mode bits control access; root bypasses these mode checks.
Directory mutation requires parent write/search permissions. Sticky directories
restrict removal to root, the parent owner, or the target owner. New directories
use mode 0755, inherit parent setgid/group where applicable, and reject owner IDs
that cannot be represented rather than narrowing them. External ACL/attribute
layouts and unsupported inode flags remain rejected; ACL policy, umask, and
timestamps are not implemented.

Writable opens can create missing regular files through the existing open
operation. Create/exclusive flags require write access; exclusive creation
rejects an existing name. Files use mode 0644 and authenticated owner/group IDs,
inheriting the parent's group in a setgid directory. Parent write/search checks
and representable IDs are required before creation; an exhausted handle table
does not create or truncate a file.

`IO_OPEN_WRITE` grants write access without changing existing contents.
`IO_OPEN_TRUNCATE` explicitly requests truncation and requires write access;
truncating a missing path also requires `IO_OPEN_CREATE`. Libc maps `O_TRUNC`
to this flag on the original open, never a remove/reopen operation. Native
replacement writers, including `write` and I/O tracing, request it explicitly.
Rust EXT2 truncation transactionally frees direct and indirect
blocks while retaining inode identity, permissions, ownership, hard links, and
other open descriptions' offsets. Create and exclusive-create flags do not
implicitly truncate; failed exclusive creation leaves the existing inode intact.
Rust memfs uses the same flag semantics. The C stream adapter has a fallible
truncate callback: legacy EXT2 updates the same inode before reclaiming its old
blocks. Its legacy reclaimer supports only first-group blocks, so truncation
validates direct/indirect allocations before changing the inode and rejects
out-of-group blocks or triple-indirect allocations. FAT32 clears the existing
entry's length while retaining its allocated
chain, and devfs rejects truncation. No backend implements truncation by deleting
the path.
Append writes select the current inode EOF for every request and update the
shared description's offset only after a successful mutation. EOF selection and
the write are serialized by the volume's dispatch worker. Nonblocking transfer
flags are accepted for regular files; they do not bypass device completion waits.
Read/execute opens support the kernel loader's `IO_OPEN_READ | IO_OPEN_EXECUTE`
request. Execute requires read access, an existing regular file, and search
permissions along its path. Non-root callers need read/execute permission in the
selected owner/group/other class; root bypasses class checks but still requires
at least one executable mode bit, matching the existing C executable gate.
Execute opens never create a missing file. Descriptions retain execute alongside
read/write access through inheritance, including exact flag validation on claim;
execute does not grant write access or change offsets.
Hard links use the existing `IO_REQUEST_LINK` operation and namespace routing;
the namespace rejects cross-mount links before either backend is invoked.
Source traversal requires directory search permissions, and destination creation
requires parent write/search permissions. The low-level transactional link
operation rejects directories, duplicate names, invalid/reserved targets, and
unsupported layouts while retaining inode identity and allocation accounting.

The shared `hunde` adapters carry the two paths without changing the ABI-v4
request layout: for `LINK`, `write_data` contains source bytes followed by target
bytes, `offset` is the source byte length, and `count` is their total length.
Neither path includes a terminator; each must be nonempty, shorter than 1024
bytes, and free of embedded NULs. The shared decoder checks the split against
the actual buffer bounds, and link flags must be zero. This is operation-specific
control metadata, not a bulk-data transport. Older components reject the
previously unsupported operation rather than interpreting a new struct layout.
The C Channel adapter packs its existing separate path fields for the Rust host;
the Rust Wuff adapter preserves both paths for the loaded filesystem.

Recursive removal uses the existing flag, checks parent permissions and sticky
ownership at every entry, and requires read/search access when enumerating a
directory. Directory inode ancestry detects cycles; the normal path length and
32-component depth limits also bound traversal. Root and `.`/`..` removal are
rejected. Each unlink/rmdir uses the existing transactional mutation; the whole
tree is not one transaction. A later permission or I/O failure may leave earlier
entries removed, and retry traverses the remaining tree. External hard links and
open descriptions retain their normal final-unlink/reclamation behavior.
Unknown flags, append on reads, and other unsupported operations return explicit
errors.
Disk, protocol, permission, and stale-handle errors translate to authoritative
C I/O status constants. Disconnect reports cleanup failures and preserves
retryable handles instead of dropping an open-file pin after a failed close.

Tests exercise credential lookup failure, owner/group permissions, ancestor
search and `..`, sticky/setgid handling, metadata/list replies, seeks, inherited
requests, open-unlinked files, disconnect failure/retry, unsupported flags,
and creation depth limits. Creation/exclusive flags, setgid file ownership,
truncation through hard links, other-handle offsets, handle exhaustion, denied
opens, append across independent descriptions, and failure rollback have
source-local regressions. Execute-open tests cover owner/group/other access,
root's executable-bit gate, missing files, ancestor search, and access-preserving
inheritance. The generated-image test also invokes the request
adapter against a real mounted filesystem. Native and cross-target validation
does not constitute live Channel or Wuff acceptance.

The I/O host loads EXT2 for configured secondary volumes through the versioned
mount boundary below, alongside socket, pipe, memfs, and procfs. EXT2's native
block adapter cannot be imported directly into a Wuff; it connects through
callbacks. Do not simultaneously
mount the active rootfs image writable in a second independent filesystem
instance. Rootfs, `/boot/config`, and bootstrap services remain unchanged.

#### Hosted EXT2 caching

The shared native Rust block client now embeds the existing C `BlockCache`
alongside its `BlockClient`; hosted EXT2 reuses it through the same per-device
callbacks. Filesystem metadata and file contents share the cache, avoiding
repeated block-service round trips without introducing a second decoded-inode
cache or Rust replacement for the generic C cache policy. This is per-opened
device userspace ownership, not a global driver-host cache or cross-process
coherence mechanism. A disk must still have one writable filesystem owner.

The existing cache batches contiguous read misses, protects repeatedly read
sectors from scans, writes through before updating resident sectors, and does
not admit uncached streaming writes. EXT2 rollback writes pass through the same
cache, restoring resident originals after a failed mutation; failed rollback
still disables volume I/O. Existing cache diagnostics belong to the I/O host
process. The base cache holds 1024 sectors and can grow through the existing
reclaimable-page chunks, which are released when the native client closes or
its owning worker handles memory pressure. The base cache remains available.
`BlockCache__init` initializes fresh storage in place, avoiding a large returned
aggregate on a host fibre's stack; it is not a reset for a live cache with chunks.
Native adapter tests cover cache hits, write/rollback coherence, short/error
completions, growth, and cleanup. Generic cache tests retain their existing
scan-resistance and failure coverage.

#### EXT2 Wuff mount boundary

`ext2fs.wuff` exports only `hunde_filesystem_component_entry_v1(host, block)`.
The common host table and returned I/O descriptor now use ABI v6; the explicit
filesystem mount entry remains v1 and validates that host version. A separate ABI-v1
`HundeBlockHostOperationsV1` table contains an opaque device context, a count
of 512-byte sectors, and exact-transfer read/write callbacks. The shared C
contract is `common_includes/hunde/filesystem_component.h`; layout/offsets are
asserted in C and Rust. No native C block symbols are imported by the Wuff.

`LoadedComponent::mount_ext2(device_path)` opens a native `hunde::block::Client`
in the host, loads only the approved `/sbin/components/ext2fs.wuff` image, and
passes that one device capability to the entry. The Wuff cannot select another
device through this table. Native transfers still use the existing BlockClient,
Channels, and `hunde-puffer`; the callback interface is not another IPC or bulk
transport. The host owns the client in stable boxed storage. The shared
`HostedDevice` checks the header, geometry, context, callbacks, and transfer
bounds and copies the table; no pointer to temporary table storage is retained.
Only `hunde` performs the raw pointer/callback translation.

The filesystem component trait has fallible, explicit mount initialization.
EXT2 validates/mounts the supplied capability before the common adapter
publishes any request callbacks. Failure returns a load error and releases the
unpublished module and host client. Each successful entry creates a distinct
volume and handle/grant registry; there is no global default mount or device
path in the driver. The bootstrap image now includes `ext2fs.wuff` alongside
the existing components, and both architecture builds enforce the existing
Wuff export, relocation, RELRO, and no-import/no-TLS checks.

Disposal now has a fallible component hook. EXT2 refuses disposal with
`IO_WOULD_BLOCK` while any open description or inheritance grant remains.
The ABI adapter preserves the component context on error, allowing retry;
it drops the driver only after success. The loaded wrapper disposes the
component first, unmaps its image next, and releases the host-owned block
client last. The existing wrapper treats failed disposal during `Drop` as
fatal instead of unmapping live callbacks. The native client's existing
best-effort drop-close behavior is unchanged.

Hosted tests cover malformed/truncated tables, invalid ranges, null buffers,
native and malformed callback errors, moved host storage, fallible mount
publication, and failed disposal/retry. EXT2 tests verify owners and committed
grants prevent disposal. Cross-built Wuff inspection is not proof of a live
Channel mount; the configured endpoint below exercises the loader and transport
before rootfs migration.

#### Configured hosted EXT2 volumes

An fstab entry such as `device /dev/virtio7 ext2.wuff /mnt/hosted-ext2`
selects the hosted EXT2 implementation. `ext2.wuff` is a loader choice, not a
different disk format. Mama starts the host before secondary block drivers;
`hundeschule` submits the ordinary `IO_REQUEST_MOUNT` over a protected
Channel to `hunde-io-host.mounts` after those drivers report ready. Only after
the host successfully publishes the volume endpoint does bootstrap register
the namespace mount. `humpen` skips these already-hosted entries rather than
starting a second C driver.

The control handler validates the authenticated client's mount capability via
the existing kernel authorization interface. It copies the request, reserves
the device and endpoint against duplicate hosted mounts, and queues work for
the root host fibre to spawn on its one shared scheduler. The worker owns its
block client, Wuff, filesystem dispatcher, and Channel endpoint for process
lifetime. It records successful readiness or an initialization error and
explicitly wakes the control poller; the initiating mount request completes
through the normal retained-request path. Failure releases its reservation;
disconnecting the configuring client does not unmount a persistent volume.
After registration, readiness/scheduler failures terminate the host rather than
unmapping callbacks behind a still-registered endpoint; listener teardown and
unmount are not implemented by this slice.
Root mount requests are admitted through the same authenticated control path.
Noncanonical paths, unsupported formats, and duplicate hosted devices/endpoints
are rejected. Before starting any configured mount, `hundeschule` validates the
whole supported disk-mount configuration, rejecting duplicate devices or
mountpoints and requiring a root entry. It initializes root before other mounts,
regardless of fstab order. A hosted root is published in the namespace only after
its endpoint is ready, without launching `humpen-wachhund` or a C root driver.
Initialization/publication errors stop startup rather than falling back to C.
This prevents duplicate C/Rust disk ownership through this boot configuration;
it is not kernel-enforced cross-process block-device exclusivity.

The QEMU configurations append a dedicated 16 MiB `hosted-ext2.img`, leaving root,
bootstrap configuration, and existing C filesystem test disks untouched. It is
deliberately not 8 MiB: bootstrap currently identifies its configuration disk
by that size. The hosted volume is
seeded from `config/hosted-ext2` plus the target's dynamically linked `cat`, staged
under the image build directory with executable permissions. The endpoint is a
normal configured filesystem,
not a test-only protocol. Runtime acceptance reads a seeded file, enumerates
directories, creates/removes a directory, and creates, appends, replaces, copies,
and removes regular files through its actual Wuff and native
block callbacks. These live file/directory checks pass on AArch64 and RISC-V.
Acceptance also launches `/mnt/hosted-ext2/cat` to read a file on that volume,
exercising kernel executable loading through the hosted provider. Its interpreter
and shared libc still come from their bootstrap/root locations. These
secondary-volume checks are separate from the root-boot acceptance below.
Live parity checks also create a hard link, move a file through libc's existing
link/unlink implementation, recursively remove a nested tree, and read the
surviving external hard link before final cleanup.
The shared decoder populates `Request::transfer_flags` only for read/write
operations; open modes, seek origins, remove flags, and inheritance flags stay
in their typed operations. This avoids treating a read-only open as a nonzero
transfer flag, while preserving unknown read/write flag bits for backend
validation. Source-local boundary tests cover both cases.
Native C block/Channel waits now park their current fibre on the host's shared
scheduler. Other fibres can dispatch socket, pipe, and filesystem requests
while a volume awaits a block reply. Cooperative ownership gates serialize the
cached buffer-manager Channel (including lazy connection and error reset) and
Wuff-loader I/O through the default C client. Gate release wakes contenders
without requiring another external event. The outer scheduler retains the
generation-checked process wait when runnable work is exhausted.
Hosted regression tests delay real queue replies and connection establishment,
verify independent fibre progress, and exercise shared-client error recovery.
Runtime smoke tests run hosted EXT2 reads alongside socket/pipe operations;
this is progress coverage, not a bound on latency or CPU-heavy handler work.
See [native wait ownership](rust-userspace.md#one-scheduler-multiple-fibres).

#### Hosted EXT2 file watches

The shared Rust adapter exposes the existing `IO_REQUEST_WATCH_FILE` and
`IO_REQUEST_WAIT_FILE_CHANGE` operations through typed handles, generations,
event flags, and a dedicated ABI-v5 completion. Native watch batches are copied
without assuming alignment; empty, oversized, and malformed batches are rejected.
Every handle in a batch must belong to the authenticated process before any
change is returned. As in the C adapter, the client opens a dedicated wait
Channel, so waits may cross sessions of that same process. Watch registration,
reads, writes, and closes retain their ordinary session/process ownership checks.
These operations do not add another transport.

EXT2 watches follow open inode descriptions, not mutable path strings. Independent
opens of the same inode observe committed writes and truncation; inherited owners
share their description's generation. Only final unlink reports deletion; an
unlinked open inode remains readable and a replacement path does not redirect its
watch. Closing the last owner or disconnecting releases the watch state with the
existing description cleanup.

Each description retains the most recent generation of each supported event kind,
not an unbounded event journal. Repeated events of one kind coalesce. Remaining
event kinds are returned in generation order, so a truncation notification does
not consume a later write/read-readiness notification in libc's kqueue adapter.
Rolled-back or empty writes do not advance generations. A wait without a change
returns pending through the existing retained-request path; the normal endpoint
poller revisits it after other requests make progress, with the host scheduler
parking normally when idle. No polling thread or service-specific queue is added.

Root admission and boot publication ordering have unit-test coverage. Hosted-root
acceptance covers live login, executable loading, and filesystem parity.
Hosted-component smoke tests exercise file watching on the configured Wuff volume
on AArch64 and RISC-V. A separate root-filesystem/shutdown test validates the
active provider without removing or skipping the original Ctrl-C smoke test.
The deterministic smoke test waits for initial output before changing the file,
then requires actual followed output rather than counting echoed shell input.
The C-root controls pass the strengthened tail-follow and filesystem portions
on both architectures, then time out at the foreground `cat` Ctrl-C check;
neither complete command smoke run is reported as passing.
Explicit truncation fixes the destructive write-only open found by `rm -P`
acceptance. Standard QEMU configurations now select `ext2.wuff` for `/`, after
hosted endpoint initialization and without a legacy C root watchdog. Acceptance
covers Bash/shared-libc command loading, non-destructive rejected secure removal,
hard links and open descriptors across `O_TRUNC`, tail-follow continuity through
POSIX redirection, copies, directory moves, recursive removal, and shutdown.
Separate legacy EXT2/FAT32 mount checks exercise the C truncate callbacks.
Previously reused root images also contain directory entries rejected by strict
Rust enumeration; corruption versus a decoder defect remains unconfirmed.
This is not evidence that validation should be weakened or
that existing disk images may be discarded automatically. `/boot` uses a
hosted Rust snapshot component; `/sbin` now hands off to the host's safe Rust
wrapper over the shared C archive backend before Mama completes bootstrap.
See [bootstrap services](bootstrap-services.md). The early-host startup slice
now mounts `/boot/config` through the host using the already available `/sbin`
archive; it does not need the application root or disk configuration to load EXT2.

### Root namespace ownership

`hunde-io-host/src/filesystem/namespace.rs` owns a single `RootFilesystem`.
Its mount tree has an explicit optional root and mounted overlays. Bootstrap
overlays can be installed before `/`; installing root preserves `/sbin`, `/boot`,
and `/boot/config`. Paths select the longest component-prefix mount, and the
selected filesystem receives a mount-relative path. Hard links cannot cross
mounts. Directory pagination emits unique immediate mount children before
rebasing the backing filesystem's offset.

Mama starts the statically linked Rust host instead of the standalone C
namespace service. The host publishes the retained `hund-dateisystem` endpoint
before loading components. After `/sbin` registration, another fibre loads the
Wuffs while the namespace fibre continues serving executable/module lookups.
Both fibres and per-volume workers use the same scheduler. Hundeschule waits
for hosted readiness instead of spawning another host.

The namespace retains the current application redirect protocol and final
filesystem endpoints. Mount selection now runs inside the host, but clients
still attach directly to the selected endpoint; this slice does not unify
backend handle spaces or remove open-time redirects. External bootstrap,
device, and FAT32 providers remain mounted by endpoint. No internal forwarding
Channel is introduced. The native service callback envelope carries bounded
redirects separately from the Wuff I/O completion, whose layout is unchanged in v6.

The host registers its application router through `hunde::service::LocalPathRouter`.
Its Wuff loader uses that router and the same namespace tree directly rather
than opening a Channel to its own process. The native I/O client checks the registered service
before connecting, then follows the normal bounded redirects to external
providers such as bootstrap embedfs before its handoff. This is an I/O routing operation, not a loopback
Channel: queue allocation, the warden, and kernel self-connection rejection are
unchanged. Registration owns its callback state, rejects replacement, and is
removed when the Rust registration is dropped; fork resets the native route.
Shared Rust glue rejects reentrant mutable callback dispatch rather than
aliasing a live handler. The local namespace and Channel-facing namespace share
the same root/mount state, not a second independently configured mount table.

### Application router ownership

`hunde-io-host/src/host/router.rs` implements the existing `hund-io-system`
endpoint inside the Rust host. Filesystem requests call the namespace directly;
socket and pipe operations select the existing hosted endpoints. Configured
console instances now own paired input/output backends, terminal state and
bidirectional handles inside the host. Hardware-profile `fstab` entries mount
`/dev/ttyN` or `/dev/vttyN` and explicitly bind `:console:` to the default.
See [configured consoles](io-system.md#configured-consoles). Unconfigured legacy
stream opens retain their old redirects during bootstrap; hardware UART,
framebuffer services still run externally; the VirtIO keyboard runs in the
driver host.

Mama waits for the host's application endpoint instead of starting a standalone
C I/O router. The namespace endpoint is published first. Both endpoints are
polled on the same scheduler before component loading starts, and the local
loader route uses the same Rust application router. The Wuff ABI and kernel IPC
rules do not change. Final resource endpoints, handle ownership, external
bootstrap filesystems, and terminal/device processes remain as before.

### Direct hosted dispatch

The application endpoint now serves hosted filesystem, socket, and pipe requests
directly. `host/dispatch.rs` owns a process-wide handle table: an application
handle identifies its component, backend description, authenticated PID, and
Channel session. Component handle collisions are isolated; closed host handles
are not reused. Namespace selection and mount-relative rewriting happen before
local component dispatch, not an application redirect to a hosted endpoint.

Standard QEMU fstab entries use `ext2.wuff` for `/`, `/mnt/channelio-ext2`, and
`/mnt/hosted-ext2`; each volume owns a separate component, block client/cache,
and worker fibre inside the same host. `/mnt/channelio-ext2` no longer starts
a standalone C EXT2 process. Mama also requests hosted `ext2.wuff` for
`/boot/config` after `/sbin` and `/boot` registration. It waits for initialization
before namespace publication and boot handoff. No standalone C EXT2 process is
needed in the standard QEMU boot sequence. Pi UEFI and Milk-V root/data fstab migration still requires
their own platform validation; FAT32 remains a separate C filesystem provider.

The host also translates inheritance tokens across components, retaining
PID-bound committed grants after source-session cleanup. Claims allocate new
host handles while the component retains its shared open-description semantics.
Aliases sharing a component, backend handle, and authenticated PID/session keep
that backend ownership until the owner's final host alias closes. Closing an
earlier alias invalidates only its host handle. Uncommitted grants remain usable
while another alias retains that backend ownership and are discarded on its
final close; committed grants survive for the target process to claim. Other
owners close their backend ownership independently.
File-change waits validate all host handles, split by component, and return the
original host handle when any component completes. Other pending wait jobs are
cancelled without exposing their backend identifiers.

Requests are admitted to bounded per-component work queues. Shared `hunde`
`OwnedRequest` translations own path/write snapshots and read staging until
completion; no C request pointer or client-buffer borrow crosses a worker fibre
wait. Completed reads copy only the validated byte count into the existing
directional `hunde-puffer` buffer. This internal staging is not a new transport
or a zero-copy claim. Up to 64 requests are admitted, with 512 host handles;
admission and identity checks precede component mutation.

Socket, pipe, memfs, and procfs workers share the component polling fibre. Each
EXT2 volume retains its own worker fibre, all on the host's existing scheduler.
Pending component operations do not block dispatch to other workers. Enqueue
and completion notifications wake the existing scheduler; pending reads and
watches are revisited after other operations progress. Disconnect cancels queued
requests and runs component cleanup on the worker before the connection's managed
buffers are released. Failed cleanup remains pending for a later wake/retry.

The frontend fibre drains kernel service events through the shared `hunde`
typed event adapter before parking. Normal-pressure notifications are consumed
without requesting reclamation; low and critical pressure coalesce into a
pending reclamation for each registered component worker. A later normal event
does not cancel outstanding reclamation. Each worker reclaims its own cache
only when it regains control, never from another fibre while a block operation
may retain cache references across a wait. Busy or failed reclamation stays
pending for retry. Mounted components forward this operation to their
host-owned native block client; no Wuff callback ABI extension is needed.

This event polling is required for progress, not just cache sizing. A captured
RISC-V stall had an unconsumed `SERVICE_EVENT_MEMORY_PRESSURE` normal-pressure
notification in the host, a ready Bash child on the same CPU, and no pending
host Channel messages. Queue wait correctly returned immediately for the
unconsumed event, leaving the service spinning instead of parking. The host must
consume these events rather than weaken kernel wakeup checks or extend smoke
timeouts.

The application endpoint opts into resource-serving buffer preparation, so its
initial open/pipe/claim can return a host handle and normal buffer descriptors
without a redirect round trip. This dispatch change preserves the wire protocol
and Wuff completion layout; the current host ABI is v6.
Existing component endpoints remain temporary compatibility/readiness aliases;
normal hosted I/O no longer uses them. Bootstrap, terminal/device, and legacy
filesystem providers still use external redirects in this slice. Replacing those
requires authenticated host-side adapters or moving their implementations; the
host must not proxy as its own privileged identity and bypass caller permissions.

### Remaining consolidation

1. Measure representative current file, cached file, block, console, and network
   paths, including open-time redirects and steady-state requests.
2. Move the remaining external hardware drivers into `hunde-treiber-host` while
   preserving their external device protocols and ownership/cleanup rules.
3. Complete the remaining external terminal/device integration. Namespace,
   socket, pipe, filesystem, and information components already execute in the
   I/O host, with dynamic ELF loading for its approved Wuffs.
4. Validate each remaining migration on AArch64 and RISC-V and compare the
   recorded measurements. See [TODO](../../TODO.md) for agreed unfinished work.

Do not preserve a permanent Channel path between components after both sides
have moved into one host. Do not remove an existing process boundary until the
replacement host owns its resources, blocked-operation progress, cleanup, and
tests.

## Open design questions

- Define how boot-critical modules are supplied without depending on the
  filesystem or device stack that those modules are needed to create.
- Decide which existing service endpoint names remain as compatibility aliases
  during migration and when each alias is removed.
- Define recovery of sessions, buffers, device state, and outstanding operations
  after either host exits. Current service restart recovery remains deferred.
- Decide whether safe runtime unloading is required. Initial dynamically loaded
  drivers remain resident because unload requires proven callback, fibre, IRQ,
  and DMA quiescence.
