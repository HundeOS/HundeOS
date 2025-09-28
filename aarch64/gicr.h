#ifndef __AARCH64_GICR_H
#define __AARCH64_GICR_H


#define GICD_BASE   0x08000000UL
#define GICR_BASE   0x080A0000UL   // per-CPU redistributor frames, stride 0x20000
#define GICR_STRIDE 0x20000UL
#define GICR_COVER  0x08080000ULL   // 2 MiB block covering Redistributor frames (e.g. 0x080A0000)

#define GICD_CTLR   0x0000

// GICR offsets we need (RD_base + SGI_base 0x10000)
#define GICR_CTLR           0x0000
#define GICR_WAKER          0x0014
#define GICR_IGROUPR0       0x10080
#define GICR_ISENABLER0     0x10100
#define GICR_ICFGR1         0x10C04      // PPIs [31:16] config (edge/level)
#define GICR_IPRIORITYR(n) (0x10400 + (n)) // byte-per-int


#endif