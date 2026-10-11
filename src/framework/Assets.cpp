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
		{"abcui/rcss/base.rcss", "base stylesheet"},
		{"abcui/lang/english.txt", "localization"},
		{"abcui/rml/health.rml", "health document"},
		{"abcui/rml/ammobar.rml", "ammo document"},
		{"abcui/rml/textmenu.rml", "textmenu document"},
		{"abcui/img/healthpanel_background.png", "health panel art"},
		{"abcui/img/ammobar_background.png", "ammo panel art"},
		{"abcui/img/flashlight_off.png", "flashlight art"},
		{"abcui/spr/weapon_error.txt", "weapon sprite fallback"},
		// Batch B (feedback panels).
		{"abcui/rml/crosshair.rml", "crosshair document"},
		{"abcui/rml/indicator.rml", "indicator document"},
		{"abcui/rml/gindicator.rml", "grenade indicator document"},
		{"abcui/rml/dmgtiles.rml", "damage tiles document"},
		{"abcui/rml/effect.rml", "effect document"},
		{"abcui/rml/popnum.rml", "pop number document"},
		{"abcui/img/efx_darkround.png", "danger vignette art"},
		{"abcui/img/pain_indicator.png", "pain indicator art"},
		{"abcui/img/grenadeicon.png", "grenade indicator icon"},
		{"abcui/img/icon_poison.png", "damage tile icon"},
		{"abcui/img/icon_shock.png", "damage tile icon"},
		{"abcui/spr/face_rainleft.spr", "water splash sprite"},
		{"abcui/spr/face_rainright.spr", "water splash sprite"},
	};
	for (const auto& check : checks) {
		if (!fileExists(check.Path))
			abc::ConsoleWarn("[AbcUi] missing %s: %s\n", check.What, check.Path);
	}
}

}

}
