#include <stdint.h>
#include <stdbool.h>
#include "rasppi/io/gpio.h"
#include "rasppi/io/uart.h"
#include "../kernel/kernel.h"

//=============================================================================
// MMU Configuration Constants
//=============================================================================

// Page/Block sizes
#define PAGE_SHIFT          12
#define PAGE_SIZE           (1ULL << PAGE_SHIFT)        // 4KB
#define BLOCK_2MB_SHIFT     21
#define BLOCK_2MB           (1ULL << BLOCK_2MB_SHIFT)   // 2MB
#define BLOCK_1GB_SHIFT     30
#define BLOCK_1GB           (1ULL << BLOCK_1GB_SHIFT)   // 1GB

// Table configuration
#define TABLE_ENTRIES       512
#define TABLE_SIZE          PAGE_SIZE
#define NUM_GB             4    // Map 4GB of physical memory

//=============================================================================
// Page Table Entry Flags
//=============================================================================

// Entry type bits [1:0]
#define PTE_VALID           (1ULL << 0)   // Entry is valid
#define PTE_TYPE_TABLE      (3ULL << 0)   // Table descriptor (L0-L2)
#define PTE_TYPE_BLOCK      (1ULL << 0)   // Block descriptor (L1-L2)
#define PTE_TYPE_PAGE       (3ULL << 0)   // Page descriptor (L3)

// Access permissions
#define PTE_AP_RW_EL1       (0ULL << 6)   // RW at EL1, no access at EL0
#define PTE_AP_RW_ALL       (1ULL << 6)   // RW at EL1, RW at EL0
#define PTE_AP_RO_EL1       (2ULL << 6)   // RO at EL1, no access at EL0
#define PTE_AP_RO_ALL       (3ULL << 6)   // RO at EL1, RO at EL0
#define PTE_USER            (1ULL << 6)   // AP[1] - User accessible

// Access flag
#define PTE_AF              (1ULL << 10)  // Access Flag (required)

// Shareability
#define PTE_SH_NONE         (0ULL << 8)   // Non-shareable
#define PTE_SH_OUTER        (2ULL << 8)   // Outer shareable
#define PTE_SH_INNER        (3ULL << 8)   // Inner shareable

// Execute never
#define PTE_PXN             (1ULL << 53)  // Privileged execute never
#define PTE_UXN             (1ULL << 54)  // User execute never
#define PTE_XN              (PTE_PXN | PTE_UXN)  // Execute never at all levels

// Memory attributes - index into MAIR_EL1
#define PTE_ATTR_SHIFT      2
#define PTE_ATTR_MASK       (7ULL << PTE_ATTR_SHIFT)
#define PTE_ATTR(idx)       ((idx) << PTE_ATTR_SHIFT)

// MAIR indices
#define MAIR_IDX_NORMAL     0  // Normal cacheable memory
#define MAIR_IDX_DEVICE     1  // Device memory
#define MAIR_IDX_NC         2  // Normal non-cacheable

//=============================================================================
// Memory Attribute Configurations
//=============================================================================

// Memory types for MAIR_EL1 (8 bits per attribute)
#define MAIR_NORMAL_WB      0xFF  // Normal, Write-Back, RW allocate
#define MAIR_NORMAL_NC      0x44  // Normal, Non-Cacheable
#define MAIR_DEVICE_nGnRnE  0x00  // Device, strictest
#define MAIR_DEVICE_nGnRE   0x04  // Device, gathering allowed
#define MAIR_DEVICE_GRE     0x0C  // Device, most relaxed

//=============================================================================
// Page Tables (aligned and in special section)
//=============================================================================

__attribute__((aligned(TABLE_SIZE), section(".bss.page_tables")))
static uint64_t l1_table[TABLE_ENTRIES];

__attribute__((aligned(TABLE_SIZE), section(".bss.page_tables")))
static uint64_t l2_tables[NUM_GB][TABLE_ENTRIES];

// Track if MMU is enabled
static bool mmu_enabled = false;

//=============================================================================
// Helper Functions
//=============================================================================

// Create a table descriptor (points to next level table)
static inline uint64_t make_table_desc(uint64_t table_addr) {
    return table_addr | PTE_TYPE_TABLE;
}

// Create a block descriptor (2MB or 1GB mapping)
static inline uint64_t make_block_desc(uint64_t phys_addr, uint64_t attrs) {
    return phys_addr | attrs | PTE_AF | PTE_TYPE_BLOCK;
}

// Create a page descriptor (4KB mapping)
static inline uint64_t make_page_desc(uint64_t phys_addr, uint64_t attrs) {
    return phys_addr | attrs | PTE_AF | PTE_TYPE_PAGE;
}

// Clear a page table
static void clear_table(uint64_t *table) {
    for (int i = 0; i < TABLE_ENTRIES; i++) {
        table[i] = 0;
    }
}

//=============================================================================
// MMU Configuration
//=============================================================================

// Configure MAIR_EL1 (Memory Attribute Indirection Register)
static void configure_mair(void) {
    uint64_t mair = 0;
    
    // Index 0: Normal cacheable memory
    mair |= (uint64_t)MAIR_NORMAL_WB << (8 * MAIR_IDX_NORMAL);
    
    // Index 1: Device memory (strictest for real hardware)
    mair |= (uint64_t)MAIR_DEVICE_nGnRnE << (8 * MAIR_IDX_DEVICE);
    
    // Index 2: Normal non-cacheable (for QEMU MMIO compatibility)
    mair |= (uint64_t)MAIR_NORMAL_NC << (8 * MAIR_IDX_NC);
    
    __asm__ volatile("msr mair_el1, %0" : : "r" (mair));
}

