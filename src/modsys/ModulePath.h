#pragma once

// framework::modulePath - the canonical spelling of a module resource path
// (a package entry or a custom file): '/' separators, no '.', '..' or empty
// segment. False for a path that escapes its root or carries a separator,
// drive, control byte or uri delimiter the module uris never spell.

#include <string>
#include <string_view>
#include <vector>

namespace framework::modulePath {

inline bool normalize(std::string_view path, std::string &out) {
  if (path.empty() || path.front() == '/' ||
      path.find_first_of("\\:\0?#", 0, 5) != std::string_view::npos)
    return false;
  for (const unsigned char c : path)
    if (c < 0x20 || c == 0x7f)
      return false;
  std::vector<std::string_view> segments;
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string_view::npos)
      end = path.size();
    const std::string_view segment = path.substr(start, end - start);
    start = end + 1;
    if (segment.empty() || segment == ".")
      continue;
    if (segment == "..") {
      if (segments.empty())
        return false;
      segments.pop_back();
      continue;
    }
    segments.push_back(segment);
  }
  if (segments.empty())
    return false;
  out.clear();
  for (const std::string_view segment : segments) {
    if (!out.empty())
      out.push_back('/');
    out.append(segment);
  }
  return true;
}

} // namespace framework::modulePath
