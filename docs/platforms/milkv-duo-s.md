# Milk-V Duo S

The Milk-V Duo S target uses the SG2000 SoC, 512 MiB of DRAM, and the vendor
FSBL and U-Boot SD boot chain. Separate RISC-V and AArch64 targets select the
C906 or Cortex-A53 application core through the matching vendor firmware. It
is a separate platform from the original 64 MiB Milk-V Duo CV1800B target.

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
make milkv-duo-s-riscv
```

The resulting payload is `build/riscv64/milkv_duo_s/boot.sd`.

## AArch64 Target

The AArch64 target uses the official v2.0.1 ARM64 firmware contract. Its FIT
keeps the vendor names `kernel-1`,
`fdt-sg2000_milkv_duos_glibc_arm64_sd`, and
`config-sg2000_milkv_duos_glibc_arm64_sd`. A small AArch64 handoff at
`0x8d000000` copies the staged kernel from `0x84000000` to the vendor kernel
entry address `0x80108000`, preserving the firmware arguments.

Build the ARM64 FIT payload with:

```sh
make milkv-duo-s-aarch64
```

The output is `build/aarch64/milkv_duo_s/boot.sd`. Build a complete SD image
based on the checksum-pinned official ARM64 image with:

```sh
make milkv-duo-s-sd-image-aarch64
```

The AArch64 platform describes the Cortex-A53, 510 MiB firmware-visible RAM,
DW APB UART0 and UART1, GICv2 distributor and CPU interface, and CV181x SDHCI
controller. Early output and the kernel UART implementation understand the
board's 32-bit, register-shift-two NS16550 layout. Kernel trace, including early
boot output before the kernel handoff, starts on UART0. After the handoff, the
userspace console remains on UART0 and kernel diagnostics move to UART1.

The common AArch64 interrupt path selects GICv2 memory-mapped acknowledge and
completion operations for this platform. Timer setup also enables the GICv2
per-CPU interrupt through the distributor. These implementations live in
`arch/aarch64/io/gicd.c`, `arch/aarch64/exception.c`, and
`arch/aarch64/cpu_timer.c`; GICv3 targets retain their system-register path.

## QEMU Approximation

The local CV1800B QEMU fork can provide a fast Duo S-shaped test loop without
writing an SD card:

```sh
make qemu-milkv-duo-s-riscv
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
program counter, and fault address through that wait request. Login prints the
report through the userspace console. Fatal process diagnostics are also copied
to the kernel console sink, independently of userspace UART service progress;
see [driver tracing](../architecture/userspace/driver-tracing.md#kernel-trace-snapshots).

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
make milkv-duo-s-sd-image-riscv
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

## Hardware Serial Debugging

Use two 3.3 V USB UART adapters to observe the console and kernel trace at the
same time. Connect UART0 using J3 pins 8 (board TX), 10 (board RX), and 6
(ground). Connect UART1 using J3 pins 18 (board TX), 22 (board RX), and 20
(ground). Cross each board TX to adapter RX and board RX to adapter TX. Do not
connect an adapter voltage pin to the board.

On macOS, list the serial adapters after connecting them:

```sh
ls -1 /dev/cu.usbserial-* /dev/cu.usbmodem-* 2>/dev/null
```

If the adapters have indistinguishable names, unplug one and run the command
again to identify it. Record which device is connected to UART0 and UART1.

Open one terminal for the UART0 console:

```sh
picocom --baud 115200 --flow n --parity n --databits 8 /dev/cu.usbserial-UART0
```

Open a second terminal for the UART1 kernel trace:

```sh
picocom --baud 115200 --flow n --parity n --databits 8 /dev/cu.usbserial-UART1
```

Start both sessions before inserting the flashed SD card and powering or
resetting the board. UART0 first shows firmware and early bootstrap output,
then becomes the interactive userspace console. UART1 begins receiving kernel
diagnostics after the architecture-neutral handoff. Exit `picocom` with
Ctrl-A followed by Ctrl-X.

Both Milk-V Duo S images enable `KERNEL_MEMORY_TRACE`. Post-handoff kernel
trace is retained in a bounded memory ring; the AArch64 image also sends it to
the trace UART. With only the UART0 console adapter connected, inspect the newest
12 KiB after login with:

```sh
cat /proc/ktrace
```

Opening `/proc/ktrace` takes a non-destructive snapshot; repeated reads do not
drain the kernel ring. Early bootstrap output before the architecture-neutral
kernel takes over is UART-only.

The normal AArch64 image deliberately leaves `KERNEL_COMMAND_TRACE` disabled.
Command-flow records are written synchronously to UART1 from scheduler and
syscall paths. At 115200 baud, a roughly 150-byte record stalls that path for
about 13 ms and materially changes single-core IPC latency.

For scheduler and IPC latency investigation, build the isolated diagnostic
image instead:

```sh
make milkv-duo-s-sd-image-aarch64-command-trace
```

It writes
`build/aarch64/milkv_duo_s_command_trace/hundeos-milkv-duo-s.img` and does not
share kernel objects with the normal image. Reproduce one delayed external
command and preserve the UART1 `COMMAND_FLOW` records from before the command
through the Enter-key event. Their microsecond timestamps distinguish a missing
notification from a process that became runnable but was dispatched late. Do
not use the diagnostic image for performance comparisons.

## Implementation And Validation

Milk-V Duo S has been successfully tested on physical hardware. This is separate
from the CV1800B QEMU approximation, whose root-startup stall remains unresolved.
The source tree alone cannot establish hardware acceptance for every peripheral
or both CPU modes; retain the architecture, image, and UART evidence when
recording additional hardware results.

The recorded RISC-V bring-up checkpoint verified the stock FSBL, OpenSBI,
U-Boot, FIT selection, FIT data
integrity, DTB selection, and the staged kernel handoff. That test also
confirmed the stock U-Boot 8 MiB kernel decompression limit; the staged
loadable handoff avoids that limit without replacing `fip.bin`.

When the boot hardware describes an SD host, `hunde-mama` starts partition
services which validate the MBR and expose `/dev/mmc0p2` and `/dev/mmc0p3`,
mounts `/dev/mmc0p2` at `/boot/config`, and then lets `humpen` mount
`/dev/mmc0p3` as root according to `fstab`. The AArch64 GIC path hosts the
CV181x `sdhci.wuff` inside `hunde-treiber-host`; the RISC-V PLIC path uses
the same Wuff and passes physical root reads, login, and IRQ-delivery checks.
Neither path depends on a VirtIO configuration disk.

The physical AArch64 image now reaches an interactive `bash-5.3$` prompt on
UART0. `ps` shows `hunde-treiber-host` and both partition services, with no
standalone SDHCI process. `/dev/mmc0` reports 512-byte blocks and 122265600
sectors; `/boot/config/fstab` names `/dev/mmc0p3` as root; `/hello.txt` reads
`hello from ext2`; and `/proc/ktrace` records `Rust SDHCI IRQ delivered` before
the ext2 root is ready. This validates the Rust CV181x read and IRQ path on
the AArch64 board. The RISC-V board separately passed the same SDHCI read and
IRQ checks; independent UART1 tracing remains unverified.
The CVITEK RTC is described accurately but is not published as a Goldfish RTC
because its register interface requires a separate userspace driver.

Both AArch64 and RISC-V FIT and SD-image targets are implemented. AArch64 also
has GICv2 interrupt handling and an isolated command-trace image. The older
bring-up checkpoints above describe their recorded validation boundaries;
they must not be read as evidence that the corresponding code is absent.
