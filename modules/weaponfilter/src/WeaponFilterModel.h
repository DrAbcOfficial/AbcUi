#pragma once

// abcui.weaponfilter - the persisted rule model.
//
// The model owns the rule set: JSON (de)serialization and validation. Nothing
// here touches the engine, the console or config.json, so the offline suite
// exercises it directly.

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace weaponfilter {

// JSON schema version of the module section.
inline constexpr int kSchemaVersion = 1;
// Most rules the section stores (and the framework applies).
inline constexpr std::size_t kMaxRules = 256;
inline constexpr std::size_t kMaxNameLength = 64;

struct Rule {
  std::string name;
  int slot = 0;
  int position = 0;

  bool operator==(const Rule &other) const = default;
};

// The canonical weapon identifier [A-Za-z0-9_]{1,64}: a server WeaponList name
// and a safe console token.
bool validName(std::string_view name);
// 0..9.
bool validSlot(int slot);
// 0..255.
bool validPosition(int position);

class Model {
public:
  // Parses a section object: {"version":1,"weapons":[...]}. Unknown fields are
  // ignored, duplicate names keep the last rule, and a damaged section leaves
  // the model unchanged.
  bool parse(std::string_view json, std::string &error);
  // Writes {"version":1,"weapons":[...]} sorted by name.
  bool serialize(std::string &json, std::string &error) const;

  const Rule *find(std::string_view name) const;
  // Validates and stores; replaces an existing rule of the same name.
  bool set(const Rule &rule, std::string &error);
  bool erase(std::string_view name);
  void clear();
  const std::map<std::string, Rule, std::less<>> &rules() const {
    return Rules;
  }

private:
  std::map<std::string, Rule, std::less<>> Rules;
};

} // namespace weaponfilter
