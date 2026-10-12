// framework::Localization implementation - see Localization.h.
#include "Localization.h"
#include "TextUtil.h"

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
	auto parseLine = [&]() { std::string key, value; if (TextUtil::parseTranslationLine(line,key,value)) Table[key]=value; line.clear(); };
	int ch;
	while ((ch = fgetc(file)) != EOF) {
		if (ch == '\n') parseLine();
		else if (line.size() < 65536) line.push_back(static_cast<char>(ch));
	}
	parseLine();
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
	parseTableFile(abc::ModPath("abcui/lang/english.txt"));
	if (strcmp(language, "english") != 0) parseTableFile(path);
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
