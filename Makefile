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

# Platform detection
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
    SHELL_CMD := powershell.exe -Command
    NULL_DEV := nul
    DOCKER_CHECK := docker version >$(NULL_DEV) 2>&1
    DOCKER_CMD := docker
    DOCKER_BUILD := docker build -t cld-kernel-env -f Dockerfile .
    DOCKER_RUN := docker run -it --rm -v "%cd%":/root/env cld-kernel-env make build-x86_64
    DOCKER_IMAGES := docker images -q cld-kernel-env
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        PLATFORM := linux
        SHELL_CMD := sh -c
        NULL_DEV := /dev/null
        DOCKER_CHECK := docker version >$(NULL_DEV) 2>&1
        DOCKER_CMD := sudo docker
        DOCKER_BUILD := sudo docker build -t cld-kernel-env -f Dockerfile .
        DOCKER_RUN := sudo docker run -it --rm -v "$$PWD":/root/env cld-kernel-env make build-x86_64
        DOCKER_IMAGES := sudo docker images -q cld-kernel-env 2>$(NULL_DEV)
    else
        $(error Unsupported platform: $(UNAME_S))
    endif
endif

# Dependency checking functions
define check_command
$(shell command -v $(1) >$(NULL_DEV) 2>&1 && echo "found" || echo "missing")
endef

define check_docker
$(shell command -v docker >$(NULL_DEV) 2>&1 && echo "found" || echo "missing")
endef

# Check if Docker image exists
define check_docker_image
$(shell $(DOCKER_IMAGES) | head -n 1)
endef

.PHONY: all clean build-x86_64 build qemu build-docker check-deps smart-build

all: smart-build

# Smart build target - chooses native or Docker based on available tools
smart-build:
	@echo "Checking dependencies for $(PLATFORM) platform..."
ifeq ($(PLATFORM),windows)
	@echo "Windows platform detected. Cross-compilation tools not typically available."
	@DOCKER_CHECK="$(call check_docker)"; \
	if [ "$$DOCKER_CHECK" = "found" ]; then \
		echo "Docker is available. Building using Docker container..."; \
		$(MAKE) build; \
	else \
		echo "ERROR: Docker is required for building on Windows."; \
		echo "Please install Docker Desktop to proceed."; \
		exit 1; \
	fi
else
	@ASM_CHECK="$(call check_command,$(ASM))"; \
	CC_CHECK="$(call check_command,$(CC))"; \
	LD_CHECK="$(call check_command,$(LD))"; \
	CPIO_CHECK="$(call check_command,cpio)"; \
	GRUB_CHECK="$(call check_command,$(GRUBMKRESCUE))"; \
	DOCKER_CHECK="$(call check_docker)"; \
	MISSING=""; \
	if [ "$$ASM_CHECK" = "missing" ]; then MISSING="$$MISSING $(ASM)"; fi; \
	if [ "$$CC_CHECK" = "missing" ]; then MISSING="$$MISSING $(CC)"; fi; \
	if [ "$$LD_CHECK" = "missing" ]; then MISSING="$$MISSING $(LD)"; fi; \
	if [ "$$CPIO_CHECK" = "missing" ]; then MISSING="$$MISSING cpio"; fi; \
	if [ "$$GRUB_CHECK" = "missing" ]; then MISSING="$$MISSING $(GRUBMKRESCUE)"; fi; \
	if [ -n "$$MISSING" ]; then \
		echo "Missing build tools:$$MISSING"; \
		if [ "$$DOCKER_CHECK" = "found" ]; then \
			echo "Docker is available. Building using Docker container..."; \
			$(MAKE) build; \
		else \
			echo "ERROR: Neither build tools nor Docker are available."; \
			echo "Please install the missing tools or Docker to proceed."; \
			exit 1; \
		fi; \
	else \
		echo "All build tools found. Building natively..."; \
		$(MAKE) build-x86_64; \
	fi
endif

