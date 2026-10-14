// Mod resource loading extracted from GameUiModule.cpp (UTF-8).
#include "modules/gameui/settings/SettingsResources.h"
#include <abc/Paths.h>
#include <abc/Plugin.h>
#include "rmlui/VGUI2ExtensionImport.h"
#include "services/settings/KeyBindModel.h"
#include <io.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
namespace gameui::settings {
std::string readModFile(const char* relativePath) {
	constexpr unsigned MaxBytes = 4 * 1024 * 1024;
	if (abc::g_pFileSystem) {
		const auto File = abc::g_pFileSystem->Open(relativePath, "rb");
		if (File) {
			const auto Size = abc::g_pFileSystem->Size(File);
			std::string Raw;
			if (Size > 0 && Size <= MaxBytes) {
				Raw.resize(Size);
				if (abc::g_pFileSystem->Read(Raw.data(), Size, File) != static_cast<int>(Size))
					Raw.clear();
			}
			abc::g_pFileSystem->Close(File);
			return services::settings::KeyBindModel::decodeResourceText(Raw.data(), Raw.size());
		}
	}
	FILE* file = nullptr;
	if (fopen_s(&file, abc::ModPath(relativePath).c_str(), "rb") != 0 || !file)
		return {};
	std::string raw;
	char buffer[4096];
	size_t got = 0;
	while (raw.size() < MaxBytes && (got = fread(buffer, 1, sizeof(buffer), file)) > 0)
		raw.append(buffer, got);
	fclose(file);
	return services::settings::KeyBindModel::decodeResourceText(raw.data(), raw.size());
}

namespace {
std::vector<std::string> scanMounted(const char* Directory, bool Directories) {
	std::vector<std::string> Names;
	if (abc::g_pFileSystem) {
		FileFindHandle_t Handle = FILESYSTEM_INVALID_FIND_HANDLE;
		std::string Mask = std::string(Directory) + (Directories ? "/*" : "/*.bmp");
		const char* Name = abc::g_pFileSystem->FindFirst(Mask.c_str(), &Handle);
		while (Name) {
			std::string Entry = Name;
			if (!Entry.empty() && Entry[0] != '.' && Entry != "remapped.bmp"
				&& (!Directories || abc::g_pFileSystem->FindIsDirectory(Handle))) {
				if (!Directories) Entry = Entry.substr(0, Entry.find_last_of('.'));
				Names.push_back(std::move(Entry));
			}
			Name = abc::g_pFileSystem->FindNext(Handle);
		}
		if (Handle != FILESYSTEM_INVALID_FIND_HANDLE) abc::g_pFileSystem->FindClose(Handle);
	}
	std::sort(Names.begin(), Names.end());
	Names.erase(std::unique(Names.begin(), Names.end()), Names.end());
	if (Names.size() > 512) Names.resize(512);
	return Names;
}
}
std::vector<std::string> scanModDirectories(const char* Directory) { return scanMounted(Directory, true); }
std::vector<std::string> scanModFileBasenames(const char* Directory) { return scanMounted(Directory, false); }

services::settings::SettingsModel::UserScrTable loadAdvancedScript() {
	services::settings::SettingsModel::UserScrTable Table;
	// Localized variant first (svencoop_schinese ships translated prompts):
	// the Path is CWD-relative like abc::ModPath - the engine mounts the
	// Language directory next to the mod directory.
	std::string Text;
	{
		const char* Language = vgui2ext::currentLanguage();
		if (Language && *Language && std::strcmp(Language, "english") != 0) {
			std::string Path = abc::GameDir();
			Path += "_";
			Path += Language;
			Path += "/user.scr";
			FILE* File = nullptr;
			if (fopen_s(&File, Path.c_str(), "rb") == 0 && File) {
				char Buffer[4096];
				size_t Got = 0;
				while ((Got = fread(Buffer, 1, sizeof(Buffer), File)) > 0)
					Text.append(Buffer, Got);
				fclose(File);
			}
		}
	}
	if (Text.empty())
		Text = readModFile("user.scr");
	// Empty/invalid scripts leave the JSON page's fallback items intact.
	const auto Decoded = services::settings::KeyBindModel::decodeResourceText(Text.data(), Text.size());
	services::settings::SettingsModel::parseUserScr(Decoded.c_str(), Table);
	return Table;
}
}
