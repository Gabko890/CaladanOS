PROGRAMS_DIR    := programs
PROGRAM_SOURCES := $(call find_asm_sources,$(PROGRAMS_DIR))
PROGRAM_OBJECTS := $(patsubst $(PROGRAMS_DIR)/%.asm,$(RAMFS_BIN_DIR)/%.o,$(PROGRAM_SOURCES))
