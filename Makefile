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

COLOR_GREEN    := \033[32m
COLOR_YELLOW   := \033[93m
COLOR_RESET    := \033[0m

ASM            := nasm
LD             := x86_64-elf-ld
CC             := x86_64-elf-gcc
GRUBMKRESCUE   := grub-mkrescue

TARGET         := CaladanOS.iso

QEMU_ISA_DEBUGCON := true

BOOT_SRC_DIR   := boot/src
BOOT_INC_DIR   := boot/include
KERNEL_SRC_DIR := kernel/src
KERNEL_INC_DIR := kernel/include
UTILS_DIR      := utils
DRIVERS_SRC_DIR := drivers
DRIVERS_INC_DIR := drivers
TESTS_SRC_DIR  := tests
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
TESTS_ASM_SOURCES   := $(shell find $(TESTS_SRC_DIR) -name '*.asm')
TESTS_C_SOURCES     := $(shell find $(TESTS_SRC_DIR) -name '*.c')

BOOT_ASM_OBJECTS    := $(patsubst $(BOOT_SRC_DIR)/%.asm, $(BUILD_DIR)/boot/%.o, $(BOOT_ASM_SOURCES))
BOOT_C_OBJECTS      := $(patsubst $(BOOT_SRC_DIR)/%.c, $(BUILD_DIR)/boot/%.o, $(BOOT_C_SOURCES))
KERNEL_ASM_OBJECTS  := $(patsubst $(KERNEL_SRC_DIR)/%.asm, $(BUILD_DIR)/kernel/%.o, $(KERNEL_ASM_SOURCES))
KERNEL_C_OBJECTS    := $(patsubst $(KERNEL_SRC_DIR)/%.c, $(BUILD_DIR)/kernel/%.o, $(KERNEL_C_SOURCES))
UTILS_ASM_OBJECTS   := $(patsubst $(UTILS_DIR)/%.asm, $(BUILD_DIR)/utils/%.o, $(UTILS_ASM_SOURCES))
UTILS_C_OBJECTS     := $(patsubst $(UTILS_DIR)/%.c, $(BUILD_DIR)/utils/%.o, $(UTILS_C_SOURCES))
DRIVERS_ASM_OBJECTS := $(patsubst $(DRIVERS_SRC_DIR)/%.asm, $(BUILD_DIR)/drivers/%.o, $(DRIVERS_ASM_SOURCES))
DRIVERS_C_OBJECTS   := $(patsubst $(DRIVERS_SRC_DIR)/%.c, $(BUILD_DIR)/drivers/%.o, $(DRIVERS_C_SOURCES))
TESTS_ASM_OBJECTS   := $(patsubst $(TESTS_SRC_DIR)/%.asm, $(BUILD_DIR)/tests/%.o, $(TESTS_ASM_SOURCES))
TESTS_C_OBJECTS     := $(patsubst $(TESTS_SRC_DIR)/%.c, $(BUILD_DIR)/tests/%.o, $(TESTS_C_SOURCES))
# Core objects (without tests)
CORE_OBJECTS   := $(BOOT_ASM_OBJECTS) $(BOOT_C_OBJECTS) $(KERNEL_ASM_OBJECTS) $(KERNEL_C_OBJECTS) $(UTILS_ASM_OBJECTS) $(UTILS_C_OBJECTS) $(DRIVERS_ASM_OBJECTS) $(DRIVERS_C_OBJECTS)

# All objects including tests
ALL_OBJECTS    := $(CORE_OBJECTS) $(TESTS_ASM_OBJECTS) $(TESTS_C_OBJECTS)

# Default objects (no tests)
OBJECTS        := $(CORE_OBJECTS)

UTILS_SUBDIRS  := $(shell find $(UTILS_DIR) -type d)
UTILS_INCLUDES := $(addprefix -I, $(UTILS_SUBDIRS))

# Base CFLAGS
CFLAGS         := -ffreestanding -m64 -mcmodel=kernel -O2 -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -nostdlib \
                  -I$(BOOT_INC_DIR) -I$(KERNEL_INC_DIR) $(UTILS_INCLUDES) \
                  -I$(DRIVERS_INC_DIR) -Idrivers/ps2 -Idrivers/pic

# CFLAGS with test support
CFLAGS_WITH_TESTS := $(CFLAGS) -DCLDTEST_ENABLED

ifeq ($(QEMU_ISA_DEBUGCON), true)
    CFLAGS += -DQEMU_ISA_DEBUGCON
		CFLAGS_WITH_TESTS += -DQEMU_ISA_DEBUGCON
endif


.PHONY: all clean build-x86_64 build-x86_64-test build-x86_64-internal test mmap build qemu build-docker

all: build-docker build


$(BUILD_DIR)/boot/%.o: $(BOOT_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/boot/%.o: $(BOOT_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
ifdef ENABLE_TESTS
	@$(CC) $(CFLAGS_WITH_TESTS) -c $< -o $@
else
	@$(CC) $(CFLAGS) -c $< -o $@
endif

$(BUILD_DIR)/utils/%.o: $(UTILS_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/utils/%.o: $(UTILS_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
ifdef ENABLE_TESTS
	@$(CC) $(CFLAGS_WITH_TESTS) -c $< -o $@
else
	@$(CC) $(CFLAGS) -c $< -o $@
endif

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
ifdef ENABLE_TESTS
	@$(CC) $(CFLAGS_WITH_TESTS) -c $< -o $@
else
	@$(CC) $(CFLAGS) -c $< -o $@
endif

$(BUILD_DIR)/tests/%.o: $(TESTS_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/tests/%.o: $(TESTS_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
ifdef ENABLE_TESTS
	@$(CC) $(CFLAGS_WITH_TESTS) -c $< -o $@
else
	@$(CC) $(CFLAGS) -c $< -o $@
endif


build-x86_64:
	@$(MAKE) build-x86_64-internal

build-x86_64-test:
	@$(MAKE) ENABLE_TESTS=1 OBJECTS="$(ALL_OBJECTS)" build-x86_64-internal

build-x86_64-internal: $(OBJECTS)
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(ISO_DIR)/boot/grub
	@echo "$(COLOR_YELLOW)Linking objects:$(COLOR_RESET) $(OBJECTS)"
	@$(LD) -n -o $(BUILD_DIR)/kernel/kernel.elf -T $(LINKER_SCRIPT) $(OBJECTS)
	@cp $(BUILD_DIR)/kernel/kernel.elf $(ISO_DIR)/boot/kernel.elf

	# Create cpio ramfs at root of archive
	@mkdir -p $(ISO_DIR)/boot
	@echo "$(COLOR_YELLOW)Building$(COLOR_RESET) ramfs archive from: $(RAMFS_DIR)"
	@(cd $(RAMFS_DIR) && find . | cpio -H newc -o > ../$(ISO_DIR)/boot/ramfs.cpio)
	@cpio -itv < $(ISO_DIR)/boot/ramfs.cpio

	@cp $(CONF_DIR)/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	@echo "$(COLOR_YELLOW)Building$(COLOR_RESET) ISO from: $(ISO_DIR)"
	@$(GRUBMKRESCUE) -o $(BUILD_DIR)/$(TARGET) $(ISO_DIR)
	
ifdef ENABLE_TESTS
	@echo "$(COLOR_GREEN)Build with tests successful.$(COLOR_RESET)\nISO created in: $(BUILD_DIR)/$(TARGET)"
else
	@echo "$(COLOR_GREEN)Build successful.$(COLOR_RESET)\nISO created in: $(BUILD_DIR)/$(TARGET)"
endif
	@echo "You can run ISO in QEMU by executing: $(COLOR_YELLOW)make qemu$(COLOR_RESET)"

build:
	@echo "$(COLOR_YELLOW)Building$(COLOR_RESET) kernel using Docker..."
	@docker run --rm -v "$$PWD":/root/env cld-kernel-env make build-x86_64

test:
	@echo "$(COLOR_YELLOW)Building$(COLOR_RESET) kernel with tests using Docker..."
	@docker run --rm -v "$$PWD":/root/env cld-kernel-env make build-x86_64-test


build-docker:
	@echo "$(COLOR_YELLOW)Building$(COLOR_RESET) Docker image..."
	@docker build -t cld-kernel-env -f Dockerfile .


qemu:
	@echo "$(COLOR_GREEN)Starting$(COLOR_RESET) QEMU..."
ifeq ($(QEMU_ISA_DEBUGCON), true)
	@qemu-system-x86_64 -m 4G -cdrom $(BUILD_DIR)/$(TARGET) -device isa-debugcon,chardev=dbg_console -chardev stdio,id=dbg_console
else
	@qemu-system-x86_64 -m 4G -cdrom $(BUILD_DIR)/$(TARGET)
endif


clean:
	rm -rf $(BUILD_DIR)/*

endif
