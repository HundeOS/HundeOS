# Target architecture and cross-compilation tools
ARCH ?= aarch64

ifeq ($(ARCH),aarch64)
CROSS_COMPILE ?= aarch64-elf-
ARCH_CFLAGS = -mcmodel=large -march=armv8-a -mno-outline-atomics -mgeneral-regs-only
ARCH_USER_CFLAGS = -mcmodel=small -march=armv8-a -mno-outline-atomics
ARCH_ASFLAGS = -mcmodel=small
OBJCOPY_BINARY_FORMAT = elf64-littleaarch64
OBJCOPY_BINARY_ARCH = aarch64
else ifeq ($(ARCH),riscv64)
CROSS_COMPILE ?= riscv64-elf-
ARCH_CFLAGS = -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -mno-relax -msmall-data-limit=0
ARCH_USER_CFLAGS = -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -mno-relax -msmall-data-limit=0
ARCH_ASFLAGS = -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -mno-relax -msmall-data-limit=0
OBJCOPY_BINARY_FORMAT = elf64-littleriscv
OBJCOPY_BINARY_ARCH = riscv
else
$(error Unsupported ARCH '$(ARCH)')
endif

AS = $(CROSS_COMPILE)gcc
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

UNITY_PATH=vendor/Unity
BOOT_PLATFORM ?= qemu_virt
BSP_DEFINES ?= -DBSP_VIRT
UV ?= uv
CLANG_FORMAT = clang-format
PYTHON = $(UV) run python
PYTHON_ENV_STAMP = .venv/.uv-sync
# Compiler flags
CFLAGS = -c -MMD -MP -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
           -nostdlib -nostartfiles $(ARCH_CFLAGS) $(BSP_DEFINES) \
           -DKERNEL_TRACE_PATH=\"$(KERNEL_TRACE_LOG)\" -Ikernel -I./common -I./common_includes $(EXTRA_KERNEL_CFLAGS)

USER_CFLAGS = -c -MMD -MP -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
           -nostdlib -nostartfiles $(ARCH_USER_CFLAGS) \
           -I./user/lib/fiber/arch/$(ARCH) -I./user/lib -I./common -I./common_includes -I./user/lib/newlib/include

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
USER_LDFLAGS = -nostdlib -Wl,-e,_start

# Output files
BUILD_ROOT = build/$(ARCH)
TARGET_BUILD_DIR = $(BUILD_ROOT)/$(BOOT_PLATFORM)
KERNEL_BUILD_DIR = $(TARGET_BUILD_DIR)/kernel
USER_BUILD_DIR = $(BUILD_ROOT)/user
IMAGE_BUILD_DIR = $(BUILD_ROOT)/images
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
	arch/riscv64/boot/qemu_virt/boot.c \
	arch/riscv64/boot/qemu_virt/device_tree.c \
	arch/riscv64/boot/qemu_virt/mmu.c \
	arch/riscv64/boot/qemu_virt/runtime.c \
	arch/riscv64/boot/qemu_virt/system_info.c \
	arch/riscv64/boot/qemu_virt/trap.c \
	kernel/early_boot.c \
	kernel/system_info.c
RISCV64_BRINGUP_ASM_SOURCES = \
	arch/riscv64/boot/qemu_virt/start.s \
	arch/riscv64/boot/qemu_virt/trap_entry.s
RISCV64_BRINGUP_OBJECTS = \
	$(patsubst %.c,$(RISCV64_BRINGUP_BUILD_DIR)/%.o,$(RISCV64_BRINGUP_C_SOURCES)) \
	$(patsubst %.s,$(RISCV64_BRINGUP_BUILD_DIR)/%.o,$(RISCV64_BRINGUP_ASM_SOURCES))
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
BOOT_CONFIG ?= qemu_virt
BOOT_CONFIG_EXT2_IMG = $(BUILD_ROOT)/boot_config/$(BOOT_CONFIG).img
HVF_BOOT_CONFIG_EXT2_IMG = $(BUILD_ROOT)/boot_config/qemu_virt_hvf.img
BOOT_CONFIG_EXT2_SIZE_MB = 8
BOOT_CONFIG_EXT2_LABEL = BOOTCFG
BOOT_CONFIG_ROOT_DIR = config/$(BOOT_CONFIG)
EXT2_IMG_BLOCK_SIZE = 1024
EXT2_INODE_SIZE = 128
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
ARCH_PLATFORM_SOURCES := $(shell find ./arch/$(ARCH)/boot/$(BOOT_PLATFORM) -name "*.c" \
	! -name "loader.c" ! -name "runtime.c" -print 2>/dev/null)
