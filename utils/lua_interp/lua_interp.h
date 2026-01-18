#ifndef CLD_LUA_INTERP_H
#define CLD_LUA_INTERP_H

#include <cldtypes.h>

// Run a Lua-like script from a RAMFS path (relative or absolute).
// Returns 0 on success, non-zero on error.
int cld_lua_run_file(const char *path);

// Run with arguments (argv[0] is script path; user args start at index 1)
int cld_lua_run_file_with_args(const char *path, int argc, const char **argv);

// Deferred runners for scheduling from shell
void cld_lua_run_deferred(void *arg);
void cld_lua_run_deferred_with_args(void *arg);

#endif // CLD_LUA_INTERP_H
