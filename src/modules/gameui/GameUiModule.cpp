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
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Debugger.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Paths.h>
#include <abc/Plugin.h>

#include <keydefs.h>
#include <ivoicetweak.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../framework/Localization.h"
#include "../../panels/console/ConsolePage.h"
#include "../../panels/creategame/CreateGamePage.h"
#include "../../panels/loading/LoadingWindows.h"
#include "../../panels/mainmenu/MainMenuPage.h"
#include "../../panels/menushared/MenuShellWindows.h"
#include "../../panels/serverbrowser/ServerBrowserPage.h"
#include "../../panels/settings/SettingsPage.h"
#include "../../panels/settings/VideoConfirmPage.h"
#include "../../rmlui/InputBridge.h"
#include "../../rmlui/TextInput.h"
#include "../../rmlui/VGUI2ExtensionImport.h"
#include "../../services/EngineUiBackend.h"
#include "../../services/GameUiBridge.h"
#include "../../services/console/ConsoleBackend.h"
#include "../../services/console/ConsoleBuffer.h"
#include "../../services/creategame/CreateGameModel.h"
#include "../../services/loading/LoadingModel.h"
#include "../../services/serverbrowser/ServerFavorites.h"
#include "../../services/serverbrowser/ServerListModel.h"
#include "../../services/serverbrowser/ServerQuery.h"
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

