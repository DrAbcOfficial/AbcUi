// The main menu preview's config model (see MenuPreviewSettings.h). The
// section is parsed and written with header-only rapidjson, like the weapon
// filter's rule model: a third-party include path, never a framework link.
#include "MenuPreviewSettings.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace menupreview {
namespace {

constexpr double kRadians = 3.14159265358979323846 / 180.0;
// A direction is a unit vector within this tolerance (float rounding of the
// engine-space components).
constexpr float kUnitTolerance = 1e-3f;
// The stored six decimals cannot place a steep direction's pitch better than
// about a thousandth of a degree, so the pitch limit counts as reached
// within this slack; only a steeper vector is past the sliders' range.
constexpr float kPitchSlack = 0.05f;

// The stored direction is rounded to six decimals: the shortest float text
// that keeps a round trip well inside the angle sliders' one-degree step.
double rounded(double Value) {
  return std::round(Value * 1e6) / 1e6;
}

bool inside(const std::array<float, 3> &Value, float Limit) {
  return std::ranges::all_of(Value, [Limit](float Component) {
    return std::isfinite(Component) && Component >= -Limit &&
           Component <= Limit;
  });
}

const std::array<float, 3> &vectorOf(const Settings &Value, Field Target) {
  switch (Target) {
  case Field::CameraOrigin:
    return Value.CameraOrigin;
  case Field::CameraAngles:
    return Value.CameraAngles;
  case Field::ModelOrigin:
    return Value.ModelOrigin;
  default:
    return Value.ModelAngles;
  }
}

std::array<float, 3> &vectorOf(Settings &Value, Field Target) {
  return const_cast<std::array<float, 3> &>(
      vectorOf(static_cast<const Settings &>(Value), Target));
}

bool isLight(Field Target) {
  return Target == Field::LightHorizontal || Target == Field::LightPitch;
}

float unitLength(const std::array<float, 3> &Value) {
  return std::sqrt(Value[0] * Value[0] + Value[1] * Value[1] +
                   Value[2] * Value[2]);
}

// The light angles in the slider spelling: whole degrees.
LightAngles sliderAngles(const std::array<float, 3> &Direction) {
  LightAngles Angles = lightAngles(Direction);
  Angles.Horizontal = std::round(Angles.Horizontal);
  Angles.Pitch = std::round(Angles.Pitch);
  return Angles;
}

rapidjson::Value numberArray(const std::array<float, 3> &Value,
                             rapidjson::Document::AllocatorType &Allocator) {
  rapidjson::Value Array(rapidjson::kArrayType);
  for (float Component : Value) {
    const double Rounded = rounded(Component);
    if (Rounded == std::trunc(Rounded))
      Array.PushBack(static_cast<int>(Rounded), Allocator);
    else
      Array.PushBack(Rounded, Allocator);
  }
  return Array;
}
} // namespace

std::array<float, 3> lightDirection(float horizontal, float pitch) {
  const float h = static_cast<float>(horizontal * kRadians);
  const float p = static_cast<float>(pitch * kRadians);
  const float cosine = std::cos(p);
  return {{cosine * std::cos(h), cosine * std::sin(h), -std::sin(p)}};
}

LightAngles lightAngles(const std::array<float, 3> &direction) {
  // Double precision: the components are float-rounded, and a component of
  // 0.9999999 must not push the sine past 1. The length also lets a stored
  // vector of any non-zero magnitude read as the direction it names.
  const double x = direction[0];
  const double y = direction[1];
  const double z = direction[2];
  LightAngles Angles;
  Angles.Horizontal = static_cast<float>(std::atan2(y, x) / kRadians);
  // The horizontal angle is periodic: +180 is the one end of its range
  // that names the ray, so -180 reads as +180.
  if (Angles.Horizontal <= -180.0f)
    Angles.Horizontal = 180.0f;
  const double length = std::sqrt(x * x + y * y + z * z);
  if (length > 0.0)
    Angles.Pitch = static_cast<float>(
        -std::asin(std::clamp(z / length, -1.0, 1.0)) / kRadians);
  return Angles;
}

