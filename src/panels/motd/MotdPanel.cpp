// panels::Motd implementation - see MotdPanel.h.
// from: ABCEnchance/src/hud/vgui/motd.cpp (GBK->UTF-8).
#include <metahook.h>

#include "MotdPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include <abc/CvarCmd.h>
#include <abc/Console.h>
#include <abc/Paths.h>

#include "../../data/NetworkMessages.h"
#include "../../framework/Commands.h"
#include "../../framework/TextUtil.h"

namespace panels {

namespace motd {
// Old ctor cvar (persisted contract - never rename).
cvar_t* HudMotd = nullptr;

void registerCvars() {
	HudMotd = abc::CREATE_CVAR("hud_motd", "1", abc::FCVAR_VALUE, nullptr);
}
}

// Old MotdPanel.res timing.
constexpr double kKeepTime = 5.0;
constexpr double kFadeTime = 1.0;

// Minimal HTML-to-text: drop script/style blocks and tags, decode the five
// required entities (the vgui HTML control rendered the full document; the
// Rml port shows the readable content - see the header D-17 note).
std::string htmlToText(std::string html) {
	std::string lowered = html;
	for (char& c : lowered)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	const auto dropBlock = [&](const char* tag) {
		size_t at = 0;
		while (true) {
			const std::string open = std::string("<") + tag;
			const size_t begin = lowered.find(open, at);
			if (begin == std::string::npos)
				break;
			const std::string close = std::string("</") + tag + ">";
			size_t end = lowered.find(close, begin);
			end = end == std::string::npos ? html.size() : end + close.size();
			html.erase(begin, end - begin);
			lowered.erase(begin, end - begin);
			at = begin;
		}
	};
	dropBlock("script");
	dropBlock("style");

	std::string out;
	out.reserve(html.size());
	for (size_t i = 0; i < html.size(); i++) {
		if (html[i] == '<') {
			const size_t close = html.find('>', i);
			i = close == std::string::npos ? html.size() : close;
			continue;
		}
		out += html[i];
	}
	const auto replaceAll = [](std::string text, const char* from, const char* to) {
		size_t at = 0;
		while ((at = text.find(from, at)) != std::string::npos) {
			text.replace(at, strlen(from), to);
			at += strlen(to);
		}
		return text;
	};
	out = replaceAll(out, "&nbsp;", " ");
	out = replaceAll(out, "&amp;", "&");
	out = replaceAll(out, "&lt;", "<");
	out = replaceAll(out, "&gt;", ">");
	out = replaceAll(out, "&quot;", "\"");
	return out;
}

MotdPanel::MotdPanel()
	: HudPanel("motd", "abcui/rml/motd.rml") {}

void MotdPanel::reset() {
	Buffer.clear();
	Page.clear();
	HasContent = false;
	Fading = false;
	// Old Reset does not clear LoadedMissionBrief (the map brief survives a
	// scoreboard re-show within one map); it reloads per map by the VidInit
	// panel reset of the old viewport - match that (fresh document state).
	LoadedMissionBrief = false;
	if (Rml::Element* box = element("box"))
		box->SetProperty("opacity", "1");
	hide();
}

void MotdPanel::append(const char* message) {
	Buffer += message;
}

void MotdPanel::startCountdown() {
	ShowTime = ClientTimeNow + kKeepTime;
	Fading = false;
	if (Rml::Element* box = element("box"))
		box->SetProperty("opacity", "1");
	show();
}

void MotdPanel::finishSend() {
	if (Buffer.empty()) {
		return;
	}
	// Old FinishSendMOTD: HTML documents and http(s) URLs went to the vgui
	// HTML control; everything else became text pages. The port has no
	// browser surface: HTML renders stripped, URLs render as text with a
	// console note (D-17 - see the header).
	if (Buffer.starts_with("<!DOCTYPE html>") || Buffer.starts_with("<!doctype html>")) {
		Page = htmlToText(Buffer);
	} else if (Buffer.starts_with("http://") || Buffer.starts_with("https://")) {
		Page = Buffer;
		abc::ConsoleWarn("[AbcUi] motd is a URL; open it in a browser manually: %s\n", Buffer.c_str());
	} else {
		Page = Buffer;
	}
	Buffer.clear();
	if (Page.empty())
		return;
	if (Rml::Element* message = element("message"))
		message->SetInnerRML(framework::TextUtil::escapeRml(Page));
	HasContent = true;
	startCountdown();
}

void MotdPanel::loadMissionBrief() {
	// Old LoadMissionBrief: <map>.txt next to the bsp, through the vgui
	// filesystem. AbcUi reads mod-relative files directly (Assets/FreeImage
	// precedent); pfnGetLevelName yields "maps/<map>.bsp".
	if (LoadedMissionBrief)
		return;
	LoadedMissionBrief = true;
	const char* levelName = gEngfuncs.pfnGetLevelName();
	if (!levelName || !levelName[0])
		return;
	char mapPath[256];
	sprintf_s(mapPath, "%s", levelName);
	const std::string dotTxt = ".bsp";
	if (const size_t at = std::string(mapPath).rfind(dotTxt); at != std::string::npos)
		mapPath[at] = '\0';
	sprintf_s(mapPath, "%s.txt", mapPath);

	FILE* f = nullptr;
	if (fopen_s(&f, abc::ModPath(mapPath).c_str(), "rb") != 0 || !f)
		return;
	char chunk[1024];
	size_t read = 0;
	while ((read = fread(chunk, 1, sizeof(chunk), f)) > 0)
		Buffer.append(chunk, read);
	fclose(f);
}

void MotdPanel::showMotd() {
	if (motd::HudMotd && motd::HudMotd->value < 1.0f)
		return;
	loadMissionBrief();
	// Old ShowMotd re-paged the accumulated buffer; the single-page form
	// renders whatever accumulated and counts down again.
	if (!Page.empty()) {
		startCountdown();
		return;
	}
	finishSend();
}

void MotdPanel::onDocumentReady() {
	subscribe(netmsg::Motd, [this](int code, const char* text) {
		if (text && text[0]) {
			append(text);
		}
		else {
			// Old empty-chunk semantics: the server closes the window.
			HasContent = false;
			hide();
		}
		if (code > 0)
			finishSend();
	});
	if (abc::CChainedCommand* brief = commands::get("missionbriefing")) {
		subscribe(brief->Event(), [this]() {
			showMotd();
			return true;
		});
	}
	hide();
}

void MotdPanel::onData(double frameTime) {
	ClientTimeNow += frameTime;
	if (!HasContent || !Document)
		return;
	if (!isVisible())
		return;

	const double remaining = ShowTime - ClientTimeNow;
	if (remaining > 0.0) {
		if (Rml::Element* progress = element("progress")) {
			const float ratio = static_cast<float>(remaining / kKeepTime);
			progress->SetProperty("width", std::to_string(122.0f * std::clamp(ratio, 0.0f, 1.0f)) + "dp");
		}
		Fading = false;
		return;
	}
	// Fade window: opacity rides the CSS transition (fade_time 1s), the box
	// hides when the window closes.
	if (ClientTimeNow > ShowTime + kFadeTime) {
		HasContent = false;
		hide();
		return;
	}
	if (!Fading) {
		Fading = true;
		if (Rml::Element* box = element("box"))
			box->SetProperty("opacity", "0");
	}
}

}
