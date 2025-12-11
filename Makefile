PREFIX ?= $(HOME)/opt/cross
TARGET ?= i686-elf
ARCH   ?= x86

PATH := $(PREFIX)/bin:$(PATH)
export PATH

AS      := nasm
CC      := $(TARGET)-gcc
LD      := $(TARGET)-ld
OBJCOPY := $(TARGET)-objcopy

INCLUDE_DIRS := src/include

CFLAGS  := -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -nostdinc -m32 -fno-pie -O2 -Wall -Wextra -std=gnu99 $(addprefix -I,$(INCLUDE_DIRS))
LDFLAGS := -nostdlib -n
NASMFLAGS := -F dwarf -g

BUILD_DIR := build
BIN_DIR   := bin
ARCH_DIR  := src/arch/$(ARCH)

BOOT_SRC        := $(ARCH_DIR)/boot/boot.asm
KERNEL_ENTRY_SRC:= $(ARCH_DIR)/kernel/entry.asm
KERNEL_IDT_SRC  := $(ARCH_DIR)/kernel/idt.asm
KERNEL_PROCESS_SRC := $(ARCH_DIR)/kernel/process.asm
LINKER_SCRIPT   := $(ARCH_DIR)/linker.ld

KERNEL_ELF := $(BIN_DIR)/kernel.elf
KERNEL_BIN := $(BIN_DIR)/kernel.bin
BOOT_BIN   := $(BIN_DIR)/boot.bin
OS_IMAGE   := $(BIN_DIR)/os.bin
SECTOR_DEF := $(BUILD_DIR)/kernel_sectors.inc

C_SOURCES := $(shell find src/kernel -name '*.c')

C_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJS := $(BUILD_DIR)/arch/$(ARCH)/kernel/entry.o $(BUILD_DIR)/arch/$(ARCH)/kernel/idt.o $(BUILD_DIR)/arch/$(ARCH)/kernel/process.o
OBJS := $(ASM_OBJS) $(C_OBJS)

.PHONY: all clean run qemu

all: $(OS_IMAGE)

run: all
	qemu-system-x86_64 -drive format=raw,file=$(OS_IMAGE)

qemu: run

$(OS_IMAGE): $(BOOT_BIN) $(KERNEL_BIN) | $(BIN_DIR)
	cat $(BOOT_BIN) $(KERNEL_BIN) > $(OS_IMAGE)
	python3 tools/pad_image.py $(OS_IMAGE)

$(BOOT_BIN): $(BOOT_SRC) $(SECTOR_DEF) | $(BIN_DIR)
	$(AS) -f bin -I $(BUILD_DIR)/ $(BOOT_SRC) -o $@

$(SECTOR_DEF): $(KERNEL_BIN) | $(BUILD_DIR)
	python3 -c "import os, sys; size=os.path.getsize(sys.argv[1]); sectors=max(1, (size + 511)//512); print(f'%define KERNEL_SECTORS {sectors}')" $(KERNEL_BIN) > $(SECTOR_DEF)

$(KERNEL_BIN): $(KERNEL_ELF) | $(BIN_DIR)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $(KERNEL_BIN)

$(KERNEL_ELF): $(OBJS) $(LINKER_SCRIPT) | $(BIN_DIR)
	$(LD) -T $(LINKER_SCRIPT) $(LDFLAGS) -o $(KERNEL_ELF) $(OBJS)

$(BUILD_DIR)/arch/$(ARCH)/kernel/entry.o: $(KERNEL_ENTRY_SRC) | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(AS) -f elf32 $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/arch/$(ARCH)/kernel/idt.o: $(KERNEL_IDT_SRC) | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(AS) -f elf32 $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/arch/$(ARCH)/kernel/process.o: $(KERNEL_PROCESS_SRC) | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(AS) -f elf32 $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR) $(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)