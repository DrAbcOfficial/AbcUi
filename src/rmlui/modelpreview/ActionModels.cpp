#include "ActionModels.h"
#include "Options.h"

#include "modsys/ModuleVfs.h"
#include <abc/Console.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <windows.h>

namespace modelpreview {
namespace {

bool materialize(const framework::PackageEntry &entry,
                 const std::filesystem::path &path) {
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
  // Without replacement: a file that appeared meanwhile stays.
  const bool complete = output && MoveFileExW(temporary.c_str(), path.c_str(),
                                              MOVEFILE_WRITE_THROUGH);
  if (!complete)
    std::filesystem::remove(temporary, error);
  return complete;
}

} // namespace

void prepareActionModels(const char *directory, const NativePath &path) {
  const std::string target = actionModelDir();
  // Without the directory (no resource root declared) there is nowhere to
  // write to.
  if (target.empty())
    return;
  std::string prefix = directory;
  if (!prefix.ends_with('/'))
    prefix += '/';
  for (const auto &uri : framework::ModuleVfs::list(directory)) {
    const std::string name = uri.substr(prefix.size());
    // Only direct model children; archive paths never become output paths.
    if (name.find('/') != std::string::npos || !Options::isModelPath(name))
      continue;
    // A file already there is the user's, or one an earlier run wrote: it
    // is never rewritten, and through the overlay it replaces this entry.
    const std::string native = path ? path(target + name) : std::string();
    std::error_code error;
    if (!native.empty() && std::filesystem::exists(native, error))
      continue;
    framework::PackageEntry entry;
    if (native.empty() ||
        !framework::ModuleVfs::open(uri.c_str(), entry, nullptr) ||
        entry.size == 0 || entry.size > 16 * 1024 * 1024 ||
        !materialize(entry, native))
      abc::ConsoleWarn("[AbcUi] menu action model unavailable: %s\n",
                       name.c_str());
  }
}

} // namespace modelpreview
