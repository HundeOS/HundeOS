# Milk-V Duo S

The Milk-V Duo S target uses the SG2000 SoC, 512 MiB of DRAM, and the vendor
FSBL, OpenSBI, and U-Boot SD boot chain. It is a separate platform from the
original 64 MiB Milk-V Duo CV1800B target.

## Boot Contract

The v1.1.4 vendor U-Boot loads `boot.sd` from the first FAT16 partition and
selects the FIT configuration `config-cv1813h_milkv_duos_sd`. The board SDK
uses the CV1813H board identifier for the Duo S firmware configuration even
though the production board carries an SG2000.

The stock U-Boot limits a kernel subimage's decompressed output to 8 MiB. The
HundeOS kernel is larger because it contains the bootstrap filesystem, so the
FIT uses U-Boot's standard loadable-image path to retain the stock firmware.
The HundeOS FIT contains:

- a small RV64 S-mode handoff loaded at `0x8d000000`;
- the LZMA-compressed kernel as a FIT loadable, decompressed at `0x84000000`;
- a Duo S device tree describing one RISC-V hart, 510 MiB of firmware-exposed
  RAM, the initial 512 KiB firmware reservation, UART0, the PLIC, the CV181x SD
  host, and the CVITEK RTC, loaded at `0x8c000000`; and
- the exact image and configuration node names used by the stock firmware.

The handoff copies the exact generated kernel image length from staging RAM to
`0x80200000`, preserves the firmware-provided hart ID and DTB pointer, executes
data and instruction fences, and enters the normal kernel `_start`. The chosen
addresses keep the final image, staging allocation, DTB, handoff,
FIT input, and U-Boot relocation regions disjoint. Build-time checks prevent
kernel growth or the loadable decompression allowance from crossing those
boundaries.

The kernel and userspace use the LP64 soft-float ABI and the RV64IMAC subset
supported by the C906 application core. The stock firmware exposes that core
to OpenSBI as the single application hart. The second RISC-V core, the C906L,
is owned by the firmware RTOS path and is not an available SMP hart under this
boot contract. Reclaiming it would require replacing or reconfiguring that
firmware path and defining how the two cores share memory and devices.

UART0 is a DW APB UART at `0x04140000`, with 32-bit registers and a register
shift of 2. It carries the early bootstrap diagnostics and the userspace console
at IRQ 44. On the Duo S J3 header, connect a 3.3 V UART adapter to pin 8
(UART0 TX), pin 10 (UART0 RX), and pin 6 (ground), crossing each board TX to
adapter RX and board RX to adapter TX.

After the architecture-neutral kernel handoff, kernel and driver syscall traces
move to UART1 at `0x04150000`, IRQ 45. Board preparation configures its baud
divisor and pads A19 and A18 before enabling address translation. Those
signals are J3 pin 18 (UART1 TX), pin 22 (UART1 RX), and pin 20 (ground).

The firmware passes the hart ID in `a0` and the selected DTB address in `a1`.

Build only the replacement FIT payload with:

```sh
make milkv-duo-s
```

The resulting payload is `build/riscv64/milkv_duo_s/boot.sd`.

## QEMU Approximation

The local CV1800B QEMU fork can provide a fast Duo S-shaped test loop without
writing an SD card:

```sh
make qemu-milkv-duo-s
```

This target boots the Duo S kernel and DTB with 510 MiB of RAM and attaches a
disposable 256 MiB copy of the complete Duo S SD image, padded to satisfy
QEMU's SD-card capacity requirement. The flashable image remains unchanged. It
uses an isolated `build/riscv64/milkv_duo_s_qemu` tree with neutral QEMU PTE
attributes. The CV1800B model does not implement UART1, so this build alone uses
UART0 for both kernel diagnostics and the userspace console; physical Duo S
builds use UART0 for the console and UART1 for kernel diagnostics. Its DTB
also describes the CV1800B model's CLINT so OpenSBI can provide timer services,
and identifies QEMU's standard SDHCI model as generic so the driver does not
access CV181x-only vendor registers.

The login process requests an abort report when it synchronously starts the
shell. If the child faults, the kernel returns the abort reason, exception code,
program counter, and fault address through that wait request. Login then prints
the report through the normal userspace console path on UART0; the kernel does
not bypass `uart_tx` to write process faults directly to the console UART.

This is not SG2000 emulation. The common RAM, UART0, PLIC, and SDHCI addresses
make it useful for kernel and userspace integration, but its CV1800B devices do
not validate CV181x-specific register behavior, UART1, pin multiplexing, or the
physical board's user-mode PLIC access restriction.

The current fork reaches generic SDHCI card initialization, partition
discovery, and the `/sbin`, `/boot`, and `/boot/config` mounts. It then stops
after starting the root `ext2fs` instance while QEMU repeatedly reports invalid
CSR opcodes on its own standard error stream. No guest process fault or kernel
panic is reported. Reaching login in this approximation remains unverified.

## SD Image

Build a complete flashable image with:

```sh
make milkv-duo-s-sd-image
```

The build downloads the official `milkv-duos-sd-v1.1.4.img.zip`, verifies its
SHA-256 digest, retains the stock MBR, `fip.bin`, and the 2 MiB raw logo region,
and replaces `boot.sd`. It installs an 8 MiB `BOOTCFG` ext2 filesystem as
partition two and relocates partition three to contain the 64 MiB
architecture-qualified HundeOS ext2 root filesystem. The resulting layout is:

1. the stock 128 MiB boot partition containing the vendor firmware and the
   HundeOS `boot.sd`;
2. the preserved 2 MiB raw logo region, which is not an MBR partition;
3. the 8 MiB `BOOTCFG` ext2 partition exposed to userspace as `/dev/mmc0p2`;
4. the 64 MiB root ext2 partition exposed as `/dev/mmc0p3`.

The output is:

```text
build/riscv64/milkv_duo_s/hundeos-milkv-duo-s.img
```

On macOS, identify the unmounted whole SD device with `diskutil list`, then
write it using the raw device corresponding to that disk number:

```sh
diskutil unmountDisk /dev/diskN
sudo dd if=build/riscv64/milkv_duo_s/hundeos-milkv-duo-s.img of=/dev/rdiskN bs=4m status=progress
sync
diskutil eject /dev/diskN
```

Verify `diskN` carefully before writing; `dd` overwrites the selected device.

## Current Boundary

Hardware has verified the stock FSBL, OpenSBI, U-Boot, FIT selection, FIT data
integrity, DTB selection, and the staged kernel handoff. That test also
confirmed the stock U-Boot 8 MiB kernel decompression limit; the staged
loadable handoff avoids that limit without replacing `fip.bin`.

When the boot hardware describes an SD host, `hunde-mama` now starts the
userspace `sdhci_channelio` service as `/dev/mmc0`, starts partition services
which validate the MBR and expose `/dev/mmc0p2` and `/dev/mmc0p3`, mounts
`/dev/mmc0p2` at `/boot/config`, and then lets `humpen` mount `/dev/mmc0p3` as
root according to `fstab`. This removes the previous dependency on a VirtIO
configuration disk. Hardware has verified CV181x card initialization, MBR
partition discovery, and the `/boot/config` mount. Completion of the root mount
and login through the split UART configuration remain hardware validation work.
The CVITEK RTC is described accurately but is not published as a Goldfish RTC
because its register interface requires a separate userspace driver.
