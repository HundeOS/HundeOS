# Bootstrap services

The kernel launches the embedded `hunde-mama` service. Mama establishes a
configuration-independent userspace filesystem before normal system startup:

1. Start `hundekanal-warter`, `hunde-puffer`, `hund-unterbrict`, and `hunde-io-host`,
   waiting for each service's readiness. The Rust host publishes both the
   `hund-dateisystem` namespace and `hund-io-system` application endpoints.
2. Start `embedfs` and register `/sbin` in the filesystem namespace.
3. Wait for the host's initialized Rust `sysinfofs.wuff` and register `/boot`,
   exposing its cached `/boot/sysinfo` file. No standalone sysinfofs is started.
4. Identify the boot configuration disk and wait for its block driver. Request
   `ext2.wuff` initialization from the host's mount-control endpoint, then
   register `/boot/config` only after the hosted volume is ready. Mama does not
   launch a standalone C EXT2 process.
5. Initialize the host's archive worker, drain and retire bootstrap `embedfs`.
6. Call `boot_complete()` after these operations succeed, then start
   `hundeschule` through normal Channel-based executable loading.

Mama remains alive to reap and report bootstrap child exits. Automatic service
restart and remapping of surviving sessions are not implemented. Readiness waits
use the existing service interface; they currently have no timeout.

