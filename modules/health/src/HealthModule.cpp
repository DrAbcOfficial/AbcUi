// abcui.health - health/armor bars with the danger hue shift and the longjump
// indicator. Moved from src/panels/health (old CHealthPanel, ABCEnchance
// sources: src/hud/vgui/health.*).
//
// Visibility rule (old Viewport hudhideCallback + SetSpectate): suit
// possessed, neither of the health/battery hide bits fully covering, and not
// spectating. hud_danger_health / hud_danger_armor keep their old names and
// defaults (persisted contract).
#include "../../common/BarMotion.h"
#include "../../common/ValueBounce.h"
#include "../../common/Panel.h"

#include <abcui/events.h>
#include <abcui/module_api.h>

#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

// Old CHealthPanel ctor defaults (persisted contract - never rename).
constexpr const char *kDangerHealth = "hud_danger_health";
constexpr const char *kDangerArmor = "hud_danger_armor";

constexpr const char *kPanelName = "health";
constexpr const char *kDocumentPath = "mod://abcui.health/hud/rml/health.rml";

// HUD accent amber (255 176 0, base.rcss --abc-hud-accent) and the scheme
// "Red" danger colour (255 0 0). The old scheme's "Default" green was
// retuned to the Half-Life amber; the hue-shift path is unchanged.
constexpr float kAccent[3] = {255.0f, 176.0f, 0.0f};
constexpr float kRed[3] = {255.0f, 0.0f, 0.0f};
AbcUiCvarHandle DangerHealth = nullptr;
AbcUiCvarHandle DangerArmor = nullptr;

// One write policy per vitals bar (first show snaps, later changes animate),
// plus the number's pop on a real value change.
struct VitalState {
  moduleui::barmotion::Tracker Motion;
  moduleui::bounce::Pulse Pop;
};

VitalState HealthVital;
VitalState ArmorVital;

AbcUiDocument *panelDocument() {
  return moduleui::panelDocument(Api, kPanelName);
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

void setElementProperty(const char *id, const char *name, const char *value) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementSetProperty(element, name, value);
}

void setLongJump(bool longJump) {
  setElementProperty("longjump", "visibility", longJump ? "visible" : "hidden");
}

void setHealth(int health);
void setArmor(int armor);
void applyHealth(const AbcUiHudState &state, int health);

// Old SetHealthVisible/SetArmorVisible element groups.
void setGroupVisible(const char *const *ids, size_t count, bool visible) {
  for (size_t i = 0; i < count; ++i)
    setElementProperty(ids[i], "visibility", visible ? "visible" : "hidden");
}

void updateVisibility() {
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  const bool hideHealth = (state.hideBits & ABCUI_HIDEBIT_HEALTH) != 0;
  const bool hideBattery = (state.hideBits & ABCUI_HIDEBIT_BATTERY) != 0;
  // kHideAll = the old Viewport-wide SetVisible(false) (hud hide-all token
  // from scripted sequences); the group split stays independent of it.
  const bool visible = state.suit && (!hideBattery || !hideHealth) &&
                       !state.spectate && !(state.hideBits & ABCUI_HIDEBIT_ALL);
  setVisible(visible);

  static const char *const kHealthGroup[] = {"health-icon", "health-label",
                                             "health-bar-bg", "health-bar"};
  static const char *const kArmorGroup[] = {"armor-icon", "armor-label",
                                            "armor-bar-bg", "armor-bar"};
  setGroupVisible(kHealthGroup, 4, !hideHealth);
  setGroupVisible(kArmorGroup, 4, !hideBattery);

  // Re-seed after the visibility flip: a group hidden while its value moved
  // must land on the current value, and the tracker snaps it instead of
  // animating from the last drawn width.
  setHealth(state.health);
  setArmor(state.armor);
}

std::string dangerColor(int value, AbcUiCvarHandle cvar, float fallback) {
  const float dangerValue =
      cvar ? Api->console->cvarValue(cvar, fallback) : fallback;
  char color[64];
  if (dangerValue > 0.0f && value < static_cast<int>(dangerValue)) {
    // Old GetDifferColor: hue pulled toward the danger red as the value falls
    // below the threshold (ratio = value / danger).
    Api->ui->differColor(kAccent, kRed, static_cast<float>(value) / dangerValue,
                         color, sizeof(color));
  } else {
    Api->ui->formatColor(kAccent, color, sizeof(color));
  }
  return color;
}

