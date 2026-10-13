// services::console::ConsoleBackend implementation - see ConsoleBackend.h.
#include "ConsoleBackend.h"

#include <algorithm>

namespace services::console {

namespace {

bool isSpace(char c) {
	return c == ' ' || c == '\t';
}

std::string trimCopy(const char* text) {
	std::string trimmed(text);
	std::size_t begin = 0;
	while (begin < trimmed.size() && isSpace(trimmed[begin]))
		++begin;
	std::size_t end = trimmed.size();
	while (end > begin && isSpace(trimmed[end - 1]))
		--end;
	return trimmed.substr(begin, end - begin);
}

char lowerAscii(char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool prefixMatches(const std::string& prefix, const std::string& name) {
	if (prefix.size() > name.size())
		return false;
	for (std::size_t i = 0; i < prefix.size(); ++i) {
		if (lowerAscii(prefix[i]) != lowerAscii(name[i]))
			return false;
	}
	return true;
}

}

bool ConsoleBackend::sensitiveCommand(const std::string& command) {
	// First token only: the command name. Any token containing "password"
	// (password, sv_password, rcon_password, PASSWORD, ...) marks the line
	// secret-bearing; its arguments are assumed to be the secret.
	std::size_t end = 0;
	while (end < command.size() && !isSpace(command[end]))
		++end;
	std::string token = command.substr(0, end);
	std::transform(token.begin(), token.end(), token.begin(), lowerAscii);
	return token.find("password") != std::string::npos;
}

ConsoleBackend::SubmitResult ConsoleBackend::submit(const char* text) {
	if (!text || !*text) {
		++Stats.RejectedEmpty;
		return SubmitResult::kRejectedEmpty;
	}
	const std::string command = trimCopy(text);
	if (command.empty()) {
		++Stats.RejectedEmpty;
		return SubmitResult::kRejectedEmpty;
	}
	if (command.size() > kMaxCommandBytes || command.find('\n') != std::string::npos
		|| command.find('\r') != std::string::npos) {
		++Stats.RejectedInvalid;
		return SubmitResult::kRejectedInvalid;
	}
	if (!Seams_.clientCmd) {
		++Stats.Unavailable;
		return SubmitResult::kUnavailable;
	}
	std::string line = command;
	line += '\n';  // exactly one command line, exactly one terminator
	if (!Seams_.clientCmd(line.c_str())) {
		++Stats.Unavailable;
		return SubmitResult::kUnavailable;
	}
	++Stats.Executed;
	if (sensitiveCommand(command)) {
		++Stats.HistorySkippedSensitive;
	} else {
		if (History.size() >= kMaxHistory)
			History.pop_front();
		History.push_back(command);
	}
	return SubmitResult::kExecuted;
}

std::vector<std::string> ConsoleBackend::complete(const char* prefix, std::size_t max) const {
	std::vector<std::string> matches;
	if (max == 0)
		return matches;
	const std::size_t cap = max < kMaxCompletions ? max : kMaxCompletions;
	const std::string needle = prefix ? prefix : "";

	std::vector<std::string> names;
	const char* borrowed[kMaxEnumeration];
	const auto collect = [&](std::size_t (*list)(const char**, std::size_t)) {
		if (!list)
			return;
		const std::size_t count = list(borrowed, kMaxEnumeration);
		for (std::size_t i = 0; i < count && i < kMaxEnumeration; ++i) {
			if (borrowed[i] && *borrowed[i])
				names.emplace_back(borrowed[i]);  // copy now; never keep the pointer
		}
	};
	collect(Seams_.listCvars);
	collect(Seams_.listCommands);

	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	for (const std::string& name : names) {
		if (!prefixMatches(needle, name))
			continue;
		matches.push_back(name);
		if (matches.size() >= cap)
			break;
	}
	return matches;
}

ConsoleBackend& consoleBackend() {
	static ConsoleBackend Backend;
	return Backend;
}

}
