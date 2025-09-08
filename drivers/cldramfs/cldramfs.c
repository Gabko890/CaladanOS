#include "cldramfs.h"
#include <kmalloc.h>
#include <string.h>
#include <vgaio.h>
#include <elf_loader.h>

Node *ramfs_root = NULL;
Node *ramfs_cwd = NULL;

static u32 hex_to_u32(const char *hex_str, u32 len) {
    u32 result = 0;
    for (u32 i = 0; i < len; i++) {
        char c = hex_str[i];
        if (c >= '0' && c <= '9') {
            result = result * 16 + (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            result = result * 16 + (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            result = result * 16 + (c - 'A' + 10);
        }
    }
    return result;
}

void cldramfs_init(void) {
    ramfs_root = cldramfs_create_node("/", DIR_NODE, NULL);
    ramfs_cwd = ramfs_root;
}

Node* cldramfs_create_node(const char *name, NodeType type, Node *parent) {
    Node *node = (Node*)kmalloc(sizeof(Node));
    if (!node) return NULL;
    
    u32 name_len = strlen(name);
    node->name = (char*)kmalloc(name_len + 1);
    if (!node->name) {
        kfree(node);
        return NULL;
    }
    strcpy(node->name, name);
    
    node->type = type;
    node->child_count = 0;
    node->child_capacity = 4;
    node->parent = parent;
    node->content_size = 0;
    
    if (type == FILE_NODE) {
        node->content = (char*)kmalloc(1);
        if (node->content) {
            node->content[0] = '\0';
        }
        node->children = NULL;
    } else {
        node->content = NULL;
        node->children = (Node**)kmalloc(node->child_capacity * sizeof(Node*));
        if (!node->children) {
            kfree(node->name);
            kfree(node);
            return NULL;
        }
    }
    
    return node;
}

Node* cldramfs_find_child(Node *dir, const char *name) {
    if (!dir || dir->type != DIR_NODE || !name) return NULL;
    
    for (u32 i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0) {
            return dir->children[i];
        }
    }
    return NULL;
}

void cldramfs_add_child(Node *parent, Node *child) {
    if (!parent || parent->type != DIR_NODE || !child) return;
    
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity *= 2;
        Node **new_children = (Node**)kmalloc(parent->child_capacity * sizeof(Node*));
        if (!new_children) return;
        
        for (u32 i = 0; i < parent->child_count; i++) {
            new_children[i] = parent->children[i];
        }
        kfree(parent->children);
        parent->children = new_children;
    }
    
    parent->children[parent->child_count++] = child;
}

Node* cldramfs_resolve_path_dir(const char *path, int create_missing) {
    if (!path) return NULL;
    
    Node *cur = (path[0] == '/') ? ramfs_root : ramfs_cwd;
    
    // Create a copy of the path for tokenization
    u32 path_len = strlen(path);
    char *temp = (char*)kmalloc(path_len + 1);
    if (!temp) return NULL;
    strcpy(temp, path);
    
    char *tok = temp;
    char *next = NULL;
    
    while (tok && *tok) {
        // Find next separator
        next = strchr(tok, '/');
        if (next) {
            *next = '\0';
            next++;
        }
        
        // Skip empty tokens
        if (*tok == '\0') {
            tok = next;
            continue;
        }
        
        if (strcmp(tok, "..") == 0) {
            if (cur->parent) {
                cur = cur->parent;
            }
        } else if (strcmp(tok, ".") != 0) {
            Node *child = cldramfs_find_child(cur, tok);
            if (!child) {
                if (create_missing) {
                    child = cldramfs_create_node(tok, DIR_NODE, cur);
                    if (child) {
                        cldramfs_add_child(cur, child);
                    }
                } else {
                    kfree(temp);
                    return NULL;
                }
            }
            if (!child || child->type != DIR_NODE) {
                kfree(temp);
                return NULL;
            }
            cur = child;
        }
        
        tok = next;
    }
    
    kfree(temp);
    return cur;
}

Node* cldramfs_resolve_path_file(const char *path, int create_dirs) {
    if (!path) return NULL;
    
    u32 path_len = strlen(path);
    char *temp = (char*)kmalloc(path_len + 1);
    if (!temp) return NULL;
    strcpy(temp, path);
    
    char *last_slash = strrchr(temp, '/');
    Node *dir;
    char *fname;
    
    if (last_slash) {
        *last_slash = '\0';
        dir = cldramfs_resolve_path_dir(temp, create_dirs);
        fname = last_slash + 1;
    } else {
        dir = ramfs_cwd;
        fname = temp;
    }
    
    if (!dir) {
        kfree(temp);
        return NULL;
    }
    
    Node *file = cldramfs_find_child(dir, fname);
    if (!file) {
        file = cldramfs_create_node(fname, FILE_NODE, dir);
        if (file) {
            cldramfs_add_child(dir, file);
        }
    }
    
    kfree(temp);
    return file;
}

