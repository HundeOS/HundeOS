# Cross-compilation tools
CROSS_COMPILE = aarch64-elf-
AS = $(CROSS_COMPILE)as
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy

# Compiler flags
CFLAGS = -nostdlib -nostartfiles -ffreestanding -O2 -Wall -Wextra -DBSP_RPI3

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
	find . -name '*.o' -exec rm -r {} \;
	rm *.img *.elf

# Run semihosting version in QEMU (RECOMMENDED)
qemu: $(BIN)
	qemu-system-aarch64 \
		-M raspi3b \
		-kernel $(BIN) \
		-serial stdio \
		-monitor none
# 	qemu-system-aarch64 -M raspi4b -kernel $(BIN) -serial stdio -d mmu,guest_errors,unimp -D qemu.log
.PHONY: all clean qemu