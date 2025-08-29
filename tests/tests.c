#include <cldtest.h>
#include <memory_mapper.h>
#include <string.h>
#include <simple_malloc.h>

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

CLDTEST_WITH_SUITE("Basic malloc test", malloc_basic_test, malloc_tests) {
    void *ptr = simple_malloc(64);
    assert(ptr != NULL);
    
    char *data = (char*)ptr;
    data[0] = 'T';
    data[63] = 'E';
    
    assert(data[0] == 'T');
    assert(data[63] == 'E');
    
    simple_free(ptr);
}

CLDTEST_WITH_SUITE("Malloc alignment test", malloc_alignment_test, malloc_tests) {
    void *ptr1 = simple_malloc(1);
    void *ptr2 = simple_malloc(1);
    void *ptr3 = simple_malloc(1);
    
    assert(ptr1 != NULL);
    assert(ptr2 != NULL);
    assert(ptr3 != NULL);
    
    uintptr_t addr1 = (uintptr_t)ptr1;
    uintptr_t addr2 = (uintptr_t)ptr2;
    uintptr_t addr3 = (uintptr_t)ptr3;
    
    assert((addr1 & 15) == 0);
    assert((addr2 & 15) == 0);
    assert((addr3 & 15) == 0);
    
    simple_free(ptr1);
    simple_free(ptr2);
    simple_free(ptr3);
}

CLDTEST_WITH_SUITE("Large allocation test", malloc_large_allocation_test, malloc_tests) {
    vga_printf("Starting large allocation test\n");
    void *ptr = simple_malloc(1024);
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
    simple_free(ptr);
    vga_printf("Large allocation test completed\n");
}

CLDTEST_WITH_SUITE("Zero size malloc test", malloc_zero_size_test, malloc_tests) {
    void *ptr = simple_malloc(0);
    assert(ptr == NULL);
}

CLDTEST_WITH_SUITE("Memory system init test", memory_system_init_test, malloc_tests) {
    void *test1 = simple_malloc(64);
    void *test2 = simple_malloc(128);
    void *test3 = simple_malloc(256);
    
    assert(test1 != NULL);
    assert(test2 != NULL);
    assert(test3 != NULL);
    
    vga_printf("memory system init test\n");

    char *data = (char*)test1;
    data[0] = 'H'; data[1] = 'i'; data[2] = '\0';
    
    assert(data[0] == 'H');
    assert(data[1] == 'i');
    assert(data[2] == '\0');
    
    simple_free(test1);
    simple_free(test2);
    simple_free(test3);
}