int cldramfs_load_cpio(void *cpio_data, u32 cpio_size) {
    if (!cpio_data || cpio_size == 0) return -1;
    
    u8 *data = (u8*)cpio_data;
    u32 offset = 0;
    
    while (offset < cpio_size) {
        struct cpio_header *header = (struct cpio_header*)(data + offset);
        
        // Check for valid CPIO magic
        if (strncmp(header->c_magic, "070701", 6) != 0) {
            break;
        }
        
        u32 namesize = hex_to_u32(header->c_namesize, 8);
        u32 filesize = hex_to_u32(header->c_filesize, 8);
        
        offset += sizeof(struct cpio_header);
        
        char *filename = (char*)(data + offset);
        offset += namesize;
        
        // Align to 4-byte boundary after filename
        offset = (offset + 3) & ~3;
        
        // Check for TRAILER marker (end of archive)
        if (strcmp(filename, "TRAILER!!!") == 0) {
            break;
        }
        
        // Skip '.' entry
        if (strcmp(filename, ".") == 0) {
            offset += filesize;
            offset = (offset + 3) & ~3;
            continue;
        }
        
        // Determine if it's a directory or file based on mode
        u32 mode = hex_to_u32(header->c_mode, 8);
        int is_dir = (mode & 0040000) != 0;  // S_IFDIR
        
        if (is_dir) {
            cldramfs_resolve_path_dir(filename, 1);
        } else {
            Node *file = cldramfs_resolve_path_file(filename, 1);
            if (file && filesize > 0) {
                kfree(file->content);
                file->content = (char*)kmalloc(filesize + 1);
                if (file->content) {
                    for (u32 i = 0; i < filesize; i++) {
                        file->content[i] = data[offset + i];
                    }
                    file->content[filesize] = '\0';
                    file->content_size = filesize;
                }
            }
        }
        
        offset += filesize;
        offset = (offset + 3) & ~3;
    }
    
    return 0;
}

void cldramfs_free_node(Node *node) {
    if (!node) return;
    
    if (node->name) kfree(node->name);
    if (node->content) kfree(node->content);
    
    if (node->children) {
        for (u32 i = 0; i < node->child_count; i++) {
            cldramfs_free_node(node->children[i]);
        }
        kfree(node->children);
    }
    
    kfree(node);
}

// Shell command implementations
void cldramfs_cmd_ls(const char *arg) {
    Node *dir = arg ? cldramfs_resolve_path_dir(arg, 0) : ramfs_cwd;
    if (!dir || dir->type != DIR_NODE) {
        vga_printf("Not a directory\n");
        return;
    }
    
    for (u32 i = 0; i < dir->child_count; i++) {
        Node *child = dir->children[i];
        vga_printf("[%s] %s\n", 
                   child->type == DIR_NODE ? "DIR" : "FILE", 
                   child->name);
    }
}

void cldramfs_cmd_cd(const char *arg) {
    if (!arg) {
        ramfs_cwd = ramfs_root;
        return;
    }
    
    Node *dir = cldramfs_resolve_path_dir(arg, 0);
    if (!dir || dir->type != DIR_NODE) {
        vga_printf("Directory not found\n");
        return;
    }
    
    ramfs_cwd = dir;
}

void cldramfs_cmd_mkdir(const char *arg) {
    if (!arg) return;
    cldramfs_resolve_path_dir(arg, 1);
}

void cldramfs_cmd_touch(const char *arg) {
    if (!arg) return;
    cldramfs_resolve_path_file(arg, 1);
}

void cldramfs_cmd_cat(const char *arg) {
    if (!arg) return;
    
    Node *file = cldramfs_resolve_path_file(arg, 0);
    if (!file || file->type != FILE_NODE) {
        vga_printf("File not found\n");
        return;
    }
    
    if (file->content) {
        vga_printf("%s", file->content);
        if (file->content_size > 0 && file->content[file->content_size - 1] != '\n') {
            vga_printf("\n");
        }
    }
}

static void write_to_file(Node *file, const char *text, int append) {
    u32 text_len = strlen(text);
    
    if (append && file->content && file->content_size > 0) {
        u32 new_size = file->content_size + text_len;
        char *new_content = (char*)kmalloc(new_size + 1);
        if (new_content) {
            strcpy(new_content, file->content);
            strcat(new_content, text);
            kfree(file->content);
            file->content = new_content;
            file->content_size = new_size;
        }
    } else {
        kfree(file->content);
        file->content = (char*)kmalloc(text_len + 1);
        if (file->content) {
            strcpy(file->content, text);
            file->content_size = text_len;
        }
    }
}

