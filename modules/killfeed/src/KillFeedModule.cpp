// abcui.killfeed - the kill feed rows. Moved from
// src/panels/deadmsg (old CDeathMsgPanel, ABCEnchance sources:
// src/hud/vgui/deadmsg.*): rows fed by the textfeed.killfeed channel, capped
// by hud_deathnotice_count, rows fading out after hud_deathnotice_time
// (0.5s fade); hud_deathnotice toggles the whole feed (persisted contract).
//
// Motion: a new row plays the stylesheet's entrance animation (rise + fade,
// see deadmsg.rcss) while the panel glides the feed's viewport to the tail
// (followTail below, the chat log's arrival scroll), so a burst reads as one
// upward slide instead of the reflow jumping the rows a height per kill.
//
// Port note kept from the built-in copy: the old AddItem console print built
// its printf with transposed Q_snprintf arguments (the victim name landed in
// the format slot); the port prints the localized "DeathMsg_ConsolePrint"
// template with a positional substitution, never a printf format.
#include "../../common/Text.h"
#include "../../common/OpacityMotion.h"
#include "../../common/UiText.h"
#include "../../common/Panel.h"
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

// Old CDeathMsgPanel ctor defaults (persisted contract - never rename).
constexpr const char *kDeathNotice = "hud_deathnotice";
constexpr const char *kDeathNoticeTime = "hud_deathnotice_time";
constexpr const char *kDeathNoticeCount = "hud_deathnotice_count";

constexpr const char *kPanelName = "deadmsg";
constexpr const char *kDocumentPath =
    "mod://abcui.killfeed/hud/rml/deadmsg.rml";

// Old RunAnimationCommand duration.
constexpr double kFadeSeconds = 0.5;

AbcUiCvarHandle DeathNotice = nullptr;
AbcUiCvarHandle DeathNoticeTime = nullptr;
AbcUiCvarHandle DeathNoticeCount = nullptr;

struct Row {
  AbcUiElement *element = nullptr;
  double createTime = 0.0;
  double fadeStartTime = -1.0;
  moduleui::opacity::Ramp opacity{1.0f};
};

double ClientTimeNow = 0.0;
std::vector<Row> Rows;

AbcUiDocument *panelDocument() {
  return moduleui::panelDocument(Api, kPanelName);
}

int cvarInt(AbcUiCvarHandle cvar, int fallback) {
  return cvar ? static_cast<int>(
                    Api->console->cvarValue(cvar, static_cast<float>(fallback)))
              : fallback;
}

void setVisible(bool visible) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

// Measure-then-copy over the bounded C API (server text is untrusted).
std::string escapeText(const char *text) {
  return moduleui::escapeText(Api, text);
}

std::string translate(const char *token, const char *fallback) {
  char out[512];
  return Api->ui->translate(token, out, sizeof(out)) ? std::string(out)
                                                     : std::string(fallback);
}

// Old console print template is a {0}..{2} template (never a printf format).
std::string substitute(const std::string &text, const char *a, const char *b,
                       const char *c) {
  const char *values[3] = {a, b, c};
  std::string out = text;
  for (int i = 0; i < 3; i++) {
    const std::string key = "{" + std::to_string(i) + "}";
    const std::string value = values[i] ? values[i] : "";
    moduleui::replaceAll(out, key, value);
  }
  return out;
}

void removeRow(Row &row) {
  if (row.element)
    Api->ui->elementRemove(row.element);
  row.element = nullptr;
}

void clearRows(AbcUiDocument *doc) {
  for (Row &row : Rows)
    removeRow(row);
  Rows.clear();
  if (doc)
    Api->ui->documentHide(doc);
}

