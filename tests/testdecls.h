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

#define CLDTEST_INIT() do { \
    cldtest_register_suite("memory_tests", 0); \
    cldtest_register_suite("string_tests", 0); \
    cldtest_register_suite("system_tests", 0); \
    cldtest_register_test("Memory map/unmap test", mm_simple_test, "memory_tests"); \
    cldtest_register_test("Memory alignment test", mm_alignment_test, "memory_tests"); \
    cldtest_register_test("String length test", strlen_test, "string_tests"); \
    cldtest_register_test("String compare test", strcmp_test, "string_tests"); \
    cldtest_register_test("String copy test", strcpy_test, "string_tests"); \
    cldtest_register_test("Data types test", types_test, "system_tests"); \
    cldtest_register_test("Arithmetic test", arithmetic_test, "system_tests"); \
    cldtest_register_test("Demo fail test", demo_fail_test, "system_tests"); \
} while(0)

#endif // TESTDECLS_H