#pragma once

// framework::modulecatalog - the installed module packages, their on-disk
// enabled state and the pending rename cache used by the module manager page.
//
// The loader still owns the actual load order: boot() asks this catalog for a
// resolution and the runtime records come back from ModuleManager. The page
// reads the same resolution, so "next start" cannot drift from what the
// loader does. Toggles never touch the disk while modules are mounted: they
// only record a PendingChange; ModuleManager::unloadAll applies the batch
// after every module and VFS mount is gone.

#include <cstdint>
#include <string>
#include <vector>

namespace framework {
namespace modulecatalog {

enum State : int32_t {
  kLoaded = 0,
  kWillLoad = 1,
  kDisabled = 2,
  kSkipped = 3,
  kFailed = 4,
  kInvalid = 5,
};

enum Pending : int32_t {
  kNoPending = 0,
  kPendingEnable = 1,
  kPendingDisable = 2,
};

enum Severity : int32_t {
  kError = 0,
  kWarning = 1,
  kInfo = 2,
};

enum RelationKind : int32_t {
  kDependency = 0,
  kDependent = 1,
  kConflict = 2,
};

enum RelationState : int32_t {
  kRelationOk = 0,
  kRelationDisabled = 1,
  kRelationMissing = 2,
  kRelationFailed = 3,
  kRelationCycle = 4,
  kRelationActive = 5,
};

enum Flag : int32_t {
  kFlagEnabled = 0x0001,
  kFlagLoadedNow = 0x0002,
  kFlagPending = 0x0004,
  kFlagResources = 0x0008,
  kFlagPreview = 0x0010,
  kFlagChanged = 0x0020,
};

enum DiagnosticCode : int32_t {
  kDiagUnreadable = 0,
  kDiagManifestInvalid = 1,
  kDiagAbiMismatch = 2,
  kDiagFrameworkTooOld = 3,
  kDiagDuplicateVersion = 4,
  kDiagAmbiguousId = 5,
  kDiagMissingDependency = 6,
  kDiagDisabledDependency = 7,
  kDiagFailedDependency = 8,
  kDiagDependencyCycle = 9,
  kDiagConflictActive = 10,
  kDiagConflictInactive = 11,
  kDiagEntryMissing = 12,
  kDiagImageLoadFailed = 13,
  kDiagInitFailed = 14,
  kDiagFileChanged = 15,
  kDiagPendingInvalid = 16,
};

struct Diagnostic {
  Severity severity = kError;
  DiagnosticCode code = kDiagManifestInvalid;
  std::string subject;
  std::string detail;
};

struct Relation {
  RelationKind kind = kDependency;
  RelationState state = kRelationMissing;
  bool declaredBy = false;
  std::string id;
  std::string name;
  std::string file;
};

struct Entry {
  std::string file;
  std::string targetFile;
  std::string id;
  std::string name;
  std::string description;
  std::string author;
  std::string version;
  std::string minAbcUi;
  std::string entry;
  int32_t abi = 0;
  bool resourcesOnly = false;
  bool enabled = false; // on-disk extension is .zip
  bool manifestValid = false;
  bool entryPresent = false;
  std::string error;
  std::string pendingError;
  std::vector<std::string> dependencies;
  std::vector<std::string> conflicts;
  bool effectiveEnabled = false; // disk + pending (or preview)
  int32_t pending = kNoPending;
  int32_t state = kInvalid;         // current session / disk state
  int32_t nextState = kDisabled;    // resolution with the pending cache
  int32_t previewState = kDisabled; // resolution with preview override
  int32_t flags = 0;
  int32_t loadOrder = -1;
  bool loaded = false;
  bool initialized = false;
  bool failed = false;
  std::string failure;
  uint64_t fileSize = 0;
  std::vector<Relation> relations;
  std::vector<Diagnostic> diagnostics;
};

struct Change {
  int32_t code = 0;
  int32_t pending = kNoPending;
  bool restartRequired = false;
  int32_t affectedCount = 0;
  std::string file;
  std::string targetFile;
};

// Error codes for Change::code.
enum ChangeCode : int32_t {
  kChangeOk = 0,
  kChangeInvalid = 1,
  kChangeNotFound = 2,
  kChangeExists = 3,
  kChangeDenied = 4,
  kChangeNoChange = 5,
};

class ModuleCatalog {
public:
  static ModuleCatalog &instance();

  uint32_t revision() const {
    return Revision;
  }
  int32_t rescan();
  int32_t count() const {
    return static_cast<int32_t>(Entries.size());
  }
  const Entry *at(int32_t index) const;
  int32_t find(const char *file) const;

  int32_t relationCount(int32_t index) const;
  const Relation *relationAt(int32_t index, int32_t relation) const;
  int32_t diagnosticCount(int32_t index) const;
  const Diagnostic *diagnosticAt(int32_t index, int32_t diagnostic) const;

  // Dry-run: compute previewState for one change without touching the cache.
  int32_t previewToggle(const char *file, int32_t enabled);
  void clearPreview();
  int32_t setPendingEnabled(const char *file, int32_t enabled, Change &out);
  int32_t pendingCount() const {
    return static_cast<int32_t>(PendingChanges.size());
  }
  bool restartRequired() const {
    return !PendingChanges.empty();
  }

  // Called from ModuleManager::unloadAll after ModuleVfs::unmountAll().
  void applyPendingRenames();

private:
  struct PendingChange {
    std::string file;
    std::string targetFile;
    bool enabled = false;
    std::string id;
    std::string version;
  };

  void ensureScanned();
  void rebuild();
  void resolve(std::vector<Entry> &entries, const std::string *previewFile,
               const bool *previewEnabled, bool preview) const;
  void buildRelations(std::vector<Entry> &entries) const;
  void addDiagnostic(Entry &entry, Severity severity, DiagnosticCode code,
                     const std::string &subject,
                     const std::string &detail) const;
  int32_t findIn(const std::vector<Entry> &entries, const char *file) const;
  void syncPending(std::vector<Entry> &entries);

  std::vector<Entry> Entries;
  std::vector<PendingChange> PendingChanges;
  uint32_t Revision = 1;
  bool Scanned = false;
};

} // namespace modulecatalog
} // namespace framework
