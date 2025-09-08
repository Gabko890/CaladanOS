#ifndef VESA_H
#define VESA_H

#include <cldtypes.h>
#include <multiboot/multiboot2.h>

struct vesa_info {
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8 framebuffer_bpp;
    u8 framebuffer_type;
    int initialized;
};

// Initialize VESA driver from multiboot info
int vesa_init(u32 mb2_info);

// Get VESA info structure
struct vesa_info* vesa_get_info(void);

// Fill entire screen with color (32-bit ARGB)
void vesa_fill_screen(u32 color);

// Draw pixel at coordinates (x, y)
void vesa_put_pixel(u32 x, u32 y, u32 color);

// Check if VESA is available and initialized
int vesa_is_available(void);

#endif // VESA_H