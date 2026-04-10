#include "png.h"

#define LODEPNG_NO_COMPILE_DISK
#define LODEPNG_NO_COMPILE_ENCODER
#define LODEPNG_NO_COMPILE_CPP
#define LODEPNG_NO_COMPILE_ALLOCATORS

#include <lodepng.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>

#define GUI_PNG_MAX_PIXELS (1920u * 1080u)

void* lodepng_malloc(size_t size);
void* lodepng_realloc(void *ptr, size_t new_size);
void lodepng_free(void *ptr);

static char g_png_last_error[96] = "no PNG load attempted";

void* lodepng_malloc(size_t size) {
    return kmalloc(size);
}

void* lodepng_realloc(void *ptr, size_t new_size) {
    return krealloc(ptr, new_size);
}

void lodepng_free(void *ptr) {
    kfree(ptr);
}

static void png_set_error(const char *msg) {
    if (!msg) msg = "unknown PNG error";
    strncpy(g_png_last_error, msg, sizeof(g_png_last_error) - 1);
    g_png_last_error[sizeof(g_png_last_error) - 1] = '\0';
}

const char *gui_png_last_error(void) {
    return g_png_last_error;
}

int gui_png_is_png(const void *data, u32 size) {
    static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (!data || size < sizeof(sig)) return 0;
    return memcmp(data, sig, sizeof(sig)) == 0;
}

int gui_png_load(const void *data, u32 size, gui_png_t *out) {
    if (!out) {
        png_set_error("internal error: null output");
        return 0;
    }
    if (!data) {
        png_set_error("file has no content");
        return 0;
    }
    if (!gui_png_is_png(data, size)) {
        png_set_error(size < 8 ? "file is too small for PNG signature" : "file is not a PNG");
        return 0;
    }

    memset(out, 0, sizeof(*out));

    unsigned w = 0, h = 0;
    LodePNGState state;
    lodepng_state_init(&state);
    unsigned error = lodepng_inspect(&w, &h, &state, (const unsigned char*)data, (size_t)size);
    lodepng_state_cleanup(&state);
    if (error) {
        snprintf(g_png_last_error, sizeof(g_png_last_error), "LodePNG %u: %s", error, lodepng_error_text(error));
        return 0;
    }
    if (w == 0 || h == 0) {
        png_set_error("PNG has empty dimensions");
        return 0;
    }
    if ((u64)w * (u64)h > GUI_PNG_MAX_PIXELS) {
        snprintf(g_png_last_error, sizeof(g_png_last_error), "PNG too large: %ux%u, max %u pixels", w, h, GUI_PNG_MAX_PIXELS);
        return 0;
    }

    unsigned char *rgba = 0;
    error = lodepng_decode32(&rgba, &w, &h, (const unsigned char*)data, (size_t)size);
    if (error || !rgba || w == 0 || h == 0) {
        if (rgba) kfree(rgba);
        if (error) {
            snprintf(g_png_last_error, sizeof(g_png_last_error), "LodePNG %u: %s", error, lodepng_error_text(error));
        } else {
            png_set_error("decoded PNG has empty image data");
        }
        return 0;
    }

    out->rgba = (u8*)rgba;
    out->width = (u32)w;
    out->height = (u32)h;
    out->has_alpha = 0;
    for (u64 i = 0; i < (u64)w * (u64)h; i++) {
        if (out->rgba[i * 4u + 3u] != 0xFF) {
            out->has_alpha = 1;
            break;
        }
    }

    snprintf(g_png_last_error, sizeof(g_png_last_error), "loaded %ux%u %sPNG", w, h, out->has_alpha ? "RGBA " : "RGB ");
    return 1;
}

void gui_png_free(gui_png_t *png) {
    if (!png) return;
    if (png->rgba) kfree(png->rgba);
    memset(png, 0, sizeof(*png));
}

void gui_png_get_rgb(const gui_png_t *png, u32 x, u32 y, u8 rgb[3]) {
    rgb[0] = rgb[1] = rgb[2] = 0;
    if (!png || !png->rgba || x >= png->width || y >= png->height) return;

    const u8 *p = png->rgba + ((u64)y * (u64)png->width + (u64)x) * 4u;
    rgb[0] = p[0];
    rgb[1] = p[1];
    rgb[2] = p[2];
}

void gui_png_get_rgba(const gui_png_t *png, u32 x, u32 y, u8 rgba[4]) {
    rgba[0] = rgba[1] = rgba[2] = 0;
    rgba[3] = 0xFF;
    if (!png || !png->rgba || x >= png->width || y >= png->height) return;

    const u8 *p = png->rgba + ((u64)y * (u64)png->width + (u64)x) * 4u;
    rgba[0] = p[0];
    rgba[1] = p[1];
    rgba[2] = p[2];
    rgba[3] = p[3];
}

void gui_png_write_fb_pixel(u8 *dst, u8 fb_bpp, const u8 rgb[3]) {
    if (!dst || !rgb) return;
    if (fb_bpp == 4) {
        dst[0] = rgb[2];
        dst[1] = rgb[1];
        dst[2] = rgb[0];
        dst[3] = 0xFF;
    } else if (fb_bpp == 3) {
        dst[0] = rgb[2];
        dst[1] = rgb[1];
        dst[2] = rgb[0];
    } else if (fb_bpp == 2) {
        u16 r = ((u16)rgb[0] & 0xF8) << 8;
        u16 g = ((u16)rgb[1] & 0xFC) << 3;
        u16 b = ((u16)rgb[2] & 0xF8) >> 3;
        u16 v = r | g | b;
        dst[0] = (u8)(v & 0xFF);
        dst[1] = (u8)(v >> 8);
    }
}

void gui_png_write_fb_pixel_rgba(u8 *dst, u8 fb_bpp, const u8 rgba[4], const u8 bg_rgb[3]) {
    if (!dst || !rgba) return;

    u8 blended[3];
    u8 alpha = rgba[3];
    if (alpha == 0xFF || !bg_rgb) {
        blended[0] = rgba[0];
        blended[1] = rgba[1];
        blended[2] = rgba[2];
    } else if (alpha == 0) {
        blended[0] = bg_rgb[0];
        blended[1] = bg_rgb[1];
        blended[2] = bg_rgb[2];
    } else {
        u16 inv = (u16)(255u - alpha);
        blended[0] = (u8)(((u16)rgba[0] * alpha + (u16)bg_rgb[0] * inv + 127u) / 255u);
        blended[1] = (u8)(((u16)rgba[1] * alpha + (u16)bg_rgb[1] * inv + 127u) / 255u);
        blended[2] = (u8)(((u16)rgba[2] * alpha + (u16)bg_rgb[2] * inv + 127u) / 255u);
    }

    gui_png_write_fb_pixel(dst, fb_bpp, blended);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#include "../../external/lodepng/lodepng.c"
#pragma GCC diagnostic pop
