#include "shell.h"
#include "tty.h"
#include "cldramfs.h"
#include <vgaio.h>
#include <string.h>

// External TTY functions
extern void tty_global_init(void);
extern int tty_global_handle_key(u8 scancode, int is_extended);
extern char* tty_global_get_line(void);
extern void tty_global_reset_line(void);

static int shell_running = 0;

void cldramfs_shell_init(void) {
    // Note: ramfs should already be initialized by now
    tty_global_init();
    shell_running = 1;
    
    // Clear screen and show welcome message
    vga_printf("\x1b[2J\x1b[H"); // Clear screen and move cursor to top
    vga_printf("Welcome to ");
    vga_attr(0x0B); // Cyan
    vga_printf("CaladanOS");
    vga_attr(0x07); // Reset to white
    vga_printf("\n\n");
    vga_printf("Type 'help' for available commands\n\n");
    
    // Show initial prompt
    tty_print_prompt();
}

static char* skip_whitespace(char *str) {
    while (*str == ' ' || *str == '\t') str++;
    return str;
}

static char* find_arg(char *str) {
    // Find first space after command
    while (*str && *str != ' ' && *str != '\t') str++;
    if (!*str) return NULL;
    
    // Skip whitespace to find argument
    return skip_whitespace(str);
}

static void parse_two_args(const char *cmd, char *arg1, char *arg2) {
    arg1[0] = '\0';
    arg2[0] = '\0';
    
    char *space1 = strchr(cmd, ' ');
    if (!space1) return;
    
    space1 = skip_whitespace(space1);
    char *space2 = strchr(space1, ' ');
    if (!space2) return;
    
    u32 arg1_len = space2 - space1;
    if (arg1_len > 255) arg1_len = 255;
    strncpy(arg1, space1, arg1_len);
    arg1[arg1_len] = '\0';
    
    space2 = skip_whitespace(space2);
    strncpy(arg2, space2, 255);
    arg2[255] = '\0';
}

int cldramfs_shell_process_command(const char *command_line) {
    if (!command_line || !*command_line) return 0;
    
    char line[256];
    strncpy(line, command_line, 255);
    line[255] = '\0';
    
    // Remove trailing whitespace
    u32 len = strlen(line);
    while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t' || line[len-1] == '\n')) {
        line[--len] = '\0';
    }
    
    if (len == 0) return 0;
    
    char *cmd = skip_whitespace(line);
    
    if (strncmp(cmd, "ls", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) {
        char *arg = find_arg(cmd);
        cldramfs_cmd_ls(arg);
    }
    else if (strncmp(cmd, "cd", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) {
        char *arg = find_arg(cmd);
        cldramfs_cmd_cd(arg);
    }
    else if (strncmp(cmd, "mkdir", 5) == 0 && (cmd[5] == '\0' || cmd[5] == ' ')) {
        char *arg = find_arg(cmd);
        if (arg) {
            cldramfs_cmd_mkdir(arg);
        } else {
            vga_printf("mkdir: missing directory name\n");
        }
    }
    else if (strncmp(cmd, "touch", 5) == 0 && (cmd[5] == '\0' || cmd[5] == ' ')) {
        char *arg = find_arg(cmd);
        if (arg) {
            cldramfs_cmd_touch(arg);
        } else {
            vga_printf("touch: missing file name\n");
        }
    }
    else if (strncmp(cmd, "cat", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) {
        char *arg = find_arg(cmd);
        if (arg) {
            cldramfs_cmd_cat(arg);
        } else {
            vga_printf("cat: missing file name\n");
        }
    }
    else if (strncmp(cmd, "echo", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) {
        char *arg = find_arg(cmd);
        cldramfs_cmd_echo(arg);
    }
    else if (strncmp(cmd, "rm", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) {
        char *arg = find_arg(cmd);
        cldramfs_cmd_rm(arg);
    }
    else if (strncmp(cmd, "rmdir", 5) == 0 && (cmd[5] == '\0' || cmd[5] == ' ')) {
        char *arg = find_arg(cmd);
        cldramfs_cmd_rmdir(arg);
    }
    else if (strncmp(cmd, "mv", 2) == 0 && cmd[2] == ' ') {
        char arg1[256], arg2[256];
        parse_two_args(cmd, arg1, arg2);
        if (arg1[0] && arg2[0]) {
            cldramfs_cmd_mv(arg1, arg2);
        } else {
            vga_printf("mv: usage: mv <source> <destination>\n");
        }
    }
    else if (strncmp(cmd, "cp", 2) == 0 && cmd[2] == ' ') {
        char arg1[256], arg2[256];
        parse_two_args(cmd, arg1, arg2);
        if (arg1[0] && arg2[0]) {
            cldramfs_cmd_cp(arg1, arg2);
        } else {
            vga_printf("cp: usage: cp <source> <destination>\n");
        }
    }
    else if (strcmp(cmd, "help") == 0) {
        vga_printf("Available commands:\n");
        vga_printf("  ls [path]           - List directory contents\n");
        vga_printf("  cd [path]           - Change directory\n");
        vga_printf("  mkdir <path>        - Create directory\n");
        vga_printf("  rmdir <path>        - Remove empty directory\n");
        vga_printf("  touch <file>        - Create file\n");
        vga_printf("  rm <file>           - Remove file\n");
        vga_printf("  cat <file>          - Display file contents\n");
        vga_printf("  cp <src> <dst>      - Copy file\n");
        vga_printf("  mv <src> <dst>      - Move/rename file\n");
        vga_printf("  echo [text]         - Print text to stdout\n");
        vga_printf("  echo [text] > file  - Write text to file\n");
        vga_printf("  echo [text] >> file - Append text to file\n");
        vga_printf("  clear               - Clear screen\n");
        vga_printf("  exit                - Exit shell\n");
    }
    else if (strcmp(cmd, "clear") == 0) {
        // Clear screen using ANSI escape sequences
        vga_printf("\x1b[2J\x1b[H");
    }
    else if (strcmp(cmd, "exit") == 0) {
        shell_running = 0;
        vga_printf("Shell exiting...\n");
        return 1;
    }
    else {
        vga_printf("Unknown command: %s\n", cmd);
        vga_printf("Type 'help' for available commands\n");
    }
    
    return 0;
}

void cldramfs_shell_handle_input(void) {
    if (!shell_running) return;
    
    // This function is called when we have input to process
    char *line = tty_global_get_line();
    if (line && *line) {
        cldramfs_shell_process_command(line);
    }
    
    tty_global_reset_line();
    if (shell_running) {
        tty_print_prompt();
    }
}

int cldramfs_shell_is_running(void) {
    return shell_running;
}