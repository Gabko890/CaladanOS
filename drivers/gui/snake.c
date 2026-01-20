#include "snake.h"
#include <fb/fb_console.h>
#include <kmalloc.h>
#include <pit/pit.h>
#include <ps2.h>

// Simple tick-based Snake game rendered inside a GUI window content rect.
// - Uses PIT ticks for timing via pit_set_callback()
// - Arrow keys change direction; Space pauses; R resets.

// Pixel rect (within window content)
static u32 s_px = 0, s_py = 0, s_pw = 0, s_ph = 0;

// Grid
static u32 cell_sz = 18; // pixels per cell (bigger squares)
static int grid_w = 0, grid_h = 0;

// Snake body (x,y arrays), with circular head/tail indices
static int *sx = 0, *sy = 0; // capacity = grid_w*grid_h
static int cap = 0;
static int head = 0; // index of head in arrays
static int len = 0;  // number of cells occupied

// Direction: 0=up,1=right,2=down,3=left
static volatile int dir = 1;
static volatile int paused = 0;
static volatile int game_over = 0;
static u64 game_over_until = 0; // absolute PIT tick deadline to end game-over screen

// Apple
static int ax = 0, ay = 0;

// Timing
static u32 step_ms = 90;  // time per step in ms (faster)
static u32 step_ticks = 0; // PIT ticks per step
static u32 tick_accum = 0;

// Colors
static const u8 COL_BG[3]   = { 0x20, 0x20, 0x22 };
static const u8 COL_GRID[3] = { 0x28, 0x28, 0x2B };
static const u8 COL_SNAKE[3]= { 0x55, 0xAA, 0x55 };
static const u8 COL_HEAD[3] = { 0x33, 0x88, 0x33 };
static const u8 COL_APPLE[3]= { 0xCC, 0x33, 0x33 };

static inline int modp(int a, int m) { int r = a % m; return r < 0 ? r + m : r; }

static void draw_cell(int gx, int gy, const u8 col[3]) {
    u32 x = s_px + (u32)gx * cell_sz;
    u32 y = s_py + (u32)gy * cell_sz;
    fb_fill_rect_rgb(x, y, cell_sz, cell_sz, col[0], col[1], col[2]);
}

static void clear_area(void) {
    fb_fill_rect_rgb(s_px, s_py, s_pw, s_ph, COL_BG[0], COL_BG[1], COL_BG[2]);
}

static void draw_grid(void) {
    // Subtle grid (vertical/horizontal separators)
    // Draw as thin lines between cells
    if (cell_sz < 6) return; // too dense otherwise
    u32 field_w_px = (u32)grid_w * cell_sz;
    u32 field_h_px = (u32)grid_h * cell_sz;
    for (int x = 1; x < grid_w; x++) {
        u32 px = s_px + (u32)x * cell_sz;
        fb_fill_rect_rgb(px, s_py, 1, field_h_px, COL_GRID[0], COL_GRID[1], COL_GRID[2]);
    }
    for (int y = 1; y < grid_h; y++) {
        u32 py = s_py + (u32)y * cell_sz;
        fb_fill_rect_rgb(s_px, py, field_w_px, 1, COL_GRID[0], COL_GRID[1], COL_GRID[2]);
    }
}

static void draw_text(u32 x, u32 y, const char* s, u8 vga_index) {
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 px = x;
    for (const char* p = s; p && *p; ++p) {
        fb_draw_char_px_nobg(px, y, *p, vga_index);
        px += (u32)cw;
    }
}

static u32 text_width_px(const char* s) {
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 n = 0; while (s && s[n]) n++;
    return (u32)cw * n;
}

