#include "ServerSettings.h"

#include <abc/Parse.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace services {
namespace creategame {
namespace {

// The header FUN_100046f0 writes on every apply (GameUI.dll). The shipped
// file carries an older NOTE block above it; the original's Save opens with
// "wb" and does not reproduce that block, so neither does this writer.
constexpr std::string_view kHeader =
    "// Sven Co-op Server Configuration Layout Script (stores last settings "
    "chosen, too)\r\n";

// The shipped writer (FUN_1000a540 -> FUN_1000b460) drops quotes and '%' and
// truncates at 4095 bytes, because these strings are interpolated into the
// printf format that writes the line. Keep that boundary exactly.
std::string sanitize(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char c : text) {
    if (c == '"' || c == '%')
      continue;
    result.push_back(c);
  }
  if (result.size() > 4095)
    result.resize(4095);
  return result;
}

bool parseBool(std::string_view text) {
  const auto value = abc::ParseFloat(text);
  return value && *value != 0.0f;
}

std::string numericDefault(std::string_view text) {
  const auto value = abc::ParseFloat(text);
  if (!value)
    return sanitize(text);
  return std::format("{:.6f}", static_cast<double>(*value));
}

struct Token {
  enum class Kind { kEnd, kWord, kQuoted, kOpen, kClose };

  Kind kind = Kind::kEnd;
  std::string text;
};

// Whitespace/comments are skipped between tokens; a quoted token may contain
// braces, slashes and spaces.
class Scanner {
public:
  explicit Scanner(std::string_view text) : Text_(text) {}

  Token next() {
    skip();
    if (Pos_ >= Text_.size())
      return {};
    const char c = Text_[Pos_];
    if (c == '{' || c == '}') {
      ++Pos_;
      return {c == '{' ? Token::Kind::kOpen : Token::Kind::kClose, {}};
    }
    if (c == '"') {
      ++Pos_;
      std::string text;
      while (Pos_ < Text_.size() && Text_[Pos_] != '"') {
        if (Text_[Pos_] == '\\' && Pos_ + 1 < Text_.size()) {
          text.push_back(Text_[Pos_ + 1]);
          Pos_ += 2;
        } else {
          text.push_back(Text_[Pos_++]);
        }
      }
      if (Pos_ < Text_.size())
        ++Pos_;
      return {Token::Kind::kQuoted, std::move(text)};
    }
    std::string text;
    while (Pos_ < Text_.size()) {
      const char ch = Text_[Pos_];
      if (std::isspace(static_cast<unsigned char>(ch)) || ch == '{' ||
          ch == '}' || ch == '"')
        break;
      text.push_back(ch);
      ++Pos_;
    }
    if (text.empty()) {
      ++Pos_;
      return next();
    }
    return {Token::Kind::kWord, std::move(text)};
  }

private:
  void skip() {
    for (;;) {
      while (Pos_ < Text_.size() &&
             std::isspace(static_cast<unsigned char>(Text_[Pos_])))
        ++Pos_;
      if (Pos_ + 1 < Text_.size() && Text_[Pos_] == '/' &&
          Text_[Pos_ + 1] == '/') {
        while (Pos_ < Text_.size() && Text_[Pos_] != '\n')
          ++Pos_;
        continue;
      }
      break;
    }
  }

  std::string_view Text_;
  std::size_t Pos_ = 0;
};

} // namespace

