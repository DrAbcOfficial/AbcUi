#pragma once

// services::creategame::ServerOptions - the create-game server option
// document (gameui/config/server_options.json): the rows the page renders and
// the four typed defaults (hostname / maxplayers / sv_password / sv_lan).
//
// The packaged file is definition data, not executable commands: it admits
// the four typed rows plus numeric/list rows named mp_* or sv_*, every name is
// [a-z0-9_], and every submitted value must be a finite number inside the
// row's domain. The page's values are saved in abc/abcui/config.json (see
// ServerSettings.h); this document itself is read-only.

#include "ServerSettings.h"

#include <string>
#include <vector>

namespace services {
namespace creategame {

enum class ServerSettingKind { kBool, kNumber, kList, kText };

struct ServerOptionChoice {
  float Value = 0;
  std::string Label;
};

struct ServerOption {
  std::string Name;
  std::string Label;
  ServerSettingKind Kind = ServerSettingKind::kNumber;
  float Default = 0;
  float Min = 0;
  float Max = 1;
  // False for the shipped unbounded spelling NUMBER -1 -1 (no min/max gate).
  bool Bounded = true;
  std::vector<ServerOptionChoice> Choices;
};

struct ServerOptionDocument {
  std::vector<ServerOption> Options;
  SavedServerSettings Defaults;
};

// Parses server_options.json. Pure (no filesystem/engine); on failure `Out` is
// empty and `Error` names the offending field.
bool parseServerOptionDocument(const std::string &Text,
                               ServerOptionDocument &Out, std::string &Error);

// One submitted value against its definition: finite, non-empty and inside
// the row's toggle, list or numeric domain.
bool validateServerOptionValue(const ServerOption &Definition,
                               const std::string &Value);

// The page submits exactly one value per rendered option.
bool validateServerOptions(const std::vector<ServerOption> &Definitions,
                           const std::vector<ServerOptionValue> &Values);

} // namespace creategame
} // namespace services
