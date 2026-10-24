# Bootstrap Filesystem Handoff

## Status

`/sbin` file operations initially run in userspace `embedfs`, started and awaited
by `hunde-mama`. Before configured startup, Mama hands the mount to the I/O
host's safe `hunde::embedfs::Filesystem` wrapper over the same shared C backend,
drains the old provider, and retires its process. The kernel `/sbin` mount and
its file-handle implementation are retired; the old implementation is only a
test fixture. Applications use the existing I/O/namespace channels with no
`/sbin` syscall bypass or failure-triggered fallback.

The kernel maps its dedicated archive region read-only and non-executable only
into `/sbin/drivers/embedfs` and `/sbin/services/hunde-io-host`. The shared parser
is in `common/fs` and the userspace backend is in `user/lib/fs`. Linker
padding keeps the final mapped page inside the archive section. The backing
remains kernel-owned; process teardown removes mappings, not archive storage.
Embedfs serves bounded chunks directly from immutable file bytes, without the
disk drivers' 12 KiB snapshot limit. Handles have independent offsets and
PID/session ownership; close, client release, and namespace disconnect release
them. Drivers and services retain hund-only access; ordinary binaries are
world-readable/executable. Unknown client credentials fail closed.

The `/sbin` namespace entry remains unchanged during handoff: registering the
host's internal worker changes subsequent requests to direct dispatch. Only
authorized mount clients can drain the old provider. Its drain acknowledgement
waits for existing handles and queued requests/replies, and Mama terminates the
authenticated Channel peer afterward. Kernel boot completion no longer requires
a standalone `/sbin` endpoint; Mama verifies the hosted mount acknowledgement
before requesting boot completion. See [bootstrap services](bootstrap-services.md).

Only Mama's boot-level-0 `boot_spawn()` uses the direct kernel archive reader.
After `boot_complete()`, that entry point is unavailable. Normal spawn uses
Channels even for embedded executables. [Mama](bootstrap-services.md) establishes
the EXT2 `/boot/config` mount and `/boot/sysinfo` in userspace before configured startup.

The Channel and nonblocking request/reply implementation lives in `common/ipc`,
linked separately into kernel and userspace. Bounded bytes use each direct
Queue's directional data area. `kernel/ipc/kernel_channel` connects directly to
the target service, without the warden or a filesystem-specific transport.

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
The generic FAT32/EXT2 adapter rejects executable opens until it can authorize
them; it must not treat readable file bytes as permission to execute.

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
before validation. Embedfs is immutable; the current disk providers use per-open
snapshots. A future mutable provider must preserve snapshot consistency for
executable opens. The loader accepts static ELF64 images for the current
architecture, bounds headers and segments, rejects overlaps and invalid entry
points, and limits image bytes to 64 MiB and segment memory to 128 MiB.

Executable loading now uses 2 MiB directional buffers managed by `hunde-puffer`,
with validated descriptors and bulk copies into the kernel image. Disk providers reject executable opens pending
execute-permission support, and retain a 12 KiB open-file snapshot limit.
Embedfs has no such 12 KiB limit. Disk permissions and the snapshot limit remain
recorded in [TODO](../TODO.md).

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
one-way bootstrap handoff. Continue verifying boot dependencies and mounts across
configured targets when changing the remaining kernel I/O compatibility paths.
Mutable-provider snapshot consistency and service-restart recovery remain
separate requirements.