// A vitals bar is drawn only while the suit is owned, the HUD is not hidden
// and the group's own hide bit is clear. A hidden bar has no live width, so the
// tracker snaps the first shown write instead of animating from a stale one.
bool healthShown(const AbcUiHudState &state) {
  return state.suit && state.spectate == 0 &&
         !(state.hideBits & ABCUI_HIDEBIT_ALL) &&
         !(state.hideBits & ABCUI_HIDEBIT_HEALTH);
}

bool armorShown(const AbcUiHudState &state) {
  return state.suit && state.spectate == 0 &&
         !(state.hideBits & ABCUI_HIDEBIT_ALL) &&
         !(state.hideBits & ABCUI_HIDEBIT_BATTERY);
}

void applyVital(const char *labelId, const char *barId, const char *iconId,
                int value, AbcUiCvarHandle cvar, float fallback, bool shown,
                VitalState &vital) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  AbcUiElement *bar = Api->ui->querySelector(doc, barId);
  if (!bar)
    return;

  // One gate for every write below: a repeated value, a hidden bar and a
  // transition that is still running must not be disturbed.
  const moduleui::barmotion::Step step = vital.Motion.observe(value, shown);
  if (!step.write)
    return;

  const std::string color = dangerColor(value, cvar, fallback);
  if (AbcUiElement *label = Api->ui->querySelector(doc, labelId)) {
    // Plain type with the 1px outline from health.rcss: the additive glyph
    // pass read worse here, so the value is text again.
    const std::string text = std::to_string(value);
    Api->ui->elementSetInnerRML(label, text.c_str());
    Api->ui->elementSetProperty(label, "color", color.c_str());
    // A real value change pops the number; the first show and a re-show land
    // on the value without one (the tracker's snap flag).
    if (!step.snap)
      vital.Pop.trigger();
  }
  // The meter is a flat rounded fill; its width is the bar-fill-N ladder
  // percentage of the well, and the box's direction class picks the ramp. The
  // icon stays the additive sprite. The danger rule retints both.
  char classes[moduleui::barmotion::kClassCapacity] = {};
  Api->ui->elementSetAttribute(
      bar, "class",
      moduleui::barmotion::barClasses(
          step.direction, step.snap,
          moduleui::barmotion::percent(static_cast<float>(value) / 100.0f),
          classes, sizeof(classes)));
  Api->ui->elementSetProperty(bar, "background-color", color.c_str());
  if (AbcUiElement *icon = Api->ui->querySelector(doc, iconId))
    Api->ui->elementSetProperty(icon, "image-color", color.c_str());
}

// The value pop: the label at a scale factor.
void writeVitalScale(AbcUiDocument *doc, const char *labelId, float scale) {
  AbcUiElement *label = doc ? Api->ui->querySelector(doc, labelId) : nullptr;
  if (!label)
    return;
  char transform[32] = {};
  Api->ui->elementSetProperty(
      label, "transform",
      moduleui::bounce::transformText(scale, transform, sizeof(transform)));
}

// One frame of a running pop. Only a running pulse writes, so a settled
// readout costs one boolean per frame.
void tickVital(const char *labelId, VitalState &vital, double frameTime) {
  if (!vital.Pop.active())
    return;
  float scale = 1.0f;
  if (!vital.Pop.advance(frameTime, scale))
    return;
  writeVitalScale(panelDocument(), labelId, scale);
}

void panelOnData(void *, double frameTime) {
  tickVital("health-label", HealthVital, frameTime);
  tickVital("armor-label", ArmorVital, frameTime);

  // The readout has no continuous feed: the Health usermsg only arrives when
  // the server's cached value changes, and the revive sample can be consumed
  // while the readout is still gated, leaving the death value on screen until
  // the next real change (issue 137; the same path produced the transient 0 of
  // issue 120). Reconcile it against the local client-data health every frame:
  // the tracker ignores a repeated value and keeps a running transition, so a
  // settled readout costs one compare and every revive or respawn converges
  // within one frame.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  applyHealth(state, state.health);
}

