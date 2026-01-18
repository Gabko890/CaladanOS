#include "lua_interp.h"

#include <vgaio.h>
#include <string.h>
#include <cldramfs/cldramfs.h>
#include <cldramfs/tty.h>
#include <ps2.h>
#include <shell_control.h>
#include <kmalloc.h>
#include <deferred.h>

// A tiny, Lua-like interpreter for a subset of Lua focusing on simple
// function calls. It supports:
// - Comments: -- until end of line
// - Function calls: name(arg1, arg2, ...)
// - Literal args: strings ('...' or "..." with simple escapes), integers,
//   booleans (true/false), and nil
// Builtins exposed:
//   print(...)
//   readfile(path)
//   writefile(path, data)
//   appendfile(path, data)
//   exit([code])

typedef enum {
    LVAL_NIL,
    LVAL_BOOL,
    LVAL_INT,
    LVAL_STR
} lval_type_t;

typedef struct {
    lval_type_t type;
    union {
        int b;
        long long i;
        const char *s; // pointer into buffer (not owned)
    } as;
} lval_t;

// Forward decl from kernel for restoring shell input
extern void shell_key_handler(u8 scancode, int is_extended, int is_pressed);

// Simple input state
static volatile int g_waiting_input = 0;
static volatile int g_input_ready = 0;
static char *g_last_input = NULL;

// getch state (single key, no echo)
static volatile int g_waiting_ch = 0;
static volatile int g_ch_ready = 0;
static char g_ch = '\0';

static void set_last_input(const char *src) {
    if (g_last_input) { kfree(g_last_input); g_last_input = NULL; }
    if (!src) return;
    u32 n = strlen(src);
    g_last_input = (char*)kmalloc(n + 1);
    if (g_last_input) { strcpy(g_last_input, src); }
}

static void lua_key_handler(u8 sc, int is_extended, int is_pressed) {
    (void)is_pressed; // always 1 from ps2.c
    if (!g_waiting_input) return;
    if (tty_global_handle_key(sc, is_extended)) {
        // Enter pressed; capture line and signal ready
        char *line = tty_global_get_line();
        set_last_input(line);
        g_input_ready = 1;
        g_waiting_input = 0;
        // Leave TTY line empty for next time
        tty_global_reset_line();
    }
}

static void lua_getch_key_handler(u8 sc, int is_extended, int is_pressed) {
    (void)is_extended; (void)is_pressed; // pressed only
    if (!g_waiting_ch) return;
    // compute shift status from current key array
    u128 keyarr = ps2_keyarr();
    int shift = (keyarr & ((u128)1 << 0x2A)) || (keyarr & ((u128)1 << 0x36));
    char c = 0;
    if (sc == US_ENTER) c = '\n';
    else c = scancode_to_char(sc, shift);
    if (c) {
        g_ch = c;
        g_ch_ready = 1;
        g_waiting_ch = 0;
    }
}

typedef struct {
    const char *src; // whole script buffer
    u32 len;
    u32 pos;
    const char *name; // script name for messages
} parser_t;

static int at_end(parser_t *p) { return p->pos >= p->len; }
static char peek(parser_t *p) { return at_end(p) ? '\0' : p->src[p->pos]; }
static char peek2(parser_t *p) { return (p->pos + 1 < p->len) ? p->src[p->pos + 1] : '\0'; }
static char advance(parser_t *p) { return at_end(p) ? '\0' : p->src[p->pos++]; }

// Forward decls used before definitions
static int expect(parser_t *p, char ch);
static int cld_lua_argc(void);
static const char* cld_lua_argv_at(int idx);
static int tokens_count(void);
static const char* tokens_at(int idx);

// =============== simple variable storage ===============
typedef struct {
    char name[32];
    lval_t val;
    int in_use;
} var_entry_t;

#define VAR_MAX 64
static var_entry_t g_vars[VAR_MAX];

static lval_t lval_copy(const lval_t *v) {
    lval_t r = *v;
    if (v->type == LVAL_STR && v->as.s) {
        u32 n = strlen(v->as.s);
        char *s = (char*)kmalloc(n+1);
        if (s) { strcpy(s, v->as.s); r.as.s = s; }
    }
    return r;
}

