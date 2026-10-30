#include "framework/TestHarness.h"

#include <cstdio>
#include <string>

namespace abctest {
namespace {

int Failures = 0;
std::string CurrentCase = "unnamed";
int CurrentStep = 0;

void printCase(FILE *stream) {
  std::fprintf(stream, "[%s]", CurrentCase.c_str());
  if (CurrentStep != 0)
    std::fprintf(stream, " step %d", CurrentStep);
}

} // namespace

int failureCount() {
  return Failures;
}

void setCase(const char *name) {
  CurrentCase = name ? name : "";
  CurrentStep = 0;
}

void setStep(int step) {
  CurrentStep = step;
}

void fail(int line, const char *expression) {
  ++Failures;
  std::fprintf(stderr, "FAIL ");
  printCase(stderr);
  std::fprintf(stderr, " line %d: %s\n", line, expression);
}

void failNear(int line, const char *valueExpression,
              const char *expectedExpression, double value, double expected,
              double tolerance) {
  ++Failures;
  std::fprintf(stderr, "FAIL ");
  printCase(stderr);
  std::fprintf(stderr,
               " line %d: %s == %s (value %.17g, expected %.17g, tolerance "
               "%.17g)\n",
               line, valueExpression, expectedExpression, value, expected,
               tolerance);
}

void expect(bool condition, const char *name) {
  if (condition) {
    std::printf("PASS %s\n", name);
    return;
  }
  ++Failures;
  std::printf("FAIL %s\n", name);
}

int finish(const char *suite) {
  const char *name = suite ? suite : "suite";
  if (Failures != 0) {
    std::fprintf(stderr, "FAIL %s: %d check(s) failed\n", name, Failures);
    return 1;
  }
  std::printf("PASS %s: all checks passed\n", name);
  return 0;
}

} // namespace abctest
