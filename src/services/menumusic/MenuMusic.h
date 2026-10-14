#pragma once

// services::menumusic::MenuMusic - the menu's background music.
//
// The engine plays one-shot UI sounds (pfnPlaySoundByName); there is no loop
// channel for a menu track, so the loop is driven here: the track's duration
// is measured from its own WAV header (no per-track configuration), and the
// service re-issues the play a fraction of a second before the tail so the
// loop has no gap. Playback stops the moment no replacement page is on
// screen, so the music never bleeds into gameplay.
//
// Deliberately narrow: it knows one mod-relative sound path, a volume and on/off;
// the owner (the module) supplies the frame delta and the "menu is up" fact.

#include <string>

namespace menumusic {

class MenuMusic {
public:
	// Mod-relative sound path as the engine sound system spells it
	// ("abcui/menu/opfor.wav" -> <mod>/sound/abcui/menu/opfor.wav). Empty
	// disables playback entirely.
	void setSource(const std::string& soundPath);
	const std::string& source() const;
	void setVolume(float volume); // 0..1
	float volume() const;
	void setEnabled(bool enabled);
	bool enabled() const;

	// Per-frame: starts/repeats/stops the track to match `menuVisible`.
	// `frameTime` is the HUD frame delta in seconds.
	void update(double frameTime, bool menuVisible);

	// Stops playback now (shutdown / level start).
	void stop();

	// Measured length of the current source in seconds; <= 0 when the header
	// could not be read (the service then plays once and stays silent).
	double duration() const;
	// Diagnostics: whether the track is currently playing and how far into it
	// the loop is.
	bool playing() const;
	double position() const;

private:
	bool startTrack();

	std::string Source;
	float Level = 0.5f;
	bool On = true;
	bool Playing = false;
	bool HeaderRead = false;
	double Duration = 0.0;
	double Elapsed = 0.0;
};

MenuMusic& menuMusic();

} // namespace menumusic
