// services::GameUiBridge implementation - see GameUiBridge.h for the contract.
// T08: the native-UI suppression rules; deliberately engine/VGUI/Rml-free so
// the exact production logic links into the offline acceptance test.
#include "GameUiBridge.h"

namespace services {

namespace {

// ASCII case-insensitive equality; the module names are engine literals
// ("GameUI", "ServerBrowser"), never localized.
bool equalsIgnoreCase(const char* a, const char* b) {
	if (!a || !b)
		return false;
	while (*a && *b) {
		char ca = *a++;
		char cb = *b++;
		if (ca >= 'A' && ca <= 'Z')
			ca = static_cast<char>(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z')
			cb = static_cast<char>(cb - 'A' + 'a');
		if (ca != cb)
			return false;
	}
	return *a == *b;
}

PaintBucket bucketFor(const char* module) {
	if (equalsIgnoreCase(module, "GameUI"))
		return PaintBucket::kGameUi;
	if (equalsIgnoreCase(module, "ServerBrowser"))
		return PaintBucket::kServerBrowser;
	return PaintBucket::kOther;
}

} // namespace

bool NativeUiBridge::nativeUiModule(const char* module) {
	return equalsIgnoreCase(module, "GameUI") || equalsIgnoreCase(module, "ServerBrowser");
}

void NativeUiBridge::arm() {
	Armed = true;
}

void NativeUiBridge::disarm() {
	Armed = false;
}

bool NativeUiBridge::gatePaintAttempt(const char* module) {
	const PaintBucket bucket = bucketFor(module);
	++Counters.PaintAttempts[(int)bucket];
	// The module rule is the whole filter; while disarmed every attempt
	// passes (a half-installed gate must not half-suppress - T08 step 7's
	// atomic install).
	if (!Armed || !nativeUiModule(module))
		return false;
	++Counters.PaintBlocked[(int)bucket];
	return true;
}

bool NativeUiBridge::gateHitTestAttempt(const char* module) {
	const PaintBucket bucket = bucketFor(module);
	++Counters.HitTestAttempts[(int)bucket];
	if (!Armed || !nativeUiModule(module))
		return false;
	++Counters.HitTestBlocked[(int)bucket];
	return true;
}

bool NativeUiBridge::requestMenuUp(NativeUiOrigin origin) {
	++Counters.ActivateRequests[(int)origin];
	if (MenuUp)
		return false;
	MenuUp = true;
	// A new menu session re-arms the physical-press guard (a release missed
	// during a focus switch must not swallow the next close press).
	EscPressRouted = false;
	++Counters.MenuUpTransitions;
	if (MenuSink)
		MenuSink->onNativeMenuShow();
	return true;
}

bool NativeUiBridge::requestMenuDown(NativeUiOrigin origin) {
	++Counters.HideRequests[(int)origin];
	if (!MenuUp)
		return false;
	MenuUp = false;
	++Counters.MenuDownTransitions;
	if (MenuSink)
		MenuSink->onNativeMenuHide();
	return true;
}

ToggleAction NativeUiBridge::engineActivate() {
	if (!Armed)
		return ToggleAction::kNone;
	return requestMenuUp(NativeUiOrigin::kBaseUi) ? ToggleAction::kOpened : ToggleAction::kNone;
}

ToggleAction NativeUiBridge::engineHide() {
	if (!Armed || !MenuUp)
		return ToggleAction::kNone;
	// The shipped CGameUI::HideGameUI only acts when the engine reports a
	// connection; without a level the home page stays put (T04) and the
	// toggle is consumed with no action - exactly the vanilla no-op. A load
	// in progress has no level either, so a direct hide does not cancel it
	// (T14 owns the cancel UX).
	if (!HasLevel)
		return ToggleAction::kNone;
	// One route per physical press: the wrapper's post dispatch repeats the
	// same engine call (IsPost=false in both phases, hook-contract T5) and a
	// held ESC would re-fire the window key handler; both must coalesce into
	// one action. The mark clears on the physical key-up or at the next
	// raise, so the composition case (press 1 cancels, release, press 2
	// closes) keeps its two-step semantics.
	if (EscPressRouted) {
		++Counters.EscRepeatsSuppressed;
		return ToggleAction::kNone;
	}
	EscPressRouted = true;
	return PageShown ? ToggleAction::kRouteClose : ToggleAction::kStateClose;
}

NativeUiBridge& nativeUiBridge() {
	static NativeUiBridge bridge;
	return bridge;
}

}
