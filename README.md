# HundeOS

HundeOS is an experimental microkernel operating system with full-system
AArch64 and RISC-V QEMU targets. Core kernel code is under `kernel/`,
architecture-specific integration is selected from `arch/`, userspace services,
commands, and drivers are under `user/`, and tests are under `test/`.

The privileged kernel owns scheduling and process lifecycle, address spaces and
memory protection, IPC foundations, hardware authority, interrupt entry and
routing, and the minimum boot and diagnostic path. Normal I/O is implemented in
userspace: filesystem routing, file handles, filesystems, block and input
drivers, console services, sockets, and system-information filesystems exchange
requests through authenticated Channels and managed shared memory. There is no
normal file-I/O syscall fallback into a kernel filesystem.

See [Goals](docs/GOALS.md) for project direction, [TODO](docs/TODO.md) for
granular work and open design questions, and the
[architecture documents](docs/architecture/) for the current system design.

## Project Assets

The [`assets`](assets/) directory contains project-owned artwork and other
non-generated media. At system launch the kernel displays the HundeOS logo as a
PNG-derived framebuffer image, or uses its ASCII counterpart when no framebuffer
is available. Kernel panic follows the same policy with the panic artwork. These
files are source assets rather than build output and must be preserved by cleanup
commands. See the [asset inventory](assets/README.md) for each file's purpose.

## Supported Architectures

AArch64 and RV64 both run the complete userspace service graph on four virtual
CPUs, reach an interactive login, and are covered by traced QEMU smoke tests.
The AArch64 target supports HVF and TCG configurations. RV64 runs on QEMU
`virt` through OpenSBI in supervisor mode with Sv39. AMD64 support is a project
goal but is not yet implemented.

## I/O Architecture

The [shared-buffer architecture](docs/architecture/shared-buffers.md) uses
`hunde-puffer` to manage and recycle directional bulk buffers at common virtual
addresses. Existing Channels carry opens, buffer descriptors, transfer commands,
and completions. File, executable, and block I/O use 2 MiB directional leases;
the initial pool retains up to four detached buffers before hard freeing.
Remaining policy and recovery work is tracked in [TODO](docs/TODO.md).

[`hunde-mama`](docs/architecture/bootstrap-services.md) brings up the userspace filesystem,
`/sbin`, `/boot`, and `/boot/config` before `hundeschule` reads startup configuration.

`hundebus` keeps its PCI inventory in userspace and serves read-only
[bus-information queries](docs/architecture/bus-info.md) over Channel. The former
`/dev/businfo` kernel stream is retired; the planned `sysinfofs` presentation
and hotplug monitoring are tracked in [TODO](docs/TODO.md).

The common userspace I/O entry point is `hund-io-system`. It redirects clients
to final socket and stream services, or through `hund-dateisystem` for mount
selection. Clients then use a direct Channel to `socket_router` or the selected
filesystem driver; filesystem drivers use another Channel to their block driver.
This includes the EXT2
root disk at `/`: `hundeschule` starts its userspace driver and waits for
the mount before boot proceeds to login. Nested mounts override root paths.
`/boot/config` uses userspace EXT2; `/boot/sysinfo` is rendered by userspace `sysinfofs`.
Console and UART I/O use `hund-io-system` channels to userspace `uart_rx`,
`uart_tx`, and `video_term`. Embedded `/sbin` is served by userspace `embedfs` over
the same I/O channels. Memfs runs in userspace at `/tmp`,
started by `hundeschule` before login. It uses the same
I/O channels as disk filesystems, without a block device.
Devfs also runs in userspace, mounted at `/dev` before login on every target.
It reads the kernel-owned device registry through a read-only shared mapping;
device listings and block metadata use the existing filesystem channels.
Keyboard input uses the userspace stream service. Kernel boot, panic, and
diagnostic output remain independent of the userspace console services.
The legacy `virtio_blk` driver is deleted; `virtio_blk_channelio` is the active
VirtIO block driver, including for `/boot/config`.
Memfs records the creator's UID/GID and enforces file and directory permissions;
new files default to `0644`, directories to `0755`.
`/boot/sysinfo` renders a typed kernel hardware snapshot in userspace.
`/proc` is a separate read-only userspace filesystem
backed by the kernel's shared process registry.
See [Userspace I/O system](docs/architecture/io-system.md) for protocol semantics,
resource lifecycle, limits, and remaining work.