void addItem(const char *victim, const char *attacker, const char *inflictor) {
  // Old hud_deathnotice callback (ShowDeathMsg) - evaluated live so the cvar
  // takes effect without a panel rebuild.
  if (cvarInt(DeathNotice, 1) <= 0)
    return;
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;

  // A killfeed during a hide-all token still appends the row (it renders
  // with the document's next show), but must not un-hide the panel.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  if (!(state.hideBits & ABCUI_HIDEBIT_ALL))
    setVisible(true);

  // Server-derived names are escaped through the shared helper (D-17 #3).
  // Rows land in the bottom-packed stack behind the scroll viewport (the
  // stack's growth is what opens the range the tail glide covers).
  const std::string rml = "<div class=\"attacker font-default\">" +
                          escapeText(attacker) +
                          "</div>"
                          "<div class=\"inflictor font-default\">[" +
                          escapeText(inflictor) +
                          "]</div>"
                          "<div class=\"victim font-default\">" +
                          escapeText(victim) + "</div>";
  AbcUiElement *stack = Api->ui->querySelector(doc, "stack");
  if (!stack)
    return;
  AbcUiElement *row = Api->ui->elementAppendRml(stack, rml.c_str(), "row");
  if (!row)
    return;

  // Old capacity: hud_deathnotice_count rows, oldest dropped first.
  const int maxRows = std::max(1, cvarInt(DeathNoticeCount, 6));
  while (static_cast<int>(Rows.size()) >= maxRows) {
    removeRow(Rows.front());
    Rows.erase(Rows.begin());
  }
  Row entry;
  entry.element = row;
  entry.createTime = ClientTimeNow;
  Rows.push_back(entry);

  // Old AddItem console print (localized template). The names never touch a
  // printf format slot.
  const std::string consoleTemplate = translate("DeathMsg_ConsolePrint", "");
  if (!consoleTemplate.empty()) {
    Api->core->print(
        "%s\n",
        substitute(consoleTemplate, victim,
                   attacker && attacker[0] ? attacker : "Something", inflictor)
            .c_str());
  }
}

// ---- event sinks ----

int canKillFeed(void *, const void *) {
  return panelDocument() != nullptr;
}

int32_t onKillFeed(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiKillFeedEvent *>(payload);
  addItem(event->victim ? event->victim : "",
          event->attacker ? event->attacker : "",
          event->inflictor ? event->inflictor : "");
  return ABCUI_DECISION_HANDLED;
}

void onHideToken(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiIntEvent *>(payload);
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  // The old viewport-wide HIDEALL token hid the feed with the rest of the
  // Viewport; rows kept fading underneath. On clear the still-alive rows
  // reappear (old Viewport SetVisible(true) revealed its child panels).
  if (event->value & ABCUI_HIDEBIT_ALL)
    setVisible(false);
  else if (!Rows.empty())
    setVisible(true);
}

// ---- tail glide ----

// The "scroll up" half of the new-row motion (the chat log's followTail,
// ChatModule.cpp): appending a row re-anchors the bottom-packed stack one row
// height higher and grows the viewport's scrollable overflow by the same
// distance at the tail end, and this per-frame ease moves the viewport down
// to the tail instead of letting the reflow jump. The target is re-read every
// frame, so a burst of kills merges into one glide, and a frame the layout
// pass has not caught up in simply finds no distance to cover. Unlike the
// chat there is no reader side - the HUD document is pointer-transparent, so
// the follow is never disarmed and the viewport writes need no own-scroll
// guard; the clamp that lands the viewport after a fade-out trim dispatches
// nothing and leaves it parked on the tail.
constexpr double kFollowSeconds = 0.075;
constexpr float kFollowEpsilonDp = 0.5f;

void followTail(double frameTime) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  AbcUiElement *feed = Api->ui->querySelector(doc, "feed");
  if (!feed)
    return;
  const float top = Api->ui->elementScrollTop(feed);
  const float tail =
      Api->ui->elementScrollHeight(feed) - Api->ui->elementClientHeight(feed);
  if (tail - top <= kFollowEpsilonDp)
    return; // the stack fits its viewport (or is parked on the tail)
  float next = tail;
  if (frameTime > 0.0) {
    // Frame-time independent ease-out: the first frames carry most of the
    // distance, which is what makes a one-row arrival read as a glide.
    const double ease = 1.0 - std::exp(-frameTime / kFollowSeconds);
    next = top + (tail - top) * static_cast<float>(ease);
    if (tail - next < kFollowEpsilonDp)
      next = tail;
  }
  Api->ui->elementSetScrollTop(feed, next);
}

