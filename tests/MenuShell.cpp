// abc_ui_menu_shell - the shell's menu definition (uiconfig's mainmenu.json):
// the fixed intents plus the "command" entries that carry their own console
// line.
//
// The framework parses the file, projects every entry through the sdk
// menuconfig section and the shell hands the fixed intents to the menu
// section. This pins the one definition-owned intent: "command" requires its
// `command` field, the field is one printable bounded line (the engine
// command buffer would otherwise run a second line), and a fixed intent
// refuses one.
#include "services/uiconfig/UiDefinitions.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "framework/TestEnvironment.h"
#include "framework/TestHarness.h"

namespace {

using services::uiconfig::ConfigError;
using services::uiconfig::DefinitionParser;
using services::uiconfig::MenuDefinition;

// The shipped quit/resume rows the parser requires, plus one command entry.
std::string menuWithMainEntry(const std::string &entry) {
  return R"({"version": 1, "title": "#abcui.gameui/MainMenu_Title",
  "main": [)" +
         entry + R"(,
    {"id": "main-quit", "label": "#abcui.gameui/MainMenu_Quit",
     "action": "quit"}],
  "pause": [
    {"id": "pause-resume", "label": "#abcui.gameui/MainMenu_Resume",
     "action": "resume"},
    {"id": "pause-quit", "label": "#abcui.gameui/MainMenu_Quit",
     "action": "quit"}]})";
}

bool parses(const std::string &text, MenuDefinition &menu, ConfigError &error) {
  return DefinitionParser::parseMenu(text, menu, error);
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

} // namespace

int main() {
  // -- the shipped definition still parses and runs no command --------------
  {
    TEST_CASE("shipped-mainmenu");
    const std::string text =
        readFile(abctest::sourceDir() /
                 "modules/gameui/assets/gameui/config/mainmenu.json");
    CHECK(!text.empty());
    MenuDefinition menu;
    ConfigError error;
    CHECK(parses(text, menu, error));
    CHECK(menu.Title == "#abcui.gameui/MainMenu_Title");
    CHECK(!menu.Main.empty());
    CHECK(!menu.Pause.empty());
    for (const auto &entry : menu.Main)
      CHECK(entry.Command.empty());
    for (const auto &entry : menu.Pause)
      CHECK(entry.Command.empty());
    bool pauseServers = false;
    for (const auto &entry : menu.Pause)
      pauseServers |= entry.Action == "servers";
    CHECK(pauseServers);
  }

  // -- a command entry keeps its own console line ---------------------------
  {
    TEST_CASE("command-entry");
    MenuDefinition menu;
    ConfigError error;
    CHECK(parses(menuWithMainEntry(
                     R"({"id": "main-join", "label": "#join",
                         "action": "command",
                         "command": "connect 192.0.2.10:27015"})"),
                 menu, error));
    CHECK(menu.Main.size() == 2);
    CHECK(menu.Main[0].Action == "command");
    CHECK(menu.Main[0].Command == "connect 192.0.2.10:27015");
    CHECK(menu.Main[0].Condition == "always");
    CHECK(menu.Main[1].Command.empty());
    // A condition still restricts when the row is available.
    MenuDefinition gated;
    CHECK(parses(menuWithMainEntry(
                     R"({"id": "main-join", "label": "#join",
                         "action": "command", "command": "retry",
                         "condition": "multiplayer"})"),
                 gated, error));
    CHECK(gated.Main[0].Condition == "multiplayer");
    CHECK(gated.Main[0].Command == "retry");
  }

  // -- the command action requires its line --------------------------------
  {
    TEST_CASE("command-required");
    MenuDefinition menu;
    ConfigError error;
    CHECK(!parses(menuWithMainEntry(
                      R"({"id": "main-join", "label": "#join",
                          "action": "command"})"),
                  menu, error));
    CHECK(error.Field == "$.main[0].command");
    CHECK(!error.Message.empty());
  }

  // -- a fixed intent never carries one ------------------------------------
  {
    TEST_CASE("command-refused-on-intent");
    MenuDefinition menu;
    ConfigError error;
    CHECK(!parses(menuWithMainEntry(
                      R"({"id": "main-servers", "label": "#servers",
                          "action": "servers", "command": "connect 1.2.3.4"})"),
                  menu, error));
    CHECK(error.Field == "$.main[0].command");
  }

  // -- one printable line: newlines and control bytes are refused ----------
  {
    TEST_CASE("command-single-line");
    // The literals are the JSON spellings: the parsed value carries the
    // control character (a raw one in the file would already be a JSON
    // syntax error).
    for (const char *bad :
         {R"(connect a\nquit)", R"(connect a\tquit)", R"(connect a\u0007quit)",
          R"(connect a\u007fquit)"}) {
      MenuDefinition menu;
      ConfigError error;
      CHECK(!parses(
          menuWithMainEntry(std::string(R"({"id": "main-join", "label": "#j",
                                       "action": "command", "command": ")") +
                            bad + "\"}"),
          menu, error));
      CHECK(error.Field == "$.main[0].command");
    }
  }

  // -- the ABI bound (256 bytes) is the parser bound -----------------------
  {
    TEST_CASE("command-length");
    MenuDefinition menu;
    ConfigError error;
    CHECK(parses(menuWithMainEntry(
                     R"({"id": "main-join", "label": "#j",
                         "action": "command", "command": ")" +
                     std::string(256, 'a') + "\"}"),
                 menu, error));
    CHECK(menu.Main[0].Command.size() == 256);
    CHECK(!parses(menuWithMainEntry(
                      R"({"id": "main-join", "label": "#j",
                          "action": "command", "command": ")" +
                      std::string(257, 'a') + "\"}"),
                  menu, error));
    CHECK(error.Field == "$.main[0].command");
  }

  // -- an invented action is still refused --------------------------------
  {
    TEST_CASE("unknown-action");
    MenuDefinition menu;
    ConfigError error;
    CHECK(!parses(menuWithMainEntry(
                      R"({"id": "main-join", "label": "#j",
                          "action": "joinserver", "command": "connect x"})"),
                  menu, error));
    CHECK(error.Field == "$.main[0].action");
  }

  return abctest::finish("abc_ui_menu_shell");
}
