// gameui feature module - assembly point of the menu replacement domain.
// T03 shipped the empty seam; T05 hung the minimal interactive page off the
// module contract; T06 added the input-ownership owner; T08 made the module
// the INativeMenuSink of the suppression bridge.
//
// T09 completes the loop: the module owns the production MenuController and
// EngineUiBackend. Every menu transition converges here -
//   * the bridge raises/lowers the menu (engine ESC, cold start, focus
//     loss) and the sink syncs the controller (kMenuShown / kMenuHidden),
//   * the page/overlay windows report fixed action intents (resume,
//     disconnect, quit confirm, open stub pages, console placeholder),
//   * the router owner turns ESC into kMenuToggle,
// and after every dispatch syncMenuPresentation() re-renders the document
// set from the controller state (never from DOM inspection), while the
// backend executes the typed actions (resume through the proven BaseUI
// hide path, disconnect/quit as the verbatim original command strings,
// singleplayer pause parity on the in-game transitions).
#include "../Module.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Debugger.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Paths.h>

#include <keydefs.h>
#include <ivoicetweak.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../framework/Localization.h"
#include "../../panels/console/ConsolePage.h"
#include "../../panels/loading/LoadingWindows.h"
#include "../../panels/mainmenu/MainMenuPage.h"
#include "../../panels/menushared/MenuShellWindows.h"
#include "../../panels/settings/SettingsPage.h"
#include "../../panels/settings/VideoConfirmPage.h"
#include "../../rmlui/InputBridge.h"
#include "../../rmlui/TextInput.h"
#include "../../rmlui/VGUI2ExtensionImport.h"
#include "../../services/EngineUiBackend.h"
#include "../../services/GameUiBridge.h"
#include "../../services/console/ConsoleBackend.h"
#include "../../services/console/ConsoleBuffer.h"
#include "../../services/loading/LoadingModel.h"
#include "../../services/settings/KeyBindModel.h"
#include "../../services/settings/SettingsModel.h"
#include "../../services/settings/VideoModeModel.h"
#include "../../services/settings/VideoPendingStore.h"
#include "../../services/settings/VoiceModel.h"
#include "MenuController.h"

#include <chrono>
#include <deque>

#include <io.h>

namespace gameui {

namespace {

// The production backend: typed actions in, engine calls out, every engine
// touchpoint behind a thin thunk (the offline backend test injects its own
// recordings through the same Seams shape).
services::EngineUiBackend& menuBackend() {
	static services::EngineUiBackend backend = [] {
		services::EngineUiBackend created;
		services::EngineUiBackend::Seams seams;
		seams.clientCmd = [](const char* command) { abc::EngineClientCmd(command); };
		seams.hideGameUi = []() { return vgui2ext::hideGameUi(); };
		seams.maxClients = []() { return gEngfuncs.GetMaxClients(); };
		seams.bridge = &services::nativeUiBridge();
		created.setSeams(seams);
		return created;
	}();
	return backend;
}

// The production cvar seams for the settings model (T10): reads through the
// cvar pointer, writes through the engine Cvar_SetValue API - never a
// concatenated "name value" command string.
services::settings::SettingsModel::Seams settingsSeams() {
	services::settings::SettingsModel::Seams seams;
	seams.exists = [](const char* name) { return abc::CVAR_GET_POINTER(name) != nullptr; };
	seams.readFloat = [](const char* name) { return abc::CVAR_GET_FLOAT(name); };
	seams.writeFloat = [](const char* name, float value) {
		if (!abc::CVAR_GET_POINTER(name))
			return false;
		abc::CVAR_SET_FLOAT(name, value);
		return true;
	};
	// T13 string rows: real engine cvar string writes (the original page
	// sent `name "%s"` / `model %s` commands; the model validated tokens
	// BEFORE either path - and the direct API keeps user text out of the
	// command channel entirely).
	seams.writeString = [](const char* name, const char* value) {
		if (!abc::CVAR_GET_POINTER(name) || !value)
			return false;
		abc::CVAR_SET_STRING(name, value);
		return true;
	};
	seams.readString = [](const char* name) -> const char* {
		const cvar_t* cvar = abc::CVAR_GET_POINTER(name);
		return cvar ? cvar->string : nullptr;
	};
	// T13 command toggles (in_mlook/in_jlook): IGameUIFuncs::IsKeyDown for
	// the truth, the engine command channel for +mlook/-mlook (buffered -
	// the model's kQueued settle covers the flush beat).
	seams.isKeyDown = [](const char* stateName) { return vgui2ext::isKeyDown(stateName); };
	seams.clientCmd = [](const char* command) {
		if (!command)
			return false;
		abc::EngineClientCmd(command);
		return true;
	};
	return seams;
}

// T13 voice seams (IVoiceTweak through cl_enginefuncs.pVoiceTweak - the
// interface slot the shipped GameUI voice page reads at engine slot +0x15c).
// Everything null-guards: a missing interface fail-closes the voice section
// in the model's snapshot.
services::settings::VoiceModel::Seams voiceSeams() {
	services::settings::VoiceModel::Seams seams;
	seams.tweakInterface = []() -> void* { return gEngfuncs.pVoiceTweak; };
	seams.startTest = []() -> int {
		return gEngfuncs.pVoiceTweak ? gEngfuncs.pVoiceTweak->StartVoiceTweakMode() : 0;
	};
	seams.stopTest = []() {
		if (gEngfuncs.pVoiceTweak)
			gEngfuncs.pVoiceTweak->EndVoiceTweakMode();
	};
	seams.getControl = [](int which) -> float {
		return gEngfuncs.pVoiceTweak ? gEngfuncs.pVoiceTweak->GetControlFloat(
			static_cast<VoiceTweakControl>(which)) : 0.0f;
	};
	seams.setControl = [](int which, float value) -> bool {
		if (!gEngfuncs.pVoiceTweak)
			return false;
		gEngfuncs.pVoiceTweak->SetControlFloat(static_cast<VoiceTweakControl>(which), value);
		return true;
	};
	seams.speakingVolume = []() -> int {
		return gEngfuncs.pVoiceTweak ? gEngfuncs.pVoiceTweak->GetSpeakingVolume() : 0;
	};
	return seams;
}

// The production bind seams (T11): reads through IGameUIFuncs (the same
// key table the bind command writes - hw.dll evidence in KeyBindModel.h),
// writes as verbatim `bind`/`unbind` commands through pfnClientCmd. The
// engine command buffer flushes between frames; the model's kQueued/verify
// pair covers that beat.
services::settings::KeyBindModel::Seams bindSeams() {
	services::settings::KeyBindModel::Seams seams;
	seams.bindingForKey = [](int keynum) -> const char* { return vgui2ext::keyBindingForKey(keynum); };
	seams.keyNameForKey = [](int keynum) -> const char* { return vgui2ext::keyNameForKey(keynum); };
	seams.keyStringToKeyNum = [](const char* name) { return vgui2ext::keyStringToKeyNum(name); };
	seams.clientCmd = [](const char* command) {
		abc::EngineClientCmd(command);
		return true; // handed to the engine buffer; truth rides the re-read
	};
	return seams;
}

// T12: the deferred `_restart`. Live-caught on the shipped engine: a
// `_restart` that shares the command flush with the `_setvideomode` writes
// it is meant to apply lets the in-process video re-init come up at the OLD
// mode (the engine re-initializes the display from its startup snapshot, not
// the freshly written registry values) - while the same writes followed by a
// `_restart` on a LATER flush (the shipped page's own two-step behaviour, and
// the raw-console repro) apply correctly, three live runs each way. The
// production thunk therefore holds the restart for the next module frame:
// the model still issues it as the last command of its transaction (offline
// semantics unchanged), only the physical engine emission is deferred one
// frame so the writes land in their own flush first.
bool DeferredVideoRestart = false;

// A confirmation raise queued by the same transaction that (re)starts the
// video: presenting the page touches the Rml context, the bridge and the
// document set - none of that may run in the same frame the engine is asked
// to tear down and re-initialize the display (live-caught: an immediate
// raise after the restart command left the process windowless). The frame
// drain raises it once Context is back.
bool DeferredVideoConfirmRaise = false;

// The production video seams (T12): actual-mode/list reads through the
// IGameUIFuncs vtable slots 5/6/7 (vgui2ext thunks), writes as the verbatim
// engine commands through pfnClientCmd, the pending record through
// abc::Config's atomic JSON writer, clocks through chrono.
services::settings::VideoModeModel::Seams videoSeams() {
	services::settings::VideoModeModel::Seams seams;
	seams.actualMode = [](services::settings::VideoMode& out) {
		return vgui2ext::currentVideoMode(out.Width, out.Height, out.Bpp, out.Windowed);
	};
	seams.modeList = [](services::settings::VideoMode* out, int max) {
		vgui2ext::video::Mode buffer[services::settings::VideoModeModel::kMaxModes];
		if (max > services::settings::VideoModeModel::kMaxModes)
			max = services::settings::VideoModeModel::kMaxModes;
		const int count = vgui2ext::videoModeList(buffer, max);
		for (int i = 0; i < count; ++i) {
			out[i].Width = buffer[i].Width;
			out[i].Height = buffer[i].Height;
			out[i].Bpp = buffer[i].Bpp;
		}
		return count;
	};
	seams.clientCmd = [](const char* command) {
		if (command && std::strcmp(command, "_restart\n") == 0) {
			DeferredVideoRestart = true; // never shares the flush with its writes
			return true;
		}
		abc::EngineClientCmd(command);
		return true; // handed to the engine buffer (same contract as bind writes)
	};
	seams.savePending = [](const services::settings::VideoPendingRecord& record) {
		return services::settings::VideoPendingStore::save(record);
	};
	seams.loadPending = [](services::settings::VideoPendingRecord& out) {
		return services::settings::VideoPendingStore::load(out);
	};
	seams.monotonicSeconds = [] {
		using clock = std::chrono::steady_clock;
		static const auto start = clock::now();
		return std::chrono::duration<double>(clock::now() - start).count();
	};
	seams.wallMs = [] {
		using clock = std::chrono::system_clock;
		return static_cast<long long>(
			std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
				.count());
	};
	return seams;
}

// Mod-dir text file (kb_act.lst / kb_def.lst / valve_english.txt), decoded
// from its shipped encoding (valve_english.txt is UTF-16LE - see
// KeyBindModel::decodeResourceText). Empty on any failure - the bind model
// treats missing sources as fail-closed.
std::string readModFile(const char* relativePath) {
	FILE* file = nullptr;
	if (fopen_s(&file, abc::ModPath(relativePath).c_str(), "rb") != 0 || !file)
		return {};
	std::string raw;
	char buffer[4096];
	size_t got = 0;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0)
		raw.append(buffer, got);
	fclose(file);
	return services::settings::KeyBindModel::decodeResourceText(raw.data(), raw.size());
}

using gameui::menu::EventKind;
using gameui::menu::MenuEvent;
using gameui::menu::MenuOverlay;
using gameui::menu::MenuPage;

// --- T13 option scans --------------------------------------------------------
// The multiplayer enumerations (model directories, spraypaint bitmaps) come
// from the mod dir exactly like the shipped GameUI list fills (COptionsSub*
// ctor helpers scanned models/player/*.* / logos/* through the engine
// filesystem with the GAME path). Plain directory enumeration on the trusted
// mod path; both scans are read-only and bounded.

// Directories under <mod>/<relativeDir> (model list = models/player), names
// sorted, dot entries skipped.
std::vector<std::string> scanModDirectories(const char* relativeDir) {
	std::vector<std::string> names;
	_finddata_t data;
	const std::string mask = abc::ModPath(relativeDir) + "/*";
	const intptr_t handle = _findfirst(mask.c_str(), &data);
	if (handle == -1)
		return names;
	do {
		if ((data.attrib & _A_SUBDIR) && data.name[0] != '.')
			names.push_back(data.name);
	} while (_findnext(handle, &data) == 0);
	_findclose(handle);
	std::sort(names.begin(), names.end());
	if (names.size() > 512)
		names.resize(512);
	return names;
}

// File basenames (extension stripped) under <mod>/<relativeDir>; skips the
// engine's remapped.bmp helper exactly like the original spray list.
std::vector<std::string> scanModFileBasenames(const char* relativeDir) {
	std::vector<std::string> names;
	_finddata_t data;
	const std::string mask = abc::ModPath(relativeDir) + "/*";
	const intptr_t handle = _findfirst(mask.c_str(), &data);
	if (handle == -1)
		return names;
	do {
		if (data.attrib & _A_SUBDIR)
			continue;
		const std::string name = data.name;
		if (name.empty() || name[0] == '.' || name == "remapped.bmp")
			continue;
		const auto dot = name.rfind('.');
		names.push_back(dot == std::string::npos ? name : name.substr(0, dot));
	} while (_findnext(handle, &data) == 0);
	_findclose(handle);
	std::sort(names.begin(), names.end());
	if (names.size() > 512)
		names.resize(512);
	return names;
}

// The advanced-rows table: the shipped user.scr is the live source (the
// original MultiplayerAdvancedDialog parses it the same way); the localized
// sibling (<gamedir>_<lang>/user.scr) wins when present - the engine's own
// search order. Read failure or an empty parse keeps the static mirror of
// the shipped script (AdvancedScriptRows). The arena outlives the model.
struct AdvancedTable {
	services::settings::SettingsModel::UserScrTable parsed;
	std::vector<services::settings::SettingRow> rows;
	bool valid = false;
};

AdvancedTable buildAdvancedTable() {
	AdvancedTable table;
	// Localized variant first (svencoop_schinese ships translated prompts):
	// the path is CWD-relative like abc::ModPath - the engine mounts the
	// language directory next to the mod directory.
	std::string text;
	{
		const char* language = vgui2ext::currentLanguage();
		if (language && *language && std::strcmp(language, "english") != 0) {
			std::string path = abc::GameDir();
			path += "_";
			path += language;
			path += "/user.scr";
			FILE* file = nullptr;
			if (fopen_s(&file, path.c_str(), "rb") == 0 && file) {
				char buffer[4096];
				size_t got = 0;
				while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0)
					text.append(buffer, got);
				fclose(file);
			}
		}
	}
	if (text.empty())
		text = readModFile("user.scr");
	if (services::settings::SettingsModel::parseUserScr(text.c_str(), table.parsed)
		&& !table.parsed.Rows.empty()) {
		table.rows = table.parsed.Rows;
		table.valid = true;
		return table;
	}
	// Static fallback: the exact mirror of the shipped script.
	table.rows.assign(services::settings::AdvancedScriptRows,
		services::settings::AdvancedScriptRows + services::settings::AdvancedScriptCount);
	table.valid = true;
	return table;
}

