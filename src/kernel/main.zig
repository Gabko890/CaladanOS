const std = @import("std");
const console = @import("console");
const mb2 = @import("arch_boot");
const cpu = @import("arch_cpu");

const MAGIC = 0xE85250D6;
const HEADER_LENGTH: u32 = 64;
const CHECKSUM: u32 = @as(u32, 0) -% (MAGIC + HEADER_LENGTH);

fn makeHeader() [HEADER_LENGTH]u8 {
    const words = [_]u32{
        MAGIC,
        0,
        HEADER_LENGTH,
        CHECKSUM,
        0x00000001,
        0x0000000c,
        0x00000008,
        0x00000000,
        0x00000005,
        0x00000014,
        0x00000400,
        0x00000300,
        0x00000020,
        0x00000000,
        0x00000000,
        0x00000008,
    };
    return @as([HEADER_LENGTH]u8, @bitCast(words));
}

export const multiboot align(8) linksection(".multiboot") = makeHeader();

var stack_bytes: [16 * 1024]u8 align(16) linksection(".bss") = undefined;

export fn _start() callconv(.naked) noreturn {
    asm volatile (
        \\ movl %[stack_top], %%esp
        \\ movl %%esp, %%ebp
        \\ pushl %%ebx
        \\ pushl %%eax
        \\ call %[kmain:P]
        \\ cli
        \\ hlt
        :
        : [stack_top] "i" (@as([*]align(16) u8, @ptrCast(&stack_bytes)) + @sizeOf(@TypeOf(stack_bytes))),
          [kmain] "X" (&kmain),
        : .{ .memory = true });
}

fn kmain(magic: u32, info_addr: u32) callconv(.c) noreturn {
    if (magic != mb2.bootloader_magic) {
        console.initializeLegacy();
        console.puts("Invalid boot magic\n");
        halt();
    }

    const info_ptr = std.math.cast(usize, info_addr) orelse {
        console.initializeLegacy();
        console.puts("Multiboot info pointer overflow\n");
        halt();
    };

    const framebuffer = mb2.locateFramebuffer(info_ptr);
    if (framebuffer) |fb| {
        switch (fb.kind) {
            .text => {
                if (fb.text_buffer) |text_ptr| {
                    console.initializeText(text_ptr, fb.width, fb.height);
                } else {
                    console.initializeLegacy();
                }
            },
            else => {
                if (fb.address) |addr| {
                    console.initializeFramebuffer(addr, fb.pitch, fb.width, fb.height, fb.bpp);
                } else {
                    console.initializeLegacy();
                }
            },
        }
    } else {
        console.initializeLegacy();
    }

    const fg = @intFromEnum(console.ConsoleColors.White);
    const bg = @intFromEnum(console.ConsoleColors.Black);
    console.setColor(fg | (bg << 4));
    console.clear();
    console.puts("Hello UEFI-ready Zig Kernel!\n");

    var brand_buf: [64]u8 = undefined;
    const brand = cpu.writeBrandString(&brand_buf);
    console.printf("CPU: {s}\n", .{brand});
    var vendor_buf: [16]u8 = undefined;
    const vendor = cpu.writeVendorString(&vendor_buf);
    console.printf("Vendor: {s}\n", .{vendor});
    const logical = cpu.logicalProcessorCount();
    console.printf("Logical processors: {d}\n", .{logical});

    if (mb2.memoryMap(info_ptr)) |map| {
        console.puts("Memory map (bootloader):\n");
        var total_usable: u64 = 0;
        var idx: usize = 0;
        while (idx < map.count) : (idx += 1) {
            const entry = map.entries[idx];
            const type_str = mb2.memoryTypeName(entry.type);
            console.printf(
                "  region {d}: base=0x{X:0>16}, len=0x{X:0>16} ({s})\n",
                .{ idx, entry.addr, entry.len, type_str },
            );
            if (entry.type == @intFromEnum(mb2.MemoryType.available)) {
                total_usable += entry.len;
            }
        }
        console.printf(
            "Total usable memory: {d} MiB\n",
            .{total_usable / (1024 * 1024)},
        );
    } else {
        console.puts("No memory map provided by bootloader\n");
    }

    halt();
}

fn halt() noreturn {
    while (true) {
        asm volatile ("hlt" ::: .{ .memory = true });
    }
}
