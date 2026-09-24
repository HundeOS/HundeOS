# Bootstrap services

The kernel launches the embedded `hunde-mama` service. Mama establishes a
configuration-independent userspace filesystem before normal system startup:

1. Start `hundekanal-warter`, `hunde-puffer`, `hund-unterbrict`, `hund-dateisystem`, and
   `hund-io-system`, waiting for each service's readiness.
2. Start `embedfs` and register `/sbin` in the filesystem namespace.
3. Start `sysinfofs` and register `/boot`, exposing `/boot/sysinfo`.
4. Identify the boot configuration disk, start its `virtio_blk_channelio`
   driver and `ext2fs`, and register `/boot/config`.
5. Call `boot_complete()` after these operations succeed, then start
   `hundeschule` through normal Channel-based executable loading.

Mama remains alive to reap and report bootstrap child exits. Automatic service
restart and remapping of surviving sessions are not implemented. Readiness waits
use the existing service interface; they currently have no timeout.

Boot disk selection retains the existing unique 8 MiB VirtIO disk convention.
Missing or ambiguous candidates fail bootstrap rather than selecting an
arbitrary disk. This convention is not a general disk-identification mechanism.

## Filesystem ownership

`hund-dateisystem` starts with an empty namespace and serves requests immediately;
it does not read `fstab` or spawn drivers. Mount registration uses
`IO_REQUEST_MOUNT` over the existing bidirectional Channel. The namespace accepts
registration only from an authenticated sender whose kernel process-registry
record identifies the `hunde-mama` or `hundeschule` executable and the `hund`
group, not from identity fields supplied in the request payload.
Duplicate mountpoints are rejected. Adding the root mount does not replace more
specific `/boot`, `/boot/config`, or `/sbin` mounts.

`hundeschule` reads its configuration through normal I/O Channels, skips the
already-running boot block driver, and starts the remaining drivers. It reads
`fstab`, registers the disk mounts, starts the root filesystem and `/tmp`, `/dev`,
and `/proc`, then launches `hundeschau`. Humpen starts the remaining configured
non-root disk filesystems later.

## Hardware information

`SERVICE_BOOT_HARDWARE_INFO` copies a typed `BootHardwareInfo` value to userspace.
The kernel provides hardware values, not filesystem paths or formatted text.
The snapshot includes CPU counts, UART, optional framebuffer/PCI/SD/Ethernet,
physical memory bounds, and interrupt-controller physical addresses. It excludes
kernel virtual addresses and internal allocator structures.

`hund-io-system` uses this interface before filesystems are available to choose
the console endpoint. `sysinfofs` renders the same information as the read-only
`/boot/sysinfo` file; other drivers can keep their existing configuration readers.
Process memory and CPU accounting remain available through `/proc`.

## Kernel boundary

Boot file operations no longer bypass Channels. Kernel root construction no
longer installs `/boot` or `/boot/config`; the old bootfs and EXT2 implementations
have been deleted. Independent boot/panic output remains a kernel responsibility.

The kernel records Mama's PID when it creates the initial process. Only that
process may call `boot_spawn()` while boot level 0 is active; it loads embedded
executables without requiring filesystem services. `boot_complete()` checks the
required service endpoints and irreversibly disables bootstrap spawning. Mama
waits for the actual mount acknowledgements before invoking it. A service name
alone does not grant bootstrap authority, and repeating handoff fails.

Normal spawn always reads through a kernel Channel client to `hund-io-system`,
including `/sbin` programs. It never falls back to the embedded reader on failure.
The kernel retains ELF validation, memory mapping, identity transitions, and
scheduling. The archive remains available to userspace embedfs after handoff.
See [Executable loading](bootstrap-filesystem-handoff.md) for limits and lifetime
handling.
