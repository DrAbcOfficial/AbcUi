// abc_ui_create_game_settings - the create-game server option document
// (server_options.json) and the saved values AbcUi keeps in config.json's
// create_game section. Pure JSON parsers/serializer: no game installation, no
// engine filesystem, no RmlUi. Pins:
//   - the packaged document's rows (bool / number / list plus the four typed
//     defaults), the unbounded NUMBER -1 -1 spelling and the list choices;
//   - the strict shape (the four typed rows, unique mp_/sv_ names, a default
//     inside its domain, no unknown fields);
//   - the value gate every submit and every loaded saved value passes;
//   - the saved section's round trip and its stale-value tolerance;
//   - the shipped server_options.json itself parses.
#include "../src/services/creategame/ServerOptions.h"
#include "../src/services/creategame/ServerSettings.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "framework/TestEnvironment.h"
#include "framework/TestHarness.h"

namespace {

using services::creategame::parseServerOptionDocument;
using services::creategame::SavedServerSettings;
using services::creategame::ServerOption;
using services::creategame::ServerOptionDocument;
using services::creategame::ServerOptionValue;
using services::creategame::ServerSettingsDocument;
using services::creategame::validateServerOptions;
using services::creategame::validateServerOptionValue;

constexpr const char *kSample = R"json({
  "version": 1,
  "items": [
    { "cvar": "sv_lan", "label": "LAN Game", "kind": "bool", "default": 0 },
    { "cvar": "hostname", "label": "#Valve_Hostname", "kind": "text",
      "default": "Sven Co-op listen server" },
    { "cvar": "maxplayers", "label": "#Valve_Max_Players", "kind": "number",
      "min": 1, "max": 32, "default": 8 },
    { "cvar": "sv_password", "label": "#Valve_Server_Password",
      "kind": "text", "default": "" },
    { "cvar": "mp_friendlyfire", "label": "Friendly Fire", "kind": "bool",
      "default": 0 },
    { "cvar": "mp_timelimit", "label": "Time Limit", "kind": "number",
      "min": 0, "max": 120, "default": 30 },
    { "cvar": "mp_telefrag", "label": "Telefrag", "kind": "number",
      "min": -1, "max": -1, "default": 1 },
    { "cvar": "sv_region", "label": "Server Location (Steam)",
      "kind": "list", "default": 0,
      "options": [
        { "value": 0, "label": "0: US East coast" },
        { "value": 1, "label": "1: US West coast" }
      ] }
  ]
})json";

std::string readFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

bool parses(const std::string &text, ServerOptionDocument &out,
            std::string &error) {
  return parseServerOptionDocument(text, out, error);
}

std::string documentWith(const std::string &item) {
  return R"({"version": 1, "items": [)" + item +
         R"(, {"cvar": "sv_lan", "label": "L", "kind": "bool", "default": 0},
            {"cvar": "hostname", "label": "H", "kind": "text", "default": "n"},
            {"cvar": "maxplayers", "label": "M", "kind": "number",
             "min": 1, "max": 32, "default": 8},
            {"cvar": "sv_password", "label": "P", "kind": "text",
             "default": ""}]})";
}

const ServerOption *find(const std::vector<ServerOption> &options,
                         const char *name) {
  for (const auto &option : options)
    if (option.Name == name)
      return &option;
  return nullptr;
}

} // namespace

