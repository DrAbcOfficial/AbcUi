#pragma once

// framework::TextUtil - RML text escaping for server-derived strings.
// Old vgui drew message text through Label (plain text, no markup surface);
// the Rml port writes the same text through SetInnerRML, so raw server text
// would gain an element-injection surface the original never had (D-17 #3).
// Every server-derived string routed into SetInnerRML goes through this.

#include <string>

namespace framework {

namespace TextUtil {

inline std::string escapeRml(std::string text) {
	std::string out;
	out.reserve(text.size());
	for (char c : text) {
		switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			default: out += c;
		}
	}
	return out;
}

}

}
