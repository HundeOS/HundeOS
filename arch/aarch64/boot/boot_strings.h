#ifndef __AARCH64_BOOT_STRINGS_H_
#define __AARCH64_BOOT_STRINGS_H_
#include "boot.h"

__BOOT_STRING__(booting) = "Kernel booting\n";
__BOOT_STRING__(secondary_booting) = "Kernel booting cpu: ";
__BOOT_STRING__(secondary_boot) = "Secondary is online: ";
__BOOT_STRING__(enabling_mmu) = "Enabling MMU\n";
__BOOT_STRING__(mmu_enabled) = "MMU is enabled\n";
__BOOT_STRING__(hypervisor_call) = "Calling hypervisor\n";
__BOOT_STRING__(post_hypervisor_call) = "Finished calling hypervisor\n";
__BOOT_STRING__(current_el) = "CurrentEL=";

__BOOT_STRING__(new_line) = "\n";
__BOOT_STRING__(sep) = "\n0x";

__BOOT_STRING__(new_table_message) = "new table\n";
__BOOT_STRING__(existing_table) = "existing table\n";
__BOOT_STRING__(new_table_init) = "new table initialised\n";
__BOOT_STRING__(bad_descriptor) = "table descriptor is wrong\n";
__BOOT_STRING__(out_of_pages) = "out of table pages\n";
__BOOT_STRING__(message) = "here\n";

__BOOT_STRING__(desc_eq) = "desc =  ";
__BOOT_STRING__(start_end_dump) = "\n------------------\n";
__BOOT_STRING__(L0) = "level 0\n";
__BOOT_STRING__(L1) = "level1\n";
__BOOT_STRING__(L2) = "level2\n";
__BOOT_STRING__(L3) = "level3\n";
__BOOT_STRING__(L4) = "level4\n";
__BOOT_STRING__(index) = "\nindex=";

__BOOT_STRING__(exe) = "Assert executable\n";
__BOOT_STRING__(read) = "Assert readable\n";
__BOOT_STRING__(not_mapped_el1) = " VA not mapped\n";
__BOOT_STRING__(virt) = "\n VA ";
__BOOT_STRING__(phys) = "\n PA ";
__BOOT_STRING__(desc) = "\n Descriptor ";
__BOOT_STRING__(not_identity_mapped_) = " Not identity mapped\n";
__BOOT_STRING__(pxn_set) = " PXN set on code";
__BOOT_STRING__(hex_pre) = "\n 0x";
__BOOT_STRING__(here) = "\nhere\n ";
__BOOT_STRING__(PC) = "PC\n";
__BOOT_STRING__(SP) = "SP\n";
__BOOT_STRING__(vectors) = "Vectors\n";

__BOOT_STRING__(spacer) = ": ";
__BOOT_STRING__(addr_spacer) = " - 0x";
__BOOT_STRING__(pool_spacer) = "pool table: ";
__BOOT_STRING__(root_table) = "root table: ";
__BOOT_STRING__(pool_level) = "";

__BOOT_STRING__(message_map_2m_L1) = "mapping 2M L1\n";
__BOOT_STRING__(message_map_2m_L2) = "mapping 2M L2\n";
__BOOT_STRING__(block_mapped) = "L2 block already mapped\n";
__BOOT_STRING__(new_block_mapped) = "\nmapping L2 block\n";

__BOOT_STRING__(message_map) = "mapping\n";

__BOOT_STRING__(low) = "low\n";
__BOOT_STRING__(high) = "high\n";
__BOOT_STRING__(heap) = "heap\n";
__BOOT_STRING__(from) = "\nfrom: 0x";
__BOOT_STRING__(to) = "\nto: 0x";

__BOOT_STRING__(message_zero) = "zeroing tables\n";
__BOOT_STRING__(message_zero_pool) = "zeroing table pool\n";

__BOOT_STRING__(message_set_ctlr) = "Setting sctlr\n";
__BOOT_STRING__(message_set_ctlr_complete) = "Setting sctlr - done\n";

__BOOT_STRING__(fault) = "Fault on boot!\n";
__BOOT_STRING__(EC) = "\nEC=0x";
__BOOT_STRING__(IL) = "\nIL=0x";
__BOOT_STRING__(ESR) = "\nESR=0x";
__BOOT_STRING__(ELR) = "\nELR=0x";
__BOOT_STRING__(FAR) = "\nFAR=0x";
__BOOT_STRING__(FSC) = "\nFSC=0x";
__BOOT_STRING__(TCR) = "\nTCR=0x";

__BOOT_STRING__(vect_init) = "Vectors init\n";
__BOOT_STRING__(u_init) = "Uart init\n";
__BOOT_STRING__(zero_init) = "Zeroing tables\n";
__BOOT_STRING__(mapping_kernel) = "Mapping kernel\n";
__BOOT_STRING__(mapping_kernel_complete) = "Mapping kernel - complete\n";
__BOOT_STRING__(mapping_uart_low) = "Mapping uart low\n";
__BOOT_STRING__(mapping_uart_high) = "Mapping uart high\n";

__BOOT_STRING__(mapping_gicd) = "Mapping GICD\n";
__BOOT_STRING__(mapping_gicr) = "Mapping GICR\n";
__BOOT_STRING__(mapping_check) = "Mapping check ";
__BOOT_STRING__(mapping_ok) = " ok";
__BOOT_STRING__(mapping_missing) = " missing";
__BOOT_STRING__(mapping_pa) = " pa=0x";
__BOOT_STRING__(mapping_level) = " level=";
__BOOT_STRING__(mapping_desc) = " desc=0x";
__BOOT_STRING__(mapping_cpu_enter) = "CPU__enter_kernel";
__BOOT_STRING__(mapping_memset) = "memset";
__BOOT_STRING__(mapping_vectors) = "vector_table";
__BOOT_STRING__(mapping_stack) = "kernel_stack";

__BOOT_STRING__(mmu_enable) = "Switching on MMU\n";
__BOOT_STRING__(entering_hi_mem) = "Entering high memory kernel\n";
__BOOT_STRING__(MAIN) = "primary_main\n";
__BOOT_STRING__(master_cpu) = "Master cpu=";
__BOOT_STRING__(start_secondary) = "Start secondary cpus\n";
__BOOT_STRING__(messaged_cpu) = "messaged cpu ";

#endif
