// Shared offline test harness.
//
// Every executable in this directory includes this header and ends with
// abctest::finish(). The harness owns the failure counter, the current case
// label and the exit code, so the suite output and the CTest contract stay
// identical across all tests:
//
//   TEST_CASE("shipped-definition");
//   CHECK(model.parse(text));
//   return abctest::finish("abc_ui_command_menu");
//
// A failed CHECK reports the case, the source line and the expression and
// keeps running (so one run shows every failing check in a case); the process
// still exits non-zero from finish().
#pragma once

#include <cmath>
#include <cstddef>

namespace abctest {

// Number of failed checks recorded so far.
int failureCount();

// Current case label shown in failure reports. TEST_CASE resets the step.
void setCase(const char *name);
void setStep(int step);

// Record one failed check. Prefer the CHECK macro below.
void fail(int line, const char *expression);
void failNear(int line, const char *valueExpression,
              const char *expectedExpression, double value, double expected,
              double tolerance);

// Record a named expectation (used by table-driven audit tests).
void expect(bool condition, const char *name);

// Print the final PASS/FAIL line and return the process exit code.
int finish(const char *suite);

} // namespace abctest

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      ::abctest::fail(__LINE__, #condition);                                   \
  } while (0)

#define CHECK_NEAR(value, expected, tolerance)                                 \
  do {                                                                         \
    const double abctest_value = (value);                                      \
    const double abctest_expected = (expected);                                \
    const double abctest_tolerance = (tolerance);                              \
    if (!(std::fabs(abctest_value - abctest_expected) <= abctest_tolerance))   \
      ::abctest::failNear(__LINE__, #value, #expected, abctest_value,          \
                          abctest_expected, abctest_tolerance);                \
  } while (0)

#define EXPECT_NAMED(condition, name) ::abctest::expect((condition), (name))

#define TEST_CASE(name) ::abctest::setCase(name)
#define TEST_STEP(step) ::abctest::setStep(step)
