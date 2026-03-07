#include "shell.h"
#include "tty.h"
#include "cldramfs.h"
#include <vgaio.h>
#include <string.h>
#include <gui/gui.h>
#include <fb/fb_console.h>
#include <shell_control.h>
#include <deferred.h>
#include <lua_vm.h>
#include <kmalloc.h>
#include <sysinfo.h>

// External TTY functions
extern void tty_global_init(void);
extern int tty_global_handle_key(u8 scancode, int is_extended);
extern char* tty_global_get_line(void);
extern void tty_global_reset_line(void);

static int shell_running = 0;
static int shell_command_from_gui = 0;
static int shell_async_command_scheduled = 0;

typedef struct {
    char *data;
    u32 len;
    u32 cap;
    int failed;
} shell_redirect_capture_t;

static shell_redirect_capture_t redirect_capture = {0};

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

static void trim_trailing_whitespace(char *str) {
    u32 len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
}

static int shell_line_is_command(const char *line, const char *expected) {
    if (!line || !expected) return 0;
    char buf[256];
    u32 expected_len = strlen(expected);
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim_trailing_whitespace(buf);
    char *cmd = skip_whitespace(buf);
    if (strncmp(cmd, expected, expected_len) != 0) return 0;
    return cmd[expected_len] == '\0' ||
           cmd[expected_len] == ' ' ||
           cmd[expected_len] == '\t' ||
           cmd[expected_len] == '>';
}

static void print_guictl_help(void) {
    vga_printf("Usage:\n");
    vga_printf("  guictl start\n");
    vga_printf("  guictl reload config\n");
    vga_printf("  guictl reload wallpaper\n");
    vga_printf("  guictl change wallpaper <path>\n");
    vga_printf("  guictl help\n");
}

static int shell_parse_redirection(char *cmd, char **filename, int *append) {
    *filename = NULL;
    *append = 0;

    char *redir = strchr(cmd, '>');
    if (!redir) return 0;

    if (redir[1] == '>') {
        *append = 1;
        *redir = '\0';
        *filename = redir + 2;
    } else {
        *redir = '\0';
        *filename = redir + 1;
    }

    trim_trailing_whitespace(cmd);
    *filename = skip_whitespace(*filename);
    trim_trailing_whitespace(*filename);

    return 1;
}

static void shell_redirect_putchar(char c) {
    if (redirect_capture.failed) return;

    if (redirect_capture.len + 1 >= redirect_capture.cap) {
        u32 new_cap = redirect_capture.cap ? redirect_capture.cap * 2 : 256;
        char *new_data = (char*)kmalloc(new_cap);
        if (!new_data) {
            redirect_capture.failed = 1;
            return;
        }
        if (redirect_capture.data) {
            memcpy(new_data, redirect_capture.data, redirect_capture.len);
            kfree(redirect_capture.data);
        }
        redirect_capture.data = new_data;
        redirect_capture.cap = new_cap;
    }

    redirect_capture.data[redirect_capture.len++] = c;
    redirect_capture.data[redirect_capture.len] = '\0';
}

static void shell_redirect_begin(void) {
    redirect_capture.data = NULL;
    redirect_capture.len = 0;
    redirect_capture.cap = 0;
    redirect_capture.failed = 0;
}

static void shell_redirect_end(void) {
    if (redirect_capture.data) {
        kfree(redirect_capture.data);
    }
    redirect_capture.data = NULL;
    redirect_capture.len = 0;
    redirect_capture.cap = 0;
    redirect_capture.failed = 0;
}

