#pragma once
// Minimal Unity stub for native g++ compilation
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static int _unity_failures = 0;
static int _unity_tests = 0;
static const char* _unity_current_test = "UNKNOWN";

#define UNITY_BEGIN() (_unity_failures = 0, _unity_tests = 0, 0)

#define UNITY_END() \
  (_unity_failures == 0 \
    ? (printf("\n%d tests, 0 failures\nOK\n", _unity_tests), 0) \
    : (printf("\n%d tests, %d failures\nFAIL\n", _unity_tests, _unity_failures), 1))

#define RUN_TEST(fn) do { \
  _unity_tests++; \
  _unity_current_test = #fn; \
  int _fails_before = _unity_failures; \
  printf("\n==================================================\n"); \
  printf("TEST START: %s\n", _unity_current_test); \
  setUp(); \
  fn(); \
  tearDown(); \
  if (_unity_failures == _fails_before) { \
    printf("TEST END  : %s -> PASS\n", _unity_current_test); \
  } else { \
    printf("TEST END  : %s -> FAIL\n", _unity_current_test); \
  } \
  printf("==================================================\n"); \
} while (0)

#define TEST_FAIL_MSG(msg) do { \
  printf("FAIL\n  [TEST=%s] %s at line %d\n", _unity_current_test, msg, __LINE__); \
  _unity_failures++; \
  return; \
} while (0)

#define TEST_ASSERT_TRUE(cond) do { \
  if (!(cond)) { \
    printf("FAIL\n  [TEST=%s] expected true: %s at line %d\n", _unity_current_test, #cond, __LINE__); \
    _unity_failures++; \
    return; \
  } \
} while (0)

#define TEST_ASSERT_FALSE(cond) do { \
  if (cond) { \
    printf("FAIL\n  [TEST=%s] expected false: %s at line %d\n", _unity_current_test, #cond, __LINE__); \
    _unity_failures++; \
    return; \
  } \
} while (0)

#define TEST_ASSERT_EQUAL(expected, actual) do { \
  if ((expected) != (actual)) { \
    printf("FAIL\n  [TEST=%s] expected=%d actual=%d (%s) at line %d\n", \
           _unity_current_test, (int)(expected), (int)(actual), #actual, __LINE__); \
    _unity_failures++; \
    return; \
  } \
} while (0)

#define TEST_ASSERT_EQUAL_UINT32(expected, actual) do { \
  uint32_t _e = (uint32_t)(expected); \
  uint32_t _a = (uint32_t)(actual); \
  if (_e != _a) { \
    printf("FAIL\n  [TEST=%s] expected=%lu actual=%lu (%s) at line %d\n", \
           _unity_current_test, (unsigned long)_e, (unsigned long)_a, #actual, __LINE__); \
    _unity_failures++; \
    return; \
  } \
} while (0)