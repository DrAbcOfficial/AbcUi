// framework::Localization implementation - see Localization.h.
#include "Localization.h"

#include <abc/Console.h>
#include <abc/Paths.h>

#include "../rmlui/VGUI2ExtensionImport.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace framework {

namespace {
std::unordered_map<std::string, std::string> Table;

// "key = value" lines; '#' starts a comment. Values keep interior spaces.
void parseTableFile(const std::string& path) {
	FILE* file = nullptr;
	if (fopen_s(&file, path.c_str(), "rb") != 0 || !file)
		return;

	std::string line;
	int ch;
	while ((ch = fgetc(file)) != EOF) {
		if (ch != '\n') {
			line.push_back(static_cast<char>(ch));
			continue;
		}
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#') {
			line.clear();
			continue;
		}
		const size_t eq = line.find('=');
		if (eq != std::string::npos) {
			std::string key = line.substr(0, eq);
			std::string value = line.substr(eq + 1);
			const auto trim = [](std::string& s) {
				const size_t b = s.find_first_not_of(" \t");
				const size_t e = s.find_last_not_of(" \t");
				s = b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
			};
			trim(key);
			trim(value);
			if (!key.empty())
				Table[key] = value;
		}
		line.clear();
	}
	if (!line.empty() && line[0] != '#') {
		// Last line without newline: parse it too (best effort).
		const size_t eq = line.find('=');
		if (eq != std::string::npos)
			Table[line.substr(0, eq)] = line.substr(eq + 1);
	}
	fclose(file);
}
} // namespace

void Localization::load() {
	Table.clear();

	const char* language = vgui2ext::currentLanguage();
	if (!language || !*language)
		language = "english";

	// Sven Co-op reports "schinese"; keep a translation-free alias chain so a
	// missing language file degrades to english instead of an empty UI.
	std::string path = abc::ModPath("abcui/lang/");
	path += language;
	path += ".txt";
	parseTableFile(path);
	if (Table.empty() && strcmp(language, "english") != 0) {
		abc::ConsoleWarn("[AbcUi] no localization for \"%s\", falling back to english\n", language);
		parseTableFile(abc::ModPath("abcui/lang/english.txt"));
	}
}

bool Localization::translate(std::string& out, const std::string& token) {
	const auto itor = Table.find(token);
	if (itor == Table.end())
		return false;
	out = itor->second;
	return true;
}

}
