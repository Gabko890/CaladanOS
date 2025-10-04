const std = @import("std");

const Config = struct {
    architecture: []const u8,
    enable_serial: bool,
};

const default_config = Config{ .architecture = "x86_64", .enable_serial = false };

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});
    const config = loadConfig(b);

    const target = b.resolveTargetQuery(targetFromArchitecture(config.architecture));

    const build_options = b.addOptions();
    build_options.addOption([]const u8, "architecture", config.architecture);
    build_options.addOption(bool, "enable_serial", config.enable_serial);

    const root_module = b.createModule(.{
        .root_source_file = b.path("src/kernel/main.zig"),
        .target = target,
        .optimize = optimize,
        .code_model = .kernel,
    });
    root_module.addOptions("build_options", build_options);

    const console_module = b.createModule(.{
        .root_source_file = b.path("src/drivers/video/console.zig"),
        .target = target,
        .optimize = optimize,
    });
    console_module.addOptions("build_options", build_options);

    const portio_module = b.createModule(.{
        .root_source_file = b.path("src/arch/x86_64/cpu/io.zig"),
        .target = target,
        .optimize = optimize,
    });

    var serial_module: ?*std.Build.Module = null;
    if (config.enable_serial) {
        const module = b.createModule(.{
            .root_source_file = b.path("src/drivers/debug/serial.zig"),
            .target = target,
            .optimize = optimize,
        });
        module.addImport("portio", portio_module);
        serial_module = module;
    }

    const multiboot_module = b.createModule(.{
        .root_source_file = b.path("src/arch/x86_64/boot/multiboot2.zig"),
        .target = target,
        .optimize = optimize,
    });
    multiboot_module.addOptions("build_options", build_options);

    const cpuid_module = b.createModule(.{
        .root_source_file = b.path("src/arch/x86_64/cpu/cpuid.zig"),
        .target = target,
        .optimize = optimize,
    });
    cpuid_module.addOptions("build_options", build_options);

    root_module.addImport("console", console_module);
    root_module.addImport("arch_boot", multiboot_module);
    root_module.addImport("arch_cpu", cpuid_module);

    if (serial_module) |module| {
        root_module.addImport("serial", module);
        console_module.addImport("serial", module);
    }

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

    const config_step = b.step("config", "Configure build options interactively");
    const config_cmd = b.addSystemCommand(&[_][]const u8{
        "bash",
        "-lc",
        "tools/configure.sh",
    });
    config_step.dependOn(&config_cmd.step);
}

fn targetFromArchitecture(arch: []const u8) std.Target.Query {
    const x86_query = x86TargetQuery();

    if (std.mem.eql(u8, arch, "x86") or std.mem.eql(u8, arch, "x86_64")) {
        return x86_query;
    }

    std.debug.print(
        "Unknown architecture '{s}', falling back to 32-bit x86 target.\n",
        .{arch},
    );
    return x86_query;
}

fn x86TargetQuery() std.Target.Query {
    var disabled = std.Target.Cpu.Feature.Set.empty;
    var enabled = std.Target.Cpu.Feature.Set.empty;

    disabled.addFeature(@intFromEnum(std.Target.x86.Feature.mmx));
    disabled.addFeature(@intFromEnum(std.Target.x86.Feature.sse));
    disabled.addFeature(@intFromEnum(std.Target.x86.Feature.sse2));
    disabled.addFeature(@intFromEnum(std.Target.x86.Feature.avx));
    disabled.addFeature(@intFromEnum(std.Target.x86.Feature.avx2));
    enabled.addFeature(@intFromEnum(std.Target.x86.Feature.soft_float));

    return .{
        .cpu_arch = .x86,
        .os_tag = .freestanding,
        .abi = .none,
        .cpu_features_sub = disabled,
        .cpu_features_add = enabled,
    };
}

fn loadConfig(b: *std.Build) Config {
    const config_path = b.path("build/config.json");
    const absolute_path = config_path.getPath(b);

    var file = std.fs.cwd().openFile(absolute_path, .{}) catch return default_config;
    defer file.close();

    const contents = file.readToEndAlloc(b.allocator, 4096) catch return default_config;
    defer b.allocator.free(contents);

    const JsonConfig = struct {
        architecture: []const u8 = default_config.architecture,
        debug: struct {
            serial: bool = default_config.enable_serial,
        } = .{},
    };

    var arena = std.heap.ArenaAllocator.init(b.allocator);
    defer arena.deinit();

    const parsed = std.json.parseFromSliceLeaky(JsonConfig, arena.allocator(), contents, .{
        .ignore_unknown_fields = true,
    }) catch return default_config;

    const arch_dup = b.allocator.dupe(u8, parsed.architecture) catch return default_config;

    return .{
        .architecture = arch_dup,
        .enable_serial = parsed.debug.serial,
    };
}
