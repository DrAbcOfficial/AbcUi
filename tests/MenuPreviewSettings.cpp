// abc_ui_menu_preview_settings - the main menu character card's config model
// (module.abcui.gameui.menu_preview): the documented defaults, the slider
// table and its ranges, the strict read gate, the serialization, the light
// angle <-> direction conversion, the applied/draft transaction, and the
// coupling to the <model> element and the shipped documents the card
// projects onto. Pure: no engine, no game installation, no RmlUi.
#include "gameui/src/settings/MenuPreviewSettings.h"
#include "rmlui/modelpreview/Options.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include "framework/TestEnvironment.h"
#include "framework/TestHarness.h"

namespace {

using menupreview::Field;
using menupreview::ParseResult;
using menupreview::Settings;
using menupreview::Slider;

// The structure the issue documents, with its documented defaults.
const char *const kDocumented = R"({
  "version": 1,
  "camera_origin": [121.0, -70.0, 55.0],
  "camera_angles": [16.0, 150.0, -3.0],
  "model_origin": [0.0, 0.0, 0.0],
  "model_angles": [0.0, -8.0, 0.0],
  "light_direction": [-0.990268, 0.139173, 0.0],
  "light_color": [255, 245, 230],
  "ambient": 96,
  "shade": 110
})";

bool near(const std::array<float, 3> &Value,
          const std::array<float, 3> &Expected, float Tolerance) {
  for (std::size_t i = 0; i < 3; ++i)
    if (!(std::fabs(Value[i] - Expected[i]) <= Tolerance))
      return false;
  return true;
}

// Every field equal; the direction within the six decimals it is stored as.
bool sameSettings(const Settings &A, const Settings &B) {
  return A.CameraOrigin == B.CameraOrigin && A.CameraAngles == B.CameraAngles &&
         A.ModelOrigin == B.ModelOrigin && A.ModelAngles == B.ModelAngles &&
         A.LightColor == B.LightColor && A.Ambient == B.Ambient &&
         A.Shade == B.Shade && near(A.LightDirection, B.LightDirection, 1e-5f);
}

const Slider &sliderByField(Field Target, uint8_t Index = 0) {
  for (const Slider &Candidate : menupreview::kSliders)
    if (Candidate.Target == Target && Candidate.Index == Index)
      return Candidate;
  return menupreview::kSliders[0];
}

// A value away from every default. The fractions are exactly representable,
// so a stored round trip has to reproduce each one bit for bit.
Settings edited() {
  Settings Value;
  Value.CameraOrigin = {123.5f, -70.25f, 55.125f};
  Value.CameraAngles = {20.5f, 149.75f, -3.5f};
  Value.ModelOrigin = {1.5f, -2.25f, 3.75f};
  Value.ModelAngles = {4.5f, -8.25f, 90.0f};
  Value.LightDirection = menupreview::lightDirection(45.0f, 30.0f);
  Value.LightColor = {12, 34, 56};
  Value.Ambient = 200;
  Value.Shade = 20;
  return Value;
}

// Every value at the end of its slider range.
Settings atLimits(bool High) {
  Settings Value;
  for (const Slider &Slider : menupreview::kSliders)
    menupreview::setSliderValue(Value, Slider, High ? Slider.Max : Slider.Min);
  return Value;
}

std::string readFile(const std::filesystem::path &Path) {
  std::ifstream File(Path, std::ios::binary);
  if (!File)
    return {};
  return {std::istreambuf_iterator<char>(File),
          std::istreambuf_iterator<char>()};
}

// The value of an attribute's first occurrence in a document's text.
std::string attribute(const std::string &Text, const char *Name) {
  const std::string Key = std::string(Name) + "=\"";
  const std::size_t At = Text.find(Key);
  if (At == std::string::npos)
    return {};
  const std::size_t End = Text.find('"', At + Key.size());
  return Text.substr(At + Key.size(), End == std::string::npos
                                          ? std::string::npos
                                          : End - At - Key.size());
}

// "module/Token = text" lines of one language table.
std::set<std::string> tableKeys(const std::filesystem::path &Path) {
  std::set<std::string> Keys;
  std::ifstream File(Path, std::ios::binary);
  std::string Line;
  while (std::getline(File, Line)) {
    const std::size_t Separator = Line.find(" = ");
    if (Separator != std::string::npos)
      Keys.insert(Line.substr(0, Separator));
  }
  return Keys;
}

std::set<std::string> cardTokens() {
  std::set<std::string> Tokens = {
      "Settings_SecMenuPreview",        "Settings_MenuPreviewLightColor",
      "Settings_MenuPreviewDegreeUnit", "Settings_MenuPreviewReset",
      "Settings_MenuPreviewPending",    "Settings_MenuPreviewSaved",
      "Settings_MenuPreviewSaveFailed", "Settings_MenuPreviewReadOnly"};
  for (const Slider &Slider : menupreview::kSliders) {
    Tokens.insert(Slider.Group);
    Tokens.insert(Slider.Label);
  }
  return Tokens;
}
} // namespace

