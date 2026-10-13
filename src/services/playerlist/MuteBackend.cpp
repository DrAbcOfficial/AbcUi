// services::playerlist::muteBackend implementation - see MuteBackend.h.
//
// Resolution path (the shipped one, through public engine plumbing): the
// MetaHook API exposes the client factory (GetClientFactory -> the client
// DLL's CreateInterface). Asking that factory for "GameClientExports001" is
// exactly what the factory protocol is for; the shipped CGameUI did it with
// the factory list it received. When the name is not served, the interface
// does not exist on this install and the surface stays disabled.
//
// Slot layout (shipped call order, GameUI.dll):
//   vtable slot 2 (+0x08) read      - "is this slot's player muted"
//   vtable slot 3 (+0x0C) mute
//   vtable slot 4 (+0x10) unmute
// All three take the 1-based entity slot as their single argument and return
// a bool-ish value (the shipped dialog tests the read's low byte; the two
// toggles are called for effect). The proxy below matches that shape.
#include "MuteBackend.h"

#include <abc/Console.h>
#include <metahook.h>

#include <cstring>

namespace services {
namespace playerlist {

namespace {

// The interface's exported name and vtable shape. Kept as a minimal
// local declaration: the shipped header never exposed this interface publicly
// (it is GameUI-private), and the slots are pinned by the call sites above.
constexpr const char* kClientExportsInterface = "GameClientExports001";

using CreateInterfaceFn = void* (*)(const char* name, int* returnCode);
using MuteQueryFn = bool (*)(void* self, int index);
using MuteSetFn = void (*)(void* self, int index);

// One resolved probe. `object` null = unavailable.
struct Probe {
	bool Done = false;
	void* Object = nullptr;
	MuteQueryFn Query = nullptr;
	MuteSetFn SetMuted = nullptr;
	MuteSetFn SetUnmuted = nullptr;
};

Probe& probe() {
	static Probe state;
	return state;
}

// Reads one function pointer out of the object's vtable. The shipped code
// dereferences the vtable through the object's head; the same here. Returns
// null for an out-of-range or unmapped slot, which keeps every call below
// fail-closed.
template <typename Fn>
Fn slot(void* object, std::size_t index) {
	if (!object)
		return nullptr;
	void** vtable = *reinterpret_cast<void***>(object);
	if (!vtable)
		return nullptr;
	void* entry = vtable[index];
	return reinterpret_cast<Fn>(entry);
}

void resolve() {
	Probe& state = probe();
	if (state.Done)
		return;
	state.Done = true;
	if (!g_pMetaHookAPI || !g_pMetaHookAPI->GetClientFactory) {
		abc::ConsoleWarn("[AbcUi] player mute backend unavailable (no client factory API)\n");
		return;
	}
	CreateInterfaceFn factory = reinterpret_cast<CreateInterfaceFn>(g_pMetaHookAPI->GetClientFactory());
	if (!factory) {
		abc::ConsoleWarn("[AbcUi] player mute backend unavailable (client factory not served)\n");
		return;
	}
	int returnCode = 0;
	void* object = factory(kClientExportsInterface, &returnCode);
	if (!object) {
		// The shipped GameUI resolves this interface for its own player list;
		// an install whose client DLL does not serve the name has no shipped
		// mute backend either - the correct answer is "unavailable".
		//
		// Positive control: the SAME factory is asked for names the client DLL
		// is known to serve (its own interface strings, see the T20 evidence),
		// so the log distinguishes "the factory path is broken" from "this one
		// interface does not exist".
		static const char* kControlNames[] = {"SCClientDLL001", "VClientVGUI001"};
		char controls[96] = {};
		for (const char* control : kControlNames) {
			int code = 0;
			const bool served = factory(control, &code) != nullptr;
			// Keep the line short; missing controls are informational only.
			char piece[40];
			_snprintf_s(piece, sizeof(piece), _TRUNCATE, "%s%s=%d", controls[0] ? "," : "",
				control, served ? 1 : 0);
			strncat_s(controls, sizeof(controls), piece, _TRUNCATE);
		}
		abc::ConsoleWarn("[AbcUi] player mute backend unavailable (client DLL does not serve %s; factory control %s)\n",
			kClientExportsInterface, controls);
		return;
	}
	// MSVC x86 vtable layout for IGameClientExports : IBaseInterface: slot 0
	// is the scalar deleting destructor, slot 1 is the first method (the
	// shipped +4 call returns the game-name string), and the mute methods sit
	// at slots 2/3/4 = +8/+0xc/+0x10 - exactly the call sites in
	// CPlayerListDialog (FUN_10015d70). Indices are the shipped +N/4 values.
	MuteQueryFn query = slot<MuteQueryFn>(object, 2);
	MuteSetFn setMuted = slot<MuteSetFn>(object, 3);
	MuteSetFn setUnmuted = slot<MuteSetFn>(object, 4);
	if (!query || !setMuted || !setUnmuted) {
		abc::ConsoleWarn("[AbcUi] player mute backend unavailable (incomplete vtable)\n");
		return;
	}
	state.Object = object;
	state.Query = query;
	state.SetMuted = setMuted;
	state.SetUnmuted = setUnmuted;
	abc::ConsolePrint("[AbcUi] player mute backend resolved (%s)\n", kClientExportsInterface);
}

class Backend final : public IMuteBackend {
public:
	bool available() const override {
		const_cast<Backend*>(this)->ensure();
		return probe().Object != nullptr;
	}
	bool isMuted(int index) const override {
		const_cast<Backend*>(this)->ensure();
		const Probe& state = probe();
		return state.Query && state.Object && state.Query(state.Object, index);
	}
	bool mute(int index) override {
		ensure();
		const Probe& state = probe();
		if (!state.SetMuted || !state.Object)
			return false;
		state.SetMuted(state.Object, index);
		return true;
	}
	bool unmute(int index) override {
		ensure();
		const Probe& state = probe();
		if (!state.SetUnmuted || !state.Object)
			return false;
		state.SetUnmuted(state.Object, index);
		return true;
	}

private:
	void ensure() { resolve(); }
};

} // namespace

IMuteBackend* muteBackend() {
	static Backend backend;
	return &backend;
}

}
}