static void lval_free(lval_t *v) {
    if (v->type == LVAL_STR && v->as.s) { kfree((void*)v->as.s); }
    v->type = LVAL_NIL; v->as.s = NULL;
}

static int var_find(const char *name) {
    for (int i=0;i<VAR_MAX;i++) if (g_vars[i].in_use && strcmp(g_vars[i].name, name)==0) return i;
    return -1;
}
static int var_alloc_slot(const char *name) {
    for (int i=0;i<VAR_MAX;i++) if (!g_vars[i].in_use) { g_vars[i].in_use=1; strncpy(g_vars[i].name,name,sizeof(g_vars[i].name)-1); g_vars[i].name[sizeof(g_vars[i].name)-1]='\0'; g_vars[i].val.type=LVAL_NIL; g_vars[i].val.as.s=NULL; return i; }
    return -1;
}
static void var_set(const char *name, const lval_t *v) {
    int idx = var_find(name);
    if (idx < 0) idx = var_alloc_slot(name);
    if (idx < 0) return;
    lval_free(&g_vars[idx].val);
    g_vars[idx].val = lval_copy(v);
}
static int var_get(const char *name, lval_t *out) {
    int idx = var_find(name);
    if (idx < 0) return 0;
    *out = lval_copy(&g_vars[idx].val);
    return 1;
}

static void skip_ws_and_comments(parser_t *p) {
    for (;;) {
        // skip whitespace
        while (!at_end(p)) {
            char c = peek(p);
            if (c==' '||c=='\t'||c=='\r'||c=='\n') { advance(p); }
            else break;
        }
        // skip line comments -- ...\n
        if (!at_end(p) && peek(p)=='-' && peek2(p)=='-') {
            while (!at_end(p) && advance(p) != '\n') { /* skip */ }
            continue; // loop to consume more ws
        }
        break;
    }
}

static int is_ident_start(char c) {
    return (c=='_' || (c>='a'&&c<='z') || (c>='A'&&c<='Z'));
}
static int is_ident(char c) {
    return is_ident_start(c) || (c>='0'&&c<='9');
}

static int parse_ident(parser_t *p, char *out, u32 out_sz) {
    if (!is_ident_start(peek(p))) return 0;
    u32 i = 0;
    while (!at_end(p) && is_ident(peek(p))) {
        if (i + 1 < out_sz) out[i++] = advance(p);
        else { advance(p); }
    }
    out[i] = '\0';
    return 1;
}

static int parse_int(parser_t *p, long long *out) {
    int neg = 0;
    if (peek(p)=='-') { neg=1; advance(p); }
    if (!(peek(p)>='0'&&peek(p)<='9')) return 0;
    long long v = 0;
    while (!at_end(p) && (peek(p)>='0'&&peek(p)<='9')) {
        v = v*10 + (advance(p)-'0');
    }
    *out = neg ? -v : v;
    return 1;
}

static int parse_str(parser_t *p, const char **start_out, u32 *len_out) {
    char q = peek(p);
    if (q!='\'' && q!='\"') return 0;
    advance(p); // consume quote
    u32 start = p->pos;
    // We parse until matching quote; support simple escapes: \\ \n \t \r \" \'
    // For simplicity, we do not unescape in-place; we return a slice if no escapes,
    // otherwise we allocate a temporary buffer (kmalloc) and copy unescaped content.
    int saw_escape = 0;
    u32 i = p->pos;
    while (i < p->len) {
        char c = p->src[i];
        if (c == '\\') { saw_escape = 1; i += 2; continue; }
        if (c == q) break;
        if (c == '\n' || c == '\r') break; // unterminated
        i++;
    }
    if (i >= p->len || p->src[i] != q) {
        vga_printf("[lua] %s: unterminated string literal\n", p->name ? p->name : "<script>");
        return 0;
    }

    // Copy/unescape into a new, null-terminated buffer
    u32 maxlen = i - start;
    char *buf = (char*)kmalloc(maxlen + 1);
    if (!buf) return 0;
    u32 bi = 0;
    if (!saw_escape) {
        // straight copy
        for (u32 si = start; si < i; ++si) buf[bi++] = p->src[si];
    } else {
        for (u32 si = start; si < i; ++si) {
            char c = p->src[si];
            if (c == '\\' && si + 1 < i) {
                char e = p->src[++si];
                switch (e) {
                    case 'n': buf[bi++]='\n'; break;
                    case 't': buf[bi++]='\t'; break;
                    case 'r': buf[bi++]='\r'; break;
                    case '\\': buf[bi++]='\\'; break;
                    case '\'': buf[bi++]='\''; break;
                    case '"': buf[bi++]='"'; break;
                    default: buf[bi++]=e; break;
                }
            } else {
                buf[bi++] = c;
            }
        }
    }
    buf[bi] = '\0';
    *start_out = buf; // ephemeral; not freed for simplicity in this run
    *len_out = bi;
    p->pos = i + 1;
    return 1;
}

