#pragma once
// Settings resource discovery and owned parsed-table storage. No DOM or input.
#include "services/settings/SettingsModel.h"
#include <cstddef>
#include <string>
#include <vector>
namespace gameui::settings {
std::string readModFile(const char* relativePath);
// Writes a mod-relative binary file through the engine filesystem (the mod
// directory is writable; the engine's own tempdecal.wad lives there). Falls
// back to a direct file beside the mod path. False + error on any failure.
bool writeModFile(const char* relativePath, const std::vector<unsigned char>& bytes,
	std::string& error);
std::vector<std::string> scanModDirectories(const char* relativeDir);
std::vector<std::string> scanModFileBasenames(const char* relativeDir);
services::settings::SettingsModel::UserScrTable loadAdvancedScript();
}
