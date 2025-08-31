#include <cldtest.h>
#include <memory_mapper.h>
#include <string.h>
#include <kmalloc.h>

#include <vgaio.h>

#define TEST_PAGE_SIZE PAGE_4K

CLDTEST_SUITE(memory_tests) {
    // Suite initialization - this runs automatically
}

CLDTEST_WITH_SUITE("Memory map/unmap test", mm_simple_test, memory_tests) {
    u64 phys_addr = 0xa45000ULL;             // 4K aligned physical page
    u64 vaddr1    = 0xffffffff80045000ULL;   // canonical kernel VA #1
    u64 vaddr2    = 0xffffffff80046000ULL;   // canonical kernel VA #2
    const char *test_str = "map/unmap works!";

    assert(mm_map(vaddr1, phys_addr, PTE_RW, TEST_PAGE_SIZE));
    strcpy((char *)vaddr1, test_str);
    assert(mm_unmap(vaddr1, TEST_PAGE_SIZE));
    assert(mm_map(vaddr2, phys_addr, PTE_RW, TEST_PAGE_SIZE));
    assert(strcmp((char *)vaddr2, test_str) == 0);
}

CLDTEST_WITH_SUITE("Memory alignment test", mm_alignment_test, memory_tests) {
    u64 test_addr = 0xffffffff80050000ULL;
    assert(is_aligned(test_addr, PAGE_4K));
    assert(is_canonical(test_addr));
}

CLDTEST_SUITE(malloc_tests) {}

CLDTEST_WITH_SUITE("Basic kmalloc test", kmalloc_basic_test, malloc_tests) {
    void *ptr = kmalloc(64);
    assert(ptr != NULL);
    
    char *data = (char*)ptr;
    data[0] = 'T';
    data[63] = 'E';
    
    assert(data[0] == 'T');
    assert(data[63] == 'E');
    
    kfree(ptr);
}

CLDTEST_WITH_SUITE("Kmalloc alignment test", kmalloc_alignment_test, malloc_tests) {
    void *ptr1 = kmalloc(1);
    void *ptr2 = kmalloc(1);
    void *ptr3 = kmalloc(1);
    
    assert(ptr1 != NULL);
    assert(ptr2 != NULL);
    assert(ptr3 != NULL);
    
    uintptr_t addr1 = (uintptr_t)ptr1;
    uintptr_t addr2 = (uintptr_t)ptr2;
    uintptr_t addr3 = (uintptr_t)ptr3;
    
    assert((addr1 & 15) == 0);
    assert((addr2 & 15) == 0);
    assert((addr3 & 15) == 0);
    
    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
}

CLDTEST_WITH_SUITE("Large allocation test", kmalloc_large_allocation_test, malloc_tests) {
    vga_printf("Starting large allocation test\n");
    void *ptr = kmalloc(1024);
    vga_printf("Allocated 1024 bytes at: 0x%llx\n", (unsigned long long)ptr);
    assert(ptr != NULL);
    
    char *data = (char*)ptr;
    vga_printf("Writing test pattern...\n");
    for (int i = 0; i < 1024; i++) {
        if (i % 256 == 0) vga_printf("Writing byte %d\n", i);
        data[i] = (char)(i & 0xFF);
    }
    vga_printf("Write loop completed\n");
    
    vga_printf("Verifying test pattern...\n");
    for (int i = 0; i < 1024; i++) {
        assert(data[i] == (char)(i & 0xFF));
    }
    
    vga_printf("Pattern verification complete\n");
    kfree(ptr);
    vga_printf("Large allocation test completed\n");
}

CLDTEST_WITH_SUITE("Zero size malloc test", kmalloc_zero_size_test, malloc_tests) {
    void *ptr = kmalloc(0);
    assert(ptr == NULL);
}

CLDTEST_WITH_SUITE("Kmalloc free and reuse test", kmalloc_free_reuse_test, malloc_tests) {
    void *ptr1 = kmalloc(64);
    assert(ptr1 != NULL);
    
    char *data1 = (char*)ptr1;
    data1[0] = 'A';
    assert(data1[0] == 'A');
    
    kfree(ptr1);
    
    void *ptr2 = kmalloc(64);
    assert(ptr2 != NULL);
    
    char *data2 = (char*)ptr2;
    data2[0] = 'B';
    assert(data2[0] == 'B');
    
    kfree(ptr2);
}

CLDTEST_WITH_SUITE("Krealloc test", krealloc_test, malloc_tests) {
    void *ptr = kmalloc(64);
    assert(ptr != NULL);
    
    char *data = (char*)ptr;
    data[0] = 'R';
    data[63] = 'T';
    
    void *new_ptr = krealloc(ptr, 128);
    assert(new_ptr != NULL);
    
    char *new_data = (char*)new_ptr;
    assert(new_data[0] == 'R');
    assert(new_data[63] == 'T');
    
    new_data[127] = 'X';
    assert(new_data[127] == 'X');
    
    kfree(new_ptr);
}

CLDTEST_WITH_SUITE("Kernel malloc test", kernel_malloc_test, malloc_tests) {
    void *ptr = kmalloc(256);
    vga_printf("Kernel allocation at: 0x%llx\n", (unsigned long long)ptr);
    assert(ptr != NULL);
    
    u64 phys_addr = kmalloc_virt_to_phys(ptr);
    vga_printf("Physical address: 0x%llx\n", phys_addr);
    assert(phys_addr != 0);
    
    char *data = (char*)ptr;
    data[0] = 'K'; data[255] = 'M';
    assert(data[0] == 'K');
    assert(data[255] == 'M');
    
    kfree(ptr);
}



