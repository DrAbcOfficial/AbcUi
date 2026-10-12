// abc_ui_interactive - T05 acceptance for the interactive-document owner.
// Links the real InteractiveDocument.cpp + MainMenuPage.cpp against static
// RmlUi with a no-op renderer. ctest runs this target with the repo assets
// directory as the working directory, so the page case loads the SHIPPED
// mainmenu.rml/mainmenu.rcss (the load also validates their syntax offline)
// and pins the element ids the page wires. Cases: the explicit load-failure
// outcome with emergency fallback, listener lifecycle across close/reload
// (no stale binding onto recycled elements), repeated show/hide idempotence
// with focus, and the page's injected action sink. Keyboard input
// arbitration is out of scope until T06; text characters until T07.
#include "framework/InteractiveDocument.h"
#include "panels/mainmenu/MainMenuPage.h"

#include <RmlUi/Core.h>

// cl_enginefunc_t + the gEngfuncs extern, with the SDK's include-order
// ritual already handled (same include set as abc_ui_hook_contract).
#include <metahook.h>

#include <windows.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

// Localization.cpp stub: the VGUI2 language import is not linked here; the
// english table simply never loads and #tokens pass through unresolved.
namespace vgui2ext {
const char* currentLanguage() { return "english"; }
}

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

class NullRenderInterface final : public Rml::RenderInterface {
public:
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return {}; }
	void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
	void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
	Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return {}; }
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return {}; }
	void ReleaseTexture(Rml::TextureHandle) override {}
	void EnableScissorRegion(bool) override {}
	void SetScissorRegion(Rml::Rectanglei) override {}
};

// Silence the no-font / layout chatter; only errors surface (and count).
class QuietSystemInterface final : public Rml::SystemInterface {
public:
	double GetElapsedTime() override { return 0.0; }
	int TranslateString(Rml::String& translated, const Rml::String& input) override {
		translated = input;
		return -1;
	}
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
		if (type <= Rml::Log::LT_ERROR)
			std::cerr << "rmlui: " << message << "\n";
		return true;
	}
};

// Test subclass carrying its own counting listener: proves the owner's
// detach-before-close with a listener object that outlives the document.
class CountingDocument final : public framework::InteractiveDocument {
public:
	explicit CountingDocument(const char* path)
		: InteractiveDocument("counting", path) {}

	class ClickCounter final : public Rml::EventListener {
	public:
		int Clicks = 0;
		void ProcessEvent(Rml::Event&) override { ++Clicks; }
	};

	ClickCounter Counter;
	int ReadyCount = 0;

protected:
	void onDocumentReady() override {
		++ReadyCount;
		if (Rml::Element* probe = document()->GetElementById("probe"))
			addListener(probe, "click", &Counter);
	}
};

// Temp fixture independent of the repo assets (the missing-file case needs
// a path that is guaranteed absent).
std::string tempFixturePath(bool create) {
	char dir[MAX_PATH];
	GetTempPathA(MAX_PATH, dir);
	std::string path = std::string(dir) + "abc_ui_interactive_fixture.rml";
	if (create) {
		FILE* file = nullptr;
		if (fopen_s(&file, path.c_str(), "wb") == 0 && file) {
			fputs("<rml><head><style>div { display: block; }</style></head>"
				"<body id=\"counting\"><div id=\"probe\">probe</div></body></rml>",
				file);
			fclose(file);
		}
	} else {
		DeleteFileA(path.c_str());
	}
	return path;
}

bool focusBelongsTo(Rml::Context* context, Rml::ElementDocument* doc) {
	Rml::Element* focus = context->GetFocusElement();
	return focus && doc && focus->GetOwnerDocument() == doc;
}

} // namespace

