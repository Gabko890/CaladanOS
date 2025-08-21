sudo qemu-system-x86_64 -cdrom dist/x86_64/kernel.iso -device isa-debugcon,chardev=dbg_console -chardev stdio,id=dbg_console