class GameUiModule final : public modules::IUiModule, private framework::uiinput::Owner, private services::INativeMenuSink {
public:
	GameUiModule()
		: Controller(&menuBackend()), CaptureOwner(this) {
		SettingsModel.setSeams(settingsSeams());
		BindModel.setSeams(bindSeams());
		VideoModel.setSeams(videoSeams());
		VoiceModel.setSeams(voiceSeams());
		modules::registry().add(this);
		services::nativeUiBridge().setMenuSink(this);
		Instance = this;
	}

	const char* name() const override { return "gameui"; }

	void onInit() override {
		// T13 description table: the first batch + the advanced rows from the
		// live (localized) user.scr - the shipped script is the same source
		// the original advanced dialog parses. A failed parse keeps the
		// static mirror of the shipped script (buildAdvancedTable), so the
		// table is never empty. Must run before the first snapshot (page
		// present) - the model owns row POINTERS into SettingsTable/Advanced,
		// both module members that outlive the model.
		Advanced = buildAdvancedTable();
		SettingsTable.assign(services::settings::FirstBatchRows,
			services::settings::FirstBatchRows + services::settings::FirstBatchCount);
		SettingsTable.insert(SettingsTable.end(), Advanced.rows.begin(), Advanced.rows.end());
		SettingsModel.setTable(SettingsTable.data(), static_cast<int>(SettingsTable.size()));
		// The multiplayer enumerations: directories (models/player) and the
		// spray basenames (logos) - the same scans the shipped list fills
		// run; empty scans leave the rows readable but unchangeable (the
		// live engine value stays selectable).
		SettingsModel.setStringOptions("model", scanModDirectories("models/player"));
		SettingsModel.setStringOptions("cl_logofile", scanModFileBasenames("logos"));
		// T11 bind sources live in the mod dir (engine-owned data, not abcui
		// assets): the action list, the factory defaults and the label table.
		// Missing files degrade to an empty action table - the settings page
		// renders the bind section as unavailable (fail-closed).
		if (!BindModel.loadActions(readModFile("gfx/shell/kb_act.lst").c_str()))
			abc::ConsoleWarn("[AbcUi] kb_act.lst malformed - key bindings unavailable\n");
		BindModel.loadDefaults(readModFile("gfx/shell/kb_def.lst").c_str());
		BindModel.loadLabels(readModFile("resource/valve_english.txt").c_str());
#ifndef NDEBUG
		// HUD_Init guarded block only (plan 08 §4.9 red line).
		abc::ADD_COMMAND("abc_ui_menu_page", menuPageCommand);
		abc::ADD_COMMAND("abc_ui_bind_status", bindStatusCommand);
		abc::ADD_COMMAND("abc_ui_settings_status", settingsStatusCommand);
		abc::ADD_COMMAND("abc_ui_settings_apply", settingsApplyCommand);
		abc::ADD_COMMAND("abc_ui_voice_status", voiceStatusCommand);
		abc::ADD_COMMAND("abc_ui_voice_boost", voiceBoostCommand);
		abc::ADD_COMMAND("abc_ui_voice_test", voiceTestCommand);
		abc::ADD_COMMAND("abc_ui_voice_apply", voiceApplyCommand);
		abc::ADD_COMMAND("abc_ui_video_status", videoStatusCommand);
		abc::ADD_COMMAND("abc_ui_video_scroll", videoScrollCommand);
		abc::ADD_COMMAND("abc_ui_video_stage", videoStageCommand);
		abc::ADD_COMMAND("abc_ui_video_apply", videoApplyCommand);
		abc::ADD_COMMAND("abc_ui_video_keep", videoKeepCommand);
		abc::ADD_COMMAND("abc_ui_video_revert", videoRevertCommand);
		abc::ADD_COMMAND("abc_ui_video_window", videoWindowCommand);
		abc::ADD_COMMAND("abc_ui_loading_connect", loadingConnectCommand);
		abc::ADD_COMMAND("abc_ui_loading_cancel", loadingCancelCommand);
		abc::ADD_COMMAND("abc_ui_loading_password", loadingPasswordCommand);
		abc::ADD_COMMAND("abc_ui_loading_status", loadingStatusCommand);
		abc::ADD_COMMAND("abc_ui_console_ui_status", consoleUiStatusCommand);
		abc::ADD_COMMAND("abc_ui_console_ui_toggle", consoleUiToggleCommand);
#endif
	}

