#pragma once

// framework::ModuleCustom - the custom files layered over one module.
//
// "abc/abcui/custom/<module-id>/<package path>" holds loose files a user
// puts beside a module package: one that shares a package entry's path
// replaces it, any other is a new "mod://<module-id>/<path>". ModuleVfs
// indexes the directory once, when the module mounts: the index never
// changes afterwards, so the video and audio threads read through it without
// sharing mutable state, and a file added, replaced or removed later only
// counts after a restart. The bytes are read at open time into an entry the
// caller owns.
//
// Only regular files reached without a link (reparse point) are indexed, and
// every open checks that the file it opened still lies inside the module's
// custom directory. A file that cannot be read is reported once and counts
// as absent, so the package entry is used instead.

#include "PackageFile.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace framework {

class ModuleCustom {
public:
  // One directory directly under the custom root. `link` marks a reparse
  // point, whose contents are never used.
  struct Folder {
    std::string name;
    bool link = false;
  };

  // The directories directly under the custom root `root`, by their exact
  // names. Empty when there is no such root: nothing to report.
  static std::vector<Folder> rootDirectories(const std::string &root);

  // Indexes the custom directory `directory` of module `moduleId`, leaving
  // out the `codePaths` (the manifest and the entry image stay
  // package-only). Null without `failure` when the directory does not
  // exist; null with `failure` set when it exists but cannot be used.
  static std::unique_ptr<ModuleCustom>
  index(const std::string &directory, const char *moduleId,
        const std::set<std::string> &codePaths, std::string &failure);

  // Queues `message` for the main thread to print; any thread may call.
  // takeReports() hands over the queue and empties it. The engine console
  // is never written from a decoder thread.
  static void report(std::string message);
  static std::vector<std::string> takeReports();

  // Indexed paths under `prefix` ('/'-terminated), in index order.
  std::vector<std::string> paths(std::string_view prefix) const;
  // True when a custom file provides `path`.
  bool contains(std::string_view path) const;
  // Reads the custom file at `path` into `out`. False with `error` set when
  // it cannot be read, which is reported once per file.
  bool read(std::string_view path, PackageEntry &out, std::string *error) const;

private:
  ModuleCustom() = default;

  struct File {
    std::wstring disk;
    // The first read failure of the file is reported once.
    mutable std::once_flag warned;
  };

  std::string Id;
  // The path the directory finally resolves to: every file opened must lie
  // below it.
  std::wstring Resolved;
  std::map<std::string, File, std::less<>> Files;
};

} // namespace framework
