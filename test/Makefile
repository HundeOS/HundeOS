# Cross-compilation tools
PLATFORM_INCLUDE = /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include

CROSS_COMPILE =
AS = gcc
CC = gcc
LD = gcc

UNITY = ../vendor/Unity
UV ?= uv
PYTHON = $(UV) run python
# Compiler flags
CFLAGS = -c -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -ffreestanding -mno-outline-atomics -include hundetest.h -I../user/lib -I../ -I$(UNITY)/src -I$(PLATFORM_INCLUDE)

# Assembler flags
ASFLAGS = -c -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -nostdlib -latomic

# Linker flags
LDFLAGS =

# Output files
BIN = ./build/tests.bin

SOURCES := $(shell find ./kernel -name "*.c" -print)
ASM_SOURCES := $(shell find ./kernel -name "*.s" -print)

KERNEL_SOURCES := $(shell find ../kernel/bin -name "*.c" -print)
KERNEL_ASM_SOURCES := $(shell find ../kernel/bin -name "*.s" -print)

# Define object files based on source files, potentially in a build directory
OBJECTS := $(patsubst ./%.c,build/test/%.o,$(SOURCES))
ASM_OBJECTS := $(patsubst ./%.s,build/test/%.o,$(ASM_SOURCES))

KERNEL_OBJECTS := $(patsubst ../kernel/%.c,build/kernel/%.o,$(KERNEL_SOURCES))
#KERNEL_ASM_OBJECTS := $(patsubst ../kernel/%.s,build/kernel/%.o,$(KERNEL_ASM_SOURCES))

all: $(BIN)

# Semihosting version (better for QEMU debugging)
setup:
	mkdir -p build

test:
	$(PYTHON) run_tests.py

clean:
	rm -rf build

.PHONY: all test clean
