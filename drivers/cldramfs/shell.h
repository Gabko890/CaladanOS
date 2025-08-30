#ifndef CLDRAMFS_SHELL_H
#define CLDRAMFS_SHELL_H

// Shell interface
void cldramfs_shell_init(void);
void cldramfs_shell_handle_input(void);
int cldramfs_shell_process_command(const char *command_line);
int cldramfs_shell_is_running(void);

#endif // CLDRAMFS_SHELL_H