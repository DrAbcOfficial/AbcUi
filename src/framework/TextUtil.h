#pragma once

// framework::TextUtil - RML text escaping for server-derived strings.
// Old vgui drew message text through Label (plain text, no markup surface);
// the Rml port writes the same text through SetInnerRML, so raw server text
// would gain an element-injection surface the original never had (D-17 #3).
// Every server-derived string routed into SetInnerRML goes through this.

#include <string>
#include <string_view>

namespace framework {

namespace TextUtil {

// ASCII-only case folding. The engine tokens this repository compares are
// ASCII literals; the Turkish-I/locale behaviour of <cctype> is not wanted.
inline char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string lowerAscii(std::string text) {
  for (char &c : text)
    c = lowerAscii(c);
  return text;
}

inline bool equalsIgnoreCase(std::string_view left, std::string_view right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t i = 0; i < left.size(); ++i)
    if (lowerAscii(left[i]) != lowerAscii(right[i]))
      return false;
  return true;
}

inline std::string trimNewlines(const std::string &text) {
  const auto first = text.find_first_not_of('\n');
  if (first == std::string::npos)
    return {};
  return text.substr(first, text.find_last_not_of('\n') - first + 1);
}

inline bool parseTranslationLine(std::string line, std::string &key,
                                 std::string &value) {
  auto trim = [](std::string &part) {
    const auto first = part.find_first_not_of(" \t\r");
    part = first == std::string::npos
               ? std::string()
               : part.substr(first, part.find_last_not_of(" \t\r") - first + 1);
  };
  trim(line);
  if (line.empty() || line[0] == '#')
    return false;
  const auto equal = line.find('=');
  if (equal == std::string::npos)
    return false;
  key = line.substr(0, equal);
  value = line.substr(equal + 1);
  trim(key);
  trim(value);
  return !key.empty();
}

inline std::string escapeRml(std::string text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    default:
      out += c;
    }
  }
  return out;
}

// Attribute-value variant: additionally escapes the quote characters that
// would close a double-quoted attribute. escapeRml alone is sufficient only
// for TEXT NODES; a server-derived string placed inside an attribute
// (data-*, title, value) needs this one, or a quote in the name escapes its
// attribute and injects markup (T20 finding: the player list carries names
// in data- attributes for the click payload).
inline std::string escapeRmlAttribute(std::string text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '\'':
      out += "&#39;";
      break;
    default:
      out += c;
    }
  }
  return out;
}

} // namespace TextUtil

} // namespace framework
