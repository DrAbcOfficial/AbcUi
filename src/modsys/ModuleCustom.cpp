// framework::ModuleCustom implementation - see ModuleCustom.h.
#include "ModuleCustom.h"

#include "../framework/NativeHandles.h"
#include "ModulePath.h"
#include "NativeText.h"

#include <mutex>
#include <utility>

#include <Windows.h>

namespace framework {
namespace {

// A decoder holds the whole file, exactly as it holds a package entry.
constexpr size_t kMaxFileBytes = kMaxEntryBytes;
constexpr size_t kReadChunkBytes = 4 * 1024 * 1024;

// Null when the file cannot be opened.
NativeHandle openFile(const std::wstring &path, DWORD access, DWORD share,
                      DWORD flags) {
  const HANDLE handle = CreateFileW(path.c_str(), access, share, nullptr,
                                    OPEN_EXISTING, flags, nullptr);
  return NativeHandle(handle == INVALID_HANDLE_VALUE ? nullptr : handle);
}

// The path `handle` finally refers to, with every link on the way resolved;
// empty when the system cannot tell.
std::wstring finalPath(HANDLE handle) {
  constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
  if (required == 0)
    return {};
  std::wstring path(static_cast<size_t>(required) + 1, L'\0');
  const DWORD written = GetFinalPathNameByHandleW(
      handle, path.data(), static_cast<DWORD>(path.size()), flags);
  if (written == 0 || written >= path.size())
    return {};
  path.resize(written);
  // Both sides of the comparison lose the prefix or keep it together;
  // dropping it keeps the path readable.
  constexpr std::wstring_view kExtended = L"\\\\?\\";
  if (path.starts_with(kExtended))
    path.erase(0, kExtended.size());
  return path;
}

// Ordinal and case-insensitive, like the file system itself.
bool liesInside(const std::wstring &path, const std::wstring &directory) {
  const auto length = static_cast<int>(directory.size());
  return path.size() > directory.size() && path[directory.size()] == L'\\' &&
         CompareStringOrdinal(path.c_str(), length, directory.c_str(), length,
                              TRUE) == CSTR_EQUAL;
}

// Files below `directory`, by package path. A link is skipped, never
// followed, so nothing outside the module's directory is ever indexed.
void collect(const std::wstring &directory, const std::string &prefix,
             std::map<std::string, std::wstring, std::less<>> &files) {
  WIN32_FIND_DATAW data{};
  NativeFindHandle find(FindFirstFileW((directory + L"\\*").c_str(), &data));
  if (find.get() == INVALID_HANDLE_VALUE)
    return;
  do {
    const std::wstring_view name(data.cFileName);
    if (name == L"." || name == L"..")
      continue;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
      continue;
    const std::string part = wtext::utf8FromWide(name);
    const std::wstring disk = directory + L"\\" + std::wstring(name);
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      collect(disk, prefix + part + "/", files);
      continue;
    }
    // A name the module uris cannot spell can never be requested.
    const std::string path = prefix + part;
    std::string normalized;
    if (modulePath::normalize(path, normalized) && normalized == path)
      files.emplace(path, disk);
  } while (FindNextFileW(find.get(), &data));
}

// The bytes of the custom file at `disk`, which must lie inside `directory`:
// empty when `out` holds them, the reason otherwise.
std::string readDisk(const std::wstring &disk, const std::wstring &directory,
                     PackageEntry &out) {
  // A link put in place of an indexed file opens as the link itself.
  NativeHandle handle(
      openFile(disk, GENERIC_READ, FILE_SHARE_READ,
               FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT));
  if (!handle)
    return "it cannot be opened";
  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(handle.get(), &information))
    return "it cannot be inspected";
  if (information.dwFileAttributes &
      (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
    return "it is a link or a directory";
  // The index never follows links, but a directory on the way may have
  // become one since: what was opened must still resolve inside.
  const std::wstring resolved = finalPath(handle.get());
  if (resolved.empty() || !liesInside(resolved, directory))
    return "it lies outside the custom directory";
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(handle.get(), &size) || size.QuadPart < 0)
    return "its size is unreadable";
  if (static_cast<unsigned long long>(size.QuadPart) > kMaxFileBytes)
    return "it exceeds 128 MiB";
  const size_t bytes = static_cast<size_t>(size.QuadPart);
  // The buffer belongs to this entry alone: whoever reads it next, from
  // whichever thread, shares nothing with another open.
  out.custom = std::make_unique<std::byte[]>(bytes);
  for (size_t at = 0; at < bytes;) {
    const size_t remaining = bytes - at;
    const auto chunk = static_cast<DWORD>(
        remaining > kReadChunkBytes ? kReadChunkBytes : remaining);
    DWORD got = 0;
    if (!ReadFile(handle.get(), out.custom.get() + at, chunk, &got, nullptr) ||
        got == 0)
      return "the read stopped early";
    at += got;
  }
  out.data = out.custom.get();
  out.size = bytes;
  return {};
}

std::mutex &reportMutex() {
  static std::mutex instance;
  return instance;
}

std::vector<std::string> &reportQueue() {
  static std::vector<std::string> instance;
  return instance;
}

} // namespace

