const std = @import("std");

pub const bootloader_magic: u32 = 0x36d76289;

pub const FramebufferType = enum(u8) {
    indexed = 0,
    rgb = 1,
    text = 2,
};

pub const FramebufferInfo = struct {
    address: ?[*]volatile u8 = null,
    pitch: u32 = 0,
    width: u32 = 0,
    height: u32 = 0,
    bpp: u8 = 0,
    kind: FramebufferType = .text,
    text_buffer: ?[*]volatile u16 = null,
};

const InfoHeader = extern struct {
    total_size: u32,
    reserved: u32,
};

const TagHeader = extern struct {
    type: u32,
    size: u32,
};

const FramebufferTag = extern struct {
    header: TagHeader,
    framebuffer_addr: u64,
    framebuffer_pitch: u32,
    framebuffer_width: u32,
    framebuffer_height: u32,
    framebuffer_bpp: u8,
    framebuffer_type: u8,
    reserved: u16,
};

fn alignTo8(value: usize) usize {
    return (value + 7) & ~@as(usize, 7);
}

pub fn locateFramebuffer(info_addr: usize) ?FramebufferInfo {
    const header_ptr = @as(*const InfoHeader, @ptrFromInt(info_addr));
    var cursor = info_addr + @sizeOf(InfoHeader);
    const end = info_addr + header_ptr.total_size;

    while (cursor < end) {
        const tag = @as(*const TagHeader, @ptrFromInt(cursor));
        if (tag.type == 0 or tag.size == 0) break;

        if (tag.type == 8) {
            const fb_tag = @as(*const FramebufferTag, @ptrCast(tag));
            const fb_type: FramebufferType = @enumFromInt(fb_tag.framebuffer_type);
            var info = FramebufferInfo{
                .pitch = fb_tag.framebuffer_pitch,
                .width = fb_tag.framebuffer_width,
                .height = fb_tag.framebuffer_height,
                .bpp = fb_tag.framebuffer_bpp,
                .kind = fb_type,
            };

            const addr = std.math.cast(usize, fb_tag.framebuffer_addr) orelse return null;
            switch (fb_type) {
                .text => {
                    info.text_buffer = @as([*]volatile u16, @ptrFromInt(addr));
                },
                else => {
                    info.address = @as([*]volatile u8, @ptrFromInt(addr));
                },
            }
            return info;
        }

        cursor = alignTo8(cursor + tag.size);
    }

    return null;
}
