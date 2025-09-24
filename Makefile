# Cross-compilation tools
CROSS_COMPILE = aarch64-elf-
AS = $(CROSS_COMPILE)as
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy

# Compiler flags
CFLAGS = -nostdlib -std=c11 -nostartfiles -ffreestanding -latomic -mno-outline-atomics -O2 -Wall -Wextra -DBSP_VIRT

# Assembler flags
ASFLAGS = 

# Linker flags
LDFLAGS = -T linker.ld 

# Output files
ELF = build/kernel.elf
BIN = build/kernel.img

SOURCES := $(shell find . -name "*.c" -print)
ASM_SOURCES := $(shell find . -name "*.s" -print)

# Define object files based on source files, potentially in a build directory
OBJECTS := $(patsubst %.c,build/%.o,$(SOURCES))
ASM_OBJECTS := $(patsubst %.s,build/%.o,$(ASM_SOURCES))

# Default target
all: $(BIN)

# Semihosting version (better for QEMU debugging)
kernel: $(BIN)

setup:
	mkdir -p build

build/%.o: %.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

# Create binary image from ELF
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

# Link object files to create ELF
$(ELF): $(ASM_OBJECTS) $(OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

# Clean build artifacts
clean:
	rm -rf build

qemu-virt: $(BIN)
	qemu-system-aarch64 \
		-M virt \
		-smp cpus=8 -cpu max -m 4G \
		-serial stdio \
		-cpu cortex-a57 \
		-kernel $(BIN) \
		-nographic \
		-monitor telnet::4444,server,nowait

.PHONY: all clean qemu