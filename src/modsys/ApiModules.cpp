// framework::ModulesApi - the sdk modules section. See ApiModules.h.
#include "ApiModules.h"

#include "ApiTable.h"
#include "ApiSnapshot.h"
#include "ModuleCatalog.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace framework {
namespace ModulesApi {
namespace {

using namespace modulecatalog;

int32_t countOfKind(const Entry &entry, RelationKind kind) {
  return static_cast<int32_t>(std::count_if(
      entry.relations.begin(), entry.relations.end(),
      [&](const Relation &relation) { return relation.kind == kind; }));
}

uint32_t apiRevision() {
  return ModuleCatalog::instance().revision();
}

int32_t apiRescan() {
  return ModuleCatalog::instance().rescan();
}

int32_t apiCount() {
  return ModuleCatalog::instance().count();
}

int32_t apiAt(int32_t index, AbcUiModuleInfo *out) {
  if (!out || out->structSize < sizeof(AbcUiModuleInfo))
    return 0;
  const Entry *entry = ModuleCatalog::instance().at(index);
  if (!entry)
    return 0;
  AbcUiModuleInfo value{};
  value.structSize = sizeof(value);
  value.index = index;
  value.state = entry->state;
  value.nextState = entry->nextState;
  value.previewState = entry->previewState;
  value.pending = entry->pending;
  value.flags = entry->flags;
  value.abi = entry->abi;
  value.loadOrder = entry->loadOrder;
  value.dependencyCount = countOfKind(*entry, kDependency);
  value.dependentCount = countOfKind(*entry, kDependent);
  value.conflictCount = countOfKind(*entry, kConflict);
  value.diagnosticCount = static_cast<int32_t>(entry->diagnostics.size());
  value.fileSize = entry->fileSize;
  abi::copyText(entry->file, value.file, sizeof(value.file));
  abi::copyText(entry->id, value.id, sizeof(value.id));
  abi::copyText(entry->name, value.name, sizeof(value.name));
  abi::copyText(entry->description, value.description,
                sizeof(value.description));
  abi::copyText(entry->author, value.author, sizeof(value.author));
  abi::copyText(entry->version, value.version, sizeof(value.version));
  abi::copyText(entry->minAbcUi, value.minAbcUi, sizeof(value.minAbcUi));
  abi::copyText(entry->entry, value.entry, sizeof(value.entry));
  *out = value;
  return 1;
}

int32_t apiFind(const char *file) {
  return ModuleCatalog::instance().find(file);
}

int32_t apiRelationAt(int32_t index, int32_t relation,
                      AbcUiModuleRelation *out) {
  if (!out || out->structSize < sizeof(AbcUiModuleRelation))
    return 0;
  const Relation *value = ModuleCatalog::instance().relationAt(index, relation);
  if (!value)
    return 0;
  AbcUiModuleRelation result{};
  result.structSize = sizeof(result);
  result.kind = value->kind;
  result.state = value->state;
  result.declaredBy = value->declaredBy ? 1 : 0;
  abi::copyText(value->id, result.id, sizeof(result.id));
  abi::copyText(value->name, result.name, sizeof(result.name));
  abi::copyText(value->file, result.file, sizeof(result.file));
  *out = result;
  return 1;
}

int32_t apiDiagnosticAt(int32_t index, int32_t diagnostic,
                        AbcUiModuleDiagnostic *out) {
  if (!out || out->structSize < sizeof(AbcUiModuleDiagnostic))
    return 0;
  const Diagnostic *value =
      ModuleCatalog::instance().diagnosticAt(index, diagnostic);
  if (!value)
    return 0;
  AbcUiModuleDiagnostic result{};
  result.structSize = sizeof(result);
  result.severity = value->severity;
  result.code = value->code;
  abi::copyText(value->subject, result.subject, sizeof(result.subject));
  abi::copyText(value->detail, result.detail, sizeof(result.detail));
  *out = result;
  return 1;
}

int32_t apiPreviewToggle(const char *file, int32_t enabled) {
  return ModuleCatalog::instance().previewToggle(file, enabled);
}

void apiClearPreview() {
  ModuleCatalog::instance().clearPreview();
}

int32_t apiSetPendingEnabled(const char *file, int32_t enabled,
                             AbcUiModuleChange *out) {
  Change change;
  const int32_t code =
      ModuleCatalog::instance().setPendingEnabled(file, enabled, change);
  if (out && out->structSize >= sizeof(AbcUiModuleChange)) {
    AbcUiModuleChange value{};
    value.structSize = sizeof(value);
    value.code = change.code;
    value.pending = change.pending;
    value.restartRequired = change.restartRequired ? 1 : 0;
    value.affectedCount = change.affectedCount;
    abi::copyText(change.file, value.file, sizeof(value.file));
    abi::copyText(change.targetFile, value.targetFile,
                  sizeof(value.targetFile));
    *out = value;
  }
  return code;
}

int32_t apiPendingCount() {
  return ModuleCatalog::instance().pendingCount();
}

int32_t apiRestartRequired() {
  return ModuleCatalog::instance().restartRequired() ? 1 : 0;
}

const AbcUiModulesApi kModulesApi = {
    sizeof(AbcUiModulesApi),
    apiRevision,
    apiRescan,
    apiCount,
    apiAt,
    apiFind,
    apiRelationAt,
    apiDiagnosticAt,
    apiPreviewToggle,
    apiClearPreview,
    apiSetPendingEnabled,
    apiPendingCount,
    apiRestartRequired,
};

} // namespace

const AbcUiModulesApi *get() {
  return &kModulesApi;
}

} // namespace ModulesApi
} // namespace framework