bool ServerSettingsDocument::parse(const std::string &text,
                                   ServerSettingsDocument &out) {
  out = ServerSettingsDocument{};
  Scanner scanner(text);
  bool found = false;
  for (;;) {
    const Token token = scanner.next();
    if (token.kind == Token::Kind::kEnd)
      return false;
    if (token.kind == Token::Kind::kWord && token.text == "VERSION") {
      // The shipped Save writes the 1.0 constant; the value is not part of
      // the document (consume it so the scan stays token-aligned).
      (void)scanner.next();
      continue;
    }
    if (token.kind == Token::Kind::kWord && token.text == "DESCRIPTION") {
      const Token name = scanner.next();
      if (name.kind != Token::Kind::kWord &&
          name.kind != Token::Kind::kQuoted)
        return false;
      const Token open = scanner.next();
      if (open.kind != Token::Kind::kOpen)
        return false;
      found = true;
      break;
    }
  }
  if (!found)
    return false;

  for (;;) {
    const Token cvar = scanner.next();
    if (cvar.kind == Token::Kind::kEnd)
      return false;
    if (cvar.kind == Token::Kind::kClose)
      break;
    if (cvar.kind != Token::Kind::kQuoted)
      return false;
    if (out.Rows.size() >= kMaxRows)
      return false;

    if (scanner.next().kind != Token::Kind::kOpen)
      return false;
    const Token prompt = scanner.next();
    if (prompt.kind != Token::Kind::kQuoted)
      return false;
    if (scanner.next().kind != Token::Kind::kOpen)
      return false;
    const Token type = scanner.next();
    if (type.kind != Token::Kind::kWord)
      return false;

    ServerSettingRow row;
    row.Cvar = cvar.text;
    row.Prompt = prompt.text;
    if (type.text == "BOOL") {
      row.Kind = ServerSettingKind::kBool;
      if (scanner.next().kind != Token::Kind::kClose)
        return false;
    } else if (type.text == "NUMBER") {
      row.Kind = ServerSettingKind::kNumber;
      std::vector<float> bounds;
      for (;;) {
        const Token value = scanner.next();
        if (value.kind == Token::Kind::kClose)
          break;
        if (value.kind != Token::Kind::kWord)
          return false;
        const auto parsed = abc::ParseFloat(value.text);
        if (!parsed)
          return false;
        bounds.push_back(*parsed);
      }
      if (!bounds.empty())
        row.Min = bounds[0];
      if (bounds.size() >= 2)
        row.Max = bounds[1];
    } else if (type.text == "LIST") {
      row.Kind = ServerSettingKind::kList;
      for (;;) {
        const Token label = scanner.next();
        if (label.kind == Token::Kind::kClose)
          break;
        if (label.kind != Token::Kind::kQuoted)
          return false;
        const Token value = scanner.next();
        if (value.kind != Token::Kind::kQuoted)
          return false;
        row.Choices.push_back({label.text, value.text});
      }
    } else if (type.text == "STRING") {
      row.Kind = ServerSettingKind::kText;
      if (scanner.next().kind != Token::Kind::kClose)
        return false;
    } else {
      return false;
    }

    if (scanner.next().kind != Token::Kind::kOpen)
      return false;
    const Token value = scanner.next();
    if (value.kind != Token::Kind::kQuoted)
      return false;
    row.Default = value.text;
    if (scanner.next().kind != Token::Kind::kClose)
      return false;
    if (scanner.next().kind != Token::Kind::kClose)
      return false;
    out.Rows.push_back(std::move(row));
  }

  return !out.Rows.empty();
}

std::string ServerSettingsDocument::generateTimestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &now);
#else
  gmtime_r(&now, &tm);
#endif
  char date[32] = {};
  std::strftime(date, sizeof(date), "%a %b %d", &tm);
  if (date[8] == '0')
    date[8] = ' ';
  int hour = tm.tm_hour;
  const char *suffix = hour >= 12 ? "PM" : "AM";
  if (hour > 12)
    hour -= 12;
  if (hour == 0)
    hour = 12;
  return std::format("{} {:02d}:{:02d}:{:02d} {}", date, hour, tm.tm_min,
                     tm.tm_sec, suffix);
}

const ServerSettingRow *
ServerSettingsDocument::find(std::string_view cvar) const {
  for (const ServerSettingRow &row : Rows) {
    if (row.Cvar == cvar)
      return &row;
  }
  return nullptr;
}

ServerSettingRow *ServerSettingsDocument::find(std::string_view cvar) {
  for (ServerSettingRow &row : Rows) {
    if (row.Cvar == cvar)
      return &row;
  }
  return nullptr;
}

