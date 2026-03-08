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

    unsigned char *rgb = 0;
    error = lodepng_decode24(&rgb, &w, &h, (const unsigned char*)data, (size_t)size);
    if (error || !rgb || w == 0 || h == 0) {
        if (rgb) kfree(rgb);
        if (error) {
            snprintf(g_png_last_error, sizeof(g_png_last_error), "LodePNG %u: %s", error, lodepng_error_text(error));
        } else {
            png_set_error("decoded PNG has empty image data");
        }
        return 0;
    }

    out->rgb = (u8*)rgb;
    out->width = (u32)w;
    out->height = (u32)h;
    snprintf(g_png_last_error, sizeof(g_png_last_error), "loaded %ux%u PNG", w, h);
    return 1;
}

void gui_png_free(gui_png_t *png) {
    if (!png) return;
    if (png->rgb) kfree(png->rgb);
    memset(png, 0, sizeof(*png));
}

void gui_png_get_rgb(const gui_png_t *png, u32 x, u32 y, u8 rgb[3]) {
    rgb[0] = rgb[1] = rgb[2] = 0;
    if (!png || !png->rgb || x >= png->width || y >= png->height) return;

    const u8 *p = png->rgb + ((u64)y * (u64)png->width + (u64)x) * 3u;
    rgb[0] = p[0];
    rgb[1] = p[1];
    rgb[2] = p[2];
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#include "../../external/lodepng/lodepng.c"
#pragma GCC diagnostic pop
