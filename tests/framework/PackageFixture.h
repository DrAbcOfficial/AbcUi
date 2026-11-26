#pragma once

// A module package for the offline tests: a zip of STORED entries, the shape
// tools/pack.py writes, so the VFS serves every entry zero-copy. The bytes
// are built in memory; the test writes them to a file and maps it with
// PackageImage::open, exactly as the loader does.

#include <miniz.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace abctest {

struct FixtureEntry {
  std::string_view name;
  std::string_view bytes;
};

// Little-endian record fields.
inline void put16(std::vector<std::byte> &out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xff));
  out.push_back(static_cast<std::byte>(value >> 8));
}

inline void put32(std::vector<std::byte> &out, std::uint32_t value) {
  for (const unsigned shift : {0u, 8u, 16u, 24u})
    out.push_back(static_cast<std::byte>(value >> shift & 0xff));
}

inline void putBytes(std::vector<std::byte> &out, std::string_view text) {
  std::ranges::copy(std::as_bytes(std::span(text)), std::back_inserter(out));
}

inline std::vector<std::byte>
buildPackage(const std::vector<FixtureEntry> &entries) {
  constexpr std::uint16_t kVersion = 20;
  constexpr std::uint16_t kDate = 0x21; // 1980-01-01
  std::vector<std::byte> package;
  std::vector<std::byte> central;
  for (const FixtureEntry &entry : entries) {
    const std::uint32_t size = static_cast<std::uint32_t>(entry.bytes.size());
    const std::uint32_t crc =
        mz_crc32(MZ_CRC32_INIT,
                 reinterpret_cast<const unsigned char *>(entry.bytes.data()),
                 entry.bytes.size());
    const std::uint16_t nameLength =
        static_cast<std::uint16_t>(entry.name.size());
    const auto offset = static_cast<std::uint32_t>(package.size());

    put32(package, 0x04034b50); // local file header
    put16(package, kVersion);
    put16(package, 0); // flags
    put16(package, 0); // STORED
    put16(package, 0); // time
    put16(package, kDate);
    put32(package, crc);
    put32(package, size);
    put32(package, size);
    put16(package, nameLength);
    put16(package, 0); // extra field
    putBytes(package, entry.name);
    putBytes(package, entry.bytes);

    put32(central, 0x02014b50); // central directory header
    put16(central, kVersion);   // version made by
    put16(central, kVersion);   // version needed
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put16(central, kDate);
    put32(central, crc);
    put32(central, size);
    put32(central, size);
    put16(central, nameLength);
    put16(central, 0); // extra field
    put16(central, 0); // comment
    put16(central, 0); // disk number
    put16(central, 0); // internal attributes
    put32(central, 0); // external attributes
    put32(central, offset);
    putBytes(central, entry.name);
  }
  // The central directory follows the entries and the end record closes it.
  const auto centralOffset = static_cast<std::uint32_t>(package.size());
  std::ranges::copy(central, std::back_inserter(package));
  const auto count = static_cast<std::uint16_t>(entries.size());
  put32(package, 0x06054b50); // end of central directory
  put16(package, 0);
  put16(package, 0);
  put16(package, count);
  put16(package, count);
  put32(package, static_cast<std::uint32_t>(central.size()));
  put32(package, centralOffset);
  put16(package, 0);
  return package;
}

} // namespace abctest
