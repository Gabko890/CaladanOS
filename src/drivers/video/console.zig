const std = @import("std");
const fmt = std.fmt;
const Writer = std.io.Writer;

pub const VGA_WIDTH = 80;
pub const VGA_HEIGHT = 25;

pub const ConsoleColors = enum(u8) {
    Black = 0,
    Blue = 1,
    Green = 2,
    Cyan = 3,
    Red = 4,
    Magenta = 5,
    Brown = 6,
    LightGray = 7,
    DarkGray = 8,
    LightBlue = 9,
    LightGreen = 10,
    LightCyan = 11,
    LightRed = 12,
    LightMagenta = 13,
    LightBrown = 14,
    White = 15,
};

const glyph_pixel_width: usize = 8;
const glyph_pixel_height: usize = 16;

const Glyph = [glyph_pixel_height]u8;
const ASCII_CASE_DELTA: u8 = @intCast('a' - 'A');

const glyph_chars = " !\"()+,-./0123456789:=?ABCDEFGHIJKLMNOPQRSTUVWXYZ\\_";
const glyph_table = [_]Glyph{
    .{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000 }, // ' '
    .{ 0b00000000, 0b00000000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00000000, 0b00000000, 0b00001000, 0b00001000 }, // '!'
    .{ 0b00000000, 0b00000000, 0b00010100, 0b00010100, 0b00010100, 0b00010100, 0b00001000, 0b00001000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000 }, // '"'
    .{ 0b00000000, 0b00000000, 0b00000100, 0b00000100, 0b00001000, 0b00001000, 0b00010000, 0b00010000, 0b00010000, 0b00010000, 0b00010000, 0b00010000, 0b00001000, 0b00001000, 0b00000100, 0b00000100 }, // '('
    .{ 0b00000000, 0b00000000, 0b00010000, 0b00010000, 0b00001000, 0b00001000, 0b00000100, 0b00000100, 0b00000100, 0b00000100, 0b00000100, 0b00000100, 0b00001000, 0b00001000, 0b00010000, 0b00010000 }, // ')'
    .{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00111110, 0b00111110, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00000000, 0b00000000 }, // '+'
    .{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00001100, 0b00001100, 0b00001000, 0b00001000 }, // ','
    .{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000 }, // '-'
    .{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00001100, 0b00001100, 0b00001100, 0b00001100 }, // '.'
    .{ 0b00000000, 0b00000000, 0b00000010, 0b00000010, 0b00000100, 0b00000100, 0b00001000, 0b00001000, 0b00010000, 0b00010000, 0b00100000, 0b00100000, 0b00000000, 0b00000000, 0b00000000, 0b00000000 }, // '/'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00100110, 0b00100110, 0b00101010, 0b00101010, 0b00110010, 0b00110010, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // '0'
    .{ 0b00000000, 0b00000000, 0b00001000, 0b00001000, 0b00011000, 0b00011000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00011100, 0b00011100 }, // '1'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00000010, 0b00000010, 0b00000100, 0b00000100, 0b00011000, 0b00011000, 0b00100000, 0b00100000, 0b00111110, 0b00111110 }, // '2'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00000010, 0b00000010, 0b00001100, 0b00001100, 0b00000010, 0b00000010, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // '3'
    .{ 0b00000000, 0b00000000, 0b00000100, 0b00000100, 0b00001100, 0b00001100, 0b00010100, 0b00010100, 0b00100100, 0b00100100, 0b00111110, 0b00111110, 0b00000100, 0b00000100, 0b00000100, 0b00000100 }, // '4'
    .{ 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00100000, 0b00100000, 0b00111100, 0b00111100, 0b00000010, 0b00000010, 0b00000010, 0b00000010, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // '5'
    .{ 0b00000000, 0b00000000, 0b00001100, 0b00001100, 0b00010000, 0b00010000, 0b00100000, 0b00100000, 0b00111100, 0b00111100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // '6'
    .{ 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00000010, 0b00000010, 0b00000100, 0b00000100, 0b00001000, 0b00001000, 0b00010000, 0b00010000, 0b00010000, 0b00010000, 0b00010000, 0b00010000 }, // '7'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // '8'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00011110, 0b00011110, 0b00000010, 0b00000010, 0b00000100, 0b00000100, 0b00011000, 0b00011000 }, // '9'
    .{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00001100, 0b00001100, 0b00001100, 0b00001100, 0b00000000, 0b00000000, 0b00001100, 0b00001100, 0b00001100, 0b00001100, 0b00000000, 0b00000000 }, // ':'
    .{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00000000, 0b00000000, 0b00000000, 0b00000000 }, // '='
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00000010, 0b00000010, 0b00000100, 0b00000100, 0b00001000, 0b00001000, 0b00000000, 0b00000000, 0b00001000, 0b00001000 }, // '?'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00111110, 0b00111110, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010 }, // 'A'
    .{ 0b00000000, 0b00000000, 0b00111100, 0b00111100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00111100, 0b00111100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00111100, 0b00111100 }, // 'B'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // 'C'
    .{ 0b00000000, 0b00000000, 0b00111000, 0b00111000, 0b00100100, 0b00100100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100100, 0b00100100, 0b00111000, 0b00111000 }, // 'D'
    .{ 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00111100, 0b00111100, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00111110, 0b00111110 }, // 'E'
    .{ 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00111100, 0b00111100, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000 }, // 'F'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100110, 0b00100110, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // 'G'
    .{ 0b00000000, 0b00000000, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00111110, 0b00111110, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010 }, // 'H'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00011100, 0b00011100 }, // 'I'
    .{ 0b00000000, 0b00000000, 0b00000110, 0b00000110, 0b00000010, 0b00000010, 0b00000010, 0b00000010, 0b00000010, 0b00000010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // 'J'
    .{ 0b00000000, 0b00000000, 0b00100010, 0b00100010, 0b00100100, 0b00100100, 0b00101000, 0b00101000, 0b00110000, 0b00110000, 0b00101000, 0b00101000, 0b00100100, 0b00100100, 0b00100010, 0b00100010 }, // 'K'
    .{ 0b00000000, 0b00000000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00111110, 0b00111110 }, // 'L'
    .{ 0b00000000, 0b00000000, 0b00100010, 0b00100010, 0b00110110, 0b00110110, 0b00101010, 0b00101010, 0b00101010, 0b00101010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010 }, // 'M'
    .{ 0b00000000, 0b00000000, 0b00100010, 0b00100010, 0b00110010, 0b00110010, 0b00101010, 0b00101010, 0b00100110, 0b00100110, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010 }, // 'N'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // 'O'
    .{ 0b00000000, 0b00000000, 0b00111100, 0b00111100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00111100, 0b00111100, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00100000 }, // 'P'
    .{ 0b00000000, 0b00000000, 0b00011100, 0b00011100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00101010, 0b00101010, 0b00100100, 0b00100100, 0b00011010, 0b00011010 }, // 'Q'
    .{ 0b00000000, 0b00000000, 0b00111100, 0b00111100, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00111100, 0b00111100, 0b00101000, 0b00101000, 0b00100100, 0b00100100, 0b00100010, 0b00100010 }, // 'R'
    .{ 0b00000000, 0b00000000, 0b00011110, 0b00011110, 0b00100000, 0b00100000, 0b00100000, 0b00100000, 0b00011100, 0b00011100, 0b00000010, 0b00000010, 0b00000010, 0b00000010, 0b00111100, 0b00111100 }, // 'S'
    .{ 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000 }, // 'T'
    .{ 0b00000000, 0b00000000, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00011100, 0b00011100 }, // 'U'
    .{ 0b00000000, 0b00000000, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00010100, 0b00010100, 0b00010100, 0b00010100, 0b00001000, 0b00001000 }, // 'V'
    .{ 0b00000000, 0b00000000, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00101010, 0b00101010, 0b00101010, 0b00101010, 0b00101010, 0b00101010, 0b00010100, 0b00010100 }, // 'W'
    .{ 0b00000000, 0b00000000, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00010100, 0b00010100, 0b00001000, 0b00001000, 0b00010100, 0b00010100, 0b00100010, 0b00100010, 0b00100010, 0b00100010 }, // 'X'
    .{ 0b00000000, 0b00000000, 0b00100010, 0b00100010, 0b00100010, 0b00100010, 0b00010100, 0b00010100, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000 }, // 'Y'
    .{ 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00000010, 0b00000010, 0b00000100, 0b00000100, 0b00001000, 0b00001000, 0b00010000, 0b00010000, 0b00100000, 0b00100000, 0b00111110, 0b00111110 }, // 'Z'
    .{ 0b00000000, 0b00000000, 0b00100000, 0b00100000, 0b00010000, 0b00010000, 0b00001000, 0b00001000, 0b00000100, 0b00000100, 0b00000010, 0b00000010, 0b00000000, 0b00000000, 0b00000000, 0b00000000 }, // '\\'
    .{ 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00111110, 0b00111110, 0b00111110, 0b00111110 }, // '_'
};

const fallback_glyph = blk: {
    for (glyph_chars, 0..) |ch, idx| {
        if (ch == '?') break :blk glyph_table[idx];
    }
    @compileError("Fallback glyph '?' missing from glyph table");
};

const palette = [_][3]u8{
    .{ 0x00, 0x00, 0x00 }, // Black
    .{ 0x00, 0x00, 0xAA }, // Blue
    .{ 0x00, 0xAA, 0x00 }, // Green
    .{ 0x00, 0xAA, 0xAA }, // Cyan
    .{ 0xAA, 0x00, 0x00 }, // Red
    .{ 0xAA, 0x00, 0xAA }, // Magenta
    .{ 0xAA, 0x55, 0x00 }, // Brown
    .{ 0xAA, 0xAA, 0xAA }, // LightGray
    .{ 0x55, 0x55, 0x55 }, // DarkGray
    .{ 0x55, 0x55, 0xFF }, // LightBlue
    .{ 0x55, 0xFF, 0x55 }, // LightGreen
    .{ 0x55, 0xFF, 0xFF }, // LightCyan
    .{ 0xFF, 0x55, 0x55 }, // LightRed
    .{ 0xFF, 0x55, 0xFF }, // LightMagenta
    .{ 0xFF, 0xFF, 0x55 }, // LightBrown / Yellow
    .{ 0xFF, 0xFF, 0xFF }, // White
};

const Backend = enum {
    text,
    graphics,
};

const GraphicsState = struct {
    buffer: [*]volatile u8,
    pitch: usize,
    fb_width: usize,
    fb_height: usize,
    bytes_per_pixel: usize,
};

var backend: Backend = .text;
var row: usize = 0;
var column: usize = 0;
var color = vgaEntryColor(ConsoleColors.LightGray, ConsoleColors.Black);
var text_buffer: ?[*]volatile u16 = null;
var gfx_state: ?GraphicsState = null;
var width: usize = VGA_WIDTH;
var height: usize = VGA_HEIGHT;

fn vgaEntryColor(fg: ConsoleColors, bg: ConsoleColors) u8 {
    return @intFromEnum(fg) | (@intFromEnum(bg) << 4);
}

fn vgaEntry(uc: u8, new_color: u8) u16 {
    const c: u16 = new_color;
    return uc | (c << 8);
}

pub fn initializeLegacy() void {
    initializeText(@ptrFromInt(0xB8000), VGA_WIDTH, VGA_HEIGHT);
}

pub fn initializeText(ptr: [*]volatile u16, w: u32, h: u32) void {
    backend = .text;
    text_buffer = ptr;
    gfx_state = null;
    width = @intCast(w);
    height = @intCast(h);
    row = 0;
    column = 0;
    clear();
}

pub fn initializeFramebuffer(ptr: [*]volatile u8, pitch: u32, w: u32, h: u32, bpp: u8) void {
    backend = .graphics;
    text_buffer = null;
    const bpp_usize: usize = @intCast(bpp);
    gfx_state = GraphicsState{
        .buffer = ptr,
        .pitch = @intCast(pitch),
        .fb_width = @intCast(w),
        .fb_height = @intCast(h),
        .bytes_per_pixel = @max(1, (bpp_usize + 7) / 8),
    };

    const glyph_w: u32 = @intCast(glyph_pixel_width);
    const glyph_h: u32 = @intCast(glyph_pixel_height);
    const cols = if (w / glyph_w == 0) 1 else w / glyph_w;
    const rows = if (h / glyph_h == 0) 1 else h / glyph_h;
    width = @intCast(cols);
    height = @intCast(rows);
    row = 0;
    column = 0;
    clear();
}

pub fn setColor(new_color: u8) void {
    color = new_color;
}

pub fn clear() void {
    switch (backend) {
        .text => {
            const buf = text_buffer orelse return;
            const size = width * height;
            @memset(buf[0..size], vgaEntry(' ', color));
        },
        .graphics => {
            if (gfx_state) |*state| {
                const background = paletteColor(backgroundIndex(color));
                fillRect(state, 0, 0, state.fb_width, state.fb_height, background);
            }
        },
    }
}

pub fn putCharAt(c: u8, new_color: u8, x: usize, y: usize) void {
    if (x >= width or y >= height) return;

    switch (backend) {
        .text => {
            const buf = text_buffer orelse return;
            const index = y * width + x;
            buf[index] = vgaEntry(c, new_color);
        },
        .graphics => {
            if (gfx_state) |*state| {
                const glyph = glyphFor(c);
                const fg = paletteColor(foregroundIndex(new_color));
                const bg = paletteColor(backgroundIndex(new_color));
                drawGlyph(state, x, y, glyph, fg, bg);
            }
        },
    }
}

pub fn putChar(c: u8) void {
    if (c == '\n') {
        column = 0;
        row = (row + 1) % height;
        return;
    }

    putCharAt(c, color, column, row);
    column += 1;
    if (column == width) {
        column = 0;
        row = (row + 1) % height;
    }
}

fn foregroundIndex(value: u8) u8 {
    return value & 0x0F;
}

fn backgroundIndex(value: u8) u8 {
    return (value >> 4) & 0x0F;
}

fn paletteColor(index: u8) [3]u8 {
    const idx: usize = @intCast(index & 0x0F);
    return palette[idx];
}

fn glyphFor(ch: u8) Glyph {
    const upper = if (ch >= 'a' and ch <= 'z') ch - ASCII_CASE_DELTA else ch;
    inline for (glyph_chars, 0..) |candidate, idx| {
        if (candidate == upper) return glyph_table[idx];
    }
    return fallback_glyph;
}

fn drawGlyph(state: *GraphicsState, cell_x: usize, cell_y: usize, glyph: Glyph, fg: [3]u8, bg: [3]u8) void {
    const px = cell_x * glyph_pixel_width;
    const py = cell_y * glyph_pixel_height;
    fillRect(state, px, py, glyph_pixel_width, glyph_pixel_height, bg);

    var row_idx: usize = 0;
    while (row_idx < glyph_pixel_height and py + row_idx < state.fb_height) : (row_idx += 1) {
        const bits = glyph[row_idx];
        var col_idx: usize = 0;
        while (col_idx < glyph_pixel_width and px + col_idx < state.fb_width) : (col_idx += 1) {
            const shift: u3 = @intCast(col_idx & 7);
            const mask: u8 = @as(u8, 0x80) >> shift;
            if ((bits & mask) != 0) {
                setPixel(state, px + col_idx, py + row_idx, fg);
            }
        }
    }
}

fn fillRect(state: *GraphicsState, start_x: usize, start_y: usize, rect_w: usize, rect_h: usize, fill_rgb: [3]u8) void {
    var y = start_y;
    while (y < start_y + rect_h and y < state.fb_height) : (y += 1) {
        var x = start_x;
        while (x < start_x + rect_w and x < state.fb_width) : (x += 1) {
            setPixel(state, x, y, fill_rgb);
        }
    }
}

fn setPixel(state: *GraphicsState, x: usize, y: usize, rgb: [3]u8) void {
    if (x >= state.fb_width or y >= state.fb_height) return;
    const offset = y * state.pitch + x * state.bytes_per_pixel;
    const pixel_ptr = state.buffer + offset;

    switch (state.bytes_per_pixel) {
        4 => {
            pixel_ptr[0] = rgb[2];
            pixel_ptr[1] = rgb[1];
            pixel_ptr[2] = rgb[0];
            pixel_ptr[3] = 0xFF;
        },
        3 => {
            pixel_ptr[0] = rgb[2];
            pixel_ptr[1] = rgb[1];
            pixel_ptr[2] = rgb[0];
        },
        2 => {
            const r: u16 = (@as(u16, rgb[0]) & 0xF8) << 8;
            const g: u16 = (@as(u16, rgb[1]) & 0xFC) << 3;
            const b: u16 = (@as(u16, rgb[2]) & 0xF8) >> 3;
            const value: u16 = r | g | b;
            pixel_ptr[0] = @intCast(value & 0x00FF);
            pixel_ptr[1] = @intCast(value >> 8);
        },
        else => {},
    }
}

pub fn puts(data: []const u8) void {
    for (data) |c|
        putChar(c);
}

pub const writer = Writer(void, error{}, callback){ .context = {} };

fn callback(_: void, string: []const u8) error{}!usize {
    puts(string);
    return string.len;
}

pub fn printf(comptime format: []const u8, args: anytype) void {
    fmt.format(writer, format, args) catch unreachable;
}
