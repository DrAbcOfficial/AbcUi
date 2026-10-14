#pragma once

#include "services/settings/SettingsModel.h"

#include <string>
#include <vector>

namespace services::uiconfig {

struct ConfigError {
	std::string File;
	std::string Field;
	std::string Message;
};

struct MenuEntry {
	std::string Id;
	std::string Label;
	std::string Action;
	std::string Condition = "always";
};

struct MenuDefinition {
	std::string Title = "#MainMenu_Title";
	std::vector<MenuEntry> Main;
	std::vector<MenuEntry> Pause;
};

struct OptionDefinition {
	float Value = 0;
	std::string Label;
};

// Own every string; materialize the legacy transaction model's pointer view
// only after loading/sorting has finished. Definitions outlive that view.
struct SettingDefinition {
	std::string Cvar;
	std::string Label;
	std::string Section;
	std::string DefaultString;
	std::string Source;
	settings::SettingKind Kind = settings::SettingKind::kBool;
	float Min = 0;
	float Max = 1;
	float Step = 1;
	float DefaultValue = 0;
	std::vector<OptionDefinition> Options;
	std::vector<settings::SettingEnumOption> OptionViews;
};

struct SettingsPageDefinition {
	std::string Id;
	std::string Label;
	int Order = 0;
	std::vector<std::string> Components;
	std::vector<SettingDefinition> Items;
	std::vector<std::string> Cvars;

	bool hasComponent(const char* Name) const;
};

// Pure parsing/validation: no filesystem, engine, UI, or logging side effects.
class DefinitionParser {
public:
	static bool parseMenu(const std::string& Text, MenuDefinition& Output, ConfigError& Error);
	static bool parsePage(const std::string& Text, SettingsPageDefinition& Output, ConfigError& Error);
	static MenuDefinition fallbackMenu();
};

} // namespace services::uiconfig
