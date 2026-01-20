#include "lua_vm.h"

#include <vgaio.h>
#include <string.h>
#include <kmalloc.h>
#include <cldramfs/cldramfs.h>
#include <cldramfs/tty.h>
#include <ps2.h>
#include <shell_control.h>
#include <pit/pit.h>

#include <lua.h>

// Restore default shell input handler symbol
extern void shell_key_handler(u8 scancode, int is_extended, int is_pressed);

typedef struct {
    int argc;
    const char **argv;
} vm_args_t;

// ---- input helpers (no echo line and getch) ----
static volatile int vm_waiting_input = 0;
static volatile int vm_input_ready = 0;
static char *vm_last_input = NULL;

static volatile int vm_waiting_ch = 0;
static volatile int vm_ch_ready = 0;
static char vm_ch = '\0';

static void set_vm_last_input(const char *src) {
    if (vm_last_input) { kfree(vm_last_input); vm_last_input = NULL; }
    if (!src) return;
    u32 n = strlen(src);
    vm_last_input = (char*)kmalloc(n + 1);
    if (vm_last_input) { strcpy(vm_last_input, src); }
}

static void vm_key_line(u8 sc, int is_extended, int is_pressed) {
    (void)is_pressed;
    if (!vm_waiting_input) return;
    if (tty_global_handle_key(sc, is_extended)) {
        char *line = tty_global_get_line();
        set_vm_last_input(line);
        vm_input_ready = 1;
        vm_waiting_input = 0;
        tty_global_reset_line();
    }
}

static void vm_key_getch(u8 sc, int is_extended, int is_pressed) {
    (void)is_extended; (void)is_pressed;
    if (!vm_waiting_ch) return;
    u128 keyarr = ps2_keyarr();
    int shift = (keyarr & ((u128)1 << 0x2A)) || (keyarr & ((u128)1 << 0x36));
    int ctrl  = (keyarr & ((u128)1 << 0x1D)) ? 1 : 0; // left ctrl
    char c = 0;
    if (sc == US_ENTER) c = '\n';
    else if (sc == US_BACKSPACE) c = '\b';
    else if (ctrl && sc == US_S) c = 0x13; // Ctrl+S
    else if (ctrl && sc == US_Q) c = 0x11; // Ctrl+Q
    else c = scancode_to_char(sc, shift);
    if (c) { vm_ch = c; vm_ch_ready = 1; vm_waiting_ch = 0; }
}

// ---- Lua C functions ----

static int l_print(lua_State *L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) vga_printf("\t");
        size_t len = 0;
        const char *s = lua_tolstring(L, i, &len);
        if (s) vga_printf("%s", s);
        else if (lua_isinteger(L, i)) vga_printf("%lld", (long long)lua_tointeger(L, i));
        else if (lua_isboolean(L, i)) vga_printf("%s", lua_toboolean(L, i) ? "true" : "false");
        else if (lua_isnil(L, i)) vga_printf("nil");
        else vga_printf("<val>");
    }
    // vga_printf("\n");
    return 0;
}

static int l_readfile(lua_State *L) {
    const char *path = lua_isstring(L, 1) ? lua_tostring(L, 1) : NULL;
    Node *f = cldramfs_resolve_path_file(path, 0);
    if (!f || f->type != FILE_NODE) return 0;
    if (f->content && f->content_size) vga_printf("%s", f->content);
    if (!f->content || (f->content_size && f->content[f->content_size-1] != '\n')) vga_printf("\n");
    return 0;
}

static void vm_file_write(Node *file, const char *data, int append) {
    if (!file) return;
    if (!data) data = "";
    u32 text_len = strlen(data);
    if (append && file->content && file->content_size > 0) {
        u32 new_size = file->content_size + text_len;
        char *new_content = (char*)kmalloc(new_size + 1);
        if (new_content) {
            strcpy(new_content, file->content);
            strcat(new_content, data);
            kfree(file->content);
            file->content = new_content;
            file->content_size = new_size;
        }
    } else {
        if (file->content) kfree(file->content);
        file->content = (char*)kmalloc(text_len + 1);
        if (file->content) {
            strcpy(file->content, data);
            file->content_size = text_len;
        }
    }
}

