# Shared clock page

HundeOS exposes monotonic time without a syscall through one kernel-owned page
mapped read-only and non-executable into every process at `0x72000000`. The page
contains clock conversion parameters, not a timestamp updated on every timer
interrupt. Userspace reads the architecture counter and converts it using the
published timer frequency.

The kernel owns the only writable mapping. Publication uses an even/odd sequence:
the writer makes the sequence odd, updates the record, then publishes the next
even sequence with release ordering. Readers accept a snapshot only when the
sequence is unchanged and even and the magic and version match. This lets the
kernel update clock parameters later without readers observing a torn record.

`clock_gettime(CLOCK_MONOTONIC)` reads `cntvct_el0` and its architectural
frequency on AArch64. RISC-V reads `time` and uses the boot timer frequency from
the page.

The page can also contain an optional pair of microsecond timestamps: Unix time
read from a wall-clock source and monotonic time sampled around that read. Boot
uses the midpoint of the two surrounding monotonic counter reads. Userspace
implements `gettimeofday` by advancing the Unix sample by the monotonic time
elapsed since that pair was captured. It does not need a syscall or repeatedly
access an RTC register.

QEMU AArch64 reads its PL031 RTC, and QEMU RISC-V discovers and reads its
Goldfish RTC from the device tree. The Raspberry Pi UEFI loader uses
`EFI_RUNTIME_SERVICES.GetTime` before `ExitBootServices` and hands the paired
sample to the kernel. A discovered MMIO RTC is also described in `/boot/sysinfo`;
the UEFI firmware sample does not claim that an RTC device is directly
available.

If boot cannot obtain a valid wall-clock sample, the realtime fields remain
absent and `gettimeofday` fails with `ENOSYS` after zeroing the supplied result.
The initial sample is not clock discipline: HundeOS does not yet support later
RTC corrections, NTP synchronization, or protection against wall-clock drift.

Per-process user and system CPU accounting is separate from this page. The
shared clock supplies elapsed time but does not implement the CPU fields needed
by `times()`.
