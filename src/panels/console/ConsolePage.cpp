// panels::console::ConsolePage implementation - see ConsolePage.h.
#include "ConsolePage.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/StringUtilities.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../services/console/ConsoleBackend.h"
#include "../../services/console/ConsoleBuffer.h"

#include <algorithm>

namespace panels::console {

namespace {

constexpr const char* kActionSubmit = "console-submit";
constexpr const char* kActionConfirmExecute = "console-confirm-execute";
constexpr const char* kActionConfirmCancel = "console-confirm-cancel";
constexpr const char* kActionClose = "console-close";

Rml::Element* actionNode(Rml::Element* node) {
	for (Rml::Element* it = node; it; it = it->GetParentNode()) {
		if (it->HasAttribute("data-action"))
			return it;
	}
	return nullptr;
}

void setText(Rml::ElementDocument* doc, const char* id, const std::string& text) {
	if (!doc)
		return;
	if (Rml::Element* node = doc->GetElementById(id))
		node->SetInnerRML(framework::TextUtil::escapeRml(text));
}

void setVisible(Rml::ElementDocument* doc, const char* id, bool visible) {
	if (!doc)
		return;
	if (Rml::Element* node = doc->GetElementById(id))
		node->SetProperty("display", visible ? "block" : "none");
}

void setControlValue(Rml::ElementDocument* doc, const char* id, const std::string& value) {
	if (!doc)
		return;
	if (auto* control = dynamic_cast<Rml::ElementFormControl*>(doc->GetElementById(id)))
		control->SetValue(value.c_str());
}

std::string controlValue(Rml::ElementDocument* doc, const char* id) {
	if (!doc)
		return {};
	if (auto* control = dynamic_cast<Rml::ElementFormControl*>(doc->GetElementById(id)))
		return control->GetValue().c_str();
	return {};
}

bool containsNewline(const std::string& text) {
	return text.find('\n') != std::string::npos || text.find('\r') != std::string::npos;
}

// Parks the editor's caret at the end of its text. The widget's selection
// API takes CHARACTER offsets and has no "end" sentinel (SetSelectionRange
// with a negative offset resolves to the first character), so the real
// character count is the value that means "behind the text" - the state
// typing leaves and the state a history recall must restore.
void parkCaretAtEnd(Rml::Element* editor) {
	auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(editor);
	if (!input)
		return;
	const Rml::String value = input->GetValue();
	const int end = static_cast<int>(
		Rml::StringUtilities::LengthUTF8(Rml::StringView(value.data(), value.data() + value.size())));
	input->SetSelectionRange(end, end);
}

// Splits a confirmed multi-line text into executable command lines: CR is
// dropped, empty/whitespace-only lines are skipped (the T15 backend refuses
// embedded newlines, so each line is its own validated submission).
std::vector<std::string> splitCommandLines(const std::string& text) {
	std::vector<std::string> lines;
	std::string current;
	const auto flush = [&]() {
		std::size_t begin = 0;
		while (begin < current.size() && (current[begin] == ' ' || current[begin] == '\t'))
			++begin;
		std::size_t end = current.size();
		while (end > begin && (current[end - 1] == ' ' || current[end - 1] == '\t'))
			--end;
		if (begin < end)
			lines.push_back(current.substr(begin, end - begin));
		current.clear();
	};
	for (char c : text) {
		if (c == '\r')
			continue;
		if (c == '\n') {
			flush();
			continue;
		}
		current.push_back(c);
	}
	flush();
	return lines;
}

}

ConsolePage::ConsolePage()
	: InteractiveDocument("menuconsole", "abcui/rml/console.rml"), Listener(this), Keys(this) {}

ConsolePage::~ConsolePage() {
	close();
}

Rml::ElementDocument* ConsolePage::documentOrNull() const {
	return document();
}

void ConsolePage::onDocumentReady() {
	Rml::ElementDocument* doc = documentOrNull();
	if (doc) {
		addListener(doc, "click", &Listener);
		// Capture phase: the interaction keys are consumed before the editor
		// widget (and the document's own TAB navigation default) sees them.
		addListener(doc, "keydown", &Keys, true);
		// Live completion dropdown: the keyup that follows an edit re-reads
		// the editor and refreshes the list (the edit keydown is consumed
		// before the widget applies it, so the value is only current here).
		addListener(doc, "keyup", &Keys, true);
	}
	// A document (re)build re-seeds the projection from the buffer; the
	// buffer itself survives document rebuilds (T15 process-scoped cache).
	ProjectedSequence = 0;
	ProjectedGeneration = 0;
	PendingText.clear();
	Suggestions.clear();
	SuggestionIndex = -1;
	HistoryCursor = -1;
	HistoryDraft.clear();
	prepare();
}

void ConsolePage::prepare() {
	Rml::ElementDocument* doc = documentOrNull();
	setControlValue(doc, "console-input", "");
	PendingText.clear();
	Suggestions.clear();
	SuggestionIndex = -1;
	HistoryCursor = -1;
	HistoryDraft.clear();
	setVisible(doc, "console-confirm", false);
	hideCompletion();
	// A fresh open reseeds the projection: the tail window of whatever the
	// buffer holds now (the reader sees current state, not an arbitrary
	// incremental slice from a previous session).
	ProjectedSequence = 0;
	ProjectedGeneration = 0;
	render();
	refocusDefault();
}

void ConsolePage::refocusDefault() {
	if (Rml::Element* editor = documentOrNull() ? documentOrNull()->GetElementById("console-input") : nullptr)
		editor->Focus();
}

// --- projection ------------------------------------------------------------

void ConsolePage::render() {
	Rml::ElementDocument* doc = documentOrNull();
	if (!doc || !Buffer)
		return;
	Rml::Element* history = doc->GetElementById("console-history");
	if (!history)
		return;

	const uint64_t generation = Buffer->generation();
	if (generation != ProjectedGeneration) {
		// Clear (or a rebuild): drop the whole view and start over from the
		// current tail window.
		ProjectedGeneration = generation;
		ProjectedSequence = 0;
		history->SetInnerRML("");
	}

	if (ProjectedSequence == 0) {
		// First render of this view: only the recent window (bounded), never
		// the full backlog - the cache is a tail by design.
		uint64_t since = 0;
		const uint64_t latest = Buffer->latestSequence();
		if (latest > static_cast<uint64_t>(kInitialLines))
			since = latest - static_cast<uint64_t>(kInitialLines);
		std::vector<services::console::ConsoleLine> lines;
		Buffer->copySince(since, lines, static_cast<std::size_t>(kInitialLines));
		const bool atBottom = true;  // a fresh view starts at the tail
		for (const auto& line : lines)
			appendLine(line.Text, line.Developer, line.Truncated);
		ProjectedSequence = Buffer->latestSequence();
		trimDom();
		if (atBottom)
			history->SetScrollTop(history->GetScrollHeight());
		updateCountLine();
		return;
	}

	const uint64_t latest = Buffer->latestSequence();
	if (latest <= ProjectedSequence) {
		// The pending tail follow (see the append branch): the metrics are
		// current now, so the one-pass-behind scroll can land exactly at the
		// bottom - unless the reader scrolled up since it was armed.
		if (FollowPending) {
			FollowPending = false;
			if (history->GetScrollTop() + 1.0f >= FollowFromScrollTop)
				history->SetScrollTop(history->GetScrollHeight());
		}
		updateCountLine();
		return;  // no new lines
	}

	// Incremental append: only lines newer than the projected cursor.
	const float scrollTopBefore = history->GetScrollTop();
	const float scrollHeightBefore = history->GetScrollHeight();
	const float clientHeight = history->GetClientHeight();
	const bool atBottom = scrollTopBefore + clientHeight >= scrollHeightBefore - 1.0f;

	std::vector<services::console::ConsoleLine> lines;
	// The consumer window is bounded too: a long gap (the page was hidden
	// while output burst) pages from the newest tail, never from an
	// unbounded backlog.
	Buffer->copySince(ProjectedSequence, lines, static_cast<std::size_t>(kMaxDomLines));
	for (const auto& line : lines)
		appendLine(line.Text, line.Developer, line.Truncated);
	ProjectedSequence = Buffer->latestSequence();
	trimDom();
	if (atBottom) {
		// The scroll set below lands on the CURRENT layout only when the
		// context already laid the appended rows out. Live frames render
		// before Context::Update, so the bounds here are one layout pass
		// behind: the follow is re-armed and re-applied on the next render
		// (one frame later, imperceptible) against fresh metrics. Without
		// that second pass the pane stays exactly one append above the
		// bottom and then stops following entirely - the live tail froze.
		history->SetScrollTop(history->GetScrollHeight());
		FollowPending = true;
		FollowFromScrollTop = scrollTopBefore;
	} else {
		FollowPending = false;
		// The reader is parked above the bottom: keep the visible content
		// stable. Trimming the oldest rows shrinks the scroll height; the
		// scroll top compensates by the same amount.
		float delta = scrollHeightBefore - history->GetScrollHeight();
		float target = scrollTopBefore - delta;
		if (target < 0.0f)
			target = 0.0f;
		history->SetScrollTop(target);
	}
	updateCountLine();
}

void ConsolePage::appendLine(const std::string& text, bool developer, bool truncated) {
	Rml::ElementDocument* doc = documentOrNull();
	Rml::Element* history = doc ? doc->GetElementById("console-history") : nullptr;
	if (!history)
		return;
	Rml::ElementPtr row = doc->CreateElement("div");
	if (!row)
		return;
	row->SetClass("console-line", true);
	if (developer)
		row->SetClass("console-dev", true);
	// Output is always text: escaped, never parsed as markup.
	row->SetInnerRML(framework::TextUtil::escapeRml(text + (truncated ? " …" : "")));
	history->AppendChild(std::move(row));
}

int ConsolePage::trimDom() {
	Rml::ElementDocument* doc = documentOrNull();
	Rml::Element* history = doc ? doc->GetElementById("console-history") : nullptr;
	if (!history)
		return 0;
	int removed = 0;
	while (history->GetNumChildren() > kMaxDomLines) {
		Rml::Element* oldest = history->GetChild(0);
		if (!oldest)
			break;
		history->RemoveChild(oldest);
		++removed;
	}
	return removed;
}

void ConsolePage::updateCountLine() {
	Rml::ElementDocument* doc = documentOrNull();
	if (!doc || !Buffer)
		return;
	// "total: N" - the buffer's own line count (the DOM holds a capped
	// window of it). Plain text; the token resolves through the lang table.
	std::string label = "#Console_Count";
	framework::Localization::translate(label, "Console_Count");
	setText(doc, "console-count", label + " " + std::to_string(Buffer->lineCount()));
}

// --- interaction -----------------------------------------------------------

void ConsolePage::handleClick(Rml::Event& event) {
	Rml::Element* entry = actionNode(event.GetTargetElement());
	if (!entry)
		return;
	const Rml::String action = entry->GetAttribute<Rml::String>("data-action", "");
	Rml::ElementDocument* doc = documentOrNull();

	if (action == "console-close") {
		const ActionSink Callback = Sink;
		if (Callback)
			Callback(kActionClose);
		return;
	}
	if (action == "console-confirm-yes") {
		executeConfirmed();
		return;
	}
	if (action == "console-confirm-no") {
		// Cancel: nothing runs and the single-line editor never receives the
		// newline-bearing text (it could not hold it honestly). The paste is
		// dropped; the editor keeps whatever it had before the paste.
		closeConfirm();
		refocusDefault();
		const ActionSink Callback = Sink;
		if (Callback)
			Callback(kActionConfirmCancel);
		return;
	}
	if (action == "console-suggestion") {
		// Fixed data-index attribute identifies the row (never the label).
		const int index = entry->GetAttribute<int>("data-index", -1);
		applyCompletion(index);
		return;
	}
}

void ConsolePage::handleKey(Rml::Event& event) {
	Rml::ElementDocument* doc = documentOrNull();
	if (!doc)
		return;
	if (event.GetId() == Rml::EventId::Keyup) {
		// Live suggestions: navigation keys keep the list they already own,
		// the modal pane owns the keyboard, and an empty editor hides it.
		const int released = event.GetParameter<int>("key_identifier", 0);
		const bool navigation = released == Rml::Input::KI_UP || released == Rml::Input::KI_DOWN
			|| released == Rml::Input::KI_RETURN || released == Rml::Input::KI_NUMPADENTER
			|| released == Rml::Input::KI_TAB || released == Rml::Input::KI_ESCAPE;
		if (navigation || !PendingText.empty())
			return;
		refreshSuggestions();
		return;
	}
	const int key = event.GetParameter<int>("key_identifier", 0);
	const bool ctrl = event.GetParameter<int>("ctrl_key", 0) != 0;
	const bool confirmOpen = !PendingText.empty();

	if (key == Rml::Input::KI_ESCAPE) {
		if (confirmOpen) {
			// One key closes one layer: the pane first (the paste is
			// dropped, nothing runs). The module's owner action runs after
			// this feed; the claim stops it from closing the console in the
			// same physical press.
			event.StopPropagation();
			EscapeClaimed = true;
			closeConfirm();
			refocusDefault();
			return;
		}
		return;  // the overlay close is the owner's routed ESC, not ours
	}
	if (confirmOpen)
		return;  // the modal pane drives; the editor stays inert

	if ((key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) && !ctrl) {
		event.StopPropagation();
		submitFromEditor();
		return;
	}
	if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN) {
		const bool suggestionsShown = !Suggestions.empty();
		if (suggestionsShown) {
			// Walking the completion list while it is up.
			event.StopPropagation();
			SuggestionIndex += key == Rml::Input::KI_DOWN ? 1 : -1;
			if (SuggestionIndex < 0)
				SuggestionIndex = static_cast<int>(Suggestions.size()) - 1;
			if (SuggestionIndex >= static_cast<int>(Suggestions.size()))
				SuggestionIndex = 0;
			Rml::Element* list = doc->GetElementById("console-suggestions");
			if (list) {
				for (int i = 0; i < list->GetNumChildren(); ++i)
					list->GetChild(i)->SetClass("selected", i == SuggestionIndex);
			}
			return;
		}
		event.StopPropagation();
		walkHistory(key == Rml::Input::KI_UP ? 1 : -1);
		return;
	}
	if (key == Rml::Input::KI_TAB) {
		event.StopPropagation();  // never the document's focus navigation
		const std::string value = controlValue(doc, "console-input");
		if (!Suggestions.empty()) {
			const int index = SuggestionIndex < 0 ? 0 : SuggestionIndex;
			applyCompletion(index);
			return;
		}
		openCompletionFor(value);
		return;
	}
	if (key == Rml::Input::KI_V && ctrl) {
		// The multi-line paste gate: clipboard text bearing newlines is
		// NEVER handed to the editor (where it would merge into one line)
		// and NEVER executed silently. It opens the confirmation pane; only
		// its explicit Confirm submits one command per line.
		Rml::String clipboard;
		if (Rml::SystemInterface* systemInterface = Rml::GetSystemInterface())
			systemInterface->GetClipboardText(clipboard);
		const std::string text = clipboard.c_str();
		if (containsNewline(text) && !splitCommandLines(text).empty()) {
			event.StopPropagation();
			openConfirm(text, true);
		}
		return;  // single-line paste is the widget's own Ctrl+V
	}
}