static int l_writefile(lua_State *L) {
    const char *path = lua_isstring(L, 1) ? lua_tostring(L, 1) : NULL;
    size_t len = 0; const char *data = lua_tolstring(L, 2, &len);
    Node *f = cldramfs_resolve_path_file(path, 1);
    if (!f) return 0;
    vm_file_write(f, data, 0);
    return 0;
}

static int l_appendfile(lua_State *L) {
    const char *path = lua_isstring(L, 1) ? lua_tostring(L, 1) : NULL;
    size_t len = 0; const char *data = lua_tolstring(L, 2, &len);
    Node *f = cldramfs_resolve_path_file(path, 1);
    if (!f) return 0;
    vm_file_write(f, data, 1);
    return 0;
}

static int l_input(lua_State *L) {
    const char *prompt = NULL;
    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) prompt = lua_tostring(L, 1);
    if (prompt) vga_printf("%s", prompt);

    // Build line using getch-style capture
    size_t cap = 64; size_t len = 0;
    char *buf = (char*)kmalloc(cap);
    if (!buf) { lua_pushstring(L, ""); return 1; }

    for (;;) {
        vm_ch_ready = 0; vm_waiting_ch = 1;
        ps2_set_key_callback(vm_key_getch);
        while (!vm_ch_ready) { }
        ps2_set_key_callback(shell_key_handler);

        char c = vm_ch;
        if (c == '\r') c = '\n';
        if (c == '\n') break;
        if (c == '\b') {
            if (len > 0) {
                len--;
                vga_printf("\x1b[D \x1b[D");
            }
            continue;
        }
        vga_putchar(c);
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *nbuf = (char*)krealloc(buf, ncap);
            if (!nbuf) { break; }
            buf = nbuf; cap = ncap;
        }
        buf[len++] = c;
    }
    vga_putchar('\n');
    if (len + 1 >= cap) {
        char *nbuf = (char*)krealloc(buf, len + 1);
        if (nbuf) buf = nbuf; else if (len > 0) len--; // ensure space
    }
    buf[len] = '\0';
    set_vm_last_input(buf);
    lua_pushstring(L, buf);
    kfree(buf);
    return 1;
}

static int l_getch(lua_State *L) {
    (void)L;
    vm_ch_ready = 0; vm_waiting_ch = 1; ps2_set_key_callback(vm_key_getch);
    while (!vm_ch_ready) { }
    ps2_set_key_callback(shell_key_handler);
    char tmp[2] = { vm_ch, '\0' };
    lua_pushstring(L, tmp);
    return 1;
}

static int l_exit(lua_State *L) {
    (void)L; /* no-op: allow scripts to call exit() without triggering longjmp */
    return 0;
}

static int l_sleep(lua_State *L) {
    int isnum = 0; long long ms = (long long)lua_tointegerx(L, 1, &isnum);
    if (!isnum) {
        if (lua_isstring(L, 1)) {
            const char *s = lua_tostring(L, 1);
            if (s) {
                u64 acc = 0; int any = 0;
                // simple decimal parse; ignore leading spaces and '+'
                while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
                if (*s == '+') s++;
                while (*s >= '0' && *s <= '9') { any = 1; acc = acc * 10 + (u64)(*s - '0'); s++; }
                if (any) ms = (long long)acc; else ms = 0;
            }
        }
    }
    if (ms <= 0) return 0;
    sleep_ms((u64)ms);
    return 0;
}

static int l_write(lua_State *L) {
    size_t len = 0; const char *s = lua_tolstring(L, 1, &len);
    if (s && len) vga_printf("%s", s);
    return 0;
}

static int l_cls(lua_State *L) {
    (void)L; vga_printf("\x1b[2J\x1b[H"); return 0;
}