ARCH_SOURCES := $(ARCH_COMMON_SOURCES) $(ARCH_PLATFORM_SOURCES)
ARCH_COMMON_ASM_SOURCES := $(shell find ./arch/$(ARCH) -path "./arch/$(ARCH)/boot/*/*" -prune -o -name "*.s" -print 2>/dev/null)
ARCH_PLATFORM_ASM_SOURCES := $(shell find ./arch/$(ARCH)/boot/$(BOOT_PLATFORM) -name "*.s" -print 2>/dev/null)
ARCH_ASM_SOURCES := $(ARCH_COMMON_ASM_SOURCES) $(ARCH_PLATFORM_ASM_SOURCES)
ARCH_COMMON_CPP_ASM_SOURCES := $(shell find ./arch/$(ARCH) -path "./arch/$(ARCH)/boot/*/*" -prune -o -name "*.S" -print 2>/dev/null)
ARCH_PLATFORM_CPP_ASM_SOURCES := $(shell find ./arch/$(ARCH)/boot/$(BOOT_PLATFORM) -name "*.S" -print 2>/dev/null)
ARCH_CPP_ASM_SOURCES := $(ARCH_COMMON_CPP_ASM_SOURCES) $(ARCH_PLATFORM_CPP_ASM_SOURCES)

# Define object files based on source files, potentially in a build directory
OBJECTS := $(patsubst %.c,$(KERNEL_BUILD_DIR)/%.o,$(SOURCES))
ASM_OBJECTS := $(patsubst %.s,$(KERNEL_BUILD_DIR)/%.o,$(ASM_SOURCES))
CPP_ASM_OBJECTS := $(patsubst %.S,$(KERNEL_BUILD_DIR)/%.o,$(CPP_ASM_SOURCES))
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

USER_BINARY_ROOTS := user/programs user/commands user/drivers user/services
USER_BINARY_DIRS := $(foreach root,$(USER_BINARY_ROOTS),$(shell find $(root) -mindepth 1 -maxdepth 1 -type d -print 2>/dev/null))
USER_DRIVER_DIRS := $(filter user/drivers/%,$(USER_BINARY_DIRS))
USER_SERVICE_DIRS := $(filter user/services/%,$(USER_BINARY_DIRS))
USER_NON_DRIVER_DIRS := $(filter-out $(USER_DRIVER_DIRS) $(USER_SERVICE_DIRS),$(USER_BINARY_DIRS))
USER_BINARIES := $(patsubst %,$(USER_BUILD_DIR)/%.bin,$(notdir $(USER_BINARY_DIRS)))
USER_VIDEO_TERM_FONT := vendor/unifont/unifont_all-17.0.05.hex
USER_VIDEO_TERM_FONT_OBJECT := $(USER_BUILD_DIR)/drivers/video_term/unifont_all.o
USER_VIDEO_TERM_EMOJI_PNGS := $(wildcard vendor/noto_emoji/png/128/emoji_u*.png)
USER_VIDEO_TERM_EMOJI_HEADER := user/drivers/video_term/emoji_atlas.h
USER_VIDEO_TERM_EMOJI_ATLAS := $(USER_BUILD_DIR)/drivers/video_term/emoji_atlas.rgba
USER_VIDEO_TERM_EMOJI_ATLAS_OBJECT := $(USER_BUILD_DIR)/drivers/video_term/emoji_atlas.o
video_term_EXTRA_OBJS := $(USER_VIDEO_TERM_FONT_OBJECT) $(USER_VIDEO_TERM_EMOJI_ATLAS_OBJECT)
EMBEDDED_FS_INPUTS := \
	$(foreach dir,$(USER_NON_DRIVER_DIRS),$(USER_BUILD_DIR)/$(notdir $(dir)).bin) \
	$(foreach dir,$(USER_DRIVER_DIRS),drivers/$(notdir $(dir))=$(USER_BUILD_DIR)/$(notdir $(dir)).bin) \
	$(foreach dir,$(USER_SERVICE_DIRS),services/$(notdir $(dir))=$(USER_BUILD_DIR)/$(notdir $(dir)).bin)
