#include "cldtest.h"
#include <string.h>

test_case_t all_tests[MAX_TESTS];
test_suite_t test_suites[MAX_SUITES];
u32 total_test_count = 0;
u32 suite_count = 0;
const char *current_suite = "";
const char *current_test_name = "";
u8 current_test_failed = 0;

void cldtest_clear_registry(void) {
    total_test_count = 0;
    suite_count = 0;
}

void cldtest_register_suite(const char *name, suite_init_func_t init_func) {
    if (suite_count >= MAX_SUITES) {
        return;
    }
    
    // Check for duplicates
    for (u32 i = 0; i < suite_count; i++) {
        if (strcmp(test_suites[i].name, name) == 0) {
            return; // Already registered
        }
    }
    
    test_suites[suite_count].name = name;
    test_suites[suite_count].init_func = init_func;
    test_suites[suite_count].test_count = 0;
    test_suites[suite_count].passed_count = 0;
    suite_count++;
}

void cldtest_register_test(const char *name, test_func_t func, const char *suite_name) {
    if (total_test_count >= MAX_TESTS) {
        return;
    }
    
    // Check for duplicates
    for (u32 i = 0; i < total_test_count; i++) {
        if (strcmp(all_tests[i].name, name) == 0 && 
            strcmp(all_tests[i].suite_name, suite_name) == 0) {
            return; // Already registered
        }
    }
    
    all_tests[total_test_count].name = name;
    all_tests[total_test_count].func = func;
    all_tests[total_test_count].suite_name = suite_name;
    total_test_count++;
}

void cldtest_init_all_suites(void) {
    for (u32 i = 0; i < suite_count; i++) {
        current_suite = test_suites[i].name;
        if (test_suites[i].init_func) {
            test_suites[i].init_func();
        }
    }
    current_suite = "";
}

void cldtest_run_test(const char *test_name) {
    for (u32 i = 0; i < total_test_count; i++) {
        if (strcmp(all_tests[i].name, test_name) == 0) {
            current_test_name = all_tests[i].name;
            current_test_failed = 0;
            
            all_tests[i].func();
            
            if (!current_test_failed) {
                vga_printf("[");
                vga_attr(VGA_COLOR_LIGHT_GREEN);
                vga_printf("  OK  ");
                vga_attr(0x07);
                vga_printf("] %s\n", current_test_name);
            }
            return;
        }
    }
}

void cldtest_run_suite(const char *suite_name) {
    u32 suite_tests = 0;
    u32 suite_passed = 0;
    
    for (u32 i = 0; i < total_test_count; i++) {
        if (strcmp(all_tests[i].suite_name, suite_name) == 0) {
            suite_tests++;
            current_test_name = all_tests[i].name;
            current_test_failed = 0;
            
            all_tests[i].func();
            
            if (!current_test_failed) {
                vga_printf("[");
                vga_attr(VGA_COLOR_LIGHT_GREEN);
                vga_printf("  OK  ");
                vga_attr(0x07);
                vga_printf("] %s\n", current_test_name);
                suite_passed++;
            }
        }
    }
}

void cldtest_run_all(void) {
    u32 total_passed = 0;
    u32 total_tests_run = 0;
    
    for (u32 i = 0; i < suite_count; i++) {
        const char *suite_name = test_suites[i].name;
        
        for (u32 j = 0; j < total_test_count; j++) {
            if (strcmp(all_tests[j].suite_name, suite_name) == 0) {
                total_tests_run++;
                current_test_name = all_tests[j].name;
                current_test_failed = 0;
                
                all_tests[j].func();
                
                if (!current_test_failed) {
                    vga_printf("[");
                    vga_attr(VGA_COLOR_LIGHT_GREEN);
                    vga_printf("  OK  ");
                    vga_attr(0x07);
                    vga_printf("] %s\n", current_test_name);
                    total_passed++;
                }
            }
        }
    }
    
    if (total_passed == total_tests_run) {
        vga_attr(VGA_COLOR_LIGHT_GREEN);
        vga_printf("All %u tests passed\n", total_tests_run);
    } else {
        vga_attr(VGA_COLOR_LIGHT_RED);
        vga_printf("%u/%u tests failed\n", total_tests_run - total_passed, total_tests_run);
    }
    vga_attr(0x07); // Reset to kernel default
}