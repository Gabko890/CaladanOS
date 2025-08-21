ASM            := nasm
LD             := x86_64-elf-ld
CC             := x86_64-elf-gcc
GRUBMKRESCUE   := grub-mkrescue

SRC_DIR        := src/x86_64
INC_DIR        := $(SRC_DIR)/include
BUILD_DIR      := build/x86_64
ISO_DIR        := build/x86_64/iso
DIST_DIR       := dist/x86_64
CONF_DIR       := config
LINKER_SCRIPT  := targets/x86_64.ld

ASM_SOURCES    := $(shell find $(SRC_DIR) -name '*.asm')
C_SOURCES      := $(shell find $(SRC_DIR) -name '*.c')

ASM_OBJECTS    := $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
C_OBJECTS      := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SOURCES))
OBJECTS        := $(ASM_OBJECTS) $(C_OBJECTS)

CFLAGS         := -ffreestanding -m64 -O2 -Wall -Wextra -nostdlib -I$(INC_DIR)

.PHONY: all clean build-x86_64

all: build-x86_64

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build-x86_64: $(OBJECTS)
	@mkdir -p $(DIST_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	$(LD) -n -o $(BUILD_DIR)/kernel/kernel.elf -T $(LINKER_SCRIPT) $(OBJECTS)
	cp $(BUILD_DIR)/kernel/kernel.elf $(ISO_DIR)/boot/kernel.elf
	cp $(CONF_DIR)/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUBMKRESCUE) -o $(DIST_DIR)/kernel.iso $(ISO_DIR)

clean:
	rm -rf $(BUILD_DIR)/*
	rm -rf $(DIST_DIR)/*
