// rmlui::AbcSystemInterface / AbcFileInterface implementation.
// Clipboard UTF-8 <-> UTF-16 conversion follows the Win32 backend pattern
// (RmlUi/Backends/RmlUi_Platform_Win32.cpp).

#include "AbcSystemInterface.h"

#include <abc/Console.h>
#include <abc/Paths.h>

#include "TextInput.h"
#include "cursor/CursorOverlay.h"
#include "framework/DocumentLoadGuard.h"
#include "framework/Localization.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <Windows.h>

namespace rmlui {

double AbcSystemInterface::GetElapsedTime() {
	static LARGE_INTEGER frequency = {};
	static bool initialized = false;
	if (!initialized) {
		QueryPerformanceFrequency(&frequency);
		initialized = true;
	}
	LARGE_INTEGER counter{};
	QueryPerformanceCounter(&counter);
	return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

int AbcSystemInterface::TranslateString(Rml::String& translated, const Rml::String& input) {
	// Localization tokens ("#token") resolve through the framework table;
	// non-tokens pass through untouched (returns -1 per Rml contract).
	if (!input.empty() && input[0] == '#') {
		if (framework::Localization::translate(translated, input.substr(1)))
			return 0;
	}
	translated = input;
	return -1;
}

bool AbcSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
	// T21: feed the document-load failure detector (see DocumentLoadGuard.h).
	framework::documentload::noteRmlLog(static_cast<int>(type), message.c_str());
	switch (type) {
	case Rml::Log::LT_ERROR:
	case Rml::Log::LT_ASSERT:
		abc::ConsoleError("[RmlUi] %s\n", message.c_str());
		break;
	case Rml::Log::LT_WARNING:
		abc::ConsoleWarn("[RmlUi] %s\n", message.c_str());
		break;
	default:
		abc::ConsolePrint("[RmlUi] %s\n", message.c_str());
		break;
	}
	return true;
}

void AbcSystemInterface::SetMouseCursor(const Rml::String& cursorName) {
	// Rml's default is a no-op. Store the requested glyph; the renderer's
	// final overlay owns drawing, independently of native GameUI visibility.
	cursorOverlay().setShape(cursorName);
}

void AbcSystemInterface::SetClipboardText(const Rml::String& text) {
	const int wideLength = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
	if (wideLength <= 0 && !text.empty())
		return;
	std::wstring wide(static_cast<size_t>(wideLength), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), wideLength);

	if (!OpenClipboard(nullptr))
		return;
	EmptyClipboard();
	const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
	if (HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
		if (void* data = GlobalLock(handle)) {
			memcpy(data, wide.c_str(), bytes);
			GlobalUnlock(handle);
			if (!SetClipboardData(CF_UNICODETEXT, handle)) GlobalFree(handle);
		}
		else {
			GlobalFree(handle);
		}
	}
	CloseClipboard();
}

void AbcSystemInterface::GetClipboardText(Rml::String& text) {
	text.clear();
	if (!OpenClipboard(nullptr))
		return;
	if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
		if (const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(handle))) {
			const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
			if (bytes > 0) {
				// The conversion writes the terminating NUL too, so size the
				// buffer for it first and trim afterwards.
				text.resize(static_cast<size_t>(bytes));
				WideCharToMultiByte(CP_UTF8, 0, wide, -1, text.data(), bytes, nullptr, nullptr);
				text.resize(static_cast<size_t>(bytes - 1));
			}
			GlobalUnlock(handle);
		}
	}
	CloseClipboard();
}

// IME placement delegates to TextInput (the HWND resolution and the imm32
// calls live next to the composition state machine). Pin backend reference:
// Backends/RmlUi_Platform_Win32.cpp SystemInterface_Win32::ActivateKeyboard.
void AbcSystemInterface::ActivateKeyboard(Rml::Vector2f caretPosition, float lineHeight) {
	updateImePlacement(caretPosition, lineHeight);
}

// Widget blur: the composition was already cancelled through the handler's
// OnDeactivate; there is no platform state worth resetting here.
void AbcSystemInterface::DeactivateKeyboard() {}

Rml::FileHandle AbcFileInterface::Open(const Rml::String& path) {	// RML/RCSS/image sources are mod-relative ("abcui/..."); the process CWD
	// is the game root, so resolve through abc::Paths (plan 08 §4.11).
	const std::string resolved = abc::ModPath(path.c_str());
	FILE* file = nullptr;
	if (fopen_s(&file, resolved.c_str(), "rb") != 0)
		file = nullptr;
	return reinterpret_cast<Rml::FileHandle>(file);
}

void AbcFileInterface::Close(Rml::FileHandle file) {
	fclose(reinterpret_cast<FILE*>(file));
}

size_t AbcFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file) {
	return fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
}

bool AbcFileInterface::Seek(Rml::FileHandle file, long offset, int origin) {
	return _fseeki64(reinterpret_cast<FILE*>(file), offset, origin) == 0;
}

size_t AbcFileInterface::Tell(Rml::FileHandle file) {
	return static_cast<size_t>(_ftelli64(reinterpret_cast<FILE*>(file)));
}

}
