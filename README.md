# Kernel Experiment

This repository contains an experimental AArch64 kernel, userspace programs, and
userspace drivers. Core kernel code is under `kernel/`, architecture-specific
code is under `arch/`, userspace code is under `user/`, and unit tests are under
`test/`.

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