// Configure TCR_EL1 (Translation Control Register)
static void configure_tcr(void) {
    uint64_t tcr = 0;
    
    // Virtual address space size (48-bit)
    tcr |= (16ULL << 0);        // T0SZ = 16 (48-bit addresses)
    
    // Translation granule
    tcr |= (0ULL << 14);        // TG0 = 4KB pages
    
    // Cacheability attributes for translation table walks
    tcr |= (1ULL << 8);         // IRGN0 = Write-Back, Write-Allocate
    tcr |= (1ULL << 10);        // ORGN0 = Write-Back, Write-Allocate
    
    // Shareability for translation table walks
    tcr |= (3ULL << 12);        // SH0 = Inner Shareable
    
    // Intermediate Physical Address Size
    tcr |= (2ULL << 32);        // IPS = 40 bits (1TB)
    
    __asm__ volatile("msr tcr_el1, %0" : : "r" (tcr));
}

// Configure SCTLR_EL1 (System Control Register)
static void configure_sctlr(bool enable_caches) {
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r" (sctlr));
    
    // Clear bits we want to control
    sctlr &= ~((1ULL << 0) |   // M - MMU enable
               (1ULL << 1) |    // A - Alignment check
               (1ULL << 2) |    // C - Data cache
               (1ULL << 12) |   // I - Instruction cache
               (1ULL << 19) |   // WXN - Write implies XN
               (1ULL << 44));   // DSSBS - Default SSBS
    
    // Set desired configuration
    sctlr |= (1ULL << 0);       // M - Enable MMU
    
    if (enable_caches) {
        sctlr |= (1ULL << 2);   // C - Enable data cache
        sctlr |= (1ULL << 12);  // I - Enable instruction cache
    }
    
    __asm__ volatile("msr sctlr_el1, %0" : : "r" (sctlr));
    __asm__ volatile("isb");
}

//=============================================================================
// Page Table Setup
//=============================================================================

// Initialize page tables with identity mapping
static void init_page_tables(void) {
    // Clear all tables
    clear_table(l1_table);
    for (int i = 0; i < NUM_GB; i++) {
        clear_table(l2_tables[i]);
    }
    
    // Set up L1 entries pointing to L2 tables
    for (int gb = 0; gb < NUM_GB; gb++) {
        l1_table[gb] = make_table_desc((uint64_t)l2_tables[gb]);
    }
    
    // Fill L2 tables with 2MB block mappings
    for (int gb = 0; gb < NUM_GB; gb++) {
        for (int mb = 0; mb < TABLE_ENTRIES; mb++) {
            uint64_t phys_addr = ((uint64_t)gb << 30) | ((uint64_t)mb << 21);
            uint64_t attrs = 0;
            
            // Determine memory type based on address
            if (phys_addr >= MMIO_BASE && phys_addr < (MMIO_BASE + 0x1000000)) {
                // MMIO region - use non-cacheable for QEMU compatibility
                attrs = PTE_ATTR(MAIR_IDX_NC) | PTE_XN;
            } else {
                // Normal memory - cacheable, executable
                attrs = PTE_ATTR(MAIR_IDX_NORMAL);
            }
            
            l2_tables[gb][mb] = make_block_desc(phys_addr, attrs);
        }
    }
}

//=============================================================================
// TLB Management
//=============================================================================

static inline void flush_tlb_all(void) {
    __asm__ volatile("tlbi vmalle1" ::: "memory");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}

static inline void flush_tlb_va(uint64_t va) {
    __asm__ volatile("tlbi vaae1, %0" :: "r"(va >> 12) : "memory");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}

//=============================================================================
// Public API
//=============================================================================

// Main MMU setup function
void setup_mmu(void) {
    if (mmu_enabled) {
        return;  // Already enabled
    }
    
    // Initialize page tables
    init_page_tables();
    
    // Configure MMU registers
    configure_mair();
    configure_tcr();
    
    // Set translation table base
    __asm__ volatile("msr ttbr0_el1, %0" : : "r" ((uint64_t)l1_table));
    
    // Ensure all writes complete
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
    
    // Enable MMU (without caches initially for safety)
    configure_sctlr(false);
    
    mmu_enabled = true;
    
    // Flush TLBs
    flush_tlb_all();
}

// Enable caches after MMU is working
void enable_caches(void) {
    if (!mmu_enabled) {
        return;  // MMU must be enabled first
    }
    
    configure_sctlr(true);
    
    // Invalidate instruction cache
    __asm__ volatile("ic iallu");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}

// Check if MMU is enabled
bool is_mmu_enabled(void) {
    return mmu_enabled;
}

// Switch to EL0 (user mode)
void switch_to_el0(void* user_entry, void* user_stack) {
    __asm__ volatile(
        "msr sp_el0, %0\n"
        "mov x0, #0x0\n"
        "msr spsr_el1, x0\n"
        "msr elr_el1, %1\n"
        "eret\n"
        : : "r" (user_stack), "r" (user_entry) : "x0"
    );
}