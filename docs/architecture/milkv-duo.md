# Milk-V Duo

The first HundeOS physical RISC-V target is the original Milk-V Duo with the
CV1800B SoC and 64 MiB of DRAM. It uses the board's vendor FSBL, OpenSBI, and
U-Boot firmware rather than replacing the mask-ROM and DRAM initialization
path.

## Boot Contract

The vendor U-Boot loads `boot.sd` from the first FAT partition into its
15 MiB image buffer and selects the FIT configuration
`config-cv1800b_milkv_duo_sd`. The HundeOS FIT contains:

- an LZMA-compressed RV64 S-mode kernel loaded at `0x80200000`;
- a minimal CV1800B device tree describing 64 MiB RAM, hart 0, the PLIC, UART0,
  and the SD host; and
- the exact configuration name selected by the stock firmware.

The kernel and userspace use the LP64 soft-float ABI and RV64IMAC without
`Zawrs`. UART0 is a DW APB UART at `0x04140000`, with 32-bit registers and a
register shift of 2.
The firmware must pass the hart ID in `a0` and the selected DTB address in `a1`.

Build the image with:

```sh
make milkv-duo
```

The resulting payload is `build/riscv64/milkv_duo/boot.sd`. The current full image
occupies about 38.4 MiB in RAM and compresses to about 4.54 MiB in the FIT,
within the stock U-Boot 15 MiB image buffer.

To produce a complete image that can be written directly to an SD card, run:

```sh
make milkv-duo-sd-image
```

This downloads the pinned official Milk-V Duo v1.1.4 image, verifies its
SHA-256 digest, retains its MBR and 128 MiB FAT16 boot partition with `fip.bin`,
replaces `boot.sd`, and appends the architecture-qualified HundeOS ext2 root as
partition two. The result is
`build/riscv64/milkv_duo/hundeos-milkv-duo.img`. The image is deliberately only
as large as the two populated partitions; an SD card may be larger.

The smaller firmware and UART checkpoint remains available separately:

```sh
make milkv-duo-bringup
```

It produces `build/riscv64/milkv_duo/bringup.sd`. At 115200 baud, a successful
checkpoint prints:

```text
HundeOS Milk-V Duo bring-up
S-mode entry=0x0000000080200000 hart=0x0000000000000000 dtb=...
DTB handoff valid
Milk-V Duo platform entry passed
```

## QEMU CV1800B Test Loop

The local `qemu-milkv-duo` build can run the original Duo platform without
writing an SD card:

```sh
make qemu-milkv-duo
```

The target defaults to the local QEMU build and OpenSBI firmware at
`~/src/qemu-milkv-duo`. Override `QEMU_MILKV_DUO` or `QEMU_MILKV_DUO_BIOS` when
they live elsewhere. It builds a separate `build/riscv64/milkv_duo_qemu` tree,
boots its kernel ELF with the original Duo DTB, and attaches its ext2 root as
the emulated SD card.

QEMU's C906 model does not implement the CV1800B vendor PTE attribute bits, so
this target uses neutral PTE attributes only in its isolated build. It is useful
for bootstrap, SDHCI, PLIC, UART, and userspace integration, but it does not
replace Duo S hardware testing or validate the CV181x user-mode PLIC-store
workaround.

## Implemented Platform

The full target shares the architecture boot, device-tree, trap, and
`SystemInfo` implementation with QEMU while selecting its own startup, linker,
and MMIO configuration. It reserves two 1 MiB boot stacks rather than QEMU's
multi-hart stack area, parses the 63.25 MiB usable-memory range, publishes the
CV1800B PLIC, word-spaced UART, and SD host, and leaves QEMU-only RTC, VirtIO,
and PCI devices absent.

## Remaining Integration

The payload and complete SD-card artifact can be verified locally. SD partitions
still need to be exposed through the existing userspace block and filesystem
protocols before the board can mount its boot configuration and root filesystem.
Hardware UART evidence is then required for kernel handoff, timer and interrupt
operation, userspace bootstrap, SD I/O, login, and Bash.
