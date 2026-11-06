// abc_ui_settings_key_row - the key-code row (the vote answer keys) in the
// JSON page tree and the settings model.
//
// The key row stays a plain float cvar row in the transaction model: the
// schema's "key" flag only changes how the page draws and edits it (the key
// name plus a capture button). This pins the schema rule (float rows only,
// flag propagated into the table), the draft/apply path that persists the
// captured key number, and the fail-closed state for an unregistered cvar.
#include "services/settings/SettingsModel.h"
#include "services/uiconfig/UiDefinitions.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "framework/TestEnvironment.h"
#include "framework/TestHarness.h"

namespace {

using services::settings::RowState;
using services::settings::SettingKind;
using services::settings::SettingsModel;
using services::uiconfig::ConfigError;
using services::uiconfig::DefinitionParser;
using services::uiconfig::SettingsPageDefinition;

constexpr const char *kKeyYes = "hud_votekey_yes";

const char *kKeyPage = R"({
  "version": 1,
  "id": "keyboard",
  "label": "#abcui.gameui/Settings_PageKeyboard",
  "order": 20,
  "sections": [
    {"id": "vote-keys", "label": "#abcui.gameui/Settings_SecVoteKeys"}
  ],
  "items": [
    {
      "cvar": "hud_votekey_yes",
      "label": "#abcui.gameui/Settings_ABC_hud_votekey_yes",
      "section": "vote-keys",
      "kind": "float",
      "key": true,
      "default": 92.0
    }
  ]
})";

// The schema rule: the "key" flag is float-only.
const char *kBadKeyPage = R"({
  "version": 1,
  "id": "keyboard",
  "label": "#abcui.gameui/Settings_PageKeyboard",
  "order": 20,
  "sections": [
    {"id": "vote-keys", "label": "#abcui.gameui/Settings_SecVoteKeys"}
  ],
  "items": [
    {
      "cvar": "hud_votekey_yes",
      "label": "#abcui.gameui/Settings_ABC_hud_votekey_yes",
      "section": "vote-keys",
      "kind": "bool",
      "key": true,
      "default": 1.0
    }
  ]
})";

// In-memory cvar table behind the model seams (the same shape the production
// composition root injects).
struct FakeCvars {
  std::map<std::string, float> values;
  int writes = 0;
  static FakeCvars *Current;

  static bool exists(const char *name) {
    return Current && name && Current->values.count(name) != 0;
  }
  static float read(const char *name) {
    if (!Current || !name)
      return 0.0f;
    const auto found = Current->values.find(name);
    return found == Current->values.end() ? 0.0f : found->second;
  }
  static bool write(const char *name, float value) {
    if (!Current || !name)
      return false;
    const auto found = Current->values.find(name);
    if (found == Current->values.end())
      return false;
    found->second = value;
    ++Current->writes;
    return true;
  }
};

FakeCvars *FakeCvars::Current = nullptr;

// The shipped module page tree (the settings page reads exactly these
// files); a page that fails to parse silently disappears at runtime, so the
// hermetic suite pins it.
std::string readFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

std::filesystem::path shippedSettingsDir() {
  return abctest::sourceDir() / "modules/gameui/assets/gameui/config/settings";
}

std::string readShipped(const char *relative) {
  return readFile(shippedSettingsDir() / relative);
}

SettingsModel::Seams fakeSeams() {
  SettingsModel::Seams seams;
  seams.exists = &FakeCvars::exists;
  seams.readFloat = &FakeCvars::read;
  seams.writeFloat = &FakeCvars::write;
  return seams;
}

} // namespace

