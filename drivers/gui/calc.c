#include "calc.h"
#include <fb/fb_console.h>
#include <cldtypes.h>
#include <ps2.h>
#include <cldramfs/tty.h>
#include <kmalloc.h>

// Simple fixed-point calculator with clickable GUI buttons.
// Supports: 0-9 digits, '.' decimal point, C (clear), +, -, *, /, =
// Uses 1e6 fixed-point scale for decimals (six fractional digits).

// Layout/region
static u32 c_px = 0, c_py = 0, c_pw = 0, c_ph = 0;

// Colors (muted theme to match other GUI)
static const u8 COL_BG[3]    = { 0xE0, 0xE0, 0xE0 }; // window content bg
static const u8 COL_DISP[3]  = { 0xF8, 0xF8, 0xF8 };
static const u8 COL_BORD[3]  = { 0x77, 0x77, 0x77 };
static const u8 COL_BTN[3]   = { 0xCC, 0xCC, 0xCC };
static const u8 COL_BTN_OP[3]= { 0xB8, 0xB8, 0xB8 };
static const u8 COL_TXT[3]   = { 0x00, 0x00, 0x00 };

// Fixed-point config and calculator state
static const long long SCALE = 1000000LL; // 6 decimals
static long long cur_val_s = 0;      // current value (scaled)
static int input_active = 0;         // 1 if entering digits
static char input_buf[64];           // current input as text (digits and at most one dot, optional leading '-')
static int input_len = 0;
static char pending_op = 0;          // '+','-','*','/' or 0
static int error_flag = 0;           // set on div-by-zero or overflow

// Display buffer
static char disp_buf[64];

// 128/64 helpers (avoid 128-bit division helpers from libgcc)
static void umul64_to_128(u64 a, u64 b, u64* hi, u64* lo) {
    unsigned __int128 p = (unsigned __int128)a * (unsigned __int128)b;
    *lo = (u64)p;
    *hi = (u64)(p >> 64);
}

static int udiv128by64(u64 hi, u64 lo, u64 d, u64* q_out, u64* r_out) {
    if (d == 0) return 0;
    u32 w[4];
    w[0] = (u32)(hi >> 32);
    w[1] = (u32)(hi & 0xFFFFFFFFu);
    w[2] = (u32)(lo >> 32);
    w[3] = (u32)(lo & 0xFFFFFFFFu);
    u32 qd[4] = {0,0,0,0};
    u64 rem = 0;
    for (int i = 0; i < 4; i++) {
        u64 cur = (rem << 32) | (u64)w[i];
        u64 q = cur / d;
        rem = cur % d;
        if (q > 0xFFFFFFFFull) q = 0xFFFFFFFFull; // clamp
        qd[i] = (u32)q;
    }
    if (qd[0] != 0 || qd[1] != 0) return -1; // overflow (more than 64-bit quotient)
    u64 q = ((u64)qd[2] << 32) | (u64)qd[3];
    if (q_out) *q_out = q;
    if (r_out) *r_out = rem;
    return 1;
}

// Button hitboxes
typedef struct { const char* label; int code; u32 x, y, w, h; } btn_t;
#define MAX_BTNS 24
static btn_t btns[MAX_BTNS];
static int btn_count = 0;

// Button codes
enum { BTN_NONE = 0,
       BTN_0 = '0', BTN_1 = '1', BTN_2 = '2', BTN_3 = '3', BTN_4 = '4', BTN_5 = '5', BTN_6 = '6', BTN_7 = '7', BTN_8 = '8', BTN_9 = '9',
       BTN_ADD = '+', BTN_SUB = '-', BTN_MUL = '*', BTN_DIV = '/', BTN_EQ = '=', BTN_CLR = 'C', BTN_DOT = '.' };

static void draw_rect(u32 x, u32 y, u32 w, u32 h, const u8 col[3]) {
    fb_fill_rect_rgb(x, y, w, h, col[0], col[1], col[2]);
}

static void draw_text(u32 x, u32 y, const char* s, u8 vga_idx) {
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 px = x;
    for (const char* p = s; p && *p; ++p) {
        fb_draw_char_px_nobg(px, y, *p, vga_idx);
        px += (u32)cw;
    }
}