The [bootstrap filesystem handoff](docs/architecture/bootstrap-filesystem-handoff.md) has
moved `/sbin` file operations and normal executable reads to userspace Channels.
Only Mama's `boot_spawn()` uses the direct archive reader; `boot_complete()`
irreversibly disables it after the basic mounts are established. The kernel
retains ELF validation and process creation. Executables transfer through shared
buffers. The disk rootfs contains pinned, unmodified OpenBSD file and text
commands under `/usr/local/bin`, including `cat`, `cp`, `grep`, `ln`, `ls`,
`mkdir`, `mv`, `rm`, and `tail`. They are built against the userspace C/POSIX
compatibility layer; see the handoff document for current permission limits.

IPC supports 1024 live queue connections system-wide, with 32 messages per
direction. A Channel currently retains two queue connections through its manager.
The service directory and listener table each support 1024 entries. Listener
arrivals occupy three read-only pages per listener.

Try local socket communication in the shell:

```sh
socket_test listen &
socket_test connect
```

## Developer Setup

Install the cross compiler, QEMU, uv, Rustup, ImageMagick, and FAT filesystem tools. The
repository's `rust-toolchain.toml` installs the pinned minimal Rust toolchain and
built-in bare-metal targets used by loadable components.

On macOS with Homebrew:

```sh
brew install aarch64-elf-gcc riscv64-elf-gcc riscv64-elf-binutils llvm lld qemu dosfstools uv imagemagick
```

On Linux:

```sh
sudo apt install gcc-aarch64-none-elf gcc-riscv64-unknown-elf clang lld qemu-system-arm qemu-system-misc dosfstools imagemagick
curl -LsSf https://astral.sh/uv/install.sh | sh
```

The build expects `mkfs.fat` from `dosfstools` when creating the raw virtio block
disk image. Python tooling is managed from `pyproject.toml`; run `make
python-venv` to create or update the local `.venv`, or use any make target that
invokes Python and it will sync the environment first.

## Build And Test

Build the kernel and userspace binaries:

```sh
make kernel
```

Target artifacts are separated first by architecture and then by platform. The
default AArch64 kernel is written to `build/aarch64/qemu_virt`; its userspace
binaries are under `build/aarch64/user`.

Run unit tests:

```sh
make test
```

Run in QEMU:

```sh
make qemu-virt
```

Useful variants:

```sh
make qemu-virt-1cpu
make qemu-virt-2cpus
make qemu-virt-1cpu-trace
make qemu-virt-2cpus-trace
make qemu-virt-tcg-icount-trace
```

Build the RV64 OpenSBI bring-up checkpoint:

```sh
make riscv64-bringup
```

Run the full RV64 system with ordinary or file-backed kernel tracing:

```sh
make qemu-riscv64
make qemu-riscv64-trace
```

The RISC-V kernel is under `build/riscv64/qemu_virt`. The trace target keeps
userspace I/O on the interactive NS16550 console and writes kernel diagnostics
to `build/riscv64/kernel-trace.log` through QEMU semihosting. The full-system
targets run four harts, use the `1G` RAM window mapped by the current bootstrap,
and attach the root, boot configuration, test, SD, and two Channel I/O disk
images described by `config/qemu_riscv64`.

