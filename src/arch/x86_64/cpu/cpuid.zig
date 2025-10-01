pub const CpuidResult = struct {
    eax: u32,
    ebx: u32,
    ecx: u32,
    edx: u32,
};

fn cpuid_raw(leaf: u32, subleaf: u32) CpuidResult {
    var result = CpuidResult{
        .eax = 0,
        .ebx = 0,
        .ecx = 0,
        .edx = 0,
    };

    asm volatile ("pushl %%ebx\nmov %[leaf], %%eax\nmov %[sub], %%ecx\ncpuid\nmov %%eax, 0(%[out])\nmov %%ebx, 4(%[out])\nmov %%ecx, 8(%[out])\nmov %%edx, 12(%[out])\npopl %%ebx"
        :
        : [leaf] "r" (leaf),
          [sub] "r" (subleaf),
          [out] "r" (&result),
        : .{ .eax = true, .ecx = true, .edx = true, .memory = true }
    );

    return result;
}

pub fn logicalProcessorCount() u32 {
    const res = cpuid_raw(0x01, 0);
    const count: u32 = (res.ebx >> 16) & 0xFF;
    return if (count == 0) 1 else count;
}

pub fn writeVendorString(buffer: []u8) []const u8 {
    if (buffer.len < 12) return buffer[0..0];
    const res = cpuid_raw(0x00, 0);
    const ebx_bytes: [4]u8 = @bitCast(res.ebx);
    const edx_bytes: [4]u8 = @bitCast(res.edx);
    const ecx_bytes: [4]u8 = @bitCast(res.ecx);
    inline for (ebx_bytes, 0..) |byte, i| buffer[i] = byte;
    inline for (edx_bytes, 0..) |byte, i| buffer[i + 4] = byte;
    inline for (ecx_bytes, 0..) |byte, i| buffer[i + 8] = byte;
    return buffer[0..12];
}

pub fn writeBrandString(buffer: []u8) []const u8 {
    const max_ext = cpuid_raw(0x80000000, 0).eax;
    if (max_ext < 0x80000004) {
        return writeVendorString(buffer);
    }

    if (buffer.len < 48) {
        return buffer[0..0];
    }

    var offset: usize = 0;
    inline for (&.{ 0x80000002, 0x80000003, 0x80000004 }) |leaf| {
        const res = cpuid_raw(leaf, 0);
        const words = [_]u32{ res.eax, res.ebx, res.ecx, res.edx };
        inline for (words) |word| {
            const bytes: [4]u8 = @bitCast(word);
            inline for (bytes, 0..) |byte, i| {
                buffer[offset + i] = byte;
            }
            offset += 4;
        }
    }

    var end = offset;
    while (end > 0) {
        const byte = buffer[end - 1];
        if (byte == 0 or byte == ' ') {
            end -= 1;
            continue;
        }
        break;
    }

    if (end == 0) return buffer[0..0];
    return buffer[0..end];
}
