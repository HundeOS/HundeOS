# Kernel Experiment

This repository contains an experimental AArch64 kernel, userspace programs, and
userspace drivers. Core kernel code is under `kernel/`, architecture-specific
code is under `arch/`, userspace code is under `user/`, and unit tests are under
`test/`.

## I/O Architecture

The common userspace I/O front end is `hund-io-system`. Sockets use its channel
connection to `socket_router`. Boot-configured FAT32 and EXT2 mounts use
`hund-io-system` -> `hund-dateisystem` -> filesystem driver -> block driver,
with a bidirectional Channel at every process boundary. This includes the EXT2
root disk at `/`: `hund-dateisystem` starts its userspace driver and waits for
the mount before boot proceeds to login. Nested mounts override root paths.
Only `/boot/config` retains kernel EXT2; `/boot/sysinfo`, consoles, and streams
remain kernel-backed. Embedded `/sbin` is served by userspace `embedfs` over
the same I/O channels. Memfs runs in userspace at `/tmp`,
started by `hund-dateisystem` before login on every target. It uses the same
I/O channels as disk filesystems, without a block device.
Devfs also runs in userspace, mounted at `/dev` before login on every target.
It reads the kernel-owned device registry through a read-only shared mapping;
device listings and block metadata use the existing filesystem channels.
Legacy stream opens explicitly retain their kernel route until stream migration.
The legacy `virtio_blk` driver is deleted; `virtio_blk_channelio` is the active
VirtIO block driver. Kernel EXT2 remains necessary for `/boot/config`.
Memfs records the creator's UID/GID and enforces file and directory permissions;
new files default to `0644`, directories to `0755`.
`/boot/sysinfo` provides kernel-generated hardware information and live statistics
for bootstrap drivers. `/proc` is now a separate read-only userspace filesystem
backed by the kernel's shared process registry.
See [Userspace I/O system](docs/io-system.md) for protocol semantics,
resource lifecycle, limits, and the migration plan.

The [bootstrap filesystem handoff](docs/bootstrap-filesystem-handoff.md) has
moved `/sbin` file operations to userspace. Embedded executable loading still
uses a direct kernel archive reader so bootstrap services can start without a
filesystem dependency. General channel-based executable loading remains pending.

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

Install the cross compiler, QEMU, uv, and FAT filesystem tools.

On macOS with Homebrew:

```sh
brew install aarch64-elf-gcc qemu dosfstools uv
```

On Linux:

```sh
sudo apt install gcc-aarch64-none-elf qemu-system-arm dosfstools
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
```

## Process CPU Usage

Userspace procfs exposes `/proc/<pid>/status`, `/cpu`, and `/memory` on every
target. `ps` reads the same shared records directly. Private RAM, shared RAM, and
MMIO are accounted separately; shared totals are per-process mappings, not unique
physical memory. See [Process registry and procfs](docs/process-registry-and-procfs.md).

The `CPU` column in `ps` is the zero-based ID of the CPU scheduler the process
is assigned to. Waiting processes retain their assignment; this column does
not imply that they are currently executing.

`ps` shows cumulative scheduled CPU time in seconds (`CPU TIME(s)`) and recent
CPU percentages (`CPU% (1s)` and `CPU% (100ms)`). A value of 100% means one fully
occupied core, not the whole machine. Time includes userspace execution and
kernel work while that process is scheduled; waiting and sleeping are excluded.

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
120-column target, and preserved include order. `tools/hundeformat.py` keeps
match branches on separate lines and preserves the placement of existing
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
