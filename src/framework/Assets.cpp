// framework::Assets implementation - see Assets.h.
#include "Assets.h"

#include <abc/Console.h>
#include <abc/Paths.h>

#include <cstdio>

namespace framework {

namespace Assets {

namespace {
bool fileExists(const char* relativePath) {
	FILE* file = nullptr;
	if (fopen_s(&file, abc::ModPath(relativePath).c_str(), "rb") != 0)
		return false;
	fclose(file);
	return true;
}
} // namespace

void verifyInstalled() {
	struct Check {
		const char* Path;
		const char* What;
	};
	constexpr Check checks[] = {
		{"abcui/rml/demo.rml", "demo document"},
		{"abcui/rcss/base.rcss", "base stylesheet"},
		{"abcui/img/test.png", "demo image"},
		{"abcui/lang/english.txt", "localization"},
	};
	for (const auto& check : checks) {
		if (!fileExists(check.Path))
			abc::ConsoleWarn("[AbcUi] missing %s: %s\n", check.What, check.Path);
	}
}

}

}
