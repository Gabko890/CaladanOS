#ifndef SYSCALL_INTERFACE_H
#define SYSCALL_INTERFACE_H

#include <cldtypes.h>

// Direct syscall interface for loaded programs (avoid int 0x80 issues)
long syscall_direct(u32 syscall_num, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6);

// Convenience wrappers
long sys_getpid_direct(void);
long sys_write_direct(long fd, long buf, long count);
long sys_exit_direct(long status);

#endif // SYSCALL_INTERFACE_H