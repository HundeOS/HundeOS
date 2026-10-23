# Target architecture and cross-compilation tools
ARCH ?= aarch64

ifeq ($(ARCH),aarch64)
CROSS_COMPILE ?= aarch64-elf-
BASH_HOST = aarch64-unknown-hunde
ARCH_CFLAGS = -mcmodel=large -march=armv8-a -mno-outline-atomics -mgeneral-regs-only
ARCH_USER_CFLAGS = -mcmodel=small -march=armv8-a -mno-outline-atomics
ARCH_ASFLAGS = -mcmodel=small
OBJCOPY_BINARY_FORMAT = elf64-littleaarch64
OBJCOPY_BINARY_ARCH = aarch64
WUFF_JUMP_SLOT_RELOCATION = R_AARCH64_JUMP_SLOT
WUFF_PROGRAM_RELOCATIONS = R_AARCH64_RELATIVE R_AARCH64_GLOB_DAT R_AARCH64_JUMP_SLOT
WUFF_PROGRAM_LINK_FLAGS = --version-script=$(WUFF_PROGRAM_VERSION_SCRIPT)
BASH_CC = $(CC)
BASH_COMPAT_CFLAGS =
BASH_LINKER_FLAGS =
BASH_SYMBOL_FLAGS = -Wl,--version-script,$(abspath $(WUFF_PROGRAM_VERSION_SCRIPT))
BASH_RUNTIME_LIBS = $(WUFF_LIBGCC)
BASH_RUNTIME_PREREQUISITES =
else ifeq ($(ARCH),riscv64)
CROSS_COMPILE ?= riscv64-elf-
BASH_HOST = riscv64-unknown-hunde
ARCH_CFLAGS = -mcmodel=medany -march=rv64imac_zicsr_zifencei_zawrs -mabi=lp64 -mno-relax -msmall-data-limit=0
ARCH_USER_CFLAGS = -mcmodel=medany -march=rv64imac_zicsr_zifencei_zawrs -mabi=lp64 -mno-relax -msmall-data-limit=0
ARCH_ASFLAGS = -mcmodel=medany -march=rv64imac_zicsr_zifencei_zawrs -mabi=lp64 -mno-relax -msmall-data-limit=0
OBJCOPY_BINARY_FORMAT = elf64-littleriscv
OBJCOPY_BINARY_ARCH = riscv
WUFF_JUMP_SLOT_RELOCATION = R_RISCV_JUMP_SLOT
WUFF_PROGRAM_RELOCATIONS = R_RISCV_RELATIVE R_RISCV_64 R_RISCV_JUMP_SLOT
WUFF_PROGRAM_LINK_FLAGS =
BASH_CC = $(LLVM_CLANG) --target=$(NEWLIB_TARGET)
BASH_COMPAT_CFLAGS = -idirafter $(shell $(CC) -print-file-name=include) \
	-U__WINT_TYPE__ -D__WINT_TYPE__=unsigned
BASH_LINKER_FLAGS = -fuse-ld=lld
BASH_SYMBOL_FLAGS =
BASH_RUNTIME_LIBS = $(abspath $(SYSROOT_COMPILER_RUNTIME))
BASH_RUNTIME_PREREQUISITES = $(SYSROOT_COMPILER_RUNTIME)
else
$(error Unsupported ARCH '$(ARCH)')
endif

AS = $(CROSS_COMPILE)gcc
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
NM = $(CROSS_COMPILE)nm
READELF = $(CROSS_COMPILE)readelf
RAW_LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
DTC ?= dtc
LZMA ?= xz

ifeq ($(ARCH),riscv64)
WUFF_LD ?= ld.lld
else
WUFF_LD ?= $(RAW_LD)
endif
WUFF_LIBGCC = $(shell $(CC) $(ARCH_USER_CFLAGS) -print-libgcc-file-name)
LLVM_CLANG ?= $(shell if command -v brew >/dev/null 2>&1; then \
	printf '%s/bin/clang' "$$(brew --prefix llvm)"; else command -v clang; fi)

UNITY_PATH=vendor/Unity
BOOT_PLATFORM ?= qemu_virt
UEFI_DEVICE_PLATFORM ?= pi_uefi
BSP_DEFINES ?= -DBSP_VIRT
UV ?= uv
CLANG_FORMAT = clang-format
MAGICK ?= magick
PYTHON = $(UV) run python
PYTHON_ENV_STAMP = .venv/.uv-sync
# Compiler flags
CFLAGS = -c -MMD -MP -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
           -nostdlib -nostartfiles $(ARCH_CFLAGS) $(BSP_DEFINES) \
           -DKERNEL_TRACE_PATH=\"$(KERNEL_TRACE_LOG)\" -Ikernel -I./common -I./common_includes \
           -I./arch/$(ARCH)/boot/common -I./arch/$(ARCH)/boot/$(BOOT_PLATFORM) $(EXTRA_KERNEL_CFLAGS)

USER_CFLAGS = -c -MMD -MP -ffreestanding -fno-pic -fno-pie -fno-stack-protector -ffunction-sections -fdata-sections -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
	           -nostdlib -nostartfiles $(ARCH_USER_CFLAGS) \
	           -I./user/lib/arch/$(ARCH)/include -I./user/lib/fiber/arch/$(ARCH) -I./user/lib -I./common -I./common_includes -I./user/lib/newlib/include \
	           -I./vendor/openbsd/src/include -idirafter $(NEWLIB_INCLUDE_DIR)

# Assembler flags
ASFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
            -nostdlib -nostartfiles $(ARCH_ASFLAGS) -latomic

USER_ASFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
            -nostdlib -nostartfiles $(ARCH_ASFLAGS) -latomic

ifdef __BOOT_DEBUG
CFLAGS += -D__BOOT_DEBUG
ASFLAGS += -Wa,-defsym,__BOOT_DEBUG=1
endif

# Linker flags
LINKER_SCRIPT ?= arch/$(ARCH)/boot/$(BOOT_PLATFORM)/linker.ld
LDFLAGS = -nostdlib -T $(LINKER_SCRIPT)
USER_LDFLAGS = -nostdlib $(ARCH_USER_CFLAGS) -Wl,-e,_start -Wl,--gc-sections

