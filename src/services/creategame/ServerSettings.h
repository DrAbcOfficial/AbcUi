#pragma once

// services::creategame::ServerSettings - the create-game values AbcUi keeps in
// abc/abcui/config.json under the `create_game` section: the four typed fields
// (hostname / maxplayers / sv_password / sv_lan) and the last applied option
// values. The packaged server_options.json owns the rows and their defaults;
// this document is only the saved state on top of them.
//
// Parsing is strict about the document shape (unknown fields, wrong types and
// duplicate option names refuse) and tolerant about stale values: a field
// outside its domain keeps the definition default, and an option value this
// build no longer defines is dropped by the model. The password is stored in
// plaintext like the shipped dialog stored it, because the create form
// prefills it on the next open; it is never logged.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace services {
namespace creategame {

// One submitted/saved option value (the page's control value as text).
struct ServerOptionValue {
  std::string Name;
  std::string Value;
};

struct SavedServerSettings {
  std::string Hostname;
  std::string Password;
  bool Lan = false;
  int MaxPlayers = 8;
  std::vector<ServerOptionValue> Options;
};

class ServerSettingsDocument {
public:
  // The config.json section holding the last applied create-game values.
  static constexpr const char *kSection = "create_game";
  // The shipped dialog's edit fields are 64-byte strncpy buffers (0x40), so
  // the stored text is capped at 63 bytes exactly like the request.
  static constexpr std::size_t kMaxTextBytes = 63;

  // Parses the section object over `Out`'s current values (the packaged
  // definitions' defaults): absent fields keep them, out-of-domain values fall
  // back. False (and `Error`) on a malformed document; `Out` is unchanged.
  static bool parse(std::string_view Text, ServerSettingsDocument &Out,
                    std::string &Error);

  // Rebuilds the section object. False when a stored string is not valid
  // UTF-8 (config.json could not carry it).
  bool serialize(std::string &Text, std::string &Error) const;

  void setSaved(SavedServerSettings Value) {
    Saved = std::move(Value);
  }
  const SavedServerSettings &saved() const {
    return Saved;
  }

  // The four typed fields from a validated request.
  void apply(const std::string &Hostname, const std::string &Password, bool Lan,
             int MaxPlayers);

  // Upserts one validated option value.
  void setValue(std::string_view Cvar, std::string_view Value);

private:
  SavedServerSettings Saved;
};

} // namespace creategame
} // namespace services