// Old g_EventHealth handler: while connected and not spectating, a
// non-positive health clamps to 1 (spectator feeds report 0). Shared by the
// usermsg path and the per-frame reconcile, so one sample always renders the
// same way whichever stream delivered it.
void applyHealth(const AbcUiHudState &state, int health) {
  AbcUiPlayerState player{};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);
  if (health <= 0 && player.index != 0 && !state.spectate)
    health = 1;
  applyVital("health-label", "health-bar", "health-icon", health, DangerHealth,
             45.0f, healthShown(state), HealthVital);
}

void setHealth(int health) {
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  applyHealth(state, health);
}

void setArmor(int armor) {
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  applyVital("armor-label", "armor-bar", "armor-icon", armor, DangerArmor,
             45.0f, armorShown(state), ArmorVital);
}

// ---- event sinks ----

void onHealth(void *, const void *payload) {
  setHealth(static_cast<const AbcUiIntEvent *>(payload)->value);
}

void onBattery(void *, const void *payload) {
  setArmor(static_cast<const AbcUiIntEvent *>(payload)->value);
}

void onSuit(void *, const void *) {
  updateVisibility();
}

void onHideToken(void *, const void *) {
  updateVisibility();
}

void onSpectate(void *, const void *) {
  updateVisibility();
}

void onLongJump(void *, const void *payload) {
  setLongJump(static_cast<const AbcUiBoolEvent *>(payload)->value != 0);
}

// ---- panel callbacks ----

void panelDocumentReady(void *) {
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  // A fresh document starts from its placeholders, so the trackers must forget
  // the previous document's widths before the first seed (the HUD cycle runs
  // again per map and the DOM is rebuilt with it).
  HealthVital.Motion.reset();
  ArmorVital.Motion.reset();
  HealthVital.Pop.reset();
  ArmorVital.Pop.reset();
  // Seed from the current snapshot: the change events may have fired before
  // the panels existed (the suit bit arrives on the first client-data frame,
  // ahead of the first HUD_Frame that builds them).
  setHealth(state.health);
  setArmor(state.armor);
  setLongJump(state.longjump != 0);
  updateVisibility();
}

void panelReset(void *) {
  // Old Reset: hide + clear the longjump marker.
  AbcUiDocument *doc = panelDocument();
  if (doc)
    Api->ui->documentHide(doc);
  setLongJump(false);
  // A new map or a reconnect starts from a clean slate: the next shown write
  // snaps, so no width from the previous session animates in.
  HealthVital.Motion.reset();
  ArmorVital.Motion.reset();
  HealthVital.Pop.reset();
  ArmorVital.Pop.reset();
  // The document outlives the reset: an interrupted pop has to be put back on
  // the rest scale, or the value stays enlarged until its next change.
  writeVitalScale(doc, "health-label", 1.0f);
  writeVitalScale(doc, "armor-label", 1.0f);
}

// ---- lifecycle ----

int onNativeHudDraw(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiNativeHudEvent *>(payload);
  return panelDocument() && (event->element == ABCUI_NATIVE_HUD_HEALTH ||
                             event->element == ABCUI_NATIVE_HUD_BATTERY);
}

void onModuleLoad() {
  AbcUiClaimDesc native{};
  native.structSize = sizeof(native);
  native.role = ABCUI_CLAIM_GATE;
  native.canHandle = &onNativeHudDraw;
  if (!Api->claims->claimDomain(ABCUI_CHANNEL_HUD_NATIVE_DRAW, &native))
    Api->core->warn("health: cannot claim the native HUD draw");
  Panel = moduleui::makePanel(kPanelName, kDocumentPath, 0);
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_HEALTH, 0, &onHealth, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_BATTERY, 0, &onBattery, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_SUIT, 0, &onSuit, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_HIDE_TOKEN, 0, &onHideToken,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_SPECTATE, 0, &onSpectate, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_LONG_JUMP, 0, &onLongJump, nullptr);
}

void onModuleInit() {
  DangerHealth =
      Api->console->registerCvar(kDangerHealth, "45", ABCUI_CVAR_VALUE);
  DangerArmor =
      Api->console->registerCvar(kDangerArmor, "45", ABCUI_CVAR_VALUE);
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
  out->id = "abcui.health";
  out->version = "3.0.1";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = nullptr;
  out->onUnload = &onModuleUnload;
  return 1;
}