int main() {
  // -- the schema parses the key flag onto the row --------------------------
  {
    TEST_CASE("schema-key-row");
    SettingsPageDefinition page;
    ConfigError error;
    CHECK(DefinitionParser::parsePage(kKeyPage, page, error));
    CHECK(error.Field.empty());
    CHECK(page.Items.size() == 1);
    CHECK(page.Items[0].Cvar == kKeyYes);
    CHECK(page.Items[0].Kind == SettingKind::kFloat);
    CHECK(page.Items[0].KeyCodeRow);
    // A key number has no numeric domain: the row must stay unbounded so the
    // captured value is never clamped against min/max.
    CHECK(!page.Items[0].Bounded);
    CHECK_NEAR(page.Items[0].DefaultValue, 92.0, 1e-6);
  }

  // -- the flag is float-only ----------------------------------------------
  {
    TEST_CASE("schema-key-requires-float");
    SettingsPageDefinition page;
    ConfigError error;
    CHECK(!DefinitionParser::parsePage(kBadKeyPage, page, error));
    CHECK(error.Field == "$.items[0].key");
  }

  // -- the shipped keyboard page owns the vote keys ------------------------
  {
    TEST_CASE("shipped-input-page");
    const std::string text = readShipped("input.json");
    CHECK(!text.empty());
    SettingsPageDefinition page;
    ConfigError error;
    CHECK(DefinitionParser::parsePage(text, page, error));
    CHECK(page.Id == "keyboard");
    CHECK(page.Items.size() == 2);
    const services::uiconfig::SectionDefinition *section =
        page.section("vote-keys");
    CHECK(section != nullptr);
    CHECK(section && section->Label == "#abcui.gameui/Settings_SecVoteKeys");
    for (const auto &item : page.Items) {
      CHECK(item.KeyCodeRow);
      CHECK(item.Section == "vote-keys");
      CHECK(item.Kind == SettingKind::kFloat);
    }
    CHECK(page.Items[0].Cvar == "hud_votekey_yes");
    CHECK(page.Items[1].Cvar == "hud_votekey_no");
  }

  // -- the ABC page no longer carries them ---------------------------------
  {
    TEST_CASE("shipped-abc-page");
    const std::string text = readShipped("general.json");
    CHECK(!text.empty());
    SettingsPageDefinition page;
    ConfigError error;
    CHECK(DefinitionParser::parsePage(text, page, error));
    for (const auto &item : page.Items) {
      CHECK(item.Cvar != "hud_votekey_yes");
      CHECK(item.Cvar != "hud_votekey_no");
    }
    // The enable toggle stays where it belongs (the vote panel itself).
    bool voteToggle = false;
    for (const auto &item : page.Items)
      voteToggle |= item.Cvar == "hud_vote";
    CHECK(voteToggle);
  }

  // -- every shipped page still parses ------------------------------------
  {
    TEST_CASE("shipped-pages-parse");
    int pages = 0;
    std::error_code error;
    for (const auto &entry :
         std::filesystem::directory_iterator(shippedSettingsDir(), error)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json")
        continue;
      SettingsPageDefinition page;
      ConfigError pageError;
      const std::string text = readFile(entry.path());
      CHECK(!text.empty());
      CHECK(DefinitionParser::parsePage(text, page, pageError));
      CHECK(!page.Id.empty());
      ++pages;
    }
    CHECK(!error);
    CHECK(pages >= 10);
  }

  // -- a key row is a plain float row for the transaction model ------------
  {
    TEST_CASE("model-key-draft-apply");
    FakeCvars cvars;
    cvars.values[kKeyYes] = 92.0f;
    FakeCvars::Current = &cvars;
    SettingsModel model;
    services::settings::SettingRow row{};
    row.Cvar = kKeyYes;
    row.Kind = SettingKind::kFloat;
    row.Bounded = false;
    row.HasDefault = true;
    row.DefaultValue = 92.0f;
    row.LabelToken = "#abcui.gameui/Settings_ABC_hud_votekey_yes";
    row.Section = "vote-keys";
    row.KeyCodeRow = true;
    model.setTable(&row, 1);
    model.setSeams(fakeSeams());
    model.snapshot();
    const auto initial = model.find(kKeyYes);
    CHECK(initial && initial->state == RowState::kClean);
    CHECK(initial && initial->row && initial->row->KeyCodeRow);
    CHECK_NEAR(initial ? initial->draftValue : 0.0f, 92.0, 1e-6);

    // The captured key number (93 = the default "no" key) lands as a draft:
    // nothing is written before Apply.
    CHECK(model.setDraftText(kKeyYes, "93"));
    CHECK(cvars.writes == 0);
    const auto drafted = model.find(kKeyYes);
    CHECK(drafted && drafted->state == RowState::kDirty);
    CHECK_NEAR(drafted ? drafted->draftValue : 0.0f, 93.0, 1e-6);

    const auto results = model.apply();
    CHECK(results.size() == 1);
    CHECK(cvars.writes == 1);
    CHECK_NEAR(cvars.values[kKeyYes], 93.0, 1e-6);
    model.snapshot();
    const auto settled = model.find(kKeyYes);
    CHECK(settled && settled->state == RowState::kClean);
    CHECK(!model.isDirty());
  }

  // -- an unregistered key cvar fails closed -------------------------------
  {
    TEST_CASE("model-key-missing");
    FakeCvars cvars; // hud_votekey_yes never registered (vote module absent)
    FakeCvars::Current = &cvars;
    SettingsModel model;
    services::settings::SettingRow row{};
    row.Cvar = kKeyYes;
    row.Kind = SettingKind::kFloat;
    row.Bounded = false;
    row.KeyCodeRow = true;
    model.setTable(&row, 1);
    model.setSeams(fakeSeams());
    model.snapshot();
    const auto view = model.find(kKeyYes);
    CHECK(view && view->missing);
    CHECK(view && view->state == RowState::kMissing);
    CHECK(!model.editable(kKeyYes));
    CHECK(!model.setDraftText(kKeyYes, "92"));
    CHECK(model.apply().empty());
    CHECK(cvars.writes == 0);
  }

  FakeCvars::Current = nullptr;
  return abctest::finish("abc_ui_settings_key_row");
}
