#pragma once

// The main menu character card's camera, model placement and light: the
// module config section `menu_preview` (module.abcui.gameui.menu_preview in
// abc/abcui/config.json), edited by the Settings > Menu card. This is the
// pure model both of them share - the defaults, the strict read gate, the
// serialization, the light angle <-> direction conversion, the slider table
// of the card and the persisted/applied/draft state - with no framework
// table and no DOM, so the offline suite links MenuPreviewSettings.cpp alone.

#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>

namespace menupreview {

// The section's local name. The host stores it under
// module.<module id>.<name>, so no other module can reach it.
constexpr const char *kSection = "menu_preview";
constexpr int kSchemaVersion = 1;

// Control ranges, in engine units and degrees. Each stays inside its <model>
// attribute's domain, so every value the section can hold projects onto the
// element unchanged.
constexpr float kCameraOriginLimit = 1024.0f;
constexpr float kModelOriginLimit = 256.0f;
constexpr float kAngleLimit = 180.0f;
// The light pitch stops one degree short of the poles, where the horizontal
// angle would lose its meaning and the angles would not round-trip.
constexpr float kLightPitchLimit = 89.0f;
constexpr int kLevelMax = 255;

// The section's values. The defaults are the main menu document's explicit
// attributes: the light ray is opposite the forward vector at the model's
// -8 degree yaw, so it shines straight onto the model's front.
struct Settings {
  std::array<float, 3> CameraOrigin{121.0f, -70.0f, 55.0f};
  std::array<float, 3> CameraAngles{16.0f, 150.0f, -3.0f};
  std::array<float, 3> ModelOrigin{0.0f, 0.0f, 0.0f};
  std::array<float, 3> ModelAngles{0.0f, -8.0f, 0.0f};
  // The world-space light-ray direction, unit length.
  std::array<float, 3> LightDirection{-0.990268f, 0.139173f, 0.0f};
  // Light RGB, 0-255.
  std::array<int, 3> LightColor{255, 245, 230};
  int Ambient = 96;
  int Shade = 110;

