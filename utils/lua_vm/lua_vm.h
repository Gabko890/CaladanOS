#ifndef CLD_LUA_VM_H
#define CLD_LUA_VM_H

#include <cldtypes.h>

// Run a Lua 5.x script from RAMFS using the embedded Lua VM
// argv[0] is script path; user args start at 1
int cld_luavm_run_file_with_args(const char *path, int argc, const char **argv);

// Execute a Lua config file and copy selected global string values.
int cld_luavm_read_config_strings(const char *path, const char **keys, char **out, const u32 *out_sizes, int count);

// Deferred runner helpers for shell scheduling
void cld_luavm_run_deferred(void *arg);
void cld_luavm_run_deferred_with_args(void *arg);

#endif // CLD_LUA_VM_H
