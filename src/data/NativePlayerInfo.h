#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hooks {

// g_PlayerExtraInfo includes row zero. Layout values come from the catalog
// matched to client.dll, not the engine build number. Keep the legacy 32-bit
// donor view; it is not a native type declaration.
//
// The word at this offset is the client's decrypted cache of two writers:
// the low 16 bits are extraslevel (ClExtrasInfo handler, client build 10257
// writes it at +0x54), bit 16 is the ScoreInfo showextras flag (+0x56).
// Note the reverse-engineered sdk writes the level through
// extra_player_info_t::adminlevel; the 10257 binary does not. Trust the
// catalog/binary, not that source line.
struct NativePlayerInfoView {
  const std::byte *base = nullptr;
  std::size_t stride = 0;
  std::size_t donorOffset = 0;

  bool valid() const {
    return base && stride >= sizeof(int32_t) &&
           donorOffset <= stride - sizeof(int32_t);
  }

  bool readDonor(int index, int32_t &value) const {
    if (!valid() || index < 1 || index > 32)
      return false;
    std::memcpy(&value,
                base + static_cast<std::size_t>(index) * stride + donorOffset,
                sizeof(value));
    return true;
  }
};

} // namespace hooks