static int shell_write_redirect_file(const char *filename, const char *data, u32 size, int append) {
    Node *file = cldramfs_resolve_path_file(filename, 1);
    if (!file) {
        vga_printf("shell: cannot create '%s'\n", filename);
        return -1;
    }
    if (file->type != FILE_NODE) {
        vga_printf("shell: %s: Is a directory\n", filename);
        return -1;
    }

    u32 old_size = (append && file->content) ? file->content_size : 0;
    char *new_content = (char*)kmalloc(old_size + size + 1);
    if (!new_content) {
        vga_printf("shell: cannot write '%s': out of memory\n", filename);
        return -1;
    }

    if (old_size > 0) {
        memcpy(new_content, file->content, old_size);
    }
    if (size > 0 && data) {
        memcpy(new_content + old_size, data, size);
    }
    new_content[old_size + size] = '\0';

    if (file->content) {
        kfree(file->content);
    }
    file->content = new_content;
    file->content_size = old_size + size;
    return 0;
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
    
    char line[TTY_BUFFER_SIZE];
    strncpy(line, command_line, TTY_BUFFER_SIZE - 1);
    line[TTY_BUFFER_SIZE - 1] = '\0';
    
    // Remove trailing whitespace
    u32 len = strlen(line);
    while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t' || line[len-1] == '\n')) {
        line[--len] = '\0';
    }
    
    if (len == 0) return 0;
    
    char *cmd = skip_whitespace(line);
    char *redirect_filename = NULL;
    int redirect_append = 0;

    if (shell_parse_redirection(cmd, &redirect_filename, &redirect_append)) {
        cmd = skip_whitespace(cmd);
        if (!redirect_filename || !*redirect_filename) {
            vga_printf("shell: syntax error: missing redirection target\n");
            return 0;
        }
        if (!*cmd) {
            shell_write_redirect_file(redirect_filename, "", 0, redirect_append);
            return 0;
        }

        shell_redirect_begin();
        if (vga_push_putchar_sink(shell_redirect_putchar, 1) != 0) {
            shell_redirect_end();
            vga_printf("shell: cannot redirect output\n");
            return 0;
        }

        int result = cldramfs_shell_process_command(cmd);
        vga_pop_putchar_sink();

        if (redirect_capture.failed) {
            vga_printf("shell: redirect output truncated: out of memory\n");
        } else {
            shell_write_redirect_file(redirect_filename, redirect_capture.data, redirect_capture.len, redirect_append);
        }

        shell_redirect_end();
        return result;
    }
    
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
    else if (strncmp(cmd, "exec", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' ')) {
        char *arg = find_arg(cmd);
        if (arg) {
            cldramfs_cmd_exec(arg);
        } else {
            vga_printf("exec: missing ELF file name\n");
            vga_printf("usage: exec <filename.o>\n");
        }
    }
    else if (strncmp(cmd, "lua", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) {
        char *argline = find_arg(cmd);
        if (!argline || !*argline) {
            vga_printf("lua: usage: lua <script.lua> [args...]\n");
        } else {
            // Split into tokens: script + args
            char buf[256]; strncpy(buf, argline, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
            char *tokens[16]; int tcount = 0;
            char *p = buf;
            while (*p && tcount < 16) {
                p = skip_whitespace(p);
                if (!*p) break;
                tokens[tcount++] = p;
                while (*p && *p!=' ' && *p!='\t') p++;
                if (*p) { *p='\0'; p++; }
            }
            if (tcount <= 0) { vga_printf("lua: bad args\n"); }
            else {
                // Build task args
                typedef struct { char *path; int argc; char **argv; int from_gui; } lua_task_args_t; // mirrored
                lua_task_args_t *task = (lua_task_args_t*)kmalloc(sizeof(*task));
                if (!task) { vga_printf("lua: oom\n"); return 0; }
                task->argc = tcount;
                task->from_gui = shell_command_from_gui;
                task->argv = (char**)kmalloc(sizeof(char*) * tcount);
                if (!task->argv) { kfree(task); vga_printf("lua: oom\n"); return 0; }
                for (int i=0;i<tcount;i++){ u32 n=strlen(tokens[i]); task->argv[i]=(char*)kmalloc(n+1); if(task->argv[i]) strcpy(task->argv[i], tokens[i]); }
                u32 pn = strlen(tokens[0]); task->path = (char*)kmalloc(pn+1); if (task->path) strcpy(task->path, tokens[0]);
                // Pause only after scheduling succeeds, otherwise input stays usable.
                extern void cld_luavm_run_deferred_with_args(void *arg);
                if (!task->path || deferred_schedule(cld_luavm_run_deferred_with_args, task) != 0) {
                    vga_printf("lua: failed to schedule\n");
                    if (task) {
                        if (task->path) kfree(task->path);
                        for (int i=0;i<tcount;i++) if (task->argv[i]) kfree(task->argv[i]);
                        kfree(task->argv);
                        kfree(task);
                    }
                } else {
                    shell_pause();
                    shell_async_command_scheduled = 1;
                }
            }
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
        vga_printf("  exec <file.o>       - Execute ELF relocatable file\n");
        vga_printf("  lua <script.lua>    - Run Lua script\n");
        vga_printf("  sysinfo             - Show kernel build information\n");
        vga_printf("  guictl <command>    - Manage GUI (guictl help)\n");
        vga_printf("  snake               - Open Snake in GUI\n");
        vga_printf("  cmd > file          - Redirect command output to file\n");
        vga_printf("  cmd >> file         - Append command output to file\n");
        vga_printf("  clear               - Clear screen\n");
        vga_printf("  exit                - Exit shell\n");
    }
    else if (strcmp(cmd, "clear") == 0) {
        // Clear screen using ANSI escape sequences
        vga_printf("\x1b[2J\x1b[H");
    }
    else if (strcmp(cmd, "sysinfo") == 0) {
        vga_printf("Kernel version: %s\n", sysinfo.kernel_version);
        vga_printf("Build label:    %s\n", sysinfo.build_label);
        vga_printf("Git branch:     %s\n", sysinfo.git_branch);
        vga_printf("Git commit:     %s\n", sysinfo.git_commit);
        vga_printf("Build datetime: %s\n", sysinfo.build_datetime);
    }
    else if ((strncmp(cmd, "guictl", 6) == 0 && (cmd[6] == '\0' || cmd[6] == ' ' || cmd[6] == '\t')) ||
             (strncmp(cmd, "guistl", 6) == 0 && (cmd[6] == '\0' || cmd[6] == ' ' || cmd[6] == '\t'))) {
        char *arg = find_arg(cmd);
        if (!arg || !*arg || strcmp(arg, "help") == 0) {
            print_guictl_help();
        } else if (strcmp(arg, "start") == 0) {
            if (!fb_console_present()) vga_printf("GUI requires framebuffer mode.\n");
            else gui_start();
        } else if (strcmp(arg, "reload config") == 0) {
            if (gui_reload_config()) vga_printf("guictl: config reloaded\n");
            else vga_printf("guictl: config missing or invalid\n");
        } else if (strcmp(arg, "reload wallpaper") == 0) {
            if (gui_reload_wallpaper()) vga_printf("guictl: wallpaper reloaded\n");
            else vga_printf("guictl: wallpaper load failed\n");
        } else if (strncmp(arg, "change wallpaper", 16) == 0 && (arg[16] == ' ' || arg[16] == '\t')) {
            char *path = skip_whitespace(arg + 16);
            if (gui_change_wallpaper(path)) vga_printf("guictl: wallpaper changed for this session\n");
            else vga_printf("guictl: cannot load wallpaper '%s'\n", path);
        } else {
            print_guictl_help();
        }
    }
    else if (strcmp(cmd, "snake") == 0) {
        if (!fb_console_present()) {
            vga_printf("GUI requires framebuffer mode.\n");
        } else {
            // Ensure GUI is started, then open snake window
            extern void gui_open_snake(void);
            gui_start();
            gui_open_snake();
        }
    }
    else if (strcmp(cmd, "exit") == 0) {
        shell_running = 0;
        vga_printf("Shell exiting...\n");
        return 1;
    }
    else {
        // Attempt to run a Lua utility from /usr/bin/<cmd>.lua
        // Extract command name (first token)
        char name[64];
        u32 i=0; while (cmd[i] && cmd[i] != ' ' && cmd[i] != '\t' && i < sizeof(name)-1) { name[i]=cmd[i]; i++; }
        name[i]='\0';
        if (name[0] != '\0') {
            // Build /usr/bin/<name>.lua
            char fullpath[128];
            strncpy(fullpath, "/usr/bin/", sizeof(fullpath)-1); fullpath[sizeof(fullpath)-1] = '\0';
            strncat(fullpath, name, sizeof(fullpath)-strlen(fullpath)-1);
            strncat(fullpath, ".lua", sizeof(fullpath)-strlen(fullpath)-1);

            // Verify existence
            Node *f = cldramfs_resolve_path_file(fullpath, 0);
            if (f && f->type == FILE_NODE) {
                // Tokenize remaining args
                char *rest = (char*)(cmd + i);
                rest = skip_whitespace(rest);
                char buf[256]; strncpy(buf, rest, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
                char *tokens[15]; int tcount = 0; // reserve 1 for script path
                char *p = buf;
                while (*p && tcount < 15) {
                    p = skip_whitespace(p);
                    if (!*p) break;
                    tokens[tcount++] = p;
                    while (*p && *p!=' ' && *p!='\t') p++;
                    if (*p) { *p='\0'; p++; }
                }
                // Build task args: path + tokens
                typedef struct { char *path; int argc; char **argv; int from_gui; } lua_task_args_t; // mirrored
                lua_task_args_t *task = (lua_task_args_t*)kmalloc(sizeof(*task));
                if (!task) { vga_printf("lua: oom\n"); return 0; }
                task->argc = tcount + 1;
                task->from_gui = shell_command_from_gui;
                task->argv = (char**)kmalloc(sizeof(char*) * (tcount + 1));
                if (!task->argv) { kfree(task); vga_printf("lua: oom\n"); return 0; }
                // argv[0] = fullpath
                u32 pn = strlen(fullpath); task->path = (char*)kmalloc(pn+1); if (task->path) strcpy(task->path, fullpath);
                task->argv[0] = (char*)kmalloc(pn+1); if (task->argv[0]) strcpy(task->argv[0], fullpath);
                for (int j=0;j<tcount;j++) { u32 n=strlen(tokens[j]); task->argv[j+1]=(char*)kmalloc(n+1); if(task->argv[j+1]) strcpy(task->argv[j+1], tokens[j]); }
                // Pause only after scheduling succeeds, otherwise input stays usable.
                extern void cld_luavm_run_deferred_with_args(void *arg);
                if (!task->path || !task->argv[0] || deferred_schedule(cld_luavm_run_deferred_with_args, task) != 0) {
                    vga_printf("lua: failed to schedule\n");
                    if (task) {
                        if (task->path) kfree(task->path);
                        if (task->argv) {
                            for (int j=0;j<task->argc;j++) if (task->argv[j]) kfree(task->argv[j]);
                            kfree(task->argv);
                        }
                        kfree(task);
                    }
                } else {
                    shell_pause();
                    shell_async_command_scheduled = 1;
                }
                return 0;
            }
        }
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
    if (shell_running && shell_is_active()) {
        tty_print_prompt();
    }
}

void cldramfs_shell_handle_gui_terminal_input(void) {
    if (!shell_running) return;

    char *line = tty_global_get_line();
    if (shell_line_is_command(line, "exit")) {
        tty_global_reset_line();
        gui_close_terminal();
        return;
    }

    if (line && *line) {
        shell_command_from_gui = 1;
        shell_async_command_scheduled = 0;
        cldramfs_shell_process_command(line);
        shell_command_from_gui = 0;
    }

    tty_global_reset_line();
    if (shell_running && !shell_async_command_scheduled) {
        tty_print_prompt();
    }
    shell_async_command_scheduled = 0;
}

int cldramfs_shell_is_running(void) {
    return shell_running;
}