int main() {
  // -- the document shapes the page ---------------------------------------
  {
    TEST_CASE("parse-document");
    ServerOptionDocument document;
    std::string error;
    CHECK(parses(kSample, document, error));
    CHECK(error.empty());
    CHECK(document.Options.size() == 4);
    CHECK(document.Defaults.Hostname == "Sven Co-op listen server");
    CHECK(document.Defaults.Password.empty());
    CHECK(!document.Defaults.Lan);
    CHECK(document.Defaults.MaxPlayers == 8);

    const auto *timelimit = find(document.Options, "mp_timelimit");
    CHECK(timelimit != nullptr);
    CHECK(timelimit &&
          timelimit->Kind == services::creategame::ServerSettingKind::kNumber);
    CHECK(timelimit && timelimit->Min == 0.0f);
    CHECK(timelimit && timelimit->Max == 120.0f);
    CHECK(timelimit && timelimit->Default == 30.0f);
    CHECK(timelimit && timelimit->Bounded);

    // The shipped unbounded spelling: NUMBER -1 -1 refuses no value.
    const auto *telefrag = find(document.Options, "mp_telefrag");
    CHECK(telefrag != nullptr);
    CHECK(telefrag && !telefrag->Bounded);
    CHECK(telefrag && validateServerOptionValue(*telefrag, "5000"));

    const auto *region = find(document.Options, "sv_region");
    CHECK(region != nullptr);
    CHECK(region &&
          region->Kind == services::creategame::ServerSettingKind::kList);
    CHECK(region && region->Choices.size() == 2);
    CHECK(region && region->Choices[1].Value == 1.0f);
    CHECK(region && region->Choices[1].Label == "1: US West coast");
  }

  // -- a malformed or out-of-domain document is refused -------------------
  {
    TEST_CASE("document-rejects");
    ServerOptionDocument document;
    std::string error;

    // The four typed rows seed the form and must exist.
    CHECK(!parses(R"({"version": 1, "items": [
        {"cvar": "mp_friendlyfire", "label": "F", "kind": "bool",
         "default": 0}]})",
                  document, error));
    CHECK(error.find("$.items") != std::string::npos);

    // Duplicate cvars, unknown kinds, a text row that is not typed, a name
    // outside the identifier grammar and a list default outside the choices
    // are all refused.
    CHECK(!parses(documentWith(R"({"cvar": "mp_friendlyfire", "label": "F",
                                  "kind": "bool", "default": 0},
                                 {"cvar": "mp_friendlyfire", "label": "F",
                                  "kind": "bool", "default": 0})"),
                  document, error));
    CHECK(!parses(documentWith(R"({"cvar": "mp_friendlyfire", "label": "F",
                                  "kind": "toggle", "default": 0})"),
                  document, error));
    CHECK(!parses(documentWith(R"({"cvar": "mp_note", "label": "F",
                                  "kind": "text", "default": "x"})"),
                  document, error));
    CHECK(!parses(documentWith(R"({"cvar": "FriendlyFire", "label": "F",
                                  "kind": "bool", "default": 0})"),
                  document, error));
    CHECK(!parses(documentWith(R"({"cvar": "mp_friendlyfire", "label": "F",
                                  "kind": "number", "min": 1, "max": 2,
                                  "default": 3})"),
                  document, error));
    CHECK(!parses(documentWith(R"({"cvar": "sv_region", "label": "R",
                                  "kind": "list", "default": 9,
                                  "options": [{"value": 0, "label": "a"}]})"),
                  document, error));
    CHECK(!parses(documentWith(R"({"cvar": "mp_friendlyfire", "label": "F",
                                  "kind": "bool", "default": 0,
                                  "extra": 1})"),
                  document, error));
    CHECK(!parses("{ not json", document, error));
  }

  // -- the value gate used by submit and by loaded saved values ------------
  {
    TEST_CASE("value-gate");
    ServerOptionDocument document;
    std::string error;
    CHECK(parses(kSample, document, error));

    const auto *toggle = find(document.Options, "mp_friendlyfire");
    const auto *timelimit = find(document.Options, "mp_timelimit");
    const auto *region = find(document.Options, "sv_region");
    CHECK(toggle && timelimit && region);
    if (toggle && timelimit && region) {
      CHECK(validateServerOptionValue(*toggle, "0"));
      CHECK(validateServerOptionValue(*toggle, "1"));
      CHECK(!validateServerOptionValue(*toggle, "2"));
      CHECK(!validateServerOptionValue(*toggle, "yes"));
      CHECK(validateServerOptionValue(*timelimit, "120"));
      CHECK(!validateServerOptionValue(*timelimit, "121"));
      CHECK(validateServerOptionValue(*region, "1"));
      CHECK(!validateServerOptionValue(*region, "2"));

      // The page submits exactly one value per rendered option.
      std::vector<ServerOptionValue> values = {
          {"mp_friendlyfire", "1"},
          {"mp_timelimit", "45"},
          {"mp_telefrag", "-3"},
          {"sv_region", "1"},
      };
      CHECK(validateServerOptions(document.Options, values));
      values.pop_back();
      CHECK(!validateServerOptions(document.Options, values));
      values.push_back({"sv_region", "1"});
      values[0].Name = "mp_unknown";
      CHECK(!validateServerOptions(document.Options, values));
    }
  }

  // -- the shipped document itself ----------------------------------------
  {
    TEST_CASE("shipped-document");
    const std::string text =
        readFile(abctest::sourceDir() /
                 "modules/gameui/assets/gameui/config/server_options.json");
    CHECK(!text.empty());
    ServerOptionDocument document;
    std::string error;
    CHECK(parses(text, document, error));
    // 24 rows in the shipped file: the four typed rows plus 20 options.
    CHECK(document.Options.size() == 20);
    CHECK(document.Defaults.MaxPlayers == 8);
    CHECK(!document.Defaults.Lan);
    const auto *region = find(document.Options, "sv_region");
    CHECK(region && region->Choices.size() == 9);
    CHECK(region && region->Default == 255.0f);

    // Every "#Token" label has a row in the module's English table; a
    // missing key would show the raw token on the page.
    std::set<std::string> keys;
    std::ifstream lang(abctest::sourceDir() /
                       "modules/gameui/assets/lang/english.txt");
    for (std::string line; std::getline(lang, line);) {
      const auto equal = line.find('=');
      if (equal == std::string::npos || equal == 0 || line.empty() ||
          line[0] == '#')
        continue;
      const auto first = line.find_first_not_of(" \t\r");
      const auto last = line.find_last_not_of(" \t\r", equal - 1);
      if (first != std::string::npos && last >= first)
        keys.insert(line.substr(first, last - first + 1));
    }
    const auto resolved = [&](const std::string &label) {
      if (label.empty() || label[0] != '#')
        return true;
      const std::string token = label.substr(1);
      const std::string key = token.find('/') == std::string::npos
                                  ? "abcui.gameui/" + token
                                  : token;
      return keys.count(key) != 0;
    };
    for (const ServerOption &option : document.Options) {
      CHECK(resolved(option.Label));
      for (const auto &choice : option.Choices)
        CHECK(resolved(choice.Label));
    }
  }

  // -- the saved section round trip ---------------------------------------
  {
    TEST_CASE("saved-round-trip");
    ServerOptionDocument definitions;
    std::string error;
    CHECK(parses(kSample, definitions, error));

    ServerSettingsDocument document;
    document.setSaved(definitions.Defaults);
    document.apply("My Server", "pw", true, 16);
    document.setValue("mp_timelimit", "45");
    document.setValue("sv_region", "1");

    std::string json;
    CHECK(document.serialize(json, error));
    CHECK(json.find("\"hostname\" : \"My Server\"") != std::string::npos ||
          json.find("\"hostname\": \"My Server\"") != std::string::npos);
    CHECK(json.find("\"password\"") != std::string::npos);
    CHECK(json.find("\"maxplayers\"") != std::string::npos);

    ServerSettingsDocument round;
    round.setSaved(definitions.Defaults);
    CHECK(ServerSettingsDocument::parse(json, round, error));
    const auto saved = round.saved();
    CHECK(saved.Hostname == "My Server");
    CHECK(saved.Password == "pw");
    CHECK(saved.Lan);
    CHECK(saved.MaxPlayers == 16);
    CHECK(saved.Options.size() == 2);

    // An unmodified document keeps the definition defaults.
    const auto defaults = definitions.Defaults;
    CHECK(defaults.Hostname == "Sven Co-op listen server");
  }

  // -- the saved section tolerates stale values ---------------------------
  {
    TEST_CASE("saved-tolerance");
    ServerOptionDocument definitions;
    std::string error;
    CHECK(parses(kSample, definitions, error));

    ServerSettingsDocument document;
    document.setSaved(definitions.Defaults);
    CHECK(ServerSettingsDocument::parse(
        R"({"version": 1, "maxplayers": 999, "hostname": ")" +
            std::string(80, 'a') +
            R"(", "lan": true, "options": {"sv_region": "1"}})",
        document, error));
    const auto saved = document.saved();
    // Out-of-range values fall back to the packaged defaults.
    CHECK(saved.MaxPlayers == 32);
    CHECK(saved.Hostname == "Sven Co-op listen server");
    CHECK(saved.Lan);
    CHECK(saved.Options.size() == 1);

    // Wrong shape still refuses (and leaves the document untouched).
    ServerSettingsDocument damaged;
    damaged.setSaved(definitions.Defaults);
    CHECK(!ServerSettingsDocument::parse(R"({"version": 1, "unknown": true})",
                                         damaged, error));
    CHECK(damaged.saved().Hostname == "Sven Co-op listen server");
    CHECK(!ServerSettingsDocument::parse(R"({"version": 1, "lan": 1})", damaged,
                                         error));
    CHECK(!ServerSettingsDocument::parse(
        R"({"version": 1, "options": {"a": "1", "a": "2"}})", damaged, error));
    CHECK(!ServerSettingsDocument::parse(R"({"version": 2})", damaged, error));
  }

  return abctest::finish("abc_ui_create_game_settings");
}
