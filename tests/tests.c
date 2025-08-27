#include <cldtest.h>
#include <memory_mapper.h>
#include <string.h>

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