// T19: the engine seams of the create-game launcher. The cvar reads/writes go
// through the same typed API the settings model uses (never a concatenated
// command), the constant actions through pfnClientCmd (the module's standard
// command seam).
services::creategame::CreateGameModel::Seams createGameSeams() {
	services::creategame::CreateGameModel::Seams seams;
	seams.readCvar = [](const char* name) -> const char* {
		const cvar_t* cvar = abc::CVAR_GET_POINTER(name);
		return cvar ? cvar->string : nullptr;
	};
	seams.writeCvarString = [](const char* name, const char* value) {
		if (!abc::CVAR_GET_POINTER(name) || !value)
			return false;
		abc::CVAR_SET_STRING(name, value);
		return true;
	};
	seams.clientCmd = [](const char* command) {
		if (!command)
			return;
		abc::EngineClientCmd(command);
	};
	return seams;
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
		// T19: the create-game launcher (typed cvar writes + the shipped
		// command sequence) and its backend relay. The thunk reaches the
		// module's request value object through the Instance pointer, the
		// same shape the Debug diagnostics use.
		CreateGame.setSeams(createGameSeams());
		menuBackend().setLocalGameLauncher(&GameUiModule::launchLocalGame);
		modules::registry().add(this);
		services::nativeUiBridge().setMenuSink(this);
		Instance = this;
	}

	const char* name() const override { return "gameui"; }

	void onInit() override {
		// T17: the server data provider. The Steam adapter resolves its
		// interface lazily (Steam may not be up yet at HUD_Init) and is a
		// process-lifetime singleton; the model owns the async lifecycle.
		ServerQueryState.setProvider(services::serverbrowser::steamQueryProvider());
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
		// T19: the map catalog. The shipped create-game page enumerates
		// IFileSystem::FindFirst("maps/*.bsp") over the MOUNTED search paths
		// (GameUI.dll FUN_10005630 with pathID NULL + "GAMEDOWNLOAD"); the
		// same call here so content mounted from any search path is seen, not
		// just one disk directory. Unreadable/missing -> empty catalog (the
		// page then refuses every request fail-closed with CG_ErrorUnavailable).
		loadMapCatalog();
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
		abc::ADD_COMMAND("abc_ui_server_query", serverQueryCommand);
		abc::ADD_COMMAND("abc_ui_server_status", serverStatusCommand);
		abc::ADD_COMMAND("abc_ui_server_cancel", serverCancelCommand);
		abc::ADD_COMMAND("abc_ui_server_release", serverReleaseCommand);
		abc::ADD_COMMAND("abc_ui_server_detail", serverDetailCommand);
		abc::ADD_COMMAND("abc_ui_server_detail_cancel", serverDetailCancelCommand);
		abc::ADD_COMMAND("abc_ui_server_refresh", serverRefreshCommand);
		abc::ADD_COMMAND("abc_ui_server_timeout", serverTimeoutCommand);
		abc::ADD_COMMAND("abc_ui_server_ui", serverUiCommand);
		abc::ADD_COMMAND("abc_ui_server_select", serverSelectCommand);
		abc::ADD_COMMAND("abc_ui_server_favorite", serverFavoriteCommand);
		abc::ADD_COMMAND("abc_ui_server_history", serverHistoryCommand);
		abc::ADD_COMMAND("abc_ui_server_connect", serverConnectCommand);
		abc::ADD_COMMAND("abc_ui_create_game_ui", createGameUiCommand);
		abc::ADD_COMMAND("abc_ui_create_game_fill", createGameFillCommand);
		abc::ADD_COMMAND("abc_ui_create_game_apply", createGameApplyCommand);
		abc::ADD_COMMAND("abc_ui_create_game_probe", createGameProbeCommand);
#endif
	}

	void onFrame(double frameTime) override {
		(void)frameTime;
		// T14: the engine's loading stream is drained first and independently
		// of menu presentation - the controller transitions (attempt start,
		// loading, success/failure/password) must not wait for Rml readiness,
		// and the drain is the ONLY consumer of the copied feed.
		drainLoadingFeed();
		// T17: the server-query drain runs every frame on this (main) thread
		// and independently of menu presentation. Provider callbacks arrive
		// from the engine's SteamAPI_RunCallbacks pump and land in the
		// provider's bounded queue; this is the only consumer, and the
		// query deadline is enforced here (bounded per-frame work). The
		// status-edge log rides the same beat.
		ServerQueryState.pump();
		logServerQueryChange();
		// T18: the browser page's projection beat (sync + bounded re-render on
		// change). Runs on the presented latch like the other pages.
		if (MenuPresented && BrowserPresented && Browser)
			Browser->tick();
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
					|| owner == (Browser ? Browser->document() : nullptr)
					|| owner == (CreateGamePage_ ? CreateGamePage_->document() : nullptr)
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
			} else if (state.pages.top() == MenuPage::kServers && Browser) {
				Browser->refocusDefault();
			} else if (state.pages.top() == MenuPage::kCreateGame && CreateGamePage_) {
				CreateGamePage_->refocusDefault();
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
		// T18: the server browser page (was the kServers stub). The query is
		// the module's T17 member; the list model is the module-owned view;
		// the favorites store is Steam's authoritative one (T18b).
		Browser = std::make_unique<panels::serverbrowser::ServerBrowserPage>();
		Browser->setQuery(&ServerQueryState);
		Browser->setListModel(&ServerListModel);
		Browser->setFavorites(services::serverbrowser::steamFavorites());
		// T19: the create-local-game page (was the kCreateGame stub).
		CreateGamePage_ = std::make_unique<panels::creategame::CreateGamePage>();
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
		wire(Browser);
		wire(CreateGamePage_);
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
		Browser.reset();
		CreateGamePage_.reset();
		VideoConfirm.reset();
		ConfirmQuit.reset();
		Loading.reset();
		ConnError.reset();
		Password.reset();
		DirectConnect.reset();
		// T17: every server query handle is released and the model latches
		// closed (late provider events drain-and-discard) - the module-side
		// half of the "released only after backend silence" rule.
		ServerQueryState.shutdown();
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
		CreateGamePresented = false;
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
			if (ServerPageOpen || ServerQueryState.listRequest() != 0)
				releaseServerPageQueries();
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
			case MenuPage::kServers:
				// T18: the real browser replaced the T09 stub. tick() runs on
				// the frame beat; the fresh-open query trigger is the T17
				// block below.
				presentServerBrowserPage();
				break;
			case MenuPage::kCreateGame:
				// T19: the real create-game page replaced the T09 stub.
				presentCreateGamePage();
				break;
			case MenuPage::kLoading:
				// Fresh projection on every sync: the attempt's values moved
				// since the last frame (the frame drain re-renders too). T19:
				// a local-server start carries its own loading title.
				if (Loading) {
					Loading->setTitleToken(CurrentAttemptIsLocal ? "#CG_LoadingTitle" : "");
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
		// T17: the server page's query ownership. The T18 browser lives at
		// kServers; opening the page is the user's trigger and starts the
		// ACTIVE TAB's list query (Internet on first open; a live request of
		// the same open is not re-triggered), leaving the page releases every
		// handle (release implies cancel for a still-live query). Exiting the
		// game releases through onShutdown's model shutdown.
		const bool serverPageOpen = !state.pages.empty()
			&& state.pages.top() == MenuPage::kServers;
		if (serverPageOpen && !ServerPageOpen) {
			// Fresh opening: start the user-triggered query for the active
			// tab with the current filter pairs.
			if (ServerQueryState.listRequest() == 0 && !ServerQueryState.closed())
				beginServerQuery(ServerQueryState.kind());
		} else if (ServerPageOpen && !serverPageOpen) {
			releaseServerPageQueries();
			logServerQueryChange();
		}
		ServerPageOpen = serverPageOpen;
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
		if (state.pages.empty() || state.pages.top() != MenuPage::kServers) {
			if (Browser) {
				Browser->hide();
				BrowserPresented = false;
			}
		}
		if (state.pages.empty() || state.pages.top() != MenuPage::kCreateGame) {
			if (CreateGamePage_) {
				CreateGamePage_->hide();
				CreateGamePresented = false;
			}
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

	// The server browser page (T18): state-driven, no snapshot step - the
	// query state is the truth and the frame beat projects it.
	void presentServerBrowserPage() {
		if (!Browser)
			return;
		Browser->show();
		BrowserPresented = true;
	}

	// The create-game page (T19): fresh-open prepare() (catalog options, the
	// live hostname default, cleared fields, re-armed Apply latch); later
	// syncs only show. A failed attempt's request is kept module-side, so the
	// page never needs to survive a dismiss for the retry to work.
	void presentCreateGamePage() {
		if (!CreateGamePage_)
			return;
		if (!CreateGamePresented) {
			const char* liveName = abc::CVAR_GET_STRING("hostname");
			CreateGamePage_->prepare(CreateGame.catalog(), liveName ? liveName : "");
			CreateGamePresented = true;
		}
		CreateGamePage_->show();
	}

	// T19: the engine filesystem enumeration (the shipped dialog's own call
	// set: "maps/*.bsp" with pathID NULL then "GAMEDOWNLOAD" - the mounted
	// search paths, so content from any mount is seen). A missing interface
	// leaves the catalog empty and the page fail-closes.
	void loadMapCatalog() {
		services::creategame::MapCatalog catalog;
		if (abc::g_pFileSystem) {
			const char* pathIds[] = {nullptr, "GAMEDOWNLOAD"};
			for (const char* pathId : pathIds) {
				FileFindHandle_t handle = FILESYSTEM_INVALID_FIND_HANDLE;
				const char* name = abc::g_pFileSystem->FindFirst("maps/*.bsp", &handle, pathId);
				while (name) {
					catalog.addRawName(name);
					name = abc::g_pFileSystem->FindNext(handle);
				}
				if (handle != FILESYSTEM_INVALID_FIND_HANDLE)
					abc::g_pFileSystem->FindClose(handle);
			}
		} else {
			abc::ConsoleWarn("[AbcUi] map catalog: engine filesystem unavailable\n");
		}
		catalog.finalize();
		CreateGame.setCatalog(std::move(catalog));
#ifndef NDEBUG
		abc::ConsolePrint("[AbcUi] map catalog: %d maps\n",
			static_cast<int>(CreateGame.catalog().size()));
#endif
	}

	// T19: Apply. The page's four field texts are assembled into the typed
	// request and validated by the production model BEFORE any engine call;
	// the refusal keeps the page up with a page-local hint and zero engine
	// calls. The accepted request is kept module-side for the retry path and
	// dispatched as the controller's typed local-game attempt (the controller
	// owns the generation, the Loading page and the cancel path; success is
	// the engine's loading stream's call - never declared here).
	void applyCreateGame() {
		if (!CreateGamePage_)
			return;
		if (CreateGame.catalog().size() == 0) {
			// No enumerated maps at all (filesystem unavailable): the request
			// cannot be valid, name the real cause.
			CreateGamePage_->showHint("#CG_ErrorUnavailable");
			return;
		}
		services::creategame::Request request;
		request.Map = CreateGamePage_->selectedMap();
		request.ServerName = CreateGamePage_->serverName();
		request.Password = CreateGamePage_->password();
		int players = 0;
		if (!services::creategame::CreateGameModel::parsePlayers(
				CreateGamePage_->playersText().c_str(), players)) {
			CreateGamePage_->showHint("#CG_ErrorPlayers");
			return;  // nothing reaches the controller or the engine
		}
		request.MaxPlayers = players;
		const auto validation =
			services::creategame::CreateGameModel::validate(CreateGame.catalog(), request);
		if (!validation.ok()) {
			CreateGamePage_->showHint(hintTokenFor(validation));
			return;  // nothing reaches the controller or the engine
		}
		PendingCreateGame = request;
		PendingCreateGameValid = true;
		Controller.dispatch(gameui::menu::makeCreateGameRequest(request.Map.c_str()));
		finishDispatch();
	}

	static const char* hintTokenFor(const services::creategame::Validation& validation) {
		using services::creategame::Error;
		using services::creategame::Field;
		switch (validation.error) {
		case Error::kPlayersRange:
			return "#CG_ErrorPlayers";
		case Error::kPasswordLength:
		case Error::kPasswordCharset:
			return "#CG_ErrorPassword";
		case Error::kNameEmpty:
		case Error::kNameLength:
		case Error::kNameCharset:
			return "#CG_ErrorName";
		default:
			(void)validation.field;
			return "#CG_ErrorMap";
		}
	}

	// T18: the query trigger shared by the page-open beat and the page's
	// tab/refresh/filter intents. `kind` is the active tab; the filter pairs
	// ride the view model's ServerFilter (the shipped master keys).
	void beginServerQuery(services::serverbrowser::QueryKind kind) {
		if (ServerQueryState.closed())
			return;
		services::serverbrowser::FilterPair pairs[8];
		const int count = ServerListModel.filter().buildPairs(pairs, 8);
		ServerQueryState.begin(kind, count > 0 ? pairs : nullptr, count);
		logServerQueryChange();
	}

	// T18b: connect the selected server through the T14 attempt path (the
	// same typed request the direct-connect overlay uses; the browser never
	// issues "connect" itself). The address is built from the snapshot tuple
	// and revalidated by LoadingModel::parseAddress before any engine call.
	void connectSelectedServer() {
		const auto* selected = ServerListModel.selectedEntry(ServerQueryState);
		if (!selected || !selected->responded) {
			abc::ConsoleWarn("[AbcUi] server connect refused (no responded selection)\n");
			return;
		}
		const std::string address = services::serverbrowser::buildConnectAddress(*selected);
		std::string host;
		int port = 0;
		if (address.empty() || !services::loading::LoadingModel::parseAddress(address.c_str(), host, port)) {
			abc::ConsoleWarn("[AbcUi] server connect refused (unrepresentable address)\n");
			return;
		}
		// The join-success memory entry (the shipped ConnectedToGame records
		// flags=2 into Steam's own store; see ServerFavorites.h). Kept for the
		// live attempt only - a cancel/failure retires it.
		PendingHistory = services::serverbrowser::favoriteKeyOf(*selected);
		PendingHistoryValid = PendingHistory.valid();
		Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
		finishDispatch();
	}

	// T18b: add/remove the selected server in Steam's authoritative store,
	// with an immediate read-back (both operations re-read the record; a
	// refused write never reports success). The page re-projects from the
	// store on the next tick.
	void toggleSelectedFavorite() {
		const auto* selected = ServerListModel.selectedEntry(ServerQueryState);
		if (!selected) {
			abc::ConsoleWarn("[AbcUi] favorite toggle refused (no selection)\n");
			return;
		}
		auto* favorites = services::serverbrowser::steamFavorites();
		const auto key = services::serverbrowser::favoriteKeyOf(*selected);
		if (!key.valid()) {
			abc::ConsoleWarn("[AbcUi] favorite toggle refused (unrepresentable address)\n");
			return;
		}
		const bool wasFavorite = favorites->isFavorite(key.ip, key.connPort, key.queryPort);
		const bool ok = wasFavorite
			? favorites->removeFavorite(key.ip, key.connPort, key.queryPort)
			: favorites->addFavorite(key.ip, key.connPort, key.queryPort);
		abc::ConsolePrint("[AbcUi] favorite %s \"%s\": %s (count=%d)\n",
			wasFavorite ? "remove" : "add", selected->name.c_str(), ok ? "OK" : "REFUSED",
			favorites->favoriteCount());
		if (Browser)
			Browser->tick();  // re-project the button state from the store
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

	// T17: page-leave release for the server data provider. Every handle the
	// page owns is handed back (release: cancel first when still live for the
	// list; the detail slot cancels its own query); the model's own latch
	// discipline keeps late provider events out of any projection.
	void releaseServerPageQueries() {
		ServerQueryState.cancelDetail();
		ServerQueryState.release();
		ServerPageOpen = false;
	}

	// T17: status-edge log for the live-acceptance evidence (Release runs
	// have no Debug command surface - the production console line is the
	// observable). One line per status transition, plus the count at
	// completion. Never called from anywhere but the main thread beat.
	void logServerQueryChange() {
		using services::serverbrowser::QueryStatus;
		const QueryStatus status = ServerQueryState.status();
		if (status == LastServerStatus)
			return;
		LastServerStatus = status;
		switch (status) {
		case QueryStatus::Querying:
			abc::ConsolePrint("[AbcUi] server query started: kind=%d gen=%u\n",
				static_cast<int>(ServerQueryState.kind()), ServerQueryState.generation());
			break;
		case QueryStatus::Completed:
			abc::ConsolePrint("[AbcUi] server query completed: gen=%u count=%d noServers=%d\n",
				ServerQueryState.generation(), ServerQueryState.count(),
				ServerQueryState.noServersListed() ? 1 : 0);
			break;
		case QueryStatus::Cancelled:
			abc::ConsolePrint("[AbcUi] server query cancelled: gen=%u partial=%d\n",
				ServerQueryState.generation(), ServerQueryState.count());
			break;
		case QueryStatus::Failed:
			abc::ConsolePrint("[AbcUi] server query failed: gen=%u failure=%d\n",
				ServerQueryState.generation(), static_cast<int>(ServerQueryState.failure()));
			break;
		default:
			break;
		}
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
		else if (intent.rfind("cg-", 0) == 0) {
			// T19 page-internal intents (fixed names only): Apply assembles
			// and validates the request; Cancel is a plain back with zero
			// engine writes (the page never held any).
			if (intent == "cg-apply")
				applyCreateGame();
			else if (intent == "cg-cancel")
				Controller.dispatch(MenuEvent{EventKind::kBack});
			else {
				abc::ConsoleWarn("[AbcUi] unknown create-game action \"%s\" dropped\n", intent.c_str());
				return;
			}
			finishDispatch();
			return;
		}
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
			// A NEW attempt generation from the same target (kept by the
			// model/request across the failure) - never a reuse of the dead
			// one. T19: a failed LOCAL attempt replays its kept request
			// through the same typed path (the launch re-runs from scratch;
			// the phase gate keeps the double-retry honest).
			if (PendingCreateGameValid && LastAttemptWasLocal) {
				Controller.dispatch(
					gameui::menu::makeCreateGameRequest(PendingCreateGame.Map.c_str()));
				finishDispatch();
				return;
			}
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
		else if (intent.rfind("sb-", 0) == 0) {
			// T18 browser intents (fixed page-internal names only): the query
			// lifecycle entries. Row/sort/paging never reach here (the page
			// resolves them against its models directly).
			using services::serverbrowser::QueryKind;
			if (intent == "sb-tab-internet")
				beginServerQuery(QueryKind::Internet);
			else if (intent == "sb-tab-lan")
				beginServerQuery(QueryKind::Lan);
			else if (intent == "sb-tab-favorites")
				beginServerQuery(QueryKind::Favorites);
			else if (intent == "sb-tab-history")
				beginServerQuery(QueryKind::History);
			else if (intent == "sb-refresh") {
				// The shipped Refresh button semantics (T17 pin): RefreshQuery
				// on a live handle; a fresh request when nothing is live.
				if (ServerQueryState.listRequest() != 0)
					ServerQueryState.refreshAll();
				else
					beginServerQuery(ServerQueryState.kind());
				logServerQueryChange();
			}
			else if (intent == "sb-stop") {
				ServerQueryState.cancel();
				logServerQueryChange();
			}
			else if (intent == "sb-filter-empty" || intent == "sb-filter-full"
				|| intent == "sb-filter-secure") {
				// The view model owns the filter truth; a toggle re-issues the
				// list query with the new master pairs (the original's filter
				// apply). Client-side the same rules re-project immediately.
				services::serverbrowser::ServerFilter filter = ServerListModel.filter();
				if (intent == "sb-filter-empty")
					filter.hideEmpty = !filter.hideEmpty;
				else if (intent == "sb-filter-full")
					filter.hideFull = !filter.hideFull;
				else
					filter.secureOnly = !filter.secureOnly;
				ServerListModel.setFilter(filter);
				beginServerQuery(ServerQueryState.kind());
			}
			else if (intent == "sb-connect") {
				connectSelectedServer();
				return;
			}
			else if (intent == "sb-favorite-toggle") {
				toggleSelectedFavorite();
				return;
			}
			else {
				abc::ConsoleWarn("[AbcUi] unknown menu action \"%s\" dropped\n", intent.c_str());
				return;
			}
			finishDispatch();
			return;
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
	// drain: reconcile the attempt bookkeeping, resolve a refused launch,
	// re-render, and lower the bridge when the state machine no longer holds
	// the menu.
	void finishDispatch() {
		syncAttemptState();
		// T19: a refused local launch (cvar write failed / seams unwired)
		// fails the attempt through the SAME typed path a connect failure
		// uses - the error overlay with the reason token. The controller
		// already started the attempt (generation + Loading page), so this
		// must run after the model sync above made the generation live.
		if (consumeLaunchFailure())
			syncAttemptState();  // the refused attempt is over: retire + re-project
		if (MenuPresented)
			syncMenuPresentation();
		if (!Controller.isMenuActive() && services::nativeUiBridge().menuUp())
			services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
	}

	// T19: a refused launch never reaches dispatch() from inside the backend
	// call (that would re-enter the controller mid-transition); the thunk
	// parks the reason token here and this consumer feeds it as the typed
	// generation-tagged failure once the dispatch returned.
	bool consumeLaunchFailure() {
		if (!PendingLaunchFailure)
			return false;
		const char* reason = PendingLaunchFailure;
		PendingLaunchFailure = nullptr;
		const uint32_t live = LoadingModelState.generation();
		if (live != 0) {
			Controller.dispatch(gameui::menu::makeLoadingFinished(live,
				gameui::menu::LoadingOutcome::kFailed, reason));
			return true;
		}
		return false;
	}

	// T19: the launcher relayed by services::EngineUiBackend::startLocalGame.
	// It owns no state of its own: the module's kept request is the truth (the
	// controller passed the same map token, so a mismatch means a wiring bug -
	// fail closed rather than launching something else).
	static bool launchLocalGame(const char* map, const char** reasonToken) {
		if (reasonToken)
			*reasonToken = "#CG_ErrorInternal";
		if (!Instance || !reasonToken)
			return false;
		GameUiModule& self = *Instance;
		if (!self.PendingCreateGameValid || !map
			|| self.PendingCreateGame.Map != map) {
			abc::ConsoleWarn("[AbcUi] local game launch mismatch (no kept request)\n");
			return false;
		}
		switch (self.CreateGame.launch(self.PendingCreateGame)) {
		case services::creategame::CreateGameModel::Launch::kLaunched:
			return true;
		case services::creategame::CreateGameModel::Launch::kInvalid:
			*reasonToken = "#CG_ErrorInternal";  // validated before the dispatch; defensive
			return false;
		case services::creategame::CreateGameModel::Launch::kWriteFailed:
			*reasonToken = "#CG_ErrorWrite";
			return false;
		case services::creategame::CreateGameModel::Launch::kUnwired:
			*reasonToken = "#CG_ErrorInternal";
			return false;
		}
		return false;
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
			// T19: which kind this generation came from (the loading page
			// title); the controller retires its own attemptKind with the
			// generation, so capture it while it is still set.
			CurrentAttemptIsLocal = state.attemptKind == gameui::menu::AttemptKind::kLocalGame;
			if (!CurrentAttemptIsLocal)
				PendingCreateGameValid = false;  // a stale local request can never retry here
			PasswordInvalid = false;
			PasswordPrompts = 0;
			PasswordReprepare = false;
		}
		if (LoadingModelState.generation() != 0 && LoadingModelState.generation() != state.connectGeneration) {
			// The attempt ended (cancel/success/failure): the stream of the
			// retired generation keeps arriving and is dropped by the model's
			// generation filter from here on.
			LoadingModelState.retire();
			// T19: remember the ended attempt's kind for the error overlay's
			// retry (the controller's own attemptKind is already retired).
			LastAttemptWasLocal = CurrentAttemptIsLocal;
			CurrentAttemptIsLocal = false;
			PasswordInvalid = false;
			PasswordPrompts = 0;
			PasswordReprepare = false;
			// T18b: an attempt that ends without the join beat retires the
			// pending history entry unrecorded (only a real join writes it).
			if (PendingHistoryValid)
				PendingHistoryValid = false;
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
				if (live != 0) {
					// T18b: the engine-confirmed join is the history moment
					// (the shipped ConnectedToGame wrote flags=2 into Steam's
					// store exactly here).
					recordPendingHistory();
					// T19: a confirmed local join consumes the kept request -
					// nothing left to retry once the server is up.
					PendingCreateGameValid = false;
					Controller.dispatch(gameui::menu::makeLoadingFinished(live,
						gameui::menu::LoadingOutcome::kSuccess, nullptr));
				}
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

	// T18b: the pending history record of the live browser-originated attempt.
	// Set on connectSelectedServer, consumed exactly once on the join-success
	// beat; a failed/cancelled attempt retires it unrecorded (the shipped
	// ConnectedToGame never fires for those either).
	void recordPendingHistory() {
		if (!PendingHistoryValid)
			return;
		PendingHistoryValid = false;
		auto* favorites = services::serverbrowser::steamFavorites();
		const bool ok = favorites->addHistory(PendingHistory.ip, PendingHistory.connPort,
			PendingHistory.queryPort);
		abc::ConsolePrint("[AbcUi] history record %u.%u.%u.%u:%u: %s\n",
			PendingHistory.ip >> 24, (PendingHistory.ip >> 16) & 0xff,
			(PendingHistory.ip >> 8) & 0xff, PendingHistory.ip & 0xff, PendingHistory.connPort,
			ok ? "OK" : "FAILED");
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
		if (Browser) {
			Browser->hide();
			BrowserPresented = false;
		}
		if (CreateGamePage_) {
			CreateGamePage_->hide();
			CreateGamePresented = false;
		}
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

	// T17 diagnostics: start a list query through the REAL production model
	// and provider (the accepted live evidence path). Usage:
	//   abc_ui_server_query <internet|lan|favorites|history|spectate|friends> [filterKey filterValue ...]
	static void serverQueryCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (gEngfuncs.Cmd_Argc() < 2) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_server_query <internet|lan|favorites|history|spectate|friends> [key value ...]\n");
			return;
		}
		const std::string kindName = gEngfuncs.Cmd_Argv(1);
		services::serverbrowser::QueryKind kind;
		if (kindName == "internet")
			kind = services::serverbrowser::QueryKind::Internet;
		else if (kindName == "lan")
			kind = services::serverbrowser::QueryKind::Lan;
		else if (kindName == "favorites")
			kind = services::serverbrowser::QueryKind::Favorites;
		else if (kindName == "history")
			kind = services::serverbrowser::QueryKind::History;
		else if (kindName == "spectate")
			kind = services::serverbrowser::QueryKind::Spectate;
		else if (kindName == "friends")
			kind = services::serverbrowser::QueryKind::Friends;
		else {
			abc::ConsoleWarn("[AbcUi] unknown query kind \"%s\"\n", kindName.c_str());
			return;
		}
		std::vector<services::serverbrowser::FilterPair> filters;
		for (int i = 2; i + 1 < gEngfuncs.Cmd_Argc(); i += 2)
			filters.push_back({gEngfuncs.Cmd_Argv(i), gEngfuncs.Cmd_Argv(i + 1)});
		self.ServerPageOpen = true;  // diagnostic opens the query context explicitly
		const bool started = self.ServerQueryState.begin(kind,
			filters.empty() ? nullptr : filters.data(), static_cast<int>(filters.size()));
		abc::ConsolePrint("[AbcUi] server query %s: kind=%s gen=%u request=%llu\n",
			started ? "STARTED" : "FAILED", kindName.c_str(), self.ServerQueryState.generation(),
			static_cast<unsigned long long>(self.ServerQueryState.listRequest()));
		serverStatusCommand();
	}

	// T17 diagnostics: one-line state projection of the model + provider.
	static void serverStatusCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		auto& query = self.ServerQueryState;
		abc::ConsolePrint(
			"[AbcUi] server: status=%d failure=%d gen=%u count=%d listing=%llu noServers=%d stale=%llu dropped=%d failedPings=%d closed=%d pageOpen=%d\n",
			static_cast<int>(query.status()), static_cast<int>(query.failure()),
			query.generation(), query.count(),
			static_cast<unsigned long long>(query.listRequest()),
			query.noServersListed() ? 1 : 0,
			static_cast<unsigned long long>(query.staleEvents()),
			query.droppedServers(), query.failedPings(), query.closed() ? 1 : 0,
			self.ServerPageOpen ? 1 : 0);
		abc::ConsolePrint(
			"[AbcUi] server: detail=%d detailStatus=%d detailFailed=%d players=%d handle=%llu outstanding=%d\n",
			static_cast<int>(query.detailKind()), static_cast<int>(query.detailStatus()),
			query.detailFailed() ? 1 : 0, query.playerCount(),
			static_cast<unsigned long long>(query.detailRequest()),
			query.providerOutstanding());
		const int shown = query.count() < 10 ? query.count() : 10;
		for (int i = 0; i < shown; ++i) {
			const auto* entry = query.entry(i);
			if (!entry)
				continue;
			abc::ConsolePrint(
				"[AbcUi] server[%d]: name=\"%s\" map=\"%s\" players=%d/%d bots=%d ping=%d password=%d secure=%d\n",
				i, entry->name.c_str(), entry->map.c_str(), entry->players,
				entry->maxPlayers, entry->botPlayers, entry->pingMs,
				entry->password ? 1 : 0, entry->secure ? 1 : 0);
		}
	}

	// T17 diagnostics: the explicit cancel path (stops work, keeps the handle).
	static void serverCancelCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		Instance->ServerQueryState.cancel();
		abc::ConsolePrint("[AbcUi] server cancel issued\n");
		serverStatusCommand();
	}

	// T17 diagnostics: the page-leave release path (hand every handle back).
	static void serverReleaseCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		Instance->releaseServerPageQueries();
		abc::ConsolePrint("[AbcUi] server release issued\n");
		serverStatusCommand();
	}

	// T17 diagnostics: single-server queries through the real model. Usage:
	//   abc_ui_server_detail <ping|players> <index>   (index into the live list)
	static void serverDetailCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (gEngfuncs.Cmd_Argc() < 3) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_server_detail <ping|players> <index>\n");
			return;
		}
		const std::string kind = gEngfuncs.Cmd_Argv(1);
		const int index = atoi(gEngfuncs.Cmd_Argv(2));
		const auto* server = self.ServerQueryState.entry(index);
		if (!server) {
			abc::ConsoleWarn("[AbcUi] no server at index %d\n", index);
			return;
		}
		uint64_t handle = 0;
		if (kind == "ping")
			handle = self.ServerQueryState.requestPing(*server);
		else if (kind == "players")
			handle = self.ServerQueryState.requestPlayers(*server);
		else {
			abc::ConsoleWarn("[AbcUi] unknown detail kind \"%s\"\n", kind.c_str());
			return;
		}
		abc::ConsolePrint("[AbcUi] server detail %s %s: handle=%llu\n",
			kind.c_str(), handle ? "STARTED" : "FAILED",
			static_cast<unsigned long long>(handle));
		serverStatusCommand();
	}

	// T17 diagnostics: cancel the live single-server query.
	static void serverDetailCancelCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		Instance->ServerQueryState.cancelDetail();
		abc::ConsolePrint("[AbcUi] server detail cancel issued\n");
		serverStatusCommand();
	}

	// T17 diagnostics: RefreshQuery through the REAL provider (the live
	// cancellation evidence rides this: refresh, then cancel mid-refresh).
	static void serverRefreshCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (self.ServerQueryState.listRequest() == 0) {
			abc::ConsoleWarn("[AbcUi] no live request to refresh\n");
			return;
		}
		self.ServerQueryState.refreshAll();
		abc::ConsolePrint("[AbcUi] server refresh issued\n");
		serverStatusCommand();
	}

	// T18b diagnostics: the SAME module path the page's favorite button
	// drives (the selected server's store toggle with read-back).
	static void serverFavoriteCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		auto* favorites = services::serverbrowser::steamFavorites();
		abc::ConsolePrint("[AbcUi] favorites: available=%d favCount=%d histCount=%d\n",
			favorites->available() ? 1 : 0, favorites->favoriteCount(),
			favorites->historyCount());
		const auto* selected = self.ServerListModel.selectedEntry(self.ServerQueryState);
		if (!selected) {
			abc::ConsoleWarn("[AbcUi] favorites: no selection (use abc_ui_server_select first)\n");
			return;
		}
		const auto key = services::serverbrowser::favoriteKeyOf(*selected);
		abc::ConsolePrint("[AbcUi] favorites: selected=\"%s\" key=%u.%u.%u.%u:%u/%u favorited=%d\n",
			selected->name.c_str(), key.ip >> 24, (key.ip >> 16) & 0xff, (key.ip >> 8) & 0xff,
			key.ip & 0xff, key.connPort, key.queryPort,
			favorites->isFavorite(key.ip, key.connPort, key.queryPort) ? 1 : 0);
	}

	// T18b diagnostics: force a history record exactly like the join beat
	// (the shipped ConnectedToGame entry). Usage: abc_ui_server_history <ip> <connPort> <queryPort>
	static void serverHistoryCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		if (gEngfuncs.Cmd_Argc() < 4) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_server_history <ip> <connPort> <queryPort>\n");
			return;
		}
		// ip accepts dotted quads (the same text form the UI shows).
		unsigned a = 0, b = 0, c = 0, d = 0;
		if (std::sscanf(gEngfuncs.Cmd_Argv(1), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
			abc::ConsoleWarn("[AbcUi] bad ip\n");
			return;
		}
		const uint32_t ip = (a << 24) | (b << 16) | (c << 8) | d;
		const uint16_t conn = static_cast<uint16_t>(atoi(gEngfuncs.Cmd_Argv(2)));
		const uint16_t query = static_cast<uint16_t>(atoi(gEngfuncs.Cmd_Argv(3)));
		auto* favorites = services::serverbrowser::steamFavorites();
		const bool ok = favorites->addHistory(ip, conn, query);
		abc::ConsolePrint("[AbcUi] history %u.%u.%u.%u:%u/%u: %s (histCount=%d)\n", a, b, c, d, conn,
			query, ok ? "OK" : "FAILED", favorites->historyCount());
	}

	// T18b diagnostics: the SAME connect path the page's connect button
	// drives (selected server -> typed T14 attempt + pending history).
	static void serverConnectCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		const auto* selected = self.ServerListModel.selectedEntry(self.ServerQueryState);
		if (!selected) {
			abc::ConsoleWarn("[AbcUi] connect: no selection\n");
			return;
		}
		self.connectSelectedServer();
		abc::ConsolePrint("[AbcUi] connect issued: phase=%d addr=\"%s\"\n",
			static_cast<int>(self.Controller.state().phase),
			self.Controller.state().connectAddress.c_str());
	}

	// T17 diagnostics: temporarily shorten the deadline (ms; 0 restores the
	// 30s default) - the live timeout-path probe (a healthy Internet query
	// never hits the deadline on its own).
	static void serverTimeoutCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (gEngfuncs.Cmd_Argc() < 2) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_server_timeout <ms|0>\n");
			return;
		}
		const int ms = atoi(gEngfuncs.Cmd_Argv(1));
		self.ServerQueryState.setTimeoutMs(
			ms > 0 ? static_cast<uint32_t>(ms) : services::serverbrowser::kDefaultTimeoutMs);
		abc::ConsolePrint("[AbcUi] server timeout set to %d ms\n", ms > 0 ? ms : 30000);
	}

	// T18 diagnostics: the SAME path a row click drives (select by stable
	// key, then the single-slot player query). T11/T12/T14 precedent: the
	// scripted click injection is unreliable in this harness, the module
	// path under test is identical. Usage: abc_ui_server_select <row>
	static void serverSelectCommand() {
		if (!Instance || !Instance->Browser || !Instance->Browser->document()) {
			abc::ConsoleWarn("[AbcUi] browser page not open\n");
			return;
		}
		if (gEngfuncs.Cmd_Argc() < 2) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_server_select <row>\n");
			return;
		}
		GameUiModule& self = *Instance;
		const int row = atoi(gEngfuncs.Cmd_Argv(1));
		self.ServerListModel.selectRow(row);
		const auto* selected = self.ServerListModel.selectedEntry(self.ServerQueryState);
		if (!selected) {
			abc::ConsoleWarn("[AbcUi] no server at row %d\n", row);
			return;
		}
		const uint64_t handle = self.ServerQueryState.requestPlayers(*selected);
		abc::ConsolePrint("[AbcUi] server select row=%d key=%llu name=\"%s\" detail=%s\n", row,
			static_cast<unsigned long long>(self.ServerListModel.selectedKey()),
			selected->name.c_str(), handle ? "STARTED" : "FAILED");
	}

	// T18 diagnostics: the browser page's DOM truth (row count, first-row
	// geometry, visible flags) - the live projection probe.
	static void serverUiCommand() {
		if (!Instance || !Instance->Browser || !Instance->Browser->document()) {
			abc::ConsoleWarn("[AbcUi] browser page not open\n");
			return;
		}
		Rml::ElementDocument* doc = Instance->Browser->document();
		Rml::Element* list = doc->GetElementById("sb-list");
		abc::ConsolePrint("[AbcUi] browser-ui: doc=%s list=%s\n", doc->IsVisible() ? "visible" : "hidden",
			list ? "found" : "missing");
		if (list) {
			abc::ConsolePrint("[AbcUi] browser-ui: rows=%d scroll=%d client=%.1fx%.1f\n",
				list->GetNumChildren(), list->GetScrollTop(), list->GetClientWidth(),
				list->GetClientHeight());
			if (list->HasChildNodes()) {
				Rml::Element* first = list->GetFirstChild();
				abc::ConsolePrint("[AbcUi] browser-ui: first=<%s> pos=%.1f,%.1f size=%.1fx%.1f visible=%d\n",
					first->GetTagName().c_str(), first->GetAbsoluteLeft(),
					first->GetAbsoluteTop(), first->GetClientWidth(),
					first->GetClientHeight(), first->IsVisible() ? 1 : 0);
			}
		}
		if (Rml::Element* status = doc->GetElementById("sb-status"))
			abc::ConsolePrint("[AbcUi] browser-ui: status='%s'\n", status->GetInnerRML().c_str());
	}

	// T19 diagnostics: the create-game page's DOM truth (map option count,
	// current field values, Apply latch) - the live projection probe.
	static void createGameUiCommand() {
		if (!Instance || !Instance->CreateGamePage_ || !Instance->CreateGamePage_->document()) {
			abc::ConsoleWarn("[AbcUi] create-game page not open\n");
			return;
		}
		GameUiModule& self = *Instance;
		const auto& catalog = self.CreateGame.catalog();
		abc::ConsolePrint("[AbcUi] create-game-ui: maps=%d first=%s\n",
			static_cast<int>(catalog.size()),
			catalog.size() > 0 ? catalog.nameAt(0).c_str() : "(none)");
		abc::ConsolePrint("[AbcUi] create-game-ui: map='%s' players='%s' name='%s' pw=%d\n",
			self.CreateGamePage_->selectedMap().c_str(),
			self.CreateGamePage_->playersText().c_str(),
			self.CreateGamePage_->serverName().c_str(),
			self.CreateGamePage_->password().empty() ? 0 : 1);
		abc::ConsolePrint("[AbcUi] create-game-ui: pending=%d map=%s lastLocal=%d\n",
			self.PendingCreateGameValid ? 1 : 0,
			self.PendingCreateGameValid ? self.PendingCreateGame.Map.c_str() : "(none)",
			self.LastAttemptWasLocal ? 1 : 0);
	}

	// T19 diagnostics: fill the page's fields through the same values the
	// clicks would produce, then report - the scripted-typing stand-in (T11/
	// T13/T14 record: injected text entry is unreliable). Never bypasses the
	// page: the fields really change, Apply still reads them.
	// Usage: abc_ui_create_game_fill <map> <players> <name> [password]
	static void createGameFillCommand() {
		if (!Instance || !Instance->CreateGamePage_ || !Instance->CreateGamePage_->document()) {
			abc::ConsoleWarn("[AbcUi] create-game page not open\n");
			return;
		}
		if (gEngfuncs.Cmd_Argc() < 4) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_create_game_fill <map> <players> <name> [password]\n");
			return;
		}
		GameUiModule& self = *Instance;
		Rml::ElementDocument* doc = self.CreateGamePage_->document();
		if (Rml::ElementFormControlSelect* select =
				rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(doc->GetElementById("cg-map"))) {
			// Select by exact value when present, else refuse (the option set
			// is the catalog - a fill cannot invent an entry).
			bool found = false;
			for (int i = 0; i < select->GetNumOptions() && !found; ++i) {
				Rml::Element* option = select->GetOption(i);
				if (option && option->GetAttribute<Rml::String>("value", "") == gEngfuncs.Cmd_Argv(1)) {
					select->SetSelection(i);
					found = true;
				}
			}
			if (!found)
				abc::ConsoleWarn("[AbcUi] map '%s' is not an enumerated option\n", gEngfuncs.Cmd_Argv(1));
		}
		if (Rml::ElementFormControl* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(
				doc->GetElementById("cg-players")))
			control->SetValue(gEngfuncs.Cmd_Argv(2));
		if (Rml::ElementFormControl* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(
				doc->GetElementById("cg-name")))
			control->SetValue(gEngfuncs.Cmd_Argv(3));
		if (gEngfuncs.Cmd_Argc() >= 5) {
			if (Rml::ElementFormControl* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(
					doc->GetElementById("cg-password")))
				control->SetValue(gEngfuncs.Cmd_Argv(4));
		}
		createGameUiCommand();
	}

	// T19 diagnostics: the same module path the Apply button drives (request
	// assembly + validation + typed dispatch). The page fields are the source,
	// exactly like a real click.
	static void createGameApplyCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		self.applyCreateGame();
		abc::ConsolePrint("[AbcUi] create-game apply issued: phase=%d gen=%u map=%s pending=%d\n",
			static_cast<int>(self.Controller.state().phase), self.Controller.state().connectGeneration,
			self.Controller.state().connectAddress.c_str(), self.PendingCreateGameValid ? 1 : 0);
	}

	// T19 diagnostics: the launch-evidence probe. Runs the launcher against a
	// synthetic request through the REAL seams (the same model path) without
	// touching the controller; prints the per-step outcome. Usage:
	//   abc_ui_create_game_probe <map> <players> <name> [password]
	static void createGameProbeCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		if (gEngfuncs.Cmd_Argc() < 4) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_create_game_probe <map> <players> <name> [password]\n");
			return;
		}
		GameUiModule& self = *Instance;
		services::creategame::Request request;
		request.Map = gEngfuncs.Cmd_Argv(1);
		int players = 0;
		if (!services::creategame::CreateGameModel::parsePlayers(gEngfuncs.Cmd_Argv(2), players)) {
			abc::ConsoleWarn("[AbcUi] probe refused: bad players\n");
			return;
		}
		request.MaxPlayers = players;
		request.ServerName = gEngfuncs.Cmd_Argv(3);
		if (gEngfuncs.Cmd_Argc() >= 5)
			request.Password = gEngfuncs.Cmd_Argv(4);
		const auto validation =
			services::creategame::CreateGameModel::validate(self.CreateGame.catalog(), request);
		abc::ConsolePrint("[AbcUi] probe validate: ok=%d error=%d field=%d\n",
			validation.ok() ? 1 : 0, static_cast<int>(validation.error),
			static_cast<int>(validation.field));
		if (!validation.ok())
			return;
		const auto outcome = self.CreateGame.launch(request);
		abc::ConsolePrint("[AbcUi] probe launch: outcome=%d failIdx=%d restoreFailures=%d\n",
			static_cast<int>(outcome), self.CreateGame.lastFailIndex(),
			self.CreateGame.lastRestoreFailures());
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
	// T17 server data provider: the async list/ping/players lifecycle (the
	// Steam adapter is injected in onInit; the model owns generations,
	// cancellation/release and the deadline). The T18 browser consumes it.
	services::serverbrowser::ServerQuery ServerQueryState;
	// T18 browser: the filter/sort/paging view model over the query snapshot
	// and the kServers page projection. Filter truth is module-held (the
	// page's intents flip it), the page renders from it.
	services::serverbrowser::ServerListModel ServerListModel;
	// T19 create-local-game domain: the enumerated map catalog (filled onInit
	// from the engine filesystem - the shipped dialog's own source) and the
	// launcher (snapshot/read-back/restore inside). The catalog outlives the
	// page; the model owns the seam wiring.
	services::creategame::CreateGameModel CreateGame;
	// The request of the live (or last failed) local-game attempt. The retry
	// path replays it verbatim; Cancel/leave never writes it anywhere.
	services::creategame::Request PendingCreateGame;
	bool PendingCreateGameValid = false;
	// T19 attempt-kind facts: which kind the LIVE model generation came from
	// (drives the loading title) and which kind the last ENDED attempt was
	// (drives the error overlay's retry). The controller retires its own
	// attemptKind with the generation, so the module remembers the two.
	bool CurrentAttemptIsLocal = false;
	bool LastAttemptWasLocal = false;
	// A refused local launch (validation passed but a cvar write failed /
	// seams unwired): the reason token is consumed by finishDispatch, which
	// fails the attempt through the same typed path a connect failure uses.
	const char* PendingLaunchFailure = nullptr;
	// Whether the kServers page is the presented surface (the frame beat's
	// tick runs on it; drives page-leave release together with the sync edge).
	bool BrowserPresented = false;
	// T19: whether the kCreateGame page is the presented surface (fresh-open
	// prepare() runs once per open; the Apply latch lives in the page).
	bool CreateGamePresented = false;
	// T18b: the join-memory entry of the live browser-originated attempt
	// (consumed on the kJoined beat; retired unrecorded otherwise).
	services::serverbrowser::FavoriteKey PendingHistory;
	bool PendingHistoryValid = false;
	// Whether the kServers page is the top menu page (fresh-open projection
	// latch by analogy with the settings page; drives page-leave release).
	bool ServerPageOpen = false;
	// Last logged query status (edge detection for the production log line).
	services::serverbrowser::QueryStatus LastServerStatus =
		services::serverbrowser::QueryStatus::Idle;
	std::unique_ptr<panels::mainmenu::MainMenuPage> Shell;
	std::unique_ptr<panels::menushared::MenuStubPage> SubStub;
	std::unique_ptr<panels::console::ConsolePage> Console;
	std::unique_ptr<panels::settings::SettingsPage> Settings;
	std::unique_ptr<panels::serverbrowser::ServerBrowserPage> Browser;
	std::unique_ptr<panels::creategame::CreateGamePage> CreateGamePage_;  // T19 page
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