static u32 text_width(const char* s) {
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 n = 0; while (s && s[n]) n++;
    return (u32)cw * n;
}

static int i64_to_str(long long v, char* out, u32 max) {
    if (!out || max == 0) return 0;
    if (error_flag) {
        const char* err = "ERR";
        u32 i = 0; while (err[i] && i + 1 < max) { out[i] = err[i]; i++; }
        out[i] = '\0';
        return (int)i;
    }
    char tmp[32];
    u32 pos = 0;
    unsigned long long x;
    int neg = 0;
    if (v < 0) { neg = 1; x = (unsigned long long)(-v); }
    else x = (unsigned long long)v;
    if (x == 0) { tmp[pos++] = '0'; }
    else {
        while (x && pos < sizeof(tmp)) { tmp[pos++] = (char)('0' + (x % 10)); x /= 10; }
    }
    u32 i = 0;
    if (neg && i + 1 < max) out[i++] = '-';
    while (pos && i + 1 < max) { out[i++] = tmp[--pos]; }
    out[i] = '\0';
    return (int)i;
}

static int scaled_to_str(long long sv, char* out, u32 max) {
    // Convert scaled integer (SCALE=1e6) to string with trimmed trailing zeros
    if (!out || max == 0) return 0;
    long long abs_v = (sv < 0) ? -sv : sv;
    long long int_part = abs_v / SCALE;
    long long frac_part = abs_v % SCALE;
    char tmpi[32];
    int pos = 0;
    if (sv < 0) { if (pos + 1 < (int)max) out[pos++] = '-'; }
    // integer part
    char ibuf[32];
    int ilen = i64_to_str(int_part, ibuf, sizeof(ibuf));
    // i64_to_str writes '-', avoid double minus; handle 0 specially
    int ibeg = 0; if (int_part == 0) { ibuf[0] = '0'; ibuf[1] = '\0'; ilen = 1; }
    for (int i = ibeg; i < ilen && pos + 1 < (int)max; i++) out[pos++] = ibuf[i];
    if (frac_part == 0) { out[pos] = '\0'; return pos; }
    if (pos + 1 >= (int)max) { out[max-1] = '\0'; return (int)max-1; }
    out[pos++] = '.';
    // Build fractional digits with exactly 6 then trim
    char fbuf[8];
    for (int i = 5; i >= 0; i--) { fbuf[i] = (char)('0' + (frac_part % 10)); frac_part /= 10; }
    int fend = 5; while (fend >= 0 && fbuf[fend] == '0') fend--;
    if (fend < 0) { // no fractional part actually
        pos--; out[pos] = '\0'; return pos;
    }
    for (int i = 0; i <= fend && pos + 1 < (int)max; i++) out[pos++] = fbuf[i];
    out[pos] = '\0';
    return pos;
}

static int parse_input_to_scaled(const char* s, long long* out_s) {
    if (!s || !*s) { if (out_s) *out_s = 0; return 1; }
    int neg = 0; u64 intp = 0; u64 fracp = 0; int frac_digits = 0; int seen_dot = 0;
    const char* p = s;
    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }
    if (!*p) { if (out_s) *out_s = 0; return 1; }
    while (*p) {
        if (*p == '.') {
            if (seen_dot) return 0; // invalid: multiple dots
            seen_dot = 1; p++; continue;
        }
        if (*p < '0' || *p > '9') return 0;
        int d = *p - '0';
        if (!seen_dot) {
            intp = intp * 10u + (u64)d;
        } else {
            if (frac_digits < 6) { fracp = fracp * 10u + (u64)d; frac_digits++; }
            // extra digits beyond precision are truncated
        }
        p++;
    }
    // Scale fractional to 6 digits
    while (frac_digits < 6) { fracp = fracp * 10u; frac_digits++; }
    __int128 sv = (__int128)intp * (__int128)SCALE + (__int128)fracp;
    if (neg) sv = -sv;
    if (sv > ( (__int128)0x7fffffffffffffffLL) || sv < -((__int128)0x8000000000000000LL)) return 0;
    if (out_s) *out_s = (long long)sv;
    return 1;
}

