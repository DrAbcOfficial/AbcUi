// abcui.division - the Division-style third-person HUD group: one compact
// plate carried at the local player model's hip.
//
// Layout (top to bottom): a white segmented health bar and a thinner orange
// armour bar, each cut into 10-point cells whose width follows the local
// player's maximum (so a 150 HP or an 80 HP maximum still fills the bar); an
// ammo line at the far left with the primary/secondary magazine and their
// reserves, followed by the flashlight battery and the longjump module state.
// First person, spectating and a failed projection return the plate to a
// fixed bottom-left anchor.
//
// Group rules (issues #15 and #14): one panel document and one pump pair. The
// framework's HUD_Redraw phase is the only place the engine's view matrices
// are live (HudPanel::onRedraw, rmlui::onRedraw), so the anchor is solved
// there while the values refresh in onData. Every DOM write is change-gated
// and the per-frame path allocates nothing.
//
// The maximum health/armour are not exposed by the client data, so 100 is the
// baseline: the module tracks the highest value observed since the last map
// change and grows the bar with more cells only above 100. A maximum below
// 100 needs no special case - the bar simply reads as partly filled.
//
// The panel replaces the classic health/ammo/flashlight readouts in this
// group, so it claims the same native HUD elements and the engine's own
// sprites never double up. It renders numbers and local sprite URIs only - no
// server string reaches the DOM, so no RML escaping is needed here.
#include "../../common/BulletFx.h"
#include "../../common/BulletLayer.h"
#include "../../common/FollowAnchor.h"
#include "../../common/SegmentBar.h"

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
constexpr const char *kSideCvar = "hud_division_side";
// hud_division_side values: 0 = left of the character, 1 = right.
constexpr int kSideLeft = 0;
constexpr const char *kDangerHealth = "hud_danger_health";
constexpr const char *kDangerArmor = "hud_danger_armor";
constexpr int kDefaultDanger = 45;
// hud_division_follow values: 0 fixed, 1 follow in third person, 2 force.
constexpr int kFollowAuto = 1;
constexpr int kFollowForce = 2;

// HUD accent amber (255 176 0, base.rcss --abc-hud-accent), the danger red the
// hue shift walks toward, and the white the health bar and numerals use.
constexpr float kAccent[3] = {255.0f, 176.0f, 0.0f};
constexpr float kRed[3] = {255.0f, 0.0f, 0.0f};
constexpr float kNumeral[3] = {243.0f, 245.0f, 246.0f};

// Placement (dp unless stated): the plate sits to the left of the projected
// player origin (the hull centre, i.e. hip height) with a fixed gap, and falls
// back to the free bottom-left corner when there is no third-person model.
constexpr float kHipOffsetDp = 24.0f;
constexpr float kFallbackLeftDp = 18.0f;
constexpr float kFallbackBottomDp = 12.0f;
constexpr float kEdgeMarginDp = 8.0f;
constexpr float kDeadbandPx = 0.5f;    // smallest DOM move worth writing
constexpr double kDwellSeconds = 0.10; // third-person flag dwell

// Plate geometry the RCSS draws (fallback when the layout has not measured).
constexpr float kClusterWidthDp = 150.0f;
constexpr float kClusterHeightDp = 40.0f;
constexpr float kHealthBarDp = 112.0f;
constexpr float kArmorBarDp = 112.0f;
constexpr float kFlashBarDp = 12.0f;
constexpr float kCellGapDp = 1.0f;

// Element ids per readout: a hidden readout keeps its boxes (visibility), so
// the plate width and the measured anchor never drift.
constexpr const char *kHealthIds[] = {"health-cells", "health-value"};
constexpr const char *kArmorIds[] = {"armor-cells", "armor-value"};
constexpr const char *kAmmoIds[] = {"ammo-row1", "ammo-row2"};
constexpr const char *kFlashIds[] = {"flash-battery", "flash-value"};
constexpr const char *kLongJumpIds[] = {"longjump"};

AbcUiCvarHandle FollowCvar = nullptr;
AbcUiCvarHandle SideCvar = nullptr;
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

