#pragma once
#include <abc/Math.h>
#include <cstdio>

// framework::ColorUtil - RGB <-> HSV conversion used by the danger/hue-shift
// colour rules (old utility RGBtoHSV/HSVtoRGB, HealthPanel.GetDifferColor and
// FlashlightPanel.SetFlashBattery). Inline helpers; guarded against the grey
// (delta == 0) inputs the scheme colours produce.

namespace framework {

namespace ColorUtil {

inline void rgbToHsv(const float rgb[3], float hsv[3]) {
	abc::CMathlib::RGBToHSV(static_cast<int>(rgb[0]),static_cast<int>(rgb[1]),static_cast<int>(rgb[2]),hsv[0],hsv[1],hsv[2]);
}

inline void hsvToRgb(const float hsv[3], float rgb[3]) {
	int r,g,b; abc::CMathlib::HSVToRGB(hsv[0],hsv[1],hsv[2],r,g,b);
	rgb[0]=static_cast<float>(r); rgb[1]=static_cast<float>(g); rgb[2]=static_cast<float>(b);
}

// Old GetDifferColor: pull the hue of `base` toward `danger` by `ratio`
// (ratio 1 = base hue, ratio 0 = danger hue); saturation/value stay base's.
inline void differColor(const float base[3], const float danger[3], float ratio, float out[3]) {
	float hsv[3], dangerHsv[3];
	rgbToHsv(base, hsv);
	rgbToHsv(danger, dangerHsv);
	hsv[0] = dangerHsv[0] + (hsv[0] - dangerHsv[0]) * ratio;
	while (hsv[0] >= 360.0f)
		hsv[0] -= 360.0f;
	while (hsv[0] < 0.0f)
		hsv[0] += 360.0f;
	hsvToRgb(hsv, out);
}

// Formats rgb() for RCSS colour properties (color/image-color).
inline const char* format(const float rgb[3], char buffer[32]) {
	sprintf_s(buffer, 32, "rgb(%d, %d, %d)",
		static_cast<int>(rgb[0]), static_cast<int>(rgb[1]), static_cast<int>(rgb[2]));
	return buffer;
}

// rgba() variant - RmlUi takes the alpha as a 0..255 integer (not the CSS
// 0..1 float). Needed for scheme colours that carry real alpha, e.g. the
// flashlight "DefaultTransparent" (0 255 0 125) icon tint.
inline const char* formatRgba(const float rgb[3], int alpha, char buffer[40]) {
	sprintf_s(buffer, 40, "rgba(%d, %d, %d, %d)",
		static_cast<int>(rgb[0]), static_cast<int>(rgb[1]), static_cast<int>(rgb[2]), alpha);
	return buffer;
}

}

}
