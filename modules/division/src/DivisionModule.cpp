// abcui.division - the Division-style third-person HUD group: health, armor,
// ammo and flashlight readouts pinned to the local player model's projected
// head position, with one fixed bottom-centre row when no third-person model
// is visible.
//
// Group rules (issues #15 and #14): one panel document and one pump pair. The
// framework's HUD_Redraw phase is the only place the engine's view matrices
// are live (HudPanel::onRedraw, rmlui::onRedraw), so the anchor is solved
// there while the values refresh in onData. Every DOM write is change-gated
// and the per-frame path allocates nothing: fixed buffers, no std::string.
//
// The panel replaces the classic health/ammo/flashlight readouts in this
// group, so it claims the same native HUD elements and the engine's own
// sprites never double up. It renders numbers and local sprite URIs only - no
// server string reaches the DOM, so no RML escaping is needed here.
#include "../../common/FollowAnchor.h"

#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

constexpr const char *kPanelName = "division";
constexpr const char *kDocumentPath =
    "mod://abcui.division/hud/rml/division.rml";

// Persisted cvar contract shared with the classic HUD modules.
constexpr const char *kFollowCvar = "hud_division_follow";
constexpr const char *kDangerHealth = "hud_danger_health";
constexpr const char *kDangerArmor = "hud_danger_armor";
constexpr int kDefaultDanger = 45;
// hud_division_follow values: 0 fixed, 1 follow in third person, 2 force.
constexpr int kFollowAuto = 1;
constexpr int kFollowForce = 2;

// HUD accent amber (255 176 0, base.rcss --abc-hud-accent) and the danger red
// the hue shift walks toward (the classic panels' ColorUtil inputs).
constexpr float kAccent[3] = {255.0f, 176.0f, 0.0f};
constexpr float kRed[3] = {255.0f, 0.0f, 0.0f};

// Anchor geometry (dp unless stated). The row floats above the projected head
// so it never covers the model.
constexpr float kHeadTopUnits = 4.0f; // world units above the eye
constexpr float kHeadLiftDp = 8.0f;   // gap between the head and the row
constexpr float kFallbackBottomDp = 12.0f;
constexpr float kEdgeMarginDp = 8.0f;
constexpr float kDeadbandPx = 0.5f;    // smallest DOM move worth writing
constexpr double kDwellSeconds = 0.10; // third-person flag dwell

// Row geometry the RCSS draws (fallback when the layout has not measured yet).
constexpr float kClusterWidthDp = 400.0f;
constexpr float kClusterHeightDp = 34.0f;
constexpr float kHealthBarDp = 76.0f;
constexpr float kFlashBarDp = 60.0f;

// Old CAmmoPanel state/flag bits the akimbo fold keys on.
constexpr int32_t kStateUzi = 1 << 2;
constexpr int32_t kUziAkimboFlag = 32;

AbcUiCvarHandle FollowCvar = nullptr;
AbcUiCvarHandle DangerHealthCvar = nullptr;
AbcUiCvarHandle DangerArmorCvar = nullptr;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

AbcUiElement *elementById(AbcUiDocument *doc, const char *id) {
  return doc ? Api->ui->querySelector(doc, id) : nullptr;
}

void setProperty(AbcUiDocument *doc, const char *id, const char *name,
                 const char *value) {
  if (AbcUiElement *element = elementById(doc, id))
    Api->ui->elementSetProperty(element, name, value);
}

void setText(AbcUiDocument *doc, const char *id, const char *text) {
  if (AbcUiElement *element = elementById(doc, id))
    Api->ui->elementSetInnerRML(element, text);
}

void setVisible(AbcUiDocument *doc, const char *id, bool visible) {
  setProperty(doc, id, "visibility", visible ? "visible" : "hidden");
}

void setWidthDp(AbcUiDocument *doc, const char *id, float dp) {
  char buffer[24] = {};
  std::snprintf(buffer, sizeof(buffer), "%gdp", dp);
  setProperty(doc, id, "width", buffer);
}

