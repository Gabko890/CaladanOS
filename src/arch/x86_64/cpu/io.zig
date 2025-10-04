const builtin = @import("builtin");

pub fn supportsPortIo() bool {
    return switch (builtin.target.cpu.arch) {
        .x86, .x86_64 => true,
        else => false,
    };
}

pub fn out8(port: u16, value: u8) void {
    asm volatile ("push %%eax\npush %%edx\nmov %[value], %%al\nmov %[port], %%dx\noutb %%al, %%dx\npop %%edx\npop %%eax"
        :
        : [value] "r" (value),
          [port] "r" (port),
        : .{ .memory = false, .eax = true, .edx = true });
}

pub fn in8(port: u16) u8 {
    var result: u8 = 0;
    asm volatile ("push %%edx\nmov %[port], %%dx\ninb %%dx, %%al\nmov %%al, %[result]\npop %%edx"
        : [result] "=r" (result),
        : [port] "r" (port),
        : .{ .memory = false, .edx = true });
    return result;
}
