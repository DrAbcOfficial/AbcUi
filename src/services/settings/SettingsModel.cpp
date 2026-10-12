// services::settings implementation - see SettingsModel.h. No engine or Rml
// dependency: every backend touchpoint rides the injected seams, so the
// production model links into the offline acceptance unchanged.
#include "SettingsModel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace services {
namespace settings {

namespace {

// Engine floats round-trip through the cvar storage; the epsilon only guards
// against representation noise, not real changes.
constexpr float kValueEpsilon = 1e-6f;

bool sameValue(float a, float b) {
	return std::fabs(a - b) <= kValueEpsilon;
}

}

SettingsModel::SettingsModel(const SettingRow* rows, int count) {
	Rows_.reserve(count > 0 ? static_cast<size_t>(count) : 0);
	for (int i = 0; i < count; i++) {
		RowRuntime runtime;
		runtime.row = &rows[i];
		Rows_.push_back(std::move(runtime));
	}
}

SettingsModel::RowRuntime* SettingsModel::runtime(const char* cvar) {
	if (!cvar)
		return nullptr;
	for (auto& entry : Rows_) {
		if (std::strcmp(entry.row->Cvar, cvar) == 0)
			return &entry;
	}
	return nullptr;
}

const SettingsModel::RowRuntime* SettingsModel::runtime(const char* cvar) const {
	if (!cvar)
		return nullptr;
	for (const auto& entry : Rows_) {
		if (std::strcmp(entry.row->Cvar, cvar) == 0)
			return &entry;
	}
	return nullptr;
}

std::string SettingsModel::formatValue(float value) {
	// %g keeps the table's values readable ("3", "0.7", "20") without
	// decimal trails; %.1f would invent precision the domain does not carry.
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
	return buffer;
}

bool SettingsModel::parseValue(const char* text, float& out) {
	if (!text)
		return false;
	// Full-consumption strtod: "3abc" and "" are input errors, not 3.
	std::string trimmed = text;
	const auto first = trimmed.find_first_not_of(" \t");
	if (first == std::string::npos)
		return false;
	const auto last = trimmed.find_last_not_of(" \t");
	trimmed = trimmed.substr(first, last - first + 1);
	char* end = nullptr;
	const double parsed = std::strtod(trimmed.c_str(), &end);
	if (end != trimmed.c_str() + trimmed.size())
		return false;
	if (!std::isfinite(parsed))
		return false;
	out = static_cast<float>(parsed);
	return true;
}

void SettingsModel::adoptEngineLocked(RowRuntime& target, float value) {
	target.engineValue = value;
	target.draftValue = value;
	target.draftText = formatValue(value);
	target.state = RowState::kClean;
}

void SettingsModel::snapshot() {
	for (auto& entry : Rows_) {
		entry.missing = !Seams_.exists || !Seams_.exists(entry.row->Cvar);
		if (entry.missing) {
			entry.state = RowState::kMissing;
			continue;
		}
		const float engine = Seams_.readFloat ? Seams_.readFloat(entry.row->Cvar) : 0.0f;
		const bool engineChanged = !sameValue(engine, entry.engineValue);
		entry.engineValue = engine;
		switch (entry.state) {
		case RowState::kDirty:
		case RowState::kConflict:
			// Pending user edit: keep the draft; an outside change surfaces
			// as kConflict, an outside revert downgrades back to plain dirty.
			entry.state = engineChanged ? RowState::kConflict : RowState::kDirty;
			break;
		case RowState::kApplyFailed:
			// The read-back disagreed at apply time; the fresh read decides:
			// the value may have landed after all, an outside change makes the
			// conflict real, otherwise the edit is simply pending again.
			if (sameValue(engine, entry.draftValue))
				adoptEngineLocked(entry, engine);
			else
				entry.state = engineChanged ? RowState::kConflict : RowState::kDirty;
			break;
		case RowState::kClean:
		case RowState::kInvalid:
			// kClean: the untouched field follows the engine silently.
			// kInvalid: the committed text never parsed - the engine read
			// refreshes, the user still owes a valid value.
			if (entry.state == RowState::kClean)
				adoptEngineLocked(entry, engine);
			break;
		case RowState::kMissing:
			break; // handled above
		}
	}
}

bool SettingsModel::setDraftText(const char* cvar, const char* text) {
	RowRuntime* entry = runtime(cvar);
	if (!entry || entry->missing)
		return false;
	float parsed = 0.0f;
	if (!parseValue(text, parsed) || parsed < entry->row->Min || parsed > entry->row->Max) {
		entry->draftText = text ? text : "";
		entry->state = RowState::kInvalid;
		return false;
	}
	entry->draftText = text;
	entry->draftValue = parsed;
	if (sameValue(parsed, entry->engineValue)) {
		entry->state = RowState::kClean; // typed back to the live value
	} else if (entry->state != RowState::kConflict) {
		entry->state = RowState::kDirty;
	}
	return true;
}

bool SettingsModel::setDraftBool(const char* cvar, bool value) {
	RowRuntime* entry = runtime(cvar);
	if (!entry || entry->missing || entry->row->Kind != SettingKind::kBool)
		return false;
	const float next = value ? 1.0f : 0.0f;
	entry->draftText = value ? "1" : "0";
	entry->draftValue = next;
	if (sameValue(next, entry->engineValue)) {
		entry->state = RowState::kClean;
	} else if (entry->state != RowState::kConflict) {
		entry->state = RowState::kDirty;
	}
	return true;
}

bool SettingsModel::adoptEngineValue(const char* cvar) {
	RowRuntime* entry = runtime(cvar);
	if (!entry || entry->missing)
		return false;
	adoptEngineLocked(*entry, entry->engineValue);
	return true;
}

void SettingsModel::resetDefaults() {
	for (auto& entry : Rows_) {
		if (entry.missing)
			continue;
		const float fallback = entry.row->DefaultValue;
		entry.draftText = formatValue(fallback);
		entry.draftValue = fallback;
		entry.state = sameValue(fallback, entry.engineValue) ? RowState::kClean : RowState::kDirty;
	}
}

void SettingsModel::cancel() {
	for (auto& entry : Rows_) {
		if (entry.missing)
			continue;
		adoptEngineLocked(entry, entry.engineValue);
	}
}

std::vector<ApplyResult> SettingsModel::apply() {
	std::vector<ApplyResult> results;
	for (auto& entry : Rows_) {
		if (entry.missing || entry.state == RowState::kClean)
			continue;
		if (entry.state == RowState::kInvalid) {
			results.push_back({entry.row->Cvar, ApplyOutcome::kInvalidRefused});
			continue;
		}
		ApplyResult result{entry.row->Cvar, ApplyOutcome::kApplied};
		if (!Seams_.writeFloat || !Seams_.writeFloat(entry.row->Cvar, entry.draftValue)) {
			result.outcome = ApplyOutcome::kWriteFailed;
		} else {
			const float readback = Seams_.readFloat ? Seams_.readFloat(entry.row->Cvar) : entry.draftValue;
			if (!sameValue(readback, entry.draftValue))
				result.outcome = ApplyOutcome::kReadbackMismatch;
		}
		if (result.outcome == ApplyOutcome::kApplied) {
			adoptEngineLocked(entry, entry.draftValue);
		} else {
			// The engine state is unknown/mixed here: keep the draft and the
			// dirty flag so the row stays visible and retryable. The next
			// snapshot() decides conflict-vs-dirty from the real engine.
			entry.state = RowState::kApplyFailed;
		}
		results.push_back(std::move(result));
	}
	return results;
}

std::vector<RowView> SettingsModel::rows() const {
	std::vector<RowView> views;
	views.reserve(Rows_.size());
	for (const auto& entry : Rows_) {
		RowView view;
		view.row = entry.row;
		view.engineValue = entry.engineValue;
		view.draftText = entry.draftText;
		view.draftValue = entry.draftValue;
		view.state = entry.state;
		view.missing = entry.missing;
		views.push_back(std::move(view));
	}
	return views;
}

const RowView* SettingsModel::find(const char* cvar) const {
	// Convenience single-row lookup over the same projection.
	static thread_local RowView view;
	const RowRuntime* entry = runtime(cvar);
	if (!entry)
		return nullptr;
	view.row = entry->row;
	view.engineValue = entry->engineValue;
	view.draftText = entry->draftText;
	view.draftValue = entry->draftValue;
	view.state = entry->state;
	view.missing = entry->missing;
	return &view;
}

bool SettingsModel::isDirty() const {
	for (const auto& entry : Rows_) {
		if (entry.missing)
			continue;
		switch (entry.state) {
		case RowState::kDirty:
		case RowState::kInvalid:
		case RowState::kConflict:
		case RowState::kApplyFailed:
			return true;
		case RowState::kClean:
		case RowState::kMissing:
			break;
		}
	}
	return false;
}

bool SettingsModel::hasConflicts() const {
	for (const auto& entry : Rows_) {
		if (!entry.missing && entry.state == RowState::kConflict)
			return true;
	}
	return false;
}

}
}