int cvarInt(AbcUiCvarHandle handle, int fallback) {
  if (!handle)
    return fallback;
  const float value =
      Api->console->cvarValue(handle, static_cast<float>(fallback));
  return static_cast<int>(std::lround(value));
}

// ---------------------------------------------------------------- values ----

struct VitalState {
  int value = -1;
  bool visible = false;
};

struct AmmoState {
  bool visible = false;
  bool hasIcon = false;
  char clip[16] = {};
  char reserve[16] = {};
  char icon[192] = {};
};

struct FlashState {
  bool visible = false;
  int battery = -1;
  bool lit = false;
};

VitalState HealthState;
VitalState ArmorState;
AmmoState Ammo;
FlashState Flash;
bool DocumentShown = false;

void applyVital(AbcUiDocument *doc, const char *slotId, const char *valueId,
                const char *fillId, int value, bool visible, int danger,
                float fullWidthDp, VitalState &state) {
  const bool becameVisible = visible && !state.visible;
  if (state.visible != visible) {
    setVisible(doc, slotId, visible);
    state.visible = visible;
  }
  if (!visible)
    return;
  if (becameVisible)
    state.value = -1;
  if (state.value == value)
    return;
  state.value = value;

  char text[16] = {};
  std::snprintf(text, sizeof(text), "%d", value);
  setText(doc, valueId, text);

  const float ratio =
      std::clamp(static_cast<float>(value) / 100.0f, 0.0f, 1.0f);
  setWidthDp(doc, fillId, fullWidthDp * ratio);

  char color[64] = {};
  if (danger > 0 && value < danger)
    Api->ui->differColor(kAccent, kRed, static_cast<float>(value) / danger,
                         color, sizeof(color));
  else
    Api->ui->formatColor(kAccent, color, sizeof(color));
  setProperty(doc, valueId, "color", color);
  setProperty(doc, fillId, "background-color", color);
}

void applyAmmo(AbcUiDocument *doc, const AbcUiHudState &hud) {
  const bool armed = !(hud.hideBits & ABCUI_HIDEBIT_WEAPONS) &&
                     !(hud.hideBits & ABCUI_HIDEBIT_ALL) && hud.spectate == 0;

  AbcUiWeaponInfo weapon = {};
  weapon.structSize = sizeof(weapon);
  const bool known = armed && Api->weapons->current(&weapon) != 0;

  char clip[16] = {};
  char reserve[16] = {};
  char icon[192] = {};
  bool hasIcon = false;
  bool visible = false;

  if (known) {
    const int primary =
        weapon.ammoType > 0 ? weapon.ammoType : weapon.ammo2Type;
    const int pool = primary > 0 ? Api->weapons->countAmmo(primary) : 0;
    if (primary > 0 || weapon.clip >= 0) {
      visible = true;
      if (weapon.clip >= 0) {
        if ((weapon.state & kStateUzi) && (weapon.flags & kUziAkimboFlag) &&
            weapon.clip2 >= 0)
          std::snprintf(clip, sizeof(clip), "%dx%d", weapon.clip2, weapon.clip);
        else
          std::snprintf(clip, sizeof(clip), "%d", weapon.clip);
        std::snprintf(reserve, sizeof(reserve), "/%d", pool);
      } else {
        std::snprintf(clip, sizeof(clip), "%d", pool);
      }
      if (weapon.ammo.status == ABCUI_OK && weapon.ammo.atlas[0] != '\0') {
        std::snprintf(icon, sizeof(icon), "spricon://%s/%d,%d,%d,%d",
                      weapon.ammo.atlas, weapon.ammo.rect[0],
                      weapon.ammo.rect[1], weapon.ammo.rect[2],
                      weapon.ammo.rect[3]);
        hasIcon = true;
      }
    }
  }

  if (Ammo.visible != visible) {
    setVisible(doc, "ammo", visible);
    Ammo.visible = visible;
  }
  if (!visible)
    return;

  if (std::strcmp(Ammo.clip, clip) != 0) {
    std::snprintf(Ammo.clip, sizeof(Ammo.clip), "%s", clip);
    setText(doc, "ammo-clip", Ammo.clip);
  }
  if (std::strcmp(Ammo.reserve, reserve) != 0) {
    std::snprintf(Ammo.reserve, sizeof(Ammo.reserve), "%s", reserve);
    setText(doc, "ammo-reserve", Ammo.reserve);
  }
  if (Ammo.hasIcon != hasIcon ||
      (hasIcon && std::strcmp(Ammo.icon, icon) != 0)) {
    std::snprintf(Ammo.icon, sizeof(Ammo.icon), "%s", icon);
    Ammo.hasIcon = hasIcon;
    if (AbcUiElement *element = elementById(doc, "ammo-icon"))
      Api->ui->elementSetAttribute(element, "src",
                                   hasIcon ? Ammo.icon : nullptr);
  }
}