EMBEDDED_FS := $(BUILD_ROOT)/kernel/embedded.fs

.PRECIOUS: $(USER_BUILD_DIR)/lib/arch/%.o

all: $(BIN)

# Semihosting version (better for QEMU debugging)
elf: $(ELF)
kernel: hundelint user_binaries $(BIN)

# Capture real kernel and userspace flags without compiling or launching QEMU.
.PHONY: compile-commands
compile-commands:
	rm -f compile_commands.json
	$(UV) tool run --from compiledb==0.10.7 compiledb --no-build --overwrite --full-path make -B ARCH=aarch64 elf
	$(UV) tool run --from compiledb==0.10.7 compiledb --no-build --full-path make -B ARCH=riscv64 riscv64-bringup-elf
	$(UV) tool run --from compiledb==0.10.7 compiledb --no-build --full-path make -B ARCH=riscv64 \
		build/riscv64/qemu_virt/kernel/arch/riscv64/page_table.o \
		build/riscv64/qemu_virt/kernel/arch/riscv64/mmu.o
	$(UV) tool run --from compiledb==0.10.7 compiledb --no-build --full-path make -B ARCH=riscv64 \
		build/riscv64/user/hello_world.bin

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
		$(EXT2_MKFS) -b $$(( $(EXT2_SIZE_MB) * 1024 )) -B $(EXT2_IMG_BLOCK_SIZE) -L $(CHANNELIO_EXT2_LABEL) -d $(EXT2_ROOT_DIR) $@.tmp; \
	else \
		$(EXT2_MKFS) -q -F -t ext2 -b $(EXT2_IMG_BLOCK_SIZE) -I $(EXT2_INODE_SIZE) -L $(CHANNELIO_EXT2_LABEL) -d $(EXT2_ROOT_DIR) $@.tmp; \
	fi
	mv $@.tmp $@

$(SD_CARD_FAT32_IMG): $(PYTHON_ENV_STAMP) tools/populate_fat32_image.py $(SD_CARD_FAT32_README_FILE) $(SD_CARD_FAT32_CONFIG_FILE) $(SD_CARD_FAT32_NOTE_FILE)
	mkdir -p `dirname $@`
	dd if=/dev/zero of=$@ bs=1048576 count=$(SD_CARD_FAT32_SIZE_MB)
	$(MKFS_FAT) -F 32 -S 512 -s 1 -R 32 -n $(SD_CARD_FAT32_LABEL) $@
	$(PYTHON) tools/populate_fat32_image.py $@ README.TXT $(SD_CARD_FAT32_README_FILE)
	$(PYTHON) tools/populate_fat32_image.py $@ CONFIG.TXT $(SD_CARD_FAT32_CONFIG_FILE)
	$(PYTHON) tools/populate_fat32_image.py $@ DOCS/NOTE.TXT $(SD_CARD_FAT32_NOTE_FILE)

$(EXT2_IMG): $(shell find $(EXT2_ROOT_DIR) -type f -print)
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
		$(EXT2_MKFS) -b $$(( $(EXT2_SIZE_MB) * 1024 )) -B $(EXT2_IMG_BLOCK_SIZE) -L $(EXT2_LABEL) -d $(EXT2_ROOT_DIR) $@.tmp; \
	else \
		$(EXT2_MKFS) -q -F -t ext2 -b $(EXT2_IMG_BLOCK_SIZE) -I $(EXT2_INODE_SIZE) -L $(EXT2_LABEL) -d $(EXT2_ROOT_DIR) $@.tmp; \
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

$(RISCV64_BRINGUP_BUILD_DIR)/%.o: %.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

$(RISCV64_BRINGUP_BUILD_DIR)/%.o: %.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