void ConsolePage::submitFromEditor() {
	Rml::ElementDocument* doc = documentOrNull();
	if (!doc)
		return;
	const std::string text = controlValue(doc, "console-input");
	std::string trimmed = text;
	while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
		trimmed.pop_back();
	if (trimmed.empty())
		return;  // empty submissions are not typos worth clearing
	if (containsNewline(text)) {
		// Defense in depth (the widget strips newlines, the paste gate is
		// the real path): a newline-bearing submission never executes
		// directly either.
		openConfirm(text, false);
		return;
	}
	setControlValue(doc, "console-input", "");
	HistoryCursor = -1;
	hideCompletion();
	if (Submit && Submit(trimmed))
		answerUnknownCommand(trimmed);
	const ActionSink Callback = Sink;
	if (Callback)
		Callback(kActionSubmit);
}

// The console's own answer for a command the client runtime catalog does not
// know (the module already decided it belongs here: the engine is silent for
// those, and a loaded level's server may own the name). The line rides the
// same scrollback the player reads - exactly where the vanilla console put
// its messages - and goes through the same escaping the projection uses.
void ConsolePage::answerUnknownCommand(const std::string& command) {
	if (!Buffer)
		return;
	const std::string verb = services::console::ConsoleBackend::verbOf(command.c_str());
	if (verb.empty())
		return;  // not classifiable: never guess a name
	std::string line;
	if (!framework::Localization::translate(line, "Console_UnknownCommand"))
		line = "Unknown command: %s";
	const std::string marker = "%s";
	const auto at = line.find(marker);
	if (at != std::string::npos)
		line.replace(at, marker.size(), verb);
	line += '\n';  // one complete console line
	Buffer->pushText(line.data(), line.size(), false);
}

