#include <syscall_test.h>
#include <syscalls.h>
#include <vgaio.h>

void test_syscalls(void) {
    vga_printf("[TEST] Testing syscall system...\n");
    
    // Test getpid syscall (syscall 20)
    long pid = syscall0(SYSCALL_GETPID);
    vga_printf("[TEST] getpid() returned: %ld\n", pid);
    
    // Test write syscall (syscall 4) - write "Hello from syscall!" to stdout
    const char* msg = "Hello from syscall!\n";
    long len = 0;
    // Calculate string length
    while (msg[len]) len++;
    
    long bytes_written = syscall3(SYSCALL_WRITE, 1, (long)msg, len);
    vga_printf("[TEST] write() returned: %ld bytes\n", bytes_written);
    
    // Test exit syscall (syscall 1)
    syscall1(SYSCALL_EXIT, 42);
    
    vga_printf("[TEST] Syscall tests completed!\n");
}