static u32 lcg_state = 1;
static u32 lcg_next(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

static void place_apple(void) {
    // Try a few times to place apple on a free cell
    for (int tries = 0; tries < 128; tries++) {
        int x = (int)(lcg_next() % (u32)grid_w);
        int y = (int)(lcg_next() % (u32)grid_h);
        // Check snake occupancy
        int ok = 1;
        for (int i = 0; i < len; i++) {
            int idx = (head - i);
            if (idx < 0) idx += cap;
            if (sx[idx] == x && sy[idx] == y) { ok = 0; break; }
        }
        if (ok) { ax = x; ay = y; return; }
    }
    // Fallback
    ax = (int)((u32)grid_w / 2); ay = (int)((u32)grid_h / 2);
}

static void reset_game(void) {
    len = (grid_w > 4 ? 4 : grid_w);
    head = 0;
    int cx = grid_w / 3;
    int cy = grid_h / 2;
    // Default facing right, build body behind head so first forward step is safe
    dir = 1; // right
    int dx = 1, dy = 0;
    // Head at (cx,cy), tail extending to the left
    for (int i = 0; i < len; i++) {
        int idx = i; // fill increasing toward head
        sx[idx] = cx - ((len - 1 - i) * dx);
        sy[idx] = cy - ((len - 1 - i) * dy);
    }
    head = len - 1; // last index is head at (cx,cy)
    // Start paused; wait for first key press to begin movement
    paused = 1;
    game_over = 0;
    lcg_state = (u32)(pit_ticks() ^ 0xA5A5A5A5u);
    place_apple();
}

static void render_all(void) {
    clear_area();
    draw_grid();
    // Apple
    draw_cell(ax, ay, COL_APPLE);
    // Snake
    for (int i = 0; i < len; i++) {
        int idx = (head - i);
        if (idx < 0) idx += cap;
        const u8* col = (i == 0) ? COL_HEAD : COL_SNAKE;
        draw_cell(sx[idx], sy[idx], col);
    }
}

static void step_snake(void) {
    if (paused || len <= 0 || game_over) return;
    int hx = sx[head];
    int hy = sy[head];
    switch (dir) {
        case 0: hy -= 1; break;
        case 1: hx += 1; break;
        case 2: hy += 1; break;
        case 3: hx -= 1; break;
    }
    // Borders are walls: out-of-range is game over
    if (hx < 0 || hy < 0 || hx >= grid_w || hy >= grid_h) {
        paused = 1; game_over = 1;
        u32 hz = pit_get_hz(); if (!hz) hz = 1000;
        u64 wait = ((u64)2000 * (u64)hz) / 1000ULL; if (wait == 0) wait = 1;
        game_over_until = pit_ticks() + wait;
        clear_area();
        // Center "GAME OVER" in red
        const char* msg = "GAME OVER";
        u32 tw = text_width_px(msg);
        int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
        u32 tx = s_px + (s_pw > tw ? (s_pw - tw) / 2 : 0);
        u32 ty = s_py + (s_ph > (u32)ch ? (s_ph - (u32)ch) / 2 : 0);
        draw_text(tx, ty, msg, 0x0C); // bright red
        return;
    }

    // Collision with body? If so, reset
    for (int i = 0; i < len; i++) {
        int idx = (head - i);
        if (idx < 0) idx += cap;
        if (sx[idx] == hx && sy[idx] == hy) {
            paused = 1; game_over = 1;
            u32 hz = pit_get_hz(); if (!hz) hz = 1000;
            u64 wait = ((u64)2000 * (u64)hz) / 1000ULL; if (wait == 0) wait = 1;
            game_over_until = pit_ticks() + wait;
            clear_area();
            const char* msg = "GAME OVER";
            u32 tw = text_width_px(msg);
            int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
            u32 tx = s_px + (s_pw > tw ? (s_pw - tw) / 2 : 0);
            u32 ty = s_py + (s_ph > (u32)ch ? (s_ph - (u32)ch) / 2 : 0);
            draw_text(tx, ty, msg, 0x0C);
            return;
        }
    }

    // Advance head
    head = (head + 1) % cap;
    sx[head] = hx; sy[head] = hy;

    if (hx == ax && hy == ay) {
        // Grow if possible
        if (len < cap) len++;
        place_apple();
    } else {
        // Keep length: nothing to do, the implicit tail shrinks by overwriting on next wraps
        if (len < cap && len < head + 1) {
            // no-op; model keeps only last 'len' entries via traversal
        }
    }
}

static void on_pit_tick(void) {
    if (step_ticks == 0) return;
    // Handle game-over delay timing
    if (game_over) {
        if (pit_ticks() >= game_over_until) {
            game_over = 0;
            reset_game();
            render_all();
        }
        return;
    }
    tick_accum++;
    if (tick_accum >= step_ticks) {
        tick_accum -= step_ticks;
        step_snake();
        if (!game_over) {
            // Best-effort redraw; in IRQ context — keep it simple
            render_all();
        }
    }
}

void gui_snake_init(u32 px, u32 py, u32 pw, u32 ph) {
    s_px = px; s_py = py; s_pw = pw; s_ph = ph;
    if (cell_sz < 4) cell_sz = 4;
    // Derive grid size from window size; fewer cells when squares are bigger
    grid_w = (int)(pw / cell_sz);
    grid_h = (int)(ph / cell_sz);
    if (grid_w < 2) grid_w = 2;
    if (grid_h < 2) grid_h = 2;
    cap = grid_w * grid_h;
    // Allocate body buffers
    sx = (int*)kmalloc((size_t)cap * sizeof(int));
    sy = (int*)kmalloc((size_t)cap * sizeof(int));
    if (!sx || !sy) return;
    // Timing setup
    u32 hz = pit_get_hz();
    if (hz == 0) hz = 1000;
    step_ticks = (step_ms * hz) / 1000u; if (step_ticks == 0) step_ticks = 1;
    tick_accum = 0;
    // Game state
    reset_game();
    render_all();
    // Attach PIT callback
    pit_set_callback(on_pit_tick);
}

void gui_snake_move(u32 px, u32 py) {
    s_px = px; s_py = py;
}

void gui_snake_render_all(void) { render_all(); }

void gui_snake_handle_key(u8 sc, int is_extended, int is_pressed) {
    if (!is_pressed) return;
    if (is_extended) {
        switch (sc) {
            case US_ARROW_UP:    if (!game_over) { if (dir != 2) dir = 0; if (paused) paused = 0; } break;
            case US_ARROW_RIGHT: if (!game_over) { if (dir != 3) dir = 1; if (paused) paused = 0; } break;
            case US_ARROW_DOWN:  if (!game_over) { if (dir != 0) dir = 2; if (paused) paused = 0; } break;
            case US_ARROW_LEFT:  if (!game_over) { if (dir != 1) dir = 3; if (paused) paused = 0; } break;
            default: break;
        }
        return;
    }
    switch (sc) {
        case US_SPACE: paused = !paused; break;
        case US_R: reset_game(); render_all(); break;
        default: break;
    }
}

void gui_snake_free(void) {
    // Detach callback first to stop IRQ drawing
    pit_set_callback(0);
    if (sx) { kfree(sx); sx = 0; }
    if (sy) { kfree(sy); sy = 0; }
    cap = grid_w = grid_h = 0; len = 0; head = 0; tick_accum = 0;
}
