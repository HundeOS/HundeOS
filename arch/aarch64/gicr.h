#ifndef __AARCH64_GICR_H
#define __AARCH64_GICR_H

#define GICR_OFFSET 0xA0000UL
#define GICR_COVER_OFFSET 0x80000ULL

#define GICR_STRIDE         0x20000UL

#define GICD_CTLR_OFFSET        0x0000

// GICR offsets we need (RD_base + SGI_base 0x10000)
#define GICR_CTLR_OFFSET           0x0000
#define GICR_WAKER_OFFSET          0x0014
#define GICR_IGROUPR0_OFFSET       0x10080
#define GICR_ISENABLER0_OFFSET     0x10100
#define GICR_ICFGR1_OFFSET         0x10C04      // PPIs [31:16] config (edge/level)
#define GICR_IPRIORITYR_OFFSET(n)  (0x10400 + (n)) // byte-per-int


#endif
