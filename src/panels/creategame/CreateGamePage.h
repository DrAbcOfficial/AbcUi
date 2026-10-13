#pragma once

// panels::creategame - the T19 create-local-game page (one interactive
// document at the kCreateGame menu page, replacing the T09 stub surface).
//
// The page is a FORM projection of the enumerated map catalog plus the three
// typed fields of the shipped CreateMultiplayerGameServerPage (map, players,
// name, password). It never writes an engine value and never assembles a
// command: the module validates the assembled request through the production
// CreateGameModel and turns the Apply intent into the controller's typed
// kCreateGameRequested attempt.
//
// Contract points (T19 steps 2/4):
//   - The map control offers exactly the catalog entries (the engine
//     filesystem enumeration); a selection cannot produce a name outside the
//     set, and any field text is validated by the model before the intent is
//     reported - a refused request produces a page-local hint and ZERO engine
//     calls.
//   - Apply is latched after the first report: one double-click window cannot
//     start two servers (the backend's per-generation latch is the second
//     guard behind it). prepare() re-arms the latch for a fresh open.
//   - Cancel and every other leave path report a plain back intent: the page
//     owns no engine write, so leaving writes nothing.
//   - All dynamic text (hints, map names) reaches the DOM escaped.

#include "../../framework/InteractiveDocument.h"
#include "../../services/creategame/CreateGameModel.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>
#include <vector>

namespace Rml {
class Element;
class Event;
}

namespace panels::creategame {

using ActionSink = std::function<void(const char* action)>;

class CreateGamePage final : public framework::InteractiveDocument {
public:
	CreateGamePage();
	~CreateGamePage() override;

	void setActionSink(ActionSink sink) { Sink = std::move(sink); }

	// Fresh projection for a new opening: rebuilds the map options from the
	// catalog, resets players/name/password to the given defaults, clears the
	// hint and re-arms the Apply latch. `defaultName` is the live hostname
	// (empty projects the localized default token).
	void prepare(const services::creategame::MapCatalog& catalog, const char* defaultName);

	// Inline feedback for a refused request (a "#token" or plain text; the
	// page resolves local tokens and escapes before the DOM). Empty clears.
	void showHint(const char* text);

	// Field reads for the module's request assembly (empty when missing).
	std::string selectedMap() const;
	std::string playersText() const;
	std::string serverName() const;
	std::string password() const;

	// Re-takes the safe default focus (module's per-frame focus guard): the
	// map control.
	void refocusDefault();

protected:
	void onDocumentReady() override;

private:
	class PageListener final : public Rml::EventListener {
	public:
		explicit PageListener(CreateGamePage* owner) : Owner(owner) {}
		void ProcessEvent(Rml::Event& event) override;

	private:
		CreateGamePage* Owner;
	};

	void handleClick(Rml::Event& event);
	Rml::Element* node(const char* id) const;

	ActionSink Sink;
	PageListener Listener;
	std::vector<std::string> Maps;
	// Once an Apply intent was reported, further Apply clicks are dropped
	// until prepare() re-arms (T19 step 4: double-click = one server).
	bool Applied = false;
};

}