bool ServerSettingsDocument::setValue(std::string_view cvar,
                                      std::string_view value) {
  ServerSettingRow *row = find(cvar);
  if (!row)
    return false;
  switch (row->Kind) {
  case ServerSettingKind::kBool:
    row->Default = parseBool(value) ? "1" : "0";
    return true;
  case ServerSettingKind::kNumber:
  case ServerSettingKind::kList: {
    const auto parsed = abc::ParseFloat(value);
    if (!parsed)
      return false;
    row->Default = std::format("{:.6f}", static_cast<double>(*parsed));
    return true;
  }
  case ServerSettingKind::kText:
    row->Default = sanitize(value);
    return true;
  }
  return false;
}

bool ServerSettingsDocument::apply(const std::string &hostname,
                                   const std::string &password, bool lan,
                                   int maxPlayers) {
  bool ok = setValue("hostname", hostname);
  ok = setValue("sv_password", password) && ok;
  ok = setValue("sv_lan", lan ? "1" : "0") && ok;
  ok = setValue("maxplayers", std::to_string(maxPlayers)) && ok;
  return ok;
}

SavedServerSettings ServerSettingsDocument::saved() const {
  SavedServerSettings result;
  if (const ServerSettingRow *row = find("hostname"))
    result.Hostname = row->Default;
  if (const ServerSettingRow *row = find("sv_password"))
    result.Password = row->Default;
  if (const ServerSettingRow *row = find("sv_lan"))
    result.Lan = parseBool(row->Default);
  if (const ServerSettingRow *row = find("maxplayers")) {
    if (const auto value = abc::ParseFloat(row->Default)) {
      const long rounded = std::lround(static_cast<double>(*value));
      result.MaxPlayers =
          static_cast<int>(std::clamp<long>(rounded, 1, 32));
    }
  }
  return result;
}

std::string
ServerSettingsDocument::serialize(std::string_view generatedAt) const {
  if (Rows.empty())
    return {};
  std::string out;
  out.reserve(8192);
  out.append(kHeader);
  out += "// File generated:  ";
  out += generatedAt.empty() ? generateTimestamp() : std::string(generatedAt);
  out += "\r\n";
  out += "//\r\n//\r\n// Cvar\t-\tSetting\r\n\r\n";
  out += "VERSION 1.0\r\n\r\n";
  out += "DESCRIPTION SERVER_OPTIONS\r\n{\r\n";
  for (const ServerSettingRow &row : Rows) {
    out += "\t\"";
    out += sanitize(row.Cvar);
    out += "\"\r\n\t{\r\n\t\t\"";
    out += sanitize(row.Prompt);
    out += "\"\r\n";
    switch (row.Kind) {
    case ServerSettingKind::kBool:
      out += "\t\t{ BOOL }\r\n";
      out += parseBool(row.Default) ? "\t\t{ \"1\" }\r\n"
                                    : "\t\t{ \"0\" }\r\n";
      break;
    case ServerSettingKind::kNumber:
      out += std::format("\t\t{{ NUMBER {:.6f} {:.6f} }}\r\n",
                         static_cast<double>(row.Min),
                         static_cast<double>(row.Max));
      out += "\t\t{ \"";
      out += numericDefault(row.Default);
      out += "\" }\r\n";
      break;
    case ServerSettingKind::kList:
      out += "\t\t{\r\n\t\t\tLIST\r\n";
      for (const ServerSettingChoice &choice : row.Choices) {
        out += "\t\t\t\"";
        out += sanitize(choice.Label);
        out += "\" \"";
        out += sanitize(choice.Value);
        out += "\"\r\n";
      }
      out += "\t\t}\r\n\t\t{ \"";
      out += numericDefault(row.Default);
      out += "\" }\r\n";
      break;
    case ServerSettingKind::kText:
      out += "\t\t{ STRING }\r\n\t\t{ \"";
      out += sanitize(row.Default);
      out += "\" }\r\n";
      break;
    }
    out += "\t}\r\n\r\n";
  }
  out += "}\r\n";
  return out;
}

} // namespace creategame
} // namespace services
