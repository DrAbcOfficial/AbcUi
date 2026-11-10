// services::creategame::ServerOptions implementation - see ServerOptions.h.
#include "ServerOptions.h"

#include <abc/Parse.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "../json/JsonFields.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace services {
namespace creategame {
namespace {

using services::json::array;
using services::json::fields;
using services::json::member;
using services::json::number;
using services::json::require;
using services::json::ValidationError;
using Value = rapidjson::Value;

// The page renders the rows in file order; the cap only guards a malformed
// document (the shipped file has 24 rows).
constexpr std::size_t kMaxItems = 64;
constexpr std::size_t kMaxLabelBytes = 1024;

bool optionName(const std::string &Name) {
  if (Name.empty() || Name.size() > 64 ||
      (!Name.starts_with("mp_") && !Name.starts_with("sv_")))
    return false;
  return std::all_of(Name.begin(), Name.end(), [](unsigned char C) {
    return (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_';
  });
}

std::string readText(const Value &Item, const char *Key,
                     const std::string &Path, bool AllowEmpty) {
  const Value &Text = member(Item, Key, Path);
  require(Text.IsString(), Path + "." + Key, "expected string");
  std::string Stored(Text.GetString(), Text.GetStringLength());
  require((AllowEmpty || !Stored.empty()) &&
              Stored.size() <= ServerSettingsDocument::kMaxTextBytes &&
              Stored.find('\0') == std::string::npos,
          Path + "." + Key, "invalid text");
  return Stored;
}

std::string readLabel(const Value &Item, const std::string &Path) {
  const Value &Label = member(Item, "label", Path);
  require(Label.IsString(), Path + ".label", "expected string");
  std::string Stored(Label.GetString(), Label.GetStringLength());
  require(!Stored.empty() && Stored.size() <= kMaxLabelBytes &&
              Stored.find('\0') == std::string::npos,
          Path + ".label", "invalid label");
  return Stored;
}

ServerSettingKind readKind(const Value &Item, const std::string &Path) {
  const Value &Kind = member(Item, "kind", Path);
  require(Kind.IsString(), Path + ".kind", "expected string");
  const std::string Text(Kind.GetString(), Kind.GetStringLength());
  if (Text == "bool")
    return ServerSettingKind::kBool;
  if (Text == "number")
    return ServerSettingKind::kNumber;
  if (Text == "list")
    return ServerSettingKind::kList;
  if (Text == "text")
    return ServerSettingKind::kText;
  require(false, Path + ".kind", "unknown kind");
  return ServerSettingKind::kText;
}

float readDefault(const Value &Item, const std::string &Path) {
  const Value &Default = member(Item, "default", Path);
  require(Default.IsNumber(), Path + ".default", "expected number");
  const float Parsed = static_cast<float>(Default.GetDouble());
  require(std::isfinite(Parsed), Path + ".default", "number exceeds range");
  return Parsed;
}

bool oneOf(const std::vector<ServerOptionChoice> &Choices, float Value) {
  return std::any_of(
      Choices.begin(), Choices.end(),
      [&](const ServerOptionChoice &Choice) { return Choice.Value == Value; });
}

} // namespace

bool parseServerOptionDocument(const std::string &Text,
                               ServerOptionDocument &Out, std::string &Error) {
  Out = ServerOptionDocument{};
  try {
    require(Text.size() <= 1024 * 1024, "$", "file exceeds 1 MiB");
    require(Text.find('\0') == std::string::npos, "$", "unexpected NUL byte");
    rapidjson::Document Doc;
    Doc.Parse<rapidjson::kParseValidateEncodingFlag |
              rapidjson::kParseIterativeFlag>(Text.data(), Text.size());
    require(!Doc.HasParseError(), "$@" + std::to_string(Doc.GetErrorOffset()),
            rapidjson::GetParseError_En(Doc.GetParseError()));
    fields(Doc, {"version", "items"}, "$");
    const Value &Version = member(Doc, "version", "$");
    require(Version.IsInt() && Version.GetInt() == 1, "$.version",
            "unsupported version");
    const Value &Items = member(Doc, "items", "$");
    array(Items, "$.items", kMaxItems);
    require(Items.Size() > 0, "$.items", "empty item list");

    std::set<std::string> Seen;
    bool HaveLan = false;
    bool HaveHostname = false;
    bool HavePassword = false;
    bool HavePlayers = false;
    SavedServerSettings Defaults;
    std::vector<ServerOption> Options;
    for (rapidjson::SizeType Index = 0; Index < Items.Size(); ++Index) {
      const std::string Path = "$.items[" + std::to_string(Index) + "]";
      const Value &Item = Items[Index];
      fields(Item,
             {"cvar", "label", "kind", "default", "min", "max", "options"},
             Path);

      ServerOption Row;
      {
        const Value &Cvar = member(Item, "cvar", Path);
        require(Cvar.IsString(), Path + ".cvar", "expected string");
        Row.Name.assign(Cvar.GetString(), Cvar.GetStringLength());
      }
      const bool Typed = Row.Name == "sv_lan" || Row.Name == "hostname" ||
                         Row.Name == "maxplayers" || Row.Name == "sv_password";
      require(Typed || optionName(Row.Name), Path + ".cvar",
              "option names are [a-z0-9_] mp_/sv_ identifiers");
      require(Seen.insert(Row.Name).second, Path + ".cvar", "duplicate cvar");
      Row.Label = readLabel(Item, Path);
      Row.Kind = readKind(Item, Path);

      // The four typed rows seed the create form and the saved values; they
      // are not rendered as option rows.
      if (Row.Name == "sv_lan") {
        require(Row.Kind == ServerSettingKind::kBool, Path + ".kind",
                "sv_lan must be bool");
        require(!Item.HasMember("min") && !Item.HasMember("max") &&
                    !Item.HasMember("options"),
                Path, "sv_lan has no range or choices");
        const float Default = readDefault(Item, Path);
        require(Default == 0.0f || Default == 1.0f, Path + ".default",
                "bool default must be 0 or 1");
        Defaults.Lan = Default != 0.0f;
        HaveLan = true;
        continue;
      }
      if (Row.Name == "hostname" || Row.Name == "sv_password") {
        require(Row.Kind == ServerSettingKind::kText, Path + ".kind",
                "text row expected");
        require(!Item.HasMember("min") && !Item.HasMember("max") &&
                    !Item.HasMember("options"),
                Path, "text row has no range or choices");
        const std::string Default =
            readText(Item, "default", Path, Row.Name == "sv_password");
        if (Row.Name == "hostname") {
          Defaults.Hostname = Default;
          HaveHostname = true;
        } else {
          Defaults.Password = Default;
          HavePassword = true;
        }
        continue;
      }
      if (Row.Name == "maxplayers") {
        require(Row.Kind == ServerSettingKind::kNumber, Path + ".kind",
                "maxplayers must be number");
        const float Default = readDefault(Item, Path);
        require(Default >= 1.0f && Default <= 32.0f, Path + ".default",
                "maxplayers default is outside 1..32");
        Defaults.MaxPlayers = static_cast<int>(std::lround(Default));
        HavePlayers = true;
        continue;
      }

      // A rendered server option: a toggle, a bounded/unbounded number or a
      // list. Text fields are only the typed rows above.
      if (Row.Kind == ServerSettingKind::kBool) {
        require(!Item.HasMember("min") && !Item.HasMember("max") &&
                    !Item.HasMember("options"),
                Path, "bool row has no range or choices");
        const float Default = readDefault(Item, Path);
        require(Default == 0.0f || Default == 1.0f, Path + ".default",
                "bool default must be 0 or 1");
        Row.Default = Default;
        Row.Bounded = true;
      } else if (Row.Kind == ServerSettingKind::kNumber) {
        require(!Item.HasMember("options"), Path + ".options",
                "number row has no choices");
        require(Item.HasMember("min") && Item.HasMember("max"), Path,
                "number row needs min and max");
        Row.Default = readDefault(Item, Path);
        Row.Min = number(Item, "min", Path);
        Row.Max = number(Item, "max", Path);
        Row.Bounded = !(Row.Min == -1.0f && Row.Max == -1.0f);
        require(!Row.Bounded || Row.Min <= Row.Max, Path, "min exceeds max");
        require(!Row.Bounded ||
                    (Row.Default >= Row.Min && Row.Default <= Row.Max),
                Path + ".default", "default is outside min..max");
      } else if (Row.Kind == ServerSettingKind::kList) {
        require(!Item.HasMember("min") && !Item.HasMember("max"), Path,
                "list row has no range");
        Row.Default = readDefault(Item, Path);
        const Value &Choices = member(Item, "options", Path);
        array(Choices, Path + ".options", 64);
        require(Choices.Size() > 0, Path + ".options", "empty choice list");
        std::set<float> Values;
        for (rapidjson::SizeType ChoiceIndex = 0; ChoiceIndex < Choices.Size();
             ++ChoiceIndex) {
          const std::string ChoicePath =
              Path + ".options[" + std::to_string(ChoiceIndex) + "]";
          const Value &Choice = Choices[ChoiceIndex];
          fields(Choice, {"value", "label"}, ChoicePath);
          const Value &ChoiceValue = member(Choice, "value", ChoicePath);
          require(ChoiceValue.IsNumber(), ChoicePath + ".value",
                  "expected number");
          const float ChoiceNumber =
              static_cast<float>(ChoiceValue.GetDouble());
          require(std::isfinite(ChoiceNumber), ChoicePath + ".value",
                  "number exceeds range");
          require(Values.insert(ChoiceNumber).second, ChoicePath + ".value",
                  "duplicate choice value");
          const Value &ChoiceLabel = member(Choice, "label", ChoicePath);
          require(ChoiceLabel.IsString(), ChoicePath + ".label",
                  "expected string");
          std::string Label(ChoiceLabel.GetString(),
                            ChoiceLabel.GetStringLength());
          require(!Label.empty() && Label.size() <= kMaxLabelBytes &&
                      Label.find('\0') == std::string::npos,
                  ChoicePath + ".label", "invalid label");
          Row.Choices.push_back({ChoiceNumber, std::move(Label)});
        }
        require(oneOf(Row.Choices, Row.Default), Path + ".default",
                "default is not one of the choices");
        Row.Bounded = true;
      } else {
        require(false, Path + ".kind", "only the typed rows are text");
      }
      Options.push_back(std::move(Row));
    }
    require(HaveLan && HaveHostname && HavePassword && HavePlayers, "$.items",
            "the four typed rows are required");
    Out.Defaults = std::move(Defaults);
    Out.Options = std::move(Options);
    return true;
  } catch (const ValidationError &Error_) {
    Error = Error_.Field + ": " + Error_.Message;
    Out = ServerOptionDocument{};
    return false;
  }
}

bool validateServerOptionValue(const ServerOption &Definition,
                               const std::string &Value) {
  if (Value.empty() || Value.size() > 32)
    return false;
  const auto Parsed = abc::ParseFloat(Value);
  if (!Parsed || !std::isfinite(*Parsed))
    return false;
  const float Number = *Parsed;
  if (Definition.Kind == ServerSettingKind::kBool && Number != 0.0f &&
      Number != 1.0f)
    return false;
  if (!Definition.Choices.empty())
    return oneOf(Definition.Choices, Number);
  return !Definition.Bounded ||
         (Number >= Definition.Min && Number <= Definition.Max);
}

bool validateServerOptions(const std::vector<ServerOption> &Definitions,
                           const std::vector<ServerOptionValue> &Values) {
  if (Values.size() != Definitions.size())
    return false;
  std::set<std::string> Seen;
  for (const ServerOptionValue &Entry : Values) {
    const auto Definition = std::find_if(
        Definitions.begin(), Definitions.end(),
        [&](const ServerOption &Option) { return Option.Name == Entry.Name; });
    if (Definition == Definitions.end() || !Seen.insert(Entry.Name).second ||
        !validateServerOptionValue(*Definition, Entry.Value))
      return false;
  }
  return true;
}

} // namespace creategame
} // namespace services
