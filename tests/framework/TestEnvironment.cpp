#include "framework/TestEnvironment.h"

#include "TestBuildConfig.h"

#include <cstdio>
#include <fstream>
#include <process.h>

namespace abctest {
namespace {

int TempCounter = 0;

} // namespace

std::filesystem::path sourceDir() {
  return std::filesystem::path(ABCUI_TEST_SOURCE_DIR);
}

std::filesystem::path assetsDir() {
  return sourceDir() / "assets";
}

std::string assetPath(std::string_view relative) {
  return (assetsDir() / std::filesystem::path(relative)).string();
}

std::string defaultFontPath() {
  return ABCUI_TEST_FONT_SANS;
}

std::string monoFontPath() {
  return ABCUI_TEST_FONT_MONO;
}

TempDir::TempDir(const char *prefix) {
  const char *safePrefix = prefix && *prefix ? prefix : "abcui-test";
  const auto root = std::filesystem::temp_directory_path();
  for (;;) {
    const std::string name = std::string(safePrefix) + "-" +
                             std::to_string(::_getpid()) + "-" +
                             std::to_string(++TempCounter);
    Path = root / name;
    std::error_code error;
    if (std::filesystem::create_directory(Path, error))
      break;
    if (error)
      break;
  }
}

TempDir::~TempDir() {
  std::error_code error;
  std::filesystem::remove_all(Path, error);
}

std::string TempDir::write(const char *name, const char *content) const {
  const auto file = Path / name;
  std::ofstream stream(file, std::ios::binary | std::ios::trunc);
  if (stream && content)
    stream << content;
  return file.string();
}

} // namespace abctest
