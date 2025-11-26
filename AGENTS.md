# Repository Guidelines

## Project Structure & Module Organization
- Source: `boot/src`, `kernel/src`, `drivers/`, `utils/` (headers in corresponding `include/` trees).
- Tests: `tests/` (C tests compiled into the kernel via CLDTEST).
- Assets: `ramfs/` (packed into `iso/boot/ramfs.cpio`), `programs/` (assembled into `ramfs/bin`).
- Build output: `build/` (ISO at `build/CaladanOS.iso`).
- Config: `config/grub.cfg`, linker script `targets/x86_64.ld`, third‑party `external/dlmalloc`.

## Build, Test, and Development Commands
- `make build-docker` — build the toolchain image (`cld-kernel-env`).
- `make` or `make build` — build the ISO using Docker; output at `build/CaladanOS.iso`.
- `make test` — build with tests enabled (`ENABLE_TESTS=1`).
- `make qemu` — boot the built ISO in QEMU.
- `make clean` — remove artifacts under `build/`.
- Local (without Docker): `make build-x86_64` (requires `nasm`, `x86_64-elf-gcc`, `x86_64-elf-ld`, `grub-mkrescue`).

## Coding Style & Naming Conventions
- Language: C (freestanding) and x86_64 NASM.
- Indentation: 4 spaces; braces on same line (K&R).
- Naming: `lower_snake_case` for functions/variables, `UPPER_SNAKE_CASE` for macros/const, header guards like `FOO_BAR_H`.
- Types: use fixed widths from `utils/cldtypes` (e.g., `u8`, `u32`, `u64`).
- File layout: place C in `*/src`, headers in `*/include`; keep private functions `static`.

## Testing Guidelines
- Framework: CLDTEST (`utils/cldtest`). Add tests in `tests/` and declare in `tests/testdecls.h`.
- Register in `CLDTEST_INIT()` and group with suites. Example:
  `CLDTEST_WITH_SUITE("String compare", strcmp_test, string_tests) { /* asserts */ }`
- Build and run: `make test && make qemu` (results printed to VGA/console). Prefer small, isolated tests.

## Commit & Pull Request Guidelines
- Commits: present tense, concise subject (≤72 chars), scoped prefix when helpful (e.g., `kernel:`, `drivers:`, `tests:`). Describe rationale in the body.
- PRs: include summary, key changes, how to test (commands + expected output), and screenshots/log excerpts when relevant. Link issues and note changes to `config/`, `targets/`, or boot flow.

## Security & Configuration Tips
- QEMU logging: toggle ISA debug console via `QEMU_ISA_DEBUGCON=true|false` when invoking `make qemu`.
- RAMFS contents: files in `ramfs/` are embedded; `programs/*.asm` assemble to `ramfs/bin/*` during build.
