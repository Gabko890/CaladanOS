const std = @import("std");

export fn memset(dest: *anyopaque, c: c_int, n: usize) *anyopaque {
    const ptr: [*]u8 = @ptrCast(dest);
    @memset(ptr[0..n], @as(u8, @intCast(c)));
    return dest;
}

export fn memcpy(dest: *anyopaque, src: *const anyopaque, n: usize) *anyopaque {
    const d: [*]u8 = @ptrCast(dest);
    const s: [*]const u8 = @ptrCast(src);
    @memcpy(d[0..n], s[0..n]);
    return dest;
}

export fn memmove(dest: *anyopaque, src: *const anyopaque, n: usize) *anyopaque {
    const d: [*]u8 = @ptrCast(dest);
    const s: [*]const u8 = @ptrCast(src);
    @memmove(d[0..n], s[0..n]);
    return dest;
}

export fn __udivdi3(a: u64, b: u64) u64 {
    // Zig lowers 64-bit division for 32-bit targets; this avoids libgcc.
    return @divTrunc(a, b);
}

export fn __umoddi3(a: u64, b: u64) u64 {
    return @mod(a, b);
}