	void onFrame(double frameTime) override {
		(void)frameTime;
		// T14: the engine's loading stream is drained first and independently
		// of menu presentation - the controller transitions (attempt start,
		// loading, success/failure/password) must not wait for Rml readiness,
		// and the drain is the ONLY consumer of the copied feed.
		drainLoadingFeed();
		// T12: drain the deferred video restart FIRST and independently of
		// menu presentation - the transaction owns it, not the page (see the
		// DeferredVideoRestart note above).
		const bool restartDrained = DeferredVideoRestart;
		if (DeferredVideoRestart) {
			DeferredVideoRestart = false;
			abc::EngineClientCmd("_restart\n");
		}
		// The queued confirmation raise never shares a frame with a restart
		// drain (see the DeferredVideoConfirmRaise note): one frame apart.
		if (!restartDrained && DeferredVideoConfirmRaise && Context && VideoConfirm) {
			DeferredVideoConfirmRaise = false;
			raiseVideoConfirmation();
		}
		if (!MenuPresented || !Shell || !Context)
			return;
		// T11 bind verification beat: pfnClientCmd lands in the engine
		// command buffer, which flushes between frames. kQueued rows resolve
		// on the first tick after Apply; a change re-projects the page once.
		if (SettingsPresented && Settings && BindModel.hasQueued()) {
			if (BindModel.verify())
				Settings->render();
		}
		// T13: the same beat covers the cvar model's kCommandToggle rows
		// (+mlook/-mlook settle against IsKeyDown) and the mic-test meter
		// (the level readout is the GetSpeakingVolume truth, one render per
		// frame while the session is up - not an animation).
		if (SettingsPresented && Settings && (SettingsModel.hasQueued() || VoiceModel.testing())) {
			if (SettingsModel.hasQueued())
				SettingsModel.snapshot();
			Settings->renderVoiceSection();
			if (SettingsModel.hasQueued())
				Settings->render();
		}
		// T12 confirmation beat: the monotonic deadline runs on real time.
		// The tick fires the auto-revert even when the window is hidden or
		// unfocused - the deadline never extends. A revert/auto-resolve
		// re-projects the dialog (or closes it) once.
		if (VideoModel.awaitingConfirm()) {
			const bool reverted = VideoModel.tick();
			tickVideoTimeout();
			if (reverted || !VideoModel.awaitingConfirm())
				syncVideoConfirmPage();
			else if (VideoConfirm)
				VideoConfirm->render();
		}
		// T16 console stream: while the overlay is presented, the page
		// appends the buffer's new lines once per frame (bounded incremental
		// projection; no-op when nothing new arrived).
		if (ConsolePresented && Console)
			Console->render();
		// Per-frame focus guard (T09 step 5): while the menu is presented,
		// the context focus must stay inside one of the menu documents.
		// Foreign holders exist (the Rml debug hook document grabs the focus
		// whenever the last menu document hides - live-verified), and a
		// sync-time guard alone loses to thieves that act between frames.
		// The debugger's own windows are exempt (a developer using F12 must
		// keep their focus).
		if (Rml::Debugger::IsVisible())
			return;  // a developer using the F12 tools keeps their focus
		Rml::Element* focus = Context->GetFocusElement();
		if (focus) {
			Rml::ElementDocument* owner = focus->GetOwnerDocument();
			// Only a VISIBLE menu document holds legitimate focus - a hidden
			// one (stub/overlay just dismissed) is exactly the stale state
			// this guard exists to repair.
			if (owner && owner->IsVisible()
				&& (owner == Shell->document() || owner == (SubStub ? SubStub->document() : nullptr)
					|| owner == (Console ? Console->document() : nullptr)
					|| owner == (Settings ? Settings->document() : nullptr)
					|| owner == (ConfirmQuit ? ConfirmQuit->document() : nullptr)
					|| owner == (VideoConfirm ? VideoConfirm->document() : nullptr)
					|| owner == (Loading ? Loading->document() : nullptr)
					|| owner == (ConnError ? ConnError->document() : nullptr)
					|| owner == (Password ? Password->document() : nullptr)
					|| owner == (DirectConnect ? DirectConnect->document() : nullptr)))
				return;
		}
		const auto& state = Controller.state();
		// T12: the display-mode confirmation outranks every other surface -
		// it holds the safe default focus while it is up.
		if (VideoConfirmPresented && VideoConfirm) {
			VideoConfirm->refocusDefault();
			return;
		}
		// T14 surfaces, top-most first (the same order the sync shows them).
		if (state.overlays.contains(MenuOverlay::kConfirmQuit) && ConfirmQuit)
			ConfirmQuit->refocusDefault();
		else if (state.overlays.contains(MenuOverlay::kDirectConnect) && DirectConnect)
			DirectConnect->refocusDefault();
		else if (state.overlays.contains(MenuOverlay::kPassword) && Password)
			Password->refocusDefault();
		else if (state.overlays.contains(MenuOverlay::kConnectionError) && ConnError)
			ConnError->refocusDefault();
		else if (state.overlays.contains(MenuOverlay::kConsole) && Console)
			Console->refocusDefault();
		else if (!state.pages.empty() && Shell) {
			if (state.pages.top() == MenuPage::kMain || state.pages.top() == MenuPage::kPause) {
				Shell->showSection(state.pages.top() == MenuPage::kMain
					? panels::mainmenu::MainMenuPage::Section::kMain
					: panels::mainmenu::MainMenuPage::Section::kPause, true);
			} else if (state.pages.top() == MenuPage::kSettings && Settings) {
				Settings->refocusDefault();
			} else if (state.pages.top() == MenuPage::kLoading && Loading) {
				Loading->refocusDefault();
			} else if (SubStub) {
				SubStub->refocusDefault();
			}
		}
	}

	void onContextReady(Rml::Context* context) override {
		Context = context;
		// One action seam for every window: fixed action names in, controller
		// intents out (never an original-UI command).
		const auto wire = [this, context](auto& page) {
			page->setActionSink([this](const char* action) { onMenuAction(action); });
			logLoadOutcome(page->load(context));
		};
		Shell = std::make_unique<panels::mainmenu::MainMenuPage>();
		SubStub = std::make_unique<panels::menushared::MenuStubPage>("menusub", "abcui/rml/menustub.rml");
		// T16: the replacement console (was the menuconsole stub).
		Console = std::make_unique<panels::console::ConsolePage>();
		Console->setBuffer(&services::console::consoleBuffer());
		Console->setBackend(&services::console::consoleBackend());
		Console->setSubmitSink([](const std::string& command) {
			// The T15 submission path: one validated engine command per call
			// (pfnClientCmd inside the backend; the engine's own chain runs).
			services::console::consoleBackend().submit(command.c_str());
		});
		Settings = std::make_unique<panels::settings::SettingsPage>();
		Settings->setModel(&SettingsModel);
		Settings->setBindModel(&BindModel);
		Settings->setVideoModel(&VideoModel);
		Settings->setVoiceModel(&VoiceModel);
		VideoConfirm = std::make_unique<panels::settings::VideoConfirmPage>();
		VideoConfirm->setVideoModel(&VideoModel);
		ConfirmQuit = std::make_unique<panels::menushared::ConfirmQuitOverlay>();
		// T14 connection surfaces.
		Loading = std::make_unique<panels::loading::LoadingPage>();
		Loading->setModel(&LoadingModelState);
		ConnError = std::make_unique<panels::loading::ConnectionErrorOverlay>();
		Password = std::make_unique<panels::loading::PasswordOverlay>();
		DirectConnect = std::make_unique<panels::loading::DirectConnectOverlay>();
		wire(Shell);
		wire(SubStub);
		wire(Console);
		wire(Settings);
		wire(VideoConfirm);
		wire(ConfirmQuit);
		wire(Loading);
		wire(ConnError);
		wire(Password);
		wire(DirectConnect);
		// T12 startup recovery: a persisted unconfirmed-switch record drives
		// the first presentation (confirmation window on the actual mode, or
		// an automatic write-back of the old values). Runs once per engine
		// session, before any menu may be raised.
		resolveVideoRecovery();
		// The takeover may have raised the menu before the context existed
		// (cold-start activation runs ~1.5s ahead of Rml assembly): the
		// pending show lands now, on the first context. A startup console
		// request (the -console/-toconsole latch) rides the same path: its
		// flag is sampled BEFORE the first presentation, because the phase
		// reconciliation inside presentMenu clears the overlay stack by
		// design (Booting -> Disconnected) and the sync's single-truth tail
		// then lowers the engine-facing answer. Re-requesting through the
		// bridge afterwards restores both truths and presents the overlay
		// (the sink pushes it once); the page's fresh-open projection
		// replays the output cache tail.
		const bool consolePending = services::nativeUiBridge().consoleRequested();
		if (services::nativeUiBridge().menuUp())
			presentMenu();
		if (consolePending)
			services::nativeUiBridge().requestConsoleOpen(services::NativeUiOrigin::kOwnUi);
	}

	void onShutdown() override {
		// Context still alive here: every document closes and drops its
		// listeners before the DOM dies (module contract). The tokens
		// release first - a stale no-op when the router already reset.
		// The mic test is a device resource: stop it before anything tears
		// down (the pairing guarantee covers the shutdown path too).
		VoiceModel.stopTest();
		CaptureToken.release();
		PageToken.release();
		Shell.reset();
		SubStub.reset();
		Console.reset();
		Settings.reset();
		VideoConfirm.reset();
		ConfirmQuit.reset();
		Loading.reset();
		ConnError.reset();
		Password.reset();
		DirectConnect.reset();
		Context = nullptr;
		MenuPresented = false;
		SettingsPresented = false;
		ConsolePresented = false;
		VideoConfirmPresented = false;
		LoadingVisible = false;
		ErrorPresented = false;
		PasswordPresented = false;
		PasswordReprepare = false;
		DirectConnectPresented = false;
		LastStubPage = MenuPage::kMain;
	}

private:
	static void logLoadOutcome(framework::InteractiveDocument::LoadOutcome outcome) {
		using LoadOutcome = framework::InteractiveDocument::LoadOutcome;
		switch (outcome) {
		case LoadOutcome::kLoaded:
			break;
		case LoadOutcome::kEmergencyFallback:
			abc::ConsoleWarn("[AbcUi] menu document degraded to the emergency page\n");
			break;
		case LoadOutcome::kFailed:
			abc::ConsoleWarn("[AbcUi] menu document failed to load entirely (T21 strict path)\n");
			break;
		}
	}

	// T11 capture ownership (kCapture tier sits above the menu page). Every
	// claimed edge is swallowed from the bindings; the InputBridge clamp
	// silences the Rml dispatch so a captured key can neither scroll the
	// list, re-click the button under the cursor, nor type anywhere.
	class KeyCaptureOwner final : public framework::uiinput::Owner {
	public:
		explicit KeyCaptureOwner(GameUiModule* owner)
			: Owner(owner) {}
		bool claimsKey(int down, int keynum) override {
			(void)down;
			(void)keynum;
			return true;
		}
		bool claimsWheel() override { return true; }
		void onClaimedKeyDown(int keynum) override { Owner->onCapturedKeyEdge(keynum); }
		bool onClaimedWheelPulse(int keynum) override {
			Owner->onCapturedKeyEdge(keynum);
			return true; // the pulse is consumed - no page scroll
		}
		void onOwnershipLost() override { Owner->onKeyCaptureForceLost(); }

	private:
		GameUiModule* Owner;
	};

	void startKeyCapture() {
		if (!BindModel.capturing() || CaptureToken.held())
			return;
#ifndef NDEBUG
		abc::ConsolePrint("[AbcUi] capture start row=%d\n", BindModel.captureRow());
#endif
		rmlui::setKeyCaptureClamp(true);
		CaptureToken = rmlui::uiRouter().acquire(&CaptureOwner, framework::uiinput::OwnerTier::kCapture);
		if (!CaptureToken.held()) {
			// Unrepresentable depth (8 owners): fail closed - no session.
			BindModel.cancelCapture();
			rmlui::setKeyCaptureClamp(false);
			abc::ConsoleWarn("[AbcUi] capture token unavailable (router full)\n");
		}
	}

	void endKeyCapture() {
		CaptureToken.release();
		rmlui::setKeyCaptureClamp(false);
	}

	// The one captured edge of the session (router dedups auto-repeat).
	// ESC and unknown keys end the session without a draft change; a
	// conflict parks in the model for the page's confirmation layer.
	void onCapturedKeyEdge(int keynum) {
		const auto result = BindModel.captureKey(keynum);
#ifndef NDEBUG
		abc::ConsolePrint("[AbcUi] capture key=%d result=%d conflict=%d\n", keynum,
			static_cast<int>(result), BindModel.conflictPending() ? 1 : 0);
#endif
		endKeyCapture(); // exactly one edge per session, whatever it was
		if (Settings)
			Settings->render();
	}

