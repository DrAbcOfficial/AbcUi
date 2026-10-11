// framework::commands implementation - see Commands.h.
#include "Commands.h"

#include <cstring>
#include <memory>
#include <vector>

namespace commands {

namespace {

struct Entry {
	const char* name;
	std::unique_ptr<abc::CChainedCommand> command;
};

std::vector<Entry>& entries() {
	static std::vector<Entry> Entries;
	return Entries;
}

}

void registerAll() {
	static bool registered = false;
	if (registered)
		return;
	registered = true;
	// Old command.cpp RegisterCommandEvents list minus votemenu (the
	// GameUI vote dialog is D-16-deferred; the wire VoteMenu message drives
	// the panel instead).
	constexpr const char* kHooked[] = {
		"slot1", "slot2", "slot3", "slot4", "slot5",
		"slot6", "slot7", "slot8", "slot9", "slot10",
		"missionbriefing", "invnext", "invprev",
		"+showscores", "-showscores", "+attack",
	};
	auto& list = entries();
	list.reserve(std::size(kHooked));
	for (const char* name : kHooked)
		list.push_back({name, std::make_unique<abc::CChainedCommand>(name)});
}

abc::CChainedCommand* get(const char* name) {
	for (auto& entry : entries()) {
		if (strcmp(entry.name, name) == 0)
			return entry.command.get();
	}
	return nullptr;
}

}
