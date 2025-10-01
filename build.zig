const std = @import("std");

pub fn build(b: *std.Build) void {
    var disabled_features = std.Target.Cpu.Feature.Set.empty;
    var enabled_features = std.Target.Cpu.Feature.Set.empty;

    disabled_features.addFeature(@intFromEnum(std.Target.x86.Feature.mmx));
    disabled_features.addFeature(@intFromEnum(std.Target.x86.Feature.sse));
    disabled_features.addFeature(@intFromEnum(std.Target.x86.Feature.sse2));
    disabled_features.addFeature(@intFromEnum(std.Target.x86.Feature.avx));
    disabled_features.addFeature(@intFromEnum(std.Target.x86.Feature.avx2));
    enabled_features.addFeature(@intFromEnum(std.Target.x86.Feature.soft_float));

    const target_query = std.Target.Query{
        .cpu_arch = std.Target.Cpu.Arch.x86,
        .os_tag = std.Target.Os.Tag.freestanding,
        .abi = std.Target.Abi.none,
        .cpu_features_sub = disabled_features,
        .cpu_features_add = enabled_features,
    };
    const optimize = b.standardOptimizeOption(.{});

    const target = b.resolveTargetQuery(target_query);

    const root_module = b.createModule(.{
        .root_source_file = b.path("src/kernel/main.zig"),
        .target = target,
        .optimize = optimize,
        .code_model = .kernel,
    });

    const console_module = b.createModule(.{
        .root_source_file = b.path("src/drivers/video/console.zig"),
        .target = target,
        .optimize = optimize,
    });

    const multiboot_module = b.createModule(.{
        .root_source_file = b.path("src/arch/x86_64/boot/multiboot2.zig"),
        .target = target,
        .optimize = optimize,
    });

    root_module.addImport("console", console_module);
    root_module.addImport("arch_boot", multiboot_module);

    const kernel = b.addExecutable(.{
        .name = "kernel.elf",
        .root_module = root_module,
    });

    kernel.setLinkerScript(b.path("src/arch/x86_64/linker/kernel.ld"));
    const install_kernel = b.addInstallArtifact(kernel, .{});

    const iso_out = b.getInstallPath(.bin, "kernel.iso");
    const kernel_out = b.getInstallPath(.bin, "kernel.elf");
    const grub_cfg = b.path("boot/grub/grub.cfg");
    const grub_cfg_path = grub_cfg.getPath(b);

    const iso_script = std.fmt.allocPrint(b.allocator,
        \\set -e
        \\ISO_DIR="zig-out/iso"
        \\KERNEL="{s}"
        \\ISO_OUT="{s}"
        \\rm -rf "$ISO_DIR"
        \\mkdir -p "$ISO_DIR/boot/grub"
        \\cp "$KERNEL" "$ISO_DIR/boot/kernel.elf"
        \\cp /usr/share/grub/unicode.pf2 "$ISO_DIR/boot/grub/unicode.pf2"
        \\cp "{s}" "$ISO_DIR/boot/grub/grub.cfg"
        \\grub-mkrescue -o "$ISO_OUT" "$ISO_DIR"
    , .{ kernel_out, iso_out, grub_cfg_path }) catch @panic("OOM");

    const iso_cmd = b.addSystemCommand(&[_][]const u8{
        "bash",
        "-lc",
        iso_script,
    });

    const kernel_step = b.step("kernel", "Build the kernel");
    kernel_step.dependOn(&kernel.step);
    kernel_step.dependOn(&install_kernel.step);

    const iso_step = b.step("iso", "Build a UEFI bootable ISO containing the kernel");
    iso_step.dependOn(kernel_step);
    iso_step.dependOn(&iso_cmd.step);
    iso_cmd.step.dependOn(&install_kernel.step);
}
