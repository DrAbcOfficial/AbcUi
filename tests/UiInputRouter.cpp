// abc_ui_input_router - T06 acceptance for the production input router.
// Links the real UiInputRouter.cpp (no model copy); the Rml feed seam is a
// recording sink, the owners are configurable fakes. Every case pins one
// T06 constraint: pairing (dual-callback dedup = N downs never outlive
// their ups), keydown/up symmetry, wheel claims, auto-repeat ESC-once,
// nested tokens, owner destruction, pre-held passthrough, focus loss.
#include "framework/UiInputRouter.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

using framework::uiinput::Owner;
using framework::uiinput::OwnerTier;
using framework::uiinput::Route;
using framework::uiinput::Router;
using framework::uiinput::Sink;
using framework::uiinput::Token;

static const char* CurrentCase = "";
static int Failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::cerr << "FAIL " << CurrentCase << " line " << __LINE__ << ": " << #cond << "\n"; \
			++Failures; \
		} \
	} while (0)

namespace {

class RecorderSink final : public Sink {
public:
	std::vector<int> Downs;
	std::vector<int> Ups;
	int ExclusiveChanges = 0;
	bool ExclusiveState = false;

	void uiKeyDown(int keynum) override { Downs.push_back(keynum); }
	void uiKeyUp(int keynum) override { Ups.push_back(keynum); }
	void onExclusiveChange(bool exclusive) override {
		ExclusiveChanges++;
		ExclusiveState = exclusive;
	}

	// Pairing invariant, per key: no up without its down run, and every key
	// that was fed down ends with at least one up (auto-repeat runs are one
	// up for N downs - the keyboard model).
	bool paired() const {
		for (int k : Downs) {
			if (count(k, Ups) > count(k, Downs))
				return false;
			if (count(k, Downs) > 0 && count(k, Ups) == 0)
				return false;
		}
		for (int k : Ups) {
			if (count(k, Ups) > count(k, Downs))
				return false;
		}
		return true;
	}
	void clear() {
		Downs.clear();
		Ups.clear();
		ExclusiveChanges = 0;
	}

private:
	static size_t count(int key, const std::vector<int>& values) {
		return static_cast<size_t>(std::count(values.begin(), values.end(), key));
	}
};

class FakeOwner final : public Owner {
public:
	bool claimsKey(int down, int keynum) override {
		(void)down;
		return ClaimAll || std::find(Claimed.begin(), Claimed.end(), keynum) != Claimed.end();
	}
	bool claimsWheel() override { return Wheel; }
	void onClaimedKeyDown(int keynum) override {
		Actions.push_back(keynum);
		if (ActionHook)
			ActionHook(keynum);
	}
	void onOwnershipLost() override { LostCount++; }

	bool ClaimAll = false;
	bool Wheel = false;
	std::vector<int> Claimed;
	std::vector<int> Actions;
	std::function<void(int)> ActionHook;
	int LostCount = 0;
};

// Engine keynums under test (keydefs.h values, kept literal here so the
// router test never includes the HLSDK).
constexpr int kKeyW = 'w';
constexpr int kKeyY = 'y';
constexpr int kEscape = 27;
constexpr int kMouse1 = 241;
constexpr int kWheelUp = 240;

void route(Router& router, int down, int keynum, bool wheel = false) {
	(void)router.onKey(down, keynum, wheel);
}

}

