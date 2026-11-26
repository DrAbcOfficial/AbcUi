#pragma once

// New implementation. Engine-independent <model> attribute validation.
// Target consumes the validated options; ElementModel supplies string values.
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace modelpreview {

// The shipped generic player model: every resolution chain ends here (it is
// the stock `models/player.mdl` the card's document also defaults to).
constexpr const char *kGenericModelPath = "models/player.mdl";

constexpr const char *kActionModelDefault = "display_male";

// The action model directory: where the action model groups live, as the
// engine filesystem spells it. It is the menu module's custom directory
// (abc/abcui/custom/<module id>/models/menu_action/): the packaged groups
// are materialized there because the Studio loader needs a file in the game
// search path, and a file the user puts there wins. The framework derives it
// from the module's resource root when that is declared; until then it is
// empty and no bare group resolves. The extraction target and the settings
// scan read it here.
void setActionModelDir(std::string directory);
const std::string &actionModelDir();

// The engine keys its model cache by the full game-relative name and caps
// that name at MAX_MODEL_NAME (64) bytes, too little for a name under the
// directory above: its 49 characters leave room for a ten-character stem. So
// the engine never loads from the directory. ActionModels.cpp keeps a short
// mirror of it here and a bare spelling resolves into the mirror.
constexpr std::string_view kActionModelLoadDir = "abc/abcui/cache/menu_action/";
// MAX_MODEL_NAME without its terminator.
constexpr std::size_t kActionModelNameMax = 63;

// The engine path of the action model group `Spelling`, a bare stem naming
// "<stem>.mdl" in the mirror. Empty for anything but a plain stem whose
// model the engine can name.
std::string actionModelLoadPath(std::string_view Spelling);

// Resolves a Studio path spelling the way the shipped cvars spell one: a
// value that already names a file (".../*.mdl") is used verbatim, anything
// else is read as the model's directory name under models/player/. An empty
// or unusable spelling falls back to `Fallback` (same grammar) and then to
// the generic player model, so the card is never left without a source.
std::string resolveModelPath(std::string_view Spelling,
                             std::string_view Fallback);

// Existence probe for one resolved Studio path (engine filesystem fact).
using ModelExists = std::function<bool(const std::string &)>;

// The main page's action group (cvar `abc_ui_menu_action_model`): a bare
// spelling names a group of the action model directory, loaded through its
// mirror ("display_male" -> "<action model load dir>display_male.mdl"); a
// .mdl path is used verbatim. Either way the path must be one the engine can
// name. Empty or invalid spelling selects the default group. A missing group
// yields an empty path: the card then draws its own model without the
// follow/merge chain.
std::string resolveActionModelPath(std::string_view Spelling,
                                   const ModelExists &Exists);

// The main page's weapon model (cvar `abc_ui_menu_weapon_model`): only a
// complete game-relative .mdl path with a directory is accepted, verbatim
// ("models/p_m16.mdl"). No filename aliases or inferred paths. Empty (or
// unusable) means no weapon. <model weapon> uses the same validation.
std::string resolveWeaponModelPath(std::string_view Spelling);

// The character card's source: `Spelling` (the menu's own override cvar; empty
// when unset) resolves through the grammar above with `PlayerModel` as the
// spelling fallback. A path the engine cannot see is not presented: it falls
// back to the shipped generic model, so the card never points at a source the
// preview renderer cannot load (an unloadable source used to leave the box
// blank/white). An empty probe means "no filesystem fact" - the resolved
// spelling is then trusted.
std::string resolveCardModelPath(std::string_view Spelling,
                                 std::string_view PlayerModel,
                                 const ModelExists &Exists);

// The card's camera, Studio lighting and background. LightDirection is the
// world-space light-ray direction, normalized by the draw bridge. The camera
// is explicit: Origin and Angles place and aim the view, nothing is fitted.
struct PreviewView {
  std::array<float, 3> Origin{120, 0, 0};
  std::array<float, 3> Angles{0, 180, 0};
  float VerticalFov = 40;
  float NearPlane = 1;
  float FarPlane = 4096;
  int Ambient = 96;
  int Shade = 96;
  std::array<float, 3> LightColor{1, 1, 1};
  std::array<float, 3> LightDirection{-1, 0, -1};
  std::array<float, 4> Background{0, 0, 0, 0};
  bool Lighting = true;
};

struct Options {
  PreviewView View;
  // The displayed player model ("src").
  std::string Source;
  // The action group that drives the displayed model and the weapon
  // ("action-src"): evaluated without rendering before each follow draw so
  // matching bones consume its saved pose. Empty keeps single-entity behaviour.
  std::string ActionSource;
  // The weapon model merged onto the action group ("weapon"). Empty = none.
  std::string WeaponSource;
  std::array<float, 3> Origin{};
  std::array<float, 3> Angles{};
  std::array<int, 3> RenderColor{255, 255, 255};
  std::array<int, 4> Controllers{};
  std::array<int, 2> Blending{};
  int Mouth = 0;
  int ColorMap = 0;
  int RenderMode = 0;
  int RenderFx = 0;
  int RenderAmount = 255;
  int Sequence = 0;
  int Skin = 0;
  int Body = 0;
  float Frame = 0;
  float FrameRate = 1;
  float Scale = 1;
  bool Animate = true;
  // Pointer drag rotation (the multiplayer settings' inspection preview):
  // while the primary button is held, horizontal motion turns the entity and
  // vertical motion tilts it within a clamped range.
  bool DragRotate = false;

  // Unknown DOM attributes (id/class/style) are not errors. Invalid known
  // attributes leave the previous value unchanged and return false.
  bool set(std::string_view Name, std::string_view Value);
  // A safe, relative Studio path: ends with ".mdl" (case-insensitive), no
  // "..", no backslash/colon/control bytes and no leading slash. Both
  // "models/..." content paths and the plugin's "abc/..." action groups pass.
  static bool isModelPath(std::string_view Path);
};

} // namespace modelpreview
