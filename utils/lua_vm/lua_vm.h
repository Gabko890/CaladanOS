#ifndef CLD_LUA_VM_H
#define CLD_LUA_VM_H

// Run a Lua 5.x script from RAMFS using the embedded Lua VM
// argv[0] is script path; user args start at 1
int cld_luavm_run_file_with_args(const char *path, int argc, const char **argv);

// Deferred runner helpers for shell scheduling
void cld_luavm_run_deferred(void *arg);
void cld_luavm_run_deferred_with_args(void *arg);

#endif // CLD_LUA_VM_H