void ConsolePage::openCompletionFor(const std::string& prefix) {
	rebuildCompletion(prefix, true);
}

void ConsolePage::refreshSuggestions() {
	Rml::ElementDocument* doc = documentOrNull();
	if (!doc)
		return;
	rebuildCompletion(controlValue(doc, "console-input"), false);
}

void ConsolePage::rebuildCompletion(const std::string& prefix, bool apply) {
	Rml::ElementDocument* doc = documentOrNull();
	Suggestions.clear();
	SuggestionIndex = -1;
	if (!doc || !Backend || prefix.empty()) {
		hideCompletion();
		return;
	}
	// One extra match is requested so the dropdown can say that more exist
	// without knowing the catalog size (the enumeration itself is capped in
	// the backend, which is what keeps a 3000-entry catalog from stalling).
	std::vector<std::string> matches = Backend->complete(prefix.c_str(), kMaxSuggestions + 1);
	const bool more = static_cast<int>(matches.size()) > kMaxSuggestions;
	if (more)
		matches.resize(kMaxSuggestions);
	Suggestions = std::move(matches);
	if (Suggestions.empty()) {
		hideCompletion();
		return;
	}
	if (apply && Suggestions.size() == 1) {
		// Unique match: complete in place, no list.
		setControlValue(doc, "console-input", Suggestions[0]);
		Suggestions.clear();
		hideCompletion();
		return;
	}
	// Multiple matches: extend the editor to the longest common prefix and
	// show the list.
	std::string common = Suggestions[0];
	for (const std::string& match : Suggestions) {
		std::size_t length = 0;
		while (length < common.size() && length < match.size() && common[length] == match[length])
			++length;
		common.resize(length);
	}
	if (apply && common.size() > prefix.size())
		setControlValue(doc, "console-input", common);
	Rml::Element* list = doc->GetElementById("console-suggestions");
	if (!list)
		return;
	list->SetInnerRML("");
	for (std::size_t i = 0; i < Suggestions.size(); ++i) {
		Rml::ElementPtr row = doc->CreateElement("div");
		if (!row)
			continue;
		row->SetAttribute("data-action", "console-suggestion");
		row->SetAttribute("data-index", static_cast<int>(i));
		row->SetClass("console-suggestion", true);
		row->SetInnerRML(framework::TextUtil::escapeRml(Suggestions[i]));
		list->AppendChild(std::move(row));
	}
	if (more) {
		Rml::ElementPtr row = doc->CreateElement("div");
		if (row) {
			row->SetClass("console-suggestion", true);
			row->SetClass("console-suggestion-more", true);
			row->SetInnerRML("&hellip;");
			list->AppendChild(std::move(row));
		}
	}
	setVisible(doc, "console-suggestions", true);
}