BASH_VENDOR_DIR = $(abspath vendor/bash/src)
BASH_PORT_DIR = $(abspath user/ports/bash)
BASH_PORT_ROOT = $(PLATFORM_BUILD_ROOT)/bash
BASH_PORT_SOURCE_DIR = $(BASH_PORT_ROOT)/source
BASH_PORT_BUILD_DIR = $(BASH_PORT_ROOT)/build
BASH_PORT_BINARY = $(BASH_PORT_BUILD_DIR)/bash
BASH_PORT_SOURCE_STAMP = $(BASH_PORT_SOURCE_DIR)/.prepared
BASH_PORT_CONFIGURE_STAMP = $(BASH_PORT_BUILD_DIR)/.configured
BASH_PORT_PATCHES = $(wildcard user/ports/bash/patches/*.patch)
BASH_PORT_CONFIG_CACHE = $(BASH_PORT_DIR)/config.cache
BASH_BUILD_CONFIG_CACHE = $(abspath $(BASH_PORT_BUILD_DIR)/config.cache)
BASH_BUILD = $(shell sh vendor/bash/src/support/config.guess)
BASH_SYSROOT = $(abspath $(PLATFORM_BUILD_ROOT)/sysroot)
BASH_CPPFLAGS = --sysroot=$(BASH_SYSROOT) -isystem $(BASH_SYSROOT)/usr/include -DNEED_EXTERN_PC
BASH_CFLAGS = -O2 -ffreestanding -fPIC -fno-stack-protector $(ARCH_USER_CFLAGS) $(BASH_COMPAT_CFLAGS)
BASH_LDFLAGS = -nostdlib $(ARCH_USER_CFLAGS) $(BASH_LINKER_FLAGS) -Wl,-Bdynamic -Wl,-pie \
	-Wl,--dynamic-linker,/sbin/wuff-ld -Wl,--hash-style=sysv -Wl,-z,max-page-size=0x1000 \
	-Wl,-z,now -Wl,-z,relro \
	-Wl,-e,_start -Wl,--gc-sections $(BASH_SYMBOL_FLAGS) \
	$(abspath $(USER_CRT_PIC_OBJECT))
BASH_LIBS = -Wl,--start-group $(abspath $(SYSROOT_WUFF_LIBC)) \
	$(abspath $(NEWLIB_PIC_LIB_DIR)/libm.a) $(BASH_RUNTIME_LIBS) -Wl,--end-group

# Output files
BUILD_ROOT = build/$(ARCH)
PLATFORM_BUILD_ROOT ?= $(BUILD_ROOT)
NEWLIB_VENDOR_DIR = $(abspath vendor/newlib/src)
NEWLIB_TARGET = $(patsubst %-,%,$(CROSS_COMPILE))
NEWLIB_BUILD_DIR = $(abspath $(PLATFORM_BUILD_ROOT)/newlib/build)
NEWLIB_INSTALL_DIR = $(abspath $(PLATFORM_BUILD_ROOT)/newlib/install)
NEWLIB_INSTALL_PREFIX = $(NEWLIB_INSTALL_DIR)/usr/$(NEWLIB_TARGET)
NEWLIB_INCLUDE_DIR = $(NEWLIB_INSTALL_PREFIX)/include
NEWLIB_LIB_DIR = $(NEWLIB_INSTALL_PREFIX)/lib
NEWLIB_CONFIGURE_STAMP = $(NEWLIB_BUILD_DIR)/.configured
NEWLIB_INSTALL_STAMP = $(NEWLIB_INSTALL_DIR)/.installed
NEWLIB_CFLAGS = -O2 -ffreestanding -fno-pic -fno-pie -fno-stack-protector $(ARCH_USER_CFLAGS)
NEWLIB_LIBC_ARCHIVE = $(NEWLIB_LIB_DIR)/libc.a
NEWLIB_LIBC_SELECTED_DIR = $(PLATFORM_BUILD_ROOT)/newlib/libc-selected
NEWLIB_LIBC_SELECTED_STAMP = $(NEWLIB_LIBC_SELECTED_DIR)/.extracted
NEWLIB_LIBC_VERIFY_OBJECT = $(NEWLIB_LIBC_SELECTED_DIR)/selected.o
NEWLIB_PIC_BUILD_DIR = $(abspath $(PLATFORM_BUILD_ROOT)/newlib-pic/build)
NEWLIB_PIC_INSTALL_DIR = $(abspath $(PLATFORM_BUILD_ROOT)/newlib-pic/install)
NEWLIB_PIC_INSTALL_PREFIX = $(NEWLIB_PIC_INSTALL_DIR)/usr/$(NEWLIB_TARGET)
NEWLIB_PIC_INCLUDE_DIR = $(NEWLIB_PIC_INSTALL_PREFIX)/include
NEWLIB_PIC_LIB_DIR = $(NEWLIB_PIC_INSTALL_PREFIX)/lib
NEWLIB_PIC_CONFIGURE_STAMP = $(NEWLIB_PIC_BUILD_DIR)/.configured
NEWLIB_PIC_INSTALL_STAMP = $(NEWLIB_PIC_INSTALL_DIR)/.installed
NEWLIB_PIC_CFLAGS = -O2 -ffreestanding -fPIC -fno-stack-protector $(ARCH_USER_CFLAGS)
NEWLIB_PIC_LIBC_ARCHIVE = $(NEWLIB_PIC_LIB_DIR)/libc.a
NEWLIB_PIC_LIBC_SELECTED_DIR = $(PLATFORM_BUILD_ROOT)/newlib-pic/libc-selected
NEWLIB_PIC_LIBC_SELECTED_STAMP = $(NEWLIB_PIC_LIBC_SELECTED_DIR)/.extracted
NEWLIB_PIC_LIBC_VERIFY_OBJECT = $(NEWLIB_PIC_LIBC_SELECTED_DIR)/selected.o
NEWLIB_LIBC_MEMBERS = \
	libc_a-bcopy.o \
	libc_a-ctype_.o \
	libc_a-memchr.o \
	libc_a-memcmp.o \
	libc_a-memcpy.o \
	libc_a-memmove.o \
	libc_a-memset.o \
	libc_a-qsort.o \
	libc_a-strcat.o \
	libc_a-strchr.o \
	libc_a-strcmp.o \
	libc_a-strcpy.o \
	libc_a-strcspn.o \
	libc_a-strlcpy.o \
	libc_a-strlen.o \
	libc_a-strncat.o \
	libc_a-strncmp.o \
	libc_a-strncpy.o \
	libc_a-strnlen.o \
	libc_a-strpbrk.o \
	libc_a-strrchr.o \
	libc_a-strspn.o \
	libc_a-strstr.o \
	libc_a-strtol.o \
	libc_a-strtoll.o \
	libc_a-strtoul.o \
	libc_a-strtoull.o
NEWLIB_LIBC_OBJECTS = $(addprefix $(NEWLIB_LIBC_SELECTED_DIR)/,$(NEWLIB_LIBC_MEMBERS))
NEWLIB_PIC_LIBC_OBJECTS = $(addprefix $(NEWLIB_PIC_LIBC_SELECTED_DIR)/,$(NEWLIB_LIBC_MEMBERS))
NEWLIB_LIBC_ALLOWED_UNDEFINED = __errno _impure_ptr
ifeq ($(COMPILE_COMMANDS),1)
NEWLIB_INSTALL_PREREQUISITE =
NEWLIB_PIC_INSTALL_PREREQUISITE =
BASH_CAPTURE_MAKEFLAGS = -o Makefile -o makefile -o config.status -o config.h -o stamp-h
BASH_CAPTURE_ARGUMENTS = $(BASH_CAPTURE_MAKEFLAGS) BASH_MAKEFLAGS="$(BASH_CAPTURE_MAKEFLAGS)"
else
NEWLIB_INSTALL_PREREQUISITE = $(NEWLIB_INSTALL_STAMP)
NEWLIB_PIC_INSTALL_PREREQUISITE = $(NEWLIB_PIC_INSTALL_STAMP)
endif
NEWLIB_MATH_HEADERS = \
	_ansi.h \
	_newlib_version.h \
	machine/_default_types.h \
	machine/_types.h \
	machine/ieeefp.h \
	math.h \
	newlib.h \
	sys/_types.h \
	sys/cdefs.h \
	sys/config.h \
	sys/features.h \
	sys/lock.h \
	sys/reent.h
TARGET_BUILD_DIR = $(BUILD_ROOT)/$(BOOT_PLATFORM)
KERNEL_BUILD_DIR = $(TARGET_BUILD_DIR)/kernel
USER_BUILD_DIR = $(PLATFORM_BUILD_ROOT)/user
IMAGE_BUILD_DIR = $(PLATFORM_BUILD_ROOT)/images
ELF = $(TARGET_BUILD_DIR)/kernel.elf
BIN = $(TARGET_BUILD_DIR)/kernel.img
HVF_TRACE_KERNEL_BUILD_DIR = $(BUILD_ROOT)/qemu_virt_trace/kernel
HVF_TRACE_ELF = $(BUILD_ROOT)/qemu_virt_trace/kernel.elf
TCG_ICOUNT_TRACE_KERNEL_BUILD_DIR = $(BUILD_ROOT)/qemu_virt_tcg_icount_trace/kernel
TCG_ICOUNT_TRACE_ELF = $(BUILD_ROOT)/qemu_virt_tcg_icount_trace/kernel.elf
RISCV64_TRACE_KERNEL_BUILD_DIR = $(BUILD_ROOT)/qemu_virt_trace/kernel
RISCV64_TRACE_ELF = $(BUILD_ROOT)/qemu_virt_trace/kernel.elf
EFI = $(TARGET_BUILD_DIR)/kernel.efi
EFI_ESP_IMG = $(TARGET_BUILD_DIR)/esp.img
PI_UEFI_BUILD_DIR = $(BUILD_ROOT)/pi_uefi
PI_UEFI_EFI = $(PI_UEFI_BUILD_DIR)/BOOTAA64.EFI
PI_UEFI_ESP_IMG = $(PI_UEFI_BUILD_DIR)/esp.img
PI_UEFI_EFI_VARS = $(PI_UEFI_BUILD_DIR)/efi-vars.fd
PI_UEFI_LOADER_ELF = $(PI_UEFI_BUILD_DIR)/loader.elf
PI_UEFI_KERNEL_IMG = $(PI_UEFI_BUILD_DIR)/kernel.img
PI_UEFI_KERNEL_ELF = $(PI_UEFI_BUILD_DIR)/kernel.elf
KERNEL_TRACE_LOG = $(BUILD_ROOT)/kernel-trace.log
RISCV64_BRINGUP_BUILD_DIR = $(BUILD_ROOT)/qemu_virt/bringup
RISCV64_BRINGUP_ELF = $(BUILD_ROOT)/qemu_virt/bringup.elf
RISCV64_BRINGUP_LOG = $(BUILD_ROOT)/qemu_virt/bringup.log
RISCV64_BRINGUP_C_SOURCES = \
	arch/riscv64/cpu_identity.c \
	arch/riscv64/boot/common/boot.c \
	arch/riscv64/boot/common/device_tree.c \
	arch/riscv64/boot/common/mmu.c \
	arch/riscv64/boot/qemu_virt/platform.c \
	arch/riscv64/boot/qemu_virt/runtime.c \
	arch/riscv64/boot/common/system_info.c \
	arch/riscv64/boot/common/trap.c \
	common/arch/riscv64/memory.c \
	common/memory.c \
	kernel/early_boot.c \
	kernel/system_info.c
RISCV64_BRINGUP_ASM_SOURCES = \
	arch/riscv64/boot/qemu_virt/start.s \
	arch/riscv64/boot/common/trap_entry.s
RISCV64_BRINGUP_OBJECTS = \
	$(patsubst %.c,$(RISCV64_BRINGUP_BUILD_DIR)/%.o,$(RISCV64_BRINGUP_C_SOURCES)) \
	$(patsubst %.s,$(RISCV64_BRINGUP_BUILD_DIR)/%.o,$(RISCV64_BRINGUP_ASM_SOURCES))
MILKV_DUO_BUILD_DIR = $(BUILD_ROOT)/milkv_duo
MILKV_DUO_BRINGUP_BUILD_DIR = $(MILKV_DUO_BUILD_DIR)/bringup
MILKV_DUO_BRINGUP_ELF = $(MILKV_DUO_BUILD_DIR)/bringup.elf
MILKV_DUO_BRINGUP_BIN = $(MILKV_DUO_BUILD_DIR)/bringup.bin
MILKV_DUO_BRINGUP_LZMA = $(MILKV_DUO_BUILD_DIR)/bringup.bin.lzma
MILKV_DUO_DTB = $(MILKV_DUO_BUILD_DIR)/milkv_duo.dtb
MILKV_DUO_BOOT_IMAGE = $(MILKV_DUO_BUILD_DIR)/boot.sd
MILKV_DUO_BRINGUP_BOOT_IMAGE = $(MILKV_DUO_BUILD_DIR)/bringup.sd
MILKV_DUO_KERNEL_LZMA = $(MILKV_DUO_BUILD_DIR)/kernel.img.lzma
MILKV_DUO_FIRMWARE_VERSION = v1.1.4
MILKV_DUO_FIRMWARE_URL = https://github.com/milkv-duo/duo-buildroot-sdk/releases/download/$(MILKV_DUO_FIRMWARE_VERSION)/milkv-duo-sd-$(MILKV_DUO_FIRMWARE_VERSION).img.zip
MILKV_DUO_FIRMWARE_SHA256 = 271a24958e637f7101ca93f10a57a3a8f1afa9f100710462cd689c795bc01e4a
MILKV_DUO_FIRMWARE_ARCHIVE = $(MILKV_DUO_BUILD_DIR)/firmware/milkv-duo-sd-$(MILKV_DUO_FIRMWARE_VERSION).img.zip
MILKV_DUO_FIRMWARE_TEMPLATE = $(MILKV_DUO_BUILD_DIR)/firmware/boot-prefix.img
MILKV_DUO_BOOT_PARTITION = $(MILKV_DUO_BUILD_DIR)/boot.fat
MILKV_DUO_SD_IMAGE = $(MILKV_DUO_BUILD_DIR)/hundeos-milkv-duo.img
MILKV_DUO_BOOT_PARTITION_SECTORS = 262144
MILKV_DUO_BRINGUP_SOURCES = arch/riscv64/boot/milkv_duo/bringup.c
MILKV_DUO_BRINGUP_ASM_SOURCES = arch/riscv64/boot/milkv_duo/bringup_start.s
MILKV_DUO_ARCH_FLAGS = -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -mno-relax -msmall-data-limit=0
MILKV_DUO_BRINGUP_OBJECTS = \
	$(patsubst %.c,$(MILKV_DUO_BRINGUP_BUILD_DIR)/%.o,$(MILKV_DUO_BRINGUP_SOURCES)) \
	$(patsubst %.s,$(MILKV_DUO_BRINGUP_BUILD_DIR)/%.o,$(MILKV_DUO_BRINGUP_ASM_SOURCES))
MILKV_DUO_QEMU_PLATFORM_BUILD_ROOT = build/riscv64/milkv_duo_qemu
MILKV_DUO_QEMU_KERNEL = $(MILKV_DUO_QEMU_PLATFORM_BUILD_ROOT)/milkv_duo/kernel.elf
MILKV_DUO_QEMU_EXT2_IMG = $(MILKV_DUO_QEMU_PLATFORM_BUILD_ROOT)/images/ext2.img
MILKV_DUO_QEMU_DTB = $(MILKV_DUO_QEMU_PLATFORM_BUILD_ROOT)/milkv_duo/milkv_duo.dtb
QEMU_MILKV_DUO ?= $(HOME)/src/qemu-milkv-duo/build/qemu-system-riscv64
QEMU_MILKV_DUO_BIOS ?= $(HOME)/src/qemu-milkv-duo/pc-bios/opensbi-riscv64-generic-fw_dynamic.bin
MILKV_DUO_S_BUILD_DIR = $(BUILD_ROOT)/milkv_duo_s
MILKV_DUO_S_DTS ?= arch/riscv64/boot/milkv_duo_s/milkv_duo_s.dts
MILKV_DUO_S_DTB = $(MILKV_DUO_S_BUILD_DIR)/milkv_duo_s.dtb
MILKV_DUO_S_BOOT_IMAGE = $(MILKV_DUO_S_BUILD_DIR)/boot.sd
MILKV_DUO_S_KERNEL_LZMA = $(MILKV_DUO_S_BUILD_DIR)/kernel.img.lzma
MILKV_DUO_S_HANDOFF_OBJECT = $(MILKV_DUO_S_BUILD_DIR)/handoff.o
MILKV_DUO_S_HANDOFF_ELF = $(MILKV_DUO_S_BUILD_DIR)/handoff.elf
MILKV_DUO_S_HANDOFF_BIN = $(MILKV_DUO_S_BUILD_DIR)/handoff.bin
MILKV_DUO_S_FIRMWARE_VERSION = v1.1.4
MILKV_DUO_S_FIRMWARE_URL = https://github.com/milkv-duo/duo-buildroot-sdk/releases/download/$(MILKV_DUO_S_FIRMWARE_VERSION)/milkv-duos-sd-$(MILKV_DUO_S_FIRMWARE_VERSION).img.zip
MILKV_DUO_S_FIRMWARE_SHA256 = ff7cd27320573103c49f4365ed261aec664446535e329835bbc2364683711c8f
MILKV_DUO_S_FIRMWARE_ARCHIVE = $(MILKV_DUO_S_BUILD_DIR)/firmware/milkv-duos-sd-$(MILKV_DUO_S_FIRMWARE_VERSION).img.zip
MILKV_DUO_S_FIRMWARE_TEMPLATE = $(MILKV_DUO_S_BUILD_DIR)/firmware/boot-prefix.img
MILKV_DUO_S_BOOT_PARTITION = $(MILKV_DUO_S_BUILD_DIR)/boot.fat
MILKV_DUO_S_SD_IMAGE = $(MILKV_DUO_S_BUILD_DIR)/hundeos-milkv-duo-s.img
MILKV_DUO_S_BOOT_PARTITION_SECTORS = 262144
MILKV_DUO_S_ROOT_START_SECTOR = 266241
MILKV_DUO_S_ARCH_FLAGS = -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -mno-relax -msmall-data-limit=0
MILKV_DUO_S_QEMU_PLATFORM_BUILD_ROOT = build/riscv64/milkv_duo_s_qemu
MILKV_DUO_S_QEMU_KERNEL = $(MILKV_DUO_S_QEMU_PLATFORM_BUILD_ROOT)/milkv_duo_s/kernel.elf
MILKV_DUO_S_QEMU_DTB = $(MILKV_DUO_S_QEMU_PLATFORM_BUILD_ROOT)/milkv_duo_s/milkv_duo_s.dtb
MILKV_DUO_S_HARDWARE_SD_IMAGE = build/riscv64/milkv_duo_s/hundeos-milkv-duo-s.img
MILKV_DUO_S_QEMU_SD_IMAGE = $(MILKV_DUO_S_QEMU_PLATFORM_BUILD_ROOT)/images/hundeos-milkv-duo-s.img
MILKV_DUO_S_AARCH64_BUILD_DIR = build/aarch64/milkv_duo_s
MILKV_DUO_S_AARCH64_DTB = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/milkv_duo_s.dtb
MILKV_DUO_S_AARCH64_BOOT_IMAGE = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/boot.sd
MILKV_DUO_S_AARCH64_KERNEL_LZMA = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/kernel.img.lzma
MILKV_DUO_S_AARCH64_HANDOFF_OBJECT = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/handoff.o
MILKV_DUO_S_AARCH64_HANDOFF_ELF = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/handoff.elf
MILKV_DUO_S_AARCH64_HANDOFF_BIN = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/handoff.bin
MILKV_DUO_S_AARCH64_FIRMWARE_VERSION = v2.0.1
MILKV_DUO_S_AARCH64_FIRMWARE_URL = https://github.com/milkv-duo/duo-buildroot-sdk-v2/releases/download/$(MILKV_DUO_S_AARCH64_FIRMWARE_VERSION)/milkv-duos-glibc-arm64-sd_$(MILKV_DUO_S_AARCH64_FIRMWARE_VERSION).img.zip
MILKV_DUO_S_AARCH64_FIRMWARE_SHA256 = ad919531d11231f1acface111435dda94c933dc33b64e3b1e5afcda7a3d8fbfb
MILKV_DUO_S_AARCH64_FIRMWARE_ARCHIVE = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/firmware/milkv-duos-glibc-arm64-sd_$(MILKV_DUO_S_AARCH64_FIRMWARE_VERSION).img.zip
MILKV_DUO_S_AARCH64_FIRMWARE_TEMPLATE = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/firmware/boot-prefix.img
MILKV_DUO_S_AARCH64_BOOT_PARTITION = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/boot.fat
MILKV_DUO_S_AARCH64_SD_IMAGE = $(MILKV_DUO_S_AARCH64_BUILD_DIR)/hundeos-milkv-duo-s.img
EFI_ESP_SIZE_MB ?= 512
DISK_IMG = $(IMAGE_BUILD_DIR)/disk.img
DISK_SIZE_MB = 64
DISK_LABEL = KERNELFS
DISK_TEST_FILE = tools/disk_images/disk_test_file.txt
DISK_TEST_NAME = TEST.TXT
DISK_LONG_FILE = tools/disk_images/disk_long_filename.txt
DISK_LONG_NAME = This is a long filename.txt
DISK_LARGE_FILE = tools/disk_images/disk_large_file.txt
DISK_LARGE_NAME = BIGFILE.TXT
CHANNELIO_DISK_IMG = $(IMAGE_BUILD_DIR)/channelio-fat32.img
CHANNELIO_DISK_SIZE_MB = 64
CHANNELIO_DISK_LABEL = CHANNELIO
CHANNELIO_DISK_TEST_NAME = CHANNEL.TXT
CHANNELIO_EXT2_IMG = $(IMAGE_BUILD_DIR)/channelio-ext2.img
CHANNELIO_EXT2_LABEL = CHANNELIO_EXT2
HOSTED_EXT2_IMG = $(IMAGE_BUILD_DIR)/hosted-ext2.img
HOSTED_EXT2_ROOT_DIR = $(IMAGE_BUILD_DIR)/hosted-ext2-root
SD_CARD_FAT32_IMG = $(IMAGE_BUILD_DIR)/sdcard-fat32.img
SD_CARD_FAT32_SIZE_MB = 64
SD_CARD_FAT32_LABEL = TESTSD
SD_CARD_FAT32_README_FILE = tools/disk_images/sdcard_fat32_readme.txt
SD_CARD_FAT32_CONFIG_FILE = tools/disk_images/sdcard_fat32_config.txt
SD_CARD_FAT32_NOTE_FILE = tools/disk_images/sdcard_fat32_note.txt
EXT2_IMG = $(IMAGE_BUILD_DIR)/ext2.img
EXT2_SIZE_MB = 64
EXT2_LABEL = EXT2FS
EXT2_ROOT_DIR = tools/ext2_root
ROOTFS_DIR = $(PLATFORM_BUILD_ROOT)/rootfs
ROOTFS_STAMP = $(ROOTFS_DIR)/.built
BOOT_CONFIG ?= qemu_virt
BOOT_CONFIG_EXT2_IMG = $(BUILD_ROOT)/boot_config/$(BOOT_CONFIG).img
HVF_BOOT_CONFIG_EXT2_IMG = $(BUILD_ROOT)/boot_config/qemu_virt_hvf.img
BOOT_CONFIG_EXT2_SIZE_MB = 8
BOOT_CONFIG_EXT2_LABEL = BOOTCFG
BOOT_CONFIG_ROOT_DIR = config/$(BOOT_CONFIG)
EXT2_IMG_BLOCK_SIZE = 1024
EXT2_INODE_SIZE = 128
EXT2_INODE_COUNT = 1024
EXT2_MKFS ?= genext2fs
MKFS_FAT ?= mkfs.fat
SERIAL ?= stdio
QEMU_PI_MACHINE ?= raspi4b
QEMU_PI_MEMORY ?= 2G
QEMU_PI_KERNEL_ADDR ?= 0x40080000
QEMU_PI_KERNEL_ENTRY ?= 0x40080080
QEMU_EFI_CODE ?= $(firstword $(wildcard /opt/homebrew/share/qemu/edk2-aarch64-code.fd /opt/homebrew/Cellar/qemu/*/share/qemu/edk2-aarch64-code.fd /usr/share/qemu-efi-aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd))
QEMU_EFI_VARS_TEMPLATE ?= $(firstword $(wildcard /opt/homebrew/share/qemu/edk2-arm-vars.fd /opt/homebrew/Cellar/qemu/*/share/qemu/edk2-arm-vars.fd /usr/share/qemu-efi-aarch64/QEMU_VARS.fd /usr/share/AAVMF/AAVMF_VARS.fd))
QEMU_EFI_VARS = $(TARGET_BUILD_DIR)/efi-vars.fd

SOURCES := $(shell find ./kernel -name "*.c" -print)
ASM_SOURCES := $(shell find ./kernel -name "*.s" -print)
CPP_ASM_SOURCES := $(shell find ./kernel -name "*.S" -print)

COMMON_SOURCES := $(shell find ./common -path "./common/arch" -prune -o -name "*.c" -print)
COMMON_ARCH_SOURCES := $(shell find ./common/arch/$(ARCH) -name "*.c" -print 2>/dev/null)

ARCH_COMMON_SOURCES := $(shell find ./arch/$(ARCH) -path "./arch/$(ARCH)/boot/*/*" -prune -o -name "*.c" -print 2>/dev/null)
ARCH_BOOT_COMMON_SOURCES := $(shell find ./arch/$(ARCH)/boot/common -name "*.c" -print 2>/dev/null)
ARCH_PLATFORM_SOURCES := $(shell find ./arch/$(ARCH)/boot/$(BOOT_PLATFORM) -name "*.c" \
	! -name "loader.c" ! -name "runtime.c" ! -name "bringup.c" ! -name "uefi_devices.c" -print 2>/dev/null)
ifeq ($(BOOT_PLATFORM),pi_uefi)
ARCH_PLATFORM_SOURCES += ./arch/$(ARCH)/boot/$(UEFI_DEVICE_PLATFORM)/uefi_devices.c
endif
ARCH_SOURCES := $(ARCH_COMMON_SOURCES) $(ARCH_BOOT_COMMON_SOURCES) $(ARCH_PLATFORM_SOURCES)
ARCH_COMMON_ASM_SOURCES := $(shell find ./arch/$(ARCH) -path "./arch/$(ARCH)/boot/*/*" -prune -o -name "*.s" -print 2>/dev/null)
ARCH_BOOT_COMMON_ASM_SOURCES := $(shell find ./arch/$(ARCH)/boot/common -name "*.s" -print 2>/dev/null)
ARCH_PLATFORM_ASM_SOURCES := $(shell find ./arch/$(ARCH)/boot/$(BOOT_PLATFORM) -name "*.s" \
	! -name "bringup_start.s" ! -name "handoff.s" -print 2>/dev/null)
ARCH_ASM_SOURCES := $(ARCH_COMMON_ASM_SOURCES) $(ARCH_BOOT_COMMON_ASM_SOURCES) $(ARCH_PLATFORM_ASM_SOURCES)
ARCH_COMMON_CPP_ASM_SOURCES := $(shell find ./arch/$(ARCH) -path "./arch/$(ARCH)/boot/*/*" -prune -o -name "*.S" -print 2>/dev/null)
ARCH_PLATFORM_CPP_ASM_SOURCES := $(shell find ./arch/$(ARCH)/boot/$(BOOT_PLATFORM) -name "*.S" -print 2>/dev/null)
ARCH_CPP_ASM_SOURCES := $(ARCH_COMMON_CPP_ASM_SOURCES) $(ARCH_PLATFORM_CPP_ASM_SOURCES)

# Define object files based on source files, potentially in a build directory
OBJECTS := $(patsubst %.c,$(KERNEL_BUILD_DIR)/%.o,$(SOURCES))
ASM_OBJECTS := $(patsubst %.s,$(KERNEL_BUILD_DIR)/%.o,$(ASM_SOURCES))
CPP_ASM_OBJECTS := $(patsubst %.S,$(KERNEL_BUILD_DIR)/%.o,$(CPP_ASM_SOURCES))
KERNEL_ARTWORK_BUILD_DIR := $(KERNEL_BUILD_DIR)/assets
KERNEL_LOGO_RGB := $(KERNEL_ARTWORK_BUILD_DIR)/logo.rgb
KERNEL_PANIC_RGB := $(KERNEL_ARTWORK_BUILD_DIR)/panic.rgb
KERNEL_LOGO_RGB_OBJECT := $(KERNEL_ARTWORK_BUILD_DIR)/logo.o
KERNEL_PANIC_RGB_OBJECT := $(KERNEL_ARTWORK_BUILD_DIR)/panic.o
KERNEL_ARTWORK_OBJECTS := $(KERNEL_LOGO_RGB_OBJECT) $(KERNEL_PANIC_RGB_OBJECT)
KERNEL_LOGO_RGB_BINARY_SYMBOL := $(subst /,_,$(subst .,_,$(KERNEL_LOGO_RGB)))
KERNEL_PANIC_RGB_BINARY_SYMBOL := $(subst /,_,$(subst .,_,$(KERNEL_PANIC_RGB)))
COMMON_KERNEL_OBJECTS := $(patsubst %.c,$(KERNEL_BUILD_DIR)/%.o,$(COMMON_SOURCES) $(COMMON_ARCH_SOURCES))

ARCH_OBJECTS := $(patsubst %.c,$(KERNEL_BUILD_DIR)/%.o,$(ARCH_SOURCES))
ARCH_ASM_OBJECTS := $(patsubst %.s,$(KERNEL_BUILD_DIR)/%.o,$(ARCH_ASM_SOURCES))
ARCH_CPP_ASM_OBJECTS := $(patsubst %.S,$(KERNEL_BUILD_DIR)/%.o,$(ARCH_CPP_ASM_SOURCES))

USER_LIB_COMMON_SOURCES := $(shell find ./user/lib -name "*.c" ! -name "nano-mallocr.c" \
	! -path "./user/lib/arch/*/*" ! -path "./user/lib/fiber/arch/*/*" \
	! -path "./user/lib/channel/arch/*/*" -print)
USER_LIB_ARCH_SOURCES := $(shell find ./user/lib/arch/$(ARCH) ./user/lib/fiber/arch/$(ARCH) \
	./user/lib/channel/arch/$(ARCH) -name "*.c" -print 2>/dev/null)
USER_LIB_SOURCES := $(USER_LIB_COMMON_SOURCES) $(USER_LIB_ARCH_SOURCES)
USER_LIB_COMMON_ASM_SOURCES := $(shell find ./user/lib -name "*.s" \
	! -path "./user/lib/arch/*/*" ! -path "./user/lib/fiber/arch/*/*" \
	! -path "./user/lib/channel/arch/*/*" -print)
USER_LIB_ARCH_ASM_SOURCES := $(shell find ./user/lib/arch/$(ARCH) ./user/lib/fiber/arch/$(ARCH) \
	./user/lib/channel/arch/$(ARCH) -name "*.s" -print 2>/dev/null)
USER_LIB_ASM_SOURCES := $(USER_LIB_COMMON_ASM_SOURCES) $(USER_LIB_ARCH_ASM_SOURCES)

USER_LIB_OBJECTS := $(patsubst ./user/%,$(USER_BUILD_DIR)/%,$(USER_LIB_SOURCES:.c=.o))
USER_LIB_ASM_OBJECTS := $(patsubst ./user/%,$(USER_BUILD_DIR)/%,$(USER_LIB_ASM_SOURCES:.s=.o))
COMMON_USER_OBJECTS := $(patsubst ./common/%.c,$(USER_BUILD_DIR)/common/%.o,$(COMMON_SOURCES) $(COMMON_ARCH_SOURCES))
USER_CRT_OBJECT := $(USER_BUILD_DIR)/lib/arch/$(ARCH)/start.o
COMPILER_RUNTIME_OBJECT := $(USER_BUILD_DIR)/lib/arch/riscv64/compiler_runtime.o
USER_LIBC_SOURCES := $(filter ./user/lib/newlib/%,$(USER_LIB_SOURCES))
USER_LIBC_C_OBJECTS := $(patsubst ./user/%,$(USER_BUILD_DIR)/%,$(USER_LIBC_SOURCES:.c=.o))
USER_LIBC_ASM_OBJECTS := $(USER_BUILD_DIR)/lib/arch/$(ARCH)/setjmp.o
USER_LIBC_OBJECTS := $(USER_LIBC_C_OBJECTS) $(USER_LIBC_ASM_OBJECTS)
USER_HUNDE_OBJECTS := $(COMMON_USER_OBJECTS) \
	$(filter-out $(USER_CRT_OBJECT) $(USER_LIBC_C_OBJECTS) $(COMPILER_RUNTIME_OBJECT),$(USER_LIB_OBJECTS)) \
	$(filter-out $(USER_LIBC_ASM_OBJECTS),$(USER_LIB_ASM_OBJECTS))

USER_PIC_BUILD_DIR := $(PLATFORM_BUILD_ROOT)/user-pic
USER_PIC_CFLAGS := $(filter-out -fno-pic -fno-pie,$(USER_CFLAGS)) -fPIC
USER_PIC_ASFLAGS := $(filter-out -fno-pic -fno-pie,$(USER_ASFLAGS)) -fPIC
USER_LIB_PIC_OBJECTS := $(patsubst ./user/%,$(USER_PIC_BUILD_DIR)/%,$(USER_LIB_SOURCES:.c=.o))
USER_LIB_PIC_ASM_OBJECTS := $(patsubst ./user/%,$(USER_PIC_BUILD_DIR)/%,$(USER_LIB_ASM_SOURCES:.s=.o))
COMMON_USER_PIC_OBJECTS := $(patsubst ./common/%.c,$(USER_PIC_BUILD_DIR)/common/%.o,$(COMMON_SOURCES) $(COMMON_ARCH_SOURCES))
USER_CRT_PIC_OBJECT := $(USER_PIC_BUILD_DIR)/lib/arch/$(ARCH)/start.o
COMPILER_RUNTIME_PIC_OBJECT := $(USER_PIC_BUILD_DIR)/lib/arch/riscv64/compiler_runtime.o
USER_LIBC_PIC_C_OBJECTS := $(patsubst ./user/%,$(USER_PIC_BUILD_DIR)/%,$(USER_LIBC_SOURCES:.c=.o))
USER_LIBC_PIC_ASM_OBJECTS := $(USER_PIC_BUILD_DIR)/lib/arch/$(ARCH)/setjmp.o
USER_LIBC_PIC_OBJECTS := $(USER_LIBC_PIC_C_OBJECTS) $(USER_LIBC_PIC_ASM_OBJECTS)
USER_HUNDE_PIC_OBJECTS := $(COMMON_USER_PIC_OBJECTS) \
	$(filter-out $(USER_CRT_PIC_OBJECT) $(USER_LIBC_PIC_C_OBJECTS) $(COMPILER_RUNTIME_PIC_OBJECT),$(USER_LIB_PIC_OBJECTS)) \
	$(filter-out $(USER_LIBC_PIC_ASM_OBJECTS),$(USER_LIB_PIC_ASM_OBJECTS))

SYSROOT_DIR := $(PLATFORM_BUILD_ROOT)/sysroot
SYSROOT_INCLUDE_DIR := $(SYSROOT_DIR)/usr/include
SYSROOT_LIB_DIR := $(SYSROOT_DIR)/usr/lib
SYSROOT_INCLUDE_STAMP := $(SYSROOT_INCLUDE_DIR)/.staged
SYSROOT_CRT := $(SYSROOT_LIB_DIR)/crt0.o
SYSROOT_LIBC := $(SYSROOT_LIB_DIR)/libc.a
SYSROOT_LIBHUNDE := $(SYSROOT_LIB_DIR)/libhunde.a
SYSROOT_LIBM := $(SYSROOT_LIB_DIR)/libm.a
SYSROOT_COMPILER_RUNTIME := $(SYSROOT_LIB_DIR)/libcompiler_rt_pic.a
SYSROOT_FILES := $(SYSROOT_INCLUDE_STAMP) $(SYSROOT_CRT) $(SYSROOT_LIBC) $(SYSROOT_LIBHUNDE) $(SYSROOT_LIBM)
SYSROOT_WUFF_LIBC := $(SYSROOT_LIB_DIR)/libc.wuff.1
SYSROOT_WUFF_LIBC_LINK := $(SYSROOT_LIB_DIR)/libc.wuff
SYSROOT_WUFF_LINK_DIR := $(SYSROOT_LIB_DIR)/wuff
SYSROOT_WUFF_LIBC_COMPAT := $(SYSROOT_WUFF_LINK_DIR)/libc.so
SYSROOT_WUFF_FILES := $(SYSROOT_WUFF_LIBC) $(SYSROOT_WUFF_LIBC_LINK) $(SYSROOT_WUFF_LIBC_COMPAT)
SYSROOT_SMOKE_SOURCE := user/ports/bash/sysroot_smoke.c
SYSROOT_SMOKE_OBJECT := $(BUILD_ROOT)/sysroot-smoke/sysroot_smoke.o
SYSROOT_SMOKE_BINARY := $(BUILD_ROOT)/sysroot-smoke/sysroot_smoke.bin
WUFF_SMOKE_SOURCE := user/ports/wuff_smoke.c
WUFF_SMOKE_OBJECT := $(PLATFORM_BUILD_ROOT)/wuff-smoke/wuff_smoke.o
WUFF_SMOKE_BINARY := $(PLATFORM_BUILD_ROOT)/wuff-smoke/wuff-smoke.wuff
WUFF_DYNAMIC_SMOKE_SOURCE := user/ports/wuff_dynamic_smoke.c
WUFF_DYNAMIC_SMOKE_OBJECT := $(PLATFORM_BUILD_ROOT)/wuff-dynamic-smoke/wuff_dynamic_smoke.o
WUFF_DYNAMIC_SMOKE_BINARY := $(PLATFORM_BUILD_ROOT)/wuff-dynamic-smoke/wuff-dynamic-smoke.bin
WUFF_PROGRAM_VERSION_SCRIPT := user/ports/wuff_program.map
RUST_TOOLCHAIN := 1.99.1
RUST_COMPONENT_TARGET_aarch64 := aarch64-unknown-none-softfloat
RUST_COMPONENT_TARGET_riscv64 := riscv64imac-unknown-none-elf
RUST_COMPONENT_TARGET := $(RUST_COMPONENT_TARGET_$(ARCH))
RUST_COMPONENT_SMOKE_SOURCE := user/components/rust_smoke/component.rs
RUST_COMPONENT_SMOKE_MAP := user/components/rust_smoke/exports.map
RUST_COMPONENT_SMOKE_BINARY := $(PLATFORM_BUILD_ROOT)/rust-component-smoke/hunde-rust-smoke.wuff
RUST_CARGO_DIR := $(PLATFORM_BUILD_ROOT)/rust
RUST_RELEASE_DIR := $(RUST_CARGO_DIR)/$(RUST_COMPONENT_TARGET)/release
RUST_IO_HOST_ARCHIVE := $(RUST_RELEASE_DIR)/libhunde_io_host.a
RUST_MEMFS_BINARY := $(RUST_CARGO_DIR)/memfs.wuff
RUST_PROCFS_BINARY := $(RUST_CARGO_DIR)/procfs.wuff
RUST_SYSINFOFS_BINARY := $(RUST_CARGO_DIR)/sysinfofs.wuff
RUST_EXT2_BINARY := $(RUST_CARGO_DIR)/ext2fs.wuff
RUST_FAT32_BINARY := $(RUST_CARGO_DIR)/fat32.wuff
RUST_IO_COMPONENT_ENTRY := hunde_io_component_entry_v6
RUST_PIPE_BINARY := $(RUST_CARGO_DIR)/pipe.wuff
RUST_SOCKET_BINARY := $(RUST_CARGO_DIR)/socket.wuff
RUST_CRATE_DIRS := user/drivers/memfs user/services/hund-rohr user/lib/hunde user/services/hunde-io-host user/drivers/socket user/components/rust_smoke user/drivers/procfs user/drivers/sysinfofs user/drivers/ext2fs user/drivers/fat32fs
RUST_PROTOCOL_INPUTS := common/ipc/io_protocol.h common_includes/hunde/io_component.h common_includes/hunde/boot_info.h common_includes/hunde/host_component.h common_includes/syscalls/io.h common_includes/syscalls/service.h common_includes/syscalls/mem.h tools/generate_rust_io_constants.py
RUST_IO_SOURCES := $(shell find $(RUST_CRATE_DIRS) -name '*.rs' -print -o -name Cargo.toml -print) user/Cargo.toml user/Cargo.lock $(RUST_PROTOCOL_INPUTS)
hunde-io-host_EXTRA_OBJS := $(RUST_IO_HOST_ARCHIVE)

USER_BINARY_ROOTS := user/programs user/commands user/drivers user/services
USER_RETIRED_BINARY_DIRS := user/commands/shell user/services/hund-dateisystem user/services/hund-io-system
USER_COMPONENT_DIRS := user/drivers/memfs user/drivers/socket user/services/hund-rohr user/drivers/procfs user/drivers/sysinfofs user/drivers/fat32fs
USER_BINARY_DIRS := $(filter-out $(USER_RETIRED_BINARY_DIRS) $(USER_COMPONENT_DIRS),$(foreach root,$(USER_BINARY_ROOTS),$(shell find $(root) -mindepth 1 -maxdepth 1 -type d -print 2>/dev/null)))
WUFF_INTERPRETER_DIR := user/programs/wuff-ld
ROOTFS_COMMAND_NAMES := cat cp env fs-latency grep kill ln ls mkdir mv ps rm shutdown tail write
ROOTFS_COMMAND_DIRS := $(wildcard $(addprefix user/commands/,$(ROOTFS_COMMAND_NAMES)))
ROOTFS_COMMAND_BINARIES := $(addprefix $(USER_BUILD_DIR)/,$(addsuffix .bin,$(ROOTFS_COMMAND_NAMES)))
WUFF_DYNAMIC_COMMAND_DIRS := $(ROOTFS_COMMAND_DIRS)
USER_STANDARD_BINARY_DIRS := $(filter-out $(WUFF_INTERPRETER_DIR) $(WUFF_DYNAMIC_COMMAND_DIRS),$(USER_BINARY_DIRS))
USER_DRIVER_DIRS := $(filter user/drivers/%,$(USER_BINARY_DIRS))
USER_SERVICE_DIRS := $(filter user/services/%,$(USER_BINARY_DIRS))
USER_NON_DRIVER_DIRS := $(filter-out $(USER_DRIVER_DIRS) $(USER_SERVICE_DIRS),$(USER_BINARY_DIRS))
USER_BINARIES := $(patsubst %,$(USER_BUILD_DIR)/%.bin,$(notdir $(USER_BINARY_DIRS)))
USER_BINARIES += $(filter-out $(USER_BINARIES),$(ROOTFS_COMMAND_BINARIES))
WUFF_INTERPRETER_SOURCES := $(wildcard $(WUFF_INTERPRETER_DIR)/*.c)
WUFF_INTERPRETER_OBJECTS := $(patsubst user/%.c,$(USER_BUILD_DIR)/%.o,$(WUFF_INTERPRETER_SOURCES))
WUFF_INTERPRETER_BINARY := $(USER_BUILD_DIR)/wuff-ld.bin
OPENBSD_CAT_SOURCE := vendor/openbsd/src/bin/cat/cat.c
OPENBSD_CAT_OBJECT := $(USER_PIC_BUILD_DIR)/vendor/openbsd/bin/cat/cat.o
OPENBSD_CP_SOURCES := $(wildcard vendor/openbsd/src/bin/cp/*.c)
OPENBSD_CP_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_CP_SOURCES))
OPENBSD_ENV_SOURCES := vendor/openbsd/src/usr.bin/env/env.c
OPENBSD_ENV_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_ENV_SOURCES))
OPENBSD_LN_SOURCES := vendor/openbsd/src/bin/ln/ln.c
OPENBSD_LN_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_LN_SOURCES))
OPENBSD_LS_SOURCES := $(wildcard vendor/openbsd/src/bin/ls/*.c)
OPENBSD_LS_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_LS_SOURCES))
OPENBSD_MKDIR_SOURCES := vendor/openbsd/src/bin/mkdir/mkdir.c
OPENBSD_MKDIR_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_MKDIR_SOURCES))
OPENBSD_MV_SOURCES := $(wildcard vendor/openbsd/src/bin/mv/*.c)
OPENBSD_MV_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_MV_SOURCES))
OPENBSD_RM_SOURCE := vendor/openbsd/src/bin/rm/rm.c
OPENBSD_RM_OBJECT := $(USER_PIC_BUILD_DIR)/vendor/openbsd/bin/rm/rm.o
OPENBSD_RM_PLATFORM_OBJECT := $(USER_PIC_BUILD_DIR)/ports/openbsd/rm/unsupported_random.o
OPENBSD_TAIL_SOURCES := $(wildcard vendor/openbsd/src/usr.bin/tail/*.c)
OPENBSD_TAIL_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_TAIL_SOURCES))
OPENBSD_GREP_SOURCES := $(wildcard vendor/openbsd/src/usr.bin/grep/*.c)
OPENBSD_GREP_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_GREP_SOURCES))
OPENBSD_REGEX_SOURCES := vendor/openbsd/src/lib/libc/regex/regcomp.c vendor/openbsd/src/lib/libc/regex/regexec.c \
	vendor/openbsd/src/lib/libc/regex/regerror.c vendor/openbsd/src/lib/libc/regex/regfree.c
OPENBSD_REGEX_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_REGEX_SOURCES))
OPENBSD_KILL_SOURCE := vendor/openbsd/src/bin/kill/kill.c
OPENBSD_KILL_SOURCES := $(OPENBSD_KILL_SOURCE) vendor/openbsd/src/lib/libc/gen/signame.c
OPENBSD_KILL_OBJECTS := $(patsubst vendor/openbsd/src/%.c,$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o,$(OPENBSD_KILL_SOURCES))
cat_WUFF_OBJECTS := $(OPENBSD_CAT_OBJECT)
cp_WUFF_OBJECTS := $(OPENBSD_CP_OBJECTS)
env_WUFF_OBJECTS := $(OPENBSD_ENV_OBJECTS)
fs-latency_WUFF_OBJECTS := $(USER_PIC_BUILD_DIR)/commands/fs-latency/main.o
grep_WUFF_OBJECTS := $(OPENBSD_GREP_OBJECTS) $(OPENBSD_REGEX_OBJECTS)
kill_WUFF_OBJECTS := $(OPENBSD_KILL_OBJECTS)
ln_WUFF_OBJECTS := $(OPENBSD_LN_OBJECTS)
ls_WUFF_OBJECTS := $(OPENBSD_LS_OBJECTS)
mkdir_WUFF_OBJECTS := $(OPENBSD_MKDIR_OBJECTS)
mv_WUFF_OBJECTS := $(OPENBSD_MV_OBJECTS)
ps_WUFF_OBJECTS := $(USER_PIC_BUILD_DIR)/commands/ps/main.o
rm_WUFF_OBJECTS := $(OPENBSD_RM_OBJECT) $(OPENBSD_RM_PLATFORM_OBJECT)
shutdown_WUFF_OBJECTS := $(USER_PIC_BUILD_DIR)/commands/shutdown/main.o
tail_WUFF_OBJECTS := $(OPENBSD_TAIL_OBJECTS)
write_WUFF_OBJECTS := $(USER_PIC_BUILD_DIR)/commands/write/main.o
setjmp_probe_EXTRA_OBJS := $(USER_BUILD_DIR)/programs/setjmp_probe/arch/$(ARCH)/registers.o
EMBEDDED_USER_NON_DRIVER_DIRS := $(filter-out $(ROOTFS_COMMAND_DIRS),$(USER_NON_DRIVER_DIRS))
USER_VIDEO_TERM_FONT := vendor/unifont/unifont_all-17.0.05.hex
USER_VIDEO_TERM_FONT_OBJECT := $(USER_BUILD_DIR)/drivers/video_term/unifont_all.o
USER_VIDEO_TERM_EMOJI_PNGS := $(wildcard vendor/noto_emoji/png/128/emoji_u*.png)
USER_VIDEO_TERM_EMOJI_HEADER := user/drivers/video_term/emoji_atlas.h
USER_VIDEO_TERM_EMOJI_ATLAS := $(USER_BUILD_DIR)/drivers/video_term/emoji_atlas.rgba
USER_VIDEO_TERM_EMOJI_ATLAS_OBJECT := $(USER_BUILD_DIR)/drivers/video_term/emoji_atlas.o
USER_VIDEO_TERM_EMOJI_ATLAS_BINARY_SYMBOL := $(subst .,_,$(subst /,_,$(USER_VIDEO_TERM_EMOJI_ATLAS)))
video_term_EXTRA_OBJS := $(USER_VIDEO_TERM_FONT_OBJECT) $(USER_VIDEO_TERM_EMOJI_ATLAS_OBJECT)
hund-unterbrict_EXTRA_OBJS := \
	$(USER_BUILD_DIR)/drivers/virtio_blk_channelio/device.o \
	$(USER_BUILD_DIR)/drivers/virtio_blk_channelio/mmio.o \
	$(USER_BUILD_DIR)/drivers/virtio_blk_channelio/runtime.o
EMBEDDED_FS_INPUTS := \
	$(foreach dir,$(EMBEDDED_USER_NON_DRIVER_DIRS),$(USER_BUILD_DIR)/$(notdir $(dir)).bin) \
	$(foreach dir,$(USER_DRIVER_DIRS),drivers/$(notdir $(dir))=$(USER_BUILD_DIR)/$(notdir $(dir)).bin) \
	$(foreach dir,$(USER_SERVICE_DIRS),services/$(notdir $(dir))=$(USER_BUILD_DIR)/$(notdir $(dir)).bin) \
	components/socket.wuff=$(RUST_SOCKET_BINARY) \
	components/pipe.wuff=$(RUST_PIPE_BINARY) \
	components/memfs.wuff=$(RUST_MEMFS_BINARY) \
	components/procfs.wuff=$(RUST_PROCFS_BINARY) \
	components/sysinfofs.wuff=$(RUST_SYSINFOFS_BINARY) \
	components/ext2fs.wuff=$(RUST_EXT2_BINARY) \
	components/fat32.wuff=$(RUST_FAT32_BINARY)
EMBEDDED_FS := $(TARGET_BUILD_DIR)/kernel/embedded.fs

.PRECIOUS: $(USER_BUILD_DIR)/lib/arch/%.o

all: $(BIN)

.PHONY: check-wuff-linker sysroot sysroot-all sysroot-wuff sysroot-wuff-all sysroot-smoke sysroot-smoke-all \
	wuff-smoke wuff-smoke-all wuff-dynamic-smoke wuff-dynamic-smoke-all rust-component-smoke \
	rust-component-smoke-all

check-wuff-linker:
	@command -v $(WUFF_LD) >/dev/null 2>&1 || { \
		echo "Missing .wuff linker: $(WUFF_LD)" >&2; \
		exit 1; \
	}

sysroot: $(SYSROOT_FILES)

sysroot-all:
	$(MAKE) ARCH=aarch64 sysroot
	$(MAKE) ARCH=riscv64 sysroot

sysroot-wuff: $(SYSROOT_FILES) $(SYSROOT_WUFF_FILES)

sysroot-wuff-all:
	$(MAKE) ARCH=aarch64 sysroot-wuff
	$(MAKE) ARCH=riscv64 sysroot-wuff

sysroot-smoke: $(SYSROOT_SMOKE_BINARY)

sysroot-smoke-all:
	$(MAKE) ARCH=aarch64 sysroot-smoke
	$(MAKE) ARCH=riscv64 sysroot-smoke

wuff-smoke: $(WUFF_SMOKE_BINARY)

wuff-smoke-all:
	$(MAKE) ARCH=aarch64 wuff-smoke
	$(MAKE) ARCH=riscv64 wuff-smoke

wuff-dynamic-smoke: $(WUFF_DYNAMIC_SMOKE_BINARY)

wuff-dynamic-smoke-all:
	$(MAKE) ARCH=aarch64 wuff-dynamic-smoke
	$(MAKE) ARCH=riscv64 wuff-dynamic-smoke

rust-component-smoke: $(RUST_COMPONENT_SMOKE_BINARY)

rust-component-smoke-all:
	$(MAKE) ARCH=aarch64 rust-component-smoke
	$(MAKE) ARCH=riscv64 rust-component-smoke

$(NEWLIB_CONFIGURE_STAMP): user/lib/newlib/configure.sh vendor/newlib/README.md vendor/newlib/src/configure
	mkdir -p $(NEWLIB_BUILD_DIR)
	cd $(NEWLIB_BUILD_DIR) && sh $(abspath user/lib/newlib/configure.sh) \
		$(NEWLIB_VENDOR_DIR) $(NEWLIB_TARGET) "$(NEWLIB_CFLAGS)"
	touch $@

$(NEWLIB_INSTALL_STAMP): $(NEWLIB_CONFIGURE_STAMP)
	$(MAKE) -C $(NEWLIB_BUILD_DIR) all-target-newlib
	$(MAKE) -C $(NEWLIB_BUILD_DIR) install-target-newlib DESTDIR=$(NEWLIB_INSTALL_DIR)
	touch $@

$(NEWLIB_PIC_CONFIGURE_STAMP): user/lib/newlib/configure.sh vendor/newlib/README.md vendor/newlib/src/configure Makefile
	rm -rf $(NEWLIB_PIC_BUILD_DIR) $(NEWLIB_PIC_INSTALL_DIR)
	mkdir -p $(NEWLIB_PIC_BUILD_DIR)
	cd $(NEWLIB_PIC_BUILD_DIR) && sh $(abspath user/lib/newlib/configure.sh) \
		$(NEWLIB_VENDOR_DIR) $(NEWLIB_TARGET) "$(NEWLIB_PIC_CFLAGS)"
	touch $@

$(NEWLIB_PIC_INSTALL_STAMP): $(NEWLIB_PIC_CONFIGURE_STAMP)
	$(MAKE) -C $(NEWLIB_PIC_BUILD_DIR) all-target-newlib
	$(MAKE) -C $(NEWLIB_PIC_BUILD_DIR) install-target-newlib DESTDIR=$(NEWLIB_PIC_INSTALL_DIR)
	touch $@

$(NEWLIB_LIBC_SELECTED_STAMP): $(NEWLIB_INSTALL_PREREQUISITE) Makefile
	mkdir -p $(NEWLIB_LIBC_SELECTED_DIR)
	for member in $(NEWLIB_LIBC_MEMBERS); do \
		count=`$(AR) t $(NEWLIB_LIBC_ARCHIVE) | awk -v expected=$$member '$$0 == expected { count++ } END { print count + 0 }'`; \
		if [ "$$count" -ne 1 ]; then \
			echo "Newlib libc member $$member occurs $$count times; expected exactly once" >&2; \
			exit 1; \
		fi; \
	done
	cd $(NEWLIB_LIBC_SELECTED_DIR) && $(AR) x $(NEWLIB_LIBC_ARCHIVE) $(NEWLIB_LIBC_MEMBERS)
	touch $@

$(NEWLIB_LIBC_OBJECTS): $(NEWLIB_LIBC_SELECTED_STAMP)
	test -f $@

$(NEWLIB_LIBC_VERIFY_OBJECT): $(NEWLIB_LIBC_OBJECTS) Makefile
	$(RAW_LD) -r -o $@ $(NEWLIB_LIBC_OBJECTS)
	for symbol in `$(NM) -u $@ | awk '{ print $$2 }'`; do \
		case " $(NEWLIB_LIBC_ALLOWED_UNDEFINED) " in \
			*" $$symbol "*) ;; \
			*) echo "Newlib libc selection has unapproved dependency $$symbol" >&2; exit 1 ;; \
		esac; \
	done

$(NEWLIB_PIC_LIBC_SELECTED_STAMP): $(NEWLIB_PIC_INSTALL_PREREQUISITE) Makefile
	rm -rf $(NEWLIB_PIC_LIBC_SELECTED_DIR)
	mkdir -p $(NEWLIB_PIC_LIBC_SELECTED_DIR)
	for member in $(NEWLIB_LIBC_MEMBERS); do \
		count=`$(AR) t $(NEWLIB_PIC_LIBC_ARCHIVE) | awk -v expected=$$member '$$0 == expected { count++ } END { print count + 0 }'`; \
		if [ "$$count" -ne 1 ]; then \
			echo "Newlib PIC libc member $$member occurs $$count times; expected exactly once" >&2; \
			exit 1; \
		fi; \
	done
	cd $(NEWLIB_PIC_LIBC_SELECTED_DIR) && $(AR) x $(NEWLIB_PIC_LIBC_ARCHIVE) $(NEWLIB_LIBC_MEMBERS)
	touch $@

$(NEWLIB_PIC_LIBC_OBJECTS): $(NEWLIB_PIC_LIBC_SELECTED_STAMP)
	test -f $@

$(NEWLIB_PIC_LIBC_VERIFY_OBJECT): $(NEWLIB_PIC_LIBC_OBJECTS) Makefile
	$(RAW_LD) -r -o $@ $(NEWLIB_PIC_LIBC_OBJECTS)
	for symbol in `$(NM) -u $@ | awk '{ print $$2 }'`; do \
		case " $(NEWLIB_LIBC_ALLOWED_UNDEFINED) " in \
		*" $$symbol "*) ;; \
		*) echo "Newlib PIC libc selection has unapproved dependency $$symbol" >&2; exit 1 ;; \
		esac; \
	done

$(SYSROOT_INCLUDE_STAMP): $(NEWLIB_INSTALL_PREREQUISITE) $(shell find user/lib/newlib/include user/lib/arch/$(ARCH)/include -type f -print)
	rm -rf $(SYSROOT_INCLUDE_DIR)
	mkdir -p $(SYSROOT_INCLUDE_DIR)/machine $(SYSROOT_INCLUDE_DIR)/sys
	for header in $(NEWLIB_MATH_HEADERS); do \
		cp $(NEWLIB_INCLUDE_DIR)/$$header $(SYSROOT_INCLUDE_DIR)/$$header; \
	done
	cp -R user/lib/newlib/include/. $(SYSROOT_INCLUDE_DIR)/
	cp -R user/lib/arch/$(ARCH)/include/. $(SYSROOT_INCLUDE_DIR)/
	touch $@

$(SYSROOT_CRT): $(USER_CRT_OBJECT)
	mkdir -p `dirname $@`
	cp $< $@

$(SYSROOT_LIBC): $(USER_LIBC_OBJECTS) $(NEWLIB_LIBC_OBJECTS) $(NEWLIB_LIBC_VERIFY_OBJECT)
	mkdir -p `dirname $@`
	rm -f $@
	$(AR) rcsP $@ $(USER_LIBC_OBJECTS) $(NEWLIB_LIBC_OBJECTS)
	$(RANLIB) $@
	for member in `$(AR) t $@ | awk '/^libc_a-/ { print }'`; do \
		case " $(NEWLIB_LIBC_MEMBERS) " in \
			*" $$member "*) ;; \
			*) echo "HundeOS libc contains unapproved Newlib member $$member" >&2; exit 1 ;; \
		esac; \
	done

$(SYSROOT_LIBHUNDE): $(USER_HUNDE_OBJECTS)
	mkdir -p `dirname $@`
	rm -f $@
	$(AR) rcsP $@ $^
	$(RANLIB) $@

$(SYSROOT_LIBM): $(NEWLIB_INSTALL_PREREQUISITE)
	mkdir -p `dirname $@`
	cp $(NEWLIB_LIB_DIR)/libm.a $@

$(SYSROOT_COMPILER_RUNTIME): $(COMPILER_RUNTIME_PIC_OBJECT) Makefile
	mkdir -p `dirname $@`
	$(AR) rcs $@ $<
	$(RANLIB) $@
	if $(NM) -u $@ | awk '$$1 == "U" { found = 1 } END { exit !found }'; then $(NM) -u $@ >&2; exit 1; fi

$(SYSROOT_WUFF_LIBC): $(USER_LIBC_PIC_OBJECTS) $(USER_HUNDE_PIC_OBJECTS) $(NEWLIB_PIC_LIBC_OBJECTS) \
		$(NEWLIB_PIC_LIBC_VERIFY_OBJECT) $(WUFF_PROGRAM_VERSION_SCRIPT) Makefile | check-wuff-linker
	mkdir -p `dirname $@`
	$(WUFF_LD) -shared -soname libc.wuff.1 --hash-style=sysv -z defs -z now -z relro --gc-sections \
		$(WUFF_PROGRAM_LINK_FLAGS) \
		--start-group $(USER_LIBC_PIC_OBJECTS) $(NEWLIB_PIC_LIBC_OBJECTS) \
		$(USER_HUNDE_PIC_OBJECTS) $(WUFF_LIBGCC) --end-group -o $@
	$(READELF) -h $@ | awk '/Type:/ { if ($$2 != "DYN") exit 1; found = 1 } END { exit !found }'
	$(READELF) -d $@ | awk '/SONAME/ { if ($$0 !~ /\[libc\.wuff\.1\]/) exit 1; found = 1 } END { exit !found }'
	if $(READELF) --dyn-syms -W $@ | awk '$$8 == "_stack" { found = 1 } END { exit !found }'; then echo "libc.wuff.1 exports synthetic _stack" >&2; exit 1; fi
	if $(READELF) -d $@ | grep -q '(TEXTREL)'; then echo "libc.wuff.1 contains text relocations" >&2; exit 1; fi
	if $(READELF) -lW $@ | grep -Eq '^[[:space:]]*TLS[[:space:]]'; then echo "libc.wuff.1 contains TLS" >&2; exit 1; fi
	if $(READELF) -dW $@ | grep -Eq '\((PREINIT_ARRAY|INIT|INIT_ARRAY|FINI|FINI_ARRAY)\)'; then echo "libc.wuff.1 contains initialization metadata" >&2; exit 1; fi
	if $(NM) -u $@ | grep -q .; then $(NM) -u $@ >&2; exit 1; fi

$(SYSROOT_WUFF_LIBC_LINK): $(SYSROOT_WUFF_LIBC)
	ln -sf libc.wuff.1 $@

$(SYSROOT_WUFF_LIBC_COMPAT): $(SYSROOT_WUFF_LIBC)
	mkdir -p $(SYSROOT_WUFF_LINK_DIR)
	ln -sf ../libc.wuff.1 $@

$(SYSROOT_SMOKE_OBJECT): $(SYSROOT_SMOKE_SOURCE) $(SYSROOT_INCLUDE_STAMP)
	mkdir -p `dirname $@`
	$(CC) $(BASH_CFLAGS) --sysroot=$(abspath $(SYSROOT_DIR)) \
		-isystem $(abspath $(SYSROOT_INCLUDE_DIR)) -c $< -o $@

$(SYSROOT_SMOKE_BINARY): $(SYSROOT_SMOKE_OBJECT) $(SYSROOT_CRT) $(SYSROOT_LIBC) $(SYSROOT_LIBHUNDE) $(SYSROOT_LIBM)
	$(LD) -nostdlib $(ARCH_USER_CFLAGS) $(SYSROOT_CRT) $< -L$(SYSROOT_LIB_DIR) -Wl,-e,_start -Wl,--gc-sections \
		-Wl,--start-group -lc -lhunde -lm -lgcc -Wl,--end-group -o $@

$(WUFF_SMOKE_OBJECT): $(WUFF_SMOKE_SOURCE) Makefile
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -fvisibility=hidden -c $< -o $@

$(WUFF_SMOKE_BINARY): $(WUFF_SMOKE_OBJECT) Makefile | check-wuff-linker
	$(WUFF_LD) -shared -soname wuff-smoke.wuff.1 --hash-style=sysv -z defs -z now -z norelro $< -o $@
	$(READELF) -h $@ | awk '/Type:/ { if ($$2 != "DYN") exit 1; found = 1 } END { exit !found }'
	$(READELF) -d $@ | awk '/SONAME/ { if ($$0 !~ /\[wuff-smoke\.wuff\.1\]/) exit 1; found = 1 } END { exit !found }'
	if $(READELF) -d $@ | grep -q '(NEEDED)'; then $(READELF) -d $@ >&2; exit 1; fi
	if $(READELF) -d $@ | grep -q '(TEXTREL)'; then echo "wuff-smoke.wuff contains text relocations" >&2; exit 1; fi
	if $(READELF) -lW $@ | grep -q 'GNU_RELRO'; then $(READELF) -lW $@ >&2; exit 1; fi
	$(READELF) -Ws $@ | awk '$$8 == "wuff_add" && $$5 == "GLOBAL" && $$7 != "UND" { found = 1 } END { exit !found }'

.PHONY: rust-test rust-io rust-format rust-format-check rust-lint rust-lint-host rust-lint-target
rust-format:
	cargo +$(RUST_TOOLCHAIN) fmt --manifest-path user/Cargo.toml --all

rust-format-check:
	cargo +$(RUST_TOOLCHAIN) fmt --manifest-path user/Cargo.toml --all --check

rust-lint: rust-lint-host rust-lint-target

rust-lint-host:
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --workspace --all-targets \
		--target-dir $(RUST_CARGO_DIR) -- -D warnings
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml -p hunde --no-default-features \
		--all-targets --target-dir $(RUST_CARGO_DIR) -- -D warnings

rust-lint-target:
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) \
		--target $(RUST_COMPONENT_TARGET) -p hunde-io-host --features service --lib -- -D warnings
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) \
		--target $(RUST_COMPONENT_TARGET) -p hunde-socket --features component --lib --bin socket-component -- -D warnings
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) \
		--target $(RUST_COMPONENT_TARGET) -p hunde-pipe --features component --lib --bin pipe-component -- -D warnings
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) \
		--target $(RUST_COMPONENT_TARGET) -p hunde-memfs --features component --lib --bin memfs-component -- -D warnings
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) \
		--target $(RUST_COMPONENT_TARGET) -p hunde-rust-smoke --features component --bin rust-smoke-component -- -D warnings

	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) --target $(RUST_COMPONENT_TARGET) -p hunde-procfs --features component --lib --bin procfs-component -- -D warnings
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) --target $(RUST_COMPONENT_TARGET) -p hunde-sysinfofs --features component --lib --bin sysinfofs-component -- -D warnings
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) --target $(RUST_COMPONENT_TARGET) -p hunde-ext2fs --features component --lib --bin ext2fs-component -- -D warnings
	cargo +$(RUST_TOOLCHAIN) clippy --manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) --target $(RUST_COMPONENT_TARGET) -p hunde-fat32fs --features component --lib --bin fat32fs-component -- -D warnings

rust-test:
	cargo +$(RUST_TOOLCHAIN) test --manifest-path user/Cargo.toml --workspace --target-dir $(RUST_CARGO_DIR)

rust-io: $(RUST_MEMFS_BINARY) $(RUST_PROCFS_BINARY) $(RUST_SYSINFOFS_BINARY) $(RUST_EXT2_BINARY) $(RUST_FAT32_BINARY) $(RUST_PIPE_BINARY) $(RUST_SOCKET_BINARY) $(USER_BUILD_DIR)/hunde-io-host.bin

$(RUST_IO_HOST_ARCHIVE): $(RUST_IO_SOURCES) rust-toolchain.toml Makefile
	RUSTFLAGS="-C relocation-model=pic" cargo +$(RUST_TOOLCHAIN) build \
		--manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) --release \
		--target $(RUST_COMPONENT_TARGET) -p hunde-io-host --features service

$(RUST_SOCKET_BINARY): RUST_IO_COMPONENT_NAME = socket
$(RUST_SOCKET_BINARY): RUST_IO_COMPONENT_DIRECTORY = user/drivers/socket
$(RUST_SOCKET_BINARY): user/drivers/socket/exports.map
$(RUST_PIPE_BINARY): RUST_IO_COMPONENT_NAME = pipe
$(RUST_PIPE_BINARY): RUST_IO_COMPONENT_DIRECTORY = user/services/hund-rohr
$(RUST_PIPE_BINARY): user/services/hund-rohr/exports.map
$(RUST_MEMFS_BINARY): RUST_IO_COMPONENT_NAME = memfs
$(RUST_MEMFS_BINARY): RUST_IO_COMPONENT_DIRECTORY = user/drivers/memfs
$(RUST_MEMFS_BINARY): user/drivers/memfs/exports.map
$(RUST_PROCFS_BINARY): RUST_IO_COMPONENT_NAME = procfs
$(RUST_PROCFS_BINARY): RUST_IO_COMPONENT_DIRECTORY = user/drivers/procfs
$(RUST_PROCFS_BINARY): user/drivers/procfs/exports.map
$(RUST_SYSINFOFS_BINARY): RUST_IO_COMPONENT_NAME = sysinfofs
$(RUST_SYSINFOFS_BINARY): RUST_IO_COMPONENT_DIRECTORY = user/drivers/sysinfofs
$(RUST_SYSINFOFS_BINARY): user/drivers/sysinfofs/exports.map
$(RUST_EXT2_BINARY): RUST_IO_COMPONENT_NAME = ext2fs
$(RUST_EXT2_BINARY): RUST_IO_COMPONENT_DIRECTORY = user/drivers/ext2fs
$(RUST_EXT2_BINARY): RUST_IO_COMPONENT_ENTRY = hunde_filesystem_component_entry_v1
$(RUST_EXT2_BINARY): user/drivers/ext2fs/exports.map
$(RUST_FAT32_BINARY): RUST_IO_COMPONENT_NAME = fat32fs
$(RUST_FAT32_BINARY): RUST_IO_COMPONENT_DIRECTORY = user/drivers/fat32fs
$(RUST_FAT32_BINARY): RUST_IO_COMPONENT_ENTRY = hunde_filesystem_component_entry_v1
$(RUST_FAT32_BINARY): user/drivers/fat32fs/exports.map

$(RUST_SOCKET_BINARY) $(RUST_PIPE_BINARY) $(RUST_MEMFS_BINARY) $(RUST_PROCFS_BINARY) $(RUST_SYSINFOFS_BINARY) $(RUST_EXT2_BINARY) $(RUST_FAT32_BINARY): $(RUST_IO_SOURCES) rust-toolchain.toml Makefile | check-wuff-linker
	RUSTFLAGS="-C relocation-model=pic" cargo +$(RUST_TOOLCHAIN) rustc \
		--manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) --release \
		--target $(RUST_COMPONENT_TARGET) -p hunde-$(RUST_IO_COMPONENT_NAME) --features component --bin $(RUST_IO_COMPONENT_NAME)-component -- \
		-C linker=$(WUFF_LD) -C linker-flavor=ld \
		-C link-args="-shared --soname $(RUST_IO_COMPONENT_NAME).wuff.1 --hash-style=sysv -z defs -z now -z relro -z text --version-script=$(abspath $(RUST_IO_COMPONENT_DIRECTORY)/exports.map) -u $(RUST_IO_COMPONENT_ENTRY)"
	cp $(RUST_RELEASE_DIR)/$(RUST_IO_COMPONENT_NAME)-component $@
	if $(READELF) -dW $@ | grep -Eq '\((NEEDED|TEXTREL|PREINIT_ARRAY|INIT|INIT_ARRAY|FINI|FINI_ARRAY)\)'; then $(READELF) -dW $@ >&2; exit 1; fi
	if $(READELF) -lW $@ | grep -Eq '^[[:space:]]*(INTERP|TLS)[[:space:]]'; then $(READELF) -lW $@ >&2; exit 1; fi
	$(READELF) -lW $@ | grep -q 'GNU_RELRO'
	$(READELF) --dyn-syms -W $@ | awk '$$7 != "UND" && $$5 == "GLOBAL" { count++; if ($$8 != "$(RUST_IO_COMPONENT_ENTRY)") bad = 1 } END { exit bad || count != 1 }'
	$(READELF) -rW $@ | awk -v allowed="$(WUFF_PROGRAM_RELOCATIONS)" '$$3 ~ /^R_/ { if (index(" " allowed " ", " " $$3 " ") == 0) { print; bad = 1 } } END { exit bad }'

$(RUST_COMPONENT_SMOKE_BINARY): $(RUST_COMPONENT_SMOKE_SOURCE) user/components/rust_smoke/Cargo.toml \
        $(RUST_IO_SOURCES) $(RUST_COMPONENT_SMOKE_MAP) rust-toolchain.toml Makefile | check-wuff-linker
	RUSTFLAGS="-C relocation-model=pic" cargo +$(RUST_TOOLCHAIN) rustc \
		--manifest-path user/Cargo.toml --target-dir $(RUST_CARGO_DIR) --release \
		--target $(RUST_COMPONENT_TARGET) -p hunde-rust-smoke --features component --bin rust-smoke-component -- \
		-C linker=$(WUFF_LD) -C linker-flavor=ld \
		-C link-args="-shared --soname hunde-rust-smoke.wuff.1 --hash-style=sysv -z defs -z now -z relro -z text --version-script=$(abspath $(RUST_COMPONENT_SMOKE_MAP)) -u hunde_component_entry_v1"
	mkdir -p `dirname $@`
	cp $(RUST_RELEASE_DIR)/rust-smoke-component $@
	$(READELF) -h $@ | awk '/Type:/ { if ($$2 != "DYN") exit 1; found = 1 } END { exit !found }'
	$(READELF) -dW $@ | awk '/SONAME/ { if ($$0 !~ /\[hunde-rust-smoke\.wuff\.1\]/) exit 1; found = 1 } END { exit !found }'
	if $(READELF) -dW $@ | grep -q '(NEEDED)'; then $(READELF) -dW $@ >&2; exit 1; fi
	if $(READELF) -dW $@ | grep -q '(TEXTREL)'; then echo "$@ contains text relocations" >&2; exit 1; fi
	$(READELF) -lW $@ | grep -q 'GNU_RELRO'
	if $(READELF) -lW $@ | grep -Eq '^[[:space:]]*(INTERP|TLS)[[:space:]]'; then $(READELF) -lW $@ >&2; exit 1; fi
	if $(READELF) -dW $@ | grep -Eq '\((PREINIT_ARRAY|INIT|INIT_ARRAY|FINI|FINI_ARRAY)\)'; then $(READELF) -dW $@ >&2; exit 1; fi
	$(READELF) --dyn-syms -W $@ | awk '$$7 != "UND" && $$5 == "GLOBAL" { count++; if ($$8 != "hunde_component_entry_v1") bad = 1 } END { exit bad || count != 1 }'
	$(READELF) -rW $@ | awk -v allowed="$(WUFF_PROGRAM_RELOCATIONS)" '$$3 ~ /^R_/ { if (index(" " allowed " ", " " $$3 " ") == 0) { print; bad = 1 } } END { exit bad }'

$(WUFF_DYNAMIC_SMOKE_OBJECT): $(WUFF_DYNAMIC_SMOKE_SOURCE) $(SYSROOT_INCLUDE_STAMP) Makefile
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) --sysroot=$(abspath $(SYSROOT_DIR)) \
		-isystem $(abspath $(SYSROOT_INCLUDE_DIR)) -c $< -o $@

$(WUFF_DYNAMIC_SMOKE_BINARY): $(WUFF_DYNAMIC_SMOKE_OBJECT) $(USER_CRT_PIC_OBJECT) $(SYSROOT_WUFF_FILES) \
        $(WUFF_PROGRAM_VERSION_SCRIPT) Makefile | check-wuff-linker
	$(WUFF_LD) -pie --dynamic-linker /sbin/wuff-ld -e _start --hash-style=sysv -z now -z relro --gc-sections \
		$(WUFF_PROGRAM_LINK_FLAGS) $(USER_CRT_PIC_OBJECT) $(WUFF_DYNAMIC_SMOKE_OBJECT) \
		-L$(SYSROOT_LIB_DIR) -l:libc.wuff.1 -o $@
	$(READELF) -h $@ | awk '/Type:/ { if ($$2 != "DYN") exit 1; found = 1 } END { exit !found }'
	$(READELF) -h $@ | awk '/Entry point address:/ { if ($$4 == "0x0") exit 1; found = 1 } END { exit !found }'
	$(READELF) -lW $@ | awk '/Requesting program interpreter:/ { if ($$0 !~ /\/sbin\/wuff-ld\]/) exit 1; found = 1 } END { exit !found }'
	$(READELF) -dW $@ | awk '/\(NEEDED\)/ { if ($$0 !~ /\[libc\.wuff\.1\]/) exit 1; found = 1 } END { exit !found }'
	if $(READELF) -dW $@ | grep -q '(TEXTREL)'; then echo "wuff-dynamic-smoke contains text relocations" >&2; exit 1; fi
	$(READELF) -lW $@ | grep -q 'GNU_RELRO'
	if $(READELF) -lW $@ | grep -Eq '^[[:space:]]*TLS[[:space:]]'; then echo "wuff-dynamic-smoke contains TLS" >&2; exit 1; fi
	if $(READELF) -dW $@ | grep -Eq '\((PREINIT_ARRAY|INIT|INIT_ARRAY|FINI|FINI_ARRAY)\)'; then echo "wuff-dynamic-smoke contains initialization metadata" >&2; exit 1; fi
	if $(READELF) --dyn-syms -W $@ | awk '$$8 == "_stack" { found = 1 } END { exit !found }'; then echo "wuff-dynamic-smoke exports synthetic _stack" >&2; exit 1; fi
	$(READELF) -rW $@ | awk '$$3 ~ /^R_/ { found = 1; if ($$3 != "$(WUFF_JUMP_SLOT_RELOCATION)") { print; bad = 1 } } END { exit bad || !found }'

.PHONY: bash bash-configure bash-build bash-configure-all bash-build-all

bash: bash-build

bash-configure: $(BASH_PORT_CONFIGURE_STAMP)

bash-build: $(BASH_PORT_BINARY)

$(BASH_PORT_BINARY): $(BASH_PORT_CONFIGURE_STAMP) $(SYSROOT_WUFF_FILES) $(USER_CRT_PIC_OBJECT) \
		$(NEWLIB_PIC_INSTALL_PREREQUISITE) $(WUFF_PROGRAM_VERSION_SCRIPT) $(BASH_RUNTIME_PREREQUISITES)
	$(MAKE) -C $(BASH_PORT_BUILD_DIR) $(BASH_CAPTURE_ARGUMENTS) bash
	$(READELF) -h $@ | awk '/Type:/ { if ($$2 != "DYN") exit 1; found = 1 } END { exit !found }'
	$(READELF) -h $@ | awk '/Entry point address:/ { if ($$4 == "0x0") exit 1; found = 1 } END { exit !found }'
	$(READELF) -lW $@ | awk '/Requesting program interpreter:/ { if ($$0 !~ /\/sbin\/wuff-ld\]/) exit 1; found = 1 } END { exit !found }'
	$(READELF) -dW $@ | awk '/\(NEEDED\)/ { if ($$0 !~ /\[libc\.wuff\.1\]/ || found) exit 1; found = 1 } END { exit !found }'
	$(READELF) -dW $@ | awk '/\(HASH\)/ { found = 1 } END { exit !found }'
	if $(READELF) -dW $@ | grep -q '(TEXTREL)'; then echo "$@ contains text relocations" >&2; exit 1; fi
	$(READELF) -lW $@ | grep -q 'GNU_RELRO'
	if $(READELF) -lW $@ | grep -Eq '^[[:space:]]*TLS[[:space:]]'; then echo "$@ contains TLS" >&2; exit 1; fi
	if $(READELF) -dW $@ | grep -Eq '\((PREINIT_ARRAY|INIT|INIT_ARRAY|FINI|FINI_ARRAY)\)'; then echo "$@ contains initialization metadata" >&2; exit 1; fi
	if $(READELF) --dyn-syms -W $@ | awk '$$8 == "_stack" { found = 1 } END { exit !found }'; then echo "$@ exports synthetic _stack" >&2; exit 1; fi
	$(READELF) -rW $@ | awk -v allowed="$(WUFF_PROGRAM_RELOCATIONS)" '$$3 ~ /^R_/ { found = 1; if (index(" " allowed " ", " " $$3 " ") == 0) { print; bad = 1 } } END { exit bad || !found }'

bash-configure-all:
	$(MAKE) ARCH=aarch64 bash-configure
	$(MAKE) ARCH=riscv64 bash-configure

bash-build-all:
	$(MAKE) ARCH=aarch64 bash-build
	$(MAKE) ARCH=riscv64 bash-build

$(BASH_PORT_SOURCE_STAMP): vendor/bash/README.md vendor/bash/SHA256SUMS \
		vendor/bash/src/patchlevel.h $(BASH_PORT_PATCHES)
	mkdir -p $(BASH_PORT_SOURCE_DIR)
	cp -R $(BASH_VENDOR_DIR)/. $(BASH_PORT_SOURCE_DIR)/
	for patch_file in $(abspath $(BASH_PORT_PATCHES)); do \
		patch -s -d $(BASH_PORT_SOURCE_DIR) -p1 < $$patch_file; \
	done
	touch $(BASH_PORT_SOURCE_DIR)/configure \
		$(BASH_PORT_SOURCE_DIR)/y.tab.c \
		$(BASH_PORT_SOURCE_DIR)/y.tab.h \
		$(BASH_PORT_SOURCE_DIR)/parser-built
	touch $@

$(BASH_PORT_CONFIGURE_STAMP): $(BASH_PORT_SOURCE_STAMP) $(BASH_PORT_CONFIG_CACHE) Makefile \
		$(SYSROOT_INCLUDE_STAMP) $(SYSROOT_WUFF_FILES) $(USER_CRT_PIC_OBJECT) $(NEWLIB_PIC_INSTALL_PREREQUISITE) \
		$(WUFF_PROGRAM_VERSION_SCRIPT) $(BASH_RUNTIME_PREREQUISITES)
	rm -rf $(BASH_PORT_BUILD_DIR)
	mkdir -p $(BASH_PORT_BUILD_DIR)
	cp $(BASH_PORT_CONFIG_CACHE) $(BASH_BUILD_CONFIG_CACHE)
	cd $(BASH_PORT_BUILD_DIR) && \
		CONFIG_SITE=/dev/null \
		CC="$(BASH_CC)" \
		CC_FOR_BUILD=cc \
		AR=$(CROSS_COMPILE)ar \
		RANLIB=$(CROSS_COMPILE)ranlib \
		STRIP=$(CROSS_COMPILE)strip \
		CPPFLAGS="$(BASH_CPPFLAGS)" \
		CFLAGS="$(BASH_CFLAGS)" \
		LDFLAGS="$(BASH_LDFLAGS)" \
		LIBS="$(BASH_LIBS)" \
		../source/configure \
			--cache-file=$(BASH_BUILD_CONFIG_CACHE) \
			--build=$(BASH_BUILD) \
			--host=$(BASH_HOST) \
			--prefix=/usr/local \
			--disable-nls \
			--disable-net-redirections \
			--enable-job-control \
			--enable-readline \
			--without-bash-malloc
	touch $@

# Semihosting version (better for QEMU debugging)
elf: $(ELF)
kernel: hundelint user_binaries $(BIN)

# Capture real kernel and userspace flags without compiling or launching QEMU.
.PHONY: compile-commands compile-commands-capture
compile-commands:
	$(PYTHON) tools/compile_commands.py --make "$(MAKE)"

compile-commands-capture:
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 elf
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 riscv64-bringup-elf
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 \
		BOOT_PLATFORM=milkv_duo BSP_DEFINES=-DBSP_MILKV_DUO ARCH_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		PLATFORM_BUILD_ROOT=build/riscv64/milkv_duo \
		build/riscv64/milkv_duo/kernel.elf
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 \
		BOOT_PLATFORM=milkv_duo_s BSP_DEFINES=-DBSP_MILKV_DUO_S ARCH_CFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" \
		PLATFORM_BUILD_ROOT=build/riscv64/milkv_duo_s \
		build/riscv64/milkv_duo_s/kernel.elf build/riscv64/milkv_duo_s/handoff.elf
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 \
		BOOT_PLATFORM=milkv_duo_s BSP_DEFINES="-DBSP_MILKV_DUO_S -DKERNEL_SERIAL_TRACE -DKERNEL_MEMORY_TRACE -DAARCH64_BOOT_MAX_CPUS=1" \
		PLATFORM_BUILD_ROOT=build/aarch64/milkv_duo_s \
		build/aarch64/milkv_duo_s/kernel.elf build/aarch64/milkv_duo_s/handoff.elf
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 \
		build/riscv64/qemu_virt/kernel/arch/riscv64/page_table.o \
		build/riscv64/qemu_virt/kernel/arch/riscv64/mmu.o
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 \
		BOOT_PLATFORM=riscv_virt_embedded \
		build/riscv64/riscv_virt_embedded/kernel/arch/riscv64/boot/riscv_virt_embedded/platform.o
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 \
		build/riscv64/user/hello_world.bin
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 \
		build/aarch64/user/setjmp_probe.bin
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 \
		build/riscv64/user/setjmp_probe.bin
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 sysroot-smoke
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 sysroot-smoke
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 wuff-smoke
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 wuff-smoke
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 wuff-dynamic-smoke
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 wuff-dynamic-smoke
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 rust-component-smoke
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 rust-component-smoke
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 \
		build/aarch64/user/lib/hunde/native.o build/aarch64/user/lib/hunde/request.o build/aarch64/user/lib/hunde/scheduler.o
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 \
		build/riscv64/user/lib/hunde/native.o build/riscv64/user/lib/hunde/request.o build/riscv64/user/lib/hunde/scheduler.o
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 \
		build/riscv64/sysroot/usr/lib/libcompiler_rt_pic.a
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 bash-build
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 bash-build
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 \
		$(addprefix build/aarch64/user/,$(addsuffix .bin,$(ROOTFS_COMMAND_NAMES)))
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=riscv64 \
		$(addprefix build/riscv64/user/,$(addsuffix .bin,$(ROOTFS_COMMAND_NAMES)))
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 \
		BOOT_PLATFORM=pi_uefi build/aarch64/pi_uefi/kernel/arch/aarch64/boot/pi_uefi/uefi_devices.o
	$(MAKE) -Bnw COMPILE_COMMANDS=1 ARCH=aarch64 \
		BOOT_PLATFORM=pi_uefi UEFI_DEVICE_PLATFORM=qemu_virt build/aarch64/pi_uefi/kernel/arch/aarch64/boot/qemu_virt/uefi_devices.o

setup: python-venv
	mkdir -p $(BUILD_ROOT)

$(PYTHON_ENV_STAMP): pyproject.toml
	$(UV) sync
	mkdir -p `dirname $@`
	touch $@

python-venv: $(PYTHON_ENV_STAMP)

$(DISK_IMG): $(PYTHON_ENV_STAMP) tools/populate_fat32_image.py $(DISK_TEST_FILE) $(DISK_LONG_FILE) $(DISK_LARGE_FILE)
	mkdir -p `dirname $@`
	dd if=/dev/zero of=$@ bs=1048576 count=$(DISK_SIZE_MB)
	$(MKFS_FAT) -F 32 -S 512 -s 1 -R 32 -n $(DISK_LABEL) $@
	$(PYTHON) tools/populate_fat32_image.py $@ $(DISK_TEST_NAME) $(DISK_TEST_FILE)
	$(PYTHON) tools/populate_fat32_image.py $@ "$(DISK_LONG_NAME)" $(DISK_LONG_FILE)
	$(PYTHON) tools/populate_fat32_image.py $@ $(DISK_LARGE_NAME) $(DISK_LARGE_FILE)

$(CHANNELIO_DISK_IMG): $(PYTHON_ENV_STAMP) tools/populate_fat32_image.py $(DISK_TEST_FILE)
	mkdir -p `dirname $@`
	dd if=/dev/zero of=$@ bs=1048576 count=$(CHANNELIO_DISK_SIZE_MB)
	$(MKFS_FAT) -F 32 -S 512 -s 1 -R 32 -n $(CHANNELIO_DISK_LABEL) $@
	$(PYTHON) tools/populate_fat32_image.py $@ $(CHANNELIO_DISK_TEST_NAME) $(DISK_TEST_FILE)

$(CHANNELIO_EXT2_IMG): $(shell find $(EXT2_ROOT_DIR) -type f -print)
	mkdir -p `dirname $@`
	rm -f $@.tmp
	dd if=/dev/zero of=$@.tmp bs=1048576 count=$(EXT2_SIZE_MB)
	@if [ "$(EXT2_MKFS)" = "genext2fs" ]; then \
		$(EXT2_MKFS) -b $$(( $(EXT2_SIZE_MB) * 1024 )) -B $(EXT2_IMG_BLOCK_SIZE) -N $(EXT2_INODE_COUNT) -L $(CHANNELIO_EXT2_LABEL) -d $(EXT2_ROOT_DIR) $@.tmp; \
	else \
		$(EXT2_MKFS) -q -F -t ext2 -b $(EXT2_IMG_BLOCK_SIZE) -I $(EXT2_INODE_SIZE) -N $(EXT2_INODE_COUNT) -L $(CHANNELIO_EXT2_LABEL) -d $(EXT2_ROOT_DIR) $@.tmp; \
	fi
	mv $@.tmp $@

$(HOSTED_EXT2_IMG): Makefile config/hosted-ext2/hello.txt $(USER_BUILD_DIR)/cat.bin
	mkdir -p `dirname $@`
	rm -rf $(HOSTED_EXT2_ROOT_DIR)
	mkdir -p $(HOSTED_EXT2_ROOT_DIR)
	cp -R config/hosted-ext2/. $(HOSTED_EXT2_ROOT_DIR)/
	cp $(USER_BUILD_DIR)/cat.bin $(HOSTED_EXT2_ROOT_DIR)/cat
	chmod 755 $(HOSTED_EXT2_ROOT_DIR)/cat
	rm -f $@.tmp
	dd if=/dev/zero of=$@.tmp bs=1048576 count=16
	@if [ "$(EXT2_MKFS)" = "genext2fs" ]; then \
		$(EXT2_MKFS) -b 16384 -B $(EXT2_IMG_BLOCK_SIZE) -N $(EXT2_INODE_COUNT) -L HOSTED_EXT2 -d $(HOSTED_EXT2_ROOT_DIR) $@.tmp; \
	else \
		$(EXT2_MKFS) -q -F -t ext2 -b $(EXT2_IMG_BLOCK_SIZE) -I $(EXT2_INODE_SIZE) -N $(EXT2_INODE_COUNT) -L HOSTED_EXT2 -d $(HOSTED_EXT2_ROOT_DIR) $@.tmp; \
	fi
	mv $@.tmp $@

$(SD_CARD_FAT32_IMG): $(PYTHON_ENV_STAMP) tools/populate_fat32_image.py $(SD_CARD_FAT32_README_FILE) $(SD_CARD_FAT32_CONFIG_FILE) $(SD_CARD_FAT32_NOTE_FILE)
	mkdir -p `dirname $@`
	dd if=/dev/zero of=$@ bs=1048576 count=$(SD_CARD_FAT32_SIZE_MB)
	$(MKFS_FAT) -F 32 -S 512 -s 1 -R 32 -n $(SD_CARD_FAT32_LABEL) $@
	$(PYTHON) tools/populate_fat32_image.py $@ README.TXT $(SD_CARD_FAT32_README_FILE)
	$(PYTHON) tools/populate_fat32_image.py $@ CONFIG.TXT $(SD_CARD_FAT32_CONFIG_FILE)
	$(PYTHON) tools/populate_fat32_image.py $@ DOCS/NOTE.TXT $(SD_CARD_FAT32_NOTE_FILE)

$(ROOTFS_STAMP): Makefile $(shell find $(EXT2_ROOT_DIR) -type f -print) $(ROOTFS_COMMAND_BINARIES) \
        $(BASH_PORT_BINARY) $(SYSROOT_WUFF_LIBC)
	rm -rf $(ROOTFS_DIR)
	mkdir -p $(ROOTFS_DIR)/usr/lib $(ROOTFS_DIR)/usr/local/bin $(ROOTFS_DIR)/var/log
	cp -R $(EXT2_ROOT_DIR)/. $(ROOTFS_DIR)/
	$(foreach binary,$(ROOTFS_COMMAND_BINARIES),cp $(binary) $(ROOTFS_DIR)/usr/local/bin/$(basename $(notdir $(binary)));)
	cp $(SYSROOT_WUFF_LIBC) $(ROOTFS_DIR)/usr/lib/libc.wuff.1
	cp $(BASH_PORT_BINARY) $(ROOTFS_DIR)/usr/local/bin/bash
	touch $@

$(EXT2_IMG): $(ROOTFS_STAMP)
	mkdir -p `dirname $@`
	rm -f $@.tmp
	dd if=/dev/zero of=$@.tmp bs=1048576 count=$(EXT2_SIZE_MB)
	@if ! command -v $(EXT2_MKFS) >/dev/null 2>&1; then \
		echo "Missing ext2 image builder: $(EXT2_MKFS)"; \
		echo "Set EXT2_MKFS=/path/to/tool or install one of: mke2fs, mkfs.ext2, genext2fs, e2fsdroid"; \
		rm -f $@.tmp; \
		exit 1; \
	fi
	@if [ "$(EXT2_MKFS)" = "genext2fs" ]; then \
		$(EXT2_MKFS) -b $$(( $(EXT2_SIZE_MB) * 1024 )) -B $(EXT2_IMG_BLOCK_SIZE) -N $(EXT2_INODE_COUNT) -L $(EXT2_LABEL) -d $(ROOTFS_DIR) $@.tmp; \
	else \
		$(EXT2_MKFS) -q -F -t ext2 -b $(EXT2_IMG_BLOCK_SIZE) -I $(EXT2_INODE_SIZE) -N $(EXT2_INODE_COUNT) -L $(EXT2_LABEL) -d $(ROOTFS_DIR) $@.tmp; \
	fi
	mv $@.tmp $@

$(BUILD_ROOT)/boot_config/%.img: $(shell find config -type f -print)
	mkdir -p `dirname $@`
	rm -f $@.tmp
	dd if=/dev/zero of=$@.tmp bs=1048576 count=$(BOOT_CONFIG_EXT2_SIZE_MB)
	@if ! command -v $(EXT2_MKFS) >/dev/null 2>&1; then \
		echo "Missing ext2 image builder: $(EXT2_MKFS)"; \
		echo "Set EXT2_MKFS=/path/to/tool or install one of: mke2fs, mkfs.ext2, genext2fs, e2fsdroid"; \
		rm -f $@.tmp; \
		exit 1; \
	fi
	@if [ "$(EXT2_MKFS)" = "genext2fs" ]; then \
		$(EXT2_MKFS) -b $$(( $(BOOT_CONFIG_EXT2_SIZE_MB) * 1024 )) -B $(EXT2_IMG_BLOCK_SIZE) -L $(BOOT_CONFIG_EXT2_LABEL) -d config/$* $@.tmp; \
	else \
		$(EXT2_MKFS) -q -F -t ext2 -b $(EXT2_IMG_BLOCK_SIZE) -I $(EXT2_INODE_SIZE) -L $(BOOT_CONFIG_EXT2_LABEL) -d config/$* $@.tmp; \
	fi
	mv $@.tmp $@

disk: $(PYTHON_ENV_STAMP) tools/populate_fat32_image.py $(DISK_TEST_FILE) $(DISK_LONG_FILE) $(DISK_LARGE_FILE)
	mkdir -p `dirname $(DISK_IMG)`
	dd if=/dev/zero of=$(DISK_IMG) bs=1048576 count=$(DISK_SIZE_MB)
	$(MKFS_FAT) -F 32 -S 512 -s 1 -R 32 -n $(DISK_LABEL) $(DISK_IMG)
	$(PYTHON) tools/populate_fat32_image.py $(DISK_IMG) $(DISK_TEST_NAME) $(DISK_TEST_FILE)
	$(PYTHON) tools/populate_fat32_image.py $(DISK_IMG) "$(DISK_LONG_NAME)" $(DISK_LONG_FILE)
	$(PYTHON) tools/populate_fat32_image.py $(DISK_IMG) $(DISK_LARGE_NAME) $(DISK_LARGE_FILE)

sd-fat32-disk: $(SD_CARD_FAT32_IMG)

ext2-disk: $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG)

$(KERNEL_BUILD_DIR)/%.o: %.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_BUILD_DIR)/%.o: %.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL_BUILD_DIR)/%.o: %.S
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL_BUILD_DIR)/./kernel/artwork_assets.o $(KERNEL_BUILD_DIR)/kernel/artwork_assets.o: kernel/artwork_assets.S assets/logo.ascii assets/panic.ascii

$(KERNEL_ARTWORK_BUILD_DIR)/%.rgb: assets/%.png
	mkdir -p `dirname $@`
	$(MAGICK) $< -resize 768x512 -background black -gravity center -extent 768x512 -alpha remove -alpha off -depth 8 rgb:$@

$(KERNEL_LOGO_RGB_OBJECT): $(KERNEL_LOGO_RGB)
	$(OBJCOPY) -I binary -O $(OBJCOPY_BINARY_FORMAT) -B $(OBJCOPY_BINARY_ARCH) \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_$(KERNEL_LOGO_RGB_BINARY_SYMBOL)_start=__hunde_logo_rgb_start \
		--redefine-sym _binary_$(KERNEL_LOGO_RGB_BINARY_SYMBOL)_end=__hunde_logo_rgb_end \
		--redefine-sym _binary_$(KERNEL_LOGO_RGB_BINARY_SYMBOL)_size=__hunde_logo_rgb_size $< $@

$(KERNEL_PANIC_RGB_OBJECT): $(KERNEL_PANIC_RGB)
	$(OBJCOPY) -I binary -O $(OBJCOPY_BINARY_FORMAT) -B $(OBJCOPY_BINARY_ARCH) \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_$(KERNEL_PANIC_RGB_BINARY_SYMBOL)_start=__hunde_panic_rgb_start \
		--redefine-sym _binary_$(KERNEL_PANIC_RGB_BINARY_SYMBOL)_end=__hunde_panic_rgb_end \
		--redefine-sym _binary_$(KERNEL_PANIC_RGB_BINARY_SYMBOL)_size=__hunde_panic_rgb_size $< $@

$(RISCV64_BRINGUP_BUILD_DIR)/%.o: %.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

$(RISCV64_BRINGUP_BUILD_DIR)/%.o: %.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

$(MILKV_DUO_BRINGUP_BUILD_DIR)/%.o: %.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) $< -o $@

$(MILKV_DUO_BRINGUP_BUILD_DIR)/%.o: %.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

$(EMBEDDED_FS): $(PYTHON_ENV_STAMP) $(USER_BINARIES) $(RUST_SOCKET_BINARY) $(RUST_PIPE_BINARY) $(RUST_MEMFS_BINARY) $(RUST_PROCFS_BINARY) $(RUST_SYSINFOFS_BINARY) $(RUST_EXT2_BINARY) $(RUST_FAT32_BINARY) tools/build_embedded_fs.py
	$(PYTHON) tools/build_embedded_fs.py $@ $(EMBEDDED_FS_INPUTS)

$(KERNEL_BUILD_DIR)/./kernel/bin/embedded_fs.o: kernel/bin/embedded_fs.S $(EMBEDDED_FS)
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) -DEMBEDDED_FS_PATH=\"$(abspath $(EMBEDDED_FS))\" $< -o $@

$(USER_BUILD_DIR)/%.o: user/%.c $(NEWLIB_INSTALL_PREREQUISITE)
	mkdir -p `dirname $@`
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/%.o: user/%.s
	mkdir -p `dirname $@`
	$(AS) $(USER_ASFLAGS) $< -o $@

$(USER_BUILD_DIR)/common/%.o: common/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_CFLAGS) -c $< -o $@

ifeq ($(ARCH),riscv64)
$(COMPILER_RUNTIME_PIC_OBJECT): user/lib/arch/riscv64/compiler_runtime.c
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -c $< -o $@
endif

$(USER_PIC_BUILD_DIR)/%.o: user/%.c $(NEWLIB_PIC_INSTALL_PREREQUISITE)
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -idirafter $(NEWLIB_PIC_INCLUDE_DIR) -c $< -o $@

$(USER_PIC_BUILD_DIR)/%.o: user/%.s
	mkdir -p `dirname $@`
	$(AS) $(USER_PIC_ASFLAGS) $< -o $@

$(USER_PIC_BUILD_DIR)/common/%.o: common/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -c $< -o $@

$(OPENBSD_CAT_OBJECT): $(OPENBSD_CAT_SOURCE)
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -c $< -o $@

$(USER_PIC_BUILD_DIR)/vendor/openbsd/bin/rm/%.o: vendor/openbsd/src/bin/rm/%.c user/ports/openbsd/rm/unsupported_random.h
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -include user/ports/openbsd/rm/unsupported_random.h -c $< -o $@

$(USER_PIC_BUILD_DIR)/vendor/openbsd/bin/ls/%.o: vendor/openbsd/src/bin/ls/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -DSMALL -c $< -o $@

$(USER_PIC_BUILD_DIR)/vendor/openbsd/usr.bin/grep/%.o: vendor/openbsd/src/usr.bin/grep/%.c user/ports/openbsd/grep/namespace_compat.h
	mkdir -p `dirname $@`
	$(CC) -I./vendor/openbsd/src/sys $(USER_PIC_CFLAGS) -DSMALL -DNOZ \
		-include user/ports/openbsd/grep/namespace_compat.h -c $< -o $@

$(USER_PIC_BUILD_DIR)/vendor/openbsd/lib/libc/regex/%.o: vendor/openbsd/src/lib/libc/regex/%.c user/ports/openbsd/grep/namespace_compat.h
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -include user/ports/openbsd/grep/namespace_compat.h -c $< -o $@

$(USER_PIC_BUILD_DIR)/vendor/openbsd/%.o: vendor/openbsd/src/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -c $< -o $@

$(OPENBSD_RM_OBJECT): $(OPENBSD_RM_SOURCE) user/ports/openbsd/rm/unsupported_random.h
	mkdir -p `dirname $@`
	$(CC) $(USER_PIC_CFLAGS) -include user/ports/openbsd/rm/unsupported_random.h -c $< -o $@

$(USER_BUILD_DIR)/vendor/openbsd/bin/ls/%.o: vendor/openbsd/src/bin/ls/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_CFLAGS) -DSMALL -c $< -o $@

$(USER_BUILD_DIR)/vendor/openbsd/usr.bin/grep/%.o: vendor/openbsd/src/usr.bin/grep/%.c user/ports/openbsd/grep/namespace_compat.h
	mkdir -p `dirname $@`
	$(CC) -I./vendor/openbsd/src/sys $(USER_CFLAGS) -DSMALL -DNOZ \
		-include user/ports/openbsd/grep/namespace_compat.h -c $< -o $@

$(USER_BUILD_DIR)/vendor/openbsd/lib/libc/regex/%.o: vendor/openbsd/src/lib/libc/regex/%.c user/ports/openbsd/grep/namespace_compat.h
	mkdir -p `dirname $@`
	$(CC) $(USER_CFLAGS) -include user/ports/openbsd/grep/namespace_compat.h -c $< -o $@

$(USER_BUILD_DIR)/vendor/openbsd/%.o: vendor/openbsd/src/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_VIDEO_TERM_FONT_OBJECT): $(USER_VIDEO_TERM_FONT)
	mkdir -p `dirname $@`
	$(OBJCOPY) -I binary -O $(OBJCOPY_BINARY_FORMAT) -B $(OBJCOPY_BINARY_ARCH) $< $@

$(USER_BUILD_DIR)/drivers/video_term/main.o: $(USER_VIDEO_TERM_EMOJI_HEADER)

$(USER_VIDEO_TERM_EMOJI_ATLAS): $(PYTHON_ENV_STAMP) tools/build_video_term_emoji_atlas.py $(USER_VIDEO_TERM_EMOJI_PNGS)
	$(PYTHON) tools/build_video_term_emoji_atlas.py $@

$(USER_VIDEO_TERM_EMOJI_ATLAS_OBJECT): $(USER_VIDEO_TERM_EMOJI_ATLAS)
	mkdir -p `dirname $@`
	$(OBJCOPY) -I binary -O $(OBJCOPY_BINARY_FORMAT) -B $(OBJCOPY_BINARY_ARCH) \
		--redefine-sym _binary_$(USER_VIDEO_TERM_EMOJI_ATLAS_BINARY_SYMBOL)_start=_binary_build_user_drivers_video_term_emoji_atlas_rgba_start \
		--redefine-sym _binary_$(USER_VIDEO_TERM_EMOJI_ATLAS_BINARY_SYMBOL)_end=_binary_build_user_drivers_video_term_emoji_atlas_rgba_end \
		--redefine-sym _binary_$(USER_VIDEO_TERM_EMOJI_ATLAS_BINARY_SYMBOL)_size=_binary_build_user_drivers_video_term_emoji_atlas_rgba_size \
		$< $@

$(KERNEL_BUILD_DIR)/./common/types/dyn_string.o: common/types/dyn_string.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -DDYN_STRING_USE_KERNEL_ALLOC -c $< -o $@

# Create binary image from ELF
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(EFI): $(ELF)
	$(OBJCOPY) -O pei-aarch64-little --subsystem=efi-app --image-base=$(QEMU_PI_KERNEL_ADDR) $< $@

$(PI_UEFI_LOADER_ELF): arch/aarch64/boot/pi_uefi/loader.c
	mkdir -p `dirname $@`
	$(CC) -ffreestanding -fpic -fshort-wchar -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -nostdlib -nostartfiles -march=armv8-a -mno-outline-atomics -Wl,-e,efi_main -Wl,-Ttext=0x1000 $< -o $@

$(PI_UEFI_EFI): $(PI_UEFI_LOADER_ELF)
	$(OBJCOPY) -O pei-aarch64-little --subsystem=efi-app -R .comment $< $@

$(PI_UEFI_ESP_IMG): $(PYTHON_ENV_STAMP) $(PI_UEFI_EFI) $(PI_UEFI_KERNEL_ELF) tools/uefi_startup.nsh
	mkdir -p `dirname $@`
	dd if=/dev/zero of=$@ bs=1048576 count=$(EFI_ESP_SIZE_MB)
	$(MKFS_FAT) -F 32 -S 512 -s 1 -R 32 -n EFIBOOT $@
	$(PYTHON) tools/populate_fat32_image.py $@ EFI/BOOT/BOOTAA64.EFI $(PI_UEFI_EFI)
	$(PYTHON) tools/populate_fat32_image.py $@ KERNEL.ELF $(PI_UEFI_KERNEL_ELF)
	$(PYTHON) tools/populate_fat32_image.py $@ STARTUP.NSH tools/uefi_startup.nsh

$(QEMU_EFI_VARS): $(QEMU_EFI_VARS_TEMPLATE)
	mkdir -p `dirname $@`
	cp $(QEMU_EFI_VARS_TEMPLATE) $@

# Link object files to create ELF
$(ELF): $(ASM_OBJECTS) $(CPP_ASM_OBJECTS) $(OBJECTS) $(COMMON_KERNEL_OBJECTS) $(ARCH_ASM_OBJECTS) $(ARCH_CPP_ASM_OBJECTS) $(ARCH_OBJECTS) $(KERNEL_ARTWORK_OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

$(RISCV64_BRINGUP_ELF): $(RISCV64_BRINGUP_OBJECTS) arch/riscv64/boot/qemu_virt/linker.ld
	$(LD) -nostdlib -Wl,--build-id=none -Wl,--no-relax -T arch/riscv64/boot/qemu_virt/linker.ld \
		$(RISCV64_BRINGUP_OBJECTS) -o $@

riscv64-bringup:
	$(MAKE) ARCH=riscv64 riscv64-bringup-elf

riscv64-bringup-elf: $(RISCV64_BRINGUP_ELF)

$(MILKV_DUO_BRINGUP_ELF): $(MILKV_DUO_BRINGUP_OBJECTS) arch/riscv64/boot/milkv_duo/bringup_linker.ld
	$(LD) -nostdlib -Wl,--build-id=none -Wl,--no-relax -T arch/riscv64/boot/milkv_duo/bringup_linker.ld \
		$(MILKV_DUO_BRINGUP_OBJECTS) -o $@

$(MILKV_DUO_BRINGUP_BIN): $(MILKV_DUO_BRINGUP_ELF)
	$(OBJCOPY) -O binary $< $@

$(MILKV_DUO_BRINGUP_LZMA): $(MILKV_DUO_BRINGUP_BIN)
	$(LZMA) --format=lzma --check=none --lzma1=preset=6 -c $< > $@

$(MILKV_DUO_DTB): arch/riscv64/boot/milkv_duo/milkv_duo.dts
	mkdir -p `dirname $@`
	$(DTC) -I dts -O dtb -o $@ $<

$(MILKV_DUO_BRINGUP_BOOT_IMAGE): $(MILKV_DUO_BRINGUP_LZMA) $(MILKV_DUO_DTB) arch/riscv64/boot/milkv_duo/bringup.its
	cd $(MILKV_DUO_BUILD_DIR) && $(DTC) -I dts -O dtb -i $(abspath $(MILKV_DUO_BUILD_DIR)) \
		-o $(abspath $@) $(abspath arch/riscv64/boot/milkv_duo/bringup.its)
	test `wc -c < $@` -le 15728640

$(MILKV_DUO_KERNEL_LZMA): $(BIN)
	$(LZMA) --format=lzma --check=none --lzma1=preset=6 -c $< > $@

$(MILKV_DUO_BOOT_IMAGE): $(MILKV_DUO_KERNEL_LZMA) $(MILKV_DUO_DTB) arch/riscv64/boot/milkv_duo/boot.its
	cd $(MILKV_DUO_BUILD_DIR) && $(DTC) -I dts -O dtb -i $(abspath $(MILKV_DUO_BUILD_DIR)) \
		-o $(abspath $@) $(abspath arch/riscv64/boot/milkv_duo/boot.its)
	test `wc -c < $@` -le 15728640

$(MILKV_DUO_FIRMWARE_ARCHIVE): $(PYTHON_ENV_STAMP) tools/download_verified.py
	$(PYTHON) tools/download_verified.py "$(MILKV_DUO_FIRMWARE_URL)" $(MILKV_DUO_FIRMWARE_SHA256) $@

$(MILKV_DUO_FIRMWARE_TEMPLATE): $(MILKV_DUO_FIRMWARE_ARCHIVE)
	mkdir -p `dirname $@`
	unzip -p $< '*.img' | dd of=$@.tmp bs=512 count=$$(( $(MILKV_DUO_BOOT_PARTITION_SECTORS) + 1 ))
	test `wc -c < $@.tmp` -eq $$(( ($(MILKV_DUO_BOOT_PARTITION_SECTORS) + 1) * 512 ))
	mv $@.tmp $@

$(MILKV_DUO_BOOT_PARTITION): $(MILKV_DUO_FIRMWARE_TEMPLATE) $(MILKV_DUO_BOOT_IMAGE) tools/populate_fat32_image.py
	dd if=$< of=$@.tmp bs=512 skip=1 count=$(MILKV_DUO_BOOT_PARTITION_SECTORS)
	$(PYTHON) tools/populate_fat32_image.py $@.tmp BOOT.SD $(MILKV_DUO_BOOT_IMAGE)
	mv $@.tmp $@

$(MILKV_DUO_SD_IMAGE): $(MILKV_DUO_FIRMWARE_TEMPLATE) $(MILKV_DUO_BOOT_PARTITION) $(EXT2_IMG) tools/build_milkv_duo_sd_image.py
	$(PYTHON) tools/build_milkv_duo_sd_image.py $< $(MILKV_DUO_BOOT_PARTITION) $(EXT2_IMG) $@

milkv-duo:
	$(MAKE) ARCH=riscv64 BSP_DEFINES=-DBSP_MILKV_DUO ARCH_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		BOOT_PLATFORM=milkv_duo \
		PLATFORM_BUILD_ROOT=build/riscv64/milkv_duo milkv-duo-build

milkv-duo-sd-image:
	$(MAKE) ARCH=riscv64 BSP_DEFINES=-DBSP_MILKV_DUO ARCH_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		BOOT_PLATFORM=milkv_duo \
		PLATFORM_BUILD_ROOT=build/riscv64/milkv_duo milkv-duo-sd-image-build

milkv-duo-bringup:
	$(MAKE) ARCH=riscv64 BSP_DEFINES=-DBSP_MILKV_DUO ARCH_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		BOOT_PLATFORM=milkv_duo \
		PLATFORM_BUILD_ROOT=build/riscv64/milkv_duo milkv-duo-bringup-build

milkv-duo-build: $(MILKV_DUO_BOOT_IMAGE)

milkv-duo-sd-image-build: $(MILKV_DUO_SD_IMAGE)

milkv-duo-qemu-build:
	$(MAKE) ARCH=riscv64 BSP_DEFINES="-DBSP_MILKV_DUO -DRISCV_MILKV_DUO_QEMU" ARCH_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		BOOT_PLATFORM=milkv_duo BUILD_ROOT=$(MILKV_DUO_QEMU_PLATFORM_BUILD_ROOT) \
		PLATFORM_BUILD_ROOT=$(MILKV_DUO_QEMU_PLATFORM_BUILD_ROOT) \
		$(MILKV_DUO_QEMU_KERNEL) $(MILKV_DUO_QEMU_EXT2_IMG) $(MILKV_DUO_QEMU_DTB)

qemu-milkv-duo: milkv-duo-qemu-build
	$(QEMU_MILKV_DUO) -M milkv-duo -smp 1 -m 64M -nographic -no-reboot \
		-bios $(QEMU_MILKV_DUO_BIOS) -dtb $(MILKV_DUO_QEMU_DTB) -kernel $(MILKV_DUO_QEMU_KERNEL) \
		-drive if=none,file=$(MILKV_DUO_QEMU_EXT2_IMG),format=raw,id=sd-card \
		-device sd-card,drive=sd-card

milkv-duo-bringup-build: $(MILKV_DUO_BRINGUP_BOOT_IMAGE)

milkv-duo-bringup-elf:
	$(MAKE) ARCH=riscv64 BSP_DEFINES=-DBSP_MILKV_DUO ARCH_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_ARCH_FLAGS)" \
		BOOT_PLATFORM=milkv_duo \
		PLATFORM_BUILD_ROOT=build/riscv64/milkv_duo milkv-duo-bringup-elf-build

milkv-duo-bringup-elf-build: $(MILKV_DUO_BRINGUP_ELF)

ifeq ($(ARCH),riscv64)
$(MILKV_DUO_S_DTB): $(MILKV_DUO_S_DTS)
	mkdir -p `dirname $@`
	$(DTC) -I dts -O dtb -o $@ $<

$(MILKV_DUO_S_KERNEL_LZMA): $(BIN)
	$(LZMA) --format=lzma --check=none --lzma1=preset=6 -c $< > $@

$(MILKV_DUO_S_HANDOFF_OBJECT): arch/riscv64/boot/milkv_duo_s/handoff.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

$(MILKV_DUO_S_HANDOFF_ELF): $(MILKV_DUO_S_HANDOFF_OBJECT) $(BIN) arch/riscv64/boot/milkv_duo_s/handoff_linker.ld
	$(RAW_LD) --build-id=none --no-relax -T arch/riscv64/boot/milkv_duo_s/handoff_linker.ld \
		--defsym HUNDEOS_KERNEL_SIZE=`wc -c < $(BIN) | tr -d ' '` $(MILKV_DUO_S_HANDOFF_OBJECT) -o $@

$(MILKV_DUO_S_HANDOFF_BIN): $(MILKV_DUO_S_HANDOFF_ELF)
	$(OBJCOPY) -O binary $< $@
	test `wc -c < $@` -lt 8388608

$(MILKV_DUO_S_BOOT_IMAGE): $(MILKV_DUO_S_HANDOFF_BIN) $(MILKV_DUO_S_KERNEL_LZMA) $(MILKV_DUO_S_DTB) arch/riscv64/boot/milkv_duo_s/boot.its
	test `wc -c < $(BIN)` -lt $$(( 0x84000000 - 0x80200000 ))
	test `wc -c < $(BIN)` -le $$(( `wc -c < $(MILKV_DUO_S_KERNEL_LZMA)` * 20 ))
	test $$(( 0x84000000 + `wc -c < $(MILKV_DUO_S_KERNEL_LZMA)` * 20 )) -lt $$(( 0x8c000000 ))
	cd $(MILKV_DUO_S_BUILD_DIR) && $(DTC) -I dts -O dtb -i $(abspath $(MILKV_DUO_S_BUILD_DIR)) \
		-o $(abspath $@) $(abspath arch/riscv64/boot/milkv_duo_s/boot.its)
	test `wc -c < $@` -le 15728640

$(MILKV_DUO_S_FIRMWARE_ARCHIVE): $(PYTHON_ENV_STAMP) tools/download_verified.py
	$(PYTHON) tools/download_verified.py "$(MILKV_DUO_S_FIRMWARE_URL)" $(MILKV_DUO_S_FIRMWARE_SHA256) $@

$(MILKV_DUO_S_FIRMWARE_TEMPLATE): $(MILKV_DUO_S_FIRMWARE_ARCHIVE)
	mkdir -p `dirname $@`
	unzip -p $< '*.img' | dd of=$@.tmp bs=512 count=$(MILKV_DUO_S_ROOT_START_SECTOR)
	test `wc -c < $@.tmp` -eq $$(( $(MILKV_DUO_S_ROOT_START_SECTOR) * 512 ))
	mv $@.tmp $@

$(MILKV_DUO_S_BOOT_PARTITION): $(MILKV_DUO_S_FIRMWARE_TEMPLATE) $(MILKV_DUO_S_BOOT_IMAGE) tools/populate_fat32_image.py
	dd if=$< of=$@.tmp bs=512 skip=1 count=$(MILKV_DUO_S_BOOT_PARTITION_SECTORS)
	$(PYTHON) tools/populate_fat32_image.py $@.tmp BOOT.SD $(MILKV_DUO_S_BOOT_IMAGE)
	mv $@.tmp $@

$(MILKV_DUO_S_SD_IMAGE): $(MILKV_DUO_S_FIRMWARE_TEMPLATE) $(MILKV_DUO_S_BOOT_PARTITION) $(BOOT_CONFIG_EXT2_IMG) $(EXT2_IMG) tools/build_milkv_duo_sd_image.py
	$(PYTHON) tools/build_milkv_duo_sd_image.py $< $(MILKV_DUO_S_BOOT_PARTITION) $(EXT2_IMG) $@ \
		--boot-config $(BOOT_CONFIG_EXT2_IMG)
endif

milkv-duo-s-riscv:
	$(MAKE) ARCH=riscv64 BSP_DEFINES=-DBSP_MILKV_DUO_S ARCH_CFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" \
		BOOT_PLATFORM=milkv_duo_s \
		PLATFORM_BUILD_ROOT=build/riscv64/milkv_duo_s milkv-duo-s-build-riscv

milkv-duo-s-sd-image-riscv:
	$(MAKE) ARCH=riscv64 BSP_DEFINES=-DBSP_MILKV_DUO_S ARCH_CFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" \
		BOOT_PLATFORM=milkv_duo_s BOOT_CONFIG=milkv_duo_s \
		PLATFORM_BUILD_ROOT=build/riscv64/milkv_duo_s milkv-duo-s-sd-image-build-riscv

milkv-duo-s-build-riscv: $(MILKV_DUO_S_BOOT_IMAGE)

milkv-duo-s-sd-image-build-riscv: $(MILKV_DUO_S_SD_IMAGE)

milkv-duo-s-qemu-build-riscv:
	$(MAKE) ARCH=riscv64 BSP_DEFINES="-DBSP_MILKV_DUO_S -DRISCV_MILKV_DUO_S_QEMU" ARCH_CFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" \
		ARCH_USER_CFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" ARCH_ASFLAGS="$(MILKV_DUO_S_ARCH_FLAGS)" \
		BOOT_PLATFORM=milkv_duo_s BUILD_ROOT=$(MILKV_DUO_S_QEMU_PLATFORM_BUILD_ROOT) \
		PLATFORM_BUILD_ROOT=$(MILKV_DUO_S_QEMU_PLATFORM_BUILD_ROOT) \
		MILKV_DUO_S_DTS=arch/riscv64/boot/milkv_duo_s/milkv_duo_s_qemu.dts \
		$(MILKV_DUO_S_QEMU_KERNEL) $(MILKV_DUO_S_QEMU_DTB)

milkv-duo-s-qemu-disk-riscv: milkv-duo-s-sd-image-riscv
	mkdir -p `dirname $(MILKV_DUO_S_QEMU_SD_IMAGE)`
	cp $(MILKV_DUO_S_HARDWARE_SD_IMAGE) $(MILKV_DUO_S_QEMU_SD_IMAGE)
	truncate -s 256M $(MILKV_DUO_S_QEMU_SD_IMAGE)

qemu-milkv-duo-s-riscv: milkv-duo-s-qemu-build-riscv milkv-duo-s-qemu-disk-riscv
	$(QEMU_MILKV_DUO) -M milkv-duo -smp 1 -m 510M -nographic -no-reboot \
		-bios $(QEMU_MILKV_DUO_BIOS) -dtb $(MILKV_DUO_S_QEMU_DTB) -kernel $(MILKV_DUO_S_QEMU_KERNEL) \
		-drive if=none,file=$(MILKV_DUO_S_QEMU_SD_IMAGE),format=raw,id=sd-card \
		-device sd-card,drive=sd-card

ifeq ($(ARCH),aarch64)
$(MILKV_DUO_S_AARCH64_DTB): arch/aarch64/boot/milkv_duo_s/milkv_duo_s.dts
	mkdir -p `dirname $@`
	$(DTC) -I dts -O dtb -o $@ $<

$(MILKV_DUO_S_AARCH64_KERNEL_LZMA): $(BIN)
	$(LZMA) --format=lzma --check=none --lzma1=preset=6 -c $< > $@

$(MILKV_DUO_S_AARCH64_HANDOFF_OBJECT): arch/aarch64/boot/milkv_duo_s/handoff.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

$(MILKV_DUO_S_AARCH64_HANDOFF_ELF): $(MILKV_DUO_S_AARCH64_HANDOFF_OBJECT) $(ELF) $(BIN) arch/aarch64/boot/milkv_duo_s/handoff_linker.ld
	kernel_entry=`$(NM) -n $(ELF) | awk '$$3 == "_start" { print "0x" $$1; exit }'`; \
		test -n "$$kernel_entry"; \
		$(RAW_LD) --build-id=none -T arch/aarch64/boot/milkv_duo_s/handoff_linker.ld \
			--defsym HUNDEOS_KERNEL_ENTRY=$$kernel_entry \
			--defsym HUNDEOS_KERNEL_SIZE=`wc -c < $(BIN) | tr -d ' '` $(MILKV_DUO_S_AARCH64_HANDOFF_OBJECT) -o $@

$(MILKV_DUO_S_AARCH64_HANDOFF_BIN): $(MILKV_DUO_S_AARCH64_HANDOFF_ELF)
	$(OBJCOPY) -O binary $< $@
	test `wc -c < $@` -lt 8388608

$(MILKV_DUO_S_AARCH64_BOOT_IMAGE): $(MILKV_DUO_S_AARCH64_HANDOFF_BIN) $(MILKV_DUO_S_AARCH64_KERNEL_LZMA) $(MILKV_DUO_S_AARCH64_DTB) arch/aarch64/boot/milkv_duo_s/boot.its
	test `wc -c < $(BIN)` -lt $$(( 0x84000000 - 0x80108000 ))
	test `wc -c < $(BIN)` -le $$(( `wc -c < $(MILKV_DUO_S_AARCH64_KERNEL_LZMA)` * 20 ))
	test $$(( 0x84000000 + `wc -c < $(MILKV_DUO_S_AARCH64_KERNEL_LZMA)` * 20 )) -lt $$(( 0x8c000000 ))
	cd $(MILKV_DUO_S_AARCH64_BUILD_DIR) && $(DTC) -I dts -O dtb -i $(abspath $(MILKV_DUO_S_AARCH64_BUILD_DIR)) \
		-o $(abspath $@) $(abspath arch/aarch64/boot/milkv_duo_s/boot.its)
	test `wc -c < $@` -le 15728640

$(MILKV_DUO_S_AARCH64_FIRMWARE_ARCHIVE): $(PYTHON_ENV_STAMP) tools/download_verified.py
	$(PYTHON) tools/download_verified.py "$(MILKV_DUO_S_AARCH64_FIRMWARE_URL)" $(MILKV_DUO_S_AARCH64_FIRMWARE_SHA256) $@

$(MILKV_DUO_S_AARCH64_FIRMWARE_TEMPLATE): $(MILKV_DUO_S_AARCH64_FIRMWARE_ARCHIVE)
	mkdir -p `dirname $@`
	unzip -p $< '*.img' | dd of=$@.tmp bs=512 count=$(MILKV_DUO_S_ROOT_START_SECTOR)
	test `wc -c < $@.tmp` -eq $$(( $(MILKV_DUO_S_ROOT_START_SECTOR) * 512 ))
	mv $@.tmp $@

$(MILKV_DUO_S_AARCH64_BOOT_PARTITION): $(MILKV_DUO_S_AARCH64_FIRMWARE_TEMPLATE) $(MILKV_DUO_S_AARCH64_BOOT_IMAGE) tools/populate_fat32_image.py
	dd if=$< of=$@.tmp bs=512 skip=1 count=$(MILKV_DUO_S_BOOT_PARTITION_SECTORS)
	$(PYTHON) tools/populate_fat32_image.py $@.tmp BOOT.SD $(MILKV_DUO_S_AARCH64_BOOT_IMAGE)
	mv $@.tmp $@

$(MILKV_DUO_S_AARCH64_SD_IMAGE): $(MILKV_DUO_S_AARCH64_FIRMWARE_TEMPLATE) $(MILKV_DUO_S_AARCH64_BOOT_PARTITION) $(BOOT_CONFIG_EXT2_IMG) $(EXT2_IMG) tools/build_milkv_duo_sd_image.py
	$(PYTHON) tools/build_milkv_duo_sd_image.py $< $(MILKV_DUO_S_AARCH64_BOOT_PARTITION) $(EXT2_IMG) $@ \
		--boot-config $(BOOT_CONFIG_EXT2_IMG)
endif

milkv-duo-s-aarch64:
	$(MAKE) ARCH=aarch64 BSP_DEFINES="-DBSP_MILKV_DUO_S -DKERNEL_SERIAL_TRACE -DKERNEL_MEMORY_TRACE -DAARCH64_BOOT_MAX_CPUS=1" BOOT_PLATFORM=milkv_duo_s \
		PLATFORM_BUILD_ROOT=build/aarch64/milkv_duo_s milkv-duo-s-build-aarch64

milkv-duo-s-sd-image-aarch64:
	$(MAKE) ARCH=aarch64 BSP_DEFINES="-DBSP_MILKV_DUO_S -DKERNEL_SERIAL_TRACE -DKERNEL_MEMORY_TRACE -DAARCH64_BOOT_MAX_CPUS=1" BOOT_PLATFORM=milkv_duo_s BOOT_CONFIG=milkv_duo_s \
		PLATFORM_BUILD_ROOT=build/aarch64/milkv_duo_s milkv-duo-s-sd-image-build-aarch64

milkv-duo-s-sd-image-aarch64-command-trace:
	$(MAKE) ARCH=aarch64 \
		BSP_DEFINES="-DBSP_MILKV_DUO_S -DKERNEL_SERIAL_TRACE -DKERNEL_MEMORY_TRACE -DKERNEL_COMMAND_TRACE -DAARCH64_BOOT_MAX_CPUS=1" \
		BOOT_PLATFORM=milkv_duo_s BOOT_CONFIG=milkv_duo_s \
		MILKV_DUO_S_AARCH64_BUILD_DIR=build/aarch64/milkv_duo_s_command_trace \
		PLATFORM_BUILD_ROOT=build/aarch64/milkv_duo_s_command_trace milkv-duo-s-sd-image-build-aarch64

milkv-duo-s-build-aarch64: $(MILKV_DUO_S_AARCH64_BOOT_IMAGE)

milkv-duo-s-sd-image-build-aarch64: $(MILKV_DUO_S_AARCH64_SD_IMAGE)

qemu-riscv64:
	$(MAKE) ARCH=riscv64 BOOT_CONFIG=qemu_riscv64 qemu-riscv64-run

qemu-riscv64-run: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	qemu-system-riscv64 -machine virt -cpu rv64,zawrs=true -smp 4 -m 1G -bios default \
		-kernel $(ELF) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(SD_CARD_FAT32_IMG),format=raw,id=sd0 \
		-device virtio-blk-device,drive=sd0 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-nographic -monitor none -no-reboot

qemu-riscv64-trace:
	$(MAKE) ARCH=riscv64 BOOT_CONFIG=qemu_riscv64 qemu-riscv64-trace-build

qemu-riscv64-trace-build:
	$(MAKE) KERNEL_BUILD_DIR="$(RISCV64_TRACE_KERNEL_BUILD_DIR)" \
		ELF="$(RISCV64_TRACE_ELF)" \
		EXTRA_KERNEL_CFLAGS="-DKERNEL_SEMIHOSTING_TRACE" $(RISCV64_TRACE_ELF)
	$(MAKE) KERNEL_BUILD_DIR="$(RISCV64_TRACE_KERNEL_BUILD_DIR)" \
		ELF="$(RISCV64_TRACE_ELF)" qemu-riscv64-trace-run

qemu-riscv64-trace-run: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	mkdir -p `dirname $(KERNEL_TRACE_LOG)`
	: > $(KERNEL_TRACE_LOG)
	qemu-system-riscv64 -machine virt -cpu rv64,zawrs=true -smp 4 -m 1G -bios default \
		-kernel $(ELF) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(SD_CARD_FAT32_IMG),format=raw,id=sd0 \
		-device virtio-blk-device,drive=sd0 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-display none -monitor none -no-reboot \
		-chardev stdio,id=console,signal=off -serial chardev:console \
		-semihosting-config enable=on,target=native

riscv-virt-embedded:
	$(MAKE) ARCH=riscv64 BOOT_PLATFORM=riscv_virt_embedded BOOT_CONFIG=qemu_riscv64 riscv-virt-embedded-run

riscv-virt-embedded-run: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	qemu-system-riscv64 -machine virt -cpu rv64,zawrs=true -smp 1 -m 510M -bios default \
		-kernel $(ELF) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(SD_CARD_FAT32_IMG),format=raw,id=sd0 \
		-device virtio-blk-device,drive=sd0 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-nographic -monitor none -no-reboot

# Clean build artifacts
clean:
	rm -rf build

test: $(PYTHON_ENV_STAMP)
	$(MAKE) rust-test
	$(PYTHON) -m pytest
	make test -C test

smoke-test: $(PYTHON_ENV_STAMP)
	$(PYTHON) -m pytest test/smoke

.PHONY: format format-check format-test
format: $(PYTHON_ENV_STAMP) rust-format
	$(PYTHON) tools/hundeformat.py -- $(CLANG_FORMAT)

format-check: $(PYTHON_ENV_STAMP) rust-format-check
	$(PYTHON) tools/hundeformat.py --check -- $(CLANG_FORMAT)

format-test: $(PYTHON_ENV_STAMP)
	$(PYTHON) -m pytest test/tools/test_hundeformat.py --clang-format "$(CLANG_FORMAT)"

hundelint: $(PYTHON_ENV_STAMP)
	$(PYTHON) tools/hundelint.py --require-tree-sitter
	$(MAKE) rust-lint

hundelint-strict: $(PYTHON_ENV_STAMP)
	$(PYTHON) tools/hundelint.py --require-tree-sitter --strict
	$(MAKE) rust-lint

hundelint-test: $(PYTHON_ENV_STAMP)
	$(PYTHON) -m pytest test/tools/test_hundelint.py

test_clean:
	make clean -C test

define make-user-bin
$(notdir $(1))_SRCS := $$(wildcard $(1)/*.c)
$(notdir $(1))_ASM_SRCS := $$(wildcard $(1)/*.s)
$(notdir $(1))_OBJS := $$(patsubst user/%.c,$$(USER_BUILD_DIR)/%.o,$$($(notdir $(1))_SRCS))
$(notdir $(1))_ASM_OBJS := $$(patsubst user/%.s,$$(USER_BUILD_DIR)/%.o,$$($(notdir $(1))_ASM_SRCS))

$$(USER_BUILD_DIR)/$(notdir $(1)).bin: $$($(notdir $(1))_OBJS) $$($(notdir $(1))_ASM_OBJS) $$($(notdir $(1))_EXTRA_OBJS) $$(SYSROOT_FILES)
	$$(LD) $$(USER_LDFLAGS) $$(SYSROOT_CRT) $$($(notdir $(1))_OBJS) $$($(notdir $(1))_ASM_OBJS) \
		$$($(notdir $(1))_EXTRA_OBJS) -L$$(SYSROOT_LIB_DIR) \
		-Wl,--start-group -lc -lhunde -lm -lgcc -Wl,--end-group -o $$@
endef

$(foreach dir,$(USER_STANDARD_BINARY_DIRS),$(eval $(call make-user-bin,$(dir))))

define register-wuff-command-objects
$(USER_BUILD_DIR)/$(1).bin: $$($(1)_WUFF_OBJECTS)
endef

$(foreach command,$(ROOTFS_COMMAND_NAMES),$(eval $(call register-wuff-command-objects,$(command))))

$(ROOTFS_COMMAND_BINARIES): $(USER_CRT_PIC_OBJECT) $(SYSROOT_WUFF_FILES) $(WUFF_PROGRAM_VERSION_SCRIPT) Makefile | check-wuff-linker
	mkdir -p `dirname $@`
	$(WUFF_LD) -pie --dynamic-linker /sbin/wuff-ld -e _start --hash-style=sysv -z now -z relro \
		--gc-sections $(WUFF_PROGRAM_LINK_FLAGS) $(filter %.o,$^) \
		-L$(SYSROOT_LIB_DIR) -l:libc.wuff.1 -o $@
	$(READELF) -h $@ | awk '/Type:/ { if ($$2 != "DYN") exit 1; found = 1 } END { exit !found }'
	$(READELF) -h $@ | awk '/Entry point address:/ { if ($$4 == "0x0") exit 1; found = 1 } END { exit !found }'
	$(READELF) -lW $@ | awk '/Requesting program interpreter:/ { if ($$0 !~ /\/sbin\/wuff-ld\]/) exit 1; found = 1 } END { exit !found }'
	$(READELF) -dW $@ | awk '/\(NEEDED\)/ { if ($$0 !~ /\[libc\.wuff\.1\]/ || found) exit 1; found = 1 } END { exit !found }'
	if $(READELF) -dW $@ | grep -q '(TEXTREL)'; then echo "$@ contains text relocations" >&2; exit 1; fi
	$(READELF) -lW $@ | grep -q 'GNU_RELRO'
	if $(READELF) -lW $@ | grep -Eq '^[[:space:]]*TLS[[:space:]]'; then echo "$@ contains TLS" >&2; exit 1; fi
	if $(READELF) -dW $@ | grep -Eq '\((PREINIT_ARRAY|INIT|INIT_ARRAY|FINI|FINI_ARRAY)\)'; then echo "$@ contains initialization metadata" >&2; exit 1; fi
	if $(READELF) --dyn-syms -W $@ | awk '$$8 == "_stack" { found = 1 } END { exit !found }'; then echo "$@ exports synthetic _stack" >&2; exit 1; fi
	$(READELF) -rW $@ | awk -v allowed="$(WUFF_PROGRAM_RELOCATIONS)" '$$3 ~ /^R_/ { found = 1; if (index(" " allowed " ", " " $$3 " ") == 0) { print; bad = 1 } } END { exit bad || !found }'

$(WUFF_INTERPRETER_BINARY): $(WUFF_INTERPRETER_OBJECTS) $(SYSROOT_FILES)
	$(LD) $(USER_LDFLAGS) $(WUFF_INTERPRETER_OBJECTS) -L$(SYSROOT_LIB_DIR) \
		-Wl,--start-group -lc -lhunde -lm -lgcc -Wl,--end-group -o $@
	@$(READELF) -h $@ | grep -Eq 'Type:[[:space:]]+EXEC' || { echo "$@ must be ET_EXEC" >&2; exit 1; }
	@! $(READELF) -l $@ | grep -q ' INTERP ' || { echo "$@ must not contain PT_INTERP" >&2; exit 1; }
	@! $(READELF) -d $@ 2>/dev/null | grep -q 'NEEDED' || { echo "$@ must not have dynamic dependencies" >&2; exit 1; }

-include $(shell find $(BUILD_ROOT) -type f -name "*.d" -print 2>/dev/null)

user_binaries: $(USER_BINARIES)

pi-uefi: BOOT_CONFIG = pi_uefi
pi-uefi:
	$(MAKE) BOOT_PLATFORM=pi_uefi BSP_DEFINES=-DBSP_RPI4 LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld all

pi-uefi-efi: BOOT_CONFIG = pi_uefi
pi-uefi-efi:
	$(MAKE) BOOT_PLATFORM=pi_uefi BSP_DEFINES=-DBSP_RPI4 LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld $(PI_UEFI_EFI)

pi-uefi-esp: BOOT_CONFIG = pi_uefi
pi-uefi-esp:
	$(MAKE) BOOT_PLATFORM=pi_uefi BSP_DEFINES=-DBSP_RPI4 LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld $(PI_UEFI_ESP_IMG)

qemu-pi-uefi: BOOT_CONFIG = qemu_pi_uefi
qemu-pi-uefi: $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG)
	$(MAKE) BOOT_PLATFORM=pi_uefi BSP_DEFINES=-DBSP_RPI4 LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld all
	qemu-system-aarch64 \
		-M $(QEMU_PI_MACHINE) \
		-m $(QEMU_PI_MEMORY) \
		-serial $(SERIAL) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-device loader,file=$(PI_UEFI_KERNEL_IMG),addr=$(QEMU_PI_KERNEL_ADDR),force-raw=on \
		-device loader,addr=$(QEMU_PI_KERNEL_ENTRY),cpu-num=0 \
		-nographic \
		-monitor telnet::4444,server,nowait

qemu-pi-uefi-firmware: BOOT_CONFIG = qemu_pi_uefi
qemu-pi-uefi-firmware:
	rm -rf $(PI_UEFI_BUILD_DIR)/kernel $(PI_UEFI_KERNEL_ELF) $(PI_UEFI_ESP_IMG)
	$(MAKE) BOOT_PLATFORM=pi_uefi UEFI_DEVICE_PLATFORM=qemu_virt BSP_DEFINES="-DBSP_RPI4 -DKERNEL_SEMIHOSTING_TRACE" LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld $(PI_UEFI_ESP_IMG) $(PI_UEFI_EFI_VARS) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG)
	@if [ -z "$(QEMU_EFI_CODE)" ]; then echo "Missing QEMU AArch64 EDK2 code firmware. Set QEMU_EFI_CODE=/path/to/code.fd"; exit 1; fi
	@if [ -z "$(QEMU_EFI_VARS_TEMPLATE)" ]; then echo "Missing QEMU EDK2 vars template. Set QEMU_EFI_VARS_TEMPLATE=/path/to/vars.fd"; exit 1; fi
	mkdir -p $(BUILD_ROOT)
	: > $(KERNEL_TRACE_LOG)
	qemu-system-aarch64 \
		-M virt,gic-version=3,virtualization=off,secure=off \
		-smp cpus=4 -cpu cortex-a57 \
		-m 4G \
		-serial $(SERIAL) \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_EFI_CODE) \
		-drive if=pflash,format=raw,file=$(PI_UEFI_EFI_VARS) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(PI_UEFI_ESP_IMG),format=raw,id=esp \
		-device virtio-blk-device,drive=esp \
		-nographic \
		-semihosting-config enable=on,target=native \
		-monitor telnet::4444,server,nowait

qemu-pi-uefi-firmware-trace: BOOT_CONFIG = qemu_pi_uefi_trace
qemu-pi-uefi-firmware-trace:
	rm -rf $(PI_UEFI_BUILD_DIR)/kernel $(PI_UEFI_KERNEL_ELF) $(PI_UEFI_ESP_IMG)
	$(MAKE) BOOT_PLATFORM=pi_uefi UEFI_DEVICE_PLATFORM=qemu_virt BSP_DEFINES="-DBSP_RPI4 -DKERNEL_SEMIHOSTING_TRACE" LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld $(PI_UEFI_ESP_IMG) $(PI_UEFI_EFI_VARS) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG)
	@if [ -z "$(QEMU_EFI_CODE)" ]; then echo "Missing QEMU AArch64 EDK2 code firmware. Set QEMU_EFI_CODE=/path/to/code.fd"; exit 1; fi
	@if [ -z "$(QEMU_EFI_VARS_TEMPLATE)" ]; then echo "Missing QEMU EDK2 vars template. Set QEMU_EFI_VARS_TEMPLATE=/path/to/vars.fd"; exit 1; fi
	mkdir -p $(BUILD_ROOT)
	: > $(KERNEL_TRACE_LOG)
	qemu-system-aarch64 \
		-M virt,gic-version=3,virtualization=off,secure=off \
		-smp cpus=4 -cpu cortex-a57 \
		-m 4G \
		-serial $(SERIAL) \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_EFI_CODE) \
		-drive if=pflash,format=raw,file=$(PI_UEFI_EFI_VARS) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(SD_CARD_FAT32_IMG),format=raw,id=sd0 \
		-device sdhci-pci,id=sdhci0 \
		-device sd-card,drive=sd0 \
		-drive if=none,file=$(PI_UEFI_ESP_IMG),format=raw,id=esp \
		-device virtio-blk-device,drive=esp \
		-nographic \
		-semihosting-config enable=on,target=native \
		-d int,unimp \
		-D $(BUILD_ROOT)/qemu-pi-uefi.log \
		-monitor none

qemu-pi-uefi-firmware-framebuffer-trace: BOOT_CONFIG = qemu_pi_uefi_video
qemu-pi-uefi-firmware-framebuffer-trace:
	rm -rf $(PI_UEFI_BUILD_DIR)/kernel $(PI_UEFI_KERNEL_ELF) $(PI_UEFI_ESP_IMG)
	$(MAKE) BOOT_PLATFORM=pi_uefi UEFI_DEVICE_PLATFORM=qemu_virt BSP_DEFINES="-DBSP_RPI4 -DKERNEL_SEMIHOSTING_TRACE" LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld $(PI_UEFI_ESP_IMG) $(PI_UEFI_EFI_VARS) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG)
	@if [ -z "$(QEMU_EFI_CODE)" ]; then echo "Missing QEMU AArch64 EDK2 code firmware. Set QEMU_EFI_CODE=/path/to/code.fd"; exit 1; fi
	@if [ -z "$(QEMU_EFI_VARS_TEMPLATE)" ]; then echo "Missing QEMU EDK2 vars template. Set QEMU_EFI_VARS_TEMPLATE=/path/to/vars.fd"; exit 1; fi
	mkdir -p $(BUILD_ROOT)
	: > $(KERNEL_TRACE_LOG)
	: > $(BUILD_ROOT)/uefi-framebuffer-serial.log
	qemu-system-aarch64 \
		-M virt,gic-version=3,virtualization=off,secure=off \
		-smp cpus=4 -cpu cortex-a57 \
		-m 4G \
		-serial file:$(BUILD_ROOT)/uefi-framebuffer-serial.log \
		-drive if=pflash,format=raw,readonly=on,file=$(QEMU_EFI_CODE) \
		-drive if=pflash,format=raw,file=$(PI_UEFI_EFI_VARS) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(SD_CARD_FAT32_IMG),format=raw,id=sd0 \
		-device sdhci-pci,id=sdhci0 \
		-device sd-card,drive=sd0 \
		-drive if=none,file=$(PI_UEFI_ESP_IMG),format=raw,id=esp \
		-device virtio-blk-device,drive=esp \
		-device ramfb \
		-device virtio-keyboard-device \
		-semihosting-config enable=on,target=native \
		-d int,unimp \
		-D $(BUILD_ROOT)/qemu-pi-uefi-framebuffer.log \
		-monitor none

qemu-virt: BOOT_CONFIG = qemu_virt
qemu-virt: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-smp cpus=4 -cpu max \
		-m 4G  \
		-serial $(SERIAL) \
		-cpu cortex-a57 \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-kernel $(ELF) \
		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-hvf: BOOT_CONFIG = qemu_virt_hvf
qemu-virt-hvf: $(ELF) $(EXT2_IMG) $(HVF_BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	qemu-system-aarch64 \
		-M virt,gic-version=3,accel=hvf \
		-smp cpus=4 \
		-cpu host \
		-m 4G  \
		-serial $(SERIAL) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(SD_CARD_FAT32_IMG),format=raw,id=sd0 \
		-device virtio-blk-device,drive=sd0 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-kernel $(ELF) \
		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-hvf-trace: BOOT_CONFIG = qemu_virt_hvf
qemu-virt-hvf-trace:
	$(MAKE) $(DISK_IMG)
	$(MAKE) $(EXT2_IMG)
	$(MAKE) $(HVF_BOOT_CONFIG_EXT2_IMG)
	$(MAKE) $(SD_CARD_FAT32_IMG)
	$(MAKE) $(CHANNELIO_DISK_IMG)
	$(MAKE) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	$(MAKE) KERNEL_BUILD_DIR="$(HVF_TRACE_KERNEL_BUILD_DIR)" ELF="$(HVF_TRACE_ELF)" \
		EXTRA_KERNEL_CFLAGS="$(EXTRA_KERNEL_CFLAGS) -DHEAP_DEBUG -DKERNEL_SERIAL_TRACE" \
		$(HVF_TRACE_ELF)
	mkdir -p $(BUILD_ROOT)
	: > $(KERNEL_TRACE_LOG)
	qemu-system-aarch64 \
		-M virt,gic-version=3,accel=hvf \
		-smp cpus=4 \
		-cpu host \
		-m 4G  \
		-chardev stdio,id=console,signal=off \
		-serial chardev:console \
		-serial file:$(KERNEL_TRACE_LOG) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(HVF_BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(SD_CARD_FAT32_IMG),format=raw,id=sd0 \
		-device virtio-blk-device,drive=sd0 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-kernel $(HVF_TRACE_ELF) \
		-display none \
		-monitor telnet::4444,server,nowait

qemu-virt-tcg-icount-trace: BOOT_CONFIG = qemu_virt_hvf
qemu-virt-tcg-icount-trace:
	$(MAKE) $(DISK_IMG)
	$(MAKE) $(EXT2_IMG)
	$(MAKE) $(HVF_BOOT_CONFIG_EXT2_IMG)
	$(MAKE) $(SD_CARD_FAT32_IMG)
	$(MAKE) $(CHANNELIO_DISK_IMG)
	$(MAKE) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	$(MAKE) KERNEL_BUILD_DIR="$(TCG_ICOUNT_TRACE_KERNEL_BUILD_DIR)" ELF="$(TCG_ICOUNT_TRACE_ELF)" \
		EXTRA_KERNEL_CFLAGS="$(EXTRA_KERNEL_CFLAGS) -DBSP_WFE_WAIT -DHEAP_DEBUG -DKERNEL_SERIAL_TRACE" \
		$(TCG_ICOUNT_TRACE_ELF)
	mkdir -p $(BUILD_ROOT)
	: > $(KERNEL_TRACE_LOG)
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-accel tcg,thread=single \
		-icount shift=auto,sleep=on \
		-smp cpus=4 \
		-cpu cortex-a57 \
		-m 4G \
		-serial $(SERIAL) \
		-serial file:$(KERNEL_TRACE_LOG) \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(HVF_BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(SD_CARD_FAT32_IMG),format=raw,id=sd0 \
		-device virtio-blk-device,drive=sd0 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-kernel $(TCG_ICOUNT_TRACE_ELF) \
		-nographic \
		-monitor none

qemu-virt-1cpu: BOOT_CONFIG = qemu_virt
qemu-virt-1cpu: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	qemu-system-aarch64 \
		-M virt,gic-version=3,virtualization=off,secure=off \
		-smp cpus=1 -cpu max \
		-m 4G  \
		-serial $(SERIAL) \
		-cpu cortex-a57 \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
  		-kernel $(ELF) \
  		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-1cpu-trace: BOOT_CONFIG = qemu_virt
qemu-virt-1cpu-trace:
	$(MAKE) $(DISK_IMG)
	$(MAKE) $(EXT2_IMG)
	$(MAKE) $(BOOT_CONFIG_EXT2_IMG)
	$(MAKE) $(CHANNELIO_DISK_IMG)
	$(MAKE) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	$(MAKE) EXTRA_KERNEL_CFLAGS="$(EXTRA_KERNEL_CFLAGS) -DKERNEL_SEMIHOSTING_TRACE" $(BIN)
	mkdir -p $(BUILD_ROOT)
	: > $(KERNEL_TRACE_LOG)
	qemu-system-aarch64 \
		-M virt,gic-version=3,virtualization=off,secure=off \
		-smp cpus=1 -cpu max \
		-m 4G  \
		-serial $(SERIAL) \
		-monitor none \
		-cpu cortex-a57 \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-kernel $(ELF) \
		-nographic \
		-semihosting-config enable=on,target=native \
		-d int,unimp \
		-D $(BUILD_ROOT)/qemu-1cpu.log

qemu-virt-2cpus-trace: BOOT_CONFIG = qemu_virt
qemu-virt-2cpus-trace:
	$(MAKE) $(DISK_IMG)
	$(MAKE) $(EXT2_IMG)
	$(MAKE) $(BOOT_CONFIG_EXT2_IMG)
	$(MAKE) $(CHANNELIO_DISK_IMG)
	$(MAKE) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	$(MAKE) EXTRA_KERNEL_CFLAGS="$(EXTRA_KERNEL_CFLAGS) -DKERNEL_SEMIHOSTING_TRACE" $(BIN)
	mkdir -p $(BUILD_ROOT)
	: > $(KERNEL_TRACE_LOG)
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-smp cpus=2 -cpu max \
		-m 4G  \
		-serial $(SERIAL) \
		-monitor none \
		-cpu cortex-a57 \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-kernel $(ELF) \
		-nographic \
		-semihosting-config enable=on,target=native \
		-d int,unimp \
		-D $(BUILD_ROOT)/qemu-2cpus.log

qemu-virt-2cpus: BOOT_CONFIG = qemu_virt
qemu-virt-2cpus: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG) $(HOSTED_EXT2_IMG)
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-smp cpus=2 -cpu max \
		-m 4G  \
		-serial $(SERIAL) \
		-cpu cortex-a57 \
		-drive if=none,file=$(EXT2_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-drive if=none,file=$(BOOT_CONFIG_EXT2_IMG),format=raw,id=hd1 \
		-device virtio-blk-device,drive=hd1 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd2 \
		-device virtio-blk-device,drive=hd2 \
		-drive if=none,file=$(CHANNELIO_DISK_IMG),format=raw,id=channelio0 \
		-device virtio-blk-device,drive=channelio0 \
		-drive if=none,file=$(CHANNELIO_EXT2_IMG),format=raw,id=channelio1 \
		-device virtio-blk-device,drive=channelio1 \
		-drive if=none,file=$(HOSTED_EXT2_IMG),format=raw,id=hostedext2 \
		-device virtio-blk-device,drive=hostedext2 \
		-kernel $(ELF) \
		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-gdb: clean $(ELF)
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-smp cpus=2 -cpu max \
		-m 4G  \
		-cpu cortex-a57 \
		-kernel $(ELF) \
		-nographic \
		-S \
		-gdb tcp::1234

.PHONY: all clean setup python-venv disk sd-fat32-disk ext2-disk qemu pi-uefi pi-uefi-efi pi-uefi-esp qemu-pi-uefi qemu-pi-uefi-firmware qemu-pi-uefi-firmware-trace qemu-virt-hvf qemu-virt-hvf-trace qemu-virt-tcg-icount-trace qemu-virt-1cpu-trace qemu-virt-2cpus-trace riscv64-bringup riscv64-bringup-elf riscv-virt-embedded riscv-virt-embedded-run milkv-duo milkv-duo-build milkv-duo-sd-image milkv-duo-sd-image-build milkv-duo-qemu-build qemu-milkv-duo milkv-duo-bringup milkv-duo-bringup-build milkv-duo-bringup-elf milkv-duo-bringup-elf-build milkv-duo-s-riscv milkv-duo-s-build-riscv milkv-duo-s-sd-image-riscv milkv-duo-s-sd-image-build-riscv milkv-duo-s-qemu-build-riscv milkv-duo-s-qemu-disk-riscv qemu-milkv-duo-s-riscv milkv-duo-s-aarch64 milkv-duo-s-build-aarch64 milkv-duo-s-sd-image-aarch64 milkv-duo-s-sd-image-aarch64-command-trace milkv-duo-s-sd-image-build-aarch64 qemu-riscv64 qemu-riscv64-run qemu-riscv64-trace qemu-riscv64-trace-build qemu-riscv64-trace-run test smoke-test hundelint hundelint-strict hundelint-test test_clean
