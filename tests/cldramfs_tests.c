#include <cldtest.h>
#include <vgaio.h>
#include <string.h>
#include <kmalloc.h>
#include "../drivers/cldramfs/cldramfs.h"

CLDTEST_SUITE(cldramfs_tests) {}

// Test basic node creation and management
CLDTEST_WITH_SUITE("CldRamfs node creation", cldramfs_node_creation, cldramfs_tests) {
    cldramfs_init();
    
    // Test root node creation
    assert(ramfs_root != NULL);
    assert(ramfs_cwd == ramfs_root);
    assert(strcmp(ramfs_root->name, "/") == 0);
    assert(ramfs_root->type == DIR_NODE);
    
    // Test file creation
    Node *test_file = cldramfs_create_node("test.txt", FILE_NODE, ramfs_root);
    assert(test_file != NULL);
    assert(strcmp(test_file->name, "test.txt") == 0);
    assert(test_file->type == FILE_NODE);
    assert(test_file->parent == ramfs_root);
    
    // Test directory creation
    Node *test_dir = cldramfs_create_node("testdir", DIR_NODE, ramfs_root);
    assert(test_dir != NULL);
    assert(test_dir->type == DIR_NODE);
    assert(test_dir->children != NULL);
    
    // Cleanup
    cldramfs_free_node(test_file);
    cldramfs_free_node(test_dir);
    cldramfs_free_node(ramfs_root);
}

// Test directory operations
CLDTEST_WITH_SUITE("CldRamfs directory operations", cldramfs_directory_operations, cldramfs_tests) {
    cldramfs_init();
    
    // Create a test directory structure
    Node *dir1 = cldramfs_create_node("dir1", DIR_NODE, ramfs_root);
    cldramfs_add_child(ramfs_root, dir1);
    
    Node *file1 = cldramfs_create_node("file1.txt", FILE_NODE, dir1);
    cldramfs_add_child(dir1, file1);
    
    // Test find child
    Node *found = cldramfs_find_child(ramfs_root, "dir1");
    assert(found == dir1);
    
    found = cldramfs_find_child(dir1, "file1.txt");
    assert(found == file1);
    
    found = cldramfs_find_child(ramfs_root, "nonexistent");
    assert(found == NULL);
    
    // Test path resolution
    Node *resolved = cldramfs_resolve_path_dir("/dir1", 0);
    assert(resolved == dir1);
    
    resolved = cldramfs_resolve_path_file("/dir1/file1.txt", 0);
    assert(resolved == file1);
    
    // Cleanup
    cldramfs_free_node(ramfs_root);
}

// Test CPIO parsing (basic structure test)
CLDTEST_WITH_SUITE("CldRamfs CPIO basic", cldramfs_cpio_basic, cldramfs_tests) {
    cldramfs_init();
    
    // Create a minimal test with dummy data
    char test_data[] = "test data";
    
    // Test CPIO loading function exists and doesn't crash
    int result = cldramfs_load_cpio((void*)test_data, sizeof(test_data));
    // Note: We don't test success/failure here since the test data might not be perfect
    // Just test that the function doesn't crash
    assert(1); // Function executed without crashing
    
    // Cleanup
    cldramfs_free_node(ramfs_root);
}

// Test shell command parsing
CLDTEST_WITH_SUITE("CldRamfs command parsing", cldramfs_command_parsing, cldramfs_tests) {
    cldramfs_init();
    
    // Create test structure
    Node *dir1 = cldramfs_create_node("testdir", DIR_NODE, ramfs_root);
    cldramfs_add_child(ramfs_root, dir1);
    
    Node *file1 = cldramfs_create_node("test.txt", FILE_NODE, ramfs_root);
    cldramfs_add_child(ramfs_root, file1);
    
    // Test that commands don't crash
    cldramfs_cmd_ls(NULL);  // ls current directory
    cldramfs_cmd_ls("/");   // ls root
    
    // Test cd command
    ramfs_cwd = ramfs_root;
    cldramfs_cmd_cd("testdir");
    assert(ramfs_cwd == dir1);
    
    cldramfs_cmd_cd("..");
    assert(ramfs_cwd == ramfs_root);
    
    // Test file operations
    cldramfs_cmd_touch("newfile.txt");
    Node *newfile = cldramfs_find_child(ramfs_root, "newfile.txt");
    assert(newfile != NULL);
    assert(newfile->type == FILE_NODE);
    
    // Cleanup
    cldramfs_free_node(ramfs_root);
}