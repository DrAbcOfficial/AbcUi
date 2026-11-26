#pragma once

// framework::wtext - UTF-8 <-> UTF-16 at the Win32 file API boundary. Paths
// are UTF-8 on the AbcCore side (abc::ModPath) and wide at the OS; names read
// back from the OS become UTF-8 package paths. Bytes that are not UTF-8
// become replacement characters rather than an exception.

#include <windows.h>

#include <string>
#include <string_view>

namespace framework::wtext {

inline std::wstring wideFromUtf8(std::string_view text) {
  if (text.empty())
    return {};
  const int size = static_cast<int>(text.size());
  const int length =
      MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
  std::wstring wide(static_cast<size_t>(length > 0 ? length : 0), L'\0');
  if (length > 0)
    MultiByteToWideChar(CP_UTF8, 0, text.data(), size, wide.data(), length);
  return wide;
}

inline std::string utf8FromWide(std::wstring_view text) {
  if (text.empty())
    return {};
  const int size = static_cast<int>(text.size());
  const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr,
                                         0, nullptr, nullptr);
  std::string out(static_cast<size_t>(length > 0 ? length : 0), '\0');
  if (length > 0)
    WideCharToMultiByte(CP_UTF8, 0, text.data(), size, out.data(), length,
                        nullptr, nullptr);
  return out;
}

} // namespace framework::wtext
