// services::creategame::ServerSettings implementation - see ServerSettings.h.
#include "ServerSettings.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "../json/JsonFields.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace services {
namespace creategame {
namespace {

using services::json::fields;
using services::json::member;
using services::json::require;
using services::json::ValidationError;
using Value = rapidjson::Value;

// A saved string keeps the default when it no longer fits the field (an old
// or hand-edited file); the shape is still validated strictly.
void readSavedText(const Value &Object, const char *Key,
                   const std::string &Path, bool AllowEmpty, std::string &Out) {
  const Value &Text = member(Object, Key, Path);
  require(Text.IsString(), Path + "." + Key, "expected string");
  if (Text.GetStringLength() > ServerSettingsDocument::kMaxTextBytes)
    return;
  std::string Stored(Text.GetString(), Text.GetStringLength());
  if (Stored.find('\0') != std::string::npos || (!AllowEmpty && Stored.empty()))
    return;
  Out = std::move(Stored);
}

} // namespace

bool ServerSettingsDocument::parse(std::string_view Text,
                                   ServerSettingsDocument &Out,
                                   std::string &Error) {
  try {
    require(Text.size() <= 64 * 1024, "$", "section exceeds 64 KiB");
    require(Text.find('\0') == std::string_view::npos, "$",
            "unexpected NUL byte");
    rapidjson::Document Doc;
    Doc.Parse<rapidjson::kParseValidateEncodingFlag |
              rapidjson::kParseIterativeFlag>(Text.data(), Text.size());
    require(!Doc.HasParseError(), "$@" + std::to_string(Doc.GetErrorOffset()),
            rapidjson::GetParseError_En(Doc.GetParseError()));
    fields(Doc,
           {"version", "hostname", "password", "lan", "maxplayers", "options"},
           "$");
    const Value &Version = member(Doc, "version", "$");
    require(Version.IsInt() && Version.GetInt() == 1, "$.version",
            "unsupported version");

    // Work on a copy: a rejected document must leave the caller's defaults
    // untouched.
    SavedServerSettings Saved = Out.saved();
    if (Doc.HasMember("hostname"))
      readSavedText(Doc, "hostname", "$", false, Saved.Hostname);
    if (Doc.HasMember("password"))
      readSavedText(Doc, "password", "$", true, Saved.Password);
    if (Doc.HasMember("lan")) {
      const Value &Lan = member(Doc, "lan", "$");
      require(Lan.IsBool(), "$.lan", "expected boolean");
      Saved.Lan = Lan.GetBool();
    }
    if (Doc.HasMember("maxplayers")) {
      const Value &Players = member(Doc, "maxplayers", "$");
      require(Players.IsNumber(), "$.maxplayers", "expected number");
      const double Parsed = Players.GetDouble();
      require(std::isfinite(Parsed), "$.maxplayers", "number exceeds range");
      Saved.MaxPlayers =
          static_cast<int>(std::clamp<long>(std::lround(Parsed), 1, 32));
    }
    if (Doc.HasMember("options")) {
      const Value &Options = member(Doc, "options", "$");
      require(Options.IsObject(), "$.options", "expected object");
      require(Options.MemberCount() <= 64, "$.options", "too many entries");
      std::vector<ServerOptionValue> Values;
      Values.reserve(Options.MemberCount());
      std::set<std::string> Seen;
      for (auto It = Options.MemberBegin(); It != Options.MemberEnd(); ++It) {
        const std::string Name(It->name.GetString(),
                               It->name.GetStringLength());
        const std::string Path = "$.options." + Name;
        require(Seen.insert(Name).second, Path, "duplicate option");
        require(!Name.empty() && Name.size() <= 64, Path,
                "invalid option name");
        require(It->value.IsString(), Path, "expected string");
        const rapidjson::SizeType Length = It->value.GetStringLength();
        require(Length > 0 && Length <= 32, Path, "invalid value length");
        std::string Value(It->value.GetString(), Length);
        require(Value.find('\0') == std::string::npos, Path,
                "unexpected NUL byte");
        Values.push_back({Name, std::move(Value)});
      }
      Saved.Options = std::move(Values);
    }
    Out.setSaved(std::move(Saved));
    return true;
  } catch (const ValidationError &Error_) {
    Error = Error_.Field + ": " + Error_.Message;
    return false;
  }
}

bool ServerSettingsDocument::serialize(std::string &Text,
                                       std::string &Error) const {
  rapidjson::StringBuffer Buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer, rapidjson::UTF8<>,
                          rapidjson::UTF8<>, rapidjson::CrtAllocator,
                          rapidjson::kWriteValidateEncodingFlag>
      Writer(Buffer);
  const auto Write = [&](const std::string &Stored) {
    return Writer.String(Stored.data(),
                         static_cast<rapidjson::SizeType>(Stored.size()));
  };
  Writer.StartObject();
  Writer.Key("version");
  Writer.Int(1);
  Writer.Key("hostname");
  if (!Write(Saved.Hostname)) {
    Error = "hostname is not valid UTF-8";
    return false;
  }
  Writer.Key("password");
  if (!Write(Saved.Password)) {
    Error = "password is not valid UTF-8";
    return false;
  }
  Writer.Key("lan");
  Writer.Bool(Saved.Lan);
  Writer.Key("maxplayers");
  Writer.Int(Saved.MaxPlayers);
  Writer.Key("options");
  Writer.StartObject();
  for (const ServerOptionValue &Option : Saved.Options) {
    if (!Writer.Key(Option.Name.data(),
                    static_cast<rapidjson::SizeType>(Option.Name.size())) ||
        !Write(Option.Value)) {
      Error = "option " + Option.Name + " is not valid UTF-8";
      return false;
    }
  }
  Writer.EndObject();
  Writer.EndObject();
  Text.assign(Buffer.GetString(), Buffer.GetSize());
  return true;
}

void ServerSettingsDocument::apply(const std::string &Hostname,
                                   const std::string &Password, bool Lan,
                                   int MaxPlayers) {
  Saved.Hostname = Hostname;
  Saved.Password = Password;
  Saved.Lan = Lan;
  Saved.MaxPlayers = MaxPlayers;
}

void ServerSettingsDocument::setValue(std::string_view Cvar,
                                      std::string_view Value) {
  for (ServerOptionValue &Option : Saved.Options) {
    if (Option.Name == Cvar) {
      Option.Value = Value;
      return;
    }
  }
  Saved.Options.push_back({std::string(Cvar), std::string(Value)});
}

} // namespace creategame
} // namespace services
