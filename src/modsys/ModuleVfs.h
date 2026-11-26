#pragma once

// framework::ModuleVfs - mounted module packages under the mod:// scheme.
//
// URIs look like "mod://<module-id>/<path>"; the path is normalized ('.'
// and '..' segments collapsed, '/' separators) and resolved against the
// package the module id maps to. Entries come back as PackageEntry views:
// zero-copy pointers into the mapped zip for STORED content, owned buffers
// for extracted ones and for custom files. Packages stay mounted for the
// process lifetime - the ModuleManager unmounts in reverse load order only
// after Rml has shut down - so zero-copy views remain valid for the whole
// UI lifetime.
//
// Each module also layers the custom directory named after it (see
// ModuleCustom.h): a custom file replaces the package entry of its path and
// any other is a new entry, for every reader alike (documents, textures,
// media, localization, the module VFS section). Code is never overlaid: the
// paths `mount` reserves stay package-only.
//
// Lookups are case sensitive and must match the archive's entry names.

#include "PackageFile.h"
#include "PackageImage.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace framework {

class ModuleCustom;

namespace ModuleVfs {

// True when the uri carries the mod:// scheme.
bool isModuleUri(std::string_view uri);

// Names the custom root once, before the first mount: `directory` is its
// game-relative spelling ("abc/abcui/custom") and `absolute` the same
// directory as the file APIs reach it (abc::ModPath). Modules mounted
// afterwards layer their "<module-id>/<package path>" files over their
// package; without a root, no module has an overlay.
void setCustomRoot(const char *directory, const char *absolute);

// Mounts a parsed package under a module id and takes ownership of the
// image, and indexes the module's custom directory when it has one. Fails
// when the id is empty or already mounted. `codePaths` (the manifest and
// the entry image) are never replaced by a custom file.
bool mount(const char *moduleId, std::unique_ptr<PackageImage> image,
           std::vector<std::string> dependencies = {},
           std::vector<std::string> codePaths = {});
// Drops one mount (load failures) or every mount (process teardown), the
// module's overlay included.
void unmount(const char *moduleId);
void unmountAll();

bool isMounted(const char *moduleId);
// Deterministic snapshot; callers must still check mount lifetime on lookup.
std::vector<std::string> mountedIds();
// Custom directories with no mounted module (a disabled or failed module, or
// a name that is not an id): their files take no part.
std::vector<std::string> unmountedCustomIds();
// Canonical package URI and dependency checks. Never searches loose files.
bool resolve(std::string_view base, std::string_view path, std::string &uri);
bool canRead(std::string_view requester, std::string_view uri);
// Package entries below `directory` and the custom files beside them, one
// entry per path, sorted.
std::vector<std::string> list(const char *directory);
bool readText(const char *uri, std::string &out, size_t maxBytes = 1024 * 1024);
// The game-relative path of a module uri's custom file
// ("mod://<id>/<path>" -> "<custom root>/<id>/<path>"). False without a
// root, for a malformed uri or for an id that is not mounted.
bool customPath(const char *uri, std::string &out);

// Resolves "mod://<id>/<path>" into an entry view. The path may carry '.'
// and '..' segments; they are collapsed before lookup.
[[nodiscard]] bool open(const char *uri, PackageEntry &out, std::string *error);
// Same, for a path inside a known module.
[[nodiscard]] bool openPath(const char *moduleId, const char *path,
                            PackageEntry &out, std::string *error);

} // namespace ModuleVfs

} // namespace framework
