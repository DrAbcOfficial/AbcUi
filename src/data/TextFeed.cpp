// data::textfeed implementation - see TextFeed.h.
// from: ABCEnchance/src/hud/Viewport.cpp g_EventTextMsg handler (GBK->UTF-8).
// Port note: the old handler built its printf calls with the format/size
// arguments transposed (Q_snprintf(buffer, msg, sstr1) with V_snprintf
// semantics = destLen garbage, sstr1 as format). The port uses the standard
// TextMsg contract: msg is the format, sstr1..4 the arguments.

#include "TextFeed.h"

#include <regex>

#include <algorithm>
#include <cstdio>

#include "NetworkMessages.h"

#include "../framework/Localization.h"

namespace textfeed {

namespace {

// Old killfeed patterns (std::wregex before; UTF-8 byte matching is
// equivalent for these ASCII patterns).
const std::regex& suicidePattern() {
	static const std::regex pattern(" committed suicide.");
	return pattern;
}
const std::regex& killedPattern() {
	static const std::regex pattern(" was killed by a ");
	return pattern;
}
const std::regex& playerPattern() {
	static const std::regex pattern(" : (.*) : ");
	return pattern;
}

std::string matchPrefix(const std::smatch& match) {
	return match.prefix().str();
}

// Old findLocalize: leading-# tokens resolve through the language table.
std::string findLocalize(const std::string& token) {
	if (token.empty() || token.front() != '#')
		return token;
	std::string translated;
	if (framework::Localization::translate(translated, token))
		return translated;
	return token;
}

// D-17 #2 (R-17 closure): msg is the printf format by protocol (the HLSDK
// TextMsg contract - msg=format, params=arguments; the vanilla client does
// the same), but the raw server text used to go straight into sprintf_s:
// a stray %n/%x or a specifier/argument mismatch crashed or garbled the
// HUD. The format is now whitelisted - only "%s" pairs and "%%" survive,
// every other '%' becomes a literal "%%" - and the substitution runs
// through bounded snprintf.
std::string sanitizePrintfFormat(const std::string& format) {
	std::string out;
	out.reserve(format.size());
	for (size_t i = 0; i < format.size(); i++) {
		if (format[i] != '%') {
			out += format[i];
			continue;
		}
		if (i + 1 < format.size() && (format[i + 1] == 's' || format[i + 1] == '%')) {
			out += format[i];
			out += format[i + 1];
			i++;
		}
		else {
			out += "%%";
		}
	}
	return out;
}

// Formats the notice text: #token resolution on msg + params, then the
// sanitized msg as the printf format with the non-empty params substituted
// (old switch on the populated-param count).
std::string formatNotice(std::string msg, std::string params[4]) {
	msg = findLocalize(msg);
	int type = 0;
	for (int i = 0; i < 4; i++) {
		params[i] = findLocalize(params[i]);
		if (!params[i].empty())
			type++;
	}
	const std::string safeFormat = sanitizePrintfFormat(msg);
	char buffer[1024];
	switch (type) {
		case 1: snprintf(buffer, sizeof(buffer), safeFormat.c_str(), params[0].c_str()); break;
		case 2: snprintf(buffer, sizeof(buffer), safeFormat.c_str(), params[0].c_str(), params[1].c_str()); break;
		case 3: snprintf(buffer, sizeof(buffer), safeFormat.c_str(), params[0].c_str(), params[1].c_str(), params[2].c_str()); break;
		case 4: snprintf(buffer, sizeof(buffer), safeFormat.c_str(), params[0].c_str(), params[1].c_str(), params[2].c_str(), params[3].c_str()); break;
		default: return msg;
	}
	std::string text = buffer;
	if (!text.empty() && text.back() == '\n')
		text.pop_back();
	std::replace(text.begin(), text.end(), '\r', '\n');
	return text;
}

int onTextMsg(int target, const char* msg, const char* param1, const char* param2,
	const char* param3, const char* param4) {
	if (target == kPrintNotify) {
		if (msg[0] == '\0')
			return true;
		const std::string text = msg;
		std::smatch match;
		const auto localized = [](const char* token) {
			std::string out;
			framework::Localization::translate(out, token);
			return out;
		};
		if (std::regex_search(text, match, suicidePattern())) {
			KillFeed(matchPrefix(match).c_str(), "", localized("DeathMsg_Suicide").c_str());
			return false;
		}
		if (std::regex_search(text, match, killedPattern())) {
			std::string killer = match.suffix().str();
			// Old: k.erase(k.end() - 2) - the trailing ".\n" of the line.
			if (killer.size() >= 2)
				killer.erase(killer.size() - 2);
			KillFeed(matchPrefix(match).c_str(), killer.c_str(),
				localized("DeathMsg_MonsterKill").c_str());
			return false;
		}
		if (std::regex_search(text, match, playerPattern())) {
			// Old: inflictor = the " : X : " in-between group, attacker =
			// prefix, victim = suffix.
			KillFeed(match.suffix().str().c_str(), matchPrefix(match).c_str(), match[1].str().c_str());
			return false;
		}
	}

	if (target != kPrintNotify && target != kPrintCenter)
		return true;

	std::string params[4] = {param1, param2, param3, param4};
	const std::string formatted = formatNotice(msg, params);
	if (formatted.empty())
		return true;
	Notice(target, formatted.c_str());
	return false;
}

} // namespace

abc::CallbackList<void(const char*, const char*, const char*)> KillFeed;
abc::CallbackList<void(int, const char*)> Notice;

void init() {
	static bool registered = false;
	if (registered)
		return;
	registered = true;
	KillFeed = {};
	Notice = {};
	netmsg::TextMsg.append(onTextMsg);
}

}