void cldramfs_cmd_echo(const char *args) {
    if (!args) return;
    
    char temp[512];
    strncpy(temp, args, 511);
    temp[511] = '\0';
    
    char *redir_pos = strstr(temp, ">>");
    int append = 0;
    char *filename = NULL;
    
    if (redir_pos) {
        append = 1;
        *redir_pos = '\0';
        filename = redir_pos + 2;
        while (*filename == ' ') filename++;
    } else {
        redir_pos = strstr(temp, ">");
        if (redir_pos) {
            *redir_pos = '\0';
            filename = redir_pos + 1;
            while (*filename == ' ') filename++;
        }
    }
    
    char *text = temp;
    while (*text == ' ') text++;
    
    u32 text_len = strlen(text);
    while (text_len > 0 && (text[text_len-1] == ' ' || text[text_len-1] == '\t')) {
        text[--text_len] = '\0';
    }
    
    if (filename) {
        Node *file = cldramfs_resolve_path_file(filename, 1);
        if (file) {
            write_to_file(file, text, append);
        }
    } else {
        vga_printf("%s\n", text);
    }
}

void cldramfs_cmd_rm(const char *arg) {
    if (!arg) {
        vga_printf("rm: missing file operand\n");
        return;
    }
    
    u32 path_len = strlen(arg);
    char *temp = (char*)kmalloc(path_len + 1);
    if (!temp) return;
    strcpy(temp, arg);
    
    char *last_slash = strrchr(temp, '/');
    Node *dir;
    char *fname;
    
    if (last_slash) {
        *last_slash = '\0';
        dir = cldramfs_resolve_path_dir(temp, 0);
        fname = last_slash + 1;
    } else {
        dir = ramfs_cwd;
        fname = temp;
    }
    
    if (!dir) {
        vga_printf("rm: cannot remove '%s': No such file or directory\n", arg);
        kfree(temp);
        return;
    }
    
    Node *file = cldramfs_find_child(dir, fname);
    if (!file) {
        vga_printf("rm: cannot remove '%s': No such file or directory\n", arg);
        kfree(temp);
        return;
    }
    
    if (file->type == DIR_NODE) {
        vga_printf("rm: cannot remove '%s': Is a directory\n", arg);
        kfree(temp);
        return;
    }
    
    for (u32 i = 0; i < dir->child_count; i++) {
        if (dir->children[i] == file) {
            for (u32 j = i; j < dir->child_count - 1; j++) {
                dir->children[j] = dir->children[j + 1];
            }
            dir->child_count--;
            break;
        }
    }
    
    cldramfs_free_node(file);
    kfree(temp);
}

void cldramfs_cmd_rmdir(const char *arg) {
    if (!arg) {
        vga_printf("rmdir: missing operand\n");
        return;
    }
    
    Node *dir = cldramfs_resolve_path_dir(arg, 0);
    if (!dir) {
        vga_printf("rmdir: failed to remove '%s': No such file or directory\n", arg);
        return;
    }
    
    if (dir == ramfs_root) {
        vga_printf("rmdir: failed to remove '%s': Cannot remove root directory\n", arg);
        return;
    }
    
    if (dir == ramfs_cwd) {
        vga_printf("rmdir: failed to remove '%s': Cannot remove current directory\n", arg);
        return;
    }
    
    if (dir->child_count > 0) {
        vga_printf("rmdir: failed to remove '%s': Directory not empty\n", arg);
        return;
    }
    
    Node *parent = dir->parent;
    if (parent) {
        for (u32 i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == dir) {
                for (u32 j = i; j < parent->child_count - 1; j++) {
                    parent->children[j] = parent->children[j + 1];
                }
                parent->child_count--;
                break;
            }
        }
    }
    
    cldramfs_free_node(dir);
}