`qemu-virt-tcg-icount-trace` exposes four guest CPUs but uses single-threaded
TCG with `-icount shift=auto,sleep=on`. This makes guest `WFE` waits suspend
under QEMU while retaining the four-CPU scheduler topology. It is intended for
wait and scheduling traces, not parallel-throughput measurements.

## Process CPU Usage

Userspace procfs exposes `/proc/<pid>/status`, `/cpu`, and `/memory` on every
target. `ps` reads the same shared records directly. Private RAM, shared RAM, and
MMIO are accounted separately; shared totals are per-process mappings, not unique
physical memory. See [Process registry and procfs](docs/architecture/process-registry-and-procfs.md).

The `CPU` column in `ps` is the zero-based ID of the CPU scheduler the process
is assigned to. Waiting processes retain their assignment; this column does
not imply that they are currently executing.

`ps` shows cumulative active CPU time in seconds (`CPU TIME(s)`) and recent CPU
percentages (`CPU% (1s)` and `CPU% (100ms)`). A value of 100% means one fully
occupied core, not the whole machine. Active time includes userspace execution
and kernel work while that process is scheduled, then subtracts published
userspace wait growth that overlaps the scheduled slice. Kernel-parked time is
also excluded. The unadjusted scheduled residency remains available through
`/proc/<pid>/cpu` as `scheduled_usec` and `SCHED CPU%`.

`USER SLEEP(s)` separately reports cumulative wall time inside published userspace
queue waits, including an in-flight interval that has been woken but not yet
closed by userspace. It is advisory diagnostic time; only its overlap with a
scheduled slice is subtracted from the active counters.

The recent windows cover the last 1,000 and 100 completed 1 ms clock ticks
(N = 100), respectively. These use the monotonic architecture clock rather than
counting scheduler calls. The incomplete current tick is excluded from the
percentages but included in cumulative time. Windows use their full duration
even for newly created processes, and sleeping processes' recent usage decays
to zero. Accounting is retained in the kernel, so no previous `ps` call is needed.

## Code Formatting

Format all project C sources and headers:

```sh
make format
```

Check formatting without modifying files:

```sh
make format-check
```

Both targets use clang-format 22.1.0, downloaded automatically through `uv`.
The base style is defined in `.clang-format`: four spaces, Allman braces, a
120-column target, preserved include order, and blank lines between function
definitions and major type definitions such as structures and enumerations.
Macro-expansion rules put
`critical_section` and `critical_section_try` block braces on separate lines,
including single-statement bodies. `tools/hundeformat.py` aligns critical-section
arguments and blocks one indentation level inside the call, with a separate
closing `);`, like match macros. The same layout applies to all `for_each_*`
iteration macros (linked lists, fixed and atomic arrays, maps, ring buffers,
and BStrings), with each argument on its own line.
Match branch names, braces, and bodies occupy
separate lines, even for single statements; no-op branch helpers remain compact.
The formatter preserves the placement of existing
line-scoped lint annotations. Those annotations can exceed the column target.
Use the make targets for the final formatting pass; plain editor clang-format
does not apply these additional conventions.

Vendored code, build output, generated Unity test runners, and the generated
emoji atlas header are excluded. Run `make format-test` to test the formatter.

## FAT32 Disk Image

QEMU attaches `build/disk.img` as a raw virtio block device. The Makefile creates
this as a 64 MiB FAT32 volume labelled `KERNELFS` and copies
`tools/disk_images/disk_test_file.txt` into the root directory as `TEST.TXT`.

Create or recreate the disk image:

```sh
make disk
```

The image is formatted with:

```sh
mkfs.fat -F 32 -S 512 -s 1 -R 32 -n KERNELFS build/disk.img
```

Then `tools/populate_fat32_image.py` writes the test file into the FAT32 root
directory without requiring the image to be mounted.

The one-sector cluster size is intentional because the current FAT32
implementation validates and uses one 512-byte sector per cluster.

## Cleaning

Remove generated build artifacts:

```sh
make clean
```
