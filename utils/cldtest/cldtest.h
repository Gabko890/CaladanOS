#ifndef CLDTEST_H
#define CLDTEST_H

#include <cldtypes.h>
#include <vgaio.h>

#define MAX_TESTS 128
#define MAX_SUITES 32

// VGA Color definitions for colorful output
#define VGA_COLOR_BLACK         0x00
#define VGA_COLOR_BLUE          0x01
#define VGA_COLOR_GREEN         0x02
#define VGA_COLOR_CYAN          0x03
#define VGA_COLOR_RED           0x04
#define VGA_COLOR_MAGENTA       0x05
#define VGA_COLOR_BROWN         0x06
#define VGA_COLOR_LIGHT_GREY    0x07
#define VGA_COLOR_DARK_GREY     0x08
#define VGA_COLOR_LIGHT_BLUE    0x09
#define VGA_COLOR_LIGHT_GREEN   0x0A
#define VGA_COLOR_LIGHT_CYAN    0x0B
#define VGA_COLOR_LIGHT_RED     0x0C
#define VGA_COLOR_LIGHT_MAGENTA 0x0D
#define VGA_COLOR_YELLOW        0x0E
#define VGA_COLOR_WHITE         0x0F

typedef void (*test_func_t)(void);
typedef void (*suite_init_func_t)(void);

typedef struct {
    const char *name;
    test_func_t func;
    const char *suite_name;
} test_case_t;

typedef struct {
    const char *name;
    suite_init_func_t init_func;
    u32 test_count;
    u32 passed_count;
} test_suite_t;

extern test_case_t all_tests[MAX_TESTS];
extern test_suite_t test_suites[MAX_SUITES];
extern u32 total_test_count;
extern u32 suite_count;
extern const char *current_suite;

#ifdef CLDTEST_ENABLED

extern const char *current_test_name;
extern u8 current_test_failed;

#define assert(condition) \
    do { \
        if (!(condition)) { \
            vga_printf("["); \
            vga_attr(VGA_COLOR_LIGHT_RED); \
            vga_printf("FAILED"); \
            vga_attr(0x07); \
            vga_printf("] %s at %s:%d\n", \
                       current_test_name, __FILE__, __LINE__); \
            current_test_failed = 1; \
            return; \
        } \
    } while (0)

#define CLDTEST_SUITE(suite_name) \
    static void cldtest_dummy_##suite_name(void)

#define CLDTEST_WITH_SUITE(test_name, test_func, suite_name) \
    void test_func(void)

// Include test function declarations
#include "../../tests/testdecls.h"

#define CLDTEST_RUN_ALL() cldtest_run_all()
#define CLDTEST_RUN_TEST(__testname__) cldtest_run_test(__testname__)
#define CLDTEST_RUN_SUITE(__suitename__) cldtest_run_suite(__suitename__)

void cldtest_register_suite(const char *name, suite_init_func_t init_func);
void cldtest_register_test(const char *name, test_func_t func, const char *suite_name);
void cldtest_run_all(void);
void cldtest_run_suite(const char *suite_name);
void cldtest_run_test(const char *test_name);
void cldtest_init_all_suites(void);
void cldtest_clear_registry(void);

#else

// When tests disabled, macros expand to nothing
#define assert(condition)
#define CLDTEST_SUITE(suite_name)
#define CLDTEST_WITH_SUITE(test_name, test_func, suite_name)
#define CLDTEST_INIT()
#define CLDTEST_RUN_ALL()
#define CLDTEST_RUN_TEST(__testname__)
#define CLDTEST_RUN_SUITE(__suitename__)

#endif

#endif // CLDTEST_H