The Rust host initially publishes its filesystem namespace and application router
under their retained endpoint names. After `/sbin` is registered, it starts component loading
on another fibre of its single scheduler, while the namespace keeps serving the
loader's requests. It loads trusted Rust `socket.wuff`, `pipe.wuff`, `memfs.wuff`,
`procfs.wuff`, and `sysinfofs.wuff` from `/sbin/components`, then publishes
the existing `socket_router`, `hund-rohr`, `/tmp`, `/proc`, and `/boot` endpoints. Hundeschule waits for
hosted mount/pipe readiness before launching login; there are no standalone
pipe or memfs processes. Disk configuration cannot start a duplicate legacy socket router, pipe/memfs service,
or I/O host. Other high-level services remain separate; see
[I/O and driver hosts](io-driver-hosts.md#first-rust-socket-slice).

The loader resolves paths through the host's process-local application router,
which calls the shared filesystem namespace directly.
During bootstrap it uses an ordinary Channel to the standalone `/sbin` embedfs
provider; there is no Channel connection back into the host itself.

Before completing bootstrap, Mama requests `device - embedfs /sbin` from the
host's authenticated mount control. The host initializes the existing C archive
backend through the safe `hunde::embedfs::Filesystem` adapter and registers an
internal dispatch worker at the existing `/sbin` mount. No second namespace mount
or host-to-self Channel is created. The kernel maps the same immutable archive
read-only and non-executable into both approved bootstrap consumers; retiring
the old process does not invalidate the host's mapping.

Mama then sends `IO_REQUEST_BOOTSTRAP_RETIRE` to the old provider. Only authorized
mount clients can initiate draining. Existing handles remain usable; new path
operations redirect through the host. The acknowledgement waits for all old
handles to close and other connections' queued requests and replies to drain.
Mama terminates the authenticated Channel peer only after that acknowledgement,
and only then disables bootstrap spawning and starts Hundeschule. A failed host
initialization, drain, or termination stops bootstrap rather than silently
retaining the legacy provider. `/boot` is served by a separate hosted filesystem
component, not another process.

### Cached boot information

`user/drivers/sysinfofs` contains the safe Rust boot-information filesystem.
Its fallible Wuff constructor calls `hunde::boot::HardwareInfo::load()` exactly
once, then formats and retains the existing decimal `key=value` text. The shared
`hunde` layer invokes `BootHardwareInfo__load()` through the versioned host table,
normalizing C optional records and the interrupt-controller union into checked
Rust data. The component contains no unsafe code or direct syscall imports.

The cached bytes live until the component is disposed. Opens share those bytes
but have independent offsets; inherited references share their open description
and offset. Metadata reports the cached length, reads support EOF and seek, and
writes and namespace mutations fail with permission errors. The snapshot includes
`cpu.online_count` as observed at initialization, not a live count; `/proc`
continues to supply live process/CPU accounting. Unavailable boot data or malformed
normalized metadata fails initialization before the host publishes `/boot` readiness.

The host keeps serving the bootstrap archive while loading this component.
Mama waits for `/boot` readiness before publishing that mount and starting the
configuration-disk mount. `/boot/config` remains an independent EXT2 overlay.

The host's process-local compatibility bridge now completes pending dispatch
through its single fibre scheduler and serves local file handles without a
self-Channel or shared-buffer leases. Post-handoff EXT2 loading works on both
architectures. Root/shutdown, hosted filesystem/procfs, and concurrent
socket/pipe/memfs acceptance tests pass on both AArch64 and RISC-V, including
the absence of the retired embedfs process. Libc uses the existing AArch64
export policy to hide the toolchain-only `_stack` marker, with a post-link
absence check on both architectures. Loader validation remains unchanged;
see [dynamic linking](dynamic-linking.md#interpreter-integration).

Boot disk selection retains the existing unique 8 MiB VirtIO disk convention.
Missing or ambiguous candidates fail bootstrap rather than selecting an
arbitrary disk. This convention is not a general disk-identification mechanism.

The AArch64 UEFI entry path uses build-selected board device descriptions.
`UEFI_DEVICE_PLATFORM=pi_uefi` retains the physical Pi SD and Ethernet controllers.
The QEMU firmware targets select `UEFI_DEVICE_PLATFORM=qemu_virt`, advertising
QEMU's VirtIO MMIO bus without Pi-specific controllers. ACPI still supplies the
UART, interrupt controller, and PCI information, and firmware supplies the
framebuffer. This lets Mama select the attached VirtIO configuration disk before
starting the video service and login.
`hundeschau` registers as a service before waiting for video and keyboard
readiness; those waits require the caller to have service process class.

## Filesystem ownership

The filesystem subsystem in `hunde-io-host` starts with an empty namespace and serves requests immediately;
it does not read `fstab` or spawn drivers. Mount registration uses
`IO_REQUEST_MOUNT` over the existing bidirectional Channel. The namespace accepts
registration only from an authenticated sender whose kernel process-registry
record identifies the `hunde-mama` or `hundeschule` executable and the `hund`
group, not from identity fields supplied in the request payload.
Duplicate mountpoints are rejected. Adding the root mount does not replace more
specific `/boot`, `/boot/config`, or `/sbin` mounts.
An explicit root slot and mounted overlays belong to one `RootFilesystem` in
`filesystem/namespace.rs`. The standalone C namespace executable is no longer
built or started. Its endpoint name remains a compatibility identifier, not a
separate process.
The standalone C `hund-io-system` executable is also retired. Its Rust replacement
selects sockets, pipes, console/keyboard streams, or the filesystem namespace;
filesystem routing is a local call, not another Channel exchange. Initial
serial/framebuffer output selection comes from the existing boot hardware API,
before filesystem-dependent component loading.

`hundeschule` reads its configuration through normal I/O Channels, skips the
already-running boot block driver, and starts the remaining drivers. It reads
`fstab`, validates supported disk entries before any mount side effects, and
requires exactly one root with no duplicate device or mountpoint. It initializes
root first, even when another entry precedes it in fstab. For `ext2.wuff`, it
waits for hosted endpoint initialization before namespace publication and never
launches a legacy root watchdog. It then registers remaining disk mounts and
establishes `/tmp`, `/dev`, and `/proc` before launching `hundeschau`. Humpen
starts remaining legacy non-root disk filesystems later, skipping hosted entries.

Standard QEMU configurations select `ext2.wuff` for root and the secondary
`/mnt/channelio-ext2` and `/mnt/hosted-ext2` mounts. The host loads its
filesystem component from the already available `/sbin/components` archive;
neither component loading nor root mount control depends on reading the new root.
Mama's `/boot/config` mount also uses hosted `ext2.wuff`, independently of fstab.
The already registered `/sbin` supplies the component image; `/boot/config` and
the application root are not required for component loading. Mount-control
readiness, volume initialization, and namespace publication must all succeed
before `boot_complete()` and configured startup. `/sbin` and `/boot` retain
their existing providers. FAT32 mounts still use standalone providers. Pi UEFI
and Milk-V root/data fstab entries retain their legacy EXT2 selections pending
target-specific validation. Mama uses the
same hosted bootstrap mount request for VirtIO and SD partition devices.

Cold-boot root/shutdown, hosted filesystem/procfs, and combined socket/pipe/memfs
smoke checks pass on AArch64 HVF and RISC-V QEMU without a standalone C EXT2
process. Native tests cover VirtIO and SD-partition request ordering and failure
propagation; physical SD-platform acceptance remains separate.

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
Before userspace starts, the kernel renders the embedded `assets/logo.png` source
when a mapped framebuffer is available and otherwise writes `assets/logo.ascii`
through architecture basic I/O. Panic handling similarly replaces the display
with the embedded `assets/panic.png` source or writes `assets/panic.ascii` when a
framebuffer is unavailable. PNG conversion happens at build time, so neither path
depends on image decoding, filesystem access, allocation, or userspace services.

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