static void update_display_buffer(void) {
    if (error_flag) { disp_buf[0] = 'E'; disp_buf[1] = 'R'; disp_buf[2] = 'R'; disp_buf[3] = '\0'; return; }
    if (input_active) {
        if (input_len == 0) { disp_buf[0] = '0'; disp_buf[1] = '\0'; }
        else { // reflect input buffer directly
            u32 n = 0; while (n < sizeof(disp_buf)-1 && n < (u32)input_len) { disp_buf[n] = input_buf[n]; n++; }
            disp_buf[n] = '\0';
        }
    } else {
        (void)scaled_to_str(cur_val_s, disp_buf, sizeof(disp_buf));
    }
}

static void apply_op_scaled(long long rhs_s) {
    if (!pending_op) { cur_val_s = rhs_s; return; }
    switch (pending_op) {
        case '+': cur_val_s = cur_val_s + rhs_s; break;
        case '-': cur_val_s = cur_val_s - rhs_s; break;
        case '*': {
            u64 a = (cur_val_s < 0) ? (u64)(-cur_val_s) : (u64)cur_val_s;
            u64 b = (rhs_s     < 0) ? (u64)(-rhs_s)     : (u64)rhs_s;
            u64 hi = 0, lo = 0; umul64_to_128(a, b, &hi, &lo);
            u64 q = 0, r = 0;
            int rc = udiv128by64(hi, lo, (u64)SCALE, &q, &r);
            if (rc <= 0) { error_flag = 1; break; }
            long long signed_q = ( (cur_val_s ^ rhs_s) < 0 ) ? -(long long)q : (long long)q;
            // Check overflow (q must fit in signed 64)
            if ((signed_q < 0 && (u64)(-signed_q) > 0x8000000000000000ull) ||
                (signed_q > 0 && (u64)signed_q > 0x7fffffffffffffffull)) { error_flag = 1; break; }
            cur_val_s = signed_q;
            break; }
        case '/': {
            long long d_s = rhs_s;
            if (d_s == 0) { error_flag = 1; break; }
            u64 a = (cur_val_s < 0) ? (u64)(-cur_val_s) : (u64)cur_val_s;
            u64 b = (d_s       < 0) ? (u64)(-d_s)       : (u64)d_s;
            // a * SCALE
            u64 hi = 0, lo = 0; umul64_to_128(a, (u64)SCALE, &hi, &lo);
            u64 q = 0, r = 0;
            int rc = udiv128by64(hi, lo, b, &q, &r);
            if (rc <= 0) { error_flag = 1; break; }
            long long signed_q = ( (cur_val_s ^ d_s) < 0 ) ? -(long long)q : (long long)q;
            if ((signed_q < 0 && (u64)(-signed_q) > 0x8000000000000000ull) ||
                (signed_q > 0 && (u64)signed_q > 0x7fffffffffffffffull)) { error_flag = 1; break; }
            cur_val_s = signed_q;
            break; }
        default: break;
    }
}

static void reset_all(void) {
    cur_val_s = 0; input_active = 0; pending_op = 0; error_flag = 0;
    input_len = 0; input_buf[0] = '\0';
    update_display_buffer();
}

