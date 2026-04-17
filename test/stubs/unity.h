#pragma once
// Minimal Unity stub for native g++ compilation
#include <stdio.h>
#include <stdlib.h>

static int _unity_failures = 0;
static int _unity_tests    = 0;

#define UNITY_BEGIN() (_unity_failures = 0, _unity_tests = 0, 0)
#define UNITY_END()   (_unity_failures == 0 \
    ? (printf("\n%d tests, 0 failures\nOK\n", _unity_tests), 0) \
    : (printf("\n%d tests, %d failures\nFAIL\n", _unity_tests, _unity_failures), 1))

#define RUN_TEST(fn) do { \
    _unity_tests++; \
    setUp(); \
    printf("  %-40s ", #fn); \
    fn(); \
    printf("PASS\n"); \
    tearDown(); \
} while(0)

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n  [ASSERT] " #cond " at line %d\n", __LINE__); \
        _unity_failures++; return; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAIL\n  [ASSERT] expected=%d actual=%d (" #actual ") at line %d\n", \
               (int)(expected), (int)(actual), __LINE__); \
        _unity_failures++; return; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_UINT32(expected, actual) TEST_ASSERT_EQUAL(expected, actual)

#define TEST_ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        printf("FAIL\n  [ASSERT] expected false: " #cond " at line %d\n", __LINE__); \
        _unity_failures++; return; \
    } \
} while(0)