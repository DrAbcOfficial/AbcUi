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
		{"abc/abcui/rcss/base.rcss", "base stylesheet"},
		{"abc/abcui/lang/english.txt", "localization"},
		{"abc/abcui/rml/health.rml", "health document"},
		{"abc/abcui/rml/ammobar.rml", "ammo document"},
		{"abc/abcui/rml/textmenu.rml", "textmenu document"},
		{"abc/abcui/img/healthpanel_background.png", "health panel art"},
		{"abc/abcui/img/ammobar_background.png", "ammo panel art"},
		{"abc/abcui/img/flashlight_off.png", "flashlight art"},
		{"abc/abcui/spr/weapon_error.txt", "weapon sprite fallback"},
		// Batch B (feedback panels).
		{"abc/abcui/rml/crosshair.rml", "crosshair document"},
		{"abc/abcui/rml/indicator.rml", "indicator document"},
		{"abc/abcui/rml/gindicator.rml", "grenade indicator document"},
		{"abc/abcui/rml/dmgtiles.rml", "damage tiles document"},
		{"abc/abcui/rml/effect.rml", "effect document"},
		{"abc/abcui/rml/popnum.rml", "pop number document"},
		{"abc/abcui/img/efx_darkround.png", "danger vignette art"},
		{"abc/abcui/img/pain_indicator.png", "pain indicator art"},
		{"abc/abcui/img/grenadeicon.png", "grenade indicator icon"},
		{"abc/abcui/img/icon_poison.png", "damage tile icon"},
		{"abc/abcui/img/icon_shock.png", "damage tile icon"},
		{"abc/abcui/spr/face_rainleft.spr", "water splash sprite"},
		{"abc/abcui/spr/face_rainright.spr", "water splash sprite"},
		// Batch D (render depth panels).
		{"abc/abcui/rml/itemhighlight.rml", "item highlight document"},
		{"abc/abcui/ItemHighLightList.txt", "item highlight list"},
		{"abc/abcui/img/itemhighlight/marker.png", "item highlight marker art"},
		{"abc/abcui/rml/radar.rml", "radar document"},
		{"abc/abcui/img/radar/radar_background.png", "radar plate art"},
		{"abc/abcui/img/radar/radar_mask.png", "radar square mask"},
		// T05 interactive documents.
		{"abc/abcui/rml/mainmenu.rml", "main menu document"},
		{"abc/abcui/rcss/mainmenu.rcss", "main menu stylesheet"},
		// T10 settings page.
		{"abc/abcui/rml/settings.rml", "settings document"},
		{"abc/abcui/rcss/settings.rcss", "settings stylesheet"},
		// T12 video confirmation dialog.
		{"abc/abcui/rml/videoconfirm.rml", "video confirm document"},
		{"abc/abcui/rcss/videoconfirm.rcss", "video confirm stylesheet"},
	};
	for (const auto& check : checks) {
		if (!fileExists(check.Path))
			abc::ConsoleWarn("[AbcUi] missing %s: %s\n", check.What, check.Path);
	}
}

}

}