static void layout_buttons(void) {
    // 4x5 grid (last row only '=' in rightmost cell):
    // Row0: 7 8 9 /
    // Row1: 4 5 6 *
    // Row2: 1 2 3 -
    // Row3: C 0 . +
    // Row4: [ ] [ ] [ ] =
    btn_count = 0;
    u32 pad = 6; u32 gap = 6;
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 disp_h = (u32)ch * 2 + 8; // display area height
    // Guard for very small regions
    if (c_pw < 60 || c_ph < disp_h + 5*20 + 5*gap + pad*2) {
        disp_h = (u32)((c_ph > 100) ? (c_ph / 4) : (c_ph > 40 ? 40 : c_ph / 2));
    }
    u32 grid_x = c_px + pad;
    u32 grid_y = c_py + pad + disp_h + gap;
    u32 grid_w = (c_pw > 2*pad ? c_pw - 2*pad : 0);
    u32 grid_h = (c_ph > (disp_h + 2*pad + gap) ? (c_ph - (disp_h + 2*pad + gap)) : 0);
    if (grid_w < 20 || grid_h < 20) return;
    u32 cols = 4, rows = 5;
    u32 cell_w = (grid_w - gap * (cols - 1)) / cols;
    u32 cell_h = (grid_h - gap * (rows - 1)) / rows;
    const char* labels[5][4] = {
        {"7","8","9","/"},
        {"4","5","6","*"},
        {"1","2","3","-"},
        {"C","0",".","+"},
        {0, 0, 0, "="}
    };
    int codes[5][4] = {
        {BTN_7, BTN_8, BTN_9, BTN_DIV},
        {BTN_4, BTN_5, BTN_6, BTN_MUL},
        {BTN_1, BTN_2, BTN_3, BTN_SUB},
        {BTN_CLR, BTN_0, BTN_DOT, BTN_ADD},
        {BTN_NONE, BTN_NONE, BTN_NONE, BTN_EQ}
    };
    for (u32 r = 0; r < rows; r++) {
        for (u32 c = 0; c < cols; c++) {
            if (btn_count >= MAX_BTNS) break;
            if (codes[r][c] == BTN_NONE || labels[r][c] == 0) continue;
            u32 bx = grid_x + c * (cell_w + gap);
            u32 by = grid_y + r * (cell_h + gap);
            btns[btn_count].label = labels[r][c];
            btns[btn_count].code = codes[r][c];
            btns[btn_count].x = bx; btns[btn_count].y = by; btns[btn_count].w = cell_w; btns[btn_count].h = cell_h;
            btn_count++;
        }
    }
}

static void render_buttons(void) {
    for (int i = 0; i < btn_count; i++) {
        const u8* col = ((btns[i].code == BTN_ADD || btns[i].code == BTN_SUB || btns[i].code == BTN_MUL || btns[i].code == BTN_DIV || btns[i].code == BTN_EQ) ? COL_BTN_OP : COL_BTN);
        draw_rect(btns[i].x, btns[i].y, btns[i].w, btns[i].h, col);
        // Border
        fb_fill_rect_rgb(btns[i].x, btns[i].y, btns[i].w, 1, COL_BORD[0], COL_BORD[1], COL_BORD[2]);
        fb_fill_rect_rgb(btns[i].x, btns[i].y + btns[i].h - 1, btns[i].w, 1, COL_BORD[0], COL_BORD[1], COL_BORD[2]);
        fb_fill_rect_rgb(btns[i].x, btns[i].y, 1, btns[i].h, COL_BORD[0], COL_BORD[1], COL_BORD[2]);
        fb_fill_rect_rgb(btns[i].x + btns[i].w - 1, btns[i].y, 1, btns[i].h, COL_BORD[0], COL_BORD[1], COL_BORD[2]);
        // Label centered
        u32 tw = text_width(btns[i].label);
        int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
        u32 tx = btns[i].x + (btns[i].w > tw ? (btns[i].w - tw) / 2 : 0);
        u32 ty = btns[i].y + (btns[i].h > (u32)ch ? (btns[i].h - (u32)ch) / 2 : 0);
        draw_text(tx, ty, btns[i].label, 0x00);
    }
}

static void render_display(void) {
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 disp_h = (u32)ch * 2 + 8;
    // Background
    draw_rect(c_px, c_py, c_pw, disp_h, COL_DISP);
    // Border
    fb_fill_rect_rgb(c_px, c_py, c_pw, 1, COL_BORD[0], COL_BORD[1], COL_BORD[2]);
    fb_fill_rect_rgb(c_px, c_py + disp_h - 1, c_pw, 1, COL_BORD[0], COL_BORD[1], COL_BORD[2]);
    fb_fill_rect_rgb(c_px, c_py, 1, disp_h, COL_BORD[0], COL_BORD[1], COL_BORD[2]);
    fb_fill_rect_rgb(c_px + c_pw - 1, c_py, 1, disp_h, COL_BORD[0], COL_BORD[1], COL_BORD[2]);
    // Right-aligned text
    update_display_buffer();
    u32 tw = text_width(disp_buf);
    u32 pad = 8;
    u32 tx = (c_pw > tw + pad ? (c_px + c_pw - tw - pad) : c_px);
    u32 ty = c_py + (disp_h > (u32)ch ? (disp_h - (u32)ch) / 2 : 0);
    draw_text(tx, ty, disp_buf, 0x00);
}