void applyFlash(AbcUiDocument *doc, const AbcUiHudState &hud, bool visible) {
  const bool becameVisible = visible && !Flash.visible;
  if (Flash.visible != visible) {
    setVisible(doc, "flash", visible);
    Flash.visible = visible;
  }
  if (!visible)
    return;
  if (becameVisible)
    Flash.battery = -1;

  const int battery = std::clamp(hud.flashBattery, 0, 100);
  const bool lit = hud.flashOn != 0;
  if (Flash.lit != lit) {
    Flash.lit = lit;
    if (AbcUiElement *element = elementById(doc, "flash"))
      Api->ui->elementSetClass(element, "lit", lit ? 1 : 0);
  }
  if (Flash.battery == battery)
    return;
  Flash.battery = battery;

  char text[16] = {};
  std::snprintf(text, sizeof(text), "%d%%", battery);
  setText(doc, "flash-value", text);
  setWidthDp(doc, "flash-fill",
             kFlashBarDp * static_cast<float>(battery) / 100.0f);

  char color[64] = {};
  Api->ui->differColor(kAccent, kRed, static_cast<float>(battery) / 100.0f,
                       color, sizeof(color));
  setProperty(doc, "flash-value", "color", color);
  setProperty(doc, "flash-fill", "background-color", color);
}

// ---------------------------------------------------------------- anchor ----

struct AnchorState {
  moduleui::follow::State Follow;
  float ClusterWidth = 0.0f;
  float ClusterHeight = 0.0f;
  float AppliedX = 0.0f;
  float AppliedY = 0.0f;
  int DocumentWidth = 0;
  int DocumentHeight = 0;
  bool Applied = false;
  bool FollowWas = false;
  bool Armed = false;
  bool SnapNext = true;
  double Dwell = 0.0;
};

AnchorState Anchor;

bool followDesired(const AbcUiHudState &hud, const AbcUiPlayerState &player) {
  if (player.index == 0)
    return false;
  if (hud.spectate != 0)
    return false;
  const int mode = cvarInt(FollowCvar, kFollowAuto);
  if (mode <= 0)
    return false;
  if (mode >= kFollowForce)
    return true;
  return Api->data->thirdPerson() != 0;
}

void updateDwell(bool desired, double frameTime) {
  if (!desired) {
    Anchor.Armed = false;
    Anchor.Dwell = 0.0;
    return;
  }
  if (Anchor.Armed) {
    Anchor.Dwell = 0.0;
    return;
  }
  Anchor.Dwell += frameTime;
  if (Anchor.Dwell < kDwellSeconds)
    return;
  Anchor.Armed = true;
  Anchor.Dwell = 0.0;
  Anchor.SnapNext = true;
}

void measureCluster(AbcUiDocument *doc) {
  AbcUiElement *cluster = elementById(doc, "cluster");
  if (!cluster)
    return;
  float width = 0.0f;
  float height = 0.0f;
  if (Api->ui->elementOffsetSize(cluster, &width, &height) && width > 1.0f &&
      height > 1.0f) {
    Anchor.ClusterWidth = width;
    Anchor.ClusterHeight = height;
  }
}

