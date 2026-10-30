// Shared RmlUi fixtures for the offline page tests.
//
// The renderer is a no-op sink. The system interface keeps the elapsed clock
// at zero, translates tokens to themselves, records and prints RmlUi errors,
// and provides an in-memory clipboard. Tests with different needs derive from
// these classes instead of copying the whole interface.
#pragma once

#include "framework/TestEnvironment.h"

#include <RmlUi/Core.h>

#include <iostream>

namespace abctest {

class NullRenderInterface : public Rml::RenderInterface {
public:
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                              Rml::Span<const int>) override {
    return {};
  }
  void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
                      Rml::TextureHandle) override {}
  void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
  Rml::TextureHandle LoadTexture(Rml::Vector2i &,
                                 const Rml::String &) override {
    return {};
  }
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,
                                     Rml::Vector2i) override {
    return {};
  }
  void ReleaseTexture(Rml::TextureHandle) override {}
  void EnableScissorRegion(bool) override {}
  void SetScissorRegion(Rml::Rectanglei) override {}
};

class QuietSystemInterface : public Rml::SystemInterface {
public:
  double GetElapsedTime() override {
    return 0.0;
  }
  int TranslateString(Rml::String &translated,
                      const Rml::String &input) override {
    translated = input;
    return -1;
  }
  bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
    if (type <= Rml::Log::LT_ERROR) {
      std::cerr << "rmlui: " << message << "\n";
      ++Errors;
    }
    return true;
  }
  void SetClipboardText(const Rml::String &text) override {
    Clipboard = text;
  }
  void GetClipboardText(Rml::String &text) override {
    text = Clipboard;
  }

  int Errors = 0;
  Rml::String Clipboard;
};

// The RmlUi submodule ships these fonts; the configure step verifies them.
inline bool loadDefaultFont() {
  const std::string path = defaultFontPath();
  return !path.empty() && Rml::LoadFontFace(path);
}

inline bool loadMonoFont() {
  const std::string path = monoFontPath();
  return !path.empty() && Rml::LoadFontFace(path);
}

} // namespace abctest
