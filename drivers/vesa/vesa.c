#include "vesa.h"
#include <string.h>

static struct vesa_info vesa_driver_info = {0};

int vesa_init(u32 mb2_info) {
    struct multiboot_tag_framebuffer *fb_tag;
    
    fb_tag = (struct multiboot_tag_framebuffer *)multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_FRAMEBUFFER);
    if (!fb_tag) {
        vesa_driver_info.initialized = 0;
        return -1; // Framebuffer not available
    }
    
    vesa_driver_info.framebuffer_addr = fb_tag->framebuffer_addr;
    vesa_driver_info.framebuffer_pitch = fb_tag->framebuffer_pitch;
    vesa_driver_info.framebuffer_width = fb_tag->framebuffer_width;
    vesa_driver_info.framebuffer_height = fb_tag->framebuffer_height;
    vesa_driver_info.framebuffer_bpp = fb_tag->framebuffer_bpp;
    vesa_driver_info.framebuffer_type = fb_tag->framebuffer_type;
    vesa_driver_info.initialized = 1;
    
    return 0; // Success
}

struct vesa_info* vesa_get_info(void) {
    return &vesa_driver_info;
}

void vesa_fill_screen(u32 color) {
    if (!vesa_driver_info.initialized || !vesa_driver_info.framebuffer_addr) return;
    
    u32 *pixels = (u32 *)vesa_driver_info.framebuffer_addr;
    u32 total_pixels = (vesa_driver_info.framebuffer_pitch * vesa_driver_info.framebuffer_height) / 4;
    
    for (u32 i = 0; i < total_pixels; i++) {
        pixels[i] = color;
    }
}

void vesa_put_pixel(u32 x, u32 y, u32 color) {
    if (!vesa_driver_info.initialized || !vesa_driver_info.framebuffer_addr || 
        x >= vesa_driver_info.framebuffer_width || y >= vesa_driver_info.framebuffer_height) return;
    
    u32 *pixels = (u32 *)vesa_driver_info.framebuffer_addr;
    u32 pixel_offset = (y * vesa_driver_info.framebuffer_pitch / 4) + x;
    pixels[pixel_offset] = color;
}

int vesa_is_available(void) {
    return vesa_driver_info.initialized;
}