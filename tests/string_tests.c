#include <cldtest.h>
#include <string.h>

CLDTEST_SUITE(string_tests) {
    // String test suite initialization
}

CLDTEST_WITH_SUITE("String length test", strlen_test, string_tests) {
    const char *test_str = "Hello World";
    assert(strlen(test_str) == 11);
    assert(strlen("") == 0);
}

CLDTEST_WITH_SUITE("String compare test", strcmp_test, string_tests) {
    assert(strcmp("hello", "hello") == 0);
    assert(strcmp("abc", "abd") < 0);
    assert(strcmp("xyz", "abc") > 0);
}

CLDTEST_WITH_SUITE("String copy test", strcpy_test, string_tests) {
    char buffer[64];
    const char *source = "Test string";
    
    strcpy(buffer, source);
    assert(strcmp(buffer, source) == 0);
}