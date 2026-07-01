# Cross-compilation tools
CROSS_COMPILE = aarch64-elf-
AS = $(CROSS_COMPILE)gcc
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

UNITY_PATH=../../tools/Unity
# Compiler flags
CFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
           -nostdlib -nostartfiles -mcmodel=large -DBSP_VIRT \
           -march=armv8-a -mno-outline-atomics -Ikernel -I./common -I./common_includes

USER_CFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
           -nostdlib -nostartfiles -mcmodel=small \
           -march=armv8-a -mno-outline-atomics -I./common -I./common_includes -I./user/lib/newlib/include

# Assembler flags
ASFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
            -nostdlib -nostartfiles -mcmodel=small -latomic

USER_ASFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
            -nostdlib -nostartfiles -mcmodel=small -latomic

ifdef __BOOT_DEBUG
CFLAGS += -D__BOOT_DEBUG
ASFLAGS += -Wa,-defsym,__BOOT_DEBUG=1
endif

# Linker flags
LDFLAGS = -nostdlib -T linker.ld
USER_LDFLAGS = -nostdlib -Wl,-e,_start

# Output files
ELF = build/kernel.elf
BIN = build/kernel.img
DISK_IMG = build/disk.img
DISK_SIZE_MB = 64
DISK_LABEL = KERNELFS
DISK_TEST_FILE = tools/disk_test_file.txt
DISK_TEST_NAME = TEST.TXT
DISK_LONG_FILE = tools/disk_long_filename.txt
DISK_LONG_NAME = This is a long filename.txt
DISK_LARGE_FILE = tools/disk_large_file.txt
DISK_LARGE_NAME = BIGFILE.TXT
MKFS_FAT ?= mkfs.fat

SOURCES := $(shell find ./kernel -name "*.c" -print)
ASM_SOURCES := $(shell find ./kernel -name "*.s" -print)

COMMON_SOURCES := $(shell find ./common -name "*.c" -print)

ARCH_SOURCES := $(shell find ./arch/aarch64 -name "*.c" -print)
ARCH_ASM_SOURCES := $(shell find ./arch/aarch64 -name "*.s" -print)

# Define object files based on source files, potentially in a build directory
OBJECTS := $(patsubst %.c,build/kernel/%.o,$(SOURCES))
ASM_OBJECTS := $(patsubst %.s,build/kernel/%.o,$(ASM_SOURCES))
COMMON_KERNEL_OBJECTS := $(patsubst %.c,build/kernel/%.o,$(COMMON_SOURCES))

ARCH_OBJECTS := $(patsubst %.c,build/kernel/%.o,$(ARCH_SOURCES))
ARCH_ASM_OBJECTS := $(patsubst %.s,build/kernel/%.o,$(ARCH_ASM_SOURCES))

USER_SOURCES := $(shell find ./user -name "*.c" -print)
USER_ASM_SOURCES := $(shell find ./user -name "*.s" -print)

USER_ARCH_SOURCES := $(shell find ./user/arch -name "*.c" -print)
USER_ARCH_ASM_SOURCES := $(shell find ./user/arch -name "*.s" -print)

USER_SYS_SOURCES := $(shell find ./user/sys -name "*.c" -print)
USER_SYS_ASM_SOURCES := $(shell find ./user/sys -name "*.s" -print)

USER_LIB_SOURCES := $(shell find ./user/lib -name "*.c" ! -name "nano-mallocr.c" -print)
USER_LIB_ASM_SOURCES := $(shell find ./user/lib -name "*.s" -print)

USER_OBJECTS := $(patsubst ./%.c,build/%.o,$(USER_SOURCES))
USER_ASM_OBJECTS := $(patsubst ./%.s,build/%.o,$(USER_ASM_SOURCES))

USER_ARCH_OBJECTS := $(patsubst ./%.c,build/%.o,$(USER_ARCH_SOURCES))
USER_ARCH_ASM_OBJECTS := $(patsubst ./%.s,build/%.o,$(USER_ARCH_ASM_SOURCES))

USER_SYS_OBJECTS := $(patsubst ./%.c,build/%.o,$(USER_SYS_SOURCES))
USER_SYS_ASM_OBJECTS := $(patsubst ./%.s,build/%.o,$(USER_SYS_ASM_SOURCES))

USER_LIB_OBJECTS := $(patsubst ./%.c,build/%.o,$(USER_LIB_SOURCES))
USER_LIB_ASM_OBJECTS := $(patsubst ./%.s,build/%.o,$(USER_LIB_ASM_SOURCES))
COMMON_USER_OBJECTS := $(patsubst ./common/%.c,build/common/%.o,$(COMMON_SOURCES))

USER_BINARY_ROOTS := user/programs user/commands user/drivers
USER_BINARY_DIRS := $(foreach root,$(USER_BINARY_ROOTS),$(shell find $(root) -mindepth 1 -maxdepth 1 -type d -print 2>/dev/null))
USER_BINARIES := $(patsubst %,build/user/%.bin,$(notdir $(USER_BINARY_DIRS)))
EMBEDDED_FS := build/kernel/embedded.fs

.PRECIOUS: build/user/arch/%.o

all: $(BIN)

# Semihosting version (better for QEMU debugging)
elf: $(ELF)
kernel: user_binaries $(BIN)

setup:
	mkdir -p build

$(DISK_IMG): tools/populate_fat32_image.py $(DISK_TEST_FILE) $(DISK_LONG_FILE) $(DISK_LARGE_FILE)
	mkdir -p `dirname $@`
	dd if=/dev/zero of=$@ bs=1048576 count=$(DISK_SIZE_MB)
	$(MKFS_FAT) -F 32 -S 512 -s 1 -R 32 -n $(DISK_LABEL) $@
	python3 tools/populate_fat32_image.py $@ $(DISK_TEST_NAME) $(DISK_TEST_FILE)
	python3 tools/populate_fat32_image.py $@ "$(DISK_LONG_NAME)" $(DISK_LONG_FILE)
	python3 tools/populate_fat32_image.py $@ $(DISK_LARGE_NAME) $(DISK_LARGE_FILE)

