// Shared RmlUi fixtures for the offline page tests.
//
// The renderer is a no-op sink. The system interface keeps the elapsed clock
// at zero, translates tokens to themselves, records and prints RmlUi errors,
// and provides an in-memory clipboard. Tests with different needs derive from
// these classes instead of copying the whole interface.
#pragma once

#include "framework/TestEnvironment.h"

#include <RmlUi/Core.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

// Accepts every texture and geometry: documents that carry icons otherwise
// log a load failure against the null renderer, and the offline tests never
// inspect pixels.
class TextureRenderInterface : public NullRenderInterface {
public:
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                              Rml::Span<const int>) override {
    return 1;
  }
  Rml::TextureHandle LoadTexture(Rml::Vector2i &size,
                                 const Rml::String &) override {
    size = Rml::Vector2i(1, 1);
    return 1;
  }
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,
                                     Rml::Vector2i) override {
    return 1;
  }
  void ReleaseTexture(Rml::TextureHandle) override {}
};

// Quiet system interface that counts warnings as failures (the page tests
// treat a warning as a broken document) and exposes a settable frame clock
// for animation tests.
class WarningSystemInterface : public QuietSystemInterface {
public:
  bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
    if (type <= Rml::Log::LT_WARNING) {
      ++Messages;
      std::cerr << "rmlui(" << static_cast<int>(type) << "): " << message
                << "\n";
    }
    return QuietSystemInterface::LogMessage(type, message);
  }
  double GetElapsedTime() override {
    return Now;
  }

  int Messages = 0;
  double Now = 0.0;
};

inline std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

inline void replaceAll(std::string &text, const std::string &from,
                       const std::string &to) {
  std::size_t at = 0;
  while ((at = text.find(from, at)) != std::string::npos) {
    text.replace(at, from.size(), to);
    at += to.size();
  }
}

inline bool hasId(Rml::ElementDocument *document, const char *id) {
  return document && document->GetElementById(id) != nullptr;
}

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
