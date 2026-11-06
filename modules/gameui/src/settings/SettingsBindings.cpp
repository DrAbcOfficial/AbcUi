// The key-binding card: the action skeleton, the per-row draft projection,
// the conflict layer and the binding status summary.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <string>
#include <vector>

namespace settingspage {

// ----------------------------------------------------------- bindings -----

// kb_act.lst delimits its sections with "blank" rows whose label is the
// vanilla file's ASCII rule ("==========================="). Those rows are
// file typography, not content: this theme renders the real titles only, so
// the rules (and any label-less divider) are dropped from the skeleton.
bool bindHeaderIsRule(const AbcUiBindAction &row) {
  for (const char *c = row.label; *c; ++c) {
    if (*c != '=' && *c != ' ' && *c != '\t')
      return false;
  }
  return true;
}

// Shared resolution for one raw label string ("#Valve_X" or literal).
std::string bindLabelText(const std::string &label) {
  if (label.empty() || label[0] != '#')
    return label;
  const std::string token = label.substr(1);
  std::string text = translate(token.c_str());
  if (text != token)
    return text;
  char buffer[ABCUI_SETTINGS_LABEL_MAX];
  buffer[0] = '\0';
  if (Api->settings &&
      Api->settings->bindLookupLabel(token.c_str(), buffer, sizeof(buffer)))
    return buffer;
  return label;
}

std::string bindActionLabel(const AbcUiBindAction &row) {
  return bindLabelText(row.label);
}

// One bind-list row's skeleton: the engine actions and the key-code rows use
// the same markup, only the intent names and the row id differ.
void AppendBindRowMarkup(std::string &markup, const std::string &id,
                         const char *captureAction, const char *clearAction) {
  markup += "<div class=\"bind-row\" id=\"bindrow-" + id +
            "\"><div class=\"bind-line\"><div class=\"bind-label\" "
            "id=\"bindrow-" +
            id + "-label\"></div><div class=\"bind-keys\" id=\"bindrow-" + id +
            "-keys\"></div><button class=\"bind-btn bind-capture-btn\" "
            "id=\"bindrow-" +
            id + "-capture\" data-action=\"" + captureAction +
            "\" data-row=\"" + escapeAttribute(id) +
            "\"></button><button class=\"bind-btn bind-clear-btn\" "
            "id=\"bindrow-" +
            id + "-clear\" data-action=\"" + clearAction + "\" data-row=\"" +
            escapeAttribute(id) +
            "\"></button></div><div class=\"bind-status\" id=\"bindrow-" + id +
            "-status\"></div></div>";
}

// The key-code rows map their settings-row state onto the bind list's
// vocabulary (their drafts are written by the page Apply, so the value never
// lands queued - the mapping is complete rather than partial).
int bindStateFor(const AbcUiSettingRow &row) {
  switch (row.state) {
  case ABCUI_SET_STATE_DIRTY:
  case ABCUI_SET_STATE_CONFLICT:
    return ABCUI_BIND_STATE_DIRTY;
  case ABCUI_SET_STATE_QUEUED:
    return ABCUI_BIND_STATE_QUEUED;
  case ABCUI_SET_STATE_APPLY_FAILED:
    return ABCUI_BIND_STATE_APPLY_FAILED;
  case ABCUI_SET_STATE_MISSING:
    return ABCUI_BIND_STATE_UNAVAILABLE;
  default:
    return ABCUI_BIND_STATE_CLEAN;
  }
}

// One bind-list row's projection (label, key list, both button captions and
// the status line): the engine-action rows and the key-code rows share it,
// only the data source differs.
void renderBindRowParts(const std::string &id, const std::string &label,
                        const std::string &keys, bool capturing, bool enabled,
                        int bindState) {
  if (!Page)
    return;
  AbcUiElement *rowNode = node("bindrow-" + id);
  if (!rowNode)
    return;
  // The capturing row highlights its key list (class, not a display
  // property - the color lands immediately).
  Api->ui->elementSetClass(rowNode, "bind-capturing", capturing);
  setInnerRml(node("bindrow-" + id + "-label"), escapeText(label));
  // Keys in draft order; the "unbound" placeholder keeps the row readable.
  setInnerRml(node("bindrow-" + id + "-keys"),
              escapeText(keys.empty()
                             ? resolve("#abcui.gameui/Settings_BindNone")
                             : keys));

  // Buttons: capture text flips while this row is capturing; clear is the
  // unbind-all-keys affordance of the row (drafts only until Apply).
  if (AbcUiElement *capture = node("bindrow-" + id + "-capture")) {
    Api->ui->elementSetDisabled(capture, !enabled);
    setInnerRml(capture,
                escapeText(resolve(
                    capturing ? "#abcui.gameui/Settings_BindCancelCapture"
                              : "#abcui.gameui/Settings_BindCapture")));
  }
  if (AbcUiElement *clear = node("bindrow-" + id + "-clear")) {
    Api->ui->elementSetDisabled(clear, !enabled);
    setInnerRml(clear, escapeText(resolve("#abcui.gameui/Settings_BindClear")));
  }

  std::string status;
  if (capturing) {
    status = resolve("#abcui.gameui/Settings_BindPressKey");
  } else {
    switch (bindState) {
    case ABCUI_BIND_STATE_DIRTY:
      status = resolve("#abcui.gameui/Settings_BindDirty");
      break;
    case ABCUI_BIND_STATE_QUEUED:
      status = resolve("#abcui.gameui/Settings_BindQueued");
      break;
    case ABCUI_BIND_STATE_APPLY_FAILED:
      status = resolve("#abcui.gameui/Settings_BindFailed");
      break;
    case ABCUI_BIND_STATE_UNAVAILABLE:
      status = resolve("#abcui.gameui/Settings_BindUnavailable");
      break;
    default:
      break;
    }
  }
  setInnerRml(node("bindrow-" + id + "-status"), escapeText(status));
}

void buildBindRows() {
  if (!Page || !Api->settings)
    return;
  clearProjectionCache();
  AbcUiElement *rows = node("bindrows");
  if (!rows)
    return;
  // One skeleton block per kb_act entry: section headers render as plain
  // dividers, actions carry label + key list + bind/clear buttons. The DOM
  // id is the action index (a plain number).
  std::string markup;
  const int32_t count = Api->settings->bindRowCount();
  for (int32_t index = 0; index < count; ++index) {
    AbcUiBindAction row{};
    row.structSize = sizeof(row);
    if (!Api->settings->bindRowAt(index, &row))
      continue;
    if (row.header) {
      if (bindHeaderIsRule(row))
        continue;
      markup += "<div class=\"bind-header\"><span>" +
                escapeText(bindActionLabel(row)) +
                "</span><span class=\"bind-header-columns\">" +
                escapeText(resolve("#abcui.gameui/Settings_BindColumns")) +
                "</span></div>";
      continue;
    }
    const std::string id = std::to_string(row.index);
    AppendBindRowMarkup(markup, id, kActionBindCapture, kActionBindClear);
  }
  // The key-code rows (the vote answer keys) close the list as their own
  // group: same rows, same buttons, same status vocabulary - only the backing
  // store differs (the row's cvar draft instead of the engine command table).
  appendKeyBindRows(markup);
  Api->ui->elementSetInnerRML(rows, markup.c_str());
}

// The section label of a key-code group: the page tree declares the group
// (the rows carry its id), so the header is translated from the same data
// every card title comes from. An undeclared token falls back to itself.
std::string sectionLabelById(const std::string &id) {
  if (id.empty())
    return {};
  for (int32_t page = 0; page < pageCount(); ++page) {
    AbcUiSettingsPage definition{};
    if (!pageAt(page, definition))
      continue;
    for (int32_t index = 0; index < definition.sectionCount; ++index) {
      AbcUiSettingsSection section{};
      if (!sectionAt(page, index, section))
        continue;
      if (id == section.id)
        return resolve(section.label);
    }
  }
  return id;
}

int keyRowCount() {
  int count = 0;
  forEachKeyRow([&](const AbcUiSettingRow &) { ++count; });
  return count;
}

void appendKeyBindRows(std::string &markup) {
  std::string group;
  forEachKeyRow([&](const AbcUiSettingRow &row) {
    if (group != row.section) {
      group = row.section;
      markup += "<div class=\"bind-header\"><span>" +
                escapeText(sectionLabelById(group)) +
                "</span><span class=\"bind-header-columns\">" +
                escapeText(resolve("#abcui.gameui/Settings_BindColumns")) +
                "</span></div>";
    }
    AppendBindRowMarkup(markup, row.cvar, kActionKeyCapture, kActionKeyClear);
  });
}

void renderKeyBindRow(const AbcUiSettingRow &row) {
  if (!Api->settings)
    return;
  char captured[ABCUI_SETTINGS_CVAR_MAX];
  captured[0] = '\0';
  const bool capturing =
      Api->settings->keyCaptureCvar(captured, sizeof(captured)) != 0 &&
      std::string(row.cvar) == captured;
  // The key name of the draft (a cleared row is 0: "unbound"). The name is
  // engine text; the shared projection escapes it like every bind row.
  const float keyValue =
      row.state == ABCUI_SET_STATE_INVALID ? row.engineValue : row.draftValue;
  char name[ABCUI_SETTINGS_KEY_MAX];
  name[0] = '\0';
  const bool named = keyValue > 0.0f &&
                     Api->ui->keyNameForKey(static_cast<int>(keyValue), name,
                                            sizeof(name)) > 0 &&
                     name[0] != '\0';
  const bool enabled = (row.flags & ABCUI_SET_ROW_MISSING) == 0 &&
                       (row.flags & ABCUI_SET_ROW_EDITABLE) != 0;
  renderBindRowParts(row.cvar, resolve(row.labelToken),
                     named ? std::string(name) : std::string(), capturing,
                     enabled, bindStateFor(row));
}

// "Restore default keys" covers the whole list: the key-code rows' defaults
// live in the page table, so the same confirmation stages them as drafts.
void resetKeyRowDefaults() {
  if (!Api->settings)
    return;
  std::vector<std::string> cvars;
  forEachKeyRow(
      [&](const AbcUiSettingRow &row) { cvars.emplace_back(row.cvar); });
  if (cvars.empty())
    return;
  std::vector<const char *> pointers;
  pointers.reserve(cvars.size());
  for (const auto &cvar : cvars)
    pointers.push_back(cvar.c_str());
  Api->settings->resetDefaults(pointers.data(),
                               static_cast<int32_t>(pointers.size()));
}

void renderBindRow(const AbcUiBindAction &row) {
  if (!Api->settings)
    return;
  // Keys in draft order; the shared projection adds the "unbound" placeholder.
  std::string keys;
  for (int32_t index = 0; index < row.keyCount; ++index) {
    char key[ABCUI_SETTINGS_KEY_MAX];
    key[0] = '\0';
    if (!Api->settings->bindKeyAt(row.index, index, key, sizeof(key)))
      continue;
    if (!keys.empty())
      keys += ", ";
    keys += key;
  }
  renderBindRowParts(std::to_string(row.index), bindActionLabel(row), keys,
                     Api->settings->bindCapturing() == row.index, true,
                     row.state);
}

void renderBindConflict() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *layer = node(kLayerBindConflict);
  if (!layer)
    return;
  if (Api->settings->bindConflictPending()) {
    // The keyname and the other action's label ride the layer text, each
    // piece escaped on its own (the label may carry user-facing engine
    // strings; nothing here is ever injected unescaped).
    char keyName[ABCUI_SETTINGS_KEY_MAX];
    char otherLabel[ABCUI_SETTINGS_LABEL_MAX];
    keyName[0] = '\0';
    otherLabel[0] = '\0';
    Api->settings->bindConflictKeyName(keyName, sizeof(keyName));
    Api->settings->bindConflictOtherLabel(otherLabel, sizeof(otherLabel));
    setInnerRml(node("bind-conflict-text"),
                escapeText(resolve("#abcui.gameui/Settings_BindConflictLead")) +
                    " " + escapeText(keyName) + " " +
                    escapeText(resolve("#abcui.gameui/Settings_BindBoundTo")) +
                    " " + escapeText(bindLabelText(otherLabel)) + "?");
    if (ActiveLayer != kLayerBindConflict)
      showLayer(kLayerBindConflict);
  } else if (ActiveLayer == kLayerBindConflict) {
    hideLayers();
  }
}