	// Force-release (focus loss / teardown): drop the session draft-free.
	void onKeyCaptureForceLost() {
		BindModel.cancelCapture();
		rmlui::setKeyCaptureClamp(false);
	}

	// services::INativeMenuSink: the bridge decided the menu state; run the
	// controller sync and present or take down the document set. Called from
	// inside engine callbacks - no re-entry into the bridge beyond the
	// explicit requestMenuDown below.
	void onNativeMenuShow() override {
		presentMenu();
	}
	void onNativeMenuHide() override {
		dismissMenu();
	}
	// T16 console entries (the bridge already raised the menu when needed):
	// present/retire the console overlay exactly once per state transition
	// (the pre+post engine dispatch pairs collapse inside the bridge, so
	// these fire once per real change - never a double toggle).
	void onNativeConsoleOpen() override {
		if (!Controller.state().overlays.contains(MenuOverlay::kConsole))
			Controller.dispatch(MenuEvent{EventKind::kConsoleToggle});
		if (MenuPresented)
			syncMenuPresentation();
	}
	void onNativeConsoleClose() override {
		if (Controller.state().overlays.contains(MenuOverlay::kConsole))
			Controller.dispatch(MenuEvent{EventKind::kConsoleToggle});
		if (MenuPresented)
			syncMenuPresentation();
	}

	void presentMenu() {
		if (!Shell || !Context || MenuPresented)
			return;
		MenuPresented = true;
		// Real ownership token (T06): the menu claims keys/wheel; mouse
		// reachability rides the router exclusivity like the pointer mode.
		PageToken = rmlui::uiRouter().acquire(this, framework::uiinput::OwnerTier::kMenu);
		if (!PageToken.held())
			abc::ConsoleWarn("[AbcUi] menu ownership token unavailable (router full)\n");
		services::nativeUiBridge().notePageShown();
		// The state machine follows the bridge (idempotent open), then the
		// presentation follows the state machine. The phase reconciles first,
		// so the open lands on the right home (Pause in-game, Main no-map).
		reconcilePhase();
		Controller.dispatch(MenuEvent{EventKind::kMenuShown});
		syncMenuPresentation();
		if (Controller.state().phase == gameui::menu::BackendPhase::kInGame)
			menuBackend().menuShownInGame();  // singleplayer pause parity
		focusLog("presented");
	}

	void dismissMenu() {
		// The bridge lowered the menu from outside the state machine's
		// decision (routed ESC close, engine hide, focus loss): force-clear
		// the controller, hide the documents, release ownership. No unpause
		// here - the simulation only resumes on a decided close
		// (EngineUiBackend::resumeGame), never on a force-release.
		// A running capture session ends with it (draft-free, clamp off).
		endKeyCapture();
		BindModel.cancelCapture();
		// T13b: menu focus loss ends the mic test with it (the device
		// session must never outlive the surface that owns it).
		VoiceModel.stopTest();
		Controller.dispatch(MenuEvent{EventKind::kMenuHidden});
		hideAllMenuDocuments();
		if (MenuPresented) {
			MenuPresented = false;
			PageToken.release();
			services::nativeUiBridge().notePageHidden();
		}
		focusLog("dismissed");
	}

	// Derives the backend phase the controller needs for its ESC decisions
	// from the bridge's level fact (noteLevelLoaded / noteDisconnected - the
	// same "connected" guard the vanilla hide uses). Production has no
	// loading chain yet (T14): the phase lives at kBooting otherwise, so the
	// reconcile covers exactly the kBooting/kInGame/kDisconnected triangle.
	// Ordering contract: presentMenu reconciles BEFORE opening (the open
	// lands on the right home); the owner's ESC reconciles AFTER its toggle
	// - a menu that survived a map load closes on the phase arrival instead
	// of re-opening as Pause from the cleared stack.
	void reconcilePhase() {
		const auto current = Controller.state().phase;
		if (current != gameui::menu::BackendPhase::kBooting
			&& current != gameui::menu::BackendPhase::kInGame
			&& current != gameui::menu::BackendPhase::kDisconnected)
			return;  // connecting/loading/fatal are T14's to reconcile
		const auto target = services::nativeUiBridge().hasLevel()
			? gameui::menu::BackendPhase::kInGame
			: gameui::menu::BackendPhase::kDisconnected;
		if (current == target)
			return;
		Controller.dispatch(gameui::menu::makePhaseChanged(NextPhaseSession++, target));
	}

	// The single render-from-state function: document visibility and the
	// shell section follow the controller; nothing here infers game state
	// from the DOM (T09 step 1).
	void syncMenuPresentation() {
		if (!Shell || !Context)
			return;
		const auto& state = Controller.state();
		if (!Controller.isMenuActive()) {
			hideAllMenuDocuments();
			return;
		}
		// GameMenu.res conditions, verified against the shipped GameUI
		// (CGameMenu::UpdateMenu 0x1000dbe0): "notsingle" rows hide only
		// in-game+singleplayer - the no-map home always shows them, and the
		// in-game half of the condition IS the page split (Main vs Pause).
		// The player list row (OnlyInGame + notsingle) therefore keys on the
		// maxplayers fact alone on the pause page.
		const bool multiplayer = menuBackend().maxClientsFact() > 1;
		Shell->setEntryVisible("playerlist", multiplayer);

		// Page layer: the shell carries the two home sections; the settings
		// page (T10) is the first real sub page; every other page lands on
		// the not-yet-implemented stub (parity TODO visible, original
		// windows never opened - T09 step 2); kLoading (T14) is the live
		// connect attempt's page.
		bool shellVisible = false;
		bool loadingVisible = false;
		if (!state.pages.empty()) {
			const bool overlaysHoldFocus = !state.overlays.empty();
			switch (state.pages.top()) {
			case MenuPage::kMain:
			case MenuPage::kPause: {
				shellVisible = true;
				// Show before the section focus pass: Focus() needs the
				// document visible (first presentation has no frame between).
				// The per-frame focus guard (onFrame) re-takes the section
				// default against focus thieves.
				Shell->show();
				const auto section = state.pages.top() == MenuPage::kMain
					? panels::mainmenu::MainMenuPage::Section::kMain
					: panels::mainmenu::MainMenuPage::Section::kPause;
				Shell->showSection(section, !overlaysHoldFocus);
				break;
			}
			case MenuPage::kSettings:
				presentSettingsPage();
				break;
			case MenuPage::kLoading:
				// Fresh projection on every sync: the attempt's values moved
				// since the last frame (the frame drain re-renders too).
				if (Loading) {
					Loading->render();
					Loading->show();
					loadingVisible = true;
					LoadingVisible = true;
				}
				break;
			default:
				showStubPage(SubStub.get(), state.pages.top(), false);
				break;
			}
		}
		if (shellVisible)
			SubStub->hide();
		else
			Shell->hide();
		if (!loadingVisible && Loading) {
			Loading->hide();
			LoadingVisible = false;
		}
		if (state.pages.empty() || state.pages.top() != MenuPage::kSettings) {
			if (Settings)
				Settings->hide();
			SettingsPresented = false;
			// T13b: the mic test is a page-scoped device session - leaving
			// the settings page stops it (the original's PageHide guarantee,
			// extended to the controller-driven leave path).
			VoiceModel.stopTest();
		}

		// Overlay layer, top-most last (each show pulls to the context
		// front); idempotent shows neither re-focus nor re-order.
		// T16 replacement console: fresh projection on every sync (the
		// page appends only new lines; showLinesForTest analog = render).
		if (state.overlays.contains(MenuOverlay::kConsole) && Console) {
			if (!ConsolePresented) {
				Console->prepare();  // fresh open: clear editor, reseed view
				ConsolePresented = true;
			}
			Console->render();
			Console->show();
		} else if (Console) {
			Console->hide();
			ConsolePresented = false;
		}
		// T14 overlays. Each "presented" latch means "projected at least
		// once for this opening" - the prepare() clears the fields exactly
		// once per open, repeated syncs are idempotent shows.
		if (state.overlays.contains(MenuOverlay::kConnectionError) && ConnError) {
			if (!ErrorPresented || LastErrorReason != state.errorReason) {
				ConnError->showFor(state.errorReason.c_str());
				LastErrorReason = state.errorReason;
				ErrorPresented = true;
			}
			ConnError->show();
		} else if (ConnError) {
			ConnError->hide();
			ErrorPresented = false;
		}
		if (state.overlays.contains(MenuOverlay::kPassword) && Password) {
			if (!PasswordPresented || PasswordReprepare) {
				Password->prepare(PasswordInvalid);
				PasswordPresented = true;
				PasswordReprepare = false;
			}
			Password->show();
		} else if (Password) {
			Password->hide();
			PasswordPresented = false;
		}
		if (state.overlays.contains(MenuOverlay::kDirectConnect) && DirectConnect) {
			if (!DirectConnectPresented) {
				DirectConnect->prepare();
				DirectConnectPresented = true;
			}
			DirectConnect->show();
		} else if (DirectConnect) {
			DirectConnect->hide();
			DirectConnectPresented = false;
		}
		if (state.overlays.contains(MenuOverlay::kConfirmQuit))
			ConfirmQuit->show();
		else
			ConfirmQuit->hide();
		// T12: the confirmation page is the top-most layer of all - a page
		// sync while it is up must not raise another document over it.
		if (VideoConfirmPresented && VideoConfirm)
			VideoConfirm->show();
		// T16 single truth: whenever the projection no longer holds the
		// console (routed ESC popped it, a force-dismiss cleared it), the
		// engine-facing answer must follow. noteConsoleDismissed never
		// notifies the sink, so this cannot re-enter the sync.
		if (!state.overlays.contains(MenuOverlay::kConsole)
			&& services::nativeUiBridge().consoleRequested())
			services::nativeUiBridge().noteConsoleDismissed();
	}

	// The settings page (T10/T11): fresh engine truth on every open (untouched
	// rows follow silently, pending drafts survive and surface), then the
	// page projects both models. Re-opening after a force-dismiss keeps the
	// drafts - the models' reopen rules prompt instead of overwriting.
	void presentSettingsPage() {
		if (!Settings)
			return;
		if (!SettingsPresented) {
			SettingsModel.snapshot();
			BindModel.snapshot();
			VideoModel.snapshot(); // fresh actual mode + enumerated list
			VoiceModel.snapshot(); // fresh device floats + interface presence
			Settings->render();
			SettingsPresented = true;
		}
		Settings->show();
	}

