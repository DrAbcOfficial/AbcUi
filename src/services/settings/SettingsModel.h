#pragma once

// services::settings - the settings description table and the draft
// transaction model (T10), extended in T13 with the remaining shipped
// options (audio, mouse, instant video rows, voice cvars, multiplayer and
// the user.scr advanced rows).
//
// The description table is the single source of truth for what the settings
// page offers. Every row cites a REAL, verified backend registration - never
// a name guessed from a resource label (settings-inventory.md boundary):
//   * engine cvars carry the registration evidence from the shipped binaries
//     (default string + flags read out of the registering module, UI domain
//     read out of the shipped GameUI page constructors),
//   * user.scr rows carry the shipped advanced-options script (the exact
//     source the original MultiplayerAdvancedDialog parses),
//   * AbcUi cvars cite our own CREATE_CVAR registration (the source IS the
//     default source).
// Rows whose backend cannot be verified stay OUT of the table; a row whose
// cvar is not registered at runtime renders as unavailable instead of
// silently no-oping.
//
// Draft transaction: the page never writes a cvar outside Apply. snapshot()
// reads engine truth; edits land in per-row drafts; apply() writes through
// the injected seams (production: CVAR_SET_FLOAT / CVAR_SET_STRING - engine
// APIs, never a concatenated "name value" command) and reads back; cancel()
// discards drafts and provably writes nothing.
//
// The ONLY command-channel writes are the kCommandToggle rows (in_mlook /
// in_jlook: +mlook / -mlook - the vanilla page sends the same commands and
// reads IsKeyDown; there is no cvar behind them). Their effect lands on the
// engine command flush, so apply leaves them kQueued and snapshot() settles
// them against the button state within a couple of frames (the T11 verify
// beat, one model earlier).
//
// String rows (name / model / cl_logofile / cl_logocolor) validate BEFORE
// the write: printable bytes only, no quote/backslash/control characters,
// enum options must come from the scanned (or fixed) option set, and name
// is capped at the RES maxchars=63 byte budget. The engine then receives a
// CVAR_SET_STRING, not a command - nothing user-typed is ever spliced into
// a command line (T13 boundary).
//
// External-change rule (V10): snapshot() refreshes rows the user has not
// touched and flags rows it HAS touched as kConflict instead of quietly
// overwriting the draft. Apply is a sequence of independent per-cvar writes -
// the engine exposes no cross-cvar atomic transaction, so a partial failure
// keeps the failed rows dirty with per-row outcomes and never pretends the
// batch committed.
//
// No engine/Rml includes: the exact production model links into offline
// tests with a fake backend (05 doc evidence tier 1/2). Persistence stays
// engine-side (config.cfg for archive cvars; session-only for the ones the
// engine registers without FCVAR_ARCHIVE - the original behaves the same).

#include <deque>
#include <string>
#include <vector>

