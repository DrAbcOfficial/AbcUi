#pragma once

// framework::ColorUtil - RGB <-> HSV conversion used by the danger/hue-shift
// colour rules (old utility RGBtoHSV/HSVtoRGB, HealthPanel.GetDifferColor and
// FlashlightPanel.SetFlashBattery). Inline helpers; guarded against the grey
// (delta == 0) inputs the scheme colours produce.

namespace framework {

namespace ColorUtil {

inline void rgbToHsv(const float rgb[3], float hsv[3]) {
	const float r = rgb[0] / 255.0f, g = rgb[1] / 255.0f, b = rgb[2] / 255.0f;
	const float maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
	const float minc = r < g ? (r < b ? r : b) : (g < b ? g : b);
	const float delta = maxc - minc;
	hsv[2] = maxc;
	hsv[1] = maxc > 0.0f ? delta / maxc : 0.0f;
	if (delta <= 0.0f) {
		hsv[0] = 0.0f;
		return;
	}
	if (maxc == r)
		hsv[0] = 60.0f * (g - b) / delta;
	else if (maxc == g)
		hsv[0] = 60.0f * ((b - r) / delta + 2.0f);
	else
		hsv[0] = 60.0f * ((r - g) / delta + 4.0f);
	if (hsv[0] < 0.0f)
		hsv[0] += 360.0f;
}

inline void hsvToRgb(const float hsv[3], float rgb[3]) {
	const float h = hsv[0] / 60.0f;
	const float s = hsv[1], v = hsv[2];
	const int sector = static_cast<int>(h) % 6;
	const float frac = h - static_cast<float>(sector);
	const float p = v * (1.0f - s);
	const float q = v * (1.0f - s * frac);
	const float t = v * (1.0f - s * (1.0f - frac));
	switch (sector) {
		case 0: rgb[0] = v; rgb[1] = t; rgb[2] = p; break;
		case 1: rgb[0] = q; rgb[1] = v; rgb[2] = p; break;
		case 2: rgb[0] = p; rgb[1] = v; rgb[2] = t; break;
		case 3: rgb[0] = p; rgb[1] = q; rgb[2] = v; break;
		case 4: rgb[0] = t; rgb[1] = p; rgb[2] = v; break;
		default: rgb[0] = v; rgb[1] = p; rgb[2] = q; break;
	}
	rgb[0] *= 255.0f;
	rgb[1] *= 255.0f;
	rgb[2] *= 255.0f;
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