static int l_readtext(lua_State *L) {
    const char *path = lua_isstring(L, 1) ? lua_tostring(L, 1) : NULL;
    if (!path) { lua_pushstring(L, ""); return 1; }
    Node *f = cldramfs_resolve_path_file(path, 0);
    if (!f || f->type != FILE_NODE || !f->content) { lua_pushstring(L, ""); return 1; }
    lua_pushlstring(L, f->content, f->content_size);
    return 1;
}

// RAMFS commands
static int l_fs_ls(lua_State *L) { const char *p = lua_isstring(L,1)?lua_tostring(L,1):NULL; cldramfs_cmd_ls(p); return 0; }
static int l_fs_cd(lua_State *L) { const char *p = lua_isstring(L,1)?lua_tostring(L,1):NULL; cldramfs_cmd_cd(p); return 0; }
static int l_fs_mkdir(lua_State *L) { const char *p = lua_isstring(L,1)?lua_tostring(L,1):NULL; if(p) cldramfs_cmd_mkdir(p); return 0; }
static int l_fs_rmdir(lua_State *L) { const char *p = lua_isstring(L,1)?lua_tostring(L,1):NULL; if(p) cldramfs_cmd_rmdir(p); return 0; }
static int l_fs_rm(lua_State *L) { const char *p = lua_isstring(L,1)?lua_tostring(L,1):NULL; if(p) cldramfs_cmd_rm(p); return 0; }
static int l_fs_cp(lua_State *L) { const char *a=lua_isstring(L,1)?lua_tostring(L,1):NULL; const char *b=lua_isstring(L,2)?lua_tostring(L,2):NULL; if(a&&b) cldramfs_cmd_cp(a,b); return 0; }
static int l_fs_mv(lua_State *L) { const char *a=lua_isstring(L,1)?lua_tostring(L,1):NULL; const char *b=lua_isstring(L,2)?lua_tostring(L,2):NULL; if(a&&b) cldramfs_cmd_mv(a,b); return 0; }
static int l_fs_pwd(lua_State *L) {
    if (!ramfs_cwd || !ramfs_root) { lua_pushstring(L, "/"); return 1; }
    if (ramfs_cwd == ramfs_root) { lua_pushstring(L, "/"); return 1; }
    Node *nodes[16]; int depth=0; Node *cur=ramfs_cwd;
    while (cur && cur != ramfs_root && depth<16) { nodes[depth++]=cur; cur=cur->parent; }
    char tmp[256]; tmp[0]='\0'; strcat(tmp, "/");
    for (int i=depth-1;i>=0;i--) { if (nodes[i] && nodes[i]->name) { strcat(tmp, nodes[i]->name); if (i>0) strcat(tmp, "/"); } }
    lua_pushstring(L, tmp); return 1;
}

static int l_argc(lua_State *L) { void *ud=NULL; lua_getallocf(L, &ud); vm_args_t *a = (vm_args_t*)ud; lua_pushinteger(L, a ? a->argc : 0); return 1; }
static int l_arg(lua_State *L) { int isnum=0; long long idx=(long long)lua_tointegerx(L,1,&isnum); if(!isnum) idx=0; void *ud=NULL; lua_getallocf(L,&ud); vm_args_t*a=(vm_args_t*)ud; if (!a||idx<0||idx>=a->argc) { lua_pushstring(L,""); } else { lua_pushstring(L,a->argv[idx]); } return 1; }

// Loader reader
typedef struct { const char *buf; size_t len; int used; } chunk_t;
static const char* lreader(lua_State *L, void *ud, size_t *sz) {
    (void)L;
    chunk_t *c = (chunk_t*)ud;
    if (c->used) { *sz=0; return NULL; }
    c->used = 1; *sz = c->len; return c->buf;
}

// Allocator bridging kmalloc/krealloc/kfree
static void* l_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) { if (ptr) kfree(ptr); return NULL; }
    if (!ptr) return kmalloc(nsize);
    return krealloc(ptr, nsize);
}

