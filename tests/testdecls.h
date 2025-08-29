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
extern void malloc_basic_test(void);
extern void malloc_alignment_test(void);
extern void malloc_large_allocation_test(void);
extern void malloc_zero_size_test(void);
extern void memory_system_init_test(void);

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
    cldtest_register_test("Basic malloc test", malloc_basic_test, "malloc_tests"); \
    cldtest_register_test("Malloc alignment test", malloc_alignment_test, "malloc_tests"); \
    cldtest_register_test("Large allocation test", malloc_large_allocation_test, "malloc_tests"); \
    cldtest_register_test("Zero size malloc test", malloc_zero_size_test, "malloc_tests"); \
    cldtest_register_test("Memory system init test", memory_system_init_test, "malloc_tests"); \
} while(0)

#endif // TESTDECLS_H
