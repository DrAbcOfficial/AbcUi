// The module manager page: a projection of the framework module catalog.
// Toggles only update the framework's in-process cache; the real package
// files are renamed later, after every module has unloaded.
#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace settingspage {
namespace {

const AbcUiModulesApi *modulesApi() {
  if (!Api || !Api->modules ||
      Api->modules->structSize < sizeof(AbcUiModulesApi))
    return nullptr;
  return Api->modules;
}

bool moduleInfo(int32_t index, AbcUiModuleInfo &out) {
  const AbcUiModulesApi *api = modulesApi();
  if (!api)
    return false;
  out = {};
  out.structSize = sizeof(out);
  return api->at(index, &out) != 0;
}

std::string lowerAscii(std::string text);

std::string moduleStateToken(int32_t state) {
  switch (state) {
  case ABCUI_MODULE_STATE_LOADED:
    return resolve("#abcui.gameui/Settings_ModulesStateLoaded");
  case ABCUI_MODULE_STATE_WILL_LOAD:
    return resolve("#abcui.gameui/Settings_ModulesStateWillLoad");
  case ABCUI_MODULE_STATE_DISABLED:
    return resolve("#abcui.gameui/Settings_ModulesStateDisabled");
  case ABCUI_MODULE_STATE_SKIPPED:
    return resolve("#abcui.gameui/Settings_ModulesStateSkipped");
  case ABCUI_MODULE_STATE_FAILED:
    return resolve("#abcui.gameui/Settings_ModulesStateFailed");
  case ABCUI_MODULE_STATE_INVALID:
    return resolve("#abcui.gameui/Settings_ModulesStateInvalid");
  default:
    return {};
  }
}

std::string modulePendingToken(int32_t pending) {
  if (pending == ABCUI_MODULE_PENDING_ENABLE)
    return resolve("#abcui.gameui/Settings_ModulesPendingEnable");
  if (pending == ABCUI_MODULE_PENDING_DISABLE)
    return resolve("#abcui.gameui/Settings_ModulesPendingDisable");
  return {};
}

std::string moduleDiagToken(int32_t code) {
  switch (code) {
  case ABCUI_MODULE_DIAG_UNREADABLE:
    return resolve("#abcui.gameui/Settings_ModulesDiagUnreadable");
  case ABCUI_MODULE_DIAG_MANIFEST_INVALID:
    return resolve("#abcui.gameui/Settings_ModulesDiagManifest");
  case ABCUI_MODULE_DIAG_ABI_MISMATCH:
    return resolve("#abcui.gameui/Settings_ModulesDiagAbi");
  case ABCUI_MODULE_DIAG_FRAMEWORK_TOO_OLD:
    return resolve("#abcui.gameui/Settings_ModulesDiagFramework");
  case ABCUI_MODULE_DIAG_DUPLICATE_VERSION:
    return resolve("#abcui.gameui/Settings_ModulesDiagDuplicate");
  case ABCUI_MODULE_DIAG_AMBIGUOUS_ID:
    return resolve("#abcui.gameui/Settings_ModulesDiagAmbiguous");
  case ABCUI_MODULE_DIAG_MISSING_DEPENDENCY:
    return resolve("#abcui.gameui/Settings_ModulesDiagMissing");
  case ABCUI_MODULE_DIAG_DISABLED_DEPENDENCY:
    return resolve("#abcui.gameui/Settings_ModulesDiagDisabled");
  case ABCUI_MODULE_DIAG_FAILED_DEPENDENCY:
    return resolve("#abcui.gameui/Settings_ModulesDiagFailed");
  case ABCUI_MODULE_DIAG_DEPENDENCY_CYCLE:
    return resolve("#abcui.gameui/Settings_ModulesDiagCycle");
  case ABCUI_MODULE_DIAG_CONFLICT_ACTIVE:
    return resolve("#abcui.gameui/Settings_ModulesDiagConflict");
  case ABCUI_MODULE_DIAG_CONFLICT_INACTIVE:
    return resolve("#abcui.gameui/Settings_ModulesDiagConflictOff");
  case ABCUI_MODULE_DIAG_ENTRY_MISSING:
    return resolve("#abcui.gameui/Settings_ModulesDiagEntry");
  case ABCUI_MODULE_DIAG_IMAGE_LOAD_FAILED:
    return resolve("#abcui.gameui/Settings_ModulesDiagImage");
  case ABCUI_MODULE_DIAG_INIT_FAILED:
    return resolve("#abcui.gameui/Settings_ModulesDiagInit");
  case ABCUI_MODULE_DIAG_FILE_CHANGED:
    return resolve("#abcui.gameui/Settings_ModulesDiagChanged");
  case ABCUI_MODULE_DIAG_PENDING_INVALID:
    return resolve("#abcui.gameui/Settings_ModulesDiagPending");
  default:
    return resolve("#abcui.gameui/Settings_ModulesDiagGeneric");
  }
}

std::string moduleRelationKindToken(int32_t kind) {
  switch (kind) {
  case ABCUI_MODULE_REL_DEPENDENCY:
    return resolve("#abcui.gameui/Settings_ModulesRelDependency");
  case ABCUI_MODULE_REL_DEPENDENT:
    return resolve("#abcui.gameui/Settings_ModulesRelDependent");
  case ABCUI_MODULE_REL_CONFLICT:
    return resolve("#abcui.gameui/Settings_ModulesRelConflict");
  default:
    return {};
  }
}

std::string moduleRelationStateToken(int32_t state) {
  switch (state) {
  case ABCUI_MODULE_REL_OK:
    return resolve("#abcui.gameui/Settings_ModulesRelOk");
  case ABCUI_MODULE_REL_DISABLED:
    return resolve("#abcui.gameui/Settings_ModulesRelDisabled");
  case ABCUI_MODULE_REL_MISSING:
    return resolve("#abcui.gameui/Settings_ModulesRelMissing");
  case ABCUI_MODULE_REL_FAILED:
    return resolve("#abcui.gameui/Settings_ModulesRelFailed");
  case ABCUI_MODULE_REL_CYCLE:
    return resolve("#abcui.gameui/Settings_ModulesRelCycle");
  case ABCUI_MODULE_REL_ACTIVE:
    return resolve("#abcui.gameui/Settings_ModulesRelActive");
  default:
    return {};
  }
}

std::string moduleErrorToken(int32_t code) {
  switch (code) {
  case ABCUI_MODULE_CACHE_INVALID:
    return resolve("#abcui.gameui/Settings_ModulesErrorInvalidName");
  case ABCUI_MODULE_CACHE_NOT_FOUND:
    return resolve("#abcui.gameui/Settings_ModulesErrorSourceMissing");
  case ABCUI_MODULE_CACHE_EXISTS:
    return resolve("#abcui.gameui/Settings_ModulesErrorTargetExists");
  case ABCUI_MODULE_CACHE_DENIED:
    return resolve("#abcui.gameui/Settings_ModulesErrorPermission");
  case ABCUI_MODULE_CACHE_NO_CHANGE:
    return resolve("#abcui.gameui/Settings_ModulesNoChange");
  default:
    return resolve("#abcui.gameui/Settings_ModulesErrorGeneric");
  }
}

std::string moduleDisplayName(const AbcUiModuleInfo &info) {
  if (info.name[0])
    return info.name;
  if (info.id[0])
    return info.id;
  return info.file;
}

bool moduleHasProblem(const AbcUiModuleInfo &info) {
  if (info.state == ABCUI_MODULE_STATE_FAILED ||
      info.state == ABCUI_MODULE_STATE_INVALID ||
      info.state == ABCUI_MODULE_STATE_SKIPPED)
    return true;
  const AbcUiModulesApi *api = modulesApi();
  if (!api)
    return false;
  for (int32_t i = 0; i < info.diagnosticCount; ++i) {
    AbcUiModuleDiagnostic diagnostic{};
    diagnostic.structSize = sizeof(diagnostic);
    if (!api->diagnosticAt(info.index, i, &diagnostic))
      break;
    if (diagnostic.severity <= ABCUI_MODULE_DIAG_WARNING)
      return true;
  }
  return false;
}

bool moduleMatchesFilter(const AbcUiModuleInfo &info,
                         const std::string &filter) {
  if (filter == "loaded")
    return info.state == ABCUI_MODULE_STATE_LOADED;
  if (filter == "enabled")
    return (info.flags & ABCUI_MODULE_FLAG_ENABLED) != 0;
  if (filter == "disabled")
    return info.state == ABCUI_MODULE_STATE_DISABLED;
  if (filter == "problems")
    return moduleHasProblem(info);
  return true;
}

std::string moduleBadge(const std::string &text, const char *className) {
  return "<span class=\"module-badge " + std::string(className) + "\">" +
         escapeText(text) + "</span>";
}

std::string moduleDiagnosticLine(int32_t index, int32_t diagnosticIndex) {
  const AbcUiModulesApi *api = modulesApi();
  if (!api)
    return {};
  AbcUiModuleDiagnostic diagnostic{};
  diagnostic.structSize = sizeof(diagnostic);
  if (!api->diagnosticAt(index, diagnosticIndex, &diagnostic))
    return {};
  std::string text = moduleDiagToken(diagnostic.code);
  if (diagnostic.subject[0] &&
      diagnostic.code != ABCUI_MODULE_DIAG_UNREADABLE &&
      diagnostic.code != ABCUI_MODULE_DIAG_MANIFEST_INVALID)
    text += ": " + std::string(diagnostic.subject);
  if (diagnostic.detail[0])
    text += " (" + std::string(diagnostic.detail) + ")";
  return "<div class=\"module-diagnostic " +
         std::string(diagnostic.severity == ABCUI_MODULE_DIAG_ERROR
                         ? "module-diag-error"
                         : "module-diag-warning") +
         "\">" + escapeText(text) + "</div>";
}

std::string moduleFact(const char *labelToken, const std::string &value) {
  return "<div class=\"module-fact\"><span class=\"module-fact-label\">" +
         escapeText(resolve(labelToken)) +
         "</span><span class=\"module-fact-value\">" + escapeText(value) +
         "</span></div>";
}

std::string moduleRelationLine(const AbcUiModuleInfo &info,
                               const AbcUiModuleRelation &relation) {
  (void)info;
  const char *stateClass = "module-rel-ok";
  switch (relation.state) {
  case ABCUI_MODULE_REL_DISABLED:
    stateClass = "module-rel-disabled";
    break;
  case ABCUI_MODULE_REL_MISSING:
    stateClass = "module-rel-missing";
    break;
  case ABCUI_MODULE_REL_FAILED:
    stateClass = "module-rel-failed";
    break;
  case ABCUI_MODULE_REL_CYCLE:
    stateClass = "module-rel-cycle";
    break;
  case ABCUI_MODULE_REL_ACTIVE:
    stateClass = "module-rel-active";
    break;
  default:
    break;
  }
  return "<div class=\"module-relation\"><span "
         "class=\"module-relation-kind\">" +
         escapeText(moduleRelationKindToken(relation.kind)) +
         "</span><span class=\"module-relation-id\">" +
         escapeText(relation.id[0] ? relation.id : "-") +
         "</span><span class=\"module-relation-state " + stateClass + "\">" +
         escapeText(moduleRelationStateToken(relation.state)) + "</span></div>";
}

std::string moduleDetailMarkup(const AbcUiModuleInfo &info) {
  const AbcUiModulesApi *api = modulesApi();
  std::string markup = "<div class=\"module-detail\">";
  markup += "<div class=\"module-facts\">";
  markup += "<div class=\"module-facts-col\">";
  markup += moduleFact("#abcui.gameui/Settings_ModulesVersion", info.version);
  markup += moduleFact("#abcui.gameui/Settings_ModulesFile", info.file);
  markup += moduleFact("ABI", std::to_string(info.abi));
  markup += "</div><div class=\"module-facts-col second\">";
  markup += moduleFact("#abcui.gameui/Settings_ModulesAuthor", info.author);
  markup += moduleFact("#abcui.gameui/Settings_ModulesEntry",
                       info.entry[0] ? info.entry : "-");
  markup += moduleFact("#abcui.gameui/Settings_ModulesMinAbcUi", info.minAbcUi);
  markup += "</div></div>";

  if (!api)
    return markup + "</div>";

  std::string relations;
  const int32_t relationCount =
      info.dependencyCount + info.dependentCount + info.conflictCount;
  for (int32_t i = 0; i < relationCount; ++i) {
    AbcUiModuleRelation relation{};
    relation.structSize = sizeof(relation);
    if (!api->relationAt(info.index, i, &relation))
      break;
    relations += moduleRelationLine(info, relation);
  }
  if (!relations.empty()) {
    markup += "<div class=\"module-detail-block\"><div "
              "class=\"module-detail-heading\">" +
              escapeText(resolve("#abcui.gameui/Settings_ModulesRelations")) +
              "</div>" + relations + "</div>";
  }

  std::string diagnostics;
  for (int32_t i = 0; i < info.diagnosticCount; ++i)
    diagnostics += moduleDiagnosticLine(info.index, i);
  if (!diagnostics.empty()) {
    markup += "<div class=\"module-detail-block\"><div "
              "class=\"module-detail-heading\">" +
              escapeText(resolve("#abcui.gameui/Settings_ModulesDiagnostics")) +
              "</div>" + diagnostics + "</div>";
  }

  markup += "</div>";
  return markup;
}

std::string moduleRowMarkup(const AbcUiModuleInfo &info) {
  const bool selected =
      lowerAscii(ModuleDetailFile) == lowerAscii(std::string(info.file));
  std::string markup = "<div class=\"module-row" +
                       std::string(selected ? " selected" : "") +
                       "\" data-action=\"" + kActionModuleDetail +
                       "\" data-file=\"" + escapeAttribute(info.file) + "\">";
  markup += "<div class=\"module-row-head\"><div class=\"module-title\">" +
            escapeText(moduleDisplayName(info));
  if (info.id[0])
    markup += "<span class=\"module-id\">" + escapeText(info.id) + "</span>";
  markup += "</div><div class=\"module-badges\">";
  markup += moduleBadge(moduleStateToken(info.state), "module-state");
  if (info.pending == ABCUI_MODULE_PENDING_ENABLE)
    markup +=
        moduleBadge(modulePendingToken(info.pending), "module-pending-badge");
  else if (info.pending == ABCUI_MODULE_PENDING_DISABLE)
    markup +=
        moduleBadge(modulePendingToken(info.pending), "module-pending-badge");
  if (info.state == ABCUI_MODULE_STATE_SKIPPED ||
      info.state == ABCUI_MODULE_STATE_FAILED ||
      info.state == ABCUI_MODULE_STATE_INVALID)
    markup += moduleBadge(resolve("#abcui.gameui/Settings_ModulesProblem"),
                          "module-problem");
  markup += "</div>";
  const bool targetEnable = info.pending != ABCUI_MODULE_PENDING_NONE
                                ? (info.flags & ABCUI_MODULE_FLAG_ENABLED) != 0
                                : (info.flags & ABCUI_MODULE_FLAG_ENABLED) == 0;
  markup += "<button class=\"module-toggle\" data-action=\"" +
            std::string(kActionModuleToggle) + "\" data-file=\"" +
            escapeAttribute(info.file) + "\" data-enable=\"" +
            (targetEnable ? "1" : "0") + "\">";
  if (info.pending == ABCUI_MODULE_PENDING_ENABLE)
    markup += escapeText(resolve("#abcui.gameui/Settings_ModulesCancelEnable"));
  else if (info.pending == ABCUI_MODULE_PENDING_DISABLE)
    markup +=
        escapeText(resolve("#abcui.gameui/Settings_ModulesCancelDisable"));
  else if (info.flags & ABCUI_MODULE_FLAG_ENABLED)
    markup += escapeText(resolve("#abcui.gameui/Settings_ModulesDisable"));
  else
    markup += escapeText(resolve("#abcui.gameui/Settings_ModulesEnable"));
  markup += "</button></div>";
  if (info.version[0])
    markup +=
        "<div class=\"module-meta\">v" + escapeText(info.version) + "</div>";
  if (info.description[0])
    markup +=
        "<div class=\"module-desc\">" + escapeText(info.description) + "</div>";
  if (selected)
    markup += moduleDetailMarkup(info);
  markup += "</div>";
  return markup;
}

std::string moduleSummaryText() {
  const AbcUiModulesApi *api = modulesApi();
  if (!api)
    return {};
  int32_t loaded = 0;
  int32_t disabled = 0;
  int32_t problems = 0;
  const int32_t count = api->count();
  for (int32_t i = 0; i < count; ++i) {
    AbcUiModuleInfo info{};
    if (!moduleInfo(i, info))
      continue;
    if (info.state == ABCUI_MODULE_STATE_LOADED)
      ++loaded;
    if (info.state == ABCUI_MODULE_STATE_DISABLED)
      ++disabled;
    if (moduleHasProblem(info))
      ++problems;
  }
  return std::to_string(loaded) + " " +
         resolve("#abcui.gameui/Settings_ModulesLoaded") + " · " +
         std::to_string(disabled) + " " +
         resolve("#abcui.gameui/Settings_ModulesDisabled") + " · " +
         std::to_string(problems) + " " +
         resolve("#abcui.gameui/Settings_ModulesProblems");
}

void setFilterSelected() {
  for (const char *filter :
       {"all", "loaded", "enabled", "disabled", "problems"}) {
    if (AbcUiElement *button = node(std::string("module-filter-") + filter))
      Api->ui->elementSetClass(button, "selected", ModuleFilter == filter);
  }
}

std::string lowerAscii(std::string text) {
  for (char &c : text) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

} // namespace

std::string ModuleFilter = "all";
std::string ModuleDetailFile;
std::string ModuleStatusText;
bool ModuleQuitConfirmOpen = false;
bool ModuleBannerDismissed = false;
uint32_t ModuleRenderedRevision = 0;

void modulesRefresh() {
  const AbcUiModulesApi *api = modulesApi();
  if (!api)
    return;
  api->rescan();
  api->clearPreview();
  if (!ModuleDetailFile.empty() && api->find(ModuleDetailFile.c_str()) < 0)
    ModuleDetailFile.clear();
  ModuleBannerDismissed = false;
  ModuleStatusText.clear();
  ModuleRenderedRevision = 0;
}

void moduleReset() {
  ModuleDetailFile.clear();
  ModuleStatusText.clear();
  ModuleQuitConfirmOpen = false;
  ModuleBannerDismissed = false;
  if (modulesApi())
    modulesApi()->clearPreview();
}

void renderModules() {
  if (!Page || !modulesApi())
    return;
  const AbcUiModulesApi *api = modulesApi();
  const int32_t pending = api->pendingCount();
  setText("module-summary", moduleSummaryText());
  setDisplay("module-pending", pending > 0 && !ModuleBannerDismissed);
  if (pending > 0) {
    std::string text = std::to_string(pending) + " " +
                       resolve("#abcui.gameui/Settings_ModulesPendingCount");
    setText("module-pending-text", text);
  }
  setFilterSelected();

  std::vector<int32_t> indices;
  for (int32_t i = 0; i < api->count(); ++i) {
    AbcUiModuleInfo info{};
    if (!moduleInfo(i, info))
      continue;
    if (!moduleMatchesFilter(info, ModuleFilter))
      continue;
    indices.push_back(i);
  }
  std::sort(indices.begin(), indices.end(), [&](int32_t left, int32_t right) {
    AbcUiModuleInfo a{};
    AbcUiModuleInfo b{};
    if (!moduleInfo(left, a) || !moduleInfo(right, b))
      return left < right;
    const std::string an = moduleDisplayName(a);
    const std::string bn = moduleDisplayName(b);
    if (an != bn)
      return lowerAscii(an) < lowerAscii(bn);
    return std::string(a.file) < std::string(b.file);
  });

  std::string markup;
  for (const int32_t index : indices) {
    AbcUiModuleInfo info{};
    if (moduleInfo(index, info))
      markup += moduleRowMarkup(info);
  }
  if (markup.empty()) {
    markup = "<div class=\"module-empty\">" +
             escapeText(resolve("#abcui.gameui/Settings_ModulesEmpty")) +
             "</div>";
  }
  setInnerRml("module-list", markup);

  setText("module-status", ModuleStatusText);
  setDisplay("module-status", !ModuleStatusText.empty());

  setDisplay("module-quit-confirm", ModuleQuitConfirmOpen);
  ModuleRenderedRevision = api->revision();
}

bool handleModuleAction(const std::string &action, AbcUiElement *entry) {
  const AbcUiModulesApi *api = modulesApi();
  if (!api || !entry)
    return false;
  if (action == kActionModuleFilter) {
    const std::string filter = attributeOf(entry, "data-filter");
    if (filter == "all" || filter == "loaded" || filter == "enabled" ||
        filter == "disabled" || filter == "problems")
      ModuleFilter = filter;
    return true;
  }
  if (action == kActionModuleRescan) {
    modulesRefresh();
    return true;
  }
  if (action == kActionModuleDetail) {
    const std::string file = attributeOf(entry, "data-file");
    ModuleDetailFile = ModuleDetailFile == file ? std::string() : file;
    return true;
  }
  if (action == kActionModuleToggle) {
    const std::string file = attributeOf(entry, "data-file");
    const bool enable = attributeOf(entry, "data-enable") == "1";
    AbcUiModuleChange change{};
    change.structSize = sizeof(change);
    const int32_t result =
        api->setPendingEnabled(file.c_str(), enable ? 1 : 0, &change);
    if (result == ABCUI_MODULE_CACHE_OK ||
        result == ABCUI_MODULE_CACHE_NO_CHANGE) {
      ModuleStatusText.clear();
      ModuleBannerDismissed = false;
    } else {
      ModuleStatusText = moduleErrorToken(result);
    }
    return true;
  }
  if (action == kActionModuleQuit) {
    ModuleQuitConfirmOpen = true;
    showLayer(kLayerModuleQuitConfirm);
    return true;
  }
  if (action == kActionModuleQuitYes) {
    ModuleQuitConfirmOpen = false;
    hideLayers();
    Api->menu->quitNow();
    return true;
  }
  if (action == kActionModuleQuitNo) {
    ModuleQuitConfirmOpen = false;
    hideLayers();
    return true;
  }
  if (action == kActionModuleLater) {
    ModuleBannerDismissed = true;
    return true;
  }
  return false;
}

} // namespace settingspage