// ------------------------------------------------------------ lifecycle ----

void panelOnData(void *, double) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;

  AbcUiHudState hud = {};
  hud.structSize = sizeof(hud);
  Api->data->hudState(&hud);

  // Classic gate set: the suit owns the vitals, the hide-all token hides the
  // whole HUD and an observer is served by the spectator panel.
  const bool session =
      hud.suit != 0 && hud.spectate == 0 && !(hud.hideBits & ABCUI_HIDEBIT_ALL);
  const bool healthVisible = session && !(hud.hideBits & ABCUI_HIDEBIT_HEALTH);
  const bool armorVisible = session && !(hud.hideBits & ABCUI_HIDEBIT_BATTERY);
  const bool flashVisible =
      session && !(hud.hideBits & ABCUI_HIDEBIT_FLASHLIGHT);

  applyVital(doc, "health", "health-value", "health-fill", hud.health,
             healthVisible, cvarInt(DangerHealthCvar, kDefaultDanger),
             kHealthBarDp, HealthState);
  applyVital(doc, "armor", "armor-value", "armor-fill", hud.armor, armorVisible,
             cvarInt(DangerArmorCvar, kDefaultDanger), kHealthBarDp,
             ArmorState);
  applyAmmo(doc, hud);
  applyFlash(doc, hud, flashVisible);

  const bool anyVisible =
      healthVisible || armorVisible || Ammo.visible || flashVisible;
  if (DocumentShown != anyVisible) {
    DocumentShown = anyVisible;
    if (anyVisible)
      Api->ui->documentShow(doc);
    else
      Api->ui->documentHide(doc);
  }
}

void panelOnRedraw(void *, double frameTime) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  int width = 0;
  int height = 0;
  if (!Api->ui->documentSize(doc, &width, &height) || width <= 0 || height <= 0)
    return;
  if (width != Anchor.DocumentWidth || height != Anchor.DocumentHeight) {
    Anchor.DocumentWidth = width;
    Anchor.DocumentHeight = height;
    Anchor.ClusterWidth = 0.0f; // the dp scale changed: measure again
    Anchor.SnapNext = true;
  }
  if (Anchor.ClusterWidth <= 1.0f)
    measureCluster(doc);

  const float scale = Api->ui->scale();
  const float clusterWidth = Anchor.ClusterWidth > 1.0f
                                 ? Anchor.ClusterWidth
                                 : kClusterWidthDp * scale;
  const float clusterHeight = Anchor.ClusterHeight > 1.0f
                                  ? Anchor.ClusterHeight
                                  : kClusterHeightDp * scale;

  moduleui::follow::Limits limits;
  limits.Width = static_cast<float>(width);
  limits.Height = static_cast<float>(height);
  limits.Margin = kEdgeMarginDp * scale;

  AbcUiHudState hud = {};
  hud.structSize = sizeof(hud);
  Api->data->hudState(&hud);
  AbcUiPlayerState player = {};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);

  updateDwell(followDesired(hud, player), frameTime);

  bool projected = false;
  float headX = 0.0f;
  float headY = 0.0f;
  if (Anchor.Armed && player.index != 0) {
    float eyeHeight[3] = {};
    Api->data->viewHeight(eyeHeight);
    const float head[3] = {player.origin[0] + eyeHeight[0],
                           player.origin[1] + eyeHeight[1],
                           player.origin[2] + eyeHeight[2] + kHeadTopUnits};
    float screen[2] = {};
    if (Api->ui->project(head, screen) == 0) {
      projected = true;
      headX = screen[0];
      headY = screen[1];
    }
  }

  const bool followNow = Anchor.Armed && projected;
  float targetX = (static_cast<float>(width) - clusterWidth) * 0.5f;
  float targetY =
      static_cast<float>(height) - kFallbackBottomDp * scale - clusterHeight;
  if (followNow) {
    targetX = headX - clusterWidth * 0.5f;
    targetY = headY - kHeadLiftDp * scale - clusterHeight;
  }

  const bool snap = Anchor.SnapNext || followNow != Anchor.FollowWas;
  float position[2] = {};
  moduleui::follow::step(Anchor.Follow, targetX, targetY, clusterWidth,
                         clusterHeight, limits, static_cast<float>(frameTime),
                         snap, position);
  Anchor.SnapNext = false;
  Anchor.FollowWas = followNow;

  if (!Anchor.Applied ||
      std::fabs(position[0] - Anchor.AppliedX) >= kDeadbandPx ||
      std::fabs(position[1] - Anchor.AppliedY) >= kDeadbandPx) {
    char left[24] = {};
    char top[24] = {};
    std::snprintf(left, sizeof(left), "%.1fpx", position[0]);
    std::snprintf(top, sizeof(top), "%.1fpx", position[1]);
    setProperty(doc, "cluster", "left", left);
    setProperty(doc, "cluster", "top", top);
    Anchor.Applied = true;
    Anchor.AppliedX = position[0];
    Anchor.AppliedY = position[1];
  }
}

