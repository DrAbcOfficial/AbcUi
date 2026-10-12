// abc_ui_modules - production module-lifecycle seam test (T03). Compiles the
// real src/modules/UiModuleRegistry.cpp (no algorithm copies) and drives it through
// recording fakes. Covers the T03 acceptance list: empty-registry dispatch,
// duplicate registration diagnostics, multi-module dispatch order, the
// double-shutdown latch, and a process exit before any context exists.
#include "modules/Module.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int WarnCount = 0;
namespace abc {
void ConsoleWarn(const char*, ...) { ++WarnCount; }
}

#define CHECK(x) do { if(!(x)) { std::cerr << __LINE__ << ": " << #x << "\n"; std::abort(); } } while(0)

namespace {

std::vector<std::string> Log;
size_t Cursor = 0;

class FakeModule final : public modules::IUiModule {
public:
	explicit FakeModule(const char* tag) : Tag(tag) { modules::registry().add(this); }

	const char* name() const override { return Tag; }
	void onInit() override { Log.push_back(std::string("init:") + Tag); }
	void onContextReady(Rml::Context* context) override {
		LastContext = context;
		Log.push_back(std::string("ready:") + Tag);
	}
	void onFrame(double frameTime) override {
		LastFrameTime = frameTime;
		Log.push_back(std::string("frame:") + Tag);
	}
	void onVidInit() override { Log.push_back(std::string("vid:") + Tag); }
	void onShutdown() override { Log.push_back(std::string("shutdown:") + Tag); }

	const char* Tag;
	Rml::Context* LastContext = nullptr;
	double LastFrameTime = 0.0;
};

void expect(const std::vector<std::string>& wanted) {
	CHECK(Log.size() >= Cursor + wanted.size());
	for (size_t i = 0; i < wanted.size(); i++)
		CHECK(Log[Cursor + i] == wanted[i]);
	Cursor += wanted.size();
}

void expectNothingMore() {
	CHECK(Log.size() == Cursor);
}

}

int main() {
	auto& reg = modules::registry();
	// Distinctive non-null stand-in: Module.h only forward-declares the
	// context, the registry contract is pointer passthrough.
	auto* context = reinterpret_cast<Rml::Context*>(static_cast<uintptr_t>(0xABCD1234));

	// 1. Empty registry: every dispatch is a silent no-op. The rehearsal
	// ends armed (contextReady re-arms the shutdown latch) so later
	// scenarios on this process-wide singleton start from the production
	// "initialised" shape.
	reg.initAll();
	reg.frameAll(0.0);
	reg.vidInitAll();
	reg.shutdownAll();
	reg.contextReadyAll(context);
	CHECK(WarnCount == 0);
	expectNothingMore();

	// 2. Null add: rejected without a diagnostic.
	reg.add(nullptr);
	CHECK(WarnCount == 0);
	expectNothingMore();

	// 3. Duplicate registration: diagnosable, dispatched once.
	static FakeModule a("a");
	reg.add(&a);
	CHECK(WarnCount == 1);
	reg.initAll();
	expect({"init:a"});
	expectNothingMore();

	// 4. Registration order == dispatch order for every event.
	static FakeModule b("b");
	static FakeModule c("c");
	reg.initAll();
	expect({"init:a", "init:b", "init:c"});
	expectNothingMore();

	// 5. Exit before any context exists: shutdown still reaches the modules,
	// no contextReady ever fired.
	reg.shutdownAll();
	expect({"shutdown:a", "shutdown:b", "shutdown:c"});
	CHECK(a.LastContext == nullptr && b.LastContext == nullptr && c.LastContext == nullptr);
	expectNothingMore();

	// 6. Double shutdown: latched, no re-dispatch.
	reg.shutdownAll();
	expectNothingMore();

	// 7. contextReady re-arms the cycle; pointer and frame time pass through.
	reg.contextReadyAll(context);
	expect({"ready:a", "ready:b", "ready:c"});
	CHECK(a.LastContext == context && b.LastContext == context && c.LastContext == context);
	reg.frameAll(0.25);
	expect({"frame:a", "frame:b", "frame:c"});
	CHECK(a.LastFrameTime == 0.25 && b.LastFrameTime == 0.25 && c.LastFrameTime == 0.25);
	reg.vidInitAll();
	expect({"vid:a", "vid:b", "vid:c"});
	expectNothingMore();

	// 8. Shutdown latches again; the next init cycle re-arms frames.
	reg.shutdownAll();
	expect({"shutdown:a", "shutdown:b", "shutdown:c"});
	reg.shutdownAll();
	expectNothingMore();
	reg.contextReadyAll(context);
	expect({"ready:a", "ready:b", "ready:c"});
	reg.frameAll(1.0);
	expect({"frame:a", "frame:b", "frame:c"});
	expectNothingMore();

	// 9. Capacity: fill to kMaxModules, the next registration warns and is
	// never dispatched.
	static FakeModule fillers[] = {
		FakeModule("f00"), FakeModule("f01"), FakeModule("f02"), FakeModule("f03"),
		FakeModule("f04"), FakeModule("f05"), FakeModule("f06"), FakeModule("f07"),
		FakeModule("f08"), FakeModule("f09"), FakeModule("f10"), FakeModule("f11"),
		FakeModule("f12"),
	};
	static_assert(sizeof(fillers) / sizeof(fillers[0]) == modules::ModuleRegistry::kMaxModules - 3);
	CHECK(WarnCount == 1);
	static FakeModule overflow("overflow");
	CHECK(WarnCount == 2);
	reg.frameAll(2.0);
	expect({
		"frame:a", "frame:b", "frame:c",
		"frame:f00", "frame:f01", "frame:f02", "frame:f03", "frame:f04", "frame:f05",
		"frame:f06", "frame:f07", "frame:f08", "frame:f09", "frame:f10", "frame:f11",
		"frame:f12",
	});
	expectNothingMore();
}
