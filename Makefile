# Cross-compilation tools
CROSS_COMPILE = aarch64-elf-
AS = $(CROSS_COMPILE)gcc
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

# Compiler flags
CFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra \
           -nostdlib -nostartfiles -mcmodel=large -DBSP_VIRT \
           -march=armv8-a -mno-outline-atomics -Ikernel

USER_CFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra \
           -nostdlib -nostartfiles -mcmodel=small \
           -march=armv8-a -mno-outline-atomics

# Assembler flags
ASFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra \
            -nostdlib -nostartfiles -mcmodel=small -latomic

USER_ASFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra \
            -nostdlib -nostartfiles -mcmodel=small -latomic

# Linker flags
LDFLAGS = -nostdlib -T linker.ld
USER_LDFLAGS = -nostdlib -Wl,-e,main

# Output files
ELF = build/kernel.elf
BIN = build/kernel.img

SOURCES := $(shell find ./kernel -name "*.c" -print)
ASM_SOURCES := $(shell find ./kernel -name "*.s" -print)

ARCH_SOURCES := $(shell find ./arch/aarch64 -name "*.c" -print)
ARCH_ASM_SOURCES := $(shell find ./arch/aarch64 -name "*.s" -print)

# Define object files based on source files, potentially in a build directory
OBJECTS := $(patsubst %.c,build/kernel/%.o,$(SOURCES))
ASM_OBJECTS := $(patsubst %.s,build/kernel/%.o,$(ASM_SOURCES))

ARCH_OBJECTS := $(patsubst %.c,build/kernel/%.o,$(ARCH_SOURCES))
ARCH_ASM_OBJECTS := $(patsubst %.s,build/kernel/%.o,$(ARCH_ASM_SOURCES))

USER_SOURCES := $(shell find ./user -name "*.c" -print)
USER_ASM_SOURCES := $(shell find ./user -name "*.s" -print)

USER_ARCH_SOURCES := $(shell find ./user/arch -name "*.c" -print)
USER_ARCH_ASM_SOURCES := $(shell find ./user/arch -name "*.s" -print)


USER_OBJECTS := $(patsubst ./%.c,build/%.o,$(USER_SOURCES))
USER_ASM_OBJECTS := $(patsubst ./%.s,build/%.o,$(USER_ASM_SOURCES))

USER_ARCH_OBJECTS := $(patsubst ./%.c,build/%.o,$(USER_ARCH_SOURCES))
USER_ARCH_ASM_OBJECTS := $(patsubst ./%.s,build/%.o,$(USER_ARCH_ASM_SOURCES))

USER_PROGRAMS := $(patsubst ./user/%,%, $(shell find ./user -mindepth 1 -maxdepth 1 -type d -name "arch" -prune -o -maxdepth 1 -type d -print))

USER_BINARY_BUILD_PATH := $(patsubst ./%,build/%,$(USER_PROGRAMS))
USER_BINARIES := $(patsubst %,build/user/%.bin,$(USER_BINARY_BUILD_PATH))

.PRECIOUS: build/user/arch/%.o

all: $(BIN)

# Semihosting version (better for QEMU debugging)
elf: $(ELF)
kernel: user_binaries $(BIN)

setup:
	mkdir -p build

build/kernel/%.o: %.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/%.o: %.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

build/user/%.o: user/%.c
	mkdir -p `dirname $@`
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/user/%.o: user/%.s
	mkdir -p `dirname $@`
	$(AS) $(USER_ASFLAGS) $< -o $@

# Create binary image from ELF
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

# Link object files to create ELF
$(ELF): $(ASM_OBJECTS) $(OBJECTS) $(ARCH_ASM_OBJECTS) $(ARCH_OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

# Clean build artifacts
clean:
	rm -rf build

test:
	make test -C test

test_clean:
	make clean -C test

define make-user-prog
$(1)_SRCS := $$(wildcard user/$(1)/*.c)
$(1)_ASM_SRCS := $$(wildcard user/$(1)/*.s)
$(1)_OBJS := $$(patsubst user/%.c,build/user/%.o,$$($(1)_SRCS))
$(1)_ASM_OBJS := $$(patsubst user/%.s,build/user/%.o,$$($(1)_ASM_SRCS))

build/user/$(1).bin: $$($(1)_OBJS) $$($(1)_OBJS) $$(USER_ARCH_ASM_OBJECTS) $$(USER_ARCH_OBJECTS)
	$$(LD) $$(USER_LDFLAGS) -Wl,-e,main $$^ -o $$@
endef

$(foreach dir,$(USER_PROGRAMS),$(eval $(call make-user-prog,$(dir))))

user_binaries: $(USER_BINARIES)


qemu-virt: $(ELF)
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-smp cpus=4 -cpu max \
		-m 4G  \
		-serial stdio \
		-cpu cortex-a57 \
		-kernel $(ELF) \
		-nographic \
		-monitor telnet::4444,server,nowait

qemu-virt-1cpu: $(ELF)
	qemu-system-aarch64 \
		-M virt,gic-version=3 \
		-smp cpus=1 -cpu max \
		-m 4G  \
		-serial stdio \
		-cpu cortex-a57 \
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

.PHONY: all clean qemu test test_clean