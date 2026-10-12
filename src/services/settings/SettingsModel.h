#pragma once

// services::settings - the settings description table and the draft
// transaction model (T10).
//
// The description table is the single source of truth for what the settings
// page offers. Every row cites a REAL, verified backend registration - never
// a name guessed from a resource label (settings-inventory.md boundary):
//   * engine cvars carry the registration evidence from the shipped binaries
//     (default string + flags read out of the registering module, UI domain
//     read out of the shipped GameUI page constructors),
//   * AbcUi cvars cite our own CREATE_CVAR registration (the source IS the
//     default source).
// Rows whose backend cannot be verified stay OUT of the table (T13 re-adds
// them with evidence); a row whose cvar is not registered at runtime renders
// as unavailable instead of silently no-oping.
//
// Draft transaction: the page never writes a cvar outside Apply. snapshot()
// reads engine truth; edits land in per-row drafts; apply() writes through
// the injected seams (abc::CVAR_SET_FLOAT in production - the engine cvar
// API, never a concatenated "name value" command) and reads back; cancel()
// discards drafts and provably writes nothing.
//
// External-change rule (V10): snapshot() refreshes rows the user has not
// touched and flags rows it HAS touched as kConflict instead of quietly
// overwriting the draft. Apply is a sequence of independent per-cvar writes -
// the engine exposes no cross-cvar atomic transaction, so a partial failure
// keeps the failed rows dirty with per-row outcomes and never pretends the
// batch committed.
//
// No engine/Rml includes: the exact production model links into offline
// tests with a fake backend (05 doc evidence tier 1/2). First-batch rows are
// all FCVAR_ARCHIVE cvars, so persistence stays engine-side (config.cfg);
// T10 introduces no second (JSON) truth for them.

#include <string>
#include <vector>

