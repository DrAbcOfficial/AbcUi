#pragma once

// Shared strict-field rules for the UI definition parsers: unknown and
// duplicate keys, required members and finite numbers. ValidationError stays
// private to the parser translation units - their public parse API catches it
// and fills its own error out-parameter.

#include <rapidjson/document.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>
#include <string>

namespace services::json {

struct ValidationError {
  std::string Field;
  std::string Message;
};

inline void require(bool valid, const std::string &field, const char *message) {
  if (!valid)
    throw ValidationError{field, message};
}

inline void fields(const rapidjson::Value &object,
                   std::initializer_list<const char *> allowed,
                   const std::string &path) {
  require(object.IsObject(), path, "expected object");
  std::set<std::string> seen;
  for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
    std::string key(it->name.GetString(), it->name.GetStringLength());
    require(seen.insert(key).second, path + "." + key, "duplicate field");
    require(std::any_of(allowed.begin(), allowed.end(),
                        [&](const char *name) { return key == name; }),
            path + "." + key, "unknown field");
  }
}

inline const rapidjson::Value &member(const rapidjson::Value &object,
                                      const char *key,
                                      const std::string &path) {
  require(object.HasMember(key), path + "." + key, "required field missing");
  return object[key];
}

inline float number(const rapidjson::Value &object, const char *key,
                    const std::string &path) {
  const auto &value = member(object, key, path);
  require(value.IsNumber(), path + "." + key, "expected finite number");
  const float result = static_cast<float>(value.GetDouble());
  require(std::isfinite(result), path + "." + key,
          "number exceeds float range");
  return result;
}

inline void array(const rapidjson::Value &value, const std::string &path,
                  unsigned limit) {
  require(value.IsArray(), path, "expected array");
  require(value.Size() <= limit, path, "too many entries");
}

} // namespace services::json