  bool operator==(const Settings &) const = default;
};

// --------------------------------------------------------- light angles ----

// The light angles in engine world axes (x forward at yaw 0, y left, z up):
// the ray travels towards (cos(pitch) cos(horizontal),
// cos(pitch) sin(horizontal), -sin(pitch)), so a positive pitch shines
// downwards. That is the direction <model light-direction> takes: the way
// the light travels, opposite the direction towards the light.
std::array<float, 3> lightDirection(float horizontal, float pitch);

struct LightAngles {
  float Horizontal = 0;
  float Pitch = 0;
};

// The inverse for any non-zero direction: horizontal in (-180, 180] (+180
// is the end that names the ray at the wrap), pitch in [-90, 90].
LightAngles lightAngles(const std::array<float, 3> &direction);

// ----------------------------------------------------------------- read ----

// Clamps every value into its control range and normalizes the direction
// (through its angles). True when a value moved.
bool clamp(Settings &Value);

// True when every value is finite and inside its control range and the
// direction is a unit vector: the gate before every write.
bool valid(const Settings &Value);

enum class ParseResult : uint8_t { Ok, Clamped, Invalid };

// Reads the stored section into Output. A document that is not a JSON
// object, a version other than 1, or a field of the wrong shape (a missing
// component, a non-numeric or non-finite value, a non-integral level) or a
// zero light direction rejects the whole section: Output keeps its value
// and Problem names the field. A finite value outside its control range is
// clamped; Problem then names the first such field. A field the section
// omits keeps its default; unknown fields are ignored.
ParseResult parse(std::string_view json, Settings &Output,
                  std::string &Problem);

// Writes the section as a JSON object. False, and Json untouched, when a
// value does not pass valid().
bool serialize(const Settings &Value, std::string &Json);

// ---------------------------------------------------------- <model> DOM ----

// The attribute spellings the card projects onto #player-model: three
// space-separated numbers each, the colour as 0-1 floats.
std::string vectorText(const std::array<float, 3> &Value);
std::string colorText(const std::array<int, 3> &Value);

// --------------------------------------------------------------- sliders ---

enum class Field : uint8_t {
  CameraOrigin,
  CameraAngles,
  ModelOrigin,
  ModelAngles,
  LightHorizontal,
  LightPitch,
  Ambient,
  Shade
};

// One slider of the Settings > Menu card: the value it edits, the group it
// sits under and its range. Every value of Settings is reached by exactly
// one slider, except the light colour (the <colorpicker>).
struct Slider {
  // DOM id stem of the control; the readout is "<id>-value".
  const char *Id;
  // The column of the card (0 or 1) and the label token of the group
  // header; consecutive sliders of one column and group share a header.
  uint8_t Column;
  const char *Group;
  // Label token of the axis.
  const char *Label;
  Field Target;
  // Component of the vector targets, zero otherwise.
  uint8_t Index;
  // The readout carries the degree unit.
  bool Degrees;
  float Min;
  float Max;
};

constexpr Slider kSliders[] = {
    {"camera-x", 0, "Settings_MenuPreviewCameraPosition",
     "Settings_MenuPreviewAxisX", Field::CameraOrigin, 0, false,
     -kCameraOriginLimit, kCameraOriginLimit},
    {"camera-y", 0, "Settings_MenuPreviewCameraPosition",
     "Settings_MenuPreviewAxisY", Field::CameraOrigin, 1, false,
     -kCameraOriginLimit, kCameraOriginLimit},
    {"camera-z", 0, "Settings_MenuPreviewCameraPosition",
     "Settings_MenuPreviewAxisZ", Field::CameraOrigin, 2, false,
     -kCameraOriginLimit, kCameraOriginLimit},
    {"camera-pitch", 0, "Settings_MenuPreviewCameraAngles",
     "Settings_MenuPreviewPitch", Field::CameraAngles, 0, true, -kAngleLimit,
     kAngleLimit},
    {"camera-yaw", 0, "Settings_MenuPreviewCameraAngles",
     "Settings_MenuPreviewYaw", Field::CameraAngles, 1, true, -kAngleLimit,
     kAngleLimit},
    {"camera-roll", 0, "Settings_MenuPreviewCameraAngles",
     "Settings_MenuPreviewRoll", Field::CameraAngles, 2, true, -kAngleLimit,
     kAngleLimit},
    // -179..180: the horizontal angle is periodic and +180 names the wrap,
    // so leaving -180 out keeps the slider from jumping across its whole
    // range for the same ray.
    {"light-horizontal", 0, "Settings_MenuPreviewLightAngle",
     "Settings_MenuPreviewHorizontal", Field::LightHorizontal, 0, true,
     -kAngleLimit + 1, kAngleLimit},
    {"light-pitch", 0, "Settings_MenuPreviewLightAngle",
     "Settings_MenuPreviewPitch", Field::LightPitch, 0, true, -kLightPitchLimit,
     kLightPitchLimit},
    {"model-x", 1, "Settings_MenuPreviewModelPosition",
     "Settings_MenuPreviewAxisX", Field::ModelOrigin, 0, false,
     -kModelOriginLimit, kModelOriginLimit},
    {"model-y", 1, "Settings_MenuPreviewModelPosition",
     "Settings_MenuPreviewAxisY", Field::ModelOrigin, 1, false,
     -kModelOriginLimit, kModelOriginLimit},
    {"model-z", 1, "Settings_MenuPreviewModelPosition",
     "Settings_MenuPreviewAxisZ", Field::ModelOrigin, 2, false,
     -kModelOriginLimit, kModelOriginLimit},
    {"model-pitch", 1, "Settings_MenuPreviewModelRotation",
     "Settings_MenuPreviewPitch", Field::ModelAngles, 0, true, -kAngleLimit,
     kAngleLimit},
    {"model-yaw", 1, "Settings_MenuPreviewModelRotation",
     "Settings_MenuPreviewYaw", Field::ModelAngles, 1, true, -kAngleLimit,
     kAngleLimit},
    {"model-roll", 1, "Settings_MenuPreviewModelRotation",
     "Settings_MenuPreviewRoll", Field::ModelAngles, 2, true, -kAngleLimit,
     kAngleLimit},
    {"ambient", 1, "Settings_MenuPreviewLevels", "Settings_MenuPreviewAmbient",
     Field::Ambient, 0, false, 0, kLevelMax},
    {"shade", 1, "Settings_MenuPreviewLevels", "Settings_MenuPreviewShade",
     Field::Shade, 0, false, 0, kLevelMax},
};
constexpr std::size_t kSliderCount = std::size(kSliders);

// The slider's value in a settings block (integral, inside its range).
float sliderValue(const Settings &Value, const Slider &Slider);
// Edits the value a slider addresses. The value is clamped into the slider's
// range; the light angles replace the direction. True when it moved.
bool setSliderValue(Settings &Value, const Slider &Slider, float New);

// ----------------------------------------------------------------- state ---

// Writes the section's JSON (the host config call); true when it was stored.
using Writer = std::function<bool(const std::string &Json)>;

// The process-lifetime state behind the main menu projection and the
// Settings > Menu card.
struct State {
  // The last values known to be on disk (the defaults while the section is
  // absent): the baseline the card's edits are measured against.
  Settings Persisted;
  // What the main menu shows: the last load or apply.
  Settings Applied;
  // The card's pending edits.
  Settings Draft;
  // The stored section could not be read (an unreadable file or a damaged
  // section): nothing is written this session.
  bool ReadOnly = false;
  // An apply took effect but its write failed: the values live in this
  // session only.
  bool Unsaved = false;
};

State &state();

// The card owes a save: edits that differ from the persisted values, or
// values a failed write left unsaved.
bool dirty();

// Applies the draft through `write`. The values always take effect for this
// session; the return says whether they were saved. A read-only session
// never writes, a draft that is not dirty saves nothing.
bool apply(const Writer &write);

// Discards the card's edits: the session returns to the persisted values.
void discardDraft();

// Stages the documented defaults as the card's draft.
void resetDraft();

} // namespace menupreview
