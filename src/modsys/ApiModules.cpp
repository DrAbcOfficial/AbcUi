// framework::ModulesApi - the sdk modules section. See ApiModules.h.
#include "ApiModules.h"

#include "ApiTable.h"
#include "ModuleCatalog.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace framework {
namespace ModulesApi {
namespace {

using namespace modulecatalog;

void copyString(char *out, size_t size, const std::string &text) {
  if (!out || size == 0)
    return;
  const size_t copy = text.size() < size - 1 ? text.size() : size - 1;
  if (copy)
    std::memcpy(out, text.data(), copy);
  out[copy] = '\0';
}

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
  copyString(value.file, sizeof(value.file), entry->file);
  copyString(value.id, sizeof(value.id), entry->id);
  copyString(value.name, sizeof(value.name), entry->name);
  copyString(value.description, sizeof(value.description), entry->description);
  copyString(value.author, sizeof(value.author), entry->author);
  copyString(value.version, sizeof(value.version), entry->version);
  copyString(value.minAbcUi, sizeof(value.minAbcUi), entry->minAbcUi);
  copyString(value.entry, sizeof(value.entry), entry->entry);
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
  copyString(result.id, sizeof(result.id), value->id);
  copyString(result.name, sizeof(result.name), value->name);
  copyString(result.file, sizeof(result.file), value->file);
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
  copyString(result.subject, sizeof(result.subject), value->subject);
  copyString(result.detail, sizeof(result.detail), value->detail);
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
    copyString(value.file, sizeof(value.file), change.file);
    copyString(value.targetFile, sizeof(value.targetFile), change.targetFile);
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
