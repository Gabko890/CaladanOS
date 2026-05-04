UTILS_DIR      := utils
UTILS_SUBDIRS  := $(shell find $(UTILS_DIR) -type d)
UTILS_INCLUDES := $(addprefix -I,$(UTILS_SUBDIRS))

UTILS_SOURCES  := $(call find_asm_sources,$(UTILS_DIR)) \
                  $(filter-out $(UTILS_DIR)/cldtest/cldtest.c,$(call find_c_sources,$(UTILS_DIR)))
UTILS_OBJECTS  := $(call to_objects,$(UTILS_SOURCES))
CLDTEST_OBJECT := $(BUILD_DIR)/utils/cldtest/cldtest.o
