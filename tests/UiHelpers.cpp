#include "framework/TextUtil.h"
#include "framework/ResourceLimits.h"
#include <climits>
#include <abc/Format.h>
#include <cstdlib>
#include <iostream>
#include "framework/TestHarness.h"
int main() {
  using namespace framework::ResourceLimits;
  size_t bytes = 0;
  CHECK(!imageBytes(65536, 16384, bytes));
  CHECK(!imageBytes(INT_MAX, INT_MAX, bytes));
  CHECK(!imageBytes(-1, 1, bytes));
  CHECK(imageBytes(4096, 4096, bytes) && bytes == 64 * 1024 * 1024);
  int x, y, w, h;
  CHECK(crop(INT_MIN, INT_MIN, INT_MAX, INT_MAX, 64, 32, x, y, w, h));
  CHECK(x == 0 && y == 0 && w == 64 && h == 32);
  CHECK(!crop(65, 0, INT_MAX, 32, 64, 32, x, y, w, h));
  CHECK(!span(SIZE_MAX, 4, 10));
  CHECK(resourceName("custom/weapon_test"));
  CHECK(!resourceName("../escape"));
  CHECK(!resourceName("x;quit"));
  CHECK(!resourceName(std::string(128, 'x')));

  using namespace framework::TextUtil;
  CHECK(trimNewlines("").empty());
  CHECK(trimNewlines("\n\n").empty());
  CHECK(trimNewlines("\n\nhello\n") == "hello");
  CHECK(trimNewlines("a\nb") == "a\nb");
  std::string key, value;
  CHECK(parseTranslationLine(" key = value \r", key, value));
  CHECK(key == "key" && value == "value");
  CHECK(!parseTranslationLine(" # comment", key, value));
  CHECK(!parseTranslationLine(" = value", key, value));
  CHECK(escapeRml("<&>") == "&lt;&amp;&gt;");
  auto longName = std::string(4096, 'x');
  CHECK(abc::Format("{0}/{1}", longName, "a") == longName + "/a");
  return abctest::finish("abc_ui_helpers");
}
