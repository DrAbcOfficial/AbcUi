// abc_ui_text_encoding - T07 step 3 acceptance for the UTF-8 boundary
// hygiene. Links the production TextEncoding.cpp; every case feeds the
// byte shapes the engine's 32-byte SDL text-event buffers actually produce:
// ASCII, BMP Chinese, supplementary-plane (4-byte UTF-8), the byte-truncated
// tail a long composition leaves behind, overlong encodings, UTF-16
// surrogate halves (CESU-8 - what a WM_IME-style producer would emit
// wrong), and out-of-range sequences.
#include "framework/TextEncoding.h"

#include <iostream>
#include <string>
#include "framework/TestHarness.h"

namespace {

std::string clean(const char *data, size_t length) {
  const auto view = framework::text::sanitizeUtf8(data, length);
  return std::string(view.begin(), view.end());
}

std::string clean(const char *data) {
  const auto view = framework::text::sanitizeUtf8(data);
  return std::string(view.begin(), view.end());
}

} // namespace

int main() {
  {
    TEST_CASE("ascii-passthrough");
    CHECK(clean("Hello _world_ 123!") == "Hello _world_ 123!");
  }

  {
    TEST_CASE("bmp-chinese");
    // "中文" = E4 B8 AD E6 96 87
    const char text[] = "\xE4\xB8\xAD\xE6\x96\x87";
    CHECK(clean(text, sizeof(text) - 1) == std::string(text, 6));
    const char mixed[] = "a\xE4\xB8\xAD"
                         "b";
    CHECK(clean(mixed, sizeof(mixed) - 1) == std::string(mixed, 5));
  }

  {
    TEST_CASE("supplementary-plane");
    // U+1F600 = F0 9F 98 80 - one code point, four bytes, kept as-is.
    const char emoji[] = "\xF0\x9F\x98\x80";
    const std::string kept = clean(emoji, 4);
    CHECK(kept.size() == 4);
    CHECK(kept == std::string(emoji, 4));
    // Two supplementary chars in one event buffer survive both.
    const char pair[] = "\xF0\x9F\x98\x80\xF0\x9F\x98\x80";
    CHECK(clean(pair, 8).size() == 8);
  }

  {
    TEST_CASE("truncated-tail");
    // The 31-byte event-buffer cut: a partial multi-byte sequence at the
    // end is dropped, the valid prefix stays.
    CHECK(clean("ab\xE4\xB8", 4) == "ab");
    CHECK(clean("\xF0\x9F\x98", 3) == "");
    // Truncated in the middle keeps the later restart impossible - the
    // sanitizer stops at the first bad byte by design (a later event
    // resends the char whole).
    CHECK(clean("\xE4\xB8\xAD\xE6\x96", 5) == "\xE4\xB8\xAD");
  }

  {
    TEST_CASE("overlong-rejected");
    // C0 80 - overlong NUL.
    CHECK(clean("\xC0\x80", 2) == "");
    // E0 80 AD - overlong U+00AD.
    CHECK(clean("\xE0\x80\xAD", 3) == "");
  }

  {
    TEST_CASE("surrogate-halves-rejected");
    // ED A0 BD ED B8 80 - CESU-8 encoding of U+1F600 via UTF-16 surrogate
    // halves. Well-formed bytes, illegal UTF-8: dropped entirely.
    CHECK(clean("\xED\xA0\xBD\xED\xB8\x80", 6) == "");
    // A lone half among valid text kills only the bad run onward.
    CHECK(clean("\xE4\xB8\xAD\xED\xA0\xBD", 6) == "\xE4\xB8\xAD");
  }

  {
    TEST_CASE("out-of-range-rejected");
    // F4 90 80 80 - U+110000, above the Unicode max.
    CHECK(clean("\xF4\x90\x80\x80", 4) == "");
    // F8+ lead bytes never start a sequence.
    CHECK(clean("\xF8\x88\x80\x80\x80", 5) == "");
  }

  {
    TEST_CASE("nul-terminated-overload");
    // The engine buffers are NUL-terminated: the char* overload stops
    // there instead of walking the fixed-size event storage.
    char buffer[32] = {};
    std::memcpy(buffer, "ok\xE4\xB8\xAD", 5);
    CHECK(clean(buffer) == "ok\xE4\xB8\xAD");
    CHECK(clean(static_cast<const char *>(nullptr)) == "");
  }

  {
    TEST_CASE("count-characters");
    // Byte offsets -> caret indices: 'a'(1) + 中(3) + U+1F600(4) = 3.
    const char text[] = "a\xE4\xB8\xAD\xF0\x9F\x98\x80";
    CHECK(framework::text::countCharacters(std::string_view(text, 8)) == 3);
    // Cursor conversion prefix lengths agree.
    CHECK(framework::text::countCharacters(std::string_view(text, 1)) == 1);
    CHECK(framework::text::countCharacters(std::string_view(text, 4)) == 2);
    // Malformed input stops the count (never runs past the bad byte):
    // E4 B8 opens a sequence that "z" breaks - zero complete chars.
    CHECK(framework::text::countCharacters(std::string_view("\xE4\xB8z", 3)) ==
          0);
    CHECK(framework::text::countCharacters(std::string_view()) == 0);
  }

  return abctest::finish("abc_ui_text_encoding");
}
