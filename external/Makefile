EXTERNAL_DIR      := external
EXTERNAL_SUBDIRS  := $(shell find $(EXTERNAL_DIR) -type d)
EXTERNAL_INCLUDES := $(addprefix -I,$(EXTERNAL_SUBDIRS)) -I$(EXTERNAL_DIR)

DLMALLOC_DIR      := $(EXTERNAL_DIR)/dlmalloc
EXTERNAL_SOURCES  := $(call find_c_sources,$(DLMALLOC_DIR))
EXTERNAL_OBJECTS  := $(call to_objects,$(EXTERNAL_SOURCES))

LUA_DIR           := $(EXTERNAL_DIR)/lua-5.5.0/src
LUA_SOURCES       := $(filter-out $(LUA_DIR)/lauxlib.c $(LUA_DIR)/linit.c $(LUA_DIR)/loadlib.c $(LUA_DIR)/lua.c $(LUA_DIR)/luac.c, \
                     $(filter-out %lib.c,$(call find_c_sources,$(LUA_DIR))))
LUA_OBJECTS       := $(call to_objects,$(LUA_SOURCES))

$(LUA_OBJECTS): TARGET_CFLAGS += -Dl_signalT=int
