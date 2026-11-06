#pragma once

// services::creategame::ServerSettings - the settings.scr SERVER_OPTIONS
// document (the file the shipped GameUI reads on open and rewrites on apply:
// its header says "stores last settings chosen, too").
//
// The page's form is a projection of this document: the four typed fields are
// the hostname / maxplayers / sv_password / sv_lan rows and every other row is
// a server option. Parsing keeps the row order, prompt, type, bounds and list
// choices so the write-back is the same file the original dialog writes; the
// write path matches its formatting (CRLF, six-decimal NUMBER/LIST values,
// integer BOOL values, quotes and '%' stripped from written strings).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace services {
namespace creategame {

enum class ServerSettingKind { kBool, kNumber, kList, kText };

struct ServerSettingChoice {
  std::string Label;
  std::string Value;
};

struct ServerSettingRow {
  std::string Cvar;
  std::string Prompt;
  ServerSettingKind Kind = ServerSettingKind::kText;
  std::string Default;
  float Min = 0.0f;
  float Max = 0.0f;
  std::vector<ServerSettingChoice> Choices;
};

// The four typed create-game fields as the shipped dialog stores them.
struct SavedServerSettings {
  std::string Hostname;
  std::string Password;
  bool Lan = false;
  int MaxPlayers = 8;
};

class ServerSettingsDocument {
public:
  // The shipped settings.scr has 24 rows; the cap only guards malformed or
  // hostile files (the page's own option list is capped separately).
  static constexpr std::size_t kMaxRows = 256;

  // Parses the first DESCRIPTION block. An empty default ("") is kept: the
  // shipped sv_password row depends on it. False on malformed input.
  static bool parse(const std::string &text, ServerSettingsDocument &out);

  // The timestamp the shipped dialog writes after "// File generated:".
  static std::string generateTimestamp();

  bool valid() const {
    return !Rows.empty();
  }
  const std::vector<ServerSettingRow> &rows() const {
    return Rows;
  }

  const ServerSettingRow *find(std::string_view cvar) const;
  ServerSettingRow *find(std::string_view cvar);

  // Updates one row's default. BOOL is normalized to "0"/"1"; NUMBER/LIST to
  // six decimals (the shipped writer's %f); STRING is written verbatim after
  // the shipped quote/'%' strip. False when the row does not exist or a
  // numeric value does not parse.
  bool setValue(std::string_view cvar, std::string_view value);

  // The four typed create-game fields. Returns false when at least one row is
  // missing, but still updates the rows that exist.
  bool apply(const std::string &hostname, const std::string &password, bool lan,
             int maxPlayers);

  // Reads the stored values back. Missing/invalid rows keep the defaults
  // (empty name/password, Lan false, MaxPlayers 8).
  SavedServerSettings saved() const;

  // Rebuilds the whole file in the shipped format. Empty when no rows parsed.
  // `generatedAt` is the text after "// File generated:  "; empty means now.
  std::string serialize(std::string_view generatedAt) const;

private:
  std::vector<ServerSettingRow> Rows;
};

} // namespace creategame
} // namespace services