static int parse_value(parser_t *p, lval_t *out) {
    skip_ws_and_comments(p);
    char c = peek(p);
    if (c=='\'' || c=='\"') {
        const char *s; u32 sl;
        if (!parse_str(p, &s, &sl)) return 0;
        out->type = LVAL_STR; out->as.s = s; (void)sl; // strings are \0-terminated slices
        return 1;
    }
    if (c=='t' || c=='f') {
        char id[8];
        if (!parse_ident(p, id, sizeof id)) return 0;
        if (strcmp(id, "true") == 0) { out->type=LVAL_BOOL; out->as.b=1; return 1; }
        if (strcmp(id, "false") == 0) { out->type=LVAL_BOOL; out->as.b=0; return 1; }
        return 0;
    }
    if (c=='n') {
        char id[8];
        if (!parse_ident(p, id, sizeof id)) return 0;
        if (strcmp(id, "nil") == 0) { out->type=LVAL_NIL; return 1; }
        return 0;
    }
    if (is_ident_start(c)) {
        char id[32];
        if (!parse_ident(p, id, sizeof id)) return 0;
        // last
        if (strcmp(id, "last") == 0) {
            out->type = LVAL_STR; out->as.s = g_last_input ? g_last_input : "";
            return 1;
        }
        // argc()
        if (strcmp(id, "argc") == 0) {
            skip_ws_and_comments(p);
            if (!expect(p, '(') || !expect(p, ')')) return 0;
            out->type = LVAL_INT; out->as.i = cld_lua_argc();
            return 1;
        }
        // arg(n)
        if (strcmp(id, "arg") == 0) {
            skip_ws_and_comments(p);
            if (!expect(p, '(')) return 0;
            long long idx = 0;
            if (!parse_int(p, &idx)) return 0;
            if (!expect(p, ')')) return 0;
            const char *s = cld_lua_argv_at((int)idx);
            out->type = LVAL_STR; out->as.s = s ? s : "";
            return 1;
        }
        // function call returning value?
        skip_ws_and_comments(p);
        if (peek(p) == '(') {
            advance(p); // '('
            lval_t argv[8]; int ac=0;
            skip_ws_and_comments(p);
            if (peek(p) != ')') {
                while (!at_end(p)) {
                    if (ac>=8) break;
                    if (!parse_value(p, &argv[ac++])) break;
                    skip_ws_and_comments(p);
                    if (peek(p)==',') { advance(p); skip_ws_and_comments(p); continue; }
                    break;
                }
            }
            if (!expect(p, ')')) return 0;
            if (strcmp(id, "concat")==0) {
                const char *a = (ac>=1 && argv[0].type==LVAL_STR)?argv[0].as.s:"";
                const char *b = (ac>=2 && argv[1].type==LVAL_STR)?argv[1].as.s:"";
                u32 na=strlen(a), nb=strlen(b);
                char *buf=(char*)kmalloc(na+nb+1); if(!buf){ out->type=LVAL_STR; out->as.s=""; return 1; }
                strcpy(buf,a); strcat(buf,b);
                out->type=LVAL_STR; out->as.s=buf; return 1;
            }
            if (strcmp(id, "cwd_str")==0) {
                if (!ramfs_cwd || !ramfs_root) { out->type=LVAL_STR; out->as.s="/"; return 1; }
                if (ramfs_cwd == ramfs_root) { out->type=LVAL_STR; out->as.s="/"; return 1; }
                Node *nodes[16]; int depth=0; Node *cur=ramfs_cwd;
                while (cur && cur != ramfs_root && depth<16) { nodes[depth++]=cur; cur=cur->parent; }
                char tmp[256]; tmp[0]='\0'; strcat(tmp,"/");
                for (int i=depth-1;i>=0;i--){ if(nodes[i]&&nodes[i]->name){ strcat(tmp,nodes[i]->name); if(i>0) strcat(tmp,"/"); }}
                char *buf=(char*)kmalloc(strlen(tmp)+1); if(buf) strcpy(buf,tmp);
                out->type=LVAL_STR; out->as.s=buf?buf:"/"; return 1;
            }
            if (strcmp(id, "tokc")==0) { out->type=LVAL_INT; out->as.i = tokens_count(); return 1; }
            if (strcmp(id, "tok")==0) {
                int idx = (ac>=1 && argv[0].type==LVAL_INT)?(int)argv[0].as.i:0;
                const char *s = tokens_at(idx);
                u32 n = strlen(s); char *cpy=(char*)kmalloc(n+1); if(cpy) strcpy(cpy,s);
                out->type=LVAL_STR; out->as.s=cpy?cpy:(char*)""; return 1; }
            out->type=LVAL_NIL; return 1;
        }
        // variable reference
        if (var_get(id, out)) return 1;
        // default to nil
        out->type = LVAL_NIL; return 1;
    }
    // integer (optional leading -)
    if (c=='-' || (c>='0'&&c<='9')) {
        long long v;
        if (!parse_int(p, &v)) return 0;
        out->type = LVAL_INT; out->as.i = v; return 1;
    }
    return 0;
}

