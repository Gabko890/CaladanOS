#ifndef CLDRAMFS_H
#define CLDRAMFS_H

#include <cldtypes.h>

typedef enum { FILE_NODE, DIR_NODE } NodeType;

typedef struct Node {
    char *name;
    NodeType type;
    char *content;
    u32 content_size;
    struct Node **children;
    u32 child_count;
    u32 child_capacity;
    struct Node *parent;
} Node;

// CPIO archive handling
struct cpio_header {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_chksum[8];
} __attribute__((packed));

// Global ramfs state
extern Node *ramfs_root;
extern Node *ramfs_cwd;

// Core ramfs functions
void cldramfs_init(void);
int cldramfs_load_cpio(void *cpio_data, u32 cpio_size);
Node* cldramfs_create_node(const char *name, NodeType type, Node *parent);
Node* cldramfs_find_child(Node *dir, const char *name);
void cldramfs_add_child(Node *parent, Node *child);
Node* cldramfs_resolve_path_dir(const char *path, int create_missing);
Node* cldramfs_resolve_path_file(const char *path, int create_dirs);
void cldramfs_free_node(Node *node);

// Shell command implementations  
void cldramfs_cmd_ls(const char *arg);
void cldramfs_cmd_cd(const char *arg);
void cldramfs_cmd_mkdir(const char *arg);
void cldramfs_cmd_touch(const char *arg);
void cldramfs_cmd_cat(const char *arg);
void cldramfs_cmd_echo(const char *args);
void cldramfs_cmd_rm(const char *arg);
void cldramfs_cmd_rmdir(const char *arg);
void cldramfs_cmd_mv(const char *src, const char *dst);
void cldramfs_cmd_cp(const char *src, const char *dst);
void cldramfs_cmd_exec(const char *arg);

#endif // CLDRAMFS_H
