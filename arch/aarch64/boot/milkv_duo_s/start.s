// The stock SG2000 ARM firmware enters the FIT kernel through the same
// AArch64 firmware-entry ABI used by the UEFI platform: x0-x3 are firmware
// arguments and execution may begin at EL1, EL2, or EL3.
.include "arch/aarch64/boot/pi_uefi/start.s"