int main() {
  // -- the defaults are the documented values ------------------------------
  {
    TEST_CASE("defaults");
    const Settings Defaults;
    CHECK(Defaults.CameraOrigin == (std::array<float, 3>{121, -70, 55}));
    CHECK(Defaults.CameraAngles == (std::array<float, 3>{16, 150, -3}));
    CHECK(Defaults.ModelOrigin == (std::array<float, 3>{0, 0, 0}));
    CHECK(Defaults.ModelAngles == (std::array<float, 3>{0, -8, 0}));
    CHECK(near(Defaults.LightDirection,
               std::array<float, 3>{-0.990268f, 0.139173f, 0}, 1e-7f));
    CHECK(Defaults.LightColor == (std::array<int, 3>{255, 245, 230}));
    CHECK(Defaults.Ambient == 96);
    CHECK(Defaults.Shade == 110);
    CHECK(menupreview::valid(Defaults));
    // The section's local name: the host stores it as
    // module.<module id>.menu_preview.
    CHECK(std::string_view(menupreview::kSection) == "menu_preview");
    CHECK(menupreview::kSchemaVersion == 1);
  }

  // -- the <model> attribute spellings -------------------------------------
  {
    TEST_CASE("attribute-spellings");
    CHECK(menupreview::vectorText({121, -70, 55}) == "121 -70 55");
    CHECK(menupreview::vectorText({0, 0, -0.0f}) == "0 0 0");
    CHECK(menupreview::vectorText({0.5f, -8, 123.456789f}) == "0.5 -8 123.457");
    CHECK(menupreview::colorText({255, 245, 230}) == "1 0.960784 0.901961");
    CHECK(menupreview::colorText({0, 0, 0}) == "0 0 0");
    CHECK(menupreview::colorText({128, 64, 32}) == "0.501961 0.25098 0.12549");
  }

  // -- the slider table reaches every value --------------------------------
  {
    TEST_CASE("slider-table");
    CHECK(menupreview::kSliderCount == 16);
    std::set<std::string> Ids;
    std::set<std::string> Addressed;
    for (const Slider &Slider : menupreview::kSliders) {
      CHECK(Ids.insert(Slider.Id).second);
      CHECK(Slider.Min < Slider.Max);
      CHECK(Slider.Column <= 1);
      // Every default lies inside its slider's range.
      const float Value = menupreview::sliderValue(Settings{}, Slider);
      CHECK(Slider.Min <= Value && Value <= Slider.Max);
      Addressed.insert(std::to_string(static_cast<int>(Slider.Target)) + ":" +
                       std::to_string(Slider.Index));
    }
    CHECK(Addressed.size() == 16);
    // The three camera sliders move the camera alone, the three model
    // sliders the model alone, and each light angle has its own slider.
    for (int Component = 0; Component < 3; ++Component) {
      CHECK(sliderByField(Field::CameraOrigin, static_cast<uint8_t>(Component))
                .Target == Field::CameraOrigin);
      CHECK(sliderByField(Field::ModelOrigin, static_cast<uint8_t>(Component))
                .Target == Field::ModelOrigin);
    }
    CHECK(sliderByField(Field::LightHorizontal).Max ==
          menupreview::kAngleLimit);
    CHECK(sliderByField(Field::LightPitch).Max ==
          menupreview::kLightPitchLimit);
    CHECK(sliderByField(Field::LightPitch).Min ==
          -menupreview::kLightPitchLimit);
    CHECK(sliderByField(Field::Ambient).Max == menupreview::kLevelMax);
    CHECK(sliderByField(Field::Shade).Max == menupreview::kLevelMax);
  }

  // -- every value the section holds is accepted by the <model> validators --
  {
    TEST_CASE("model-element-accepts-the-projection");
    for (const Settings &Value :
         {Settings{}, edited(), atLimits(false), atLimits(true)}) {
      modelpreview::Options Element;
      CHECK(Element.set("camera-origin",
                        menupreview::vectorText(Value.CameraOrigin)));
      CHECK(Element.set("camera-angles",
                        menupreview::vectorText(Value.CameraAngles)));
      CHECK(Element.set("origin", menupreview::vectorText(Value.ModelOrigin)));
      CHECK(Element.set("angles", menupreview::vectorText(Value.ModelAngles)));
      CHECK(Element.set("light-direction",
                        menupreview::vectorText(Value.LightDirection)));
      CHECK(
          Element.set("light-color", menupreview::colorText(Value.LightColor)));
      CHECK(Element.set("ambient", std::to_string(Value.Ambient)));
      CHECK(Element.set("shade", std::to_string(Value.Shade)));
      // What lands is what the section holds.
      CHECK(near(Element.View.Origin, Value.CameraOrigin, 1e-4f));
      CHECK(near(Element.View.Angles, Value.CameraAngles, 1e-4f));
      CHECK(near(Element.Origin, Value.ModelOrigin, 1e-4f));
      CHECK(near(Element.Angles, Value.ModelAngles, 1e-4f));
      CHECK(Element.View.Ambient == Value.Ambient);
      CHECK(Element.View.Shade == Value.Shade);
    }
    // The ambient range is the whole 0-255 the card exposes; past it the
    // element refuses the attribute.
    modelpreview::Options Element;
    CHECK(Element.set("ambient", "255"));
    CHECK(Element.View.Ambient == 255);
    CHECK(!Element.set("ambient", "256"));
    CHECK(!Element.set("ambient", "-1"));
  }

  // -- light angles and the direction are one value ------------------------
  {
    TEST_CASE("default-light-is-172-degrees-level");
    const menupreview::LightAngles Angles =
        menupreview::lightAngles(Settings{}.LightDirection);
    CHECK_NEAR(Angles.Horizontal, 172.0, 1e-3);
    CHECK_NEAR(Angles.Pitch, 0.0, 1e-3);
    // The card's angle sliders show exactly those angles for the default.
    CHECK_NEAR(menupreview::sliderValue(Settings{},
                                        sliderByField(Field::LightHorizontal)),
               172.0, 1e-6);
    CHECK_NEAR(
        menupreview::sliderValue(Settings{}, sliderByField(Field::LightPitch)),
        0.0, 1e-6);
    // And the vector those angles name is the default one.
    CHECK(near(menupreview::lightDirection(172.0f, 0.0f),
               Settings{}.LightDirection, 1e-6f));
  }
  {
    TEST_CASE("light-angles-round-trip");
    // A positive pitch is a ray that travels downwards (towards -z).
    CHECK(near(menupreview::lightDirection(0.0f, 0.0f),
               std::array<float, 3>{1, 0, 0}, 1e-6f));
    CHECK(near(menupreview::lightDirection(90.0f, 0.0f),
               std::array<float, 3>{0, 1, 0}, 1e-6f));
    CHECK(near(menupreview::lightDirection(172.0f, 30.0f),
               std::array<float, 3>{-0.8576f, 0.1205f, -0.5f}, 1e-4f));
    for (const float Horizontal : {-180.0f, -135.0f, -90.0f, -45.0f, 0.0f,
                                   30.0f, 90.0f, 172.0f, 179.0f, 180.0f})
      for (const float Pitch : {-89.0f, -45.0f, 0.0f, 30.0f, 89.0f}) {
        const std::array<float, 3> Direction =
            menupreview::lightDirection(Horizontal, Pitch);
        // The horizontal angle is periodic and +180 is the end of its range
        // that names the wrap: -180 reads back as +180.
        const float Named = Horizontal <= -180.0f ? 180.0f : Horizontal;
        const menupreview::LightAngles Angles =
            menupreview::lightAngles(Direction);
        CHECK_NEAR(Angles.Horizontal, Named, 1e-3);
        CHECK_NEAR(Angles.Pitch, Pitch, 1e-3);
        // Through the six decimals the direction is stored as. They place a
        // steep direction's horizontal angle to about a tenth of a degree,
        // far inside the sliders' one-degree step: the sliders show the
        // same whole angles again.
        Settings Stored;
        Stored.LightDirection = Direction;
        std::string Json;
        CHECK(menupreview::serialize(Stored, Json));
        Settings Loaded;
        std::string Problem;
        CHECK(menupreview::parse(Json, Loaded, Problem) == ParseResult::Ok);
        CHECK_NEAR(menupreview::sliderValue(
                       Loaded, sliderByField(Field::LightHorizontal)),
                   Named, 1e-6);
        CHECK_NEAR(
            menupreview::sliderValue(Loaded, sliderByField(Field::LightPitch)),
            Pitch, 1e-6);
        const menupreview::LightAngles Again =
            menupreview::lightAngles(Loaded.LightDirection);
        CHECK_NEAR(Again.Horizontal, Named, 0.25);
        CHECK_NEAR(Again.Pitch, Pitch, 0.25);
      }
    // The horizontal slider stops at -179: its other end, 180, already
    // names that ray, so a drag to an end never jumps across the range.
    const Slider &Horizontal = sliderByField(Field::LightHorizontal);
    CHECK(Horizontal.Min == -menupreview::kAngleLimit + 1);
    CHECK(Horizontal.Max == menupreview::kAngleLimit);
    Settings Value;
    CHECK(menupreview::setSliderValue(Value, Horizontal, -5000));
    CHECK(menupreview::sliderValue(Value, Horizontal) == -179);
    CHECK(near(Value.LightDirection, menupreview::lightDirection(-179.0f, 0.0f),
               1e-6f));
    // A stored vector at the wrap itself reads as +180 whatever end named
    // it when it was written.
    CHECK_NEAR(
        menupreview::lightAngles(menupreview::lightDirection(-180.0f, 30.0f))
            .Horizontal,
        180.0, 1e-3);
  }
  {
    TEST_CASE("stored-light-at-the-pitch-limit-is-not-clamped");
    // A light saved at a slider limit reads back as the same value, not as
    // an out-of-range one: the six stored decimals cannot be more exact than
    // the limit itself.
    Settings Value;
    CHECK(menupreview::setSliderValue(
        Value, sliderByField(Field::LightHorizontal), 179));
    CHECK(menupreview::setSliderValue(Value, sliderByField(Field::LightPitch),
                                      menupreview::kLightPitchLimit));
    std::string Json;
    CHECK(menupreview::serialize(Value, Json));
    Settings Loaded;
    std::string Problem = "stale";
    CHECK(menupreview::parse(Json, Loaded, Problem) == ParseResult::Ok);
    CHECK(Problem.empty());
    CHECK(!menupreview::clamp(Loaded));
    CHECK(menupreview::valid(Loaded));
  }

  // -- reading: the documented structure ------------------------------------
  {
    TEST_CASE("parse-documented-section");
    Settings Value;
    std::string Problem = "stale";
    CHECK(menupreview::parse(kDocumented, Value, Problem) == ParseResult::Ok);
    CHECK(Problem.empty());
    CHECK(sameSettings(Value, Settings{}));
  }
  {
    TEST_CASE("parse-missing-and-unknown-fields");
    // A field the section omits keeps the base value; unknown fields are
    // ignored whatever they hold.
    Settings Value = edited();
    std::string Problem = "stale";
    CHECK(menupreview::parse(R"({"version": 1})", Value, Problem) ==
          ParseResult::Ok);
    CHECK(Problem.empty());
    CHECK(sameSettings(Value, edited()));
    CHECK(menupreview::parse(
              R"({"version": 1, "future_field": {"a": [1, "x"]},
                  "ambient": 120, "light_direction": [1, 0, 0]})",
              Value, Problem) == ParseResult::Ok);
    CHECK(Value.Ambient == 120);
    CHECK(Value.Shade == 20);
    CHECK(near(Value.LightDirection, std::array<float, 3>{1, 0, 0}, 1e-6f));
    CHECK(Value.CameraOrigin == edited().CameraOrigin);
  }
  {
    TEST_CASE("parse-normalizes-a-stored-direction");
    Settings Value;
    std::string Problem;
    // The direction is named, not its magnitude: the same ray at another
    // length reads as the default, without a clamp.
    CHECK(menupreview::parse(
              R"({"version": 1, "light_direction": [-99.0268, 13.9173, 0]})",
              Value, Problem) == ParseResult::Ok);
    CHECK(Problem.empty());
    CHECK(near(Value.LightDirection, Settings{}.LightDirection, 1e-6f));
    CHECK(menupreview::parse(
              R"({"version": 1, "light_direction": [1e300, 1e300, 0]})", Value,
              Problem) == ParseResult::Ok);
    CHECK(near(Value.LightDirection,
               std::array<float, 3>{0.707107f, 0.707107f, 0}, 1e-5f));
    // A unit vector is kept as stored.
    CHECK(menupreview::parse(
              R"({"version": 1, "light_direction": [0.6, 0.8, 0.0]})", Value,
              Problem) == ParseResult::Ok);
    CHECK(
        near(Value.LightDirection, std::array<float, 3>{0.6f, 0.8f, 0}, 1e-6f));
  }

  // -- reading: a wrong shape rejects the whole section --------------------
  {
    TEST_CASE("parse-rejects-bad-shapes");
    const auto rejects = [&](std::string_view Json) {
      Settings Value = edited();
      std::string Problem = "stale";
      const ParseResult Result = menupreview::parse(Json, Value, Problem);
      // The section falls back as a whole: nothing of the damaged value is
      // used, and the reason is named.
      EXPECT_NAMED(Result == ParseResult::Invalid &&
                       sameSettings(Value, edited()) && !Problem.empty() &&
                       Problem != "stale",
                   std::string(Json).c_str());
    };
    rejects("");
    rejects("92");
    rejects("[]");
    rejects("{}");
    rejects(R"({"version": 2})");
    rejects(R"({"version": "1"})");
    rejects(R"({"version": 1, "ambient": 96)");
    // A level that is not a whole number, or not a number at all.
    rejects(R"({"version": 1, "ambient": "96"})");
    rejects(R"({"version": 1, "ambient": "96", "shade": 200})");
    rejects(R"({"version": 1, "ambient": 96.5})");
    rejects(R"({"version": 1, "shade": -3.5})");
    rejects(R"({"version": 1, "ambient": true})");
    rejects(R"({"version": 1, "ambient": null})");
    // A vector with the wrong count or a non-numeric component.
    rejects(R"({"version": 1, "camera_origin": [1, 2]})");
    rejects(R"({"version": 1, "camera_origin": [1, 2, 3, 4]})");
    rejects(R"({"version": 1, "camera_origin": "1 2 3"})");
    rejects(R"({"version": 1, "camera_origin": 121})");
    rejects(
        R"({"version": 1, "camera_origin": {"x": 121, "y": -70, "z": 55}})");
    rejects(R"({"version": 1, "camera_angles": [16, "150", -3]})");
    rejects(R"({"version": 1, "model_origin": [0, null, 0]})");
    rejects(R"({"version": 1, "model_angles": [0, -8, true]})");
    // The colour: three whole numbers.
    rejects(R"({"version": 1, "light_color": [255, 245]})");
    rejects(R"({"version": 1, "light_color": [255, 245, 230.5]})");
    rejects(R"({"version": 1, "light_color": "fff5e6"})");
    rejects(R"({"version": 1, "light_color": [255, 245, "230"]})");
    // A light that shines nowhere.
    rejects(R"({"version": 1, "light_direction": [0, 0, 0]})");
    rejects(R"({"version": 1, "light_direction": [0, -0.0, 0]})");
    rejects(R"({"version": 1, "light_direction": [1, 0]})");
    // NaN and infinity are not values, wherever they appear.
    rejects(R"({"version": 1, "ambient": NaN})");
    rejects(R"({"version": 1, "ambient": Infinity})");
    rejects(R"({"version": 1, "shade": -Infinity})");
    rejects(R"({"version": 1, "camera_origin": [1e999, 0, 0]})");
    rejects(R"({"version": 1, "camera_origin": [NaN, 0, 0]})");
    rejects(R"({"version": 1, "camera_angles": [0, -Infinity, 0]})");
    rejects(R"({"version": 1, "light_direction": [NaN, 1, 0]})");
    rejects(R"({"version": 1, "light_direction": [Infinity, 1, 0]})");
    rejects(R"({"version": 1, "light_color": [255, 245, 1e999]})");
  }
  {
    TEST_CASE("parse-reject-keeps-the-caller-value");
    // The caller's value is the fallback: a rejected section leaves it
    // exactly as it was, a base other than the defaults included.
    Settings Value = edited();
    std::string Problem;
    CHECK(menupreview::parse(R"({"version": 1, "ambient": "96"})", Value,
                             Problem) == ParseResult::Invalid);
    CHECK(sameSettings(Value, edited()));
    CHECK(Problem.find("ambient") != std::string::npos);
    // A missing field is not a rejected one.
    CHECK(menupreview::parse(R"({"version": 1, "shade": 7})", Value, Problem) ==
          ParseResult::Ok);
    CHECK(Value.Ambient == edited().Ambient);
    CHECK(Value.Shade == 7);
  }

  // -- reading: a finite value outside its range is clamped ----------------
  {
    TEST_CASE("parse-clamps-out-of-range");
    Settings Value;
    std::string Problem;
    CHECK(menupreview::parse(
              R"({"version": 1, "ambient": 300, "shade": -5,
                  "camera_origin": [5000, -70, 55],
                  "camera_angles": [16, 270, -3],
                  "model_origin": [0, 0, -9999],
                  "light_color": [300, 245, -1]})",
              Value, Problem) == ParseResult::Clamped);
    CHECK(!Problem.empty());
    CHECK(Value.Ambient == 255);
    CHECK(Value.Shade == 0);
    CHECK(Value.CameraOrigin ==
          (std::array<float, 3>{menupreview::kCameraOriginLimit, -70, 55}));
    CHECK(Value.CameraAngles ==
          (std::array<float, 3>{16, menupreview::kAngleLimit, -3}));
    CHECK(Value.ModelOrigin ==
          (std::array<float, 3>{0, 0, -menupreview::kModelOriginLimit}));
    CHECK(Value.LightColor == (std::array<int, 3>{255, 245, 0}));
    // The fields inside their range are untouched, and the result is a
    // value the card can show and save.
    CHECK(Value.ModelAngles == Settings{}.ModelAngles);
    CHECK(near(Value.LightDirection, Settings{}.LightDirection, 1e-6f));
    CHECK(menupreview::valid(Value));
    std::string Json;
    CHECK(menupreview::serialize(Value, Json));
    // An in-range section reports no clamp and names nothing.
    CHECK(menupreview::parse(kDocumented, Value, Problem) == ParseResult::Ok);
    CHECK(Problem.empty());
  }
  {
    TEST_CASE("parse-clamps-a-steep-light");
    // Straight down is past the pitch slider: the nearest angle it can show
    // is kept.
    Settings Value;
    std::string Problem;
    CHECK(menupreview::parse(R"({"version": 1, "light_direction": [0, 0, -1]})",
                             Value, Problem) == ParseResult::Clamped);
    CHECK(Problem.find("light_direction") != std::string::npos);
    CHECK(near(Value.LightDirection,
               menupreview::lightDirection(0.0f, menupreview::kLightPitchLimit),
               1e-5f));
    CHECK(menupreview::valid(Value));
  }

  // -- clamp: the gate for values that did not come from the file ----------
  {
    TEST_CASE("clamp");
    Settings Value = edited();
    Value.CameraOrigin = {5000, -5000, 0};
    Value.CameraAngles = {400, -400, 0};
    Value.ModelOrigin = {9999, -9999, 0};
    Value.ModelAngles = {-999, 999, 0};
    Value.Ambient = 999;
    Value.Shade = -999;
    Value.LightColor = {300, -1, 128};
    Value.LightDirection = {0, 0, 0};
    CHECK(menupreview::clamp(Value));
    CHECK(Value.CameraOrigin ==
          (std::array<float, 3>{menupreview::kCameraOriginLimit,
                                -menupreview::kCameraOriginLimit, 0}));
    CHECK(Value.CameraAngles ==
          (std::array<float, 3>{menupreview::kAngleLimit,
                                -menupreview::kAngleLimit, 0}));
    CHECK(Value.ModelOrigin ==
          (std::array<float, 3>{menupreview::kModelOriginLimit,
                                -menupreview::kModelOriginLimit, 0}));
    CHECK(Value.ModelAngles ==
          (std::array<float, 3>{-menupreview::kAngleLimit,
                                menupreview::kAngleLimit, 0}));
    CHECK(Value.Ambient == 255);
    CHECK(Value.Shade == 0);
    CHECK(Value.LightColor == (std::array<int, 3>{255, 0, 128}));
    // A light that shines nowhere is the default light.
    CHECK(near(Value.LightDirection, Settings{}.LightDirection, 1e-6f));
    CHECK(menupreview::valid(Value));
    CHECK(!menupreview::clamp(Value));
    // A stored vector of another length is the ray it names; a steep one is
    // pulled into the pitch range.
    Value.LightDirection = {4, 0, 0};
    CHECK(menupreview::clamp(Value));
    CHECK(near(Value.LightDirection, std::array<float, 3>{1, 0, 0}, 1e-6f));
    Value.LightDirection = {0, 0, -1};
    CHECK(menupreview::clamp(Value));
    CHECK(near(Value.LightDirection,
               menupreview::lightDirection(0.0f, menupreview::kLightPitchLimit),
               1e-5f));
  }

  // -- writing ---------------------------------------------------------------
  {
    TEST_CASE("serialize-round-trip");
    std::string Json;
    CHECK(menupreview::serialize(Settings{}, Json));
    CHECK(Json.find("\"version\":1") != std::string::npos);
    CHECK(Json.find("\"light_color\":[255,245,230]") != std::string::npos);
    CHECK(Json.find("\"ambient\":96") != std::string::npos);
    CHECK(Json.find("\"shade\":110") != std::string::npos);
    CHECK(Json.find("\"light_direction\":[-0.990268,0.139173,0]") !=
          std::string::npos);
    Settings Loaded;
    std::string Problem;
    CHECK(menupreview::parse(Json, Loaded, Problem) == ParseResult::Ok);
    CHECK(sameSettings(Loaded, Settings{}));

    Json.clear();
    CHECK(menupreview::serialize(edited(), Json));
    CHECK(Json.find("\"camera_origin\":[123.5,-70.25,55.125]") !=
          std::string::npos);
    CHECK(Json.find("\"light_color\":[12,34,56]") != std::string::npos);
    CHECK(Json.find("\"ambient\":200") != std::string::npos);
    CHECK(Json.find("\"shade\":20") != std::string::npos);
    CHECK(menupreview::parse(Json, Loaded, Problem) == ParseResult::Ok);
    CHECK(sameSettings(Loaded, edited()));
    // The section is a JSON object, as the host requires of a write.
    CHECK(Json.front() == '{');
    CHECK(Json.back() == '}');
  }
  {
    TEST_CASE("serialize-refuses-invalid-values");
    const auto refuses = [&](const Settings &Value) {
      std::string Json = "untouched";
      EXPECT_NAMED(!menupreview::serialize(Value, Json) && Json == "untouched",
                   "an invalid value is never written");
    };
    Settings Value = edited();
    Value.CameraOrigin[0] = std::nanf("");
    refuses(Value);
    Value = edited();
    Value.CameraAngles[2] = 181.0f;
    refuses(Value);
    Value = edited();
    Value.ModelOrigin[1] = menupreview::kModelOriginLimit + 0.5f;
    refuses(Value);
    Value = edited();
    Value.Ambient = 256;
    refuses(Value);
    Value = edited();
    Value.Shade = -1;
    refuses(Value);
    Value = edited();
    Value.LightColor = {0, 256, 0};
    refuses(Value);
    Value = edited();
    Value.LightDirection = {0, 0, 0};
    refuses(Value);
    Value = edited();
    Value.LightDirection = {2, 0, 0};
    refuses(Value);
    Value = edited();
    Value.LightDirection = {0, 0, -1};
    refuses(Value);
    Value = edited();
    Value.LightDirection[0] = std::nanf("");
    refuses(Value);
  }

  // -- the sliders edit their own value and nothing else --------------------
  {
    TEST_CASE("slider-edits-are-independent");
    Settings Value;
    Settings Expected;
    // Camera and model are two groups: each slider moves its own value and
    // leaves the other group alone, so both can be combined freely.
    CHECK(menupreview::setSliderValue(
        Value, sliderByField(Field::CameraOrigin, 0), 200));
    Expected.CameraOrigin[0] = 200;
    CHECK(Value == Expected);
    CHECK(menupreview::setSliderValue(
        Value, sliderByField(Field::CameraAngles, 1), -30));
    Expected.CameraAngles[1] = -30;
    CHECK(Value == Expected);
    CHECK(menupreview::setSliderValue(
        Value, sliderByField(Field::ModelOrigin, 2), 12));
    Expected.ModelOrigin[2] = 12;
    CHECK(Value == Expected);
    CHECK(menupreview::setSliderValue(
        Value, sliderByField(Field::ModelAngles, 2), 45));
    Expected.ModelAngles[2] = 45;
    CHECK(Value == Expected);
    CHECK(
        menupreview::setSliderValue(Value, sliderByField(Field::Ambient), 255));
    Expected.Ambient = 255;
    CHECK(Value == Expected);
    CHECK(menupreview::setSliderValue(Value, sliderByField(Field::Shade), 20));
    Expected.Shade = 20;
    CHECK(Value == Expected);
    // The readout follows the slider that owns each value.
    CHECK(menupreview::sliderValue(
              Value, sliderByField(Field::CameraOrigin, 0)) == 200);
    CHECK(menupreview::sliderValue(
              Value, sliderByField(Field::CameraAngles, 1)) == -30);
    CHECK(menupreview::sliderValue(Value,
                                   sliderByField(Field::ModelOrigin, 2)) == 12);
    CHECK(menupreview::sliderValue(Value,
                                   sliderByField(Field::ModelAngles, 2)) == 45);
    // A value past the slider's ends lands on the end.
    CHECK(menupreview::setSliderValue(
        Value, sliderByField(Field::CameraOrigin, 0), 5000));
    CHECK(Value.CameraOrigin[0] == menupreview::kCameraOriginLimit);
    CHECK(menupreview::setSliderValue(
        Value, sliderByField(Field::CameraAngles, 1), -5000));
    CHECK(Value.CameraAngles[1] == -menupreview::kAngleLimit);
    CHECK(
        menupreview::setSliderValue(Value, sliderByField(Field::Ambient), -1));
    CHECK(Value.Ambient == 0);
    // A value that is not a number is not an edit.
    Expected = Value;
    CHECK(!menupreview::setSliderValue(Value, sliderByField(Field::Shade),
                                       std::nanf("")));
    CHECK(!menupreview::setSliderValue(Value, sliderByField(Field::Shade),
                                       INFINITY));
    CHECK(Value == Expected);
    // The value the slider already shows is not an edit either - also when
    // the stored number lies between two of its steps.
    CHECK(!menupreview::setSliderValue(Value, sliderByField(Field::Shade), 20));
    Value.CameraOrigin[1] = 121.6f;
    Expected = Value;
    CHECK(!menupreview::setSliderValue(
        Value, sliderByField(Field::CameraOrigin, 1), 122));
    CHECK(Value == Expected);
    CHECK(menupreview::setSliderValue(
        Value, sliderByField(Field::CameraOrigin, 1), 123));
    CHECK(Value.CameraOrigin[1] == 123);
  }
  {
    TEST_CASE("light-sliders-turn-the-light");
    Settings Value;
    CHECK(menupreview::setSliderValue(
        Value, sliderByField(Field::LightHorizontal), 90));
    CHECK(near(Value.LightDirection, std::array<float, 3>{0, 1, 0}, 1e-6f));
    CHECK(menupreview::setSliderValue(Value, sliderByField(Field::LightPitch),
                                      30));
    CHECK(near(Value.LightDirection, menupreview::lightDirection(90.0f, 30.0f),
               1e-6f));
    const menupreview::LightAngles Angles =
        menupreview::lightAngles(Value.LightDirection);
    CHECK_NEAR(Angles.Horizontal, 90.0, 1e-3);
    CHECK_NEAR(Angles.Pitch, 30.0, 1e-3);
    CHECK(menupreview::valid(Value));
    // The angle the other slider owns keeps its stored precision.
    Value = Settings{};
    CHECK(menupreview::setSliderValue(Value, sliderByField(Field::LightPitch),
                                      30));
    const menupreview::LightAngles Kept =
        menupreview::lightAngles(Value.LightDirection);
    CHECK_NEAR(Kept.Horizontal, 172.0, 1e-3);
    CHECK_NEAR(Kept.Pitch, 30.0, 1e-3);
    CHECK(!menupreview::setSliderValue(Value, sliderByField(Field::LightPitch),
                                       30));
  }

  // -- the transaction: what is persisted, what is in effect, what is edited -
  {
    TEST_CASE("apply-writes-once-when-dirty");
    menupreview::State &Shared = menupreview::state();
    Shared = menupreview::State{};
    CHECK(!menupreview::dirty());
    int Writes = 0;
    std::string Written;
    const menupreview::Writer Write = [&](const std::string &Json) {
      ++Writes;
      Written = Json;
      return true;
    };
    // A clean card saves nothing.
    CHECK(menupreview::apply(Write));
    CHECK(Writes == 0);
    // An edit is a draft: nothing is in effect and nothing is written before
    // Apply.
    CHECK(menupreview::setSliderValue(
        Shared.Draft, sliderByField(Field::CameraOrigin, 0), 130));
    CHECK(menupreview::dirty());
    CHECK(Shared.Applied.CameraOrigin[0] == 121);
    CHECK(Writes == 0);
    CHECK(menupreview::apply(Write));
    CHECK(Writes == 1);
    CHECK(Shared.Applied.CameraOrigin[0] == 130);
    CHECK(Shared.Persisted == Shared.Draft);
    CHECK(!menupreview::dirty());
    CHECK(!Shared.Unsaved);
    // Nothing new: nothing is written again.
    CHECK(menupreview::apply(Write));
    CHECK(Writes == 1);
    // What was written is the draft.
    Settings Loaded;
    std::string Problem;
    CHECK(menupreview::parse(Written, Loaded, Problem) == ParseResult::Ok);
    CHECK(sameSettings(Loaded, Shared.Draft));
  }
  {
    TEST_CASE("apply-failed-write-keeps-the-session-values");
    menupreview::State &Shared = menupreview::state();
    Shared = menupreview::State{};
    CHECK(menupreview::setSliderValue(Shared.Draft,
                                      sliderByField(Field::Ambient), 200));
    CHECK(!menupreview::apply([](const std::string &) { return false; }));
    // The values are in effect for this session, but they are not saved.
    CHECK(Shared.Applied.Ambient == 200);
    CHECK(Shared.Persisted.Ambient == 96);
    CHECK(Shared.Unsaved);
    CHECK(menupreview::dirty());
    // A missing writer is a failed write.
    Shared = menupreview::State{};
    Shared.Draft.Shade = 7;
    CHECK(!menupreview::apply(menupreview::Writer{}));
    CHECK(Shared.Unsaved);
    CHECK(Shared.Applied.Shade == 7);
    // A later successful save clears it.
    CHECK(menupreview::apply([](const std::string &) { return true; }));
    CHECK(!Shared.Unsaved);
    CHECK(!menupreview::dirty());
    CHECK(Shared.Persisted.Shade == 7);
  }
  {
    TEST_CASE("apply-read-only-session");
    menupreview::State &Shared = menupreview::state();
    Shared = menupreview::State{};
    Shared.ReadOnly = true;
    int Writes = 0;
    const menupreview::Writer Write = [&](const std::string &) {
      ++Writes;
      return true;
    };
    CHECK(menupreview::apply(Write));
    CHECK(Writes == 0);
    // The controls stay editable; Apply reports that it could not save.
    CHECK(menupreview::setSliderValue(
        Shared.Draft, sliderByField(Field::CameraAngles, 1), 170));
    CHECK(!menupreview::apply(Write));
    // The stored file is never written over.
    CHECK(Writes == 0);
    CHECK(Shared.Applied.CameraAngles[1] == 170);
    CHECK(Shared.Persisted.CameraAngles[1] == 150);
    CHECK(Shared.Unsaved);
    CHECK(menupreview::dirty());
    // Discarding returns the session to what is persisted.
    menupreview::discardDraft();
    CHECK(!menupreview::dirty());
    CHECK(!Shared.Unsaved);
    CHECK(Shared.Applied == Settings{});
    CHECK(Shared.Draft == Settings{});
    CHECK(Shared.Persisted == Settings{});
    Shared = menupreview::State{};
  }
  {
    TEST_CASE("reset-and-discard-draft");
    menupreview::State &Shared = menupreview::state();
    Shared = menupreview::State{};
    // Against the persisted defaults a reset has nothing to save.
    CHECK(menupreview::setSliderValue(Shared.Draft, sliderByField(Field::Shade),
                                      20));
    menupreview::resetDraft();
    CHECK(Shared.Draft == Settings{});
    CHECK(!menupreview::dirty());
    // Against other persisted values the same reset is a pending edit.
    Shared.Persisted = Shared.Applied = Shared.Draft = edited();
    menupreview::resetDraft();
    CHECK(Shared.Draft == Settings{});
    CHECK(menupreview::dirty());
    CHECK(Shared.Applied == edited());
    CHECK(menupreview::apply([](const std::string &) { return true; }));
    CHECK(Shared.Persisted == Settings{});
    CHECK(!menupreview::dirty());
    // Discarding drops an edit that was never applied.
    CHECK(menupreview::setSliderValue(
        Shared.Draft, sliderByField(Field::CameraOrigin, 0), 5));
    CHECK(menupreview::dirty());
    menupreview::discardDraft();
    CHECK(!menupreview::dirty());
    CHECK(Shared.Draft == Shared.Persisted);
    Shared = menupreview::State{};
  }

  // -- the shipped documents and tables agree with the model ----------------
  {
    TEST_CASE("shipped-documents");
    const std::filesystem::path Assets =
        abctest::sourceDir() / "modules/gameui/assets";
    const std::string Menu = readFile(Assets / "gameui/rml/mainmenu.rml");
    const std::string SettingsDocument =
        readFile(Assets / "gameui/rml/settings.rml");
    const std::string Sheet = readFile(Assets / "gameui/rcss/settings.rcss");
    CHECK(!Menu.empty());
    CHECK(!SettingsDocument.empty());
    CHECK(!Sheet.empty());

    // No shipped document names the removed attributes any more.
    std::error_code Error;
    int Documents = 0;
    for (const auto &Entry :
         std::filesystem::recursive_directory_iterator(Assets, Error)) {
      if (!Entry.is_regular_file() || Entry.path().extension() != ".rml")
        continue;
      ++Documents;
      const std::string Text = readFile(Entry.path());
      CHECK(Text.find("auto-frame") == std::string::npos);
      CHECK(Text.find("frame-margin") == std::string::npos);
    }
    CHECK(!Error);
    CHECK(Documents > 0);

    // The main menu card's explicit attributes are the section's defaults,
    // so the first projection changes nothing.
    const Settings Defaults;
    CHECK(attribute(Menu, "camera-origin") ==
          menupreview::vectorText(Defaults.CameraOrigin));
    CHECK(attribute(Menu, "camera-angles") ==
          menupreview::vectorText(Defaults.CameraAngles));
    CHECK(attribute(Menu, "angles") ==
          menupreview::vectorText(Defaults.ModelAngles));
    CHECK(attribute(Menu, "light-direction") ==
          menupreview::vectorText(Defaults.LightDirection));
    CHECK(attribute(Menu, "light-color") ==
          menupreview::colorText(Defaults.LightColor));
    CHECK(attribute(Menu, "ambient") == "96");
    CHECK(attribute(Menu, "shade") == "110");

    // The settings document carries the card's fixed parts, and every class
    // the built rows use has a rule in the stylesheet.
    const char *const Ids[] = {
        "menu-preview-controls", "menu-preview-color",
        "menu-preview-swatch",   "menu-preview-color-value",
        "menu-preview-status",   "menu-preview-reset"};
    for (const char *Id : Ids)
      CHECK(SettingsDocument.find(std::string("id=\"") + Id + "\"") !=
            std::string::npos);
    CHECK(SettingsDocument.find("data-action=\"menu-preview-reset\"") !=
          std::string::npos);
    // A translation token in the document is a whole text node: the lookup
    // is exact, so a token with trailing text (a space before a sibling
    // element) would render literally instead of translating.
    for (const char *Token :
         {"Settings_MenuPreviewLightColor", "Settings_MenuPreviewReset"})
      CHECK(SettingsDocument.find(std::string(">#abcui.gameui/") + Token +
                                  "<") != std::string::npos);
    const char *const Classes[] = {"menu-preview-col", "menu-preview-group",
                                   "menu-preview-row", "menu-preview-axis",
                                   "menu-preview-hex"};
    for (const char *Name : Classes)
      CHECK(Sheet.find(std::string(".") + Name) != std::string::npos);
    // The rows are built inside this container, never declared twice.
    CHECK(SettingsDocument.find("id=\"menu-preview-controls\"></div>") !=
          std::string::npos);
    // The settings preview keeps a fixed, level camera: the player model's
    // origin is the middle of its own bounding box, so a camera at the model
    // origin's height frames the character centred whatever the card height.
    CHECK(attribute(SettingsDocument, "camera-origin") == "100 0 0");
    CHECK(attribute(SettingsDocument, "camera-angles") == "0 180 0");

    // Every token the card names exists in each shipped language table.
    const std::set<std::string> Tokens = cardTokens();
    CHECK(Tokens.size() > 8);
    for (const char *Language : {"english", "schinese", "spanish"}) {
      const std::set<std::string> Keys =
          tableKeys(Assets / "lang" / (std::string(Language) + ".txt"));
      for (const std::string &Token : Tokens)
        EXPECT_NAMED(Keys.count("abcui.gameui/" + Token) != 0,
                     (std::string(Language) + ": " + Token).c_str());
    }
  }

  menupreview::state() = menupreview::State{};
  return abctest::finish("abc_ui_menu_preview_settings");
}
