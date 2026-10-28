#include "ActionModels.h"
#include "Options.h"

#include "modsys/ModuleVfs.h"
#include <abc/Console.h>
#include <abc/Paths.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <windows.h>

namespace modelpreview {
namespace {

bool materialize(const framework::PackageEntry &entry,
                 const std::filesystem::path &path) {
  if (path.empty())
    return false;
  // Do not rewrite unchanged files, including while the engine has them open.
  std::ifstream current(path, std::ios::binary | std::ios::ate);
  if (current && current.tellg() == static_cast<std::streamoff>(entry.size)) {
    std::string bytes(entry.size, '\0');
    current.seekg(0);
    if (current.read(bytes.data(),
                     static_cast<std::streamsize>(bytes.size())) &&
        std::equal(bytes.begin(), bytes.end(),
                   static_cast<const char *>(entry.data)))
      return true;
  }
  current.close();
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error)
    return false;
  auto temporary = path;
  temporary += ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  output.write(static_cast<const char *>(entry.data),
               static_cast<std::streamsize>(entry.size));
  output.close();
  const bool complete =
      output && MoveFileExW(temporary.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  if (!complete)
    std::filesystem::remove(temporary, error);
  return complete;
}

} // namespace

void prepareActionModels(const char *directory) {
  std::string prefix = directory;
  if (!prefix.ends_with('/'))
    prefix += '/';
  for (const auto &uri : framework::ModuleVfs::list(directory)) {
    const std::string name = uri.substr(prefix.size());
    // Only direct model children; archive paths never become output paths.
    if (name.find('/') != std::string::npos || !Options::isModelPath(name))
      continue;
    framework::PackageEntry entry;
    if (!framework::ModuleVfs::open(uri.c_str(), entry, nullptr) ||
        entry.size == 0 || entry.size > 16 * 1024 * 1024 ||
        !materialize(
            entry, abc::ModPath((std::string(kActionModelDir) + name).c_str())))
      abc::ConsoleWarn("[AbcUi] menu action model unavailable: %s\n",
                       name.c_str());
  }
}

} // namespace modelpreview
