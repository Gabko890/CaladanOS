KERNEL_SRC_DIR := kernel/src
KERNEL_INC_DIR := kernel/include

KERNEL_SOURCES := $(call find_asm_sources,$(KERNEL_SRC_DIR)) $(call find_c_sources,$(KERNEL_SRC_DIR))
KERNEL_OBJECTS := $(call to_objects,$(KERNEL_SOURCES))

$(BUILD_DIR)/kernel/src/sysinfo.o: $(KERNEL_SRC_DIR)/sysinfo.c $(SYSINFO_HEADER)
