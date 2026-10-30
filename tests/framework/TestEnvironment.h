// Offline test environment.
//
// The source tree, the asset root and the test fonts are provided by CMake at
// configure time (TestBuildConfig.h), never by local paths or environment
// variables. Tests never read the game installation, the developer profile or
// the process working directory to find these files.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace abctest {

// Absolute path of the AbcUi checkout that built this test.
std::filesystem::path sourceDir();

// Absolute path of the repository asset root (assets/).
std::filesystem::path assetsDir();

// assetsDir() / relative, as a forward-slash string.
std::string assetPath(std::string_view relative);

// Font files shipped with the RmlUi submodule (no OS font directory).
std::string defaultFontPath();
std::string monoFontPath();

// Scratch directory under the system temp root. Removed with the object.
class TempDir {
public:
  explicit TempDir(const char *prefix = "abcui-test");
  ~TempDir();

  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;

  const std::filesystem::path &path() const {
    return Path;
  }

  // Write a UTF-8 text file inside the directory and return its path.
  std::string write(const char *name, const char *content) const;

private:
  std::filesystem::path Path;
};

} // namespace abctest
