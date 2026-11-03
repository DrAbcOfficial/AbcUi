// framework::modulecatalog implementation - see ModuleCatalog.h.
#include "ModuleCatalog.h"

#include "FrameworkVersion.h"
#include "ModuleManager.h"
#include "ModuleManifest.h"
#include "PackageImage.h"

#include <abc/Console.h>
#include <abc/Paths.h>

#include <Windows.h>

#include <abcui/abi.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace framework {
namespace modulecatalog {
namespace {

constexpr size_t kManifestMaxBytes = 256u * 1024u;

bool endsWithIgnoreCase(const std::string &text, std::string_view suffix) {
  if (text.size() < suffix.size())
    return false;
  const size_t offset = text.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    char a = text[offset + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z')
      a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
      b = static_cast<char>(b - 'A' + 'a');
    if (a != b)
      return false;
  }
  return true;
}

bool extensionKind(const std::string &file, bool &enabled) {
  if (endsWithIgnoreCase(file, ".zip")) {
    enabled = true;
    return true;
  }
  if (endsWithIgnoreCase(file, ".disable")) {
    enabled = false;
    return true;
  }
  return false;
}

std::string replaceExtension(const std::string &file, bool enabled) {
  const size_t dot = file.find_last_of('.');
  const std::string stem =
      dot == std::string::npos ? file : file.substr(0, dot);
  return stem + (enabled ? ".zip" : ".disable");
}

bool validFileName(const char *name) {
  if (!name || !*name)
    return false;
  const std::string file(name);
  if (file.size() > 240 || file == "." || file == "..")
    return false;
  if (file.back() == '.' || file.back() == ' ')
    return false;
  for (const unsigned char c : file) {
    if (c < 0x20 || c == 0x7f)
      return false;
    if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
        c == '|' || c == '?' || c == '*')
      return false;
  }
  bool enabled = false;
  return extensionKind(file, enabled);
}

std::wstring wideFromUtf8(const std::string &text) {
  if (text.empty())
    return {};
  const int length = MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(length > 0 ? length : 0), L'\0');
  if (length > 0) {
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        wide.data(), length);
  }
  return wide;
}

std::string utf8FromWide(const wchar_t *text) {
  if (!text || !*text)
    return {};
  const int length =
      WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1)
    return {};
  std::string out(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), length, nullptr,
                      nullptr);
  out.resize(static_cast<size_t>(length - 1));
  return out;
}

std::string moduleDirectory() {
  return abc::ModPath("abc/abcui/modules");
}

std::string fullPath(const std::string &file) {
  std::string directory = moduleDirectory();
  if (!directory.empty() && directory.back() != '\\' && directory.back() != '/')
    directory.push_back('\\');
  return directory + file;
}

