// Mod resource loading extracted from GameUiModule.cpp (UTF-8).
#include "modules/gameui/settings/SettingsResources.h"
#include <abc/Paths.h>
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
	FILE* file = nullptr;
	if (fopen_s(&file, abc::ModPath(relativePath).c_str(), "rb") != 0 || !file)
		return {};
	std::string raw;
	char buffer[4096];
	size_t got = 0;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0)
		raw.append(buffer, got);
	fclose(file);
	return services::settings::KeyBindModel::decodeResourceText(raw.data(), raw.size());
}

std::vector<std::string> scanModDirectories(const char* relativeDir) {
	std::vector<std::string> names;
	_finddata_t data;
	const std::string mask = abc::ModPath(relativeDir) + "/*";
	const intptr_t handle = _findfirst(mask.c_str(), &data);
	if (handle == -1)
		return names;
	do {
		if ((data.attrib & _A_SUBDIR) && data.name[0] != '.')
			names.push_back(data.name);
	} while (_findnext(handle, &data) == 0);
	_findclose(handle);
	std::sort(names.begin(), names.end());
	if (names.size() > 512)
		names.resize(512);
	return names;
}

std::vector<std::string> scanModFileBasenames(const char* relativeDir) {
	std::vector<std::string> names;
	_finddata_t data;
	const std::string mask = abc::ModPath(relativeDir) + "/*";
	const intptr_t handle = _findfirst(mask.c_str(), &data);
	if (handle == -1)
		return names;
	do {
		if (data.attrib & _A_SUBDIR)
			continue;
		const std::string name = data.name;
		if (name.empty() || name[0] == '.' || name == "remapped.bmp")
			continue;
		const auto dot = name.rfind('.');
		names.push_back(dot == std::string::npos ? name : name.substr(0, dot));
	} while (_findnext(handle, &data) == 0);
	_findclose(handle);
	std::sort(names.begin(), names.end());
	if (names.size() > 512)
		names.resize(512);
	return names;
}

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
	services::settings::SettingsModel::parseUserScr(Text.c_str(), Table);
	return Table;
}
}