$(EMBEDDED_FS): $(PYTHON_ENV_STAMP) $(USER_BINARIES) tools/build_embedded_fs.py
	$(PYTHON) tools/build_embedded_fs.py $@ $(EMBEDDED_FS_INPUTS)

$(KERNEL_BUILD_DIR)/./kernel/bin/embedded_fs.o: kernel/bin/embedded_fs.S $(EMBEDDED_FS)
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) -DEMBEDDED_FS_PATH=\"$(abspath $(EMBEDDED_FS))\" $< -o $@

$(USER_BUILD_DIR)/%.o: user/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/%.o: user/%.s
	mkdir -p `dirname $@`
	$(AS) $(USER_ASFLAGS) $< -o $@

$(USER_BUILD_DIR)/common/%.o: common/%.c
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
		--redefine-sym _binary_build_$(ARCH)_user_drivers_video_term_emoji_atlas_rgba_start=_binary_build_user_drivers_video_term_emoji_atlas_rgba_start \
		--redefine-sym _binary_build_$(ARCH)_user_drivers_video_term_emoji_atlas_rgba_end=_binary_build_user_drivers_video_term_emoji_atlas_rgba_end \
		--redefine-sym _binary_build_$(ARCH)_user_drivers_video_term_emoji_atlas_rgba_size=_binary_build_user_drivers_video_term_emoji_atlas_rgba_size \
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
$(ELF): $(ASM_OBJECTS) $(CPP_ASM_OBJECTS) $(OBJECTS) $(COMMON_KERNEL_OBJECTS) $(ARCH_ASM_OBJECTS) $(ARCH_CPP_ASM_OBJECTS) $(ARCH_OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

$(RISCV64_BRINGUP_ELF): $(RISCV64_BRINGUP_OBJECTS) arch/riscv64/boot/qemu_virt/linker.ld
	$(LD) -nostdlib -Wl,--build-id=none -Wl,--no-relax -T arch/riscv64/boot/qemu_virt/linker.ld \
		$(RISCV64_BRINGUP_OBJECTS) -o $@

riscv64-bringup:
	$(MAKE) ARCH=riscv64 riscv64-bringup-elf

riscv64-bringup-elf: $(RISCV64_BRINGUP_ELF)

qemu-riscv64:
	$(MAKE) ARCH=riscv64 BOOT_CONFIG=qemu_riscv64 qemu-riscv64-run

qemu-riscv64-run: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG)
	qemu-system-riscv64 -machine virt -cpu rv64 -smp 4 -m 1G -bios default \
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
		-nographic -monitor none -no-reboot

qemu-riscv64-trace:
	$(MAKE) ARCH=riscv64 BOOT_CONFIG=qemu_riscv64 qemu-riscv64-trace-build

qemu-riscv64-trace-build:
	$(MAKE) KERNEL_BUILD_DIR="$(RISCV64_TRACE_KERNEL_BUILD_DIR)" \
		ELF="$(RISCV64_TRACE_ELF)" EXTRA_KERNEL_CFLAGS="-DKERNEL_SEMIHOSTING_TRACE" $(RISCV64_TRACE_ELF)
	$(MAKE) KERNEL_BUILD_DIR="$(RISCV64_TRACE_KERNEL_BUILD_DIR)" \
		ELF="$(RISCV64_TRACE_ELF)" qemu-riscv64-trace-run

qemu-riscv64-trace-run: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG)
	mkdir -p `dirname $(KERNEL_TRACE_LOG)`
	: > $(KERNEL_TRACE_LOG)
	qemu-system-riscv64 -machine virt -cpu rv64 -smp 4 -m 1G -bios default \
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
		-nographic -monitor none -no-reboot \
		-semihosting-config enable=on,target=native

# Clean build artifacts
clean:
	rm -rf build

test: $(PYTHON_ENV_STAMP)
	$(PYTHON) -m pytest
	make test -C test

smoke-test: $(PYTHON_ENV_STAMP)
	$(PYTHON) -m pytest test/smoke

.PHONY: format format-check format-test
format: $(PYTHON_ENV_STAMP)
	$(PYTHON) tools/hundeformat.py -- $(CLANG_FORMAT)

