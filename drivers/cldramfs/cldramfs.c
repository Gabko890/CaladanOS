#include "cldramfs.h"
#include <kmalloc.h>
#include <string.h>
#include <vgaio.h>

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

void cldramfs_cmd_echo(const char *text, const char *path) {
    if (!text || !path) return;
    
    Node *file = cldramfs_resolve_path_file(path, 1);
    if (!file) return;
    
    u32 text_len = strlen(text);
    kfree(file->content);
    file->content = (char*)kmalloc(text_len + 1);
    if (file->content) {
        strcpy(file->content, text);
        file->content_size = text_len;
    }
}