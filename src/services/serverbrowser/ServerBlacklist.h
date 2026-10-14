#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace services::serverbrowser {
struct BlacklistRule {
	uint32_t Ip = 0;
	uint16_t Port = 0; // zero means all connection ports on this IPv4 address
	std::string Name;
	std::string address() const;
};

class ServerBlacklist {
public:
	using SaveSink = std::function<bool(const std::vector<BlacklistRule>&)>;
	static bool parseAddress(const std::string& Text, BlacklistRule& Output);
	void setSaveSink(SaveSink Sink) { Save = std::move(Sink); }
	void restore(std::vector<BlacklistRule> Value) { Rules = std::move(Value); ++Revision; }
	bool add(const std::string& Address, const std::string& Name = {});
	bool remove(const std::string& Address);
	bool contains(uint32_t Ip, uint16_t ConnectionPort) const;
	const std::vector<BlacklistRule>& rules() const { return Rules; }
	uint64_t revision() const { return Revision; }
private:
	bool commit(std::vector<BlacklistRule> Next);
	std::vector<BlacklistRule> Rules;
	SaveSink Save;
	uint64_t Revision = 0;
};
} // namespace services::serverbrowser
