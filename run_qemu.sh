sudo qemu-system-x86_64 -cdrom build/kernel.iso -device isa-debugcon,chardev=dbg_console -chardev stdio,id=dbg_console
