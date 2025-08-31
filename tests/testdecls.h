#ifndef TESTDECLS_H
#define TESTDECLS_H

// Forward declarations for all test functions
extern void mm_simple_test(void);
extern void mm_alignment_test(void);
extern void strlen_test(void);
extern void strcmp_test(void);
extern void strcpy_test(void);
extern void types_test(void);
extern void arithmetic_test(void);
extern void demo_fail_test(void);

// Memory allocation test functions
extern void kmalloc_basic_test(void);
extern void kmalloc_alignment_test(void);
extern void kmalloc_large_allocation_test(void);
extern void kmalloc_zero_size_test(void);
extern void kmalloc_free_reuse_test(void);
extern void krealloc_test(void);
extern void kernel_malloc_test(void);

#define CLDTEST_INIT() do { \
    cldtest_register_suite("memory_tests", 0); \
    cldtest_register_suite("string_tests", 0); \
    cldtest_register_suite("system_tests", 0); \
    cldtest_register_suite("malloc_tests", 0); \
    cldtest_register_test("Memory map/unmap test", mm_simple_test, "memory_tests"); \
    cldtest_register_test("Memory alignment test", mm_alignment_test, "memory_tests"); \
    cldtest_register_test("String length test", strlen_test, "string_tests"); \
    cldtest_register_test("String compare test", strcmp_test, "string_tests"); \
    cldtest_register_test("String copy test", strcpy_test, "string_tests"); \
    cldtest_register_test("Data types test", types_test, "system_tests"); \
    cldtest_register_test("Arithmetic test", arithmetic_test, "system_tests"); \
    cldtest_register_test("Demo fail test", demo_fail_test, "system_tests"); \
    cldtest_register_test("Basic kmalloc test", kmalloc_basic_test, "malloc_tests"); \
    cldtest_register_test("Kmalloc alignment test", kmalloc_alignment_test, "malloc_tests"); \
    cldtest_register_test("Large allocation test", kmalloc_large_allocation_test, "malloc_tests"); \
    cldtest_register_test("Zero size malloc test", kmalloc_zero_size_test, "malloc_tests"); \
    cldtest_register_test("Kmalloc free and reuse test", kmalloc_free_reuse_test, "malloc_tests"); \
    cldtest_register_test("Krealloc test", krealloc_test, "malloc_tests"); \
    cldtest_register_test("Kernel malloc test", kernel_malloc_test, "malloc_tests"); \
} while(0)

#endif // TESTDECLS_H