bool clamp(Settings &Value) {
  bool Moved = false;
  const auto vector = [&Moved](std::array<float, 3> &Vector, float Limit) {
    for (float &Component : Vector) {
      if (!std::isfinite(Component))
        Component = 0.0f;
      const float clamped = std::clamp(Component, -Limit, Limit);
      Moved = Moved || clamped != Component;
      Component = clamped;
    }
  };
  vector(Value.CameraOrigin, kCameraOriginLimit);
  vector(Value.CameraAngles, kAngleLimit);
  vector(Value.ModelOrigin, kModelOriginLimit);
  vector(Value.ModelAngles, kAngleLimit);
  const auto level = [&Moved](int &Level) {
    const int clamped = std::clamp(Level, 0, kLevelMax);
    Moved = Moved || clamped != Level;
    Level = clamped;
  };
  level(Value.Ambient);
  level(Value.Shade);
  for (int &Channel : Value.LightColor)
    Channel = std::clamp(Channel, 0, kLevelMax);

  // The direction is rebuilt only when it is not usable as it is: a vector
  // that is not unit, or one that pitches past the sliders' range.
  const bool finite = std::ranges::all_of(
      Value.LightDirection, [](float c) { return std::isfinite(c); });
  const float length = unitLength(Value.LightDirection);
  if (!finite || !(length > 0.0f)) {
    Value.LightDirection = Settings{}.LightDirection;
    Moved = true;
  } else if (std::abs(length - 1.0f) > kUnitTolerance) {
    for (float &Component : Value.LightDirection)
      Component /= length;
    Moved = true;
  }
  const LightAngles Angles = lightAngles(Value.LightDirection);
  if (std::abs(Angles.Pitch) > kLightPitchLimit + kPitchSlack) {
    Value.LightDirection = lightDirection(
        Angles.Horizontal,
        std::clamp(Angles.Pitch, -kLightPitchLimit, kLightPitchLimit));
    Moved = true;
  }
  return Moved;
}

bool valid(const Settings &Value) {
  if (!inside(Value.CameraOrigin, kCameraOriginLimit) ||
      !inside(Value.CameraAngles, kAngleLimit) ||
      !inside(Value.ModelOrigin, kModelOriginLimit) ||
      !inside(Value.ModelAngles, kAngleLimit))
    return false;
  if (Value.Ambient < 0 || Value.Ambient > kLevelMax || Value.Shade < 0 ||
      Value.Shade > kLevelMax)
    return false;
  for (int Channel : Value.LightColor)
    if (Channel < 0 || Channel > kLevelMax)
      return false;
  // The components are checked first: a comparison against a NaN length is
  // false and would let a NaN direction through.
  const bool finite = std::ranges::all_of(
      Value.LightDirection, [](float c) { return std::isfinite(c); });
  if (!finite ||
      std::abs(unitLength(Value.LightDirection) - 1.0f) > kUnitTolerance)
    return false;
  const LightAngles Angles = lightAngles(Value.LightDirection);
  return std::abs(Angles.Pitch) <= kLightPitchLimit + kPitchSlack;
}