static int expect(parser_t *p, char ch) {
    skip_ws_and_comments(p);
    if (peek(p) != ch) return 0;
    advance(p);
    return 1;
}

// RAMFS helpers
static Node* resolve_file(const char *path, int create) {
    return cldramfs_resolve_path_file(path, create ? 1 : 0);
}

static void file_write(Node *file, const char *data, int append) {
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

// Builtins
static void builtin_print(lval_t *args, int argc) {
    for (int i = 0; i < argc; ++i) {
        if (i) vga_printf("\t");
        switch (args[i].type) {
            case LVAL_STR: vga_printf("%s", args[i].as.s ? args[i].as.s : ""); break;
            case LVAL_INT: vga_printf("%lld", args[i].as.i); break;
            case LVAL_BOOL: vga_printf("%s", args[i].as.b ? "true" : "false"); break;
            case LVAL_NIL: default: vga_printf("nil"); break;
        }
    }
    vga_printf("\n");
}

static void builtin_readfile(lval_t *args, int argc) {
    if (argc < 1 || args[0].type != LVAL_STR) {
        vga_printf("[lua] readfile: expected (path)\n");
        return;
    }
    Node *f = resolve_file(args[0].as.s, 0);
    if (!f || f->type != FILE_NODE) {
        vga_printf("[lua] readfile: not found: %s\n", args[0].as.s);
        return;
    }
    if (f->content && f->content_size) vga_printf("%s", f->content);
    if (!f->content || (f->content_size && f->content[f->content_size-1] != '\n')) vga_printf("\n");
}

static void builtin_writefile(lval_t *args, int argc, int append) {
    if (argc < 2 || args[0].type != LVAL_STR) {
        vga_printf("[lua] %s: expected (path, data)\n", append?"appendfile":"writefile");
        return;
    }
    Node *f = resolve_file(args[0].as.s, 1);
    if (!f) {
        vga_printf("[lua] %s: cannot open: %s\n", append?"appendfile":"writefile", args[0].as.s);
        return;
    }
    // Coerce data to string for convenience
    char tmpbuf[64];
    const char *data = NULL;
    switch (args[1].type) {
        case LVAL_STR: data = args[1].as.s; break;
        case LVAL_INT: {
            // simple itoa for long long
            long long v = args[1].as.i;
            int neg = 0; int idx = 0;
            if (v == 0) { tmpbuf[0]='0'; tmpbuf[1]='\0'; data = tmpbuf; break; }
            if (v < 0) { neg = 1; v = -v; }
            char rev[32]; int ri=0;
            while (v && ri < (int)sizeof(rev)) { rev[ri++] = (char)('0' + (v % 10)); v/=10; }
            if (neg) tmpbuf[idx++]='-';
            while (ri>0 && idx < (int)sizeof(tmpbuf)-1) tmpbuf[idx++] = rev[--ri];
            tmpbuf[idx]='\0'; data = tmpbuf; break;
        }
        case LVAL_BOOL: data = args[1].as.b?"true":"false"; break;
        case LVAL_NIL: default: data = "nil"; break;
    }
    file_write(f, data, append);
}

static int builtin_exit(lval_t *args, int argc) {
    int code = 0;
    if (argc >= 1) {
        if (args[0].type == LVAL_INT) code = (int)args[0].as.i;
        else if (args[0].type == LVAL_BOOL) code = args[0].as.b ? 0 : 1;
    }
    return code ? code : -1; // -1 indicates request to stop (success)
}

static void builtin_input(lval_t *args, int argc) {
    // Optional prompt
    if (argc >= 1 && args[0].type == LVAL_STR && args[0].as.s) {
        vga_printf("%s", args[0].as.s);
    }
    // Temporarily take over keyboard input to collect a line
    g_input_ready = 0;
    g_waiting_input = 1;
    ps2_set_key_callback(lua_key_handler);
    while (!g_input_ready) {
        // busy wait; interrupts deliver keystrokes
    }
    // Restore shell input handler
    ps2_set_key_callback(shell_key_handler);
}

static void builtin_getch(lval_t *args, int argc) {
    // Optional prompt
    if (argc >= 1 && args[0].type == LVAL_STR && args[0].as.s) {
        vga_printf("%s", args[0].as.s);
    }
    g_ch_ready = 0;
    g_waiting_ch = 1;
    ps2_set_key_callback(lua_getch_key_handler);
    while (!g_ch_ready) {
        // busy wait; interrupts deliver keystrokes
    }
    // Store as last (single-char string)
    char tmp[2]; tmp[0] = g_ch; tmp[1] = '\0';
    set_last_input(tmp);
    // Restore shell input handler
    ps2_set_key_callback(shell_key_handler);
}

static void builtin_shell_run(void) {
    // Start the C shell loop (TTY-based) and run until 'exit'
    cldramfs_shell_init();
    shell_resume();
    while (cldramfs_shell_is_running()) {
        __asm__ volatile("hlt");
        deferred_process_all();
    }
}

// Tokenizer helpers for shell
static char* g_tokens[16];
static int g_tokc = 0;
static void tokens_clear(void){ for(int i=0;i<g_tokc;i++){ if(g_tokens[i]) kfree(g_tokens[i]); g_tokens[i]=NULL; } g_tokc=0; }

static void builtin_tokenize(lval_t *args, int argc){
    tokens_clear();
    const char *s = (argc>=1 && args[0].type==LVAL_STR)?args[0].as.s:NULL;
    if(!s){ g_tokc=0; return; }
    char buf[256]; strncpy(buf,s,sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
    char *p = buf; while(*p){ while(*p==' '||*p=='\t') p++; if(!*p) break; if(g_tokc>=16) break; char *start=p; while(*p && *p!=' ' && *p!='\t') p++; char c=*p; *p='\0'; u32 n=strlen(start); char *t=(char*)kmalloc(n+1); if(t){ strcpy(t,start); g_tokens[g_tokc++]=t; } if(c=='\0') break; p++; }
}

static int tokens_count(void){ return g_tokc; }
static const char* tokens_at(int idx){ if(idx>=0 && idx<g_tokc) return g_tokens[idx]; return ""; }

static void builtin_lua_exec_from_tokens(lval_t *args, int argc){
    if(argc<1 || args[0].type!=LVAL_STR) return;
    const char *path = args[0].as.s;
    int ac = 1 + (g_tokc>0 ? (g_tokc-1) : 0);
    const char **argv = (const char**)kmalloc(sizeof(char*)*ac);
    if(!argv) return;
    argv[0]=path;
    for(int i=1;i<ac;i++) argv[i]=g_tokens[i];
    (void)cld_lua_run_file_with_args(path, ac, argv);
    kfree(argv);
}

static int call_builtin(const char *name, lval_t *args, int argc) {
    if (strcmp(name, "print") == 0) { builtin_print(args, argc); return 0; }
    if (strcmp(name, "readfile") == 0) { builtin_readfile(args, argc); return 0; }
    if (strcmp(name, "writefile") == 0) { builtin_writefile(args, argc, 0); return 0; }
    if (strcmp(name, "appendfile") == 0) { builtin_writefile(args, argc, 1); return 0; }
    if (strcmp(name, "input") == 0) { builtin_input(args, argc); return 0; }
    if (strcmp(name, "getch") == 0) { builtin_getch(args, argc); return 0; }
    if (strcmp(name, "tokenize") == 0) { builtin_tokenize(args, argc); return 0; }
    if (strcmp(name, "lua_exec_from_tokens") == 0) { builtin_lua_exec_from_tokens(args, argc); return 0; }
    if (strcmp(name, "shell_run") == 0) { builtin_shell_run(); return 0; }
    // RAMFS command wrappers to expose filesystem operations
    if (strcmp(name, "fs_ls") == 0) {
        const char *p = (argc>=1 && args[0].type==LVAL_STR)?args[0].as.s:NULL;
        cldramfs_cmd_ls(p);
        return 0;
    }
    if (strcmp(name, "fs_pwd") == 0) {
        // Build current path similar to shell prompt
        if (!ramfs_cwd || !ramfs_root) { vga_printf("/\n"); return 0; }
        if (ramfs_cwd == ramfs_root) { vga_printf("/\n"); return 0; }
        Node *nodes[16]; int depth=0; Node *cur=ramfs_cwd;
        while (cur && cur != ramfs_root && depth < 16) { nodes[depth++] = cur; cur = cur->parent; }
        char path[256]; path[0]='\0'; strcat(path, "/");
        for (int i=depth-1; i>=0; --i) { if (nodes[i] && nodes[i]->name) { strcat(path, nodes[i]->name); if (i>0) strcat(path, "/"); } }
        vga_printf("%s\n", path);
        return 0;
    }
    if (strcmp(name, "fs_cd") == 0) {
        const char *p = (argc>=1 && args[0].type==LVAL_STR)?args[0].as.s:NULL;
        cldramfs_cmd_cd(p);
        return 0;
    }
    if (strcmp(name, "fs_mkdir") == 0) {
        if (argc>=1 && args[0].type==LVAL_STR) cldramfs_cmd_mkdir(args[0].as.s);
        else vga_printf("mkdir: missing directory name\n");
        return 0;
    }
    if (strcmp(name, "fs_rmdir") == 0) {
        if (argc>=1 && args[0].type==LVAL_STR) cldramfs_cmd_rmdir(args[0].as.s);
        else vga_printf("rmdir: missing operand\n");
        return 0;
    }
    if (strcmp(name, "fs_rm") == 0) {
        if (argc>=1 && args[0].type==LVAL_STR) cldramfs_cmd_rm(args[0].as.s);
        else vga_printf("rm: missing file operand\n");
        return 0;
    }
    if (strcmp(name, "fs_cp") == 0) {
        if (argc>=2 && args[0].type==LVAL_STR && args[1].type==LVAL_STR) cldramfs_cmd_cp(args[0].as.s, args[1].as.s);
        else vga_printf("cp: usage: cp <src> <dst>\n");
        return 0;
    }
    if (strcmp(name, "fs_mv") == 0) {
        if (argc>=2 && args[0].type==LVAL_STR && args[1].type==LVAL_STR) cldramfs_cmd_mv(args[0].as.s, args[1].as.s);
        else vga_printf("mv: usage: mv <src> <dst>\n");
        return 0;
    }
    if (strcmp(name, "exit") == 0) {
        int rc = builtin_exit(args, argc);
        if (rc == -1) return 1; // stop execution
        return rc; // treat non-zero as error code; will stop execution
    }
    vga_printf("[lua] unknown function: %s\n", name);
    return 0;
}

static int parse_call(parser_t *p) {
    char fname[64];
    if (!parse_ident(p, fname, sizeof fname)) return 0; // not a call
    skip_ws_and_comments(p);
    if (!expect(p, '(')) {
        vga_printf("[lua] %s: expected '(' after %s\n", p->name?p->name:"<script>", fname);
        return -2; // error
    }

    lval_t argv[8];
    int argc = 0;
    skip_ws_and_comments(p);
    if (peek(p) != ')') {
        while (!at_end(p)) {
            if (argc >= 8) { vga_printf("[lua] too many args (max 8)\n"); return -2; }
            if (!parse_value(p, &argv[argc++])) { vga_printf("[lua] bad argument\n"); return -2; }
            skip_ws_and_comments(p);
            if (peek(p) == ',') { advance(p); skip_ws_and_comments(p); continue; }
            break;
        }
    }
    if (!expect(p, ')')) {
        vga_printf("[lua] %s: expected ')'\n", p->name?p->name:"<script>");
        return -2;
    }
    // optional semicolon
    skip_ws_and_comments(p);
    if (peek(p) == ';') advance(p);

    int rc = call_builtin(fname, argv, argc);
    if (rc == 1) return 1; // exit
    if (rc > 1) return rc; // exit with error code
    return 0;
}

// ======== minimal control flow (if/while/break) ========
static int parse_expr_bool(parser_t *p) {
    lval_t a;
    if (!parse_value(p, &a)) return 0;
    skip_ws_and_comments(p);
    if (peek(p)=='=' && (p->pos+1<p->len) && p->src[p->pos+1]=='=') {
        p->pos+=2; // '=='
        lval_t b; if (!parse_value(p,&b)) { if (a.type==LVAL_STR) lval_free(&a); return 0; }
        int eq=0;
        if (a.type==LVAL_STR && b.type==LVAL_STR) eq = (strcmp(a.as.s? a.as.s: "", b.as.s? b.as.s: "")==0);
        else if (a.type==LVAL_INT && b.type==LVAL_INT) eq = (a.as.i == b.as.i);
        else if (a.type==LVAL_BOOL && b.type==LVAL_BOOL) eq = (a.as.b == b.as.b);
        if (a.type==LVAL_STR) lval_free(&a); if (b.type==LVAL_STR) lval_free(&b);
        return eq;
    }
    int truth = 0;
    if (a.type==LVAL_BOOL) truth = a.as.b;
    else if (a.type==LVAL_INT) truth = (a.as.i != 0);
    else if (a.type==LVAL_STR) truth = (a.as.s && a.as.s[0] != '\0');
    if (a.type==LVAL_STR) lval_free(&a);
    return truth;
}

static int g_break_flag = 0;
static void parse_block(parser_t *p);
static void parse_statement(parser_t *p) {
    skip_ws_and_comments(p);
    if (at_end(p)) return;
    if (strncmp(&p->src[p->pos], "break", 5)==0) { p->pos += 5; g_break_flag = 1; return; }
    if (strncmp(&p->src[p->pos], "while", 5)==0) {
        p->pos += 5; skip_ws_and_comments(p);
        u32 cond_pos = p->pos;
        int cond = parse_expr_bool(p);
        skip_ws_and_comments(p);
        if (strncmp(&p->src[p->pos], "do", 2)==0) p->pos += 2;
        u32 block_pos = p->pos;
        for (;;) {
            if (cond) {
                parse_block(p);
                if (g_break_flag) { g_break_flag = 0; skip_ws_and_comments(p); if (strncmp(&p->src[p->pos], "end", 3)==0) p->pos += 3; break; }
                // reset to block start
                p->pos = block_pos;
                // re-evaluate condition
                u32 saved = p->pos;
                p->pos = cond_pos;
                cond = parse_expr_bool(p);
                skip_ws_and_comments(p);
                if (strncmp(&p->src[p->pos], "do", 2)==0) p->pos += 2;
                block_pos = p->pos;
                p->pos = saved;
                continue;
            } else {
                // skip the block
                parse_block(p);
                skip_ws_and_comments(p);
                if (strncmp(&p->src[p->pos], "end", 3)==0) p->pos += 3;
                break;
            }
        }
        return;
    }
    if (strncmp(&p->src[p->pos], "if", 2)==0) {
        p->pos += 2; skip_ws_and_comments(p);
        int cond = parse_expr_bool(p);
        skip_ws_and_comments(p);
        if (strncmp(&p->src[p->pos], "then", 4)==0) p->pos += 4;
        if (cond) {
            parse_block(p);
            skip_ws_and_comments(p);
            if (strncmp(&p->src[p->pos], "else", 4)==0) { p->pos += 4; parse_block(p); }
            skip_ws_and_comments(p);
            if (strncmp(&p->src[p->pos], "end", 3)==0) p->pos += 3;
        } else {
            // skip then block
            parse_block(p);
            skip_ws_and_comments(p);
            if (strncmp(&p->src[p->pos], "else", 4)==0) { p->pos += 4; parse_block(p); }
            skip_ws_and_comments(p);
            if (strncmp(&p->src[p->pos], "end", 3)==0) p->pos += 3;
        }
        return;
    }
    // assignment or call
    if (is_ident_start(peek(p))) {
        // try to capture identifier
        u32 save = p->pos;
        char id[32]; if (!parse_ident(p, id, sizeof id)) { p->pos = save; }
        skip_ws_and_comments(p);
        if (peek(p) == '=') {
            advance(p);
            lval_t v; if (parse_value(p, &v)) var_set(id, &v);
            if (v.type==LVAL_STR) lval_free(&v);
            return;
        } else {
            // reset and parse as call
            p->pos = save;
            (void)parse_call(p);
            return;
        }
    }
    // fallback: call
    (void)parse_call(p);
}

static void parse_block(parser_t *p) {
    for (;;) {
        skip_ws_and_comments(p);
        if (at_end(p)) return;
        if (strncmp(&p->src[p->pos], "end", 3)==0) return;
        if (strncmp(&p->src[p->pos], "else", 4)==0) return;
        parse_statement(p);
        if (g_break_flag) return;
    }
}

static int run_source(const char *src, u32 len, const char *name) {
    parser_t P = { .src = src, .len = len, .pos = 0, .name = name };
    while (!at_end(&P)) { parse_statement(&P); skip_ws_and_comments(&P); }
    return 0;
}

// argv support
static const char **g_argv = NULL;
static int g_argc = 0;
static int cld_lua_argc(void) { return g_argc; }
static const char* cld_lua_argv_at(int idx) {
    if (idx < 0 || idx >= g_argc) return NULL;
    return g_argv[idx];
}

int cld_lua_run_file_with_args(const char *path, int argc, const char **argv) {
    if (!path || !*path) {
        vga_printf("lua: missing script path\n");
        return 1;
    }
    Node *file = cldramfs_resolve_path_file(path, 0);
    if (!file || file->type != FILE_NODE) {
        vga_printf("lua: file not found: %s\n", path);
        return 1;
    }
    if (!file->content) {
        vga_printf("lua: empty script: %s\n", path);
        return 1;
    }
    // Set argv
    g_argv = argv;
    g_argc = argc;
    int rc = run_source(file->content, file->content_size, path);
    g_argv = NULL; g_argc = 0;
    return rc;
}

int cld_lua_run_file(const char *path) {
    const char *argv0 = path;
    return cld_lua_run_file_with_args(path, 1, &argv0);
}

typedef struct {
    char *path;
    int argc;
    char **argv;
} lua_task_args_t;

// Deferred runner shim to integrate with shell's deferred queue
void cld_lua_run_deferred(void *arg) {
    const char *path = (const char*)arg;
    const char *argv0 = path;
    int rc = cld_lua_run_file_with_args(path, 1, &argv0);
    if (rc != 0) vga_printf("lua: exited with code %d\n", rc);
    if (arg) kfree(arg);
    // Resume shell input and prompt now that script completed
    shell_resume();
}

void cld_lua_run_deferred_with_args(void *arg) {
    lua_task_args_t *t = (lua_task_args_t*)arg;
    const char **argv = (const char**)t->argv;
    int rc = cld_lua_run_file_with_args(t->path, t->argc, argv);
    if (rc != 0) vga_printf("lua: exited with code %d\n", rc);
    // free
    if (t) {
        if (t->path) kfree(t->path);
        for (int i = 0; i < t->argc; i++) if (t->argv && t->argv[i]) kfree(t->argv[i]);
        if (t->argv) kfree(t->argv);
        kfree(t);
    }
    shell_resume();
}