	// --- T12 display-mode transaction ------------------------------------
	// Startup recovery (onContextReady): a persisted unconfirmed-switch
	// record either re-opens the confirmation window on the ACTUAL mode or
	// auto-reverts to the old values (see VideoModeModel). Runs once per
	// context; the record survives restarts, so this is the crash path too.
	void resolveVideoRecovery() {
		const auto outcome = VideoModel.resolveRecovery();
		switch (outcome) {
		case services::settings::VideoModeModel::RecoveryResult::kPending:
			abc::ConsoleWarn("[AbcUi] display change awaiting confirmation (%s)\n",
				services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
			break;
		case services::settings::VideoModeModel::RecoveryResult::kAutoReverted:
			abc::ConsoleWarn("[AbcUi] unresolved display change restored to %s\n",
				services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
			break;
		case services::settings::VideoModeModel::RecoveryResult::kAccepted:
			abc::ConsoleWarn("[AbcUi] stale display-change record accepted as kept\n");
			break;
		case services::settings::VideoModeModel::RecoveryResult::kDeferred:
			abc::ConsoleWarn("[AbcUi] display-change record kept (query interface missing)\n");
			break;
		case services::settings::VideoModeModel::RecoveryResult::kNoRecord:
			break;
		}
		if (VideoModel.awaitingConfirm())
			DeferredVideoConfirmRaise = true; // frame drain raises it (restart-safe)
	}

	// Applies the staged video change (Settings "video-apply" intent): the
	// model persists the record, writes the changed commands and restarts
	// the video in-process; the confirmation page is raised immediately (the
	// documents survive the HUD_Shutdown/HUD_Init cycle - the record is the
	// crash truth, these are only views).
	void applyVideoChange() {
		if (!VideoModel.available() || !VideoModel.hasStaged() || VideoModel.awaitingConfirm())
			return;
		if (!VideoModel.apply()) {
			abc::ConsoleWarn("[AbcUi] display switch refused (record or command write failed)\n");
			if (Settings)
				Settings->render();
			return;
		}
		// The restart is queued in the same transaction (deferred thunk) and
		// the confirmation raise rides the frame drain: never from here.
		DeferredVideoConfirmRaise = true;
		if (Settings)
			Settings->render();
	}

	void raiseVideoConfirmation() {
		if (!VideoConfirm || !Context)
			return;
		// The takeover rule: the confirmation outranks everything else in
		// the menu (it is the only way back from a bad mode). If the menu
		// was not up, raise it through the bridge - the takeover keeps the
		// original UI suppressed.
		if (!MenuPresented)
			services::nativeUiBridge().requestMenuUp(services::NativeUiOrigin::kOwnUi);
		// The page covers the whole viewport: hide the other documents while
		// it is up (the sync re-shows the right one when it closes).
		if (Shell)
			Shell->hide();
		if (SubStub)
			SubStub->hide();
		if (Console)
			Console->hide();
		if (Settings)
			Settings->hide();
		VideoConfirmPresented = true;
		VideoCountdownShown = VideoModel.remainingSeconds();
		VideoConfirm->render();
		VideoConfirm->show();
	}

	void closeVideoConfirmation() {
		if (VideoConfirm)
			VideoConfirm->hide();
		VideoConfirmPresented = false;
		VideoCountdownShown = -1;
		if (MenuPresented)
			syncMenuPresentation();
	}

	void syncVideoConfirmPage() {
		if (!VideoConfirm)
			return;
		if (VideoModel.awaitingConfirm()) {
			// Never raise inside a frame that issued engine commands: queue
			// it for the frame drain (see DeferredVideoConfirmRaise).
			if (!VideoConfirmPresented || !VideoConfirm->isVisible())
				DeferredVideoConfirmRaise = true;
		} else if (VideoConfirmPresented) {
			closeVideoConfirmation();
		}
	}

	// Countdown re-render throttle: once per second (the value only moves on
	// second boundaries; per-frame DOM writes would be churn).
	void tickVideoTimeout() {
		if (!VideoConfirm || !VideoConfirmPresented)
			return;
		const int seconds = VideoModel.remainingSeconds();
		if (seconds == VideoCountdownShown)
			return;
		VideoCountdownShown = seconds;
		VideoConfirm->render();
	}

	// Points a stub window at one unfinished surface; kMain is the console
	// placeholder variant (the real console is T16).
	void showStubPage(panels::menushared::MenuStubPage* stub, MenuPage page, bool console) {
		if (!stub)
			return;
		if (!console && LastStubPage == page && stub->isVisible())
			return;  // same surface already presented: no text churn
		LastStubPage = page;
		using Resolve = framework::Localization;
		std::string title, body;
		const char* titleToken = "#MainMenu_StubTitle";
		const char* bodyToken = "#MainMenu_StubSettings";
		switch (page) {
		case MenuPage::kServers:
			bodyToken = "#MainMenu_StubServers";
			break;
		case MenuPage::kCreateGame:
			bodyToken = "#MainMenu_StubCreateGame";
			break;
		case MenuPage::kPlayerList:
			bodyToken = "#MainMenu_StubPlayerList";
			break;
		case MenuPage::kLoading:
			bodyToken = "#MainMenu_StubLoading";
			break;
		case MenuPage::kMain:
			bodyToken = "#MainMenu_StubConsole";
			break;
		default:
			break;  // settings: the default tokens
		}
		Resolve::translate(title, titleToken + 1);
		Resolve::translate(body, bodyToken + 1);
		stub->showFor(title.c_str(), body.c_str());
		stub->show();
	}

	// Fixed action intents from the menu windows (page-internal constants,
	// never user strings - T04 step 2). After the dispatch the presentation
	// re-syncs; a decided close lowers the bridge through the backend's
	// resume path (sink dismissal), never through a second truth here.
	void onMenuAction(const char* action) {
		const std::string intent = action ? action : "";
		if (intent == "resume")
			Controller.dispatch(MenuEvent{EventKind::kResume});
		else if (intent == "disconnect")
			Controller.dispatch(MenuEvent{EventKind::kDisconnectRequested});
		else if (intent == "quit")
			Controller.dispatch(MenuEvent{EventKind::kQuitRequested});
		else if (intent == "quit-yes")
			Controller.dispatch(gameui::menu::makeQuitResolved(true));
		else if (intent == "quit-no")
			Controller.dispatch(gameui::menu::makeQuitResolved(false));
		else if (intent == "settings")
			Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kSettings));
		else if (intent == "servers")
			Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kServers));
		else if (intent == "creategame")
			Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kCreateGame));
		else if (intent == "playerlist")
			Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kPlayerList));
		else if (intent == "console") {
			// T16: the main-menu console entry toggles the replacement
			// console through the bridge (the same funnel as the engine
			// entries), so the engine-facing IsConsoleVisible answer and the
			// overlay stack never disagree.
			auto& bridge = services::nativeUiBridge();
			if (bridge.consoleRequested())
				bridge.requestConsoleClose(services::NativeUiOrigin::kOwnUi);
			else
				bridge.requestConsoleOpen(services::NativeUiOrigin::kOwnUi);
			finishDispatch();
			return;
		}
		else if (intent == "directconnect")
			Controller.dispatch(MenuEvent{EventKind::kDirectConnectToggle});
		else if (intent == "loading-cancel") {
			// The proven termination action (vanilla Cancel). The model latch
			// is the UI-edge guard (exactly one request per attempt); the
			// controller phase gate and the backend's generation latch are
			// the second and third guards behind it.
			const uint32_t live = LoadingModelState.generation();
			if (live != 0 && !LoadingModelState.requestCancel(live))
				return;  // already latched
			Controller.dispatch(MenuEvent{EventKind::kConnectCancel});
		}
		else if (intent == "error-retry") {
			// A NEW attempt generation from the same address (kept by the
			// model across the failure) - never a reuse of the dead one.
			const std::string address = LoadingModelState.address();
			if (address.empty())
				return;  // nothing to retry: the overlay stays honest
			Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
		}
		else if (intent == "error-return") {
			Controller.dispatch(MenuEvent{EventKind::kBack});
		}
		else if (intent == "directconnect-connect") {
			if (!DirectConnect)
				return;
			const std::string address = DirectConnect->value();
			std::string host;
			int port = 0;
			if (!services::loading::LoadingModel::parseAddress(address.c_str(), host, port)) {
				DirectConnect->showHint("#Loading_AddressInvalid");
				return;  // stay put: nothing reaches the engine unvalidated
			}
			Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
		}
		else if (intent == "directconnect-cancel")
			Controller.dispatch(MenuEvent{EventKind::kBack});
		else if (intent == "password-connect") {
			if (!Password)
				return;
			const std::string password = Password->value();
			if (password.empty())
				return;  // empty submissions are not typed intents
			Controller.dispatch(gameui::menu::makePasswordSubmitted(password.c_str()));
		}
		else if (intent == "password-cancel")
			// Vanilla parity: closing the password dialog does not kill the
			// attempt (its backend Close only destroys the dialog). The
			// loading page stays; its Cancel is the termination path.
			Controller.dispatch(MenuEvent{EventKind::kBack});
		else if (intent == "return" || intent == "close")
			Controller.dispatch(MenuEvent{EventKind::kBack});
		else if (intent == "bind-capture-begin") {
			// T11: the settings page started a capture session in the bind
			// model; the module raises the kCapture ownership tier above the
			// menu page so the next physical edge is captured, not played.
			startKeyCapture();
			return; // no controller transition - the page state already moved
		}
		else if (intent == "video-apply") {
			applyVideoChange();
			return;
		}
		else if (intent == "video-keep") {
			// Keep the confirmed mode; a refused confirmation stays put (the
			// page re-renders the current truth).
			if (VideoModel.confirm()) {
				abc::ConsolePrint("[AbcUi] display change confirmed (%s)\n",
					services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
				syncVideoConfirmPage();
			} else if (VideoConfirm) {
				VideoConfirm->render();
			}
			return;
		}
		else if (intent == "video-revert") {
			// Restore the previous mode; a refused write keeps the dialog up
			// (the deadline retries every second, the record survives).
			if (VideoModel.revert()) {
				abc::ConsolePrint("[AbcUi] display change reverted to %s\n",
					services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
				syncVideoConfirmPage();
			} else if (VideoConfirm) {
				VideoConfirm->render();
			}
			return;
		}
		else {
			abc::ConsoleWarn("[AbcUi] unknown menu action \"%s\" dropped\n", intent.c_str());
			return;
		}
		finishDispatch();
	}

	// Post-dispatch convergence shared by the page intents and the loading
	// drain: reconcile the attempt bookkeeping, re-render, and lower the
	// bridge when the state machine no longer holds the menu.
	void finishDispatch() {
		syncAttemptState();
		if (MenuPresented)
			syncMenuPresentation();
		if (!Controller.isMenuActive() && services::nativeUiBridge().menuUp())
			services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
	}

	// --- T14 loading stream -------------------------------------------------

	// The attempt bookkeeping between the controller (generation truth) and
	// the loading model (presentation values). Idempotent; runs after every
	// dispatch and inside the frame drain.
	void syncAttemptState() {
		const auto& state = Controller.state();
		if (state.phase == gameui::menu::BackendPhase::kConnecting && state.connectGeneration != 0
			&& LoadingModelState.generation() != state.connectGeneration) {
			LoadingModelState.startAttempt(state.connectGeneration, state.connectAddress.c_str());
			PasswordInvalid = false;
			PasswordPrompts = 0;
			PasswordReprepare = false;
		}
		if (LoadingModelState.generation() != 0 && LoadingModelState.generation() != state.connectGeneration) {
			// The attempt ended (cancel/success/failure): the stream of the
			// retired generation keeps arriving and is dropped by the model's
			// generation filter from here on.
			LoadingModelState.retire();
			PasswordInvalid = false;
			PasswordPrompts = 0;
			PasswordReprepare = false;
		}
	}

	// Drains the copied engine loading events on the main thread (the entry
	// layer pushed borrowed-pointer copies). Model values update always; the
	// controller transitions ride the same generation-tagged kinds the
	// offline table pins. A late event of a retired generation is dropped by
	// the model filter AND by the controller's generation check.
	void drainLoadingFeed() {
		services::LoadingFeedEvent event;
		bool changed = false;
		bool rendered = false;
		while (services::loadingFeed().pop(event)) {
			changed = true;
			syncAttemptState();
			const uint32_t live = LoadingModelState.generation();
			switch (event.kind) {
			case services::LoadingFeedKind::kLoadingStarted:
				LoadingModelState.loadingStarted(live, event.text.c_str(), event.text2.c_str());
				break;
			case services::LoadingFeedKind::kLoadingFinished:
				LoadingModelState.loadingFinished(live, event.text.c_str());
				break;
			case services::LoadingFeedKind::kStartProgress:
				LoadingModelState.startProgress(live, event.text.c_str(), event.steps);
				break;
			case services::LoadingFeedKind::kContinueProgress:
				LoadingModelState.continueProgress(live, event.point, event.fraction);
				break;
			case services::LoadingFeedKind::kStopProgress:
				LoadingModelState.stopProgress(live, event.error);
				break;
			case services::LoadingFeedKind::kStatusText:
				LoadingModelState.setStatusText(live, event.text.c_str());
				break;
			case services::LoadingFeedKind::kSecondaryProgress:
				LoadingModelState.setSecondaryProgress(live, event.fraction);
				break;
			case services::LoadingFeedKind::kSecondaryText:
				LoadingModelState.setSecondaryProgressText(live, event.text.c_str());
				break;
			case services::LoadingFeedKind::kPasswordPrompt:
				// The engine asked for a server password (IGameUI +0x58).
				// The first prompt of a generation = "required"; any later
				// one = the previous password was rejected (the engine's own
				// flag polarity is not pinned - the per-generation count is
				// the authoritative signal for the lead line).
				if (live != 0) {
					PasswordInvalid = PasswordPrompts > 0;
					++PasswordPrompts;
					PasswordReprepare = true;  // re-prepare clears the field
				}
				break;
			case services::LoadingFeedKind::kCount:
				break;
			}
			// The classification is the single decision table (pinned by
			// abc_ui_loading_model): value-only events never transition.
			switch (services::classifyLoadingEvent(event)) {
			case services::LoadingStep::kNone:
				break;
			case services::LoadingStep::kStarted:
				if (live != 0 && Controller.state().phase == gameui::menu::BackendPhase::kConnecting)
					Controller.dispatch(gameui::menu::makeLoadingStarted(live));
				break;
			case services::LoadingStep::kJoined:
				if (live != 0)
					Controller.dispatch(gameui::menu::makeLoadingFinished(live,
						gameui::menu::LoadingOutcome::kSuccess, nullptr));
				break;
			case services::LoadingStep::kFailed: {
				if (live == 0)
					break;
				const std::string& reason = event.text.empty() ? event.text2 : event.text;
				Controller.dispatch(gameui::menu::makeLoadingFinished(live,
					gameui::menu::LoadingOutcome::kFailed, reason.c_str()));
				break;
			}
			case services::LoadingStep::kPasswordRequired:
				if (live != 0)
					Controller.dispatch(gameui::menu::makeLoadingFinished(live,
						gameui::menu::LoadingOutcome::kPasswordRequired, nullptr));
				break;
			}
			// The visible loading page re-projects once per drained batch
			// (values moved); never inside the pop loop's per-event churn.
			if (!rendered && MenuPresented && LoadingVisible && Loading) {
				Loading->render();
				rendered = true;
			}
		}
		if (!changed)
			return;
		syncAttemptState();
		if (MenuPresented)
			syncMenuPresentation();
		// A drained success/failure moved the controller; a no-longer-active
		// menu lowers the bridge exactly like a decided close (T14: the
		// loading page disappears on success through the same path).
		if (!Controller.isMenuActive() && services::nativeUiBridge().menuUp())
			services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
	}

	void hideAllMenuDocuments() {
		if (Shell)
			Shell->hide();
		if (SubStub)
			SubStub->hide();
		if (Console) {
			Console->hide();
			ConsolePresented = false;
		}
		if (Settings)
			Settings->hide();
		if (ConfirmQuit)
			ConfirmQuit->hide();
		// T14 connection surfaces: hidden with the menu; a later present
		// re-projects from the state (the latches reset here).
		if (Loading)
			Loading->hide();
		if (ConnError)
			ConnError->hide();
		if (Password)
			Password->hide();
		if (DirectConnect)
			DirectConnect->hide();
		ErrorPresented = false;
		PasswordPresented = false;
		PasswordReprepare = false;
		DirectConnectPresented = false;
		LoadingVisible = false;
		// The video confirmation is NOT hidden by a generic hide: it must
		// stay up until confirmed/reverted (a dismissal cannot silently
		// abandon a mode the user has not confirmed). dismissMenu() calls
		// this, so keep it visibly module-owned.
		SettingsPresented = false;
	}

	void focusLog(const char* phase) {
#ifndef NDEBUG
		if (Context) {
			if (Rml::Element* focus = Context->GetFocusElement())
				abc::ConsolePrint("[AbcUi] menu %s, focus on '%s'\n", phase, focus->GetId().c_str());
			else
				abc::ConsoleWarn("[AbcUi] menu %s, no focus element\n", phase);
		}
#else
		(void)phase;
#endif
	}

	// framework::uiinput::Owner (menu tier, T06/T07 semantics preserved).
	bool claimsKey(int down, int keynum) override {
		(void)down;
		(void)keynum;
		return true;
	}
	bool claimsWheel() override { return true; }
	void onClaimedKeyDown(int keynum) override {
		// Exactly once per physical press (the router suppresses auto-repeat
		// here); the release pairs the just-fed ESC down inside the router.
		if (keynum == '`' || keynum == '~') {
			// T16 console hotkey (the ` / toggleconsole key): while the menu
			// owns the keyboard the engine never sees this edge, so the
			// overlay toggle lives here. The key's own committed character
			// ("`"/"~" through the T07 text pipeline) is swallowed - the
			// toggle key never types (vanilla parity).
			if (!MenuPresented)
				return;
			rmlui::swallowToggleKeyText();
			onMenuAction("console");
			return;
		}
		if (keynum != K_ESCAPE || !MenuPresented)
			return;
		// T16: an ESC press the console page's own paste-confirm pane
		// consumed (the Rml feed runs before this action) closes only that
		// layer - "同一事件不关闭两层".
		if (Console && Console->consumeEscapeClaim())
			return;
		// T07.b ESC priority: the first press cancels a running composition
		// instead of navigating; the next press toggles. The IME normally
		// swallows ESC itself, so this is the leak path.
		if (rmlui::textComposing()) {
			rmlui::cancelComposition();
			return;
		}
		// T12: an outstanding display confirmation is modal - ESC is consumed
		// (the dialog resolves by button or by its own deadline; a dismissal
		// cannot silently abandon an unconfirmed mode).
		if (VideoModel.awaitingConfirm())
			return;
		// The state machine owns the ESC semantics: pop one overlay, return
		// from a sub page, or close+resume on the pause home (T04 table).
		// The phase reconciles AFTER the toggle: a menu that survived a map
		// load (stale no-map phase) closes on the phase arrival instead of
		// re-opening as Pause from the cleared stack.
		// T10 exception: ESC on a dirty settings page is consumed by the
		// unsaved-changes prompt (second press dismisses it = stay) - the
		// controller's plain pop would silently discard the drafts.
		if (Controller.state().overlays.empty()
			&& Controller.state().pages.topIs(MenuPage::kSettings) && Settings
			&& Settings->isDirty()) {
			Settings->requestLeave();
			syncMenuPresentation();
			return;
		}
		Controller.dispatch(MenuEvent{EventKind::kMenuToggle});
		reconcilePhase();
		syncMenuPresentation();
		if (!Controller.isMenuActive())
			services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
	}
	void onOwnershipLost() override {
		// Force-release (focus loss / teardown): the bridge state must follow
		// so a later ESC cannot route into an empty router. The dismissal
		// clears the controller and the documents in one place.
		services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
	}

#ifndef NDEBUG
	// T11 diagnostics: the capture/clamp/router state and every bind row's
	// draft - the live-debugging counterpart of the offline model dumps.
	// Debug-only voice diagnostics (T13b): device floats vs drafts + the
	// test session state. One line, no DOM reads - the model is the truth.
	static void voiceStatusCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		abc::ConsolePrint("[AbcUi] voice available=%d testing=%d mic=%.3f micDraft=%.3f micDirty=%d boost=%d boostDraft=%d boostDirty=%d level=%.3f\n",
			self.VoiceModel.available() ? 1 : 0, self.VoiceModel.testing() ? 1 : 0,
			self.VoiceModel.micVolume(), self.VoiceModel.micVolumeDraft(),
			self.VoiceModel.micDirty() ? 1 : 0, self.VoiceModel.micBoost() ? 1 : 0,
			self.VoiceModel.micBoostDraft() ? 1 : 0, self.VoiceModel.micBoostDirty() ? 1 : 0,
			self.VoiceModel.speakingLevel());
	}

	// Debug-only: flip the boost draft from the console (the mouse path probe
	// when injected clicks are unreliable).
	static void voiceBoostCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (!self.VoiceModel.available()) {
			abc::ConsoleWarn("[AbcUi] voice unavailable\n");
			return;
		}
		const bool current = self.VoiceModel.micBoostDirty() ? self.VoiceModel.micBoostDraft()
															 : self.VoiceModel.micBoost();
		self.VoiceModel.setMicBoostDraft(!current);
		if (self.Settings)
			self.Settings->renderVoiceSection();
		voiceStatusCommand();
	}

	// Debug-only: drive the microphone test start/stop through the REAL
	// engine interface (StartVoiceTweakMode) - the device evidence probe.
	static void voiceTestCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (self.VoiceModel.testing()) {
			self.VoiceModel.stopTest();
			abc::ConsolePrint("[AbcUi] voice test STOPPED\n");
		} else if (self.VoiceModel.startTest()) {
			abc::ConsolePrint("[AbcUi] voice test STARTED (engine accepted the tweak mode)\n");
		} else {
			abc::ConsoleWarn("[AbcUi] voice test REFUSED (no device / engine returned 0)\n");
		}
		if (self.Settings)
			self.Settings->renderVoiceSection();
		voiceStatusCommand();
	}

	// Debug-only: apply the pending voice drafts exactly like the page's
	// Apply button does (per-control write + read-back).
	static void voiceApplyCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (!self.VoiceModel.available()) {
			abc::ConsoleWarn("[AbcUi] voice unavailable\n");
			return;
		}
		const bool micOk = self.VoiceModel.applyMicVolume();
		const bool boostOk = self.VoiceModel.applyMicBoost();
		abc::ConsolePrint("[AbcUi] voice apply mic=%d boost=%d\n", micOk ? 1 : 0, boostOk ? 1 : 0);
		if (self.Settings)
			self.Settings->renderVoiceSection();
		voiceStatusCommand();
	}

	// Debug-only: run the settings model's apply() exactly like the page's
	// Apply button (write + read-back per row) and print per-row outcomes.
	static void settingsApplyCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		const auto results = self.SettingsModel.apply();
		abc::ConsolePrint("[AbcUi] settings apply rows=%d\n", static_cast<int>(results.size()));
		for (const auto& result : results)
			abc::ConsolePrint("[AbcUi] apply '%s' outcome=%d\n", result.cvar.c_str(),
				static_cast<int>(result.outcome));
		if (self.Settings)
			self.Settings->render();
		settingsStatusCommand();
	}

	// Debug-only dump of the T13 description-table state: one line per row
	// (cvar, kind, state, draft/engine text) plus the voice interface facts.
	static void settingsStatusCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		abc::ConsolePrint("[AbcUi] settings rows=%d voice=%d testing=%d mic=%.2f boost=%d\n",
			static_cast<int>(self.SettingsModel.rows().size()), self.VoiceModel.available() ? 1 : 0,
			self.VoiceModel.testing() ? 1 : 0, self.VoiceModel.micVolume(),
			self.VoiceModel.micBoost() ? 1 : 0);
		for (const auto& view : self.SettingsModel.rows()) {
			if (view.state == services::settings::RowState::kMissing)
				continue; // registered rows only, keeps the dump readable
			abc::ConsolePrint("[AbcUi] row '%s' kind=%d state=%d draft='%s' engine='%s'\n",
				view.row->Cvar, static_cast<int>(view.row->Kind), static_cast<int>(view.state),
				view.draftText.c_str(), view.engineText.empty() ? "" : view.engineText.c_str());
		}
	}

	static void bindStatusCommand() {		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		unsigned wheelPulses = 0, wheelDelivered = 0;
		rmlui::wheelDiagCounts(wheelPulses, wheelDelivered);
		float panelScroll = -1.0f, panelScrollHeight = -1.0f;
		std::string hoverId = "(none)";
		if (self.Context) {
			if (Rml::Element* hover = self.Context->GetHoverElement())
				hoverId = hover->GetId().empty() ? hover->GetTagName() : hover->GetId();
			if (self.Settings && self.Settings->document()) {
				if (Rml::Element* panel = self.Settings->document()->GetElementById("panel")) {
					panelScroll = panel->GetScrollTop();
					panelScrollHeight = panel->GetScrollHeight();
				}
			}
		}
		abc::ConsolePrint("[AbcUi] binds: reads=%d capturing=%d row=%d conflict=%d token=%d clamp=%d\n",
			self.BindModel.readsAvailable() ? 1 : 0, self.BindModel.capturing() ? 1 : 0,
			self.BindModel.captureRow(), self.BindModel.conflictPending() ? 1 : 0,
			self.CaptureToken.held() ? 1 : 0, rmlui::keyCaptureClampActive() ? 1 : 0);
		abc::ConsolePrint("[AbcUi] wheel: pulses=%u delivered=%u panelScroll=%.1f scrollHeight=%.1f hover=%s\n",
			wheelPulses, wheelDelivered, panelScroll, panelScrollHeight, hoverId.c_str());
		for (const auto& view : self.BindModel.rows()) {
			if (view.header)
				continue;
			if (view.state == services::settings::BindRowState::kClean
				&& view.draftKeyNames.empty())
				continue; // only active rows, keeps the dump readable
			std::string keys;
			for (const auto& key : view.draftKeyNames)
				keys += key + " ";
			abc::ConsolePrint("[AbcUi] bind %d '%s' state=%d keys=[%s]\n", view.actionIndex,
				view.action ? view.action->Command.c_str() : "?", static_cast<int>(view.state),
				keys.c_str());
		}
	}

	// T12 diagnostics: the video seams step-by-step (query reachability,
	// actual mode, enumerated count) without opening any page - bisects the
	// query path from the page path during live debugging.
	static void videoStatusCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		abc::ConsolePrint("[AbcUi] video: available=%d\n", self.VideoModel.available() ? 1 : 0);
		services::settings::VideoMode actual;
		const bool haveActual = vgui2ext::currentVideoMode(actual.Width, actual.Height, actual.Bpp,
			actual.Windowed);
		abc::ConsolePrint("[AbcUi] video: current=%d %s\n", haveActual ? 1 : 0,
			haveActual ? services::settings::VideoModeModel::describe(actual).c_str() : "(unreadable)");
		vgui2ext::video::Mode modes[64];
		const int count = vgui2ext::videoModeList(modes, 64);
		abc::ConsolePrint("[AbcUi] video: modes=%d first=%dx%d\n", count,
			count > 0 ? modes[0].Width : 0, count > 0 ? modes[0].Height : 0);
		self.VideoModel.snapshot();
		abc::ConsolePrint("[AbcUi] video: snapshot actualKnown=%d staged=%d awaiting=%d\n",
			self.VideoModel.actualKnown() ? 1 : 0, self.VideoModel.hasStaged() ? 1 : 0,
			self.VideoModel.awaitingConfirm() ? 1 : 0);
	}

	// T12 diagnostic: scroll the settings panel (the scripted wheel injection
	// is unreliable - T11 record; this only moves the scroll container for
	// screenshot/verification purposes, exactly what the panelScroll field of
	// abc_ui_bind_status reports).
	static void videoScrollCommand() {
		if (!Instance || !Instance->Settings || !Instance->Settings->document()) {
			abc::ConsoleWarn("[AbcUi] settings page not open\n");
			return;
		}
		Rml::Element* panel = Instance->Settings->document()->GetElementById("panel");
		if (!panel) {
			abc::ConsoleWarn("[AbcUi] settings panel missing\n");
			return;
		}
		panel->SetScrollTop(panel->GetScrollHeight());
		abc::ConsolePrint("[AbcUi] video: panel scrollTop=%.1f height=%.1f\n", panel->GetScrollTop(),
			panel->GetScrollHeight());
	}

	// T12 diagnostics: drive the SAME module paths the page intents call
	// (stage is model-direct in the page; apply/keep/revert are the module
	// handlers) - scripted UI clicking is unreliable for the select/confirm
	// buttons, the state machine under test is identical.
	static void videoStageCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		self.VideoModel.snapshot();
		// Args: width height (engine argv through the client engine funcs).
		if (gEngfuncs.Cmd_Argc() < 3) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_video_stage <width> <height>\n");
			return;
		}
		const int w = atoi(gEngfuncs.Cmd_Argv(1));
		const int h = atoi(gEngfuncs.Cmd_Argv(2));
		if (w <= 0 || h <= 0) {
			abc::ConsoleWarn("[AbcUi] bad dimensions\n");
			return;
		}
		if (!self.VideoModel.stageResolution(w, h)) {
			abc::ConsoleWarn("[AbcUi] stage refused (not an enumerated mode)\n");
			return;
		}
		abc::ConsolePrint("[AbcUi] video staged: %s\n",
			services::settings::VideoModeModel::describe(self.VideoModel.stagedTarget()).c_str());
		if (self.Settings && self.SettingsPresented)
			self.Settings->render();
	}

	static void videoApplyCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		Instance->applyVideoChange();
		abc::ConsolePrint("[AbcUi] video apply issued (awaiting=%d)\n",
			Instance->VideoModel.awaitingConfirm() ? 1 : 0);
	}

	static void videoKeepCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (self.VideoModel.confirm()) {
			abc::ConsolePrint("[AbcUi] video kept: %s\n",
				services::settings::VideoModeModel::describe(self.VideoModel.actual()).c_str());
			self.syncVideoConfirmPage();
		} else {
			abc::ConsoleWarn("[AbcUi] keep refused (kind=%d)\n",
				static_cast<int>(self.VideoModel.confirmKind()));
			self.syncVideoConfirmPage();
		}
	}

	// T12 diagnostic: override the in-session confirmation window (seconds).
	// Debug-only transport accommodation - the CLI's RCON channel rebuild
	// after `_restart` can outlast the production 15s window, so keep/reject
	// paths cannot be commanded before the auto-revert fires. 0 restores the
	// production constant.
	static void videoWindowCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		int seconds = 0;
		if (gEngfuncs.Cmd_Argc() >= 2)
			seconds = atoi(gEngfuncs.Cmd_Argv(1));
		Instance->VideoModel.setConfirmWindowMsForTesting(seconds > 0 ? seconds * 1000 : 0);
		abc::ConsolePrint("[AbcUi] video confirm window: %ds\n", seconds > 0 ? seconds : 15);
	}

	static void videoRevertCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (self.VideoModel.revert())
			abc::ConsolePrint("[AbcUi] video reverted\n");
		else
			abc::ConsoleWarn("[AbcUi] revert refused/failed\n");
		self.syncVideoConfirmPage();
	}

	// T14 diagnostics: drive the SAME module paths the page intents drive
	// (scripted text entry into the Rml fields is unreliable with the
	// injection tooling - T11/T13 record); the state machine under test is
	// identical. Address validation, generation handling, cancel latching
	// and the password chain are the production code paths.
	static void loadingConnectCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (gEngfuncs.Cmd_Argc() < 2) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_loading_connect <host[:port]>\n");
			return;
		}
		const std::string address = gEngfuncs.Cmd_Argv(1);
		std::string host;
		int port = 0;
		if (!services::loading::LoadingModel::parseAddress(address.c_str(), host, port)) {
			abc::ConsoleWarn("[AbcUi] loading connect refused (invalid address \"%s\")\n", address.c_str());
			return;
		}
		self.Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
		self.finishDispatch();
		abc::ConsolePrint("[AbcUi] loading connect issued: \"%s\" gen=%u phase=%d\n", address.c_str(),
			self.LoadingModelState.generation(), static_cast<int>(self.Controller.state().phase));
	}

	static void loadingCancelCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		const uint32_t live = self.LoadingModelState.generation();
		if (live != 0 && !self.LoadingModelState.requestCancel(live)) {
			abc::ConsoleWarn("[AbcUi] loading cancel already latched (gen=%u)\n", live);
			return;
		}
		self.Controller.dispatch(MenuEvent{EventKind::kConnectCancel});
		self.finishDispatch();
		abc::ConsolePrint("[AbcUi] loading cancel issued (previous gen=%u, phase=%d)\n", live,
			static_cast<int>(self.Controller.state().phase));
	}

	static void loadingPasswordCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (gEngfuncs.Cmd_Argc() < 2) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_loading_password <password>\n");
			return;
		}
		if (!self.Controller.state().overlays.contains(MenuOverlay::kPassword)) {
			abc::ConsoleWarn("[AbcUi] no password overlay is up\n");
			return;
		}
		self.Controller.dispatch(gameui::menu::makePasswordSubmitted(gEngfuncs.Cmd_Argv(1)));
		self.finishDispatch();
		abc::ConsolePrint("[AbcUi] loading password submitted (gen=%u)\n",
			self.Controller.state().connectGeneration);
	}

	static void loadingStatusCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		const auto& state = self.Controller.state();
		abc::ConsolePrint("[AbcUi] loading: phase=%d gen=%u modelGen=%u canceled=%d addr=\"%s\"\n",
			static_cast<int>(state.phase), state.connectGeneration, self.LoadingModelState.generation(),
			self.LoadingModelState.canceled() ? 1 : 0, self.LoadingModelState.address().c_str());
		abc::ConsolePrint("[AbcUi] loading: pages=%d overlays=%d menuPresented=%d loadingVisible=%d\n",
			static_cast<int>(state.pages.size()), static_cast<int>(state.overlays.size()),
			self.MenuPresented ? 1 : 0, self.LoadingVisible ? 1 : 0);
		{
			std::string stack = "pages=[";
			for (const MenuPage page : state.pages)
				stack += std::to_string(static_cast<int>(page)) + " ";
			stack += "] overlays=[";
			for (const MenuOverlay overlay : state.overlays)
				stack += std::to_string(static_cast<int>(overlay)) + " ";
			stack += "]";
			abc::ConsolePrint("[AbcUi] loading: %s\n", stack.c_str());
		}
		abc::ConsolePrint("[AbcUi] loading: primary known=%d point=%d steps=%d frac=%.3f status=\"%s\"\n",
			self.LoadingModelState.primary().Known ? 1 : 0, self.LoadingModelState.primary().Point,
			self.LoadingModelState.primary().Steps, self.LoadingModelState.primary().Fraction,
			self.LoadingModelState.statusText().c_str());
		abc::ConsolePrint("[AbcUi] loading: secondary frac=%.3f text=\"%s\" resource=\"%s\" errorReason=\"%s\"\n",
			self.LoadingModelState.secondaryFraction(), self.LoadingModelState.secondaryText().c_str(),
			self.LoadingModelState.resourceText().c_str(), state.errorReason.c_str());
		abc::ConsolePrint("[AbcUi] loading: hasLevel=%d feedDropped=%u passwordInvalid=%d prompts=%d\n",
			services::nativeUiBridge().hasLevel() ? 1 : 0, services::loadingFeed().dropped(),
			self.PasswordInvalid ? 1 : 0, self.PasswordPrompts);
		abc::ConsolePrint("[AbcUi] loading: hookSlot22=%d\n", vgui2ext::gameUiLoadingHookInstalled() ? 1 : 0);
	}

	// T16 diagnostics: the console entry state (engine-facing answer, the
	// module's presented latch, the DOM row count) - the visible half of the
	// console backend counters (T15's abc_ui_console_status covers the cache).
	static void consoleUiStatusCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		auto& bridge = services::nativeUiBridge();
		int rows = -1;
		int scrollTop = -1;
		int scrollHeight = -1;
		if (self.Console && self.Console->document()) {
			if (Rml::Element* history = self.Console->document()->GetElementById("console-history")) {
				rows = history->GetNumChildren();
				scrollTop = static_cast<int>(history->GetScrollTop());
				scrollHeight = static_cast<int>(history->GetScrollHeight());
			}
		}
		abc::ConsolePrint(
			"[AbcUi] console-ui: requested=%d visible=%d page=%d menu=%d rows=%d scrollTop=%d scrollHeight=%d\n",
			bridge.consoleRequested() ? 1 : 0, bridge.consoleVisible() ? 1 : 0,
			self.ConsolePresented ? 1 : 0, self.MenuPresented ? 1 : 0, rows, scrollTop, scrollHeight);
	}

	// T16 diagnostics: present the console through the SAME module path the
	// engine entries drive (the bridge funnel) - the scripted-click stand-in
	// for the page interactions.
	static void consoleUiToggleCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		Instance->onMenuAction("console");
		consoleUiStatusCommand();
	}

	static void menuPageCommand() {		if (!Instance || !Instance->Shell || !Instance->Context) {
			abc::ConsoleWarn("[AbcUi] menu not available\n");
			return;
		}
		auto& bridge = services::nativeUiBridge();
		if (Instance->MenuPresented) {
			bridge.requestMenuDown(services::NativeUiOrigin::kOwnUi);
			abc::ConsolePrint("[AbcUi] menu hidden (bridge menuUp=%d)\n", bridge.menuUp() ? 1 : 0);
			return;
		}
		// Diagnostic raise: the same sink path the takeover drives (this is
		// the T05/T06 dev seam, not a second truth).
		bridge.requestMenuUp(services::NativeUiOrigin::kOwnUi);
		abc::ConsolePrint("[AbcUi] menu shown (bridge menuUp=%d)\n", bridge.menuUp() ? 1 : 0);
	}
