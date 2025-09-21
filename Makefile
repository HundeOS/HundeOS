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

# Source files
BOOT_SRC = boot.s
C_SIMP_SRC = main_simple.c

# Object files
BOOT_OBJ = boot.o
C_SIMP_OBJ = main_simphost.o

# Output files
SIMP_ELF = kernel_simphost.elf
SIMP_BIN = kernel_simphost.img

SOURCES := $(shell find . -name "*.c" -print)
ASM_SOURCES := $(shell find . -name "*.s" -print)

# Define object files based on source files, potentially in a build directory
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))
ASM_OBJECTS := $(patsubst %.s,%.o,$(ASM_SOURCES))

# Default target
all: $(SIMP_BIN)

# Semihosting version (better for QEMU debugging)
simple: $(SIMP_BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(AS) $(ASFLAGS) $< -o $@

# Create binary image from ELF
$(SIMP_BIN): $(SIMP_ELF)
	$(OBJCOPY) -O binary $< $@


# Link object files to create ELF
$(SIMP_ELF): $(ASM_OBJECTS) $(OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

# Clean build artifacts
clean:
	find . -name '*.o' -exec rm -r {} \;
	rm *.img *.elf

# Run semihosting version in QEMU (RECOMMENDED)
qemu-simple: $(SIMP_BIN)
	qemu-system-aarch64 \
		-M raspi3b \
		-kernel $(SIMP_BIN) \
		-serial stdio \
		-monitor none
# 	qemu-system-aarch64 -M raspi4b -kernel $(SIMP_BIN) -serial stdio -d mmu,guest_errors,unimp -D qemu.log
.PHONY: all clean qemu-simple debug list-machines