format-check: $(PYTHON_ENV_STAMP)
	$(PYTHON) tools/hundeformat.py --check -- $(CLANG_FORMAT)

format-test: $(PYTHON_ENV_STAMP)
	$(PYTHON) -m pytest test/tools/test_hundeformat.py --clang-format "$(CLANG_FORMAT)"

hundelint: $(PYTHON_ENV_STAMP)
	$(PYTHON) tools/hundelint.py --require-tree-sitter

hundelint-strict: $(PYTHON_ENV_STAMP)
	$(PYTHON) tools/hundelint.py --require-tree-sitter --strict

hundelint-test: $(PYTHON_ENV_STAMP)
	$(PYTHON) -m pytest test/tools/test_hundelint.py

test_clean:
	make clean -C test

define make-user-bin
$(notdir $(1))_SRCS := $$(wildcard $(1)/*.c)
$(notdir $(1))_ASM_SRCS := $$(wildcard $(1)/*.s)
$(notdir $(1))_OBJS := $$(patsubst user/%.c,$$(USER_BUILD_DIR)/%.o,$$($(notdir $(1))_SRCS))
$(notdir $(1))_ASM_OBJS := $$(patsubst user/%.s,$$(USER_BUILD_DIR)/%.o,$$($(notdir $(1))_ASM_SRCS))

$$(USER_BUILD_DIR)/$(notdir $(1)).bin: $$($(notdir $(1))_OBJS) $$($(notdir $(1))_ASM_OBJS) $$($(notdir $(1))_EXTRA_OBJS) $$(COMMON_USER_OBJECTS) $$(USER_LIB_OBJECTS) $$(USER_LIB_ASM_OBJECTS)
	$$(LD) $$(USER_LDFLAGS) $$^ -o $$@
endef

$(foreach dir,$(USER_BINARY_DIRS),$(eval $(call make-user-bin,$(dir))))

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
	$(MAKE) BOOT_PLATFORM=pi_uefi BSP_DEFINES="-DBSP_RPI4 -DKERNEL_SEMIHOSTING_TRACE" LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld $(PI_UEFI_ESP_IMG) $(PI_UEFI_EFI_VARS) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG)
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
	$(MAKE) BOOT_PLATFORM=pi_uefi BSP_DEFINES="-DBSP_RPI4 -DKERNEL_SEMIHOSTING_TRACE" LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld $(PI_UEFI_ESP_IMG) $(PI_UEFI_EFI_VARS) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG)
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
	$(MAKE) BOOT_PLATFORM=pi_uefi BSP_DEFINES="-DBSP_RPI4 -DKERNEL_SEMIHOSTING_TRACE" LINKER_SCRIPT=arch/aarch64/boot/pi_uefi/linker.ld $(PI_UEFI_ESP_IMG) $(PI_UEFI_EFI_VARS) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG)
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
qemu-virt: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG)
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
		-kernel $(ELF) \
		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-hvf: BOOT_CONFIG = qemu_virt_hvf
qemu-virt-hvf: $(ELF) $(EXT2_IMG) $(HVF_BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(SD_CARD_FAT32_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG)
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
		-kernel $(ELF) \
		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-hvf-trace: BOOT_CONFIG = qemu_virt_hvf
qemu-virt-hvf-trace:
	$(MAKE) $(DISK_IMG)
	@if [ ! -f "$(EXT2_IMG)" ]; then $(MAKE) $(EXT2_IMG); fi
	$(MAKE) $(HVF_BOOT_CONFIG_EXT2_IMG)
	$(MAKE) $(SD_CARD_FAT32_IMG)
	$(MAKE) $(CHANNELIO_DISK_IMG)
	$(MAKE) $(CHANNELIO_EXT2_IMG)
	$(MAKE) KERNEL_BUILD_DIR="$(HVF_TRACE_KERNEL_BUILD_DIR)" ELF="$(HVF_TRACE_ELF)" \
		EXTRA_KERNEL_CFLAGS="$(EXTRA_KERNEL_CFLAGS) -DHEAP_DEBUG -DKERNEL_SERIAL_TRACE -DKERNEL_LOW_VOLUME_TRACE" \
		$(HVF_TRACE_ELF)
	mkdir -p $(BUILD_ROOT)
	: > $(KERNEL_TRACE_LOG)
	qemu-system-aarch64 \
		-M virt,gic-version=3,accel=hvf \
		-smp cpus=4 \
		-cpu host \
		-m 4G  \
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
		-kernel $(HVF_TRACE_ELF) \
		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-tcg-icount-trace: BOOT_CONFIG = qemu_virt_hvf
qemu-virt-tcg-icount-trace:
	$(MAKE) $(DISK_IMG)
	@if [ ! -f "$(EXT2_IMG)" ]; then $(MAKE) $(EXT2_IMG); fi
	$(MAKE) $(HVF_BOOT_CONFIG_EXT2_IMG)
	$(MAKE) $(SD_CARD_FAT32_IMG)
	$(MAKE) $(CHANNELIO_DISK_IMG)
	$(MAKE) $(CHANNELIO_EXT2_IMG)
	$(MAKE) KERNEL_BUILD_DIR="$(TCG_ICOUNT_TRACE_KERNEL_BUILD_DIR)" ELF="$(TCG_ICOUNT_TRACE_ELF)" \
		EXTRA_KERNEL_CFLAGS="$(EXTRA_KERNEL_CFLAGS) -DBSP_WFE_WAIT -DHEAP_DEBUG -DKERNEL_SERIAL_TRACE -DKERNEL_LOW_VOLUME_TRACE" \
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
		-kernel $(TCG_ICOUNT_TRACE_ELF) \
		-nographic \
		-monitor none

qemu-virt-1cpu: BOOT_CONFIG = qemu_virt
qemu-virt-1cpu: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG)
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
  		-kernel $(ELF) \
  		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-1cpu-trace: BOOT_CONFIG = qemu_virt
qemu-virt-1cpu-trace:
	$(MAKE) $(DISK_IMG)
	@if [ ! -f "$(EXT2_IMG)" ]; then $(MAKE) $(EXT2_IMG); fi
	$(MAKE) $(BOOT_CONFIG_EXT2_IMG)
	$(MAKE) $(CHANNELIO_DISK_IMG)
	$(MAKE) $(CHANNELIO_EXT2_IMG)
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
		-kernel $(ELF) \
		-nographic \
		-semihosting-config enable=on,target=native \
		-d int,unimp \
		-D $(BUILD_ROOT)/qemu-1cpu.log

qemu-virt-2cpus-trace: BOOT_CONFIG = qemu_virt
qemu-virt-2cpus-trace:
	$(MAKE) $(DISK_IMG)
	@if [ ! -f "$(EXT2_IMG)" ]; then $(MAKE) $(EXT2_IMG); fi
	$(MAKE) $(BOOT_CONFIG_EXT2_IMG)
	$(MAKE) $(CHANNELIO_DISK_IMG)
	$(MAKE) $(CHANNELIO_EXT2_IMG)
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
		-kernel $(ELF) \
		-nographic \
		-semihosting-config enable=on,target=native \
		-d int,unimp \
		-D $(BUILD_ROOT)/qemu-2cpus.log

qemu-virt-2cpus: BOOT_CONFIG = qemu_virt
qemu-virt-2cpus: $(ELF) $(EXT2_IMG) $(BOOT_CONFIG_EXT2_IMG) $(DISK_IMG) $(CHANNELIO_DISK_IMG) $(CHANNELIO_EXT2_IMG)
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

.PHONY: all clean setup python-venv disk sd-fat32-disk ext2-disk qemu pi-uefi pi-uefi-efi pi-uefi-esp qemu-pi-uefi qemu-pi-uefi-firmware qemu-pi-uefi-firmware-trace qemu-virt-hvf qemu-virt-hvf-trace qemu-virt-tcg-icount-trace qemu-virt-1cpu-trace qemu-virt-2cpus-trace riscv64-bringup riscv64-bringup-elf qemu-riscv64 qemu-riscv64-run qemu-riscv64-trace qemu-riscv64-trace-build qemu-riscv64-trace-run test smoke-test hundelint hundelint-strict hundelint-test test_clean
