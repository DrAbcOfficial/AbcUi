// framework::ModuleVfs implementation - see ModuleVfs.h.
#include "ModuleVfs.h"

#include "ModuleCustom.h"
#include "ModulePath.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <utility>
#include <vector>

namespace framework {
namespace ModuleVfs {
namespace {

constexpr std::string_view kScheme = "mod://";

struct Mount {
  std::shared_ptr<PackageImage> image;
  std::vector<std::string> dependencies;
  // Null while the module has no usable custom directory.
  std::unique_ptr<const ModuleCustom> custom;
};

std::map<std::string, Mount> &mounts() {
  static std::map<std::string, Mount> instance;
  return instance;
}

// The custom root since setCustomRoot named it. Its directories are read
// once, by their exact names: a name is used only as the id it spells.
struct CustomRoot {
  std::string relative; // "abc/abcui/custom"
  std::string absolute; // the same directory for the file APIs
  std::vector<ModuleCustom::Folder> directories;
};

CustomRoot &customRoot() {
  static CustomRoot instance;
  return instance;
}

// The module's overlay: null while it has no custom directory, which is
// the ordinary case and is reported for nothing.
std::unique_ptr<const ModuleCustom>
indexCustom(const char *moduleId, const std::vector<std::string> &codePaths) {
  const auto &root = customRoot();
  if (root.absolute.empty())
    return nullptr;
  const auto directory = std::ranges::find(root.directories, moduleId,
                                           &ModuleCustom::Folder::name);
  if (directory == root.directories.end())
    return nullptr;
  const std::string module = std::string("module \"") + moduleId + "\"";
  if (directory->link) {
    ModuleCustom::report(module +
                         ": its custom directory is a link, so its files are "
                         "not used");
    return nullptr;
  }
  std::set<std::string> reserved;
  for (const auto &path : codePaths) {
    std::string normalized;
    if (modulePath::normalize(path, normalized))
      reserved.insert(std::move(normalized));
  }
  std::string failure;
  auto custom = ModuleCustom::index(root.absolute + "\\" + directory->name,
                                    moduleId, reserved, failure);
  if (!custom && !failure.empty())
    ModuleCustom::report(module + ": its custom directory is not used (" +
                         failure + ")");
  return custom;
}

} // namespace

bool isModuleUri(std::string_view uri) {
  return uri.size() > kScheme.size() && uri.starts_with(kScheme);
}

void setCustomRoot(const char *directory, const char *absolute) {
  auto &root = customRoot();
  root = {};
  // An empty path (no game directory yet) would address the drive root.
  if (!directory || !absolute || !*absolute)
    return;
  std::string relative;
  if (!modulePath::normalize(directory, relative))
    return;
  root.relative = std::move(relative);
  root.absolute = absolute;
  root.directories = ModuleCustom::rootDirectories(absolute);
}

bool mount(const char *moduleId, std::unique_ptr<PackageImage> image,
           std::vector<std::string> dependencies,
           std::vector<std::string> codePaths) {
  if (!moduleId || !*moduleId || !image)
    return false;
  auto &table = mounts();
  if (table.contains(moduleId))
    return false;
  Mount entry;
  entry.image = std::move(image);
  entry.dependencies = std::move(dependencies);
  entry.custom = indexCustom(moduleId, codePaths);
  table.emplace(moduleId, std::move(entry));
  return true;
}

void unmount(const char *moduleId) {
  if (!moduleId)
    return;
  mounts().erase(moduleId);
}

void unmountAll() {
  mounts().clear();
}

bool isMounted(const char *moduleId) {
  if (!moduleId)
    return false;
  const auto &table = mounts();
  return table.contains(moduleId);
}

std::vector<std::string> mountedIds() {
  std::vector<std::string> ids;
  ids.reserve(mounts().size());
  for (const auto &[id, mount] : mounts())
    ids.push_back(id);
  return ids;
}

std::vector<std::string> unmountedCustomIds() {
  std::vector<std::string> ids;
  for (const auto &directory : customRoot().directories)
    if (!mounts().contains(directory.name))
      ids.push_back(directory.name);
  // The directory order is the file system's; the report is ours.
  std::sort(ids.begin(), ids.end());
  return ids;
}

bool open(const char *uri, PackageEntry &out, std::string *error) {
  out = {};
  if (!uri || !isModuleUri(uri)) {
    if (error)
      *error = "not a module uri";
    return false;
  }
  const std::string_view rest(uri + kScheme.size());
  const size_t slash = rest.find('/');
  if (slash == std::string_view::npos || slash == 0) {
    if (error)
      *error = "module uri has no path";
    return false;
  }
  const std::string moduleId(rest.substr(0, slash));
  std::string path;
  if (!modulePath::normalize(rest.substr(slash + 1), path)) {
    if (error)
      *error = "module uri path is invalid";
    return false;
  }
  return openPath(moduleId.c_str(), path.c_str(), out, error);
}

bool openPath(const char *moduleId, const char *path, PackageEntry &out,
              std::string *error) {
  out = {};
  if (!moduleId || !path) {
    if (error)
      *error = "module lookup arguments are null";
    return false;
  }
  const auto &table = mounts();
  const auto it = table.find(moduleId);
  if (it == table.end()) {
    if (error)
      *error = "module is not mounted";
    return false;
  }
  std::string normalized;
  if (!modulePath::normalize(path, normalized)) {
    if (error)
      *error = "invalid package path";
    return false;
  }
  const auto &mount = it->second;
  // A custom file replaces the package entry of its path; one that cannot
  // be read counts as absent, and the package entry is served instead.
  if (mount.custom && mount.custom->contains(normalized)) {
    if (mount.custom->read(normalized, out, error))
      return true;
    if (error)
      error->clear();
  }
  if (!mount.image->file().read(normalized.c_str(), out, error))
    return false;
  out.mappingPin = mount.image;
  return true;
}

bool resolve(std::string_view base, std::string_view path, std::string &uri) {
  std::string input;
  if (isModuleUri(path)) {
    input = path;
  } else {
    if (!isModuleUri(base))
      return false;
    const auto slash = base.rfind('/');
    if (slash < 6 || slash == base.npos)
      return false;
    input = std::string(base.substr(0, slash + 1)) + std::string(path);
  }
  const auto slash = input.find('/', 6);
  if (slash == input.npos || slash == 6)
    return false;
  const auto id = std::string_view(input).substr(6, slash - 6);
  if (id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.") != id.npos)
    return false;
  std::string normalized;
  if (!modulePath::normalize(std::string_view(input).substr(slash + 1),
                             normalized))
    return false;
  uri = input.substr(0, slash + 1) + normalized;
  if (isModuleUri(base)) {
    const auto sourceSlash = base.find('/', 6);
    if (sourceSlash == base.npos ||
        !canRead(base.substr(6, sourceSlash - 6), uri))
      return false;
  }
  return true;
}

bool canRead(std::string_view requester, std::string_view uri) {
  std::string canonical;
  if (!resolve({}, uri, canonical))
    return false;
  const auto slash = canonical.find('/', 6);
  const std::string provider = canonical.substr(6, slash - 6);
  if (!isMounted(provider.c_str()))
    return false;
  if (requester == provider)
    return true;
  const auto it = mounts().find(std::string(requester));
  return it != mounts().end() &&
         std::find(it->second.dependencies.begin(),
                   it->second.dependencies.end(),
                   provider) != it->second.dependencies.end();
}

std::vector<std::string> list(const char *directory) {
  std::vector<std::string> result;
  if (!directory)
    return result;
  // A sentinel canonicalizes directory paths without accepting root escapes.
  std::string base = directory;
  if (!base.ends_with('/'))
    base += '/';
  std::string canonical;
  if (!resolve({}, base + "_", canonical))
    return result;
  canonical.pop_back();
  const auto slash = canonical.find('/', 6);
  const auto it = mounts().find(canonical.substr(6, slash - 6));
  if (it == mounts().end())
    return result;
  const auto prefix = canonical.substr(slash + 1);
  const auto &mount = it->second;
  const auto &file = mount.image->file();
  for (size_t i = 0; i < file.entryCount(); ++i) {
    auto name = file.entryName(i);
    if (name.starts_with(prefix) && !name.ends_with('/'))
      result.push_back(canonical.substr(0, slash + 1) + name);
  }
  if (mount.custom)
    for (const auto &name : mount.custom->paths(prefix))
      result.push_back(canonical.substr(0, slash + 1) + name);
  // One entry per path: a custom file takes the place of the package entry.
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

bool readText(const char *uri, std::string &out, size_t maxBytes) {
  out.clear();
  PackageEntry entry;
  if (!open(uri, entry, nullptr) || entry.size > maxBytes)
    return false;
  out.assign(static_cast<const char *>(entry.data), entry.size);
  if (out.starts_with("\xef\xbb\xbf"))
    out.erase(0, 3);
  return true;
}

bool customPath(const char *uri, std::string &out) {
  out.clear();
  const auto &root = customRoot();
  if (root.relative.empty() || !uri || !isModuleUri(uri))
    return false;
  const std::string_view rest(uri + kScheme.size());
  const auto slash = rest.find('/');
  if (slash == std::string_view::npos || slash == 0)
    return false;
  const std::string id(rest.substr(0, slash));
  std::string path;
  if (!mounts().contains(id) ||
      !modulePath::normalize(rest.substr(slash + 1), path))
    return false;
  out = root.relative + "/" + id + "/" + path;
  return true;
}

} // namespace ModuleVfs
} // namespace framework