// ---- panel callbacks ----

void panelOnData(void *, double frameTime) {
  frameTime = moduleui::opacity::frameDelta(frameTime);
  ClientTimeNow += frameTime;
  if (cvarInt(DeathNotice, 1) <= 0) {
    clearRows(panelDocument());
    return;
  }
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  const double keep = static_cast<double>(cvarInt(DeathNoticeTime, 6));
  for (auto itor = Rows.begin(); itor != Rows.end();) {
    Row &row = *itor;
    bool fadeStarted = false;
    if (row.fadeStartTime < 0.0) {
      // Old per-row RunAnimationCommand: fade starts keep seconds after the
      // row was added.
      if (ClientTimeNow >= row.createTime + keep) {
        row.fadeStartTime = ClientTimeNow;
        row.opacity.retarget(0.0f, kFadeSeconds);
        fadeStarted = true;
      }
    }
    if (row.fadeStartTime >= 0.0 &&
        ClientTimeNow >= row.fadeStartTime + kFadeSeconds) {
      removeRow(row);
      itor = Rows.erase(itor);
      continue;
    }
    float opacity = 1.0f;
    if (row.element &&
        row.opacity.advance(fadeStarted ? 0.0 : frameTime, opacity)) {
      char value[16];
      moduleui::opacity::text(opacity, value);
      Api->ui->elementSetProperty(row.element, "opacity", value);
    }
    ++itor;
  }
  // The appended row is measured by the next layout pass: until then the
  // scroll range is stale and the ease finds nothing to cover (the chat's
  // snap step has no equivalent here - no input line, no viewport resize).
  followTail(frameTime);
  if (Rows.empty())
    Api->ui->documentHide(doc);
}

void panelReset(void *) {
  // Old Reset: clear rows + hide.
  clearRows(panelDocument());
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel = moduleui::makePanel(kPanelName, kDocumentPath, 0);
  Panel.userdata = nullptr;
  Panel.onDocumentReady = nullptr;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  AbcUiClaimDesc feed{};
  feed.structSize = sizeof(feed);
  feed.role = ABCUI_CLAIM_OWNER;
  feed.canHandle = &canKillFeed;
  feed.handle = &onKillFeed;
  if (!Api->claims->claimDomain(ABCUI_CHANNEL_TEXTFEED_KILL_FEED, &feed))
    Api->core->warn("killfeed: cannot claim the kill feed");
  Api->events->subscribe(ABCUI_CHANNEL_HUD_HIDE_TOKEN, 0, &onHideToken,
                         nullptr);
}

void onModuleInit() {
  DeathNotice = Api->console->registerCvar(kDeathNotice, "1", ABCUI_CVAR_VALUE);
  DeathNoticeTime =
      Api->console->registerCvar(kDeathNoticeTime, "6", ABCUI_CVAR_VALUE);
  DeathNoticeCount =
      Api->console->registerCvar(kDeathNoticeCount, "6", ABCUI_CVAR_VALUE);
}

void onModuleShutdown() {
  // The document (and every appended row) dies with the context; the handles
  // must not survive into the next cycle.
  Rows.clear();
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (!AbcUiModule_Validate(
          api, ABCUI_MODULE_SECTION_CORE | ABCUI_MODULE_SECTION_CONSOLE |
                   ABCUI_MODULE_SECTION_EVENTS | ABCUI_MODULE_SECTION_PANELS |
                   ABCUI_MODULE_SECTION_DATA | ABCUI_MODULE_SECTION_UI |
                   ABCUI_MODULE_SECTION_CLAIMS))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.killfeed";
  out->version = "3.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
