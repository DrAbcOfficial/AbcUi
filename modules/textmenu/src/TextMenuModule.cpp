// abcui.textmenu - vanilla ShowMenu replacement (number-key menus). Moved
// from src/panels/textmenu (old ABCEnchance textmenu.*): chunked ShowMenu
// accumulation (bits&0x7f = more-chunks pending, bit7 = ammo-selection
// menu), shutoff countdown, the inline colour stream repaint and the
// slot1..10 key claims - while this module renders the menu it owns the slot
// commands and turns them into menuselect; the framework menu fact gates the
// other consumers (the weaponchoose module yields while it is visible).
#include "../../common/OpacityMotion.h"
#include "../../common/Panel.h"
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <cstdio>
#include <format>
#include <cstring>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

void panelReset(void *userdata);

// Old StartFade sequences TextMenuIn/Out: 0.2s alpha, visibility flips.
constexpr double kFadeSeconds = 0.2;

// Menu state (the ported panel members).
int ValidSlots = 0;
bool IsASMenu = false;
bool WaitingForMore = false;
std::string MenuString;
double ShutoffTime = -1.0;
double HideAtTime = -1.0;
double ClientTimeNow = 0.0;
moduleui::opacity::Ramp Fade;
// True while this module owns the displayed menu. The framework menu fact is
// the authority for input ownership; this is the renderer's own state.
bool Displayed = false;

void fadeTo(bool state) {
  Fade.retarget(state ? 1.0f : 0.0f, kFadeSeconds);
}

// Old TextImageEx::Paint inline colour stream (SC 5.26 menu text): \w \d \y
// \r \c \g \m \o switch colour mid-string, \R toggles right-aligned flow,
// a newline resets to the label colour. Colours byte-exact from the old
// switch; \d is the label fg itself (TextMenu.TextColor #dedede).
const char *menuColorCode(char code) {
  switch (code) {
  case 'w':
    return "#ffffff";
  case 'd':
    return "#dedede";
  case 'y':
    return "#ffd240"; // 255 210 64
  case 'r':
    return "#d24000"; // 210 64 0
  case 'c':
    return "#00ffff";
  case 'g':
    return "#00ff00";
  case 'm':
    return "#ff00ff";
  case 'o':
    return "#ffa500"; // 255 165 0
  default:
    return nullptr;
  }
}

std::string colorizeMenuRml(const std::string &text) {
  std::string out;
  out.reserve(text.size() + 32);
  bool inSpan = false;
  const auto closeSpan = [&]() {
    if (inSpan) {
      out += "</span>";
      inSpan = false;
    }
  };
  for (size_t i = 0; i < text.size(); i++) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '\\' && i + 1 < text.size()) {
      if (const char *hex = menuColorCode(text[i + 1])) {
        closeSpan();
        out += "<span style=\"color: ";
        out += hex;
        out += "\">";
        inSpan = true;
        i++;
        continue;
      }
      if (text[i + 1] == 'R') {
        // Right-align flow control: no inline-markup equivalent in Rml;
        // consume so it does not print (server menus rarely ship it).
        i++;
        continue;
      }
    }
    if (c == '\r' || c <= 8) {
      // Old Paint: CR and control chars <= 8 are skipped.
      continue;
    }
    if (c == '\n') {
      // The break must be a <br/>: a raw newline lands between spans and
      // the parser drops whitespace-only text there regardless of
      // white-space (found live via the abc_ui_menu injector).
      closeSpan();
      out += "<br/>";
      continue;
    }
    if (c == '&') {
      // "&&" draws one ampersand; a single & was the hotkey marker (old
      // code skipped it, the next char still drew).
      if (i + 1 < text.size() && text[i + 1] == '&') {
        out += "&amp;";
        i++;
      }
      continue;
    }
    switch (c) {
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    default:
      out += c;
    }
  }
  closeSpan();
  return out;
}

// Old TextUtil::trimNewlines: strip the leading/trailing newline runs
// servers send.
std::string trimNewlines(const std::string &text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && (text[begin] == '\n' || text[begin] == '\r'))
    begin++;
  while (end > begin && (text[end - 1] == '\n' || text[end - 1] == '\r'))
    end--;
  return text.substr(begin, end - begin);
}

void setMenuInnerRml(const char *rml) {
  AbcUiDocument *doc = moduleui::panelDocument(Api, "textmenu");
  if (!doc)
    return;
  if (AbcUiElement *menu = Api->ui->querySelector(doc, "menu"))
    Api->ui->elementSetInnerRML(menu, rml);
}

void closeForFade() {
  // The document stays up for the fade-out; hide it once the opacity
  // transition has finished (old OnFadeOutDone at +0.2s).
  fadeTo(false);
  HideAtTime = ClientTimeNow + kFadeSeconds;
}

// Old MsgShowMenu.
void showMenu(int validSlots, int displaySeconds, int bits, const char *text) {
  if (!WaitingForMore)
    MenuString.clear();
  ValidSlots = validSlots;
  const int needMore = bits & 0x7F;
  IsASMenu = ((bits >> 7) & 1) != 0;
  ShutoffTime = displaySeconds > 0 ? ClientTimeNow + displaySeconds : -1.0;
  const size_t textSize = text ? strlen(text) : 0;
  if (ValidSlots) {
    if (textSize > 16384 || MenuString.size() > 16384 - textSize) {
      MenuString.clear();
      panelReset(nullptr);
      return;
    }
    MenuString += text;
    if (needMore == 0) {
      MenuString = trimNewlines(MenuString);
      const std::string rml = colorizeMenuRml(MenuString);
      setMenuInnerRml(rml.c_str());
      fadeTo(true);
      Api->ui->documentShow(moduleui::panelDocument(Api, "textmenu"));
      Displayed = true;
      // A fresh menu cancels a pending fade-out-hide: the server's answer
      // to a menuselect usually lands inside the 0.2s window, and letting
      // the stale deadline fire would close the just-opened submenu.
      HideAtTime = -1.0;
    }
  } else {
    Displayed = false;
    closeForFade();
  }
  WaitingForMore = needMore != 0;
}

