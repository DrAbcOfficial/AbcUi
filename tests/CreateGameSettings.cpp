// abc_ui_create_game_settings - the settings.scr SERVER_OPTIONS document the
// create-game page projects and writes back. Pure parser/serializer: no game
// installation, no engine filesystem, no RmlUi. Pins:
//   - the shipped grammar (BOOL/NUMBER/LIST/STRING, empty STRING defaults,
//     comments and arbitrary whitespace);
//   - the four typed rows the shipped GameplayPage persists;
//   - the writer's normalization (BOOL "0"/"1", NUMBER/LIST %f, quote/'%'
//     strip) and the header the shipped ApplyChanges regenerates;
//   - a serialize -> parse round trip that keeps every row and choice.
#include "../src/services/creategame/ServerSettings.h"

#include "framework/TestHarness.h"

#include <string>

namespace {

using services::creategame::ServerSettingsDocument;

constexpr const char *kSample = R"scr(
// ignored comments
VERSION 1.0

DESCRIPTION SERVER_OPTIONS
{
	"sv_lan"
	{
		"LAN Game"
		{ BOOL }
		{ "0" }
	}

	"hostname"
	{
		"#Valve_Hostname"
		{ STRING }
		{ "Sven Co-op listen server" }
	}

	"maxplayers"
	{
		"#Valve_Max_Players"
		{ NUMBER 1.000000 32.000000 }
		{ "8.000000" }
	}

	"sv_password"
	{
		"#Valve_Server_Password"
		{ STRING }
		{ "" }
	}

	"mp_friendlyfire"
	{
		"Friendly Fire"
		{ BOOL }
		{ "0" }
	}

	"mp_timelimit"
	{
		"Time Limit"
		{ NUMBER 0.000000 120.000000 }
		{ "30.000000" }
	}

	"mp_telefrag"
	{
		"Telefrag"
		{ NUMBER -1.000000 -1.000000 }
		{ "1.000000" }
	}

	"sv_region"
	{
		"Server Location (Steam)"
		{
			LIST
			"0: US East coast" "0"
			"1: US West coast" "1"
		}
		{ "0.000000" }
	}
}
)scr";

void testParseAndSaved() {
  TEST_CASE("parse-and-saved");
  ServerSettingsDocument document;
  CHECK(ServerSettingsDocument::parse(kSample, document));
  CHECK(document.rows().size() == 8);
  const auto *telefrag = document.find("mp_telefrag");
  CHECK(telefrag != nullptr);
  CHECK(telefrag && telefrag->Min == -1.0f);
  CHECK(telefrag && telefrag->Max == -1.0f);

  const auto saved = document.saved();
  CHECK(saved.Hostname == "Sven Co-op listen server");
  CHECK(saved.Password.empty());
  CHECK(!saved.Lan);
  CHECK(saved.MaxPlayers == 8);

  const auto *password = document.find("sv_password");
  CHECK(password != nullptr);
  CHECK(password->Kind ==
        services::creategame::ServerSettingKind::kText);
  CHECK(password->Default.empty());

  const auto *region = document.find("sv_region");
  CHECK(region != nullptr);
  CHECK(region->Choices.size() == 2);
  CHECK(region->Choices[1].Value == "1");
}

void testSetValueNormalization() {
  TEST_CASE("set-value-normalization");
  ServerSettingsDocument document;
  CHECK(ServerSettingsDocument::parse(kSample, document));

  CHECK(document.setValue("hostname", "100% Fun \"Clan\""));
  CHECK(document.setValue("sv_lan", "1"));
  CHECK(document.setValue("maxplayers", "12"));
  CHECK(document.setValue("sv_password", "secret"));

  const auto saved = document.saved();
  CHECK(saved.Hostname == "100 Fun Clan");
  CHECK(saved.Lan);
  CHECK(saved.MaxPlayers == 12);
  CHECK(saved.Password == "secret");

  const auto *maxplayers = document.find("maxplayers");
  CHECK(maxplayers != nullptr);
  CHECK(maxplayers && maxplayers->Default == "12.000000");
  const auto *lan = document.find("sv_lan");
  CHECK(lan != nullptr);
  CHECK(lan && lan->Default == "1");
  CHECK(!document.setValue("missing", "1"));
  CHECK(!document.setValue("mp_timelimit", "not-a-number"));
}

void testApplyAndSerialize() {
  TEST_CASE("apply-and-serialize");
  ServerSettingsDocument document;
  CHECK(ServerSettingsDocument::parse(kSample, document));
  CHECK(document.apply("My Server", "pw", true, 16));
  CHECK(document.setValue("mp_friendlyfire", "1"));

  const std::string text = document.serialize("Sun Jan 01 12:00:00 PM");
  CHECK(!text.empty());
  CHECK(text.find("// File generated:  Sun Jan 01 12:00:00 PM\r\n") !=
        std::string::npos);
  CHECK(text.find("// Sven Co-op Server Configuration Layout Script (stores "
                  "last settings chosen, too)\r\n") != std::string::npos);
  // The shipped file has an older NOTE block; the original Save (wb + this
  // header) drops it, so the writer must not reproduce it.
  CHECK(text.find("// NOTE:") == std::string::npos);
  CHECK(text.find("VERSION 1.0\r\n") != std::string::npos);
  CHECK(text.find("DESCRIPTION SERVER_OPTIONS\r\n{\r\n") !=
        std::string::npos);
  CHECK(text.find("\t\"hostname\"\r\n\t{\r\n\t\t\"#Valve_Hostname\""
                  "\r\n\t\t{ STRING }\r\n\t\t{ \"My Server\" }\r\n"
                  "\t}\r\n") != std::string::npos);
  CHECK(text.find("\t\t{ \"1\" }\r\n") != std::string::npos);
  CHECK(text.find("\t\t{ \"16.000000\" }\r\n") != std::string::npos);
  CHECK(text.find("\t\t\tLIST\r\n") != std::string::npos);
  CHECK(text.find("\t\t\t\"1: US West coast\" \"1\"\r\n") !=
        std::string::npos);

  ServerSettingsDocument round;
  CHECK(ServerSettingsDocument::parse(text, round));
  CHECK(round.rows().size() == document.rows().size());
  const auto saved = round.saved();
  CHECK(saved.Hostname == "My Server");
  CHECK(saved.Password == "pw");
  CHECK(saved.Lan);
  CHECK(saved.MaxPlayers == 16);
  const auto *roundFire = round.find("mp_friendlyfire");
  CHECK(roundFire != nullptr);
  CHECK(roundFire && roundFire->Default == "1");
}

void testMalformedInput() {
  TEST_CASE("malformed-input");
  ServerSettingsDocument document;
  CHECK(!ServerSettingsDocument::parse("DESCRIPTION SERVER_OPTIONS { \"x\"",
                                       document));
  CHECK(!document.valid());
  CHECK(document.serialize("x").empty());

  ServerSettingsDocument empty;
  CHECK(empty.serialize("x").empty());
}

} // namespace

int main() {
  testParseAndSaved();
  testSetValueNormalization();
  testApplyAndSerialize();
  testMalformedInput();
  return abctest::finish("abc_ui_create_game_settings");
}