ParseResult parse(std::string_view json, Settings &Output,
                  std::string &Problem) {
  Problem.clear();
  const auto reject = [&Problem](const std::string &Message) {
    Problem = Message;
    return ParseResult::Invalid;
  };
  // The first field that had to be clamped, for the warning.
  const auto note = [&Problem](const std::string &Message) {
    if (Problem.empty())
      Problem = Message;
  };

  rapidjson::Document document;
  document.Parse<rapidjson::kParseValidateEncodingFlag |
                 rapidjson::kParseIterativeFlag>(json.data(), json.size());
  if (document.HasParseError() || !document.IsObject())
    return reject("the section is not a JSON object");
  const auto version = document.FindMember("version");
  if (version == document.MemberEnd() || !version->value.IsInt() ||
      version->value.GetInt() != kSchemaVersion)
    return reject("version must be 1");

  // Every present field is read into a copy; a rejected section leaves
  // Output untouched.
  Settings Value = Output;
  bool Clamped = false;

  // A finite JSON number, or a rejection naming the field's shape.
  const auto number = [&reject](const rapidjson::Value &Member, double &Out,
                                const std::string &Shape) {
    if (Member.IsNumber()) {
      Out = Member.GetDouble();
      if (std::isfinite(Out))
        return true;
    }
    reject(Shape);
    return false;
  };
  // A field of three numbers. A value beyond the range is clamped and the
  // first one is noted.
  const auto vector = [&](const char *Key, float Limit,
                          std::array<float, 3> &Out) {
    const auto member = document.FindMember(Key);
    if (member == document.MemberEnd())
      return true;
    const std::string shape = std::string(Key) + " must be three numbers";
    const rapidjson::Value &array = member->value;
    if (!array.IsArray() || array.Size() != 3) {
      reject(shape);
      return false;
    }
    for (rapidjson::SizeType i = 0; i < 3; ++i) {
      double parsed = 0;
      if (!number(array[i], parsed, shape))
        return false;
      if (std::abs(parsed) > Limit) {
        Clamped = true;
        note(std::string(Key) + " out of range");
      }
      Out[i] = static_cast<float>(std::clamp(
          parsed, -static_cast<double>(Limit), static_cast<double>(Limit)));
    }
    return true;
  };
  if (!vector("camera_origin", kCameraOriginLimit, Value.CameraOrigin) ||
      !vector("camera_angles", kAngleLimit, Value.CameraAngles) ||
      !vector("model_origin", kModelOriginLimit, Value.ModelOrigin) ||
      !vector("model_angles", kAngleLimit, Value.ModelAngles))
    return ParseResult::Invalid;

  // A level is a whole number 0-255: a fractional value is a wrong shape,
  // not a value to round.
  const auto level = [&](const char *Key, bool Triple, double Parsed,
                         int &Out) {
    const std::string shape =
        std::string(Key) +
        (Triple ? " must be three integers" : " must be an integer");
    if (Parsed != std::trunc(Parsed)) {
      reject(shape);
      return false;
    }
    if (Parsed < 0 || Parsed > kLevelMax) {
      Clamped = true;
      note(std::string(Key) + " out of range");
    }
    Out = static_cast<int>(std::clamp(Parsed, 0.0, double(kLevelMax)));
    return true;
  };
  const auto scalarLevel = [&](const char *Key, int &Out) {
    const auto member = document.FindMember(Key);
    if (member == document.MemberEnd())
      return true;
    double parsed = 0;
    const std::string shape = std::string(Key) + " must be an integer";
    return number(member->value, parsed, shape) &&
           level(Key, false, parsed, Out);
  };
  if (!scalarLevel("ambient", Value.Ambient) ||
      !scalarLevel("shade", Value.Shade))
    return ParseResult::Invalid;
  {
    const auto member = document.FindMember("light_color");
    if (member != document.MemberEnd()) {
      const std::string shape = "light_color must be three integers";
      const rapidjson::Value &array = member->value;
      if (!array.IsArray() || array.Size() != 3)
        return reject(shape);
      for (rapidjson::SizeType i = 0; i < 3; ++i) {
        double parsed = 0;
        if (!number(array[i], parsed, shape) ||
            !level("light_color", true, parsed, Value.LightColor[i]))
          return ParseResult::Invalid;
      }
    }
  }
  {
    const auto member = document.FindMember("light_direction");
    if (member != document.MemberEnd()) {
      const std::string shape =
          "light_direction must be three numbers, not all zero";
      const rapidjson::Value &array = member->value;
      if (!array.IsArray() || array.Size() != 3)
        return reject(shape);
      double components[3] = {0, 0, 0};
      double scale = 0;
      for (rapidjson::SizeType i = 0; i < 3; ++i) {
        if (!number(array[i], components[i], shape))
          return ParseResult::Invalid;
        scale = std::max(scale, std::abs(components[i]));
      }
      // Dividing by the largest component first keeps a vector of any
      // magnitude inside double range.
      if (!(scale > 0.0))
        return reject(shape);
      double length = 0;
      for (double &Component : components) {
        Component /= scale;
        length += Component * Component;
      }
      length = std::sqrt(length);
      std::array<float, 3> unit{};
      for (rapidjson::SizeType i = 0; i < 3; ++i)
        unit[i] = static_cast<float>(components[i] / length);
      const LightAngles angles = lightAngles(unit);
      if (std::abs(angles.Pitch) > kLightPitchLimit + kPitchSlack) {
        // Past the sliders' pitch range: the direction the nearest slider
        // angle names is the one kept.
        Clamped = true;
        note("light_direction out of range");
        Value.LightDirection = lightDirection(
            angles.Horizontal,
            std::clamp(angles.Pitch, -kLightPitchLimit, kLightPitchLimit));
      } else
        Value.LightDirection = unit;
    }
  }
  if (clamp(Value)) {
    Clamped = true;
    note("a value out of range");
  }
  Output = Value;
  return Clamped ? ParseResult::Clamped : ParseResult::Ok;
}