disk: tools/populate_fat32_image.py $(DISK_TEST_FILE) $(DISK_LONG_FILE) $(DISK_LARGE_FILE)
	mkdir -p `dirname $(DISK_IMG)`
	dd if=/dev/zero of=$(DISK_IMG) bs=1048576 count=$(DISK_SIZE_MB)
	$(MKFS_FAT) -F 32 -S 512 -s 1 -R 32 -n $(DISK_LABEL) $(DISK_IMG)
	python3 tools/populate_fat32_image.py $(DISK_IMG) $(DISK_TEST_NAME) $(DISK_TEST_FILE)
	python3 tools/populate_fat32_image.py $(DISK_IMG) "$(DISK_LONG_NAME)" $(DISK_LONG_FILE)
	python3 tools/populate_fat32_image.py $(DISK_IMG) $(DISK_LARGE_NAME) $(DISK_LARGE_FILE)

build/kernel/%.o: %.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/%.o: %.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

$(EMBEDDED_FS): $(USER_BINARIES) tools/build_embedded_fs.py
	python3 tools/build_embedded_fs.py $@ $(USER_BINARIES)

build/kernel/./kernel/bin/embedded.o: $(EMBEDDED_FS)

build/user/%.o: user/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/user/%.o: user/%.s
	mkdir -p `dirname $@`
	$(AS) $(USER_ASFLAGS) $< -o $@

build/common/%.o: common/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_CFLAGS) -c $< -o $@

# Create binary image from ELF
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

# Link object files to create ELF
$(ELF): $(ASM_OBJECTS) $(OBJECTS) $(COMMON_KERNEL_OBJECTS) $(ARCH_ASM_OBJECTS) $(ARCH_OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

# Clean build artifacts
clean:
	rm -rf build

test:
	make test -C test

test_clean:
	make clean -C test

define make-user-bin
$(notdir $(1))_SRCS := $$(wildcard $(1)/*.c)
$(notdir $(1))_ASM_SRCS := $$(wildcard $(1)/*.s)
$(notdir $(1))_OBJS := $$(patsubst user/%.c,build/user/%.o,$$($(notdir $(1))_SRCS))
$(notdir $(1))_ASM_OBJS := $$(patsubst user/%.s,build/user/%.o,$$($(notdir $(1))_ASM_SRCS))

build/user/$(notdir $(1)).bin: $$($(notdir $(1))_OBJS) $$($(notdir $(1))_ASM_OBJS) $$(COMMON_USER_OBJECTS) $$(USER_ARCH_ASM_OBJECTS) $$(USER_ARCH_OBJECTS) $$(USER_SYS_OBJECTS) $$(USER_SYS_ASM_OBJECTS) $$(USER_LIB_OBJECTS) $$(USER_LIB_ASM_OBJECTS)
	$$(LD) $$(USER_LDFLAGS) $$^ -o $$@
endef

$(foreach dir,$(USER_BINARY_DIRS),$(eval $(call make-user-bin,$(dir))))

user_binaries: $(USER_BINARIES)


qemu-virt: $(ELF) $(DISK_IMG)
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-smp cpus=4 -cpu max \
		-m 4G  \
		-serial stdio \
		-cpu cortex-a57 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-kernel $(ELF) \
		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-1cpu: $(ELF) $(DISK_IMG)
	qemu-system-aarch64 \
		-M virt,gic-version=3,virtualization=off,secure=off \
		-smp cpus=1 -cpu max \
		-m 4G  \
		-serial stdio \
		-cpu cortex-a57 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
  		-kernel $(ELF) \
  		-nographic \
  		-d int,unimp \
		-D qemu-1cpu.log \
		-monitor telnet::4444,server,nowait

qemu-virt-1cpu-trace:
	$(MAKE) clean
	$(MAKE) $(DISK_IMG)
	$(MAKE) CFLAGS="$(CFLAGS) -DKERNEL_SEMIHOSTING_TRACE" $(BIN)
	mkdir -p build
	: > build/kernel-trace.log
	qemu-system-aarch64 \
		-M virt,gic-version=3,virtualization=off,secure=off \
		-smp cpus=1 -cpu max \
		-m 4G  \
		-serial stdio \
		-monitor none \
		-cpu cortex-a57 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-kernel $(ELF) \
		-nographic \
		-semihosting-config enable=on,target=native \
		-d int,unimp \
		-D qemu-1cpu.log

qemu-virt-2cpus-trace:
	$(MAKE) clean
	$(MAKE) $(DISK_IMG)
	$(MAKE) CFLAGS="$(CFLAGS) -DKERNEL_SEMIHOSTING_TRACE" $(BIN)
	mkdir -p build
	: > build/kernel-trace.log
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-smp cpus=2 -cpu max \
		-m 4G  \
		-serial stdio \
		-monitor none \
		-cpu cortex-a57 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-kernel $(ELF) \
		-nographic \
		-semihosting-config enable=on,target=native \
		-d int,unimp \
		-D qemu-2cpus.log

qemu-virt-2cpus: $(ELF) $(DISK_IMG)
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-smp cpus=2 -cpu max \
		-m 4G  \
		-serial stdio \
		-cpu cortex-a57 \
		-drive if=none,file=$(DISK_IMG),format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
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

.PHONY: all clean disk qemu qemu-virt-1cpu-trace qemu-virt-2cpus-trace test test_clean