int main() {
	{
		CurrentCase = "passthrough when unowned";
		RecorderSink sink;
		Router router(&sink);
		CHECK(!router.exclusive());
		CHECK(!router.wheelClaimed());
		const Route down = router.onKey(1, kKeyW, false);
		const Route up = router.onKey(0, kKeyW, false);
		CHECK(down.uiKey && !down.consumeEngine);
		CHECK(up.uiKey && !up.consumeEngine);
		CHECK(sink.paired() && sink.Downs.size() == 1);
		CHECK(sink.ExclusiveChanges == 0);
	}
	{
		CurrentCase = "claimed keys are swallowed and fed with pairing";
		RecorderSink sink;
		Router router(&sink);
		FakeOwner menu;
		menu.ClaimAll = true;
		Token token = router.acquire(&menu, OwnerTier::kMenu);
		CHECK(router.exclusive());
		CHECK(sink.ExclusiveChanges == 1 && sink.ExclusiveState);
		const Route down = router.onKey(1, kKeyW, false);
		const Route up = router.onKey(0, kKeyW, false);
		CHECK(down.uiKey && down.consumeEngine);
		CHECK(up.uiKey && up.consumeEngine);
		CHECK(sink.paired() && sink.Downs.size() == 1 && sink.Ups.size() == 1);
		// A spurious second up (the dual WndProc dispatch class of problem)
		// must not fabricate extra state.
		router.onKey(0, kKeyW, false);
		CHECK(sink.paired());
		token.release();
		CHECK(!router.exclusive());
		CHECK(sink.ExclusiveChanges == 2 && !sink.ExclusiveState);
		// Normal self-release does not fire the force-release notification.
		CHECK(menu.LostCount == 0);
	}
	{
		CurrentCase = "ESC fires exactly once per physical press";
		RecorderSink sink;
		Router router(&sink);
		FakeOwner menu;
		menu.ClaimAll = true;
		Token token = router.acquire(&menu, OwnerTier::kMenu);
		// Auto-repeat: three downs, one action, and ONE up pairs the run.
		route(router, 1, kEscape);
		route(router, 1, kEscape);
		route(router, 1, kEscape);
		route(router, 0, kEscape);
		CHECK(menu.Actions.size() == 1 && menu.Actions[0] == kEscape);
		CHECK(sink.paired());
		CHECK(sink.Ups.size() == 1);  // one up per physical press, not per repeat
		// Next physical press fires again.
		route(router, 1, kEscape);
		route(router, 0, kEscape);
		CHECK(menu.Actions.size() == 2);
		CHECK(sink.paired());
		token.release();
	}
	{
		CurrentCase = "owner releasing inside the ESC action pairs cleanly";
		RecorderSink sink;
		Router router(&sink);
		FakeOwner menu;
		menu.ClaimAll = true;
		Token token = router.acquire(&menu, OwnerTier::kMenu);
		sink.clear();
		menu.ActionHook = [&](int) { token.release(); };
		route(router, 1, kEscape);
		CHECK(!router.exclusive());
		route(router, 0, kEscape);  // real keyup lands on the empty stack
		CHECK(sink.paired());
		CHECK(menu.LostCount == 0);
		menu.ActionHook = nullptr;
	}
	{
		CurrentCase = "pre-held keys keep engine pairing across the toggle";
		RecorderSink sink;
		Router router(&sink);
		route(router, 1, kKeyW);  // W held in play
		FakeOwner menu;
		menu.ClaimAll = true;
		Token token = router.acquire(&menu, OwnerTier::kMenu);
		// W auto-repeat and its release pass through (engine pairing), never
		// fed to the UI (its held state was cleaned at acquire).
		const Route repeat = router.onKey(1, kKeyW, false);
		const Route release = router.onKey(0, kKeyW, false);
		CHECK(!repeat.uiKey && !repeat.consumeEngine);
		CHECK(!release.uiKey && !release.consumeEngine);
		CHECK(sink.Ups.size() == 1);  // only the acquire-time synthetic up
		// A key pressed after the menu opened is fully claimed.
		const Route fresh = router.onKey(1, kKeyY, false);
		CHECK(fresh.uiKey && fresh.consumeEngine);
		token.release();
		// The late Y release lands on the empty stack; the synth pairing up
		// at release already covered it, so no duplicate is fed.
		const Route lateUp = router.onKey(0, kKeyY, false);
		CHECK(!lateUp.uiKey && !lateUp.consumeEngine);
		CHECK(sink.paired());
	}
	{
		CurrentCase = "unclaimed keys observe-first while a HUD owns";
		RecorderSink sink;
		Router router(&sink);
		FakeOwner hud;
		hud.Claimed = {kMouse1};
		Token token = router.acquire(&hud, OwnerTier::kInteractiveHud);
		// Scoreboard pointer contract: mouse keys claimed, chat keys pass.
		const Route mouse = router.onKey(1, kMouse1, false);
		const Route chat = router.onKey(1, kKeyY, false);
		CHECK(mouse.uiKey && mouse.consumeEngine);
		CHECK(chat.uiKey && !chat.consumeEngine);
		token.release();
		CHECK(sink.paired());
	}
	{
		CurrentCase = "wheel claims";
		RecorderSink sink;
		Router router(&sink);
		CHECK(!router.wheelClaimed());
		FakeOwner hud;
		hud.Wheel = true;
		Token token = router.acquire(&hud, OwnerTier::kInteractiveHud);
		CHECK(router.wheelClaimed());
		token.release();
		CHECK(!router.wheelClaimed());
		// A non-claiming owner does not take the wheel.
		FakeOwner quiet;
		Token quietToken = router.acquire(&quiet, OwnerTier::kInteractiveHud);
		CHECK(!router.wheelClaimed());
		// Wheel pulses never enter the pairing state (down-only pulses).
		router.onKey(1, kWheelUp, true);
		router.onKey(1, kWheelUp, true);
		CHECK(sink.Downs.empty() && sink.Ups.empty());
		quietToken.release();
		CHECK(sink.paired());
	}
	{
		CurrentCase = "nested tokens: top owner decides, mid-stack release restores";
		RecorderSink sink;
		Router router(&sink);
		FakeOwner hud;
		hud.Claimed = {kMouse1};
		hud.Wheel = true;
		FakeOwner menu;
		menu.ClaimAll = true;
		menu.Wheel = true;
		Token hudToken = router.acquire(&hud, OwnerTier::kInteractiveHud);
		Token menuToken = router.acquire(&menu, OwnerTier::kMenu);
		CHECK(router.ownerCount() == 2);
		CHECK(sink.ExclusiveChanges == 1);  // one transition, not two
		// Menu on top: ESC reaches the menu, wheel claim follows the menu.
		route(router, 1, kEscape);
		CHECK(menu.Actions.size() == 1 && hud.Actions.empty());
		CHECK(router.wheelClaimed());
		// Menu pops (ESC closed it): the HUD contract is live again - mouse
		// keys claimed, keyboard passes, and the UI-held ESC pairs.
		menuToken.release();
		CHECK(router.ownerCount() == 1 && router.exclusive());
		CHECK(sink.ExclusiveChanges == 1);
		const Route chat = router.onKey(1, kKeyY, false);
		CHECK(chat.uiKey && !chat.consumeEngine);
		const Route mouse = router.onKey(1, kMouse1, false);
		CHECK(mouse.consumeEngine);
		CHECK(router.wheelClaimed());
		hudToken.release();
		CHECK(!router.exclusive());
		CHECK(sink.ExclusiveChanges == 2);
		CHECK(sink.paired());
	}
	{
		CurrentCase = "tier priority: capture sits above menu";
		RecorderSink sink;
		Router router(&sink);
		FakeOwner menu;
		menu.ClaimAll = true;
		FakeOwner capture;
		capture.ClaimAll = true;
		Token menuToken = router.acquire(&menu, OwnerTier::kMenu);
		Token captureToken = router.acquire(&capture, OwnerTier::kCapture);
		route(router, 1, kEscape);
		CHECK(menu.Actions.empty() && capture.Actions.size() == 1);
		captureToken.release();
		route(router, 1, kEscape);
		CHECK(menu.Actions.size() == 1);
		menuToken.release();
		CHECK(sink.paired());
	}
	{
		CurrentCase = "owner destruction and stale tokens";
		RecorderSink sink;
		Router router(&sink);
		FakeOwner owner;
		owner.ClaimAll = true;
		{
			Token token = router.acquire(&owner, OwnerTier::kMenu);
			CHECK(router.exclusive());
		}
		CHECK(!router.exclusive());
		CHECK(owner.LostCount == 0);  // RAII release, not a force-release
		// A moved-from token releases as a no-op.
		Token moved = router.acquire(&owner, OwnerTier::kMenu);
		Token stale = std::move(moved);
		moved.release();
		CHECK(router.ownerCount() == 1);
		stale.release();
		CHECK(router.ownerCount() == 0);
		// Force-release marks held() stale; a late release() must not remove
		// an unrelated newer token.
		Token doomed = router.acquire(&owner, OwnerTier::kMenu);
		FakeOwner other;
		Token kept = router.acquire(&other, OwnerTier::kMenu);
		router.onFocusLost();
		CHECK(!doomed.held() && !kept.held());
		CHECK(owner.LostCount == 1 && other.LostCount == 1);
		doomed.release();
		kept.release();
		CHECK(router.ownerCount() == 0);
		CHECK(owner.LostCount == 1);
		CHECK(sink.paired());
	}
	{
		CurrentCase = "focus loss: cleanup, notification, idempotency";
		RecorderSink sink;
		Router router(&sink);
		FakeOwner menu;
		menu.ClaimAll = true;
		Token token = router.acquire(&menu, OwnerTier::kMenu);
		route(router, 1, kKeyW);
		route(router, 1, kEscape);
		CHECK(sink.Downs.size() == 2);
		router.onFocusLost();
		CHECK(!router.exclusive());
		CHECK(menu.LostCount == 1);
		CHECK(sink.paired());  // both held keys got synthetic ups
		CHECK(sink.Ups.size() == 2);
		CHECK(!sink.ExclusiveState && sink.ExclusiveChanges == 2);
		// The pre/post double dispatch of the surface WndProc must not
		// double-notify or double-release.
		router.onFocusLost();
		router.onFocusLost();
		CHECK(menu.LostCount == 1);
		CHECK(sink.ExclusiveChanges == 2);
		// Engine down-state was cleared as stale: the next acquire
		// re-baselines instead of treating the dead W as pre-held.
		FakeOwner again;
		again.ClaimAll = true;
		Token reToken = router.acquire(&again, OwnerTier::kMenu);
		const Route freshW = router.onKey(1, kKeyW, false);
		CHECK(freshW.uiKey && freshW.consumeEngine);
		reToken.release();
		CHECK(sink.paired());
	}
	{
		CurrentCase = "reset equals focus loss for teardown";
		RecorderSink sink;
		Router router(&sink);
		FakeOwner owner;
		owner.ClaimAll = true;
		Token token = router.acquire(&owner, OwnerTier::kMenu);
		route(router, 1, kKeyW);
		router.reset();
		CHECK(!router.exclusive());
		CHECK(owner.LostCount == 1);
		CHECK(sink.paired());
		router.reset();  // idempotent
		CHECK(owner.LostCount == 1);
	}
	{
		CurrentCase = "acquire-time UI cleanup and depth cap";
		RecorderSink sink;
		Router router(&sink);
		route(router, 1, kKeyW);  // UI holds W from the observe-first mirror
		FakeOwner first;
		first.ClaimAll = true;
		Token token = router.acquire(&first, OwnerTier::kInteractiveHud);
		CHECK(sink.Ups.size() == 1);  // synthetic pairing up at acquire
		// A nested acquire does not re-snapshot (the baseline is the first
		// session's) and does not re-fire the exclusive transition.
		const int changesBefore = sink.ExclusiveChanges;
		FakeOwner second;
		second.ClaimAll = true;
		Token nested = router.acquire(&second, OwnerTier::kMenu);
		CHECK(sink.ExclusiveChanges == changesBefore);
		// Depth cap: past kMaxOwners the acquire fails with an empty token
		// instead of corrupting the stack.
		FakeOwner fillers[Router::kMaxOwners];
		Token extra[Router::kMaxOwners];
		size_t acquired = 0;
		while (acquired < Router::kMaxOwners) {
			extra[acquired] = router.acquire(&fillers[acquired], OwnerTier::kMenu);
			if (!extra[acquired].held())
				break;
			acquired++;
		}
		CHECK(acquired == Router::kMaxOwners - 2);
		for (size_t i = 0; i < acquired; i++)
			extra[i].release();
		CHECK(router.ownerCount() == 2);
		nested.release();
		token.release();
		CHECK(!router.exclusive());
		CHECK(sink.paired());
	}

	if (Failures) {
		std::cerr << "abc_ui_input_router: " << Failures << " failure(s)\n";
		return 1;
	}
	std::cout << "abc_ui_input_router: all cases passed\n";
	return 0;
}