bool serialize(const Settings &Value, std::string &Json) {
  if (!valid(Value))
    return false;
  rapidjson::Document document;
  document.SetObject();
  auto &allocator = document.GetAllocator();
  document.AddMember("version", kSchemaVersion, allocator);
  document.AddMember("camera_origin",
                     numberArray(Value.CameraOrigin, allocator), allocator);
  document.AddMember("camera_angles",
                     numberArray(Value.CameraAngles, allocator), allocator);
  document.AddMember("model_origin", numberArray(Value.ModelOrigin, allocator),
                     allocator);
  document.AddMember("model_angles", numberArray(Value.ModelAngles, allocator),
                     allocator);
  document.AddMember("light_direction",
                     numberArray(Value.LightDirection, allocator), allocator);
  rapidjson::Value color(rapidjson::kArrayType);
  for (int Channel : Value.LightColor)
    color.PushBack(Channel, allocator);
  document.AddMember("light_color", color, allocator);
  document.AddMember("ambient", Value.Ambient, allocator);
  document.AddMember("shade", Value.Shade, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  if (!document.Accept(writer))
    return false;
  Json.assign(buffer.GetString(), buffer.GetSize());
  return true;
}

std::string vectorText(const std::array<float, 3> &Value) {
  // {:g} writes an integer bare and six significant digits otherwise; the
  // added zero drops a negative zero.
  return std::format("{:g} {:g} {:g}", Value[0] + 0.0f, Value[1] + 0.0f,
                     Value[2] + 0.0f);
}

std::string colorText(const std::array<int, 3> &Value) {
  return std::format("{:g} {:g} {:g}", Value[0] / 255.0f, Value[1] / 255.0f,
                     Value[2] / 255.0f);
}

float sliderValue(const Settings &Value, const Slider &Slider) {
  if (isLight(Slider.Target)) {
    const LightAngles angles = sliderAngles(Value.LightDirection);
    return Slider.Target == Field::LightHorizontal ? angles.Horizontal
                                                   : angles.Pitch;
  }
  if (Slider.Target == Field::Ambient)
    return static_cast<float>(Value.Ambient);
  if (Slider.Target == Field::Shade)
    return static_cast<float>(Value.Shade);
  return std::round(vectorOf(static_cast<const Settings &>(Value),
                             Slider.Target)[Slider.Index]);
}

bool setSliderValue(Settings &Value, const Slider &Slider, float New) {
  if (!std::isfinite(New))
    return false;
  const float Rounded = std::clamp(std::round(New), Slider.Min, Slider.Max);
  // The slider shows whole values: setting the value it already shows is
  // not an edit, even when the stored number lies between two steps.
  if (Rounded == sliderValue(Value, Slider))
    return false;
  if (isLight(Slider.Target)) {
    // The angle the other slider owns stays at its stored precision.
    LightAngles angles = lightAngles(Value.LightDirection);
    if (Slider.Target == Field::LightHorizontal)
      angles.Horizontal = Rounded;
    else
      angles.Pitch = Rounded;
    Value.LightDirection = lightDirection(angles.Horizontal, angles.Pitch);
    return true;
  }
  if (Slider.Target == Field::Ambient)
    Value.Ambient = static_cast<int>(Rounded);
  else if (Slider.Target == Field::Shade)
    Value.Shade = static_cast<int>(Rounded);
  else
    vectorOf(Value, Slider.Target)[Slider.Index] = Rounded;
  return true;
}

State &state() {
  static State Instance;
  return Instance;
}

bool dirty() {
  const State &Current = state();
  return Current.Draft != Current.Persisted || Current.Unsaved;
}

bool apply(const Writer &write) {
  State &Current = state();
  if (!dirty())
    return true;
  // The values always take effect for this session; what a refused or
  // failed write costs is only the next launch.
  Current.Applied = Current.Draft;
  if (Current.ReadOnly) {
    Current.Unsaved = true;
    return false;
  }
  std::string json;
  if (!serialize(Current.Draft, json) || !write || !write(json)) {
    Current.Unsaved = true;
    return false;
  }
  Current.Persisted = Current.Draft;
  Current.Unsaved = false;
  return true;
}

void discardDraft() {
  State &Current = state();
  Current.Draft = Current.Persisted;
  Current.Applied = Current.Persisted;
  Current.Unsaved = false;
}

void resetDraft() {
  state().Draft = Settings{};
}

} // namespace menupreview