#endif

	static GameUiModule* Instance;

	Rml::Context* Context = nullptr;
	gameui::menu::MenuController Controller;
	// T10/T11 settings domain: the description-table models (engine truth
	// via the cvar/bind seams) and their page projections. The models
	// outlive the documents - drafts survive a force-dismiss and re-surface
	// on the next open.
	services::settings::SettingsModel SettingsModel;
	services::settings::KeyBindModel BindModel;
	// T13: the description-table storage (static rows + the parsed user.scr
	// arena) and the IVoiceTweak hardware model. Both outlive the documents;
	// SettingsTable must not be mutated after onInit (the model keeps row
	// pointers into it).
	std::vector<services::settings::SettingRow> SettingsTable;
	AdvancedTable Advanced;
	services::settings::VoiceModel VoiceModel;
	// T12 display-mode transaction: engine reads/writes + the persisted
	// unconfirmed-switch record. Lives across HUD cycles like the other
	// models (the record file is the crash truth; this is the cache).
	services::settings::VideoModeModel VideoModel;
	// T14 connection domain: the presentation values of the live attempt
	// (engine-stream copies) and the four surfaces. The model outlives the
	// documents like the other models.
	services::loading::LoadingModel LoadingModelState;
	std::unique_ptr<panels::mainmenu::MainMenuPage> Shell;
	std::unique_ptr<panels::menushared::MenuStubPage> SubStub;
	std::unique_ptr<panels::console::ConsolePage> Console;
	std::unique_ptr<panels::settings::SettingsPage> Settings;
	std::unique_ptr<panels::settings::VideoConfirmPage> VideoConfirm;
	std::unique_ptr<panels::menushared::ConfirmQuitOverlay> ConfirmQuit;
	std::unique_ptr<panels::loading::LoadingPage> Loading;
	std::unique_ptr<panels::loading::ConnectionErrorOverlay> ConnError;
	std::unique_ptr<panels::loading::PasswordOverlay> Password;
	std::unique_ptr<panels::loading::DirectConnectOverlay> DirectConnect;
	// Router ownership while the menu is presented (T06); the document set
	// mirrors it (dismissal / onOwnershipLost keep them in step).
	framework::uiinput::Token PageToken;
	// T11 key-capture session: the kCapture tier owner above the menu page,
	// held exactly while the bind model waits for one physical edge.
	KeyCaptureOwner CaptureOwner;
	framework::uiinput::Token CaptureToken;
	// Whether THIS module presented the menu (vs. the bridge being up before
	// Rml was ready). Mirrors the bridge's PageShown.
	bool MenuPresented = false;
	// Whether the settings page is the presented surface (snapshot-on-open
	// runs once per open, not on every sync).
	bool SettingsPresented = false;
	// T16: whether the console page is the presented surface (fresh-open
	// projection runs once per open; repeated syncs only append).
	bool ConsolePresented = false;
	// T12: whether the confirmation page is up (module-owned truth; the DOM
	// visibility follows it).
	bool VideoConfirmPresented = false;
	// T14: per-opening projection latches (the prepare() clears input fields
	// exactly once per open; repeated syncs are idempotent shows).
	bool LoadingVisible = false;
	bool ErrorPresented = false;
	bool PasswordPresented = false;
	bool DirectConnectPresented = false;
	// The engine asked for a password more than once for this generation
	// (the previous attempt was rejected): drives the "invalid" lead line.
	bool PasswordInvalid = false;
	// Password prompts seen for the live generation (count-based invalid
	// logic; reset with the attempt).
	int PasswordPrompts = 0;
	// Set by a second prompt so the sync re-prepares (clears) the field.
	bool PasswordReprepare = false;
	// Last error reason projected into the connection-error overlay (a
	// changed reason re-projects, e.g. a second failure with a new text).
	std::string LastErrorReason;
	// Last countdown seconds value projected into the confirm page (the
	// per-frame tick re-renders only when the shown second moves).
	int VideoCountdownShown = -1;
	// Monotonic phase-event sessions for reconcilePhase (the controller
	// drops anything below its accepted session).
	uint32_t NextPhaseSession = 1;
	// Last surface the sub stub rendered (text churn guard only).
	MenuPage LastStubPage = MenuPage::kMain;
};

GameUiModule* GameUiModule::Instance = nullptr;

// The static object of this TU is the module's owner and destructor; the
// registry only borrows the pointer (coding rule 11).
const GameUiModule RegisterModule;

}
}
