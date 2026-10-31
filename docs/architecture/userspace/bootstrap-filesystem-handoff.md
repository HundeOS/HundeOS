# Bootstrap Filesystem Handoff

## Status

The standalone embedfs process and its drain/handoff protocol are retired.
The host reads its bootstrap manifests and Wuffs directly from the immutable
archive mapping, then loads `embedfs.wuff` and registers `/sbin` using
`/sbin/config/bootstrap.fstab`. See [bootstrap services](bootstrap-services.md).
Applications and executable loading use ordinary I/O Channels and host handles,
with no kernel filesystem or syscall fallback.

Only the I/O host receives the archive mapping: read-only and non-executable,
with kernel-owned backing storage. The userspace loader privately maps and
relocates Wuffs. The shared parser is in `common/fs`; the retained C backend is in
`user/lib/fs`, used through a safe `hunde` adapter by the embedfs Wuff.

Embedfs serves bounded chunks from immutable bytes without snapshot limits.
Handles retain independent offsets and authenticated PID/session ownership.
Services and drivers require hund credentials; ordinary binaries are
world-readable/executable. Unknown credentials fail closed.

## Executable Loading

The kernel opens through `hund-io-system`, served by the Rust I/O host. Hosted
filesystems are selected and dispatched inside that process; their handles
remain on the host's Channel. External providers may still return bounded
redirects, after which the kernel uses a direct Channel to the final provider.
The retained `hund-dateisystem` endpoint is the host's bootstrap mount-control
interface, not another process on the executable-loading path. Do not introduce
a filesystem-specific request/reply queue or bypass userspace I/O routing.

The kernel supplies the originating process identity through an authenticated
kernel endpoint. Userspace endpoints cannot impersonate it or choose another
client's PID. Embedfs checks authoritative credentials for executable access.
The native Rust EXT2 component checks caller credentials, path traversal, and
execute permission. FAT32 uses synthesized permissions and restricts mutation
to root. Read permission alone is not permission to execute.

Each process owns at most one pending executable load. The initial syscall copies
its arguments and consumes an authentication ticket once. The kernel parks that
syscall as `PARK_WAIT_EXEC_LOAD`; scheduler observation of the kernel Channel's
atomic queue cursors makes its continuation runnable. The continuation advances
OPEN, READ, and CLOSE without resubmitting caller pointers or credentials. A
background spawn completes only after the child is scheduled and returns its PID;
a waiting spawn transfers into the existing child wait. Each continuation performs
bounded, nonblocking protocol work, and no scheduler mutex spans a wait or context
switch. Request and reply bytes use the direct Channel's directional Queue data;
the kernel validates connection identity and mappings under the scheduler lock.
Disconnect, requester cleanup, and malformed replies cancel the job and close
its Channel. Service-side handles are released through normal session cleanup.

The transport endpoint uses the reserved `QUEUE_KERNEL_PID`, not process PID zero.
Its Channel ticket separately records the spawning process as the authenticated
origin, so dependency scheduling can relate userspace services back to the blocked
caller without pretending the kernel transport endpoint is a schedulable process.

The kernel retains ELF validation, address-space construction, credential
transitions, and scheduling. It copies the provider's bytes into an owned image
before validation. Embedfs is immutable; disk components stream reads from their
open descriptions. Consistency under concurrent mutation is a separate
requirement, not a claim that all disk opens take immutable snapshots.
The loader validates ELF64 architecture, headers, segments, overlaps, and entry
points, with image bytes limited to 64 MiB and segment memory to 128 MiB.
Static programs and dynamically linked PIEs use the loader/interpreter contract
described in [dynamic linking](dynamic-linking.md#interpreter-integration).

Executable loading now uses 2 MiB directional buffers managed by `hunde-puffer`,
with validated descriptors and bulk copies into the kernel image. Native Rust
EXT2 and FAT32 components accept executable opens and stream files beyond the
pseudo-filesystem snapshot limit. Their permission and mutation rules are
documented in [Userspace I/O](io-system.md).

## Bootstrap Ownership

Mama starts without filesystem access and establishes the providers before
launching configured startup:

- Embedded binaries use a dedicated page-aligned, read-only archive. Userspace
  owns file operations; the kernel retains backing-storage ownership.
- The userspace block driver owns the boot configuration device from startup;
  no kernel boot mount accesses it. Its EXT2 provider serves `/boot/config`.
- A typed hardware-information syscall supplies the snapshot that userspace
  `sysinfofs` renders as `/boot/sysinfo`.

Readiness gates prevent configured startup before these mounts succeed.
The old kernel bootfs and EXT2 providers have been deleted, so failures cannot
reopen them. Restart/remapping recovery remains separate work.

## Verification Gates

Tests cover permission and authentication handling, malformed ELF rejection,
incremental direct-Channel loading, connection closure, requester cleanup, and
archive-backed component loading before filesystem mounts. Continue verifying boot dependencies and mounts across
configured targets when changing the remaining kernel I/O compatibility paths.
Mutable-provider snapshot consistency and service-restart recovery remain
separate requirements.