// Old SelectMenuItem: valid slot -> menuselect/as_menuselect + fade out.
bool selectMenuItem(int slot) {
  if (slot > 0 && (ValidSlots & (1 << (slot - 1)))) {
    const std::string command =
        std::format("{} {}\n", IsASMenu ? "as_menuselect" : "menuselect", slot);
    Api->console->engineCommand(command.c_str());
    Displayed = false;
    closeForFade();
    return true;
  }
  return false;
}

void panelDocumentReady(void *) {
  // Invisible until a ShowMenu arrives (opacity 0, not display:none - the
  // fade-out transition needs the element alive).
  Fade.reset();
  Api->ui->documentSetProperty(moduleui::panelDocument(Api, "textmenu"),
                               "opacity", "0");
}

void panelOnData(void *, double frameTime) {
  frameTime = moduleui::opacity::frameDelta(frameTime);
  ClientTimeNow += frameTime;
  // Old OnThink shutoff.
  bool fadeStarted = false;
  if (ShutoffTime >= 0.0 && ClientTimeNow >= ShutoffTime) {
    Displayed = false;
    closeForFade();
    ShutoffTime = -1.0;
    fadeStarted = true;
  }
  float opacity = 0.0f;
  if (Fade.advance(fadeStarted ? 0.0 : frameTime, opacity)) {
    char value[16];
    moduleui::opacity::text(opacity, value);
    Api->ui->documentSetProperty(moduleui::panelDocument(Api, "textmenu"),
                                 "opacity", value);
  }
  // Fade-out finished: take the document out of the layout. Doing it here -
  // instead of keeping the document permanently shown - is what keeps
  // slot-key chaining honest.
  if (HideAtTime >= 0.0 && ClientTimeNow >= HideAtTime) {
    HideAtTime = -1.0;
    Api->ui->documentHide(moduleui::panelDocument(Api, "textmenu"));
  }
}

void panelReset(void *) {
  // Old Reset.
  MenuString.clear();
  ShutoffTime = -1.0;
  WaitingForMore = false;
  IsASMenu = false;
  ValidSlots = 0;
  Displayed = false;
  HideAtTime = -1.0;
  setMenuInnerRml("");
  Fade.reset();
  Api->ui->documentSetProperty(moduleui::panelDocument(Api, "textmenu"),
                               "opacity", "0");
  Api->ui->documentHide(moduleui::panelDocument(Api, "textmenu"));
}

// ---- event sinks ----

int canShowMenu(void *, const void *) {
  return moduleui::panelDocument(Api, "textmenu") != nullptr;
}

int32_t onShowMenu(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiShowMenuEvent *>(payload);
  showMenu(event->validSlots, event->displaySeconds, event->bits, event->text);
  return ABCUI_DECISION_HANDLED;
}

void onInitHud(void *, const void *) {
  panelReset(nullptr);
}

// The menu owns a number key only while it is the displayed renderer and the
// framework says a menu is up. The pure predicate runs before any handler, so
// no other module can observe a mid-dispatch change.
int slotOwned(void *, const void *) {
  if (!Displayed || !moduleui::panelDocument(Api, "textmenu"))
    return 0;
  AbcUiMenuFacts menu{};
  menu.structSize = sizeof(menu);
  return Api->facts->get(ABCUI_FACT_MENU, &menu, sizeof(menu)) &&
         menu.visible != 0;
}

int32_t onSlotCommand(void *userdata, const void *) {
  const intptr_t slot = reinterpret_cast<intptr_t>(userdata);
  const int32_t close = selectMenuItem(static_cast<int>(slot) + 1)
                            ? ABCUI_DECISION_CLOSE_MENU
                            : ABCUI_DECISION_NONE;
  return ABCUI_DECISION_HANDLED | close;
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel = moduleui::makePanel("textmenu",
                              "mod://abcui.textmenu/hud/rml/textmenu.rml", 0);
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  AbcUiClaimDesc menu{};
  menu.structSize = sizeof(menu);
  menu.role = ABCUI_CLAIM_OWNER;
  menu.canHandle = &canShowMenu;
  menu.handle = &onShowMenu;
  if (!Api->claims->claimDomain(ABCUI_CHANNEL_NETMSG_SHOW_MENU, &menu))
    Api->core->warn("textmenu: cannot claim the ShowMenu message");
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_INIT_HUD, 0, &onInitHud, nullptr);
  for (intptr_t i = 0; i < 10; i++) {
    AbcUiClaimDesc desc{};
    desc.structSize = sizeof(desc);
    desc.role = ABCUI_CLAIM_OWNER;
    desc.canHandle = &slotOwned;
    desc.handle = &onSlotCommand;
    desc.userdata = reinterpret_cast<void *>(i);
    const std::string name = std::format("slot{}", i + 1);
    if (!Api->claims->claimCommand(name.c_str(), &desc))
      Api->core->warn("textmenu: cannot claim command %s", name.c_str());
  }
}

void onModuleShutdown() {
  Displayed = false;
  Fade.reset();
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
                   ABCUI_MODULE_SECTION_UI | ABCUI_MODULE_SECTION_CLAIMS |
                   ABCUI_MODULE_SECTION_FACTS))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.textmenu";
  out->version = "3.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = nullptr;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
