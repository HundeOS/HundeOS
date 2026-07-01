# Kernel Experiment

This repository contains an experimental AArch64 kernel, userspace programs, and
userspace drivers. Core kernel code is under `kernel/`, architecture-specific
code is under `arch/`, userspace code is under `user/`, and unit tests are under
`test/`.

## Developer Setup

Install the cross compiler, QEMU, Python, and FAT filesystem tools.

On macOS with Homebrew:

```sh
brew install aarch64-elf-gcc qemu dosfstools
```

On Linux:

```sh
sudo apt install gcc-aarch64-none-elf qemu-system-arm dosfstools python3
```

The build expects `mkfs.fat` from `dosfstools` when creating the raw virtio block
disk image.

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

## FAT32 Disk Image

QEMU attaches `build/disk.img` as a raw virtio block device. The Makefile creates
this as a 64 MiB FAT32 volume labelled `KERNELFS` and copies
`tools/disk_test_file.txt` into the root directory as `TEST.TXT`.

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