void cldramfs_cmd_mv(const char *src, const char *dst) {
    if (!src || !dst) {
        vga_printf("mv: missing file operand\n");
        return;
    }
    
    u32 src_len = strlen(src);
    char *src_temp = (char*)kmalloc(src_len + 1);
    if (!src_temp) return;
    strcpy(src_temp, src);
    
    char *src_last_slash = strrchr(src_temp, '/');
    Node *src_dir;
    char *src_fname;
    
    if (src_last_slash) {
        *src_last_slash = '\0';
        src_dir = cldramfs_resolve_path_dir(src_temp, 0);
        src_fname = src_last_slash + 1;
    } else {
        src_dir = ramfs_cwd;
        src_fname = src_temp;
    }
    
    if (!src_dir) {
        vga_printf("mv: cannot stat '%s': No such file or directory\n", src);
        kfree(src_temp);
        return;
    }
    
    Node *src_node = cldramfs_find_child(src_dir, src_fname);
    if (!src_node) {
        vga_printf("mv: cannot stat '%s': No such file or directory\n", src);
        kfree(src_temp);
        return;
    }
    
    u32 dst_len = strlen(dst);
    char *dst_temp = (char*)kmalloc(dst_len + 1);
    if (!dst_temp) {
        kfree(src_temp);
        return;
    }
    strcpy(dst_temp, dst);
    
    char *dst_last_slash = strrchr(dst_temp, '/');
    Node *dst_dir;
    char *dst_fname;
    
    if (dst_last_slash) {
        *dst_last_slash = '\0';
        dst_dir = cldramfs_resolve_path_dir(dst_temp, 0);
        dst_fname = dst_last_slash + 1;
    } else {
        dst_dir = ramfs_cwd;
        dst_fname = dst_temp;
    }
    
    if (!dst_dir) {
        vga_printf("mv: cannot move '%s' to '%s': No such file or directory\n", src, dst);
        kfree(src_temp);
        kfree(dst_temp);
        return;
    }
    
    for (u32 i = 0; i < src_dir->child_count; i++) {
        if (src_dir->children[i] == src_node) {
            for (u32 j = i; j < src_dir->child_count - 1; j++) {
                src_dir->children[j] = src_dir->children[j + 1];
            }
            src_dir->child_count--;
            break;
        }
    }
    
    kfree(src_node->name);
    u32 new_name_len = strlen(dst_fname);
    src_node->name = (char*)kmalloc(new_name_len + 1);
    if (src_node->name) {
        strcpy(src_node->name, dst_fname);
    }
    src_node->parent = dst_dir;
    
    cldramfs_add_child(dst_dir, src_node);
    
    kfree(src_temp);
    kfree(dst_temp);
}

void cldramfs_cmd_cp(const char *src, const char *dst) {
    if (!src || !dst) {
        vga_printf("cp: missing file operand\n");
        return;
    }
    
    u32 src_len = strlen(src);
    char *src_temp = (char*)kmalloc(src_len + 1);
    if (!src_temp) return;
    strcpy(src_temp, src);
    
    char *src_last_slash = strrchr(src_temp, '/');
    Node *src_dir;
    char *src_fname;
    
    if (src_last_slash) {
        *src_last_slash = '\0';
        src_dir = cldramfs_resolve_path_dir(src_temp, 0);
        src_fname = src_last_slash + 1;
    } else {
        src_dir = ramfs_cwd;
        src_fname = src_temp;
    }
    
    if (!src_dir) {
        vga_printf("cp: cannot stat '%s': No such file or directory\n", src);
        kfree(src_temp);
        return;
    }
    
    Node *src_node = cldramfs_find_child(src_dir, src_fname);
    if (!src_node) {
        vga_printf("cp: cannot stat '%s': No such file or directory\n", src);
        kfree(src_temp);
        return;
    }
    
    if (src_node->type == DIR_NODE) {
        vga_printf("cp: omitting directory '%s'\n", src);
        kfree(src_temp);
        return;
    }
    
    Node *dst_node = cldramfs_resolve_path_file(dst, 1);
    if (!dst_node) {
        vga_printf("cp: cannot create '%s'\n", dst);
        kfree(src_temp);
        return;
    }
    
    if (src_node->content && src_node->content_size > 0) {
        kfree(dst_node->content);
        dst_node->content = (char*)kmalloc(src_node->content_size + 1);
        if (dst_node->content) {
            strcpy(dst_node->content, src_node->content);
            dst_node->content_size = src_node->content_size;
        }
    } else {
        kfree(dst_node->content);
        dst_node->content = (char*)kmalloc(1);
        if (dst_node->content) {
            dst_node->content[0] = '\0';
            dst_node->content_size = 0;
        }
    }
    
    kfree(src_temp);
}

void cldramfs_cmd_exec(const char *arg) {
    if (!arg || strlen(arg) == 0) {
        vga_printf("exec: missing ELF file name\n");
        vga_printf("usage: exec <filename.o>\n");
        return;
    }
    
    // Find the file in ramfs
    Node *file_node = cldramfs_resolve_path_file(arg, 0);
    if (!file_node) {
        vga_printf("exec: file '%s' not found\n", arg);
        return;
    }
    
    if (file_node->type != FILE_NODE) {
        vga_printf("exec: '%s' is not a file\n", arg);
        return;
    }
    
    if (!file_node->content || file_node->content_size == 0) {
        vga_printf("exec: file '%s' is empty\n", arg);
        return;
    }
    
    // Load the ELF file
    loaded_elf_t loaded_elf;
    int result = elf_load(file_node->content, file_node->content_size, &loaded_elf);
    if (result != 0) {
        vga_printf("exec: failed to load ELF file '%s'\n", arg);
        return;
    }
    
    // Execute the ELF file
    result = elf_execute(&loaded_elf, arg);
    vga_printf("Program exited with code: %d\n", result);
    
    // Clean up
    // elf_unload(&loaded_elf);  // COMMENTED FOR NOW TO TEST
}