void ConsolePage::hideCompletion() {
	Rml::ElementDocument* doc = documentOrNull();
	Suggestions.clear();
	SuggestionIndex = -1;
	setVisible(doc, "console-suggestions", false);
}

#ifndef NDEBUG
void ConsolePage::debugType(const char* text) {
	Rml::ElementDocument* doc = documentOrNull();
	setControlValue(doc, "console-input", text ? text : "");
	// The debug seam stands in for real typing, and typing leaves the caret
	// behind the inserted text: park it at the end so caret-moving keys
	// (LEFT/RIGHT) have the same starting point they would after real input.
	if (doc)
		parkCaretAtEnd(doc->GetElementById("console-input"));
	refreshSuggestions();
}
#endif

void ConsolePage::applyCompletion(int index) {
	Rml::ElementDocument* doc = documentOrNull();
	if (!doc || index < 0 || index >= static_cast<int>(Suggestions.size()))
		return;
	setControlValue(doc, "console-input", Suggestions[index]);
	hideCompletion();
	refocusDefault();
}

void ConsolePage::openConfirm(const std::string& text, bool fromPaste) {
	Rml::ElementDocument* doc = documentOrNull();
	if (!doc)
		return;
	PendingText = text;
	PendingFromPaste = fromPaste;
	const std::vector<std::string> lines = splitCommandLines(text);
	std::string lead = "#Console_ConfirmLead";
	framework::Localization::translate(lead, "Console_ConfirmLead");
	setText(doc, "console-confirm-lead", lead + " " + std::to_string(lines.size()));
	// The full text, escaped, in a scrollable block.
	setText(doc, "console-confirm-text", text);
	setVisible(doc, "console-confirm", true);
	if (Rml::Element* yes = doc->GetElementById("console-confirm-yes"))
		yes->Focus();
}

