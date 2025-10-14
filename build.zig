const std = @import("std");

const Config = struct {
    architecture: []const u8,
    enable_serial: bool,
};

const default_config = Config{ .architecture = "x86", .enable_serial = false };

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});
    const config = loadConfig(b);
    const target = b.resolveTargetQuery(targetFromArchitecture(config.architecture));
    const use_docker_tools = b.option(bool, "use_docker_tools", "Use Docker for NASM/LD/GRUB tools") orelse false;

    const build_options = b.addOptions();
    build_options.addOption([]const u8, "architecture", config.architecture);
    build_options.addOption(bool, "enable_serial", config.enable_serial);

    // Kernel modules
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
    cpuid_module.addImport("console", console_module);

    const rt_module = b.createModule(.{
        .root_source_file = b.path("src/lib/builtins.zig"),
        .target = target,
        .optimize = optimize,
    });

    root_module.addImport("console", console_module);
    root_module.addImport("arch_boot", multiboot_module);
    root_module.addImport("arch_cpu", cpuid_module);
    root_module.addImport("rt", rt_module);
    if (serial_module) |m| {
        root_module.addImport("serial", m);
        console_module.addImport("serial", m);
    }

    // ------------------------------------------------------------------------
    // Compile Zig kernel sources to object file (no linking)
    // ------------------------------------------------------------------------
    const zig_obj = b.addObject(.{
        .name = "kernel_zig",
        .root_module = root_module,
    });

    const obj_dir = "zig-out/obj";
    const out_bin_dir_rel = "zig-out/bin";
    const mk_obj_dir = b.addSystemCommand(&[_][]const u8{ "mkdir", "-p", obj_dir });
    mk_obj_dir.step.dependOn(&zig_obj.step);

    // Normalize Zig-produced object to zig-out/obj/kernel_zig.o (location can vary by Zig version)
    const zig_obj_path = zig_obj.getEmittedBin();
    const copy_zig_obj = b.addSystemCommand(&[_][]const u8{ "cp", "-f" });
    copy_zig_obj.addFileArg(zig_obj_path);
    copy_zig_obj.addArg(obj_dir ++ "/kernel_zig.o");
    copy_zig_obj.step.dependOn(&mk_obj_dir.step);

    // ------------------------------------------------------------------------
    // Assemble NASM sources under ./src into objects
    // ------------------------------------------------------------------------
    const arch = config.architecture;
    const nasm_fmt = if (std.mem.eql(u8, arch, "x86_64")) "elf64" else "elf32";

    const nasm_prefix = if (use_docker_tools)
        "docker run --rm -u $(id -u):$(id -g) -v \"$PWD:/work\" -w /work caladanos-build:latest nasm"
    else
        "nasm";

    const asm_script = std.fmt.allocPrint(b.allocator,
        "set -euo pipefail; mkdir -p {s}; shopt -s nullglob globstar; files=(src/**/*.asm src/*.asm); for f in \"${{files[@]}}\"; do [ -e \"$f\" ] || continue; base=\"$(basename \"$f\" .asm)\"; {s} -f {s} \"$f\" -o {s}/$base.o; done",
        .{ obj_dir, nasm_prefix, nasm_fmt, obj_dir },
    ) catch unreachable;
    const compile_asm = b.addSystemCommand(&[_][]const u8{ "bash", "-lc", asm_script });

    // ------------------------------------------------------------------------
    // Link all objects with x86_64-elf-ld to produce kernel.elf
    // ------------------------------------------------------------------------
    const ld_script_rel = "src/arch/x86_64/linker/kernel.ld";
    const kernel_elf_rel = out_bin_dir_rel ++ "/kernel.elf";
    const ld_machine = if (std.mem.eql(u8, arch, "x86_64")) null else "-m elf_i386";
    const ld_prefix = if (use_docker_tools)
        "docker run --rm -u $(id -u):$(id -g) -v \"$PWD:/work\" -w /work caladanos-build:latest x86_64-elf-ld"
    else
        "x86_64-elf-ld";
    const link_out = if (use_docker_tools) "/work/" ++ kernel_elf_rel else kernel_elf_rel;
    const link_script = std.fmt.allocPrint(b.allocator,
        "set -e; mkdir -p {s}; {s} {s} -T {s} -nostdlib -static -o {s} {s}/*.o 2>&1",
        .{ out_bin_dir_rel, ld_prefix, if (ld_machine) |m| m else "", ld_script_rel, link_out, obj_dir },
    ) catch unreachable;
    const link_cmd = b.addSystemCommand(&[_][]const u8{ "bash", "-lc", link_script });
    link_cmd.step.dependOn(&mk_obj_dir.step);
    link_cmd.step.dependOn(&compile_asm.step);
    link_cmd.step.dependOn(&copy_zig_obj.step);

    // ------------------------------------------------------------------------
    // Hybrid ISO step (pure Zig)
    // ------------------------------------------------------------------------
    const iso_step = b.step("iso", "Build hybrid BIOS+UEFI ISO");

    const iso_dir = "zig-out/iso";
    const kernel_path = kernel_elf_rel;
    const iso_out_rel = out_bin_dir_rel ++ "/kernel.iso";
    const grub_cfg = b.path("boot/grub/grub.cfg");

    const make_dirs = b.addSystemCommand(&[_][]const u8{
        "mkdir",                 "-p",
        iso_dir ++ "/boot/grub", iso_dir ++ "/EFI/BOOT",
    });
    make_dirs.step.dependOn(&link_cmd.step);

    const copy_kernel = b.addSystemCommand(&[_][]const u8{
        "cp", kernel_path, iso_dir ++ "/boot/kernel.elf",
    });
    copy_kernel.step.dependOn(&make_dirs.step);

    const copy_font = b.addSystemCommand(&[_][]const u8{
        "bash",                                                                                 "-c",
        "cp /usr/share/grub/unicode.pf2 zig-out/iso/boot/grub/unicode.pf2 2>/dev/null || true",
    });
    copy_font.step.dependOn(&copy_kernel.step);

    const copy_cfg = b.addSystemCommand(&[_][]const u8{
        "cp", grub_cfg.getPath(b), iso_dir ++ "/boot/grub/grub.cfg",
    });
    copy_cfg.step.dependOn(&copy_font.step);

    const grub_prefix = if (use_docker_tools)
        "docker run --rm -u $(id -u):$(id -g) -v \"$PWD:/work\" -w /work caladanos-build:latest"
    else
        "";
    const mkimage = b.addSystemCommand(&[_][]const u8{
        "bash",
        "-lc",
        std.fmt.allocPrint(b.allocator,
            "{s} grub-mkimage -O x86_64-efi -o {s} -p /boot/grub iso9660 part_gpt part_msdos efi_gop efi_uga multiboot2 normal",
            .{ grub_prefix, iso_dir ++ "/EFI/BOOT/BOOTX64.EFI" },
        ) catch unreachable,
    });
    mkimage.step.dependOn(&copy_cfg.step);

    const iso_out = if (use_docker_tools) "/work/" ++ iso_out_rel else iso_out_rel;
    const mkrescue = b.addSystemCommand(&[_][]const u8{
        "bash",
        "-lc",
        std.fmt.allocPrint(b.allocator, "{s} grub-mkrescue -o {s} {s}", .{ grub_prefix, iso_out, iso_dir }) catch unreachable,
    });
    mkrescue.step.dependOn(&mkimage.step);

    iso_step.dependOn(&mkrescue.step);

    const kernel_step = b.step("kernel", "Build kernel ELF (extern ld)");
    kernel_step.dependOn(&link_cmd.step);

    iso_step.dependOn(kernel_step);

    // ------------------------------------------------------------------------
    // Config
    // ------------------------------------------------------------------------
    const config_step = b.step("config", "Configure build options");
    const config_cmd = b.addSystemCommand(&[_][]const u8{
        "bash", "-lc", "tools/configure.sh",
    });
    config_step.dependOn(&config_cmd.step);
}

