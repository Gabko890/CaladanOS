#include <cldtest.h>
#include <cldtypes.h>

CLDTEST_SUITE(system_tests) {
    // System test suite initialization
}

CLDTEST_WITH_SUITE("Data types test", types_test, system_tests) {
    u8 test_u8 = 255;
    u16 test_u16 = 65535;
    u32 test_u32 = 0xFFFFFFFF;
    u64 test_u64 = 0xFFFFFFFFFFFFFFFFULL;
    
    assert(test_u8 == 255);
    assert(test_u16 == 65535);
    assert(test_u32 == 0xFFFFFFFF);
    assert(test_u64 == 0xFFFFFFFFFFFFFFFFULL);
}

CLDTEST_WITH_SUITE("Arithmetic test", arithmetic_test, system_tests) {
    u32 a = 10, b = 5;
    
    assert(a + b == 15);
    assert(a - b == 5);
    assert(a * b == 50);
    assert(a / b == 2);
    assert(a % b == 0);
}

CLDTEST_WITH_SUITE("Demo fail test", demo_fail_test, system_tests) {
    u32 x = 42;
    assert(x == 24); // This will fail
}