void setBindStatusSummary() {
  if (!Page || !Api->settings)
    return;
  // The key-code rows share the list, so their drafts count as pending key
  // changes too (and their presence keeps the section usable when the engine
  // bind table itself is unreachable).
  int keyRows = 0;
  bool keyDirty = false;
  forEachKeyRow([&](const AbcUiSettingRow &row) {
    ++keyRows;
    switch (row.state) {
    case ABCUI_SET_STATE_DIRTY:
    case ABCUI_SET_STATE_CONFLICT:
    case ABCUI_SET_STATE_QUEUED:
    case ABCUI_SET_STATE_APPLY_FAILED:
      keyDirty = true;
      break;
    default:
      break;
    }
  });
  std::string text;
  if (!Api->settings->bindReadsAvailable() && keyRows == 0) {
    text = resolve("#abcui.gameui/Settings_BindUnavailable");
  } else if (Api->settings->bindConflictPending()) {
    text = resolve("#abcui.gameui/Settings_BindConflictSummary");
  } else if (!Api->settings->bindDirty() && !keyDirty) {
    text = resolve("#abcui.gameui/Settings_BindClean");
  } else {
    text = resolve("#abcui.gameui/Settings_BindDirtySummary");
  }
  setInnerRml(node("bind-status"), escapeText(text));
}

} // namespace settingspage
