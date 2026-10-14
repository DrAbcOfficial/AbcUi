// services::menumusic::MenuMusic implementation - see MenuMusic.h.
#include "MenuMusic.h"

#include <metahook.h>

#include <abc/Console.h>
#include <abc/Paths.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace menumusic {

namespace {

// Re-issue the play this many seconds before the measured end: the engine's
// UI sound start latency is a frame or two, and overlapping one tail sample is
// inaudible while a gap would be obvious.
constexpr double kLoopLeadSeconds = 0.25;

uint32_t readLe32(const unsigned char* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
		| (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readLe16(const unsigned char* p) {
	return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

// Minimal RIFF/WAVE scan: the fmt chunk (byte rate) and the data chunk (size)
// are all the duration needs. Returns 0 when the file is not a plain PCM WAV.
double measureWavSeconds(const std::string& path) {
	std::FILE* file = nullptr;
	if (fopen_s(&file, path.c_str(), "rb") != 0 || !file)
		return 0.0;
	unsigned char header[12] = {};
	if (std::fread(header, 1, sizeof(header), file) != sizeof(header)
		|| std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
		std::fclose(file);
		return 0.0;
	}
	uint32_t byteRate = 0;
	uint32_t dataBytes = 0;
	for (int guard = 0; guard < 64; guard++) {
		unsigned char chunk[8] = {};
		if (std::fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk))
			break;
		const uint32_t size = readLe32(chunk + 4);
		if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
			unsigned char fmt[16] = {};
			if (std::fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt))
				break;
			const uint16_t format = readLe16(fmt);
			if (format != 1 && format != 0xFFFE) // PCM (or extensible PCM)
				break;
			byteRate = readLe32(fmt + 8);
			if (size > sizeof(fmt))
				std::fseek(file, static_cast<long>(size - sizeof(fmt)), SEEK_CUR);
			continue;
		}
		if (std::memcmp(chunk, "data", 4) == 0) {
			dataBytes = size;
			break;
		}
		std::fseek(file, static_cast<long>(size + (size & 1)), SEEK_CUR);
	}
	std::fclose(file);
	if (byteRate == 0 || dataBytes == 0)
		return 0.0;
	return static_cast<double>(dataBytes) / byteRate;
}

} // namespace

void MenuMusic::setSource(const std::string& soundPath) {
	if (Source == soundPath)
		return;
	stop();
	Source = soundPath;
	HeaderRead = false;
	Duration = 0.0;
}

const std::string& MenuMusic::source() const { return Source; }

void MenuMusic::setVolume(float volume) {
	Level = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}

float MenuMusic::volume() const { return Level; }

void MenuMusic::setEnabled(bool enabled) {
	On = enabled;
	if (!On)
		stop();
}

bool MenuMusic::enabled() const { return On; }

bool MenuMusic::playing() const { return Playing; }

double MenuMusic::position() const { return Elapsed; }

double MenuMusic::duration() const { return Duration; }

void MenuMusic::stop() {
	Playing = false;
	Elapsed = 0.0;
}

bool MenuMusic::startTrack() {
	if (Source.empty() || Level <= 0.0f || !gEngfuncs.pfnPlaySoundByName)
		return false;
	// The engine resolves the name under <search path>/sound/.
	gEngfuncs.pfnPlaySoundByName(const_cast<char*>(Source.c_str()), Level);
	Playing = true;
	Elapsed = 0.0;
	return true;
}

void MenuMusic::update(double frameTime, bool menuVisible) {
	if (!On || Source.empty() || !menuVisible) {
		if (Playing)
			stop();
		return;
	}
	if (!HeaderRead) {
		HeaderRead = true;
		Duration = measureWavSeconds(abc::ModPath(("sound/" + Source).c_str()));
		if (Duration <= 0.0)
			abc::ConsoleWarn("[AbcUi music] %s: no readable PCM header; playing once\n", Source.c_str());
	}
	if (!Playing) {
		startTrack();
		return;
	}
	if (frameTime > 0.0)
		Elapsed += frameTime;
	if (Duration <= 0.0)
		return; // one-shot: the engine stops it, we just stop tracking
	if (Elapsed >= Duration - kLoopLeadSeconds)
		startTrack();
}

MenuMusic& menuMusic() {
	static MenuMusic Instance;
	return Instance;
}

} // namespace menumusic