// ------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------
fn targetFromArchitecture(arch: []const u8) std.Target.Query {
    if (std.mem.eql(u8, arch, "x86_64")) return x86_64TargetQuery();
    if (std.mem.eql(u8, arch, "x86")) return x86TargetQuery();
    std.debug.print("Unknown architecture '{s}', defaulting to 32-bit x86.\n", .{arch});
    return x86TargetQuery();
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

fn x86_64TargetQuery() std.Target.Query {
    return .{
        .cpu_arch = .x86_64,
        .os_tag = .freestanding,
        .abi = .none,
    };
}

fn loadConfig(b: *std.Build) Config {
    const path = b.path("build/config.json");
    var file = std.fs.cwd().openFile(path.getPath(b), .{}) catch return default_config;
    defer file.close();

    const contents = file.readToEndAlloc(b.allocator, 4096) catch return default_config;
    defer b.allocator.free(contents);

    const JsonConfig = struct {
        architecture: []const u8 = default_config.architecture,
        debug: struct { serial: bool = default_config.enable_serial } = .{},
    };

    var arena = std.heap.ArenaAllocator.init(b.allocator);
    defer arena.deinit();

    const parsed = std.json.parseFromSliceLeaky(JsonConfig, arena.allocator(), contents, .{
        .ignore_unknown_fields = true,
    }) catch return default_config;

    const arch_dup = b.allocator.dupe(u8, parsed.architecture) catch return default_config;
    return .{ .architecture = arch_dup, .enable_serial = parsed.debug.serial };
}
