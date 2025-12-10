PREFIX ?= $(HOME)/opt/cross
TARGET ?= i686-elf

PATH := $(PREFIX)/bin:$(PATH)
export PATH

AS      := nasm
CC      := $(TARGET)-gcc
LD      := $(TARGET)-ld
OBJCOPY := $(TARGET)-objcopy

INCLUDE_DIR := src/include

CFLAGS  := -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -nostdinc -m32 -fno-pie -O2 -Wall -Wextra -std=gnu99 -I$(INCLUDE_DIR)
LDFLAGS := -nostdlib -n
NASMFLAGS := -F dwarf -g

BUILD_DIR := build
BIN_DIR   := bin

KERNEL_ELF := $(BIN_DIR)/kernel.elf
KERNEL_BIN := $(BIN_DIR)/kernel.bin
BOOT_BIN   := $(BIN_DIR)/boot.bin
OS_IMAGE   := $(BIN_DIR)/os.bin
SECTOR_DEF := $(BUILD_DIR)/kernel_sectors.inc

C_SOURCES := \
	src/kernel.c \
	src/lib/string.c \
	src/lib/tty.c

C_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJS := $(BUILD_DIR)/kernel_entry.o
OBJS := $(ASM_OBJS) $(C_OBJS)

.PHONY: all clean run qemu

all: $(OS_IMAGE)

run: all
	qemu-system-x86_64 -drive format=raw,file=$(OS_IMAGE)

qemu: run

$(OS_IMAGE): $(BOOT_BIN) $(KERNEL_BIN) | $(BIN_DIR)
	cat $(BOOT_BIN) $(KERNEL_BIN) > $(OS_IMAGE)
	python3 tools/pad_image.py $(OS_IMAGE)

$(BOOT_BIN): src/boot/boot.asm $(SECTOR_DEF) | $(BIN_DIR)
	$(AS) -f bin -I $(BUILD_DIR)/ $< -o $@

$(SECTOR_DEF): $(KERNEL_BIN) | $(BUILD_DIR)
	python3 -c "import os, sys; size=os.path.getsize(sys.argv[1]); sectors=max(1, (size + 511)//512); print(f'%define KERNEL_SECTORS {sectors}')" $(KERNEL_BIN) > $(SECTOR_DEF)

$(KERNEL_BIN): $(KERNEL_ELF) | $(BIN_DIR)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $(KERNEL_BIN)

$(KERNEL_ELF): $(OBJS) src/linker.ld | $(BIN_DIR)
	$(LD) -T src/linker.ld $(LDFLAGS) -o $(KERNEL_ELF) $(OBJS)

$(BUILD_DIR)/kernel_entry.o: src/kernel.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(AS) -f elf32 $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR) $(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)