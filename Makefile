ifeq ($(OS),Windows_NT)

.DEFAULT_GOAL := all

DOCKER_RUN := docker run -it --rm -v "$(CURDIR):/root/env" cld-kernel-env

.PHONY: all rundocker build test clean

define RUN_WINDOWS_TARGET
	@echo Building $(if $(filter build-x86_64-test,$(1)),kernel with tests,kernel using Docker)...
	@$(DOCKER_RUN) make $(1)
endef

all: rundocker

rundocker:
	@echo Building Docker...
	@docker build -t cld-kernel-env -f Dockerfile .
	@echo Running Docker...
	@$(DOCKER_RUN) make build-x86_64

build:
	$(call RUN_WINDOWS_TARGET,build-x86_64)

test:
	$(call RUN_WINDOWS_TARGET,build-x86_64-test)

clean:
	rm -rf build/*

else

.DEFAULT_GOAL := all

COLOR_GREEN   := \033[32m
COLOR_YELLOW  := \033[93m
COLOR_RESET   := \033[0m

ASM           := nasm
LD            := x86_64-elf-ld
CC            := x86_64-elf-gcc
GRUBMKRESCUE  := grub-mkrescue

TARGET            := CaladanOS.iso
QEMU_ISA_DEBUGCON := true

BUILD_DIR         := build
ISO_DIR           := $(BUILD_DIR)/iso
SYSINFO_HEADER    := $(BUILD_DIR)/generated/sysinfo_values.h
CONF_DIR          := config
LINKER_SCRIPT     := targets/x86_64.ld
RAMFS_DIR         := ramfs
RAMFS_BIN_DIR     := $(RAMFS_DIR)/bin
RAMFS_CPIO        := $(ISO_DIR)/boot/ramfs.cpio
GRUB_CFG_DST      := $(ISO_DIR)/boot/grub/grub.cfg
SYSINFO_SCRIPT    := scripts/generate_sysinfo_header.sh

find_files        = $(sort $(shell find $(1) -type f \( $(2) \) | sort))
find_c_sources    = $(call find_files,$(1),-name '*.c')
find_asm_sources  = $(call find_files,$(1),-name '*.asm')
to_objects        = $(addprefix $(BUILD_DIR)/,$(patsubst %.asm,%.o,$(patsubst %.c,%.o,$(1))))

include boot/Makefile
include kernel/Makefile
include utils/Makefile
include drivers/Makefile
include tests/Makefile
include external/Makefile
include programs/Makefile

RAMFS_STATIC_FILES := $(sort $(shell find $(RAMFS_DIR) -type f ! -name '*.o' | sort))

CORE_OBJECTS      := $(BOOT_OBJECTS) $(KERNEL_OBJECTS) $(UTILS_OBJECTS) $(DRIVERS_OBJECTS) $(EXTERNAL_OBJECTS) $(LUA_OBJECTS)
ALL_OBJECTS       := $(CORE_OBJECTS) $(TESTS_OBJECTS) $(CLDTEST_OBJECT)
OBJECTS           := $(if $(ENABLE_TESTS),$(ALL_OBJECTS),$(CORE_OBJECTS))

CFLAGS            := -ffreestanding -m64 -mcmodel=kernel -O2 -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -nostdlib \
                     -I$(BOOT_INC_DIR) -I$(KERNEL_INC_DIR) -I$(BUILD_DIR)/generated $(UTILS_INCLUDES) \
                     -I$(DRIVERS_INC_DIR) -Idrivers/ps2 -Idrivers/pic -I$(LUA_DIR) $(EXTERNAL_INCLUDES) \
                     -DMSPACES -DONLY_MSPACES -DUSE_DL_PREFIX
CFLAGS_WITH_TESTS := $(CFLAGS) -DCLDTEST_ENABLED
ACTIVE_CFLAGS     = $(if $(ENABLE_TESTS),$(CFLAGS_WITH_TESTS),$(CFLAGS))

ifeq ($(QEMU_ISA_DEBUGCON),true)
    CFLAGS += -DQEMU_ISA_DEBUGCON
    CFLAGS_WITH_TESTS += -DQEMU_ISA_DEBUGCON
endif

define PROMPT_BUILD_LABEL
label="$(BUILD_LABEL)"; \
if [ -z "$$label" ]; then \
	printf "Build label [build]: "; \
	read label || label=""; \
	if [ -z "$$label" ]; then label="build"; fi; \
fi
endef

define RUN_LABELED_MAKE
@$(PROMPT_BUILD_LABEL); \
$(MAKE) BUILD_LABEL="$$label" $(1) build-x86_64-internal
endef

define RUN_DOCKER_TARGET
@echo "$(COLOR_YELLOW)Building$(COLOR_RESET) kernel$(if $(filter build-x86_64-test,$(1)), with tests,) using Docker..."
	@$(PROMPT_BUILD_LABEL); \
	git_dir=$$(pwd); \
	git_branch=$$(git -c safe.directory="$$git_dir" rev-parse --abbrev-ref HEAD 2>/dev/null || printf "unknown"); \
	git_commit=$$(git -c safe.directory="$$git_dir" rev-parse --short=12 HEAD 2>/dev/null || printf "unknown"); \
	docker run --rm -e BUILD_LABEL="$$label" -e SYSINFO_GIT_BRANCH="$$git_branch" -e SYSINFO_GIT_COMMIT="$$git_commit" -v "$$PWD":/root/env cld-kernel-env make BUILD_LABEL="$$label" $(1)
endef

.PHONY: all clean build-x86_64 build-x86_64-test build-x86_64-internal test build qemu build-docker FORCE

all: build-docker build

$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
	@$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "$(COLOR_GREEN)Compiling:$(COLOR_RESET) $<"
	@$(CC) $(ACTIVE_CFLAGS) $(TARGET_CFLAGS) -c $< -o $@

$(RAMFS_BIN_DIR)/%.o: $(PROGRAMS_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "  Compiling $(basename $(notdir $<))"
	@$(ASM) -f elf64 $< -o $@

$(SYSINFO_HEADER): version.txt $(SYSINFO_SCRIPT) FORCE
	@mkdir -p $(dir $@)
	@sh $(SYSINFO_SCRIPT) $@ "$(BUILD_LABEL)" "$(SYSINFO_GIT_BRANCH)" "$(SYSINFO_GIT_COMMIT)"

$(BUILD_DIR)/kernel/kernel.elf: $(OBJECTS)
	@mkdir -p $(dir $@)
	@echo "$(COLOR_YELLOW)Linking kernel$(COLOR_RESET)"
	@$(LD) -n -o $@ -T $(LINKER_SCRIPT) $(OBJECTS)

$(ISO_DIR)/boot/kernel.elf: $(BUILD_DIR)/kernel/kernel.elf
	@mkdir -p $(dir $@)
	@cp $< $@

$(GRUB_CFG_DST): $(CONF_DIR)/grub.cfg
	@mkdir -p $(dir $@)
	@cp $< $@

$(RAMFS_CPIO): $(RAMFS_STATIC_FILES) $(PROGRAM_OBJECTS)
	@echo "$(COLOR_YELLOW)Building$(COLOR_RESET) ramfs archive from: $(RAMFS_DIR)"
	@mkdir -p $(dir $@)
	@mkdir -p $(RAMFS_DIR)/etc
	@mkdir -p $(RAMFS_DIR)/usr/share/fonts
	@if [ -f Example/src/drivers/video/Lat15-Terminus16.psf ]; then \
		cp Example/src/drivers/video/Lat15-Terminus16.psf $(RAMFS_DIR)/usr/share/fonts/Lat15-Terminus16.psf; \
	fi
	@(cd $(RAMFS_DIR) && find . | cpio -H newc -o > ../$@)
	@cpio -itv < $@

$(BUILD_DIR)/$(TARGET): $(ISO_DIR)/boot/kernel.elf $(RAMFS_CPIO) $(GRUB_CFG_DST)
	@echo "$(COLOR_YELLOW)Building$(COLOR_RESET) ISO from: $(ISO_DIR)"
	@$(GRUBMKRESCUE) -o $@ $(ISO_DIR)

build-x86_64:
	$(call RUN_LABELED_MAKE,)

build-x86_64-test:
	$(call RUN_LABELED_MAKE,ENABLE_TESTS=1 )

build-x86_64-internal: $(BUILD_DIR)/$(TARGET)
ifdef ENABLE_TESTS
	@echo "$(COLOR_GREEN)Build with tests successful.$(COLOR_RESET)\nISO created in: $(BUILD_DIR)/$(TARGET)"
else
	@echo "$(COLOR_GREEN)Build successful.$(COLOR_RESET)\nISO created in: $(BUILD_DIR)/$(TARGET)"
endif
	@echo "You can run ISO in QEMU by executing: $(COLOR_YELLOW)make qemu$(COLOR_RESET)"

build:
	$(call RUN_DOCKER_TARGET,build-x86_64)

test:
	$(call RUN_DOCKER_TARGET,build-x86_64-test)

build-docker:
	@echo "$(COLOR_YELLOW)Building$(COLOR_RESET) Docker image..."
	@docker build -t cld-kernel-env -f Dockerfile .

qemu:
	@echo "$(COLOR_GREEN)Starting$(COLOR_RESET) QEMU..."
ifeq ($(QEMU_ISA_DEBUGCON),true)
	@qemu-system-x86_64 -m 4G -cdrom $(BUILD_DIR)/$(TARGET) -device isa-debugcon,chardev=dbg_console -chardev stdio,id=dbg_console -no-reboot $(QEMU_DISPLAY_OPTS)
else
	@qemu-system-x86_64 -m 4G -cdrom $(BUILD_DIR)/$(TARGET) -no-reboot -no-shutdown $(QEMU_DISPLAY_OPTS)
endif

clean:
	rm -rf $(BUILD_DIR)/*

FORCE:

endif