std::vector<ModuleCustom::Folder>
ModuleCustom::rootDirectories(const std::string &root) {
  std::vector<Folder> directories;
  const std::wstring wide = wtext::wideFromUtf8(root);
  WIN32_FIND_DATAW data{};
  NativeFindHandle find(FindFirstFileW((wide + L"\\*").c_str(), &data));
  if (find.get() == INVALID_HANDLE_VALUE)
    return directories;
  do {
    const std::wstring_view name(data.cFileName);
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name == L"." ||
        name == L"..")
      continue;
    directories.push_back(
        {wtext::utf8FromWide(name),
         (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0});
  } while (FindNextFileW(find.get(), &data));
  return directories;
}

std::unique_ptr<ModuleCustom>
ModuleCustom::index(const std::string &directory, const char *moduleId,
                    const std::set<std::string> &codePaths,
                    std::string &failure) {
  failure.clear();
  const std::wstring wide = wtext::wideFromUtf8(directory);
  const DWORD attributes = GetFileAttributesW(wide.c_str());
  // No custom directory: the package stands alone and nothing is reported.
  if (attributes == INVALID_FILE_ATTRIBUTES)
    return nullptr;
  if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    failure = "it is a link";
    return nullptr;
  }
  if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    failure = "it is not a directory";
    return nullptr;
  }
  NativeHandle handle(
      openFile(wide, FILE_READ_ATTRIBUTES,
               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT));
  BY_HANDLE_FILE_INFORMATION information{};
  const std::wstring resolved =
      handle && GetFileInformationByHandle(handle.get(), &information) &&
              !(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
          ? finalPath(handle.get())
          : std::wstring();
  if (resolved.empty()) {
    failure = "its location cannot be resolved";
    return nullptr;
  }

  std::map<std::string, std::wstring, std::less<>> disks;
  collect(wide, {}, disks);
  std::unique_ptr<ModuleCustom> custom(new ModuleCustom());
  custom->Id = moduleId;
  custom->Resolved = resolved;
  for (auto &[path, disk] : disks) {
    // Code is never overlaid.
    if (!codePaths.contains(path))
      custom->Files[path].disk = std::move(disk);
  }
  return custom;
}

void ModuleCustom::report(std::string message) {
  const std::lock_guard lock(reportMutex());
  reportQueue().push_back(std::move(message));
}

std::vector<std::string> ModuleCustom::takeReports() {
  const std::lock_guard lock(reportMutex());
  return std::exchange(reportQueue(), {});
}

std::vector<std::string> ModuleCustom::paths(std::string_view prefix) const {
  std::vector<std::string> result;
  for (const auto &[path, file] : Files)
    if (path.starts_with(prefix))
      result.push_back(path);
  return result;
}

bool ModuleCustom::contains(std::string_view path) const {
  return Files.contains(path);
}

bool ModuleCustom::read(std::string_view path, PackageEntry &out,
                        std::string *error) const {
  out = {};
  const auto file = Files.find(path);
  if (file == Files.end()) {
    if (error)
      *error = "no custom file";
    return false;
  }
  const std::string reason = readDisk(file->second.disk, Resolved, out);
  if (reason.empty())
    return true;
  out = {};
  if (error)
    *error = reason;
  // One report per file: a decoder that retries must not flood the console.
  std::call_once(file->second.warned, [&] {
    report("file \"" + Id + "/" + std::string(path) + "\" could not be read (" +
           reason + ")");
  });
  return false;
}

} // namespace framework