void setGroupVisible(AbcUiDocument *doc, const char *const *ids, size_t count,
                     bool visible) {
  for (size_t i = 0; i < count; ++i)
    setVisible(doc, ids[i], visible);
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

struct BarState {
  int value = -1;
  int cells = 0;
  float cellWidth = 0.0f;
  bool visible = false;
};

struct AmmoState {
  bool visible = false;
  bool hasSecond = false;
  bool hasReserve1 = false;
  bool hasReserve2 = false;
  bool hasIcon = false;
  bool hasIcon2 = false;
  int clip1 = -1;
  int clip2 = -1;
  int reserve1 = -1;
  int reserve2 = -1;
  // Weapon identity and the previous magazines, for the fired-round check.
  int weaponSlot = -1;
  int weaponPos = -1;
  int lastClip1 = -1;
  int lastClip2 = -1;
  char icon[192] = {};
  char icon2[192] = {};
};

struct FlashState {
  bool visible = false;
  int battery = -1;
  bool lit = false;
};

struct LongJumpState {
  bool visible = false;
};

BarState HealthBar;
BarState ArmorBar;
AmmoState Ammo;
moduleui::bulletfx::Bullets Bullets;
FlashState Flash;
LongJumpState LongJump;
int PeakHealth = 0;
int PeakArmor = 0;
bool DocumentShown = false;

// The cells are appended once per document; the bars then only reposition
// them and set the fill widths (no per-frame DOM construction).
void buildCells(AbcUiDocument *doc) {
  static constexpr char kCell[] =
      "<div class=\"cell\"><div class=\"fill\"></div></div>";
  char markup[4096] = {};
  size_t used = 0;
  for (int i = 0; i < moduleui::segment::kMaxCells; ++i) {
    if (used + sizeof(kCell) >= sizeof(markup))
      break;
    std::memcpy(markup + used, kCell, sizeof(kCell) - 1);
    used += sizeof(kCell) - 1;
  }
  markup[used] = '\0';
  if (AbcUiElement *health = elementById(doc, "health-cells"))
    Api->ui->elementSetInnerRML(health, markup);
  if (AbcUiElement *armor = elementById(doc, "armor-cells"))
    Api->ui->elementSetInnerRML(armor, markup);
}

// One segmented bar: the cell count follows the maximum (10 points per cell),
// the bar length stays fixed and the cells share it, and the last cell fills
// partially when the value is not a multiple of ten.
void applyBar(AbcUiDocument *doc, const char *containerId, const char *valueId,
              int value, int maximum, bool visible, float barWidthDp,
              const float normalColor[3], int danger, BarState &state) {
  const bool becameVisible = visible && !state.visible;
  if (state.visible != visible) {
    setVisible(doc, containerId, visible);
    setVisible(doc, valueId, visible);
    state.visible = visible;
  }
  if (!visible)
    return;

  const int cells = moduleui::segment::cellCount(maximum);
  if (cells != state.cells) {
    const float cellWidth =
        moduleui::segment::cellWidth(barWidthDp, kCellGapDp, cells);
    AbcUiElement *container = elementById(doc, containerId);
    for (int i = 0; i < moduleui::segment::kMaxCells; ++i) {
      AbcUiElement *cell =
          container ? Api->ui->elementChild(container, i) : nullptr;
      if (!cell)
        continue;
      const bool drawn = i < cells;
      Api->ui->elementSetProperty(cell, "display", drawn ? "block" : "none");
      if (!drawn)
        continue;
      char left[24] = {};
      char width[24] = {};
      std::snprintf(left, sizeof(left), "%gdp",
                    static_cast<float>(i) * (cellWidth + kCellGapDp));
      std::snprintf(width, sizeof(width), "%gdp", cellWidth);
      Api->ui->elementSetProperty(cell, "left", left);
      Api->ui->elementSetProperty(cell, "width", width);
    }
    state.cells = cells;
    state.cellWidth = cellWidth;
  }

  if (state.value == value && !becameVisible)
    return;
  state.value = value;

  char text[16] = {};
  std::snprintf(text, sizeof(text), "%d", value);
  setText(doc, valueId, text);

  char color[64] = {};
  const bool dangerHit = danger > 0 && value < danger;
  if (dangerHit)
    Api->ui->differColor(kAccent, kRed, static_cast<float>(value) / danger,
                         color, sizeof(color));
  else
    Api->ui->formatColor(normalColor, color, sizeof(color));
  setProperty(doc, valueId, "color", color);

  AbcUiElement *container = elementById(doc, containerId);
  for (int i = 0; i < state.cells; ++i) {
    AbcUiElement *cell =
        container ? Api->ui->elementChild(container, i) : nullptr;
    AbcUiElement *fill = cell ? Api->ui->elementChild(cell, 0) : nullptr;
    if (!fill)
      continue;
    char width[24] = {};
    std::snprintf(width, sizeof(width), "%gdp",
                  moduleui::segment::fillWidth(value, i, state.cellWidth));
    Api->ui->elementSetProperty(fill, "width", width);
  }
}

// One ammo sprite as a spricon:// URI; false when the weapon has no usable
// sprite for that slot.
bool spriteUri(const AbcUiWeaponSprite &sprite, char *out, size_t size) {
  if (sprite.status != ABCUI_OK || sprite.atlas[0] == '\0')
    return false;
  std::snprintf(out, size, "spricon://%s/%d,%d,%d,%d", sprite.atlas,
                sprite.rect[0], sprite.rect[1], sprite.rect[2], sprite.rect[3]);
  return true;
}

// One fired round: a copy of the magazine's sprite falling from its icon.
void fireRound(AbcUiDocument *doc, const char *iconId, const char *uri) {
  AbcUiElement *icon = elementById(doc, iconId);
  if (!icon || !uri || !*uri)
    return;
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  if (!Api->ui->elementAbsoluteOffset(icon, &x, &y))
    return;
  Api->ui->elementOffsetSize(icon, &width, &height);

  // The round is a copy of the icon itself: same centre, same size, so it
  // appears to leave the magazine instead of floating next to it.
  const std::size_t slot = Bullets.fire({x + width * 0.5f, y + height * 0.5f});
  AbcUiElement *layer = elementById(doc, "bullets");
  if (!layer)
    return;
  AbcUiElement *element =
      Api->ui->elementChild(layer, static_cast<int32_t>(slot));
  if (!element)
    return;

  char sizeWidth[24] = {};
  char sizeHeight[24] = {};
  std::snprintf(sizeWidth, sizeof(sizeWidth), "%.1fpx", width);
  std::snprintf(sizeHeight, sizeof(sizeHeight), "%.1fpx", height);
  Api->ui->elementSetProperty(element, "width", sizeWidth);
  Api->ui->elementSetProperty(element, "height", sizeHeight);
  Api->ui->elementSetAttribute(element, "src", uri);
}

void updateBullets(AbcUiDocument *doc, double frameTime) {
  if (!Bullets.update(static_cast<float>(frameTime)))
    return;
  if (AbcUiElement *layer = elementById(doc, "bullets"))
    moduleui::bulletfx::paint(Api, layer, Bullets);
}

void applyAmmo(AbcUiDocument *doc, const AbcUiHudState &hud) {
  const bool armed = !(hud.hideBits & ABCUI_HIDEBIT_WEAPONS) &&
                     !(hud.hideBits & ABCUI_HIDEBIT_ALL) && hud.spectate == 0;

  AbcUiWeaponInfo weapon = {};
  weapon.structSize = sizeof(weapon);
  const bool known = armed && Api->weapons->current(&weapon) != 0;

  const int pool1 =
      weapon.ammoType > 0 ? Api->weapons->countAmmo(weapon.ammoType) : -1;
  // A second pool only counts when it is a different ammo type: the akimbo
  // case ships a second magazine but shares the primary pool.
  const bool distinctSecond =
      weapon.ammo2Type > 0 && weapon.ammo2Type != weapon.ammoType;
  const int pool2 =
      distinctSecond ? Api->weapons->countAmmo(weapon.ammo2Type) : -1;

  const bool visible = known && (weapon.clip >= 0 || weapon.clip2 >= 0 ||
                                 pool1 >= 0 || pool2 >= 0);
  // Row two carries the second magazine: the akimbo clip when the ammo type is
  // shared, or the distinct secondary pool.
  const bool hasSecond = visible && (weapon.clip2 >= 0 || pool2 >= 0);
  const bool hasReserve1 = visible && weapon.clip >= 0 && pool1 >= 0;
  const bool hasReserve2 = visible && weapon.clip2 >= 0 && pool2 >= 0;

  const int clip1 = weapon.clip >= 0 ? weapon.clip : pool1;
  const int reserve1 = hasReserve1 ? pool1 : 0;
  const int clip2 = weapon.clip2 >= 0 ? weapon.clip2 : pool2;
  const int reserve2 = hasReserve2 ? pool2 : 0;

  if (Ammo.visible != visible) {
    setGroupVisible(doc, kAmmoIds, std::size(kAmmoIds), visible);
    Ammo.visible = visible;
    if (visible) {
      // The group write above turns every ammo slot on; restore the per-slot
      // state so a hidden second row never flashes in.
      setVisible(doc, "ammo-row2", Ammo.hasSecond);
      setVisible(doc, "ammo1", Ammo.hasReserve1);
      setVisible(doc, "ammo2", Ammo.hasReserve2);
    }
  }
  if (!visible)
    return;

  if (Ammo.hasSecond != hasSecond) {
    Ammo.hasSecond = hasSecond;
    setVisible(doc, "ammo-row2", hasSecond);
    // A single row is centred in the ammo band instead of leaving the second
    // row's space empty; the flashlight and longjump marks follow it.
    setProperty(doc, "ammo-row1", "top", hasSecond ? "13dp" : "20dp");
    setProperty(doc, "flash-battery", "top", hasSecond ? "20dp" : "27dp");
    setProperty(doc, "flash-value", "top", hasSecond ? "15dp" : "22dp");
    setProperty(doc, "longjump", "top", hasSecond ? "15dp" : "22dp");
  }
  if (Ammo.hasReserve1 != hasReserve1) {
    Ammo.hasReserve1 = hasReserve1;
    setVisible(doc, "ammo1", hasReserve1);
  }
  if (Ammo.hasReserve2 != hasReserve2) {
    Ammo.hasReserve2 = hasReserve2;
    setVisible(doc, "ammo2", hasReserve2);
  }

  if (Ammo.clip1 != clip1) {
    Ammo.clip1 = clip1;
    char text[16] = {};
    std::snprintf(text, sizeof(text), "%d", clip1);
    setText(doc, "clip1", text);
  }
  if (hasSecond && Ammo.clip2 != clip2) {
    Ammo.clip2 = clip2;
    char text[16] = {};
    std::snprintf(text, sizeof(text), "%d", clip2);
    setText(doc, "clip2", text);
  }
  if (hasReserve1 && Ammo.reserve1 != reserve1) {
    Ammo.reserve1 = reserve1;
    char text[16] = {};
    std::snprintf(text, sizeof(text), "%d", reserve1);
    setText(doc, "ammo1", text);
  }
  if (hasReserve2 && Ammo.reserve2 != reserve2) {
    Ammo.reserve2 = reserve2;
    char text[16] = {};
    std::snprintf(text, sizeof(text), "%d", reserve2);
    setText(doc, "ammo2", text);
  }

  char icon[192] = {};
  const bool hasIcon = spriteUri(weapon.ammo, icon, sizeof(icon));
  if (Ammo.hasIcon != hasIcon ||
      (hasIcon && std::strcmp(Ammo.icon, icon) != 0)) {
    std::snprintf(Ammo.icon, sizeof(Ammo.icon), "%s", icon);
    Ammo.hasIcon = hasIcon;
    if (AbcUiElement *element = elementById(doc, "ammo-icon"))
      Api->ui->elementSetAttribute(element, "src",
                                   hasIcon ? Ammo.icon : nullptr);
  }

  // The second row's sprite: the weapon's "ammo2" slot, falling back to the
  // primary sprite when the registry has no separate one.
  char icon2[192] = {};
  bool hasIcon2 = hasSecond && spriteUri(weapon.ammo2, icon2, sizeof(icon2));
  if (hasSecond && !hasIcon2) {
    std::snprintf(icon2, sizeof(icon2), "%s", icon);
    hasIcon2 = hasIcon;
  }
  if (Ammo.hasIcon2 != hasIcon2 ||
      (hasIcon2 && std::strcmp(Ammo.icon2, icon2) != 0)) {
    std::snprintf(Ammo.icon2, sizeof(Ammo.icon2), "%s", icon2);
    Ammo.hasIcon2 = hasIcon2;
    if (AbcUiElement *element = elementById(doc, "ammo2-icon"))
      Api->ui->elementSetAttribute(element, "src",
                                   hasIcon2 ? Ammo.icon2 : nullptr);
  }

  // A shot shows as a round of that magazine's sprite falling from its icon.
  // A weapon change only resets the counters, so switching guns never fires.
  const bool sameWeapon = known && weapon.slot == Ammo.weaponSlot &&
                          weapon.slotPos == Ammo.weaponPos;
  if (sameWeapon && Ammo.lastClip1 >= 0 && clip1 < Ammo.lastClip1) {
    const int fired = std::min(Ammo.lastClip1 - clip1, 3);
    for (int i = 0; i < fired; ++i)
      fireRound(doc, "ammo-icon", Ammo.icon);
  }
  if (sameWeapon && hasSecond && Ammo.lastClip2 >= 0 &&
      clip2 < Ammo.lastClip2) {
    const int fired = std::min(Ammo.lastClip2 - clip2, 3);
    for (int i = 0; i < fired; ++i)
      fireRound(doc, "ammo2-icon", Ammo.icon2);
  }
  Ammo.weaponSlot = known ? weapon.slot : -1;
  Ammo.weaponPos = known ? weapon.slotPos : -1;
  Ammo.lastClip1 = visible ? clip1 : -1;
  Ammo.lastClip2 = (visible && hasSecond) ? clip2 : -1;
}

void applyFlash(AbcUiDocument *doc, const AbcUiHudState &hud, bool visible) {
  const bool becameVisible = visible && !Flash.visible;
  if (Flash.visible != visible) {
    setGroupVisible(doc, kFlashIds, std::size(kFlashIds), visible);
    Flash.visible = visible;
  }
  if (!visible)
    return;
  if (becameVisible)
    Flash.battery = -1;

  const int battery = std::clamp(hud.flashBattery, 0, 100);
  if (Flash.battery == battery)
    return;
  Flash.battery = battery;

  char text[16] = {};
  std::snprintf(text, sizeof(text), "%d%%", battery);
  setText(doc, "flash-value", text);
  setWidthDp(doc, "flash-fill",
             kFlashBarDp * static_cast<float>(battery) / 100.0f);
}

void applyLongJump(AbcUiDocument *doc, const AbcUiHudState &hud, bool session) {
  const bool visible = session && hud.longjump != 0;
  if (LongJump.visible == visible)
    return;
  LongJump.visible = visible;
  setGroupVisible(doc, kLongJumpIds, std::size(kLongJumpIds), visible);
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
  // Third-person lean, expressed as the player's normalised horizontal offset
  // from the camera axis (-1..1); 0 keeps the plate flat.
  float Tilt = 0.0f;
  bool TiltApplied = false;
  // hud_division_side: whether the plate currently floats right of the model.
  bool SideRight = false;
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

void panelOnData(void *, double frameTime) {
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

  if (hud.health > PeakHealth)
    PeakHealth = hud.health;
  if (hud.armor > PeakArmor)
    PeakArmor = hud.armor;

  // 100 is the baseline maximum: a higher value seen adds cells, a lower one
  // simply reads as a partly filled bar.
  const int maxHealth = std::max(PeakHealth, 100);
  const int maxArmor = std::max(PeakArmor, 100);

  applyBar(doc, "health-cells", "health-value", hud.health, maxHealth,
           healthVisible, kHealthBarDp, kNumeral,
           cvarInt(DangerHealthCvar, kDefaultDanger), HealthBar);
  applyBar(doc, "armor-cells", "armor-value", hud.armor, maxArmor, armorVisible,
           kArmorBarDp, kAccent, cvarInt(DangerArmorCvar, kDefaultDanger),
           ArmorBar);
  applyAmmo(doc, hud);
  applyFlash(doc, hud, flashVisible);
  applyLongJump(doc, hud, session);

  const bool anyVisible = healthVisible || armorVisible || Ammo.visible ||
                          flashVisible || LongJump.visible;
  if (DocumentShown != anyVisible) {
    DocumentShown = anyVisible;
    if (anyVisible)
      Api->ui->documentShow(doc);
    else
      Api->ui->documentHide(doc);
  }

  updateBullets(doc, frameTime);
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
  float hipX = 0.0f;
  float hipY = 0.0f;
  if (Anchor.Armed && player.index != 0) {
    // The player origin is the hull centre, i.e. hip height: the plate's
    // vertical centre rides it, its right edge keeps a fixed gap to the model.
    float screen[2] = {};
    if (Api->ui->project(player.origin, screen) == 0) {
      projected = true;
      hipX = screen[0];
      hipY = screen[1];
    }
  }

  const bool followNow = Anchor.Armed && projected;

  // hud_division_side picks the side the plate floats on. Flipping it
  // re-pivots the lean at the other edge and snaps the plate, so it never
  // slides through the character on the way over.
  const bool rightSide = cvarInt(SideCvar, kSideLeft) != kSideLeft;
  if (Anchor.SideRight != rightSide) {
    Anchor.SideRight = rightSide;
    setProperty(doc, "cluster", "transform-origin",
                rightSide ? "0% 50%" : "100% 50%");
    Anchor.TiltApplied = false;
    Anchor.SnapNext = true;
  }

  float targetX = rightSide ? static_cast<float>(width) -
                                  kFallbackLeftDp * scale - clusterWidth
                            : kFallbackLeftDp * scale;
  float targetY =
      static_cast<float>(height) - kFallbackBottomDp * scale - clusterHeight;
  if (followNow) {
    targetX = rightSide ? hipX + kHipOffsetDp * scale
                        : hipX - clusterWidth - kHipOffsetDp * scale;
    targetY = hipY - clusterHeight * 0.5f;
  }

  // Third person only: the plate leans like a world-anchored panel. The lean
  // follows the player's horizontal offset from the camera axis, so the panel
  // reads as attached to the character; first person keeps it flat. The
  // projection's perspective gives the parallelogram its depth, the skew
  // supplies the parallelogram itself.
  float tilt = 0.0f;
  if (followNow) {
    const float halfWidth = static_cast<float>(width) * 0.5f;
    tilt = std::clamp((hipX - halfWidth) / halfWidth, -1.0f, 1.0f);
  }
  if (!Anchor.TiltApplied || std::fabs(tilt - Anchor.Tilt) > 0.02f) {
    // The far edge is the one away from the character, so the lean mirrors
    // with the side.
    const float direction = Anchor.SideRight ? -1.0f : 1.0f;
    char transform[96] = {};
    std::snprintf(transform, sizeof(transform),
                  "perspective(600dp) rotateY(%.1fdeg) skewX(%.1fdeg)",
                  tilt * 14.0f * direction, tilt * 6.0f * direction);
    setProperty(doc, "cluster", "transform", transform);
    Anchor.Tilt = tilt;
    Anchor.TiltApplied = true;
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
  PeakHealth = 0;
  PeakArmor = 0;
  Bullets.clear();
  // Force the next show to rewrite every readout.
  HealthBar = {};
  ArmorBar = {};
  Ammo = {};
  Flash = {};
  LongJump = {};
}

void panelDocumentReady(void *) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  // The document starts with every readout visible (the RML is a layout, not
  // a runtime state). Hide all groups before the first snapshot so a readout
  // that is already gated off cannot flash its placeholder zero, then build
  // the bar cells once and seed from the live snapshot.
  setGroupVisible(doc, kHealthIds, std::size(kHealthIds), false);
  setGroupVisible(doc, kArmorIds, std::size(kArmorIds), false);
  setGroupVisible(doc, kAmmoIds, std::size(kAmmoIds), false);
  setGroupVisible(doc, kFlashIds, std::size(kFlashIds), false);
  setGroupVisible(doc, kLongJumpIds, std::size(kLongJumpIds), false);
  buildCells(doc);
  Bullets.clear();
  HealthBar = {};
  ArmorBar = {};
  Ammo = {};
  Flash = {};
  LongJump = {};
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
  SideCvar = Api->console->registerCvar(kSideCvar, "0", ABCUI_CVAR_VALUE);
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
  Anchor.Tilt = 0.0f;
  Anchor.TiltApplied = false;
  Anchor.SideRight = false;
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
