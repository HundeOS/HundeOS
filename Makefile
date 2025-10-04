# Cross-compilation tools
CROSS_COMPILE = aarch64-elf-
#AS = $(CROSS_COMPILE)as
AS = $(CROSS_COMPILE)gcc
CC = $(CROSS_COMPILE)gcc
#LD = $(CROSS_COMPILE)ld
LD = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

# Compiler flags
CFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra \
           -nostdlib -nostartfiles -mcmodel=large -DBSP_VIRT \
           -march=armv8-a -mno-outline-atomics


# Assembler flags
ASFLAGS = -c -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra \
            -nostdlib -nostartfiles -mcmodel=small -latomic

# Linker flags
LDFLAGS = -nostdlib -T linker.ld

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
elf: $(ELF)
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

.PHONY: all clean qemu