void gui_calc_init(u32 px, u32 py, u32 pw, u32 ph) {
    c_px = px; c_py = py; c_pw = pw; c_ph = ph;
    reset_all();
    draw_rect(c_px, c_py, c_pw, c_ph, COL_BG);
    layout_buttons();
    render_display();
    render_buttons();
}

void gui_calc_move(u32 px, u32 py) {
    c_px = px; c_py = py;
}

void gui_calc_render_all(void) {
    draw_rect(c_px, c_py, c_pw, c_ph, COL_BG);
    layout_buttons();
    render_display();
    render_buttons();
}

static void press_digit(int d) {
    if (error_flag) { reset_all(); }
    if (!input_active) { input_active = 1; input_len = 0; input_buf[0] = '\0'; }
    if (input_len < (int)sizeof(input_buf) - 1) {
        input_buf[input_len++] = (char)('0' + d);
        input_buf[input_len] = '\0';
    }
}

static void press_dot(void) {
    if (error_flag) { reset_all(); }
    if (!input_active) { input_active = 1; input_len = 0; input_buf[0] = '0'; input_buf[1] = '\0'; input_len = 1; }
    // Do not add more than one dot
    for (int i = 0; i < input_len; i++) if (input_buf[i] == '.') return;
    if (input_len < (int)sizeof(input_buf) - 1) { input_buf[input_len++] = '.'; input_buf[input_len] = '\0'; }
}

static void press_op(char op) {
    if (error_flag) { reset_all(); }
    if (input_active) {
        long long rhs = 0; if (!parse_input_to_scaled(input_buf, &rhs)) { error_flag = 1; }
        else { apply_op_scaled(rhs); }
        input_active = 0; input_len = 0; input_buf[0] = '\0';
    }
    pending_op = op;
}

static void press_eq(void) {
    if (error_flag) { reset_all(); return; }
    if (input_active) {
        long long rhs = 0; if (!parse_input_to_scaled(input_buf, &rhs)) { error_flag = 1; }
        else { apply_op_scaled(rhs); }
        input_active = 0; input_len = 0; input_buf[0] = '\0';
    }
    pending_op = 0;
}

int gui_calc_on_click(u32 px, u32 py) {
    // Find button
    for (int i = 0; i < btn_count; i++) {
        u32 x = btns[i].x, y = btns[i].y, w = btns[i].w, h = btns[i].h;
        if (px >= x && px < x + w && py >= y && py < y + h) {
            int code = btns[i].code;
            if (code >= '0' && code <= '9') { press_digit(code - '0'); gui_calc_render_all(); return 1; }
            switch (code) {
                case BTN_CLR: reset_all(); gui_calc_render_all(); return 1;
                case BTN_ADD: press_op('+'); gui_calc_render_all(); return 1;
                case BTN_SUB: press_op('-'); gui_calc_render_all(); return 1;
                case BTN_MUL: press_op('*'); gui_calc_render_all(); return 1;
                case BTN_DIV: press_op('/'); gui_calc_render_all(); return 1;
                case BTN_DOT: press_dot(); gui_calc_render_all(); return 1;
                case BTN_EQ:  press_eq(); gui_calc_render_all(); return 1;
                default: break;
            }
            break;
        }
    }
    return 0;
}

void gui_calc_handle_key(u8 sc, int is_ext, int is_pressed) {
    (void)is_ext;
    if (!is_pressed) return;
    // Map to printable char (respect shift);
    u128 ka = ps2_keyarr();
    int shift = (ka & ((u128)1 << 0x2A)) || (ka & ((u128)1 << 0x36));
    extern char scancode_to_char(u8, int);
    char c = scancode_to_char(sc, shift);
    if (c >= '0' && c <= '9') { press_digit(c - '0'); gui_calc_render_all(); return; }
    if (c == '+' || c == '-' || c == '*' || c == '/') { press_op(c); gui_calc_render_all(); return; }
    if (c == '=') { press_eq(); gui_calc_render_all(); return; }
    if (c == '.') { press_dot(); gui_calc_render_all(); return; }
    if (sc == US_ENTER) { press_eq(); gui_calc_render_all(); return; }
    if (c == 'c' || c == 'C') { reset_all(); gui_calc_render_all(); return; }
}

void gui_calc_free(void) {
    // nothing to free yet
}
