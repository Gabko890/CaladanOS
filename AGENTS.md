# Repository Guidelines

## Project Structure & Module Organization
- Source: `boot/src`, `kernel/src`, `drivers/`, `utils/` (headers in matching `include/` trees).
- Tests: `tests/` (C tests via CLDTEST); declarations in `tests/testdecls.h`.
- Assets: `ramfs/` (packed to `iso/boot/ramfs.cpio`); `programs/` (assembled into `ramfs/bin`).
- Build output: `build/` (ISO at `build/CaladanOS.iso`).
- Config: `config/grub.cfg`, linker script `targets/x86_64.ld`, third‑party in `external/dlmalloc`.

## Build, Test, and Development Commands
- `make build-docker` — build the toolchain image (`cld-kernel-env`).
- `make` / `make build` — build the ISO → `build/CaladanOS.iso`.
- `make test` — build with tests enabled (`ENABLE_TESTS=1`).
- `make qemu` — boot the ISO in QEMU (e.g., `QEMU_ISA_DEBUGCON=true make qemu`).
- `make clean` — remove artifacts under `build/`.
- Local toolchain: `make build-x86_64` (requires `nasm`, `x86_64-elf-gcc`, `x86_64-elf-ld`, `grub-mkrescue`).

## Coding Style & Naming Conventions
- Languages: C (freestanding) and x86_64 NASM.
- Indentation: 4 spaces; K&R braces (same line).
- Naming: `lower_snake_case` (functions/vars), `UPPER_SNAKE_CASE` (macros/const), header guards like `FOO_BAR_H`.
- Types: prefer fixed widths from `utils/cldtypes` (`u8`, `u32`, `u64`).
- Layout: C in `*/src`, headers in `*/include`; keep private functions `static`.

## Testing Guidelines
- Framework: CLDTEST (`utils/cldtest`). Place tests in `tests/` and declare in `tests/testdecls.h`.
- Register in `CLDTEST_INIT()` and group with suites. Example: `CLDTEST_WITH_SUITE("String compare", strcmp_test, string_tests) { /* asserts */ }`.
- Run: `make test && make qemu`. Results print to VGA/console. Prefer small, isolated tests.

## Commit & Pull Request Guidelines
- Commits: present tense; concise subject (≤72 chars). Use scoped prefixes when helpful (e.g., `kernel:`, `drivers:`, `tests:`). Provide rationale in the body.
- PRs: include summary, key changes, how to test (commands + expected output), and screenshots/log excerpts when relevant. Link issues and call out changes to `config/`, `targets/`, or boot flow.

## Security & Configuration Tips
- Toggle ISA debug console: `QEMU_ISA_DEBUGCON=true|false make qemu`.
- RAMFS is embedded; `programs/*.asm` assemble to `ramfs/bin/*` during build.

## Agent‑Specific Instructions
- This AGENTS.md applies repo‑wide; deeper files take precedence if present.
- Follow style, naming, and layout rules; keep diffs minimal and focused.
- Update this document when altering build, test flow, or configuration paths.

