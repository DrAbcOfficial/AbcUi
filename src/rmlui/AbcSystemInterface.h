#pragma once

// rmlui::AbcSystemInterface + AbcFileInterface - RmlUi platform seams.
// Plan 05 §3.3: elapsed time (QPC), logging (-> abc::Console levels),
// clipboard (Win32 CF_UNICODETEXT), localization hook; plus the file
// interface resolving RML/RCSS/image paths against the mod dir
// (abc::Paths::ModPath) instead of the process CWD.

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>

namespace rmlui {

class AbcSystemInterface : public Rml::SystemInterface {
public:
	double GetElapsedTime() override;
	int TranslateString(Rml::String& translated, const Rml::String& input) override;
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
	void SetClipboardText(const Rml::String& text) override;
	void GetClipboardText(Rml::String& text) override;
};

class AbcFileInterface : public Rml::FileInterface {
public:
	Rml::FileHandle Open(const Rml::String& path) override;
	void Close(Rml::FileHandle file) override;
	size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
	bool Seek(Rml::FileHandle file, long offset, int origin) override;
	size_t Tell(Rml::FileHandle file) override;
};

}
