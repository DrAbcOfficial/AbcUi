#include "WeaponFilterModel.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <utility>

namespace weaponfilter {
namespace {

bool validNameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

const char *const kInvalidName = "the weapon name must be [A-Za-z0-9_]{1,64}";
const char *const kInvalidSlot = "the slot must be 0-9";
const char *const kInvalidPosition = "the position must be 0-255";
const char *const kLimitReached = "the 256-rule limit is reached";

} // namespace

bool validName(std::string_view name) {
  if (name.empty() || name.size() > kMaxNameLength)
    return false;
  return std::all_of(name.begin(), name.end(), validNameChar);
}

bool validSlot(int slot) {
  return slot >= 0 && slot <= 9;
}

bool validPosition(int position) {
  return position >= 0 && position <= 255;
}

bool Model::parse(std::string_view json, std::string &error) {
  error.clear();
  rapidjson::Document document;
  document.Parse<rapidjson::kParseValidateEncodingFlag |
                 rapidjson::kParseIterativeFlag>(json.data(), json.size());
  if (document.HasParseError() || !document.IsObject()) {
    error = "the section is not a JSON object";
    return false;
  }
  const auto version = document.FindMember("version");
  if (version == document.MemberEnd() || !version->value.IsInt() ||
      version->value.GetInt() != kSchemaVersion) {
    error = "the section version is not 1";
    return false;
  }
  const auto weapons = document.FindMember("weapons");
  if (weapons == document.MemberEnd() || !weapons->value.IsArray()) {
    error = "the weapons array is missing";
    return false;
  }

  std::map<std::string, Rule, std::less<>> parsed;
  for (const auto &entry : weapons->value.GetArray()) {
    if (!entry.IsObject()) {
      error = "a rule is not an object";
      return false;
    }
    const auto name = entry.FindMember("name");
    const auto slot = entry.FindMember("slot");
    const auto position = entry.FindMember("position");
    if (name == entry.MemberEnd() || !name->value.IsString() ||
        slot == entry.MemberEnd() || !slot->value.IsInt() ||
        position == entry.MemberEnd() || !position->value.IsInt()) {
      error = "a rule needs a name, a slot and a position";
      return false;
    }
    Rule rule;
    rule.name.assign(name->value.GetString(), name->value.GetStringLength());
    rule.slot = slot->value.GetInt();
    rule.position = position->value.GetInt();
    if (!validName(rule.name)) {
      error = kInvalidName;
      return false;
    }
    if (!validSlot(rule.slot)) {
      error = kInvalidSlot;
      return false;
    }
    if (!validPosition(rule.position)) {
      error = kInvalidPosition;
      return false;
    }
    if (parsed.find(rule.name) == parsed.end() && parsed.size() >= kMaxRules) {
      error = kLimitReached;
      return false;
    }
    const std::string key = rule.name;
    parsed[key] = std::move(rule);
  }
  Rules = std::move(parsed);
  return true;
}

bool Model::serialize(std::string &json, std::string &error) const {
  error.clear();
  rapidjson::Document document;
  document.SetObject();
  auto &allocator = document.GetAllocator();
  document.AddMember("version", kSchemaVersion, allocator);

  rapidjson::Value weapons(rapidjson::kArrayType);
  for (const auto &[name, rule] : Rules) {
    rapidjson::Value entry(rapidjson::kObjectType);
    entry.AddMember(
        "name",
        rapidjson::Value(rule.name.c_str(),
                         static_cast<rapidjson::SizeType>(rule.name.size()),
                         allocator),
        allocator);
    entry.AddMember("slot", rule.slot, allocator);
    entry.AddMember("position", rule.position, allocator);
    weapons.PushBack(entry, allocator);
  }
  document.AddMember("weapons", weapons, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  if (!document.Accept(writer)) {
    error = "the rules could not be serialized";
    return false;
  }
  json.assign(buffer.GetString(), buffer.GetSize());
  return true;
}

const Rule *Model::find(std::string_view name) const {
  const auto it = Rules.find(name);
  return it != Rules.end() ? &it->second : nullptr;
}

bool Model::set(const Rule &rule, std::string &error) {
  error.clear();
  if (!validName(rule.name)) {
    error = kInvalidName;
    return false;
  }
  if (!validSlot(rule.slot)) {
    error = kInvalidSlot;
    return false;
  }
  if (!validPosition(rule.position)) {
    error = kInvalidPosition;
    return false;
  }
  if (Rules.find(rule.name) == Rules.end() && Rules.size() >= kMaxRules) {
    error = kLimitReached;
    return false;
  }
  Rules[rule.name] = rule;
  return true;
}

bool Model::erase(std::string_view name) {
  return Rules.erase(std::string(name)) > 0;
}

void Model::clear() {
  Rules.clear();
}

} // namespace weaponfilter