namespace services {
namespace settings {

enum class SettingKind {
	kBool,          // domain {0, 1}; toggle control; cvar float write
	kFloat,         // [Min, Max] numeric entry; cvar float write
	kEnum,          // LIST from the shipped table: value picked from Options
	kNegate,        // m_pitch: bool view over the sign; toggle flips the draft
	kText,          // name: validated text row; CVAR_SET_STRING write
	kStringEnum,    // model / cl_logofile / cl_logocolor: token from a fixed
	                // or scanned option set; CVAR_SET_STRING write
	kCommandToggle, // in_mlook/in_jlook: read IsKeyDown(state), write the
	                // +/- command; kQueued until the engine flushes
};

// One LIST option (user.scr LIST rows and cl_logocolor). Label is a lang
// token ("#Valve_Green") or a literal; the page resolves and escapes it.
struct SettingEnumOption {
	float Value;
	const char* Label;
};

// One description-table row. Static-literal fields for table rows; dynamic
// rows (user.scr) own their strings in a UserScrTable arena that outlives
// the model.
struct SettingRow {
	const char* Cvar;        // engine-registered name, referenced verbatim
	SettingKind Kind;
	float Min;               // UI domain (kBool/kCommandToggle: 0)
	float Max;               // UI domain (kBool/kCommandToggle: 1)
	float Step;              // slider stride hint (0 = free numeric entry)
	float DefaultValue;      // verified registration / user.scr default
	const char* LabelToken;  // "#Settings_*" lang token
	const char* Section;     // "#Settings_Sec*" group header token; the page
	                         // renders a divider whenever it changes
	const SettingEnumOption* Options = nullptr; // kEnum option table
	int OptionCount = 0;
	const char* StateName = nullptr;  // kCommandToggle: IsKeyDown state name
	                                  // ("in_mlook"); kStringEnum: fixed
	                                  // option set name (diagnostics only)
	const char* DefaultString = nullptr; // kText/kStringEnum default value
	                                     // (null: format DefaultValue)
	const char* OnCommand = nullptr;  // kCommandToggle: checked-side command
	const char* OffCommand = nullptr; // kCommandToggle: unchecked-side command
};

// The T10 first batch + T13 additions. Verification per row lives next to
// the row (see the .cpp comments and execution/T13.md for the address-level
// evidence): engine cvars cite the registering module (hw.dll static cvar
// table entry or client.dll pfnRegisterVariable call site), UI domains cite
// the shipped GameUI page constructors, user.scr rows cite the shipped
// script, AbcUi cvars cite our CREATE_CVAR calls.
inline constexpr SettingEnumOption kHpkMaxsizeOptions[] = {
	{-1.0f, "#Settings_HpkDisabled"}, {2.0f, "#Settings_HpkSmall"},
	{4.0f, "#Settings_HpkNormal"}, {16.0f, "#Settings_HpkLarge"},
};
inline constexpr SettingEnumOption kWeaponAutoSwitchOptions[] = {
	{0.0f, "#Settings_AutoSwitchNever"}, {1.0f, "#Settings_AutoSwitchBetter"},
	{2.0f, "#Settings_AutoSwitchAlways"},
};
inline constexpr SettingEnumOption kBobOptions[] = {
	{0.0f, "#Settings_BobOff"}, {0.01f, "#Settings_BobNormal"},
	{0.025f, "#Settings_BobExtra"},
};
inline constexpr SettingEnumOption kRollAngleOptions[] = {
	{0.0f, "#Settings_BobOff"}, {2.0f, "#Settings_BobNormal"},
	{5.0f, "#Settings_BobExtra"}, {-2.0f, "#Settings_BobInverted"},
	{-5.0f, "#Settings_BobInvertedExtra"},
};
inline constexpr SettingEnumOption kDiscordOptions[] = {
	{0.0f, "#Settings_DiscordGame"}, {1.0f, "#Settings_DiscordStats"},
	{2.0f, "#Settings_DiscordInvites"},
};
inline constexpr SettingEnumOption kDeveloperOptions[] = {
	{0.0f, "#Settings_DeveloperOff"}, {1.0f, "#Settings_DeveloperStandard"},
	{2.0f, "#Settings_DeveloperVerbose"}, {3.0f, "#Settings_DeveloperVery"},
	{4.0f, "#Settings_DeveloperExtreme"},
};
// cl_logocolor: the exact 9-entry table the shipped GameUI multiplayer page
// iterates (GameUI.dll 0x100a5a98..0x100a5b28, applied via
// `cl_logocolor <token>`); values keep the "#" prefix the engine stores
// (config.cfg ships `cl_logocolor "#Valve_Ltblue"`).
inline constexpr SettingEnumOption kLogoColorOptions[] = {
	{0.0f, "#Valve_Orange"}, {1.0f, "#Valve_Yellow"}, {2.0f, "#Valve_Blue"},
	{3.0f, "#Valve_Ltblue"}, {4.0f, "#Valve_Green"}, {5.0f, "#Valve_Red"},
	{6.0f, "#Valve_Brown"}, {7.0f, "#Valve_Ltgray"}, {8.0f, "#Valve_Dkgray"},
};

inline constexpr SettingRow FirstBatchRows[] = {
	// ---- T10 first batch (evidence: SettingsModel history / T10 record) ----
	{"sensitivity", SettingKind::kFloat, 0.2f, 20.0f, 0.1f, 3.0f, "#Settings_Sensitivity", "#Settings_SecMouse"},
	{"volume", SettingKind::kFloat, 0.0f, 1.0f, 0.05f, 0.7f, "#Settings_SfxVolume", "#Settings_SecAudio"},
	{"cl_crosshair", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_CrosshairEnable", "#Settings_SecBasic"},
	{"cl_crosshairsize", SettingKind::kFloat, 4.0f, 64.0f, 1.0f, 24.0f, "#Settings_CrosshairSize", "#Settings_SecBasic"},
	{"hud_scoreboard_showavatars", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_ScoreboardAvatars", "#Settings_SecBasic"},
	// ---- T13 audio (hw.dll cvar table entries; GameUI COptionsSubAudio
	//      ctor 0x10017040 fixes the 0..1 domains and the hisound combo) ----
	{"suitvolume", SettingKind::kFloat, 0.0f, 1.0f, 0.05f, 0.25f, "#Settings_SuitVolume", "#Settings_SecAudio"},
	{"mp3volume", SettingKind::kFloat, 0.0f, 1.0f, 0.05f, 0.8f, "#Settings_Mp3Volume", "#Settings_SecAudio"},
	{"hisound", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_Hisound", "#Settings_SecAudio"},
	// ---- T13 instant video rows (T12 handover: evidence gathered there,
	//      domain from COptionsSubVideo ctor FUN_1001cca0) ----
	{"gl_vsync", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_VSync", "#Settings_SecVideo"},
	{"brightness", SettingKind::kFloat, 0.0f, 2.0f, 0.05f, 1.0f, "#Settings_Brightness", "#Settings_SecVideo"},
	{"gamma", SettingKind::kFloat, 1.8f, 3.0f, 0.05f, 2.5f, "#Settings_Gamma", "#Settings_SecVideo"},
	{"r_detailtextures", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_DetailTextures", "#Settings_SecVideo"},
	// ---- T13 mouse remainder (COptionsSubMouse ctor 0x10019eb0 control
	//      set; registrations in client.dll FUN_1003a960 / FUN_1003c640 and
	//      the hw.dll cvar table) ----
	{"m_pitch", SettingKind::kNegate, -20.0f, 20.0f, 0.0f, 0.022f, "#Settings_ReverseMouse", "#Settings_SecMouse"},
	{"m_filter", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_MouseFilter", "#Settings_SecMouse"},
	{"joystick", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_Joystick", "#Settings_SecMouse"},
	{"in_mlook", SettingKind::kCommandToggle, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_MouseLook", "#Settings_SecMouse",
		nullptr, 0, "in_mlook", nullptr, "+mlook", "-mlook"},
	{"in_jlook", SettingKind::kCommandToggle, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_JoystickLook", "#Settings_SecMouse",
		nullptr, 0, "in_jlook", nullptr, "+jlook", "-jlook"},
	{"m_rawinput", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_RawInput", "#Settings_SecMouse"},
	{"sv_aim", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_AutoAim", "#Settings_SecMouse"},
	// ---- T13 voice cvars (voice_scale: hw table "1" archive, GameUI voice
	//      page slider 0..1; voice_modenable: client.dll FUN_1009dce0
	//      registers "1" archive). The mic volume/boost/test hardware paths
	//      live in VoiceModel, not here. ----
	{"voice_scale", SettingKind::kFloat, 0.0f, 1.0f, 0.05f, 1.0f, "#Settings_VoiceReceive", "#Settings_SecVoice"},
	{"voice_modenable", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_VoiceEnable", "#Settings_SecVoice"},
	// ---- T13 multiplayer (hw.dll table defaults + flags; GameUI
	//      COptionsSubMultiplayer ctor FUN_1001a680 domains: colors 0..255) --
	{"name", SettingKind::kText, 0.0f, 0.0f, 0.0f, 0.0f, "#Settings_PlayerName", "#Settings_SecMultiplayer",
		nullptr, 0, nullptr, "New Player"},
	{"model", SettingKind::kStringEnum, 0.0f, 0.0f, 0.0f, 0.0f, "#Settings_PlayerModel", "#Settings_SecMultiplayer",
		nullptr, 0, "models/player", ""},
	{"topcolor", SettingKind::kFloat, 0.0f, 255.0f, 1.0f, 0.0f, "#Settings_PrimaryColor", "#Settings_SecMultiplayer"},
	{"bottomcolor", SettingKind::kFloat, 0.0f, 255.0f, 1.0f, 0.0f, "#Settings_SecondaryColor", "#Settings_SecMultiplayer"},
	{"cl_logofile", SettingKind::kStringEnum, 0.0f, 0.0f, 0.0f, 0.0f, "#Settings_Spraypaint", "#Settings_SecMultiplayer",
		nullptr, 0, "logos", "lambda"},
	{"cl_logocolor", SettingKind::kStringEnum, 0.0f, 0.0f, 0.0f, 0.0f, "#Settings_SpraypaintColor", "#Settings_SecMultiplayer",
		kLogoColorOptions, 9, "logocolor", "Ltblue"},
};

// The advanced rows the original MultiplayerAdvancedDialog parses from the
// shipped user.scr (CInfoDescription). The static rows here mirror that
// script exactly (prompt -> lang token); parseUserScr() re-reads the live
// file at init and OVERRIDES these with the mod's current script (a mod can
// ship its own), so a modded script keeps working.
inline constexpr SettingRow AdvancedScriptRows[] = {
	{"crosshair", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_AdvCrosshair", "#Settings_SecAdvanced"},
	{"hud_weaponautoswitch", SettingKind::kEnum, 0.0f, 2.0f, 1.0f, 1.0f, "#Settings_AdvAutoSwitch", "#Settings_SecAdvanced", kWeaponAutoSwitchOptions, 3},
	{"cl_showfps", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvShowFps", "#Settings_SecAdvanced"},
	{"hud_saytext", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_AdvSayText", "#Settings_SecAdvanced"},
	{"hud_saytext_time", SettingKind::kFloat, 0.0f, 15.0f, 1.0f, 5.0f, "#Settings_AdvSayTextTime", "#Settings_SecAdvanced"},
	{"hud_saytext_lines", SettingKind::kFloat, 0.0f, 20.0f, 1.0f, 6.0f, "#Settings_AdvSayTextLines", "#Settings_SecAdvanced"},
	{"hud_saytext_block_muted", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvSayTextBlock", "#Settings_SecAdvanced"},
	{"cl_hidextra", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvHideExtras", "#Settings_SecAdvanced"},
	{"cl_hideadmin", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvHideAdmin", "#Settings_SecAdvanced"},
	{"r_borderless", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvBorderless", "#Settings_SecAdvanced"},
	{"hpk_maxsize", SettingKind::kEnum, -1.0f, 16.0f, 1.0f, 4.0f, "#Settings_AdvHpkMaxsize", "#Settings_SecAdvanced", kHpkMaxsizeOptions, 4},
	{"mp_decals", SettingKind::kFloat, 1.0f, 4096.0f, 1.0f, 4096.0f, "#Settings_AdvDecals", "#Settings_SecAdvanced"},
	{"cl_particles_max", SettingKind::kFloat, 0.0f, 4096.0f, 1.0f, 800.0f, "#Settings_AdvParticles", "#Settings_SecAdvanced"},
	{"cl_bob", SettingKind::kEnum, 0.0f, 0.025f, 0.005f, 0.025f, "#Settings_AdvBob", "#Settings_SecAdvanced", kBobOptions, 3},
	{"cl_rollangle", SettingKind::kEnum, -5.0f, 5.0f, 0.5f, 2.0f, "#Settings_AdvRollAngle", "#Settings_SecAdvanced", kRollAngleOptions, 5},
	{"s_mute_unfocused", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_AdvMuteUnfocused", "#Settings_SecAdvanced"},
	{"s_force_dsound", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvForceDSound", "#Settings_SecAdvanced"},
	{"violence_ablood", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvAlienBlood", "#Settings_SecAdvanced"},
	{"violence_agibs", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvAlienGibs", "#Settings_SecAdvanced"},
	{"violence_hblood", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvHumanBlood", "#Settings_SecAdvanced"},
	{"violence_hgibs", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 0.0f, "#Settings_AdvHumanGibs", "#Settings_SecAdvanced"},
	{"cl_discord", SettingKind::kEnum, 0.0f, 2.0f, 1.0f, 2.0f, "#Settings_AdvDiscord", "#Settings_SecAdvanced", kDiscordOptions, 3},
	{"con_mono", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_AdvMonoConsole", "#Settings_SecAdvanced"},
	{"developer", SettingKind::kEnum, 0.0f, 4.0f, 1.0f, 0.0f, "#Settings_AdvDeveloper", "#Settings_SecAdvanced", kDeveloperOptions, 5},
};

constexpr int FirstBatchCount = static_cast<int>(sizeof(FirstBatchRows) / sizeof(FirstBatchRows[0]));
constexpr int AdvancedScriptCount = static_cast<int>(sizeof(AdvancedScriptRows) / sizeof(AdvancedScriptRows[0]));

// Per-row lifecycle. kClean -> (edit) -> kDirty -> (apply) -> kClean, with
// the error branches never silently losing the user's input. kMissing is the
// fail-closed state for a cvar the engine never registered. kQueued is the
// kCommandToggle post-apply state: the command is in the engine buffer, the
// state read-back has not caught up yet.
enum class RowState {
	kClean,       // draft == engine snapshot; nothing to apply
	kDirty,       // user edit pending (not yet applied)
	kInvalid,     // last committed draft text is not a value in the domain
	kConflict,    // pending edit + the engine value changed behind the page
	kQueued,      // command accepted, engine buffer not flushed yet
	kApplyFailed, // apply wrote nothing effective for this row; draft kept
	kMissing,     // cvar not registered: row unavailable, never editable
};

enum class ApplyOutcome {
	kApplied,
	kQueued,           // command channel write; settles via snapshot()
	kWriteFailed,      // backend refused the write
	kReadbackMismatch, // write accepted, engine does not report the value
	kInvalidRefused,   // not written: the draft text never parsed into domain
};

struct ApplyResult {
	std::string cvar;
	ApplyOutcome outcome = ApplyOutcome::kApplied;
};

// Read-only projection of one row for a page render pass.
struct RowView {
	const SettingRow* row = nullptr;
	float engineValue = 0.0f;
	std::string engineText;  // kText/kStringEnum: the engine's stored string
	std::string draftText;   // committed field text (valid or not)
	float draftValue = 0.0f; // meaningful when state != kInvalid
	RowState state = RowState::kClean;
	bool missing = false;    // cvar not registered: row unavailable
	const std::vector<std::string>* options = nullptr; // kStringEnum scan
};

class SettingsModel {
public:
	// Backend seams - the production wiring injects the abc::CvarCmd
	// wrappers; tests inject an in-memory fake. Function pointers, no
	// captures (EngineUiBackend's Seams shape).
	struct Seams {
		bool (*exists)(const char* name) = nullptr;
		float (*readFloat)(const char* name) = nullptr;
		// Must be a real cvar write (engine-side callbacks and persistence
		// ride along); returns false when the write did not happen.
		bool (*writeFloat)(const char* name, float value) = nullptr;
		// T13 string rows: real engine cvar string write + read (never a
		// command); readString returns null when the cvar is missing.
		bool (*writeString)(const char* name, const char* value) = nullptr;
		const char* (*readString)(const char* name) = nullptr;
		// T13 kCommandToggle: IGameUIFuncs::IsKeyDown and the engine command
		// channel (buffered - the model treats the effect as deferred).
		bool (*isKeyDown)(const char* stateName) = nullptr;
		bool (*clientCmd)(const char* command) = nullptr;
	};

	SettingsModel(const SettingRow* rows, int count);
	SettingsModel() = default;

	// Replaces the row table (module init: FirstBatch + T13 static advanced
	// rows + parsed user.scr overrides). Resets all runtime state - call
	// before the first snapshot.
	void setTable(const SettingRow* rows, int count);

	void setSeams(const Seams& seams) { Seams_ = seams; }

	// kStringEnum rows: installs the scanned option set (model/logo lists
	// from the module's directory scan). Values already equal to the engine
	// value stay acceptable even when not in the list (a downloaded model
	// must not be force-changed by opening the page).
	void setStringOptions(const char* cvar, std::vector<std::string> options);

	// Re-reads engine truth. Untouched rows adopt the new value silently;
	// rows with a pending user edit keep the draft and flip to kConflict
	// (the page prompts - it never quietly overwrites). Missing rows stay
	// missing: an unregistered cvar is reported, not guessed. kQueued rows
	// resolve here: the flushed command state matches -> kClean, still not
	// after kQueuedTicksLimit snapshots -> kApplyFailed.
	void snapshot();

	// Commits one field edit. Parses the full text as a finite float and
	// validates the domain: on success the row turns kDirty (kConflict if
	// the engine moved meanwhile - snapshot() flags first), on failure the
	// text is kept for display and the row turns kInvalid. Returns success.
	bool setDraftText(const char* cvar, const char* text);
	// Toggle path for kBool rows (always in domain); also drives kNegate
	// (sign flip over the current draft magnitude) and kCommandToggle
	// (draft target state; command written on apply).
	bool setDraftBool(const char* cvar, bool value);
	// kEnum rows: value must be one of the row's Options.
	bool setDraftEnum(const char* cvar, float value);
	// kText / kStringEnum rows: validated text (printable, no quote or
	// control bytes, length cap; enum options must be known). Returns false
	// and keeps the text for display (kInvalid) on rejection.
	bool setDraftString(const char* cvar, const char* text);

	// Conflict resolution: drop the pending edit and take the engine value.
	bool adoptEngineValue(const char* cvar);

	// Drafts := verified table defaults (rows whose engine value already
	// equals the default turn clean). Confirmation is the page's job; the
	// write still waits for apply().
	void resetDefaults();

	// Discards every draft and error flag. Provably writes nothing.
	void cancel();

	// Sequential per-row write + read-back over the seams. Clean and missing
	// rows are skipped; invalid drafts are refused without touching the
	// backend. Partial failure keeps the failed rows at kApplyFailed (still
	// dirty); applied rows re-snapshot to the written value. kCommandToggle
	// rows land kQueued (command channel, settles on the frame beat).
	std::vector<ApplyResult> apply();

	std::vector<RowView> rows() const;
	const RowView* find(const char* cvar) const;

	// True while anything would change (or failed to change) on Apply:
	// dirty, invalid, conflict, queued or apply-failed rows exist.
	bool isDirty() const;
	bool hasConflicts() const;
	// True while a kCommandToggle write is still settling (the module's
	// per-frame beat re-snapshots until it resolves).
	bool hasQueued() const;

	// user.scr parser (the original CInfoDescription grammar): fills the
	// owning table with one row per DESCRIPTION entry (BOOL / NUMBER min max
	// / STRING / LIST "label" "value" ...). Malformed entries are skipped;
	// a table with zero rows reports false (caller keeps the static
	// AdvancedScriptRows fallback). The arena strings outlive the model.
	struct UserScrTable {
		std::vector<SettingRow> Rows;
		std::vector<std::vector<SettingEnumOption>> Options;
		// Arena for the cvar/prompt/default strings the rows point into. A
		// deque, NOT a vector: push_back never moves existing elements, so
		// the pointers already stored in Rows cannot dangle (vector
		// reallocation moves SSO string objects and the stored c_str()
		// pointers with them - caught by the multi-entry parser test).
		std::deque<std::string> Strings;
	};
	static bool parseUserScr(const char* text, UserScrTable& out);

private:
	struct RowRuntime {
		const SettingRow* row = nullptr;
		float engineValue = 0.0f;
		std::string engineText;
		std::string draftText;
		float draftValue = 0.0f;
		std::vector<std::string> options; // kStringEnum scan options
		int queuedTicks = 0;
		// Pre-snapshot placeholder: the first snapshot() runs the kClean
		// adoption and lands every registered row on the engine truth.
		RowState state = RowState::kClean;
		bool missing = true;
	};

	RowRuntime* runtime(const char* cvar);
	const RowRuntime* runtime(const char* cvar) const;
	void adoptEngineLocked(RowRuntime& target, float value);
	void adoptEngineTextLocked(RowRuntime& target, const std::string& text);
	static std::string formatValue(float value);
	static bool parseValue(const char* text, float& out);
	// kText/kStringEnum write validation (T13 boundary): printable bytes
	// only, no quote/backslash/control characters, kText capped at 63 bytes
	// (the original RES maxchars), enum tokens limited to the option set.
	bool validateString(const RowRuntime& entry, const char* text) const;
	float engineTruthLocked(const RowRuntime& entry) const;

	Seams Seams_;
	std::vector<RowRuntime> Rows_;
};

// kText byte budget: the shipped OptionsSubMultiplayer.res NameEntry
// maxchars=63 (the original page caps at the same count; the engine
// PRINTABLEONLY flag handles the rest).
constexpr size_t kNameMaxBytes = 63;
// kQueued settle budget: the engine command buffer flushes within a frame;
// two snapshot beats without a matching state read mean the command did not
// take (reported as kApplyFailed instead of spinning forever).
constexpr int kQueuedTicksLimit = 2;

}
}