void ConsolePage::closeConfirm() {
	PendingText.clear();
	PendingFromPaste = false;
	setVisible(documentOrNull(), "console-confirm", false);
}

void ConsolePage::executeConfirmed() {
	Rml::ElementDocument* doc = documentOrNull();
	if (PendingText.empty())
		return;
	const std::vector<std::string> lines = splitCommandLines(PendingText);
	closeConfirm();
	if (doc)
		setControlValue(doc, "console-input", "");
	HistoryCursor = -1;
	for (const std::string& line : lines) {
		if (Submit)
			Submit(line);  // one validated submission per line, in order
	}
	refocusDefault();
	const ActionSink Callback = Sink;
	if (Callback)
		Callback(kActionConfirmExecute);
}

void ConsolePage::walkHistory(int delta) {
	Rml::ElementDocument* doc = documentOrNull();
	if (!doc || !Backend)
		return;
	const auto& history = Backend->history();
	if (history.empty())
		return;
	if (HistoryCursor < 0 && delta > 0) {
		// Starting the walk: remember the live draft to restore on the way
		// back down.
		HistoryDraft = controlValue(doc, "console-input");
		HistoryCursor = 0;
	} else if (delta > 0) {
		if (HistoryCursor + 1 >= static_cast<int>(history.size()))
			return;  // oldest end
		++HistoryCursor;
	} else {
		if (HistoryCursor <= 0) {
			// Back to the live draft.
			HistoryCursor = -1;
			setControlValue(doc, "console-input", HistoryDraft);
			parkCaretAtEnd(doc->GetElementById("console-input"));
			return;
		}
		--HistoryCursor;
	}
	const int index = static_cast<int>(history.size()) - 1 - HistoryCursor;
	setControlValue(doc, "console-input", history[static_cast<std::size_t>(index)]);
	// Caret to the end so typing appends (the widget's selection rides the
	// parent element's selection API, not the form-control base).
	parkCaretAtEnd(doc->GetElementById("console-input"));
}

void ConsolePage::WindowListener::ProcessEvent(Rml::Event& event) {
	Owner->handleClick(event);
}

void ConsolePage::KeyListener::ProcessEvent(Rml::Event& event) {
	Owner->handleKey(event);
}

}