bool fileExists(const std::string &file) {
  const std::wstring wide = wideFromUtf8(fullPath(file));
  const DWORD attributes = GetFileAttributesW(wide.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool regularFile(const std::string &path) {
  const std::wstring wide = wideFromUtf8(path);
  const DWORD attributes = GetFileAttributesW(wide.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         !(attributes & FILE_ATTRIBUTE_DIRECTORY) &&
         !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

std::string lowerCopy(std::string text) {
  for (char &c : text) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

} // namespace

ModuleCatalog &ModuleCatalog::instance() {
  static ModuleCatalog catalog;
  return catalog;
}

const Entry *ModuleCatalog::at(int32_t index) const {
  if (index < 0 || index >= static_cast<int32_t>(Entries.size()))
    return nullptr;
  return &Entries[static_cast<size_t>(index)];
}

int32_t ModuleCatalog::findIn(const std::vector<Entry> &entries,
                              const char *file) const {
  if (!file)
    return -1;
  const std::string wanted = lowerCopy(file);
  for (size_t i = 0; i < entries.size(); ++i) {
    if (lowerCopy(entries[i].file) == wanted)
      return static_cast<int32_t>(i);
  }
  return -1;
}

int32_t ModuleCatalog::find(const char *file) const {
  return findIn(Entries, file);
}

int32_t ModuleCatalog::relationCount(int32_t index) const {
  const Entry *entry = at(index);
  return entry ? static_cast<int32_t>(entry->relations.size()) : 0;
}

const Relation *ModuleCatalog::relationAt(int32_t index,
                                          int32_t relation) const {
  const Entry *entry = at(index);
  if (!entry || relation < 0 ||
      relation >= static_cast<int32_t>(entry->relations.size()))
    return nullptr;
  return &entry->relations[static_cast<size_t>(relation)];
}

int32_t ModuleCatalog::diagnosticCount(int32_t index) const {
  const Entry *entry = at(index);
  return entry ? static_cast<int32_t>(entry->diagnostics.size()) : 0;
}

const Diagnostic *ModuleCatalog::diagnosticAt(int32_t index,
                                              int32_t diagnostic) const {
  const Entry *entry = at(index);
  if (!entry || diagnostic < 0 ||
      diagnostic >= static_cast<int32_t>(entry->diagnostics.size()))
    return nullptr;
  return &entry->diagnostics[static_cast<size_t>(diagnostic)];
}

void ModuleCatalog::ensureScanned() {
  if (!Scanned)
    rescan();
}

void ModuleCatalog::addDiagnostic(Entry &entry, Severity severity,
                                  DiagnosticCode code,
                                  const std::string &subject,
                                  const std::string &detail) const {
  Diagnostic diagnostic;
  diagnostic.severity = severity;
  diagnostic.code = code;
  diagnostic.subject = subject;
  diagnostic.detail = detail;
  entry.diagnostics.push_back(std::move(diagnostic));
}

int32_t ModuleCatalog::rescan() {
  std::vector<Entry> fresh;
  const std::string directory = moduleDirectory();
  const std::wstring pattern = wideFromUtf8(directory + "\\*");
  WIN32_FIND_DATAW data{};
  HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) {
    Entries.clear();
    PendingChanges.clear();
    Scanned = true;
    ++Revision;
    return -1;
  }

  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      continue;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
      continue;
    const std::string file = utf8FromWide(data.cFileName);
    bool enabled = false;
    if (!extensionKind(file, enabled))
      continue;

    Entry entry;
    entry.file = file;
    entry.enabled = enabled;
    entry.fileSize =
        (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;

    const std::string path = fullPath(file);
    std::string error;
    auto image = PackageImage::open(path.c_str(), &error);
    if (!image) {
      entry.error = error;
      fresh.push_back(std::move(entry));
      continue;
    }

    PackageEntry manifestEntry;
    if (!image->file().read("manifest.json", manifestEntry, &error) ||
        manifestEntry.size > kManifestMaxBytes) {
      entry.error = manifestEntry.size > kManifestMaxBytes
                        ? "manifest is too large"
                        : error;
      fresh.push_back(std::move(entry));
      continue;
    }

    ModuleManifest manifest;
    if (!parseManifest(static_cast<const char *>(manifestEntry.data),
                       manifestEntry.size, manifest, &error)) {
      entry.error = error;
      fresh.push_back(std::move(entry));
      continue;
    }

    entry.id = manifest.id;
    entry.name = manifest.name;
    entry.description = manifest.description;
    entry.author = manifest.author;
    entry.version = manifest.version;
    entry.minAbcUi = manifest.minAbcUi;
    entry.entry = manifest.entry;
    entry.abi = manifest.abi;
    entry.resourcesOnly = manifest.resourcesOnly;
    entry.dependencies = manifest.dependencies;
    entry.conflicts = manifest.conflicts;
    entry.manifestValid = true;
    entry.entryPresent =
        manifest.resourcesOnly || image->file().exists(manifest.entry.c_str());
    fresh.push_back(std::move(entry));
  } while (FindNextFileW(handle, &data));
  FindClose(handle);

  Entries = std::move(fresh);
  syncPending(Entries);
  Scanned = true;
  rebuild();
  return count();
}

void ModuleCatalog::syncPending(std::vector<Entry> &entries) {
  for (Entry &entry : entries)
    entry.pendingError.clear();

  std::vector<PendingChange> kept;
  for (const PendingChange &change : PendingChanges) {
    const int32_t index = findIn(entries, change.file.c_str());
    if (index < 0)
      continue;
    Entry &entry = entries[static_cast<size_t>(index)];
    if (entry.id != change.id || entry.version != change.version) {
      entry.pendingError = "package contents changed; pending change dropped";
      continue;
    }
    if (fileExists(change.targetFile)) {
      entry.pendingError = "target file already exists; pending change dropped";
      continue;
    }
    kept.push_back(change);
  }
  PendingChanges = std::move(kept);
}

void ModuleCatalog::rebuild() {
  const std::vector<modmanager::RuntimeModule> runtime =
      modmanager::runtimeSnapshot();

  for (Entry &entry : Entries) {
    entry.pending = kNoPending;
    entry.targetFile.clear();
    entry.effectiveEnabled = entry.enabled;
    entry.diagnostics.clear();
    entry.relations.clear();
    entry.loaded = false;
    entry.initialized = false;
    entry.failed = false;
    entry.failure.clear();
    entry.loadOrder = -1;
    entry.flags = entry.enabled ? kFlagEnabled : 0;
    if (entry.resourcesOnly)
      entry.flags |= kFlagResources;
  }

  for (const PendingChange &change : PendingChanges) {
    const int32_t index = findIn(Entries, change.file.c_str());
    if (index < 0)
      continue;
    Entry &entry = Entries[static_cast<size_t>(index)];
    entry.pending = change.enabled ? kPendingEnable : kPendingDisable;
    entry.targetFile = change.targetFile;
    entry.effectiveEnabled = change.enabled;
    entry.flags |= kFlagPending;
  }

  for (Entry &entry : Entries) {
    const modmanager::RuntimeModule *record = nullptr;
    for (const auto &candidate : runtime) {
      if (lowerCopy(candidate.file) == lowerCopy(entry.file)) {
        record = &candidate;
        break;
      }
    }
    if (!record) {
      for (const auto &candidate : runtime) {
        if (candidate.id == entry.id && candidate.version == entry.version) {
          record = &candidate;
          break;
        }
      }
    }
    if (record) {
      entry.loaded = record->loaded;
      entry.initialized = record->initialized;
      entry.failed = record->failed;
      entry.failure = record->failure;
      entry.loadOrder = record->loadOrder;
      if (entry.loaded)
        entry.flags |= kFlagLoadedNow;
    }

    if (!entry.manifestValid) {
      addDiagnostic(entry, kError,
                    entry.id.empty() ? kDiagUnreadable : kDiagManifestInvalid,
                    {}, entry.error);
    } else {
      if (entry.abi != ABCUI_ABI_VERSION) {
        addDiagnostic(entry, kError, kDiagAbiMismatch, {},
                      "module ABI does not match this AbcUi build");
      }
      if (compareVersions(kFrameworkVersion, entry.minAbcUi) < 0) {
        addDiagnostic(entry, kError, kDiagFrameworkTooOld, {},
                      "module needs a newer AbcUi");
      }
      if (!entry.resourcesOnly && !entry.entryPresent) {
        addDiagnostic(entry, kError, kDiagEntryMissing, {},
                      "package entry is missing");
      }
    }
    if (!entry.pendingError.empty()) {
      addDiagnostic(entry, kWarning, kDiagPendingInvalid, {},
                    entry.pendingError);
    }

    const bool compatible =
        entry.manifestValid && entry.abi == ABCUI_ABI_VERSION &&
        compareVersions(kFrameworkVersion, entry.minAbcUi) >= 0 &&
        (entry.resourcesOnly || entry.entryPresent);
    if (!compatible) {
      entry.state = kInvalid;
    } else if (!entry.enabled) {
      entry.state = kDisabled;
    } else if (entry.failed) {
      entry.state = kFailed;
    } else if (entry.loaded && entry.initialized) {
      entry.state = kLoaded;
    } else {
      entry.state = kSkipped;
    }
  }

  resolve(Entries, nullptr, nullptr, false);
  buildRelations(Entries);

  for (Entry &entry : Entries) {
    entry.previewState = entry.nextState;
    entry.flags &= ~kFlagPreview;
  }
  ++Revision;
}

void ModuleCatalog::resolve(std::vector<Entry> &entries,
                            const std::string *previewFile,
                            const bool *previewEnabled, bool preview) const {
  for (Entry &entry : entries) {
    entry.effectiveEnabled = entry.enabled;
    if (entry.pending == kPendingEnable)
      entry.effectiveEnabled = true;
    else if (entry.pending == kPendingDisable)
      entry.effectiveEnabled = false;
    if (preview && previewFile && previewEnabled &&
        lowerCopy(entry.file) == lowerCopy(*previewFile)) {
      entry.effectiveEnabled = *previewEnabled;
    }
    entry.nextState = entry.effectiveEnabled ? kSkipped : kDisabled;
  }

  std::map<std::string, std::vector<int32_t>> groups;
  for (size_t i = 0; i < entries.size(); ++i) {
    const Entry &entry = entries[i];
    const bool compatible =
        entry.manifestValid && entry.abi == ABCUI_ABI_VERSION &&
        compareVersions(kFrameworkVersion, entry.minAbcUi) >= 0 &&
        (entry.resourcesOnly || entry.entryPresent);
    if (!entry.effectiveEnabled)
      continue;
    if (!compatible) {
      entries[i].nextState = kInvalid;
      continue;
    }
    groups[entry.id].push_back(static_cast<int32_t>(i));
  }

  std::map<std::string, int32_t> selected;
  for (auto &[id, indices] : groups) {
    int32_t best = -1;
    std::string bestVersion;
    for (const int32_t index : indices) {
      const std::string &version = entries[static_cast<size_t>(index)].version;
      const int order = best < 0 ? 1 : compareVersions(version, bestVersion);
      if (order > 0) {
        best = index;
        bestVersion = version;
      }
    }
    int bestCount = 0;
    for (const int32_t index : indices) {
      if (compareVersions(entries[static_cast<size_t>(index)].version,
                          bestVersion) == 0)
        ++bestCount;
    }
    if (bestCount > 1) {
      for (const int32_t index : indices) {
        entries[static_cast<size_t>(index)].nextState = kSkipped;
        if (!preview) {
          addDiagnostic(entries[static_cast<size_t>(index)], kError,
                        kDiagAmbiguousId, id,
                        "multiple packages share this id and version");
        }
      }
      continue;
    }
    selected[id] = best;
    for (const int32_t index : indices) {
      if (index == best)
        continue;
      entries[static_cast<size_t>(index)].nextState = kSkipped;
      if (!preview) {
        addDiagnostic(entries[static_cast<size_t>(index)], kInfo,
                      kDiagDuplicateVersion, id,
                      "a newer package of this id is selected");
      }
    }
  }

  std::map<std::string, std::set<std::string>> conflicts;
  for (const auto &[id, index] : selected) {
    for (const std::string &other :
         entries[static_cast<size_t>(index)].conflicts) {
      if (!selected.contains(other))
        continue;
      conflicts[id].insert(other);
      conflicts[other].insert(id);
    }
  }

  std::set<std::string> loaded;
  std::set<std::string> failed;
  // A conflict pair reports from both sides: when a package is excluded
  // because its partner loaded first, the loaded side records the exclusion
  // so the manager page shows the whole pair without deriving it itself.
  std::vector<std::pair<int32_t, std::string>> conflictExclusions;
  bool progress = true;
  while (progress) {
    progress = false;
    for (const auto &[id, index] : selected) {
      if (loaded.contains(id) || failed.contains(id))
        continue;
      Entry &entry = entries[static_cast<size_t>(index)];
      bool blocked = false;
      std::string subject;
      DiagnosticCode code = kDiagMissingDependency;
      Severity severity = kError;

      for (const std::string &other : conflicts[id]) {
        if (loaded.contains(other)) {
          blocked = true;
          subject = other;
          code = kDiagConflictActive;
          break;
        }
      }

      bool waiting = false;
      if (!blocked) {
        for (const std::string &dependency : entry.dependencies) {
          if (loaded.contains(dependency))
            continue;
          if (failed.contains(dependency)) {
            blocked = true;
            subject = dependency;
            code = kDiagFailedDependency;
            break;
          }
          if (!selected.contains(dependency)) {
            blocked = true;
            subject = dependency;
            const auto found = std::find_if(entries.begin(), entries.end(),
                                            [&](const Entry &candidate) {
                                              return candidate.id == dependency;
                                            });
            if (found == entries.end()) {
              code = kDiagMissingDependency;
            } else if (!found->effectiveEnabled) {
              code = kDiagDisabledDependency;
              severity = kWarning;
            } else {
              code = kDiagFailedDependency;
            }
            break;
          }
          waiting = true;
        }
      }

      if (blocked) {
        failed.insert(id);
        entry.nextState = kSkipped;
        if (!preview)
          addDiagnostic(entry, severity, code, subject, {});
        if (code == kDiagConflictActive)
          conflictExclusions.emplace_back(selected.at(subject), id);
        progress = true;
        continue;
      }
      if (waiting)
        continue;
      loaded.insert(id);
      entry.nextState = kWillLoad;
      progress = true;
    }
  }

  for (const auto &[id, index] : selected) {
    if (loaded.contains(id) || failed.contains(id))
      continue;
    Entry &entry = entries[static_cast<size_t>(index)];
    entry.nextState = kSkipped;
    if (!preview) {
      addDiagnostic(entry, kError, kDiagDependencyCycle, id,
                    "dependency cycle prevents this module from loading");
    }
  }

  // The loaded side of every excluded pair reports the exclusion as an
  // informational note (it loaded fine, so this is never a problem badge).
  for (const auto &[winnerIndex, loserId] : conflictExclusions) {
    if (!preview)
      addDiagnostic(entries[static_cast<size_t>(winnerIndex)], kInfo,
                    kDiagConflictInactive, loserId,
                    "the conflicting package was excluded");
  }
}

void ModuleCatalog::buildRelations(std::vector<Entry> &entries) const {
  const auto findById = [&](const std::string &id) -> const Entry * {
    const Entry *fallback = nullptr;
    for (const Entry &entry : entries) {
      if (entry.id != id)
        continue;
      if (entry.effectiveEnabled && entry.nextState == kWillLoad)
        return &entry;
      if (!fallback)
        fallback = &entry;
    }
    return fallback;
  };
  // The index twin of findById for the mirror pass, which appends the
  // reciprocal edge onto the target entry.
  const auto findIdIndex = [&](const std::string &id) -> int32_t {
    int32_t fallback = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
      const Entry &candidate = entries[i];
      if (candidate.id != id)
        continue;
      if (candidate.effectiveEnabled && candidate.nextState == kWillLoad)
        return static_cast<int32_t>(i);
      if (fallback < 0)
        fallback = static_cast<int32_t>(i);
    }
    return fallback;
  };

  // A declared conflict is live only while both sides are enabled, and the
  // state reads from the observing side: the side that loads reports the
  // pair as resolved (ok), the excluded side reports it as blocking
  // (active).
  const auto conflictState = [](const Entry &from, const Entry &to) {
    if (!from.effectiveEnabled || !to.effectiveEnabled)
      return kRelationDisabled;
    return from.nextState == kWillLoad ? kRelationOk : kRelationActive;
  };

  for (Entry &entry : entries) {
    entry.relations.clear();
    for (const std::string &id : entry.dependencies) {
      Relation relation;
      relation.kind = kDependency;
      relation.declaredBy = true;
      relation.id = id;
      const Entry *target = findById(id);
      if (!target) {
        relation.state = kRelationMissing;
      } else {
        relation.name = target->name;
        relation.file = target->file;
        if (!target->effectiveEnabled) {
          relation.state = kRelationDisabled;
        } else if (target->nextState == kWillLoad) {
          relation.state = kRelationOk;
        } else {
          relation.state = kRelationFailed;
        }
      }
      entry.relations.push_back(std::move(relation));
    }
    for (const Entry &other : entries) {
      if (other.file == entry.file)
        continue;
      if (std::find(other.dependencies.begin(), other.dependencies.end(),
                    entry.id) == other.dependencies.end())
        continue;
      Relation relation;
      relation.kind = kDependent;
      relation.id = other.id;
      relation.name = other.name;
      relation.file = other.file;
      if (!entry.effectiveEnabled) {
        relation.state = kRelationDisabled;
      } else if (entry.nextState == kWillLoad) {
        relation.state = kRelationOk;
      } else {
        relation.state = kRelationFailed;
      }
      entry.relations.push_back(std::move(relation));
    }
    for (const std::string &id : entry.conflicts) {
      Relation relation;
      relation.kind = kConflict;
      relation.declaredBy = true;
      relation.id = id;
      const Entry *target = findById(id);
      if (!target) {
        relation.state = kRelationMissing;
      } else {
        relation.name = target->name;
        relation.file = target->file;
        relation.state = conflictState(entry, *target);
      }
      entry.relations.push_back(std::move(relation));
    }
  }

  // The mirror edge: a module whose conflict partner declares the pair
  // against it shows the same edge, so both sides of a conflict tell the
  // story no matter which package declared it. A package that declares the
  // pair itself already owns its edge from the loop above.
  for (size_t i = 0; i < entries.size(); ++i) {
    const Entry &entry = entries[i];
    for (const std::string &id : entry.conflicts) {
      const int32_t targetIndex = findIdIndex(id);
      if (targetIndex < 0 ||
          entries[static_cast<size_t>(targetIndex)].file == entry.file)
        continue;
      const Entry &target = entries[static_cast<size_t>(targetIndex)];
      if (std::find(target.conflicts.begin(), target.conflicts.end(),
                    entry.id) != target.conflicts.end())
        continue;
      Relation relation;
      relation.kind = kConflict;
      relation.declaredBy = false;
      relation.id = entry.id;
      relation.name = entry.name;
      relation.file = entry.file;
      relation.state = conflictState(target, entry);
      entries[static_cast<size_t>(targetIndex)].relations.push_back(
          std::move(relation));
    }
  }
}

int32_t ModuleCatalog::previewToggle(const char *file, int32_t enabled) {
  ensureScanned();
  if (!validFileName(file))
    return kChangeInvalid;
  const int32_t index = findIn(Entries, file);
  if (index < 0)
    return kChangeNotFound;
  const Entry &entry = Entries[static_cast<size_t>(index)];
  const std::string target = replaceExtension(entry.file, enabled != 0);
  if (target == entry.file)
    return kChangeNoChange;
  if (findIn(Entries, target.c_str()) >= 0 || fileExists(target))
    return kChangeExists;

  std::vector<Entry> previewEntries = Entries;
  const std::string previewFile = entry.file;
  const bool previewEnabled = enabled != 0;
  resolve(previewEntries, &previewFile, &previewEnabled, true);

  for (Entry &current : Entries) {
    const int32_t previewIndex = findIn(previewEntries, current.file.c_str());
    if (previewIndex < 0)
      continue;
    current.previewState =
        previewEntries[static_cast<size_t>(previewIndex)].nextState;
    if (lowerCopy(current.file) == lowerCopy(previewFile) ||
        current.previewState != current.nextState) {
      current.flags |= kFlagPreview;
    } else {
      current.flags &= ~kFlagPreview;
    }
  }
  ++Revision;
  return kChangeOk;
}

void ModuleCatalog::clearPreview() {
  for (Entry &entry : Entries) {
    entry.previewState = entry.nextState;
    entry.flags &= ~kFlagPreview;
  }
  ++Revision;
}

int32_t ModuleCatalog::setPendingEnabled(const char *file, int32_t enabled,
                                         Change &out) {
  out = {};
  ensureScanned();
  if (!validFileName(file)) {
    out.code = kChangeInvalid;
    return out.code;
  }
  const int32_t index = findIn(Entries, file);
  if (index < 0) {
    out.code = kChangeNotFound;
    return out.code;
  }
  Entry &entry = Entries[static_cast<size_t>(index)];
  const std::string target = replaceExtension(entry.file, enabled != 0);
  out.file = entry.file;
  out.targetFile = target;
  if (target == entry.file) {
    PendingChanges.erase(
        std::remove_if(PendingChanges.begin(), PendingChanges.end(),
                       [&](const PendingChange &change) {
                         return lowerCopy(change.file) == lowerCopy(entry.file);
                       }),
        PendingChanges.end());
    rebuild();
    out.code = kChangeNoChange;
    out.pending = kNoPending;
    out.restartRequired = restartRequired();
    return out.code;
  }
  if (findIn(Entries, target.c_str()) >= 0 || fileExists(target)) {
    out.code = kChangeExists;
    return out.code;
  }

  std::vector<int32_t> before;
  before.reserve(Entries.size());
  for (const Entry &current : Entries)
    before.push_back(current.nextState);

  PendingChanges.erase(
      std::remove_if(PendingChanges.begin(), PendingChanges.end(),
                     [&](const PendingChange &change) {
                       return lowerCopy(change.file) == lowerCopy(entry.file);
                     }),
      PendingChanges.end());

  const bool desired = enabled != 0;

  PendingChange change;
  change.file = entry.file;
  change.targetFile = target;
  change.enabled = desired;
  change.id = entry.id;
  change.version = entry.version;
  PendingChanges.push_back(std::move(change));

  rebuild();

  const int32_t afterIndex = findIn(Entries, file);
  out.code = kChangeOk;
  out.pending = afterIndex >= 0
                    ? Entries[static_cast<size_t>(afterIndex)].pending
                    : kNoPending;
  out.restartRequired = restartRequired();
  int32_t affected = 0;
  for (size_t i = 0; i < Entries.size() && i < before.size(); ++i) {
    if (Entries[i].nextState != before[i])
      ++affected;
  }
  out.affectedCount = affected;
  return out.code;
}

void ModuleCatalog::applyPendingRenames() {
  if (PendingChanges.empty())
    return;

  for (const PendingChange &change : PendingChanges) {
    if (!validFileName(change.file.c_str()) ||
        !validFileName(change.targetFile.c_str())) {
      abc::ConsoleWarn("[AbcUi] pending module rename has an invalid name\n");
      continue;
    }
    const std::wstring source = wideFromUtf8(fullPath(change.file));
    const std::wstring target = wideFromUtf8(fullPath(change.targetFile));
    if (!regularFile(fullPath(change.file))) {
      abc::ConsoleWarn("[AbcUi] pending module rename source is gone: %s\n",
                       change.file.c_str());
      continue;
    }
    if (fileExists(change.targetFile)) {
      abc::ConsoleWarn("[AbcUi] pending module rename target exists: %s\n",
                       change.targetFile.c_str());
      continue;
    }
    if (!MoveFileExW(source.c_str(), target.c_str(), 0)) {
      abc::ConsoleWarn("[AbcUi] pending module rename failed (%lu): %s -> %s\n",
                       GetLastError(), change.file.c_str(),
                       change.targetFile.c_str());
      continue;
    }
    abc::ConsolePrint("[AbcUi] module package renamed: %s -> %s\n",
                      change.file.c_str(), change.targetFile.c_str());
  }
  PendingChanges.clear();
  ++Revision;
}

} // namespace modulecatalog
} // namespace framework