# Dependency checking target
check-deps:
	@echo "Checking dependencies for $(PLATFORM) platform..."
	@ASM_CHECK="$(call check_command,$(ASM))"; \
	CC_CHECK="$(call check_command,$(CC))"; \
	LD_CHECK="$(call check_command,$(LD))"; \
	CPIO_CHECK="$(call check_command,cpio)"; \
	GRUB_CHECK="$(call check_command,$(GRUBMKRESCUE))"; \
	DOCKER_CHECK="$(call check_docker)"; \
	MISSING=""; \
	if [ "$$ASM_CHECK" = "missing" ]; then MISSING="$$MISSING $(ASM)"; fi; \
	if [ "$$CC_CHECK" = "missing" ]; then MISSING="$$MISSING $(CC)"; fi; \
	if [ "$$LD_CHECK" = "missing" ]; then MISSING="$$MISSING $(LD)"; fi; \
	if [ "$$CPIO_CHECK" = "missing" ]; then MISSING="$$MISSING cpio"; fi; \
	if [ "$$GRUB_CHECK" = "missing" ]; then MISSING="$$MISSING $(GRUBMKRESCUE)"; fi; \
	if [ -n "$$MISSING" ]; then \
		echo "Missing build tools:$$MISSING"; \
		if [ "$$DOCKER_CHECK" = "found" ]; then \
			echo "Docker is available. Will build using Docker container."; \
			exit 0; \
		else \
			echo "ERROR: Neither build tools nor Docker are available."; \
			echo "Please install the missing tools or Docker to proceed."; \
			exit 1; \
		fi; \
	else \
		echo "All build tools found. Building natively."; \
	fi


# Build rules for ASM and C sources
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

$(BUILD_DIR)/utils/%.o: $(UTILS_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/utils/%.o: $(UTILS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Main build target
build-x86_64: $(OBJECTS)
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(ISO_DIR)/boot/grub
	$(LD) -n -o $(BUILD_DIR)/kernel/kernel.elf -T $(LINKER_SCRIPT) $(OBJECTS)
	cp $(BUILD_DIR)/kernel/kernel.elf $(ISO_DIR)/boot/kernel.elf

	# Create cpio ramfs at root of archive
	@mkdir -p $(ISO_DIR)/boot
	@(cd $(RAMFS_DIR) && find . | cpio -H newc -o > ../$(ISO_DIR)/boot/ramfs.cpio)
	@echo "RAMFS contents:"
	@cpio -itv < $(ISO_DIR)/boot/ramfs.cpio

	cp $(CONF_DIR)/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUBMKRESCUE) -o $(BUILD_DIR)/kernel.iso $(ISO_DIR)

# Build using Docker (from build.sh)
build:
	@echo "Building kernel using Docker..."
	@if [ -z "$$($(call check_docker_image))" ]; then \
		echo "Docker image 'cld-kernel-env' not found. Building it..."; \
		$(MAKE) build-docker; \
	fi
	@$(DOCKER_RUN)

# Build Docker image (from docker/build_docker.sh)
build-docker:
	@echo "Building Docker image..."
	@if [ "$$($(call check_docker))" = "missing" ]; then \
		echo "ERROR: Docker is not available. Cannot build Docker image."; \
		exit 1; \
	fi
ifeq ($(PLATFORM),windows)
	@echo "Building Docker image on Windows..."
else
	@echo "Note: This may require sudo password for Docker access."
endif
	$(DOCKER_BUILD)

# Run kernel in QEMU (from run_qemu.sh)
qemu:
	@echo "Starting QEMU..."
ifeq ($(QEMU_ISA_DEBUGCON), true)
	sudo qemu-system-x86_64 -m 4G -cdrom build/kernel.iso -device isa-debugcon,chardev=dbg_console -chardev stdio,id=dbg_console
else
	sudo qemu-system-x86_64 -m 4G -cdrom build/kernel.iso
endif

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)/*

