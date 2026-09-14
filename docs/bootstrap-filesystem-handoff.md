# Bootstrap Filesystem Handoff

## Status

`/sbin` file operations now run in userspace `embedfs`, started and awaited by
`hund-dateisystem` before namespace readiness. The kernel `/sbin` mount and
its file-handle implementation are retired; the old implementation is only a
test fixture. Applications use the existing I/O/namespace channels with no
`/sbin` syscall bypass or failure-triggered fallback.

The kernel maps its dedicated archive region read-only and non-executable only
into `/sbin/drivers/embedfs`. The shared parser is in `common/fs`. Linker
padding keeps the final mapped page inside the archive section. The backing
remains kernel-owned; process teardown removes mappings, not archive storage.
Embedfs serves bounded chunks directly from immutable file bytes, without the
disk drivers' 12 KiB snapshot limit. Handles have independent offsets and
PID/session ownership; close, client release, and namespace disconnect release
them. Drivers and services retain hund-only access; ordinary binaries are
world-readable/executable. Unknown client credentials fail closed.

Embedded executable loading uses a direct kernel archive reader, independent
of filesystem mounts, for bootstrap and subsequent embedded launches. This is
not channel-based executable loading. The EXT2 `/boot/config` mount and
`/boot/sysinfo` also remain kernel-backed.

The Channel payload and nonblocking request/reply implementation now live in
`common/ipc`, linked separately into kernel and userspace. The existing
userspace client uses that implementation. Connection establishment, endpoint
mapping, and waits still live in userspace; the kernel cannot yet open a Channel.

## Remaining Executable Loading

The target path is kernel -> Channel -> `hund-io-system` -> Channel ->
`hund-dateisystem` -> Channel -> filesystem driver. Do not introduce a
filesystem-specific request/reply queue or bypass I/O mediation.

The kernel supplies the originating process identity through an authenticated
kernel endpoint. Userspace endpoints cannot impersonate it or choose another
client's PID. Filesystems enforce execute and directory search permissions
using authoritative credentials.

The requester parks while I/O is pending. Progress is nonblocking; no scheduler
mutex remains held across a wait or context switch. Resolve shared payload
addresses through validated ownership, mapping, bounds, and lifetime checks;
never trust a peer-supplied address as a kernel pointer. Disconnect, requester
exit, and malformed replies must release resources and fail explicitly.

The kernel retains ELF validation, address-space construction, credential
transitions, and scheduling. Loading requires a stable image snapshot, not
reads that can mix file versions. The current 12 KiB disk-driver snapshot limit
must be removed from this path before switching it on.

## Remaining Bootstrap Ownership

Use explicit bootstrap -> preparing -> userspace-active states. Bootstrap
services must start without depending on the filesystem service they establish.
Preparing retains bootstrap ownership until replacement providers are ready.

- Embedded binaries: publish a dedicated page-aligned, read-only archive.
  Exported pages must not expose neighboring kernel memory. Userspace owns
  file operations; the kernel retains backing-storage ownership.
- Boot configuration: quiesce kernel block access before transferring the
  device to the userspace block driver and mounting its EXT2 filesystem.
  The two drivers must never operate the same device concurrently.
- Boot information: expose typed read-only shared information and render
  `/boot/sysinfo` in userspace.

Switch routing only after readiness and ownership transfer succeed. Once
userspace-active, failures must not silently reopen the old kernel path.
Restart/remapping recovery remains separate work.

## Verification Gates

Before activation, test permission rejection, malformed and large executables,
stable snapshots during mutation, backpressure, requester and service exits,
and resource cleanup. Verify boot dependencies and mounts across configured
targets before removing retired kernel providers.