int main() {
	QuietSystemInterface systemInterface;
	NullRenderInterface renderer;
	Rml::SetSystemInterface(&systemInterface);
	Rml::SetRenderInterface(&renderer);
	if (!Rml::Initialise()) {
		std::cerr << "FAIL: Rml::Initialise\n";
		return 1;
	}
	// Layout stability for text-heavy documents; absence is tolerated (the
	// tests assert structure, not glyph metrics).
	Rml::LoadFontFace("C:\\Windows\\Fonts\\verdana.ttf");
	Rml::Context* context = Rml::CreateContext("test", Rml::Vector2i(1280, 720));
	if (!context) {
		std::cerr << "FAIL: CreateContext\n";
		Rml::Shutdown();
		return 1;
	}

	{
		CurrentCase = "load-failure-emergency";
		CountingDocument missing(tempFixturePath(false).c_str());
		const auto outcome = missing.load(context);
		CHECK(outcome == framework::InteractiveDocument::LoadOutcome::kEmergencyFallback);
		CHECK(missing.isDegraded());
		CHECK(missing.isLoaded());
		CHECK(missing.document() == context->GetDocument("counting"));
		CHECK(missing.ReadyCount == 0); // no page wiring against emergency DOM
		// The emergency page shows and focuses like any interactive document.
		missing.show();
		CHECK(missing.isVisible());
		CHECK(focusBelongsTo(context, missing.document()));
		missing.close();
		CHECK(!missing.isLoaded() && !missing.isDegraded());
	}

	{
		CurrentCase = "listener-lifecycle";
		CountingDocument doc(tempFixturePath(true).c_str());
		CHECK(doc.load(context) == framework::InteractiveDocument::LoadOutcome::kLoaded);
		CHECK(!doc.isDegraded());
		CHECK(doc.ReadyCount == 1);
		Rml::Element* probe = doc.document()->GetElementById("probe");
		CHECK(probe != nullptr);
		if (probe)
			probe->DispatchEvent("click", Rml::Dictionary());
		CHECK(doc.Counter.Clicks == 1);

		// Close, then reload into a fresh object. Rml recycles element
		// memory, so the old binding - had close() not detached it - could
		// resurface on the new DOM; the old counter must stay silent while
		// the fresh wiring fires.
		const int oldClicks = doc.Counter.Clicks;
		doc.close();
		CHECK(!doc.isLoaded());
		{
			CountingDocument reloaded(tempFixturePath(true).c_str());
			CHECK(reloaded.load(context) == framework::InteractiveDocument::LoadOutcome::kLoaded);
			Rml::Element* probe2 = reloaded.document()->GetElementById("probe");
			CHECK(probe2 != nullptr);
			if (probe2)
				probe2->DispatchEvent("click", Rml::Dictionary());
			CHECK(doc.Counter.Clicks == oldClicks); // stale binding silent
			CHECK(reloaded.Counter.Clicks == 1);    // fresh binding live
		}
	}

	{
		CurrentCase = "show-hide-idempotence";
		CountingDocument doc(tempFixturePath(true).c_str());
		CHECK(doc.load(context) == framework::InteractiveDocument::LoadOutcome::kLoaded);
		doc.show();
		CHECK(doc.isVisible());
		CHECK(focusBelongsTo(context, doc.document()));
		Rml::Element* firstFocus = context->GetFocusElement();
		doc.show(); // repeated show: no re-focus, no churn
		CHECK(doc.isVisible());
		CHECK(context->GetFocusElement() == firstFocus);
		doc.hide();
		CHECK(!doc.isVisible());
		// Focus release semantics are T06: with this test context holding
		// only our document, RmlUi's UnfocusDocument has no other document
		// to hand focus to and the context focus pointer stays put.
		doc.hide(); // repeated hide stays benign
		CHECK(!doc.isVisible());
		doc.show();
		CHECK(doc.isVisible() && focusBelongsTo(context, doc.document()));
		// Bindings survive hide/show cycles.
		if (Rml::Element* probe = doc.document()->GetElementById("probe"))
			probe->DispatchEvent("click", Rml::Dictionary());
		CHECK(doc.Counter.Clicks == 1);
		doc.close();
	}

	{
		CurrentCase = "mainmenu-page";
		panels::mainmenu::MainMenuPage page;
		std::vector<std::string> actions;
		page.setActionSink([&actions](const char* action) { actions.push_back(action); });
		CHECK(page.load(context) == framework::InteractiveDocument::LoadOutcome::kLoaded);
		CHECK(page.document() == context->GetDocument("mainmenu"));
		// The shipped RML must carry every id the page wires (also proves
		// the real stylesheet parsed: id selectors fail loudly at layout).
		for (const char* id : {"title", "focus-status", "action-button", "text-field", "password-field", "area-field", "close-button", "return-button"})
			CHECK(page.document()->GetElementById(id) != nullptr);

		page.show();
		CHECK(page.isVisible());
		// autofocus lands on the action button (FocusFlag::Auto contract).
		Rml::Element* focus = context->GetFocusElement();
		CHECK(focus && focus->GetId() == "action-button");

		if (Rml::Element* button = page.document()->GetElementById("action-button")) {
			button->DispatchEvent("click", Rml::Dictionary());
			CHECK(actions.size() == 1 && actions.back() == "action");
		}
		// close/return report through the sink AND hide the page themselves.
		if (Rml::Element* close = page.document()->GetElementById("close-button")) {
			close->DispatchEvent("click", Rml::Dictionary());
			CHECK(actions.size() == 2 && actions.back() == "close");
			CHECK(!page.isVisible());
		}
		page.show();
		if (Rml::Element* back = page.document()->GetElementById("return-button")) {
			back->DispatchEvent("click", Rml::Dictionary());
			CHECK(actions.size() == 3 && actions.back() == "return");
			CHECK(!page.isVisible());
		}

		// Focus visual state: the status line tracks the focused element via
		// the (non-bubbling) focus/blur bindings. The lang table is not
		// loaded here, so #tokens pass through unresolved - exactly the
		// documented fallback.
		page.show();
		if (Rml::Element* status = page.document()->GetElementById("focus-status")) {
			Rml::Element* field = page.document()->GetElementById("text-field");
			CHECK(field != nullptr);
			if (field) {
				CHECK(field->Focus());
				CHECK(status->GetInnerRML() == "#MainMenu_Text");
				field->Blur();
				CHECK(status->GetInnerRML() == "#MainMenu_FocusNone");
			}
		}
		page.close();
		CHECK(!page.isLoaded());
	}

	tempFixturePath(false); // remove the fixture file
	Rml::RemoveContext("test");
	Rml::Shutdown();

	if (Failures) {
		std::cerr << Failures << " failure(s)\n";
		return 1;
	}
	std::cout << "abc_ui_interactive: all cases passed\n";
	return 0;
}
