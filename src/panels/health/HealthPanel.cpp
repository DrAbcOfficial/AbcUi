// panels::HealthPanel implementation - see HealthPanel.h.
// from: ABCEnchance/src/hud/vgui/health.* (GBK->UTF-8).
#include <metahook.h>

#include "HealthPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

#include <abc/CvarCmd.h>

#include <string>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../framework/ColorUtil.h"
#include "../../framework/GlyphText.h"

namespace panels {

namespace health {

// Old CHealthPanel ctor defaults (persisted contract - never rename).
cvar_t *DangerHealth = nullptr;
cvar_t *DangerArmor = nullptr;

void registerCvars() {
  DangerHealth =
      abc::CREATE_CVAR("hud_danger_health", "45", abc::FCVAR_VALUE, nullptr);
  DangerArmor =
      abc::CREATE_CVAR("hud_danger_armor", "45", abc::FCVAR_VALUE, nullptr);
}

} // namespace health

namespace {

// HUD accent amber (255 176 0, base.rcss --abc-hud-accent) and the scheme
// "Red" danger colour (255 0 0). The old scheme's "Default" green was
// retuned to the Half-Life amber; the hue-shift path is unchanged.
constexpr float kAccent[3] = {255.0f, 176.0f, 0.0f};
constexpr float kRed[3] = {255.0f, 0.0f, 0.0f};
// Bar geometry (health.rcss meter): 92dp full width.
constexpr float kBarWidth = 92.0f;

} // namespace

HealthPanel::HealthPanel()
    : HudPanel("health", "abc/abcui/hud/rml/health.rml") {}

void HealthPanel::onDocumentReady() {
  setHealth(hudstate::messages().Health);
  setArmor(hudstate::messages().Armor);
  setLongJump(hudstate::hasLongJump());
  subscribe(netmsg::Health, [this](int value) { setHealth(value); });
  subscribe(netmsg::Battery, [this](int value) { setArmor(value); });
  subscribe(hudstate::SuitChanged, [this](bool) { updateVisibility(); });
  subscribe(hudstate::HideTokenChanged, [this](int) { updateVisibility(); });
  subscribe(hudstate::SpectateChanged, [this](bool) { updateVisibility(); });
  subscribe(hudstate::LongJumpChanged,
            [this](bool longJump) { setLongJump(longJump); });
  // The change events may have fired before the panels existed (the suit
  // bit arrives on the first client-data frame, ahead of the first
  // HUD_Frame that builds them) - evaluate the current state once here.
  updateVisibility();
}

void HealthPanel::reset() {
  // Old Reset: hide + clear the longjump marker.
  hide();
  setLongJump(false);
}

void HealthPanel::updateVisibility() {
  const bool hideHealth = hudstate::isHudHidden(hudstate::kHideHealth);
  const bool hideBattery = hudstate::isHudHidden(hudstate::kHideBattery);
  // kHideAll = the old Viewport-wide SetVisible(false) (hud hide-all token
  // from scripted sequences); the group split stays independent of it.
  const bool visible = hudstate::hasSuit() && (!hideBattery || !hideHealth) &&
                       !hudstate::isSpectate() &&
                       !hudstate::isHudHidden(hudstate::kHideAll);
  if (visible)
    show();
  else
    hide();
  // Old SetHealthVisible/SetArmorVisible element groups.
  const char *healthGroup[] = {"health-icon", "health-label", "health-bar-bg",
                               "health-bar-wrap"};
  const char *armorGroup[] = {"armor-icon", "armor-label", "armor-bar-bg",
                              "armor-bar-wrap"};
  for (const char *id : healthGroup) {
    if (Rml::Element *el = element(id))
      el->SetProperty("visibility", hideHealth ? "hidden" : "visible");
  }
  for (const char *id : armorGroup) {
    if (Rml::Element *el = element(id))
      el->SetProperty("visibility", hideBattery ? "hidden" : "visible");
  }
}

void HealthPanel::setHealth(int health) {
  // Old g_EventHealth handler: while connected and not spectating, a
  // non-positive health clamps to 1 (spectator feeds report 0).
  if (health <= 0) {
    const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
    if (local && !hudstate::isSpectate())
      health = 1;
  }
  applyVital("health-label", "health-bar", "health-icon", "health-bar-wrap",
             health,
             health::DangerHealth ? health::DangerHealth->value : 45.0f);
}

void HealthPanel::setArmor(int armor) {
  applyVital("armor-label", "armor-bar", "armor-icon", "armor-bar-wrap",
             armor,
             health::DangerArmor ? health::DangerArmor->value : 45.0f);
}

Rml::String HealthPanel::dangerColor(int value, float dangerValue) const {
  float rgb[3];
  if (dangerValue > 0.0f && value < dangerValue) {
    // Old GetDifferColor: hue pulled toward the danger red as the value
    // falls below the threshold (ratio = value / danger).
    framework::ColorUtil::differColor(kAccent, kRed, value / dangerValue, rgb);
  } else {
    rgb[0] = kAccent[0];
    rgb[1] = kAccent[1];
    rgb[2] = kAccent[2];
  }
  return framework::ColorUtil::format(rgb);
}

void HealthPanel::applyVital(const char *labelId, const char *barId,
                             const char *iconId, const char *wrapId, int value,
                             float dangerValue) {
  const Rml::String color = dangerColor(value, dangerValue);
  if (Rml::Element *label = element(labelId)) {
    // Additive glyph ink (framework::glyphs): the texadd:// images are what
    // make the value glow like the sprite icons, so no outline is needed.
    // The text fallback keeps the readout visible if a glyph is missing.
    const std::string text = std::to_string(value);
    const std::string ink = framework::glyphs::markup(text, color.c_str());
    label->SetInnerRML(ink.empty() ? text : ink);
    label->SetProperty("color", color);
  }
  // The meter fill is the additive strip, the icon a tinted sprite; the
  // danger rule retints both through image-color.
  if (Rml::Element *bar = element(barId))
    bar->SetProperty("image-color", color);
  if (Rml::Element *icon = element(iconId))
    icon->SetProperty("image-color", color);
  if (Rml::Element *wrap = element(wrapId)) {
    const float pct = std::clamp(value / 100.0f, 0.0f, 1.0f);
    wrap->SetProperty("width",
                      Rml::String(std::to_string(pct * kBarWidth)) + "dp");
  }
}

void HealthPanel::setLongJump(bool longJump) {
  if (Rml::Element *icon = element("longjump"))
    icon->SetProperty("visibility", longJump ? "visible" : "hidden");
}

} // namespace panels