void panelReset(void *) {
  if (AbcUiDocument *doc = panelDocument())
    Api->ui->documentHide(doc);
  DocumentShown = false;
  Anchor = {};
  Anchor.SnapNext = true;
  // Force the next show to rewrite every readout.
  HealthState = {};
  ArmorState = {};
  Ammo = {};
  Flash = {};
}

void panelDocumentReady(void *) {
  // Seed from the live snapshot: the change events may have fired before the
  // document existed (same seeding the classic panels do).
  panelOnData(nullptr, 0.0);
  Anchor = {};
  Anchor.SnapNext = true;
}

int onNativeHudDraw(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiNativeHudEvent *>(payload);
  if (!event || !panelDocument())
    return 0;
  return event->element == ABCUI_NATIVE_HUD_HEALTH ||
         event->element == ABCUI_NATIVE_HUD_BATTERY ||
         event->element == ABCUI_NATIVE_HUD_AMMO ||
         event->element == ABCUI_NATIVE_HUD_FLASHLIGHT;
}

void onModuleLoad() {
  Api->events->subscribeVeto(ABCUI_CHANNEL_HUD_NATIVE_DRAW, 0, &onNativeHudDraw,
                             nullptr);

  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = &panelOnData;
  Panel.onRedraw = &panelOnRedraw;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);
}

void onModuleInit() {
  FollowCvar = Api->console->registerCvar(kFollowCvar, "1", ABCUI_CVAR_VALUE);
  // The classic health module owns these two in its own group; share them
  // when it is loaded, otherwise keep the same defaults so switching groups
  // does not reset the user's thresholds.
  DangerHealthCvar = Api->console->findCvar(kDangerHealth);
  if (!DangerHealthCvar)
    DangerHealthCvar =
        Api->console->registerCvar(kDangerHealth, "45", ABCUI_CVAR_VALUE);
  DangerArmorCvar = Api->console->findCvar(kDangerArmor);
  if (!DangerArmorCvar)
    DangerArmorCvar =
        Api->console->registerCvar(kDangerArmor, "45", ABCUI_CVAR_VALUE);
}

void onModuleVidInit() {
  Anchor.ClusterWidth = 0.0f;
  Anchor.ClusterHeight = 0.0f;
  Anchor.DocumentWidth = 0;
  Anchor.DocumentHeight = 0;
  Anchor.Applied = false;
  Anchor.SnapNext = true;
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
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data || !api->weapons)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->events->structSize < sizeof(AbcUiEventsApi) ||
      api->data->structSize < sizeof(AbcUiDataApi) ||
      api->weapons->structSize < sizeof(AbcUiWeaponsApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.division";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = &onModuleVidInit;
  out->onShutdown = nullptr;
  out->onUnload = &onModuleUnload;
  return 1;
}