namespace services {
namespace settings {

enum class SettingKind {
	kBool,   // domain {0, 1}; toggle control
	kFloat,  // [Min, Max] numeric entry
};

// One description-table row. All pointers are static literals.
struct SettingRow {
	const char* Cvar;        // engine-registered name, referenced verbatim
	SettingKind Kind;
	float Min;               // UI domain (kBool: 0)
	float Max;               // UI domain (kBool: 1)
	float Step;              // slider stride hint (0 = free numeric entry)
	float DefaultValue;      // verified registration default
	const char* LabelToken;  // "#Settings_*" lang token
};

// The T10 first batch (task step 4: mouse sensitivity, one volume item, a
// few existing AbcUi bool/number items). Verification per row:
//
// - sensitivity: client.dll FUN_1003c640 registers
//   pfnRegisterVariable("sensitivity", "3", FCVAR_ARCHIVE) - default "3".
//   UI domain 0.2..20.0 from the shipped GameUI COptionsSubMouse ctor
//   (GameUI.dll 0x10019eb0: CCvarSlider("Slider", ..., 0.2, 20.0,
//   "sensitivity", 1)); the original page shows one decimal (" %.1f").
// - volume: hw.dll cvar registration table entry
//   (name "volume" @0x01e6a4f8, default "0.7", flags FCVAR_ARCHIVE).
//   UI domain 0.0..1.0 from COptionsSubAudio ctor (GameUI.dll 0x10017040:
//   CCvarSlider("SFX Slider", ..., 0.0, 1.0, "volume", 0)).
// - cl_crosshair / cl_crosshairsize: AbcUi CrosshairPanel.cpp CREATE_CVAR
//   ("1" / "24"), abc::FCVAR_VALUE (archive). The size domain is AbcUi's own
//   choice; the panel consumes the raw value as a view-space length.
// - hud_scoreboard_showavatars: AbcUi ScoreboardPanel.cpp CREATE_CVAR ("1"),
//   abc::FCVAR_VALUE; consumed as `value > 0` (bool).
//
// None of the first-batch rows needs a restart (all consumers read the cvar
// per frame or apply it immediately engine-side); RestartRequired stays a
// table column for the T12 video rows.
inline constexpr SettingRow FirstBatchRows[] = {
	{"sensitivity", SettingKind::kFloat, 0.2f, 20.0f, 0.1f, 3.0f, "#Settings_Sensitivity"},
	{"volume", SettingKind::kFloat, 0.0f, 1.0f, 0.05f, 0.7f, "#Settings_SfxVolume"},
	{"cl_crosshair", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_CrosshairEnable"},
	{"cl_crosshairsize", SettingKind::kFloat, 4.0f, 64.0f, 1.0f, 24.0f, "#Settings_CrosshairSize"},
	{"hud_scoreboard_showavatars", SettingKind::kBool, 0.0f, 1.0f, 1.0f, 1.0f, "#Settings_ScoreboardAvatars"},
};

constexpr int FirstBatchCount = static_cast<int>(sizeof(FirstBatchRows) / sizeof(FirstBatchRows[0]));

// Per-row lifecycle. kClean -> (edit) -> kDirty -> (apply) -> kClean, with
// the error branches never silently losing the user's input. kMissing is the
// fail-closed state for a cvar the engine never registered.
enum class RowState {
	kClean,       // draft == engine snapshot; nothing to apply
	kDirty,       // user edit pending (not yet applied)
	kInvalid,     // last committed draft text is not a value in the domain
	kConflict,    // pending edit + the engine value changed behind the page
	kApplyFailed, // apply wrote nothing effective for this row; draft kept
	kMissing,     // cvar not registered: row unavailable, never editable
};

enum class ApplyOutcome {
	kApplied,
	kWriteFailed,     // backend refused the write
	kReadbackMismatch, // write accepted, engine does not report the value
	kInvalidRefused,  // not written: the draft text never parsed into domain
};

struct ApplyResult {
	std::string cvar;
	ApplyOutcome outcome = ApplyOutcome::kApplied;
};

// Read-only projection of one row for a page render pass.
struct RowView {
	const SettingRow* row = nullptr;
	float engineValue = 0.0f;
	std::string draftText;   // committed field text (valid or not)
	float draftValue = 0.0f; // meaningful when state != kInvalid
	RowState state = RowState::kClean;
	bool missing = false;    // cvar not registered: row unavailable
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
	};

	SettingsModel(const SettingRow* rows, int count);

	void setSeams(const Seams& seams) { Seams_ = seams; }

	// Re-reads engine truth. Untouched rows adopt the new value silently;
	// rows with a pending user edit keep the draft and flip to kConflict
	// (the page prompts - it never quietly overwrites). Missing rows stay
	// missing: an unregistered cvar is reported, not guessed.
	void snapshot();

	// Commits one field edit. Parses the full text as a finite float and
	// validates the domain: on success the row turns kDirty (kConflict if
	// the engine moved meanwhile - snapshot() flags first), on failure the
	// text is kept for display and the row turns kInvalid. Returns success.
	bool setDraftText(const char* cvar, const char* text);
	// Toggle path for kBool rows (always in domain).
	bool setDraftBool(const char* cvar, bool value);

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
	// dirty); applied rows re-snapshot to the written value.
	std::vector<ApplyResult> apply();

	std::vector<RowView> rows() const;
	const RowView* find(const char* cvar) const;

	// True while anything would change (or failed to change) on Apply:
	// dirty, invalid, conflict or apply-failed rows exist.
	bool isDirty() const;
	bool hasConflicts() const;

private:
	struct RowRuntime {
		const SettingRow* row = nullptr;
		float engineValue = 0.0f;
		std::string draftText;
		float draftValue = 0.0f;
		// Pre-snapshot placeholder: the first snapshot() runs the kClean
		// adoption and lands every registered row on the engine truth.
		RowState state = RowState::kClean;
		bool missing = true;
	};

	RowRuntime* runtime(const char* cvar);
	const RowRuntime* runtime(const char* cvar) const;
	void adoptEngineLocked(RowRuntime& target, float value);
	static std::string formatValue(float value);
	static bool parseValue(const char* text, float& out);

	Seams Seams_;
	std::vector<RowRuntime> Rows_;
};

}
}
