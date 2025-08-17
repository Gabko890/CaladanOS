ASM            := nasm
LD             := x86_64-elf-ld
CC             := x86_64-elf-gcc
GRUBMKRESCUE   := grub-mkrescue

BOOT_SRC_DIR   := boot/src
BOOT_INC_DIR   := boot/include
KERNEL_SRC_DIR := kernel/src
KERNEL_INC_DIR := kernel/include
BUILD_DIR      := build
ISO_DIR        := $(BUILD_DIR)/iso
CONF_DIR       := config
LINKER_SCRIPT  := targets/x86_64.ld

BOOT_ASM_SOURCES    := $(shell find $(BOOT_SRC_DIR) -name '*.asm')
BOOT_C_SOURCES      := $(shell find $(BOOT_SRC_DIR) -name '*.c')
KERNEL_ASM_SOURCES  := $(shell find $(KERNEL_SRC_DIR) -name '*.asm')
KERNEL_C_SOURCES    := $(shell find $(KERNEL_SRC_DIR) -name '*.c')

BOOT_ASM_OBJECTS    := $(patsubst $(BOOT_SRC_DIR)/%.asm, $(BUILD_DIR)/boot/%.o, $(BOOT_ASM_SOURCES))
BOOT_C_OBJECTS      := $(patsubst $(BOOT_SRC_DIR)/%.c, $(BUILD_DIR)/boot/%.o, $(BOOT_C_SOURCES))
KERNEL_ASM_OBJECTS  := $(patsubst $(KERNEL_SRC_DIR)/%.asm, $(BUILD_DIR)/kernel/%.o, $(KERNEL_ASM_SOURCES))
KERNEL_C_OBJECTS    := $(patsubst $(KERNEL_SRC_DIR)/%.c, $(BUILD_DIR)/kernel/%.o, $(KERNEL_C_SOURCES))
OBJECTS        := $(BOOT_ASM_OBJECTS) $(BOOT_C_OBJECTS) $(KERNEL_ASM_OBJECTS) $(KERNEL_C_OBJECTS)

CFLAGS         := -ffreestanding -m64 -O2 -Wall -Wextra -nostdlib -I$(BOOT_INC_DIR) -I$(KERNEL_INC_DIR)

.PHONY: all clean build-x86_64

all: build-x86_64

$(BUILD_DIR)/boot/%.o: $(BOOT_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/boot/%.o: $(BOOT_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build-x86_64: $(OBJECTS)
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(ISO_DIR)/boot/grub
	$(LD) -n -o $(BUILD_DIR)/kernel/kernel.elf -T $(LINKER_SCRIPT) $(OBJECTS)
	cp $(BUILD_DIR)/kernel/kernel.elf $(ISO_DIR)/boot/kernel.elf
	cp $(CONF_DIR)/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUBMKRESCUE) -o $(BUILD_DIR)/kernel.iso $(ISO_DIR)

clean:
	rm -rf $(BUILD_DIR)/*
