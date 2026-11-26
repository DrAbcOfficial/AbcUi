#include "ActionModels.h"
#include "Options.h"

#include "modsys/ModuleVfs.h"
#include <abc/Console.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

#include <windows.h>

namespace modelpreview {
namespace {

// The bytes of `path`; false when it cannot be read whole.
bool readFile(const std::filesystem::path &path, std::string &out) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream)
    return false;
  const std::streamoff size = stream.tellg();
  if (size <= 0)
    return false;
  out.resize(static_cast<size_t>(size));
  stream.seekg(0);
  return static_cast<bool>(stream.read(out.data(), size));
}

// Writes `bytes` beside `path`, then renames them into place. A file already
// there is replaced only when `replace` is set: the extraction never does.
bool writeFile(std::string_view bytes, const std::filesystem::path &path,
               bool replace) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error)
    return false;
  auto temporary = path;
  temporary += ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) {
    std::filesystem::remove(temporary, error);
    return false;
  }
  const bool complete = MoveFileExW(
      temporary.c_str(), path.c_str(),
      MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0));
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
    // Without replacement: a model that appeared there meanwhile stays.
    if (native.empty() ||
        !framework::ModuleVfs::open(uri.c_str(), entry, nullptr) ||
        entry.size == 0 || entry.size > 16 * 1024 * 1024 ||
        !writeFile({static_cast<const char *>(entry.data), entry.size}, native,
                   false))
      abc::ConsoleWarn("[AbcUi] menu action model unavailable: %s\n",
                       name.c_str());
  }
}

void mirrorActionModels(const NativePath &path) {
  const std::string directory = actionModelDir();
  if (directory.empty())
    return;
  const std::string source = path ? path(directory) : std::string();
  const std::string mirror =
      path ? path(std::string(kActionModelLoadDir)) : std::string();
  if (source.empty() || mirror.empty()) {
    abc::ConsoleWarn(
        "[AbcUi] menu action models have no path for the engine's copy\n");
    return;
  }
  // What the mirror holds when this is done.
  std::set<std::string> copied;
  std::error_code error;
  std::error_code kind;
  for (std::filesystem::directory_iterator it(source, error), end; it != end;
       it.increment(error)) {
    const std::string name = it->path().filename().string();
    // Only the direct model children: nothing else there is a group.
    if (!it->is_regular_file(kind) || !Options::isModelPath(name))
      continue;
    // The engine can only name what fits MAX_MODEL_NAME: for a longer stem
    // there is no loadable copy to make.
    if (kActionModelLoadDir.size() + name.size() > kActionModelNameMax) {
      abc::ConsoleWarn("[AbcUi] menu action model name is too long for the "
                       "engine: %s\n",
                       name.c_str());
      continue;
    }
    const auto destination =
        std::filesystem::path(mirror) / it->path().filename();
    std::string wanted, current;
    if (readFile(destination, current) && readFile(it->path(), wanted) &&
        current == wanted) {
      copied.insert(name);
      continue;
    }
    wanted.clear();
    if (readFile(it->path(), wanted) && writeFile(wanted, destination, true))
      copied.insert(name);
    else
      abc::ConsoleWarn("[AbcUi] menu action model cannot be copied for the "
                       "engine: %s\n",
                       name.c_str());
  }
  // The mirror only ever holds the models copied above: anything else there
  // is a stale copy, a stray file or a temporary one an interrupted copy
  // left.
  for (std::filesystem::directory_iterator it(mirror, error), end; it != end;
       it.increment(error)) {
    if (!it->is_regular_file(kind) ||
        copied.contains(it->path().filename().string()))
      continue;
    std::filesystem::remove(it->path(), kind);
  }
}

} // namespace modelpreview
