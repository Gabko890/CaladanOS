ifeq ($(OS), Windows_NT)

DOCKER_RUN := docker run -it --rm -v "$(CURDIR):/root/env" cld-kernel-env

.PHONY: all rundocker

all: rundocker

rundocker:
	@echo "Building Docker..."
	@docker build -t cld-kernel-env -f Dockerfile .

	@echo "Running Docker..."
	@$(DOCKER_RUN) make build-x86_64
		
else

ASM            := nasm
LD             := x86_64-elf-ld
CC             := x86_64-elf-gcc
GRUBMKRESCUE   := grub-mkrescue

# Configuration
QEMU_ISA_DEBUGCON := true

BOOT_SRC_DIR   := boot/src
BOOT_INC_DIR   := boot/include
KERNEL_SRC_DIR := kernel/src
KERNEL_INC_DIR := kernel/include
UTILS_DIR      := utils
DRIVERS_SRC_DIR := drivers
DRIVERS_INC_DIR := drivers
BUILD_DIR      := build
ISO_DIR        := $(BUILD_DIR)/iso
CONF_DIR       := config
LINKER_SCRIPT  := targets/x86_64.ld
RAMFS_DIR      := ramfs

BOOT_ASM_SOURCES    := $(shell find $(BOOT_SRC_DIR) -name '*.asm')
BOOT_C_SOURCES      := $(shell find $(BOOT_SRC_DIR) -name '*.c')
KERNEL_ASM_SOURCES  := $(shell find $(KERNEL_SRC_DIR) -name '*.asm')
KERNEL_C_SOURCES    := $(shell find $(KERNEL_SRC_DIR) -name '*.c')
UTILS_ASM_SOURCES   := $(shell find $(UTILS_DIR) -name '*.asm')
UTILS_C_SOURCES     := $(shell find $(UTILS_DIR) -name '*.c')
DRIVERS_ASM_SOURCES := $(shell find $(DRIVERS_SRC_DIR) -name '*.asm')
DRIVERS_C_SOURCES   := $(shell find $(DRIVERS_SRC_DIR) -name '*.c')

BOOT_ASM_OBJECTS    := $(patsubst $(BOOT_SRC_DIR)/%.asm, $(BUILD_DIR)/boot/%.o, $(BOOT_ASM_SOURCES))
BOOT_C_OBJECTS      := $(patsubst $(BOOT_SRC_DIR)/%.c, $(BUILD_DIR)/boot/%.o, $(BOOT_C_SOURCES))
KERNEL_ASM_OBJECTS  := $(patsubst $(KERNEL_SRC_DIR)/%.asm, $(BUILD_DIR)/kernel/%.o, $(KERNEL_ASM_SOURCES))
KERNEL_C_OBJECTS    := $(patsubst $(KERNEL_SRC_DIR)/%.c, $(BUILD_DIR)/kernel/%.o, $(KERNEL_C_SOURCES))
UTILS_ASM_OBJECTS   := $(patsubst $(UTILS_DIR)/%.asm, $(BUILD_DIR)/utils/%.o, $(UTILS_ASM_SOURCES))
UTILS_C_OBJECTS     := $(patsubst $(UTILS_DIR)/%.c, $(BUILD_DIR)/utils/%.o, $(UTILS_C_SOURCES))
DRIVERS_ASM_OBJECTS := $(patsubst $(DRIVERS_SRC_DIR)/%.asm, $(BUILD_DIR)/drivers/%.o, $(DRIVERS_ASM_SOURCES))
DRIVERS_C_OBJECTS   := $(patsubst $(DRIVERS_SRC_DIR)/%.c, $(BUILD_DIR)/drivers/%.o, $(DRIVERS_C_SOURCES))
OBJECTS        := $(BOOT_ASM_OBJECTS) $(BOOT_C_OBJECTS) $(KERNEL_ASM_OBJECTS) $(KERNEL_C_OBJECTS) $(UTILS_ASM_OBJECTS) $(UTILS_C_OBJECTS) $(DRIVERS_ASM_OBJECTS) $(DRIVERS_C_OBJECTS)

UTILS_SUBDIRS  := $(shell find $(UTILS_DIR) -type d)
UTILS_INCLUDES := $(addprefix -I, $(UTILS_SUBDIRS))

CFLAGS         := -ffreestanding -m64 -mcmodel=kernel -O2 -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -nostdlib \
                  -I$(BOOT_INC_DIR) -I$(KERNEL_INC_DIR) $(UTILS_INCLUDES) \
                  -I$(DRIVERS_INC_DIR) -Idrivers/ps2 -Idrivers/pic

ifeq ($(QEMU_ISA_DEBUGCON), true)
    CFLAGS += -DQEMU_ISA_DEBUGCON
endif

.PHONY: all clean build-x86_64 mmap build qemu build-docker

all: build-docker build

# Build rules for ASM and C sources
$(BUILD_DIR)/boot/%.o: $(BOOT_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "\033[32mCompiling:\033[0m $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/boot/%.o: $(BOOT_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "\033[32mCompiling:\033[0m $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "\033[32mCompiling:\033[0m $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "\033[32mCompiling:\033[0m $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/utils/%.o: $(UTILS_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "\033[32mCompiling:\033[0m $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/utils/%.o: $(UTILS_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "\033[32mCompiling:\033[0m $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "\033[32mCompiling:\033[0m $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "\033[32mCompiling:\033[0m $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Main build target
build-x86_64: $(OBJECTS)
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(ISO_DIR)/boot/grub
	@echo "\033[33mLinking objects: \033[0m $(OBJECTS)"
	@$(LD) -n -o $(BUILD_DIR)/kernel/kernel.elf -T $(LINKER_SCRIPT) $(OBJECTS)
	@cp $(BUILD_DIR)/kernel/kernel.elf $(ISO_DIR)/boot/kernel.elf

	# Create cpio ramfs at root of archive
	@mkdir -p $(ISO_DIR)/boot
	@echo "\033[33mBuilding\033[0m ramfs archive from: $(RAMFS_DIR)"
	@(cd $(RAMFS_DIR) && find . | cpio -H newc -o > ../$(ISO_DIR)/boot/ramfs.cpio)
	@cpio -itv < $(ISO_DIR)/boot/ramfs.cpio

	@cp $(CONF_DIR)/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	@echo "\033[33mBuilding\033[0m ISO from: $(ISO_DIR)"
	@$(GRUBMKRESCUE) -o $(BUILD_DIR)/kernel.iso $(ISO_DIR)

# Build using Docker (from build.sh)
build:
	@echo "\033[33mBuilding\033[0m kernel using Docker..."
	@docker run -it --rm -v "$$PWD":/root/env cld-kernel-env make build-x86_64

# Build Docker image (from docker/build_docker.sh)
build-docker:
	@echo "\033[33mBuilding\033[0m Docker image..."
	@docker build -t cld-kernel-env -f Dockerfile .

# Run kernel in QEMU (from run_qemu.sh)
qemu:
	@echo "\033[32mStarting\033[0m QEMU..."
ifeq ($(QEMU_ISA_DEBUGCON), true)
	@qemu-system-x86_64 -m 4G -cdrom build/kernel.iso -device isa-debugcon,chardev=dbg_console -chardev stdio,id=dbg_console
else
	@qemu-system-x86_64 -m 4G -cdrom build/kernel.iso
endif

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)/*

endif
