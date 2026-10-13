#pragma once
// Settings resource discovery and owned parsed-table storage. No DOM or input.
#include "services/settings/SettingsModel.h"
#include <string>
#include <vector>
namespace gameui::settings {
struct AdvancedTable {
	services::settings::SettingsModel::UserScrTable parsed;
	std::vector<services::settings::SettingRow> rows;
	bool valid = false;
};
std::string readModFile(const char* relativePath);
std::vector<std::string> scanModDirectories(const char* relativeDir);
std::vector<std::string> scanModFileBasenames(const char* relativeDir);
AdvancedTable buildAdvancedTable();
}
