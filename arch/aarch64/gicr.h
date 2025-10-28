#ifndef __AARCH64_GICR_H
#define __AARCH64_GICR_H

#define GICR_OFFSET 0xA0000UL
#define GICR_COVER_OFFSET 0x80000ULL

#define GICD_PHYS_BASE   0x08000000UL
#define GICR_PHYS_BASE   GICD_PHYS_BASE + GICR_OFFSET
#define GICR_PHYS_COVER  GICD_PHYS_BASE + GICR_COVER_OFFSET

extern const uint64_t __gicd_start[];

#define GICD_BASE           (uint64_t)&__gicd_start // virtual base of GICD
#define GICR_BASE           (GICD_BASE + GICR_OFFSET) // per-CPU redistributor frames, s
#define GICR_COVER          (GICD_BASE + GICR_COVER_OFFSET)   // 2 MiB block covering Redistributor frames (e.g. 0x080A0000)

#define GICR_STRIDE         0x20000UL

#define GICD_CTLR_OFFSET        0x0000
#define GICD_CTLR               (GICD_BASE + 0x000)
#define GICD_ISENABLER1         (GICD_BASE + 0x104)
#define GICD_IROUTER(n)         (GICD_BASE + 0x6000 + (8 * (n)))
#define GICD_IGROUPR(n)  (*(volatile uint32_t *)(GICD_BASE + 0x080 + (n) * 4))

#define GICR_WAKER(cpu)         (volatile uint32_t *)(GICR_BASE + 0x14 * cpu)


// GICR offsets we need (RD_base + SGI_base 0x10000)
#define GICR_CTLR_OFFSET           0x0000
#define GICR_WAKER_OFFSET          0x0014
#define GICR_IGROUPR0_OFFSET       0x10080
#define GICR_ISENABLER0_OFFSET     0x10100
#define GICR_ICFGR1_OFFSET         0x10C04      // PPIs [31:16] config (edge/level)
#define GICR_IPRIORITYR_OFFSET(n)  (0x10400 + (n)) // byte-per-int


#endif