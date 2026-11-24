#pragma once

// The sprite files a weapon's HUD sprites are read from (svencoop_sdk
// cl_dll/ammo.cpp WeaponsResource::LoadWeaponSprites):
//   sprites/<spriteName>.txt
//   sprites/<customDirectory>/<spriteName>.txt  (after a CustWeapon message)
// and, per list entry, sprites/<szSprite>.spr. A trailing slash in the
// directory produces the same doubled separator the shipped client produces;
// the engine normalizes it, so the bytes must match.
//
// The name and the directory are server strings and may carry ".." segments:
// the stock REPLACE_WEAPON_SPRITES map script calls
// LoadSprites("../<dir>/<name>") to leave a weapon's custom directory, and
// the engine folds the segments when it opens the file (issue #141). Such a
// path is refused only when folding it climbs above the game directory.
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>

#include "../framework/ResourceLimits.h"

namespace weaponres {

inline std::string spriteListPath(std::string_view spriteName,
                                  std::string_view spriteDirectory) {
  std::string path = "sprites/";
  if (!spriteDirectory.empty()) {
    path += spriteDirectory;
    path += '/';
  }
  path += spriteName;
  path += ".txt";
  return path;
}

inline std::string spriteAtlasPath(std::string_view atlasName) {
  std::string path = "sprites/";
  path += atlasName;
  path += ".spr";
  return path;
}

// The composed path handed to the engine never reaches this length. The
// shipped client truncates its own 128-byte buffer instead, which resolves
// nothing either.
constexpr std::size_t kMaxSpritePath = 256;

enum class SpritePathStatus {
  kAccepted = 0,
  // Empty, absolute, too long, or outside ResourceLimits::ResourceAlphabet.
  kInvalidPart,
  // A ".." segment climbs above the game directory.
  kEscapesRoot,
  // The composed path reaches kMaxSpritePath.
  kTooLong,
};

// True when the engine's lexical normalization keeps the path inside the
// game directory: lexically_normal() folds "." and "..", keeps a ".." that
// has nothing left to cancel and never invents a root.
inline bool foldsInsideRoot(const std::filesystem::path &path) {
  const std::filesystem::path folded = path.lexically_normal();
  return !folded.has_root_path() &&
         std::ranges::none_of(folded, [](const std::filesystem::path &part) {
           return part == "..";
         });
}

inline SpritePathStatus checkSpritePath(std::string path) {
  if (path.size() >= kMaxSpritePath)
    return SpritePathStatus::kTooLong;
  if (!foldsInsideRoot(std::filesystem::path{std::move(path)}))
    return SpritePathStatus::kEscapesRoot;
  return SpritePathStatus::kAccepted;
}

inline SpritePathStatus
checkSpriteListPath(std::string_view spriteName,
                    std::string_view spriteDirectory) {
  if (!framework::ResourceLimits::relativeResourceName(spriteName) ||
      (!spriteDirectory.empty() &&
       !framework::ResourceLimits::relativeResourceName(spriteDirectory)))
    return SpritePathStatus::kInvalidPart;
  return checkSpritePath(spriteListPath(spriteName, spriteDirectory));
}

inline SpritePathStatus checkSpriteAtlasPath(std::string_view atlasName) {
  if (!framework::ResourceLimits::relativeResourceName(atlasName))
    return SpritePathStatus::kInvalidPart;
  return checkSpritePath(spriteAtlasPath(atlasName));
}

} // namespace weaponres
