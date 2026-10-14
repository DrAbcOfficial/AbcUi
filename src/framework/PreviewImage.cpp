#include "PreviewImage.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace framework {
namespace {
bool token(const std::string& Text) {
	return !Text.empty() && Text.size() <= 96 && Text.find("..") == std::string::npos
		&& std::all_of(Text.begin(), Text.end(), [](unsigned char C) {
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z')
				|| (C >= '0' && C <= '9') || C == '_' || C == '-' || C == '.';
		});
}
void hue(ImageLoader::PaletteColor& Pixel, int Color) {
	const float Max = std::max({Pixel.Red, Pixel.Green, Pixel.Blue}) / 255.0f;
	const float Min = std::min({Pixel.Red, Pixel.Green, Pixel.Blue}) / 255.0f;
	const float Chroma = Max - Min;
	const float H = Color * 6.0f / 256.0f;
	const float X = Chroma * (1 - std::abs(std::fmod(H, 2.0f) - 1));
	float R = 0, G = 0, B = 0;
	switch (int(H)) {
	case 0: R = Chroma; G = X; break;
	case 1: R = X; G = Chroma; break;
	case 2: G = Chroma; B = X; break;
	case 3: G = X; B = Chroma; break;
	case 4: R = X; B = Chroma; break;
	default: R = Chroma; B = X; break;
	}
	Pixel.Blue = static_cast<unsigned char>((B + Min) * 255);
	Pixel.Green = static_cast<unsigned char>((G + Min) * 255);
	Pixel.Red = static_cast<unsigned char>((R + Min) * 255);
}
} // namespace

bool parsePreviewUri(const std::string& Uri, PreviewRequest& Output) {
	if (Uri.rfind("preview://", 0) != 0)
		return false;
	std::vector<std::string> Parts;
	size_t Start = 10;
	while (Start <= Uri.size()) {
		const auto End = Uri.find('/', Start);
		Parts.push_back(Uri.substr(Start, End == std::string::npos ? End : End - Start));
		if (End == std::string::npos) break;
		Start = End + 1;
	}
	if (Parts.size() < 3 || !token(Parts[1]))
		return false;
	PreviewRequest Result;
	Result.Model = Parts[0] == "model";
	if (Result.Model) {
		if (Parts.size() != 4) return false;
		auto Parse = [](const std::string& Text, int& Value) {
			const auto R = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
			return R.ec == std::errc() && R.ptr == Text.data() + Text.size() && Value >= 0 && Value <= 255;
		};
		if (!Parse(Parts[2], Result.Top) || !Parse(Parts[3], Result.Bottom)) return false;
		Result.Path = "models/player/" + Parts[1] + "/" + Parts[1] + ".bmp";
	} else {
		if (Parts[0] != "spray" || Parts.size() != 3 || !token(Parts[2])) return false;
		Result.Path = "logos/" + Parts[1] + ".bmp";
		Result.SprayColor = Parts[2];
	}
	Output = std::move(Result);
	return true;
}

bool decodePreview(const PreviewRequest& Request, const std::vector<unsigned char>& Bytes,
	ImageLoader::DecodedImage& Output) {
	const std::array<const char*, 9> Names = {"Orange", "Yellow", "Blue", "Ltblue", "Green", "Red", "Brown", "Ltgray", "Dkgray"};
	const unsigned char Colors[9][3] = {{255,120,0},{255,255,0},{0,0,255},{0,180,255},{0,255,0},
		{255,0,0},{128,64,0},{192,192,192},{64,64,64}};
	const auto It = std::find(Names.begin(), Names.end(), Request.SprayColor);
	// FreeImage owns format detection, header validation, decompression and
	// palette access. This callback only supplies the preview's color policy.
	return ImageLoader::loadFromMemory(Bytes.data(), Bytes.size(), Output,
		[&](unsigned Index, ImageLoader::PaletteColor& Pixel) {
		if (Request.Model) {
			if (Index >= 160 && Index <= 191) hue(Pixel, Request.Top);
			if (Index >= 192 && Index <= 223) hue(Pixel, Request.Bottom);
		} else if (It != Names.end() && Index != 255) {
			const float Light = std::max({Pixel.Red, Pixel.Green, Pixel.Blue}) / 255.0f;
			const auto& Color = Colors[It - Names.begin()];
			Pixel.Red = static_cast<unsigned char>(Color[0] * Light);
			Pixel.Green = static_cast<unsigned char>(Color[1] * Light);
			Pixel.Blue = static_cast<unsigned char>(Color[2] * Light);
		}
	});
}
} // namespace framework
