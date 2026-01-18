#include "lua_interp.h"

#include <vgaio.h>
#include <string.h>
#include <cldramfs/cldramfs.h>
#include <cldramfs/tty.h>
#include <ps2.h>
#include <shell_control.h>
#include <kmalloc.h>

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
        if (strcmp(id, "last") == 0) {
            out->type = LVAL_STR; out->as.s = g_last_input ? g_last_input : "";
            return 1;
        }
        // Unknown identifier as value is not supported
        return 0;
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

static int call_builtin(const char *name, lval_t *args, int argc) {
    if (strcmp(name, "print") == 0) { builtin_print(args, argc); return 0; }
    if (strcmp(name, "readfile") == 0) { builtin_readfile(args, argc); return 0; }
    if (strcmp(name, "writefile") == 0) { builtin_writefile(args, argc, 0); return 0; }
    if (strcmp(name, "appendfile") == 0) { builtin_writefile(args, argc, 1); return 0; }
    if (strcmp(name, "input") == 0) { builtin_input(args, argc); return 0; }
    if (strcmp(name, "getch") == 0) { builtin_getch(args, argc); return 0; }
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

static int run_source(const char *src, u32 len, const char *name) {
    parser_t P = { .src = src, .len = len, .pos = 0, .name = name };
    while (!at_end(&P)) {
        skip_ws_and_comments(&P);
        if (at_end(&P)) break;
        int rc = parse_call(&P);
        if (rc < 0) return 1; // parse error
        if (rc > 0) return (rc == 1) ? 0 : rc; // exit/exit code
        skip_ws_and_comments(&P);
    }
    return 0;
}

int cld_lua_run_file(const char *path) {
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
    return run_source(file->content, file->content_size, path);
}

// Deferred runner shim to integrate with shell's deferred queue
void cld_lua_run_deferred(void *arg) {
    const char *path = (const char*)arg;
    int rc = cld_lua_run_file(path);
    if (rc != 0) vga_printf("lua: exited with code %d\n", rc);
    if (arg) kfree(arg);
    // Resume shell input and prompt now that script completed
    shell_resume();
}