int cld_luavm_run_file_with_args(const char *path, int argc, const char **argv) {
    if (!path || !*path) { vga_printf("lua(vm): missing script path\n"); return 1; }
    Node *file = cldramfs_resolve_path_file(path, 0);
    if (!file || file->type != FILE_NODE || !file->content) { vga_printf("lua(vm): file not found: %s\n", path); return 1; }

    vm_args_t args = { .argc = argc, .argv = argv };
    lua_State *L = lua_newstate(l_alloc, &args, 0x12345678u);
    if (!L) { vga_printf("lua(vm): failed to create state\n"); return 1; }

    // Register globals
    lua_pushcfunction(L, l_print); lua_setglobal(L, "print");
    lua_pushcfunction(L, l_input); lua_setglobal(L, "input");
    lua_pushcfunction(L, l_getch); lua_setglobal(L, "getch");
    lua_pushcfunction(L, l_readfile); lua_setglobal(L, "readfile");
    lua_pushcfunction(L, l_writefile); lua_setglobal(L, "writefile");
    lua_pushcfunction(L, l_appendfile); lua_setglobal(L, "appendfile");
    lua_pushcfunction(L, l_exit); lua_setglobal(L, "exit");
    lua_pushcfunction(L, l_sleep); lua_setglobal(L, "sleep");
    lua_pushcfunction(L, l_write); lua_setglobal(L, "write");
    lua_pushcfunction(L, l_cls); lua_setglobal(L, "cls");
    lua_pushcfunction(L, l_readtext); lua_setglobal(L, "readtext");
    lua_pushcfunction(L, l_fs_ls); lua_setglobal(L, "fs_ls");
    lua_pushcfunction(L, l_fs_cd); lua_setglobal(L, "fs_cd");
    lua_pushcfunction(L, l_fs_mkdir); lua_setglobal(L, "fs_mkdir");
    lua_pushcfunction(L, l_fs_rmdir); lua_setglobal(L, "fs_rmdir");
    lua_pushcfunction(L, l_fs_rm); lua_setglobal(L, "fs_rm");
    lua_pushcfunction(L, l_fs_cp); lua_setglobal(L, "fs_cp");
    lua_pushcfunction(L, l_fs_mv); lua_setglobal(L, "fs_mv");
    lua_pushcfunction(L, l_fs_pwd); lua_setglobal(L, "fs_pwd");
    lua_pushcfunction(L, l_argc); lua_setglobal(L, "argc");
    lua_pushcfunction(L, l_arg); lua_setglobal(L, "arg");

    // Load chunk
    chunk_t ck = { .buf = file->content, .len = file->content_size, .used = 0 };
    if (lua_load(L, lreader, &ck, path, NULL) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        vga_printf("lua(vm): load error: %s\n", err ? err : "<err>");
        return 1;
    }
    // Run
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        vga_printf("lua(vm): runtime error: %s\n", err ? err : "<err>");
        return 1;
    }
    return 0;
}

typedef struct { char *path; int argc; char **argv; } lua_task_args_t;

void cld_luavm_run_deferred(void *arg) {
    const char *path = (const char*)arg; const char *argv0 = path;
    int rc = cld_luavm_run_file_with_args(path, 1, &argv0);
    if (rc != 0) vga_printf("lua(vm): exited with code %d\n", rc);
    if (arg) kfree(arg);
    shell_resume();
}

void cld_luavm_run_deferred_with_args(void *arg) {
    lua_task_args_t *t = (lua_task_args_t*)arg; const char **argv = (const char**)t->argv;
    int rc = cld_luavm_run_file_with_args(t->path, t->argc, argv);
    if (rc != 0) vga_printf("lua(vm): exited with code %d\n", rc);
    if (t) {
        if (t->path) kfree(t->path);
        for (int i=0;i<t->argc;i++) { if (t->argv && t->argv[i]) kfree(t->argv[i]); }
        if (t->argv) { kfree(t->argv); }
        kfree(t);
    }
    shell_resume();
}
