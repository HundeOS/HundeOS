# Running HundeOS In QEMU

Run commands from the repository root after completing the
[developer setup](../../README.md#developer-setup). The Make targets build the kernel,
userspace, and required disk images before launching QEMU.

## AArch64

```sh
make qemu-virt
make qemu-virt-hvf
```

`qemu-virt` uses the QEMU `virt` machine with four CPUs and 4 GiB of RAM.
`qemu-virt-hvf` uses Apple's Hypervisor Framework and the host CPU; it requires
an Apple Silicon Mac. Each selects its matching startup configuration under
`config/qemu_virt` or `config/qemu_virt_hvf`.

Additional CPU-count and diagnostic variants:

```sh
make qemu-virt-1cpu
make qemu-virt-2cpus
make qemu-virt-1cpu-trace
make qemu-virt-2cpus-trace
make qemu-virt-hvf-trace
make qemu-virt-tcg-icount-trace
```

`qemu-virt-tcg-icount-trace` exposes four guest CPUs but uses single-threaded
TCG with `-icount shift=auto,sleep=on`. This lets guest `WFE` waits suspend
under QEMU while retaining the four-CPU scheduler topology. Use it for wait and
scheduling investigation, rather than parallel-throughput measurements.

## RISC-V

```sh
make qemu-riscv64
make qemu-riscv64-trace
```

These run the complete RV64 system on QEMU `virt` through OpenSBI in supervisor
mode with Sv39, four harts, and 1 GiB of RAM. QEMU must support the configured
`rv64,zawrs=true` CPU. Startup configuration is under `config/qemu_riscv64`.
The trace target keeps the NS16550 userspace console interactive and sends
kernel diagnostics to a file through semihosting.

`make riscv64-bringup` builds a separate early platform checkpoint, rather than
the complete interactive system.

## Artifacts And Tracing

Default kernel artifacts live under `build/<arch>/qemu_virt`, userspace binaries
under `build/<arch>/user`, and disk images under `build/<arch>/images`.
Boot configuration images live under `build/<arch>/boot_config`.

The HVF and RISC-V trace targets write `build/aarch64/kernel-trace.log` and
`build/riscv64/kernel-trace.log`, respectively. Trace targets select separate
kernel objects where needed for their diagnostic configuration.

For Milk-V emulator approximations, which require a separate QEMU fork, see
the [Duo](milkv-duo.md) and [Duo S](milkv-duo-s.md)
board guides. Those runs do not replace physical hardware tests.

## Disk Images

The QEMU targets attach a root EXT2 image, a boot-configuration EXT2 image,
and additional FAT32/EXT2 test volumes. The HVF and RISC-V configurations also
attach an SD-card test volume. Device ordering and mountpoints are specified by
the matching `config/<platform>/fstab`; consult that file when changing disks.

Image creation requires `mkfs.fat` from `dosfstools` and an EXT2 builder.
The Makefile defaults to `genext2fs`; `EXT2_MKFS` can select `mke2fs`,
`mkfs.ext2`, or `e2fsdroid` instead.

### FAT32 Test Image

The main FAT32 test image is `build/<arch>/images/disk.img`, a 64 MiB volume
labelled `KERNELFS`. Recreate it with:

```sh
make disk
make ARCH=riscv64 disk
```

This reformats the selected generated image and populates `TEST.TXT`,
`This is a long filename.txt`, and `BIGFILE.TXT` from `tools/disk_images/`.
The formatting parameters are `-F 32 -S 512 -s 1 -R 32 -n KERNELFS`:
512-byte sectors, one sector per cluster, and 32 reserved sectors.
`tools/populate_fat32_image.py` writes the fixtures without mounting the image.
This image is distinct from the root disk and the mounted Channel I/O FAT32
test volume, `channelio-fat32.img`.

## Smoke And Integration Tests

```sh
make smoke-test
uv run python -m pytest test/smoke -v
uv run python -m pytest test/smoke -v -k riscv64
```

The pytest suite in `test/smoke/test_qemu.py` launches Make targets through a
pseudo-terminal, waits for login, runs integration commands, and checks console
output and kernel traces. Coverage includes external commands, nested forks,
procfs, hosted filesystems, sockets/pipes, and shutdown.
The parametrized AArch64 cases use `qemu-virt-hvf-trace`; RISC-V cases use
`qemu-riscv64-trace`. The single-CPU external-command test uses `qemu-virt-1cpu`.

Run tests serially because they share image and trace paths. The harness
recreates the root EXT2 image before starting each session; do not rely on
test images to preserve guest data. Failures include console and trace tails
in pytest output. Inspect the complete trace files for further diagnosis.
