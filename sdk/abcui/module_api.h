#pragma once

// sdk/abcui/module_api.h - the AbcUiModuleApi table and module descriptor.
//
// A module's DLL exports exactly one function:
//
//   ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
//                                            const AbcUiModuleApi *api,
//                                            AbcUiModuleDescriptor *out);
//
// It receives the framework service table and fills the descriptor. Return
// nonzero on success; a zero return (or an inconsistent descriptor) skips
// the module and everything that depends on it. Descriptor callbacks run on
// the engine main thread at the same lifecycle points the framework's
// built-in modules use (see AGENTS.md "UI module architecture").

#include <stdint.h>

#include "abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Packed semantic version: (major << 16) | (minor << 8) | patch.
#define ABCUI_VERSION_PACK(major, minor, patch)                                \
  (((uint32_t)(major) << 16) | ((uint32_t)(minor) << 8) | (uint32_t)(patch))

typedef uint64_t AbcUiSubscription; // 0 is never issued.

// Data-channel sink: the payload pointer is only valid for the call.
typedef void (*AbcUiEventSink)(void *userdata, const void *payload);
// Veto-channel sink: returning nonzero vetoes and stops the dispatch here.
typedef int (*AbcUiEventVeto)(void *userdata, const void *payload);

// Console command callback (no arguments; use engine argv through the
// console api when needed).
typedef void (*AbcUiCommandFn)(void *userdata);

// ---------------------------------------------------------------- core ----

typedef struct AbcUiCoreApi {
  uint32_t structSize;
  uint32_t frameworkVersion; // ABCUI_VERSION_PACK of the running framework
  const char *frameworkVersionString;
  // Console output through the framework channel ("[module]" prefix). The
  // printf-style formatting happens framework-side.
  void (*print)(const char *format, ...);
  void (*warn)(const char *format, ...);
  void (*error)(const char *format, ...);
} AbcUiCoreApi;

// ------------------------------------------------------------- console ----

typedef void *AbcUiCvarHandle; // opaque registered cvar

// Flags for console->registerCvar: mirrors AbcCore's abc::FCVAR_VALUE
// (FCVAR_PRINTABLEONLY | FCVAR_CLIENTDLL | FCVAR_ARCHIVE) so a module needs
// no AbcCore or engine header. Other engine flags are not part of the ABI.
#define ABCUI_CVAR_VALUE 0x00000091

typedef struct AbcUiConsoleApi {
  uint32_t structSize;
  // Registers a cvar and returns its handle (null when the engine refused).
  // Cvars survive HUD_Shutdown cycles; register from onInit.
  AbcUiCvarHandle (*registerCvar)(const char *name, const char *value,
                                  int flags);
  float (*cvarValue)(AbcUiCvarHandle cvar, float fallback);
  // Copies the cvar string (at most outSize-1 bytes + terminator).
  void (*cvarString)(AbcUiCvarHandle cvar, char *out, uint32_t outSize,
                     const char *fallback);
  // Registers a console command. Registering the same name twice updates
  // the handler without touching the engine table.
  void (*registerCommand)(const char *name, AbcUiCommandFn fn, void *userdata);
  // Runs a client command through the engine command path (menus send
  // "menuselect 3" and friends through this).
  void (*engineCommand)(const char *command);
  // Runs a server command (abc::ServerCmd; the vote module answers
  // "voteyes"/"voteno").
  void (*serverCommand)(const char *command);
  // Hands the hud_vote gate cvar to the netmsg VoteMenu consumer: while the
  // gate is off the message passes through untouched (old IsVoteEnable).
  // Pass null to remove the gate (the message is then not routed at all).
  void (*setVoteGate)(AbcUiCvarHandle cvar);
  // Registers a cvar and arms an engine-side change callback (the old
  // CREATE_CVAR callback contract: clamping and re-set reactions). The
  // callback runs on the engine cvar-set path with the module's userdata.
  AbcUiCvarHandle (*registerCvarWatch)(const char *name, const char *value,
                                       int flags, AbcUiCommandFn fn,
                                       void *userdata);
  // Plays an engine sound by path (gEngfuncs.pfnPlaySoundByName).
  void (*playSound)(const char *path, float volume);
  // Looks up an already-registered cvar by name (null when absent) - shared
  // settings such as hud_danger_health or the engine's default_fov.
  AbcUiCvarHandle (*findCvar)(const char *name);
  // Engine key binding for a command name ("use"); "" when unbound. Same
  // measure-then-copy contract as ui->escapeRml.
  int32_t (*keyBinding)(const char *name, char *out, uint32_t outSize);
  // Writes a cvar's float value (the old clamp-style cvar callbacks did this
  // directly; registerCvarWatch's callback has no cvar argument).
  void (*setCvarValue)(AbcUiCvarHandle cvar, float value);
} AbcUiConsoleApi;

// -------------------------------------------------------------- events ----

// Channels are lowercase dotted strings owned by the publishing subsystem
// (see sdk/abcui/events.h for the constants and their payload structs).
// Priority > 0 intercepts before the publisher's default 0, < 0 runs after;
// the same priority keeps subscription order. Subscribe from onLoad or
// onDocumentReady and unsubscribe before onUnload returns.
typedef struct AbcUiEventsApi {
  uint32_t structSize;
  AbcUiSubscription (*subscribe)(const char *channel, int priority,
                                 AbcUiEventSink sink, void *userdata);
  AbcUiSubscription (*subscribeVeto)(const char *channel, int priority,
                                     AbcUiEventVeto sink, void *userdata);
  void (*unsubscribe)(AbcUiSubscription subscription);
  // Aggregate veto variant: every sink runs in priority order regardless of
  // the other sinks' results; the return of any sink consumes the event.
  AbcUiSubscription (*subscribeVetoAggregate)(const char *channel, int priority,
                                              AbcUiEventVeto sink,
                                              void *userdata);
} AbcUiEventsApi;

// ----------------------------------------------------------------- vfs ----

typedef struct AbcUiFileView AbcUiFileView; // opaque opened package entry

typedef struct AbcUiVfsApi {
  uint32_t structSize;
  // Opens "path" inside the module's own package ("rml/health.rml"). The
  // view stays valid until viewFree or the module unloads.
  AbcUiFileView *(*open)(const char *moduleId, const char *path);
  const void *(*viewData)(AbcUiFileView *view);
  uint32_t (*viewSize)(AbcUiFileView *view);
  // Releases a view (no-op for zero-copy entries, frees extracted ones).
  void (*viewFree)(AbcUiFileView *view);
  int (*exists)(const char *moduleId, const char *path);
  // Reads an engine-relative file through the engine filesystem (the same
  // mounted search path the framework's resource reader uses). Returns the
  // byte count, or -1 when the file is missing, invalid or larger than
  // maxBytes. Pass out == null (or a null outSize) to measure first.
  int (*readEngineFile)(const char *path, void *out, uint32_t outSize,
                        uint32_t maxBytes);
} AbcUiVfsApi;

// -------------------------------------------------------------- panels ----

// Registers a HUD panel: one Rml document fed by the framework frame pumps.
// documentPath is a mod:// uri inside the module package ("mod://<id>/rml/
// health.rml"). Registration is only legal from onLoad; the panel joins the
// next installHudPanels pass (the same HUD_Init cycle).
typedef struct AbcUiPanelDesc {
  uint32_t structSize;
  const char *name;         // unique panel name (debug lookup, cvar prefix)
  const char *documentPath; // mod:// uri of the panel's rml
  int zIndex;               // document paint order; 0 = default
  void *userdata;           // passed back to every callback
  void (*onDocumentReady)(void *userdata);
  void (*onData)(void *userdata, double frameTime);   // HUD_Frame pump
  void (*onRedraw)(void *userdata, double frameTime); // HUD_Redraw pump
  void (*reset)(void *userdata);                      // map transition
} AbcUiPanelDesc;

typedef struct AbcUiPanelsApi {
  uint32_t structSize;
  int (*registerPanel)(const AbcUiPanelDesc *desc);
} AbcUiPanelsApi;

// ----------------------------------------------------------------- data ----

// Snapshot of the local player HUD state the visibility rules hang off
// (predicted client data plus the usermsg-fed values). Values are copied
// out; int32_t flags are 0/1.
typedef struct AbcUiHudState {
  uint32_t structSize;
  int32_t suit;
  int32_t spectate;
  int32_t longjump;
  int32_t hideBits; // raw last-write HideWeapon/HideHUD token
  int32_t flashOn;
  int32_t flashBattery;
  int32_t health; // predicted health (0 until the first prediction frame)
  int32_t armor;  // message-fed armor value
  int32_t waterLevel;
  float fov;
  float punchAngle[3]; // predicted view punch (old hudstate::punchangle)
} AbcUiHudState;

// Local player readout (data->playerState): copied out, never a pointer.
typedef struct AbcUiPlayerState {
  uint32_t structSize;
  int32_t index;      // engine entindex, 0 when there is no local player
  int32_t frags;      // score from the ScoreInfo feed
  float speed;        // horizontal velocity (the old side readout)
  float origin[3];    // local player origin (grenade range / projection)
  float angles[3];    // local view angles (gEngfuncs.GetViewAngles)
  int32_t messageNum; // entity message generation (stale-entity gate)
} AbcUiPlayerState;

// PlayerTrace types shared with data->traceEndPos.
#define ABCUI_TRACE_VIEW 0
#define ABCUI_TRACE_HEAD 1
#define ABCUI_TRACE_FOOT 2

typedef struct AbcUiDataApi {
  uint32_t structSize;
  void (*hudState)(AbcUiHudState *out);
  // Current level name ("maps/<map>.bsp"); empty when the engine has none.
  // Same measure-then-copy contract as ui->escapeRml.
  int (*levelName)(char *out, uint32_t outSize);
  // Local player readout: entindex (0 when there is none), frags from the
  // ScoreInfo feed and the horizontal speed the HUD readouts display.
  void (*playerState)(AbcUiPlayerState *out);
  // Engine third-person view flag (gExportfuncs.CL_IsThirdPerson).
  int32_t (*thirdPerson)(void);
  // Cached player trace end position (the same traces the HUD projects);
  // 1 on success. `type` is one of ABCUI_TRACE_*.
  int32_t (*traceEndPos)(int32_t type, float out[3]);
  // Engine model index for a model path (EV_FindModelIndex); 0 when the
  // engine does not know the model.
  int32_t (*modelIndex)(const char *path);
  // Local player view height offset (EV_LocalPlayerViewheight).
  void (*viewHeight)(float out[3]);
  // Scoreboard header data: server name, next map and the time limit the
  // server announced (0 = none). Strings use the measure-then-copy contract
  // (out == null to measure); either string may be null when not needed.
  void (*serverInfo)(char *serverName, uint32_t serverNameSize, char *nextMap,
                     uint32_t nextMapSize, int32_t *timeEnd);
} AbcUiDataApi;

// ------------------------------------------------------------------- ui ----

// Dynamic crosshair layout request (the old services::crosshair::Layout).
typedef struct AbcUiCrosshairLayout {
  float thickness;
  float length;
  float offset;       // gap + the caller's punch drift
  float outlineWidth; // 0 = no outline
  int32_t dot;
  int32_t tShape;
} AbcUiCrosshairLayout;

typedef struct AbcUiDocument AbcUiDocument; // opaque panel-backed document
// Element handles are only valid for the callback that queried them; query
// again per event (the built-ins do the same).
typedef struct AbcUiElement AbcUiElement;

// One DOM event delivered to a module listener. Mouse coordinates are in
// context pixels; `button` is -1 for non-mouse events.
typedef struct AbcUiElementEvent {
  uint32_t structSize;
  const char *type; // "click", "mousedown", ...
  int32_t button;
  float mouseX;
  float mouseY;
} AbcUiElementEvent;

// Returns nonzero to stop propagation (the module handled the event).
typedef int32_t (*AbcUiElementListener)(void *userdata, AbcUiElement *target,
                                        const AbcUiElementEvent *event);

typedef struct AbcUiUiApi {
  uint32_t structSize;
  // The document of one of this module's registered panels, by name.
  AbcUiDocument *(*panelDocument)(const char *panelName);
  // Session-aware visibility (the same gating the built-in panels use).
  void (*documentShow)(AbcUiDocument *doc);
  void (*documentHide)(AbcUiDocument *doc);
  // Sets a document-level RCSS property (e.g. a --token).
  void (*documentSetProperty)(AbcUiDocument *doc, const char *name,
                              const char *value);
  // "#id" lookup inside the document; null when missing.
  AbcUiElement *(*querySelector)(AbcUiDocument *doc, const char *id);
  void (*elementSetProperty)(AbcUiElement *elem, const char *name,
                             const char *value);
  void (*elementSetInnerRML)(AbcUiElement *elem, const char *rml);
  // Additive digit-glyph markup for readouts (framework glyph atlas; the
  // accent tint is used when `tint` is null). Returns 0 when the text lacks
  // glyph coverage and plain text is the fallback. Writes at most
  // outSize-1 bytes plus the terminator.
  int (*inkMarkup)(const char *text, const char *tint, char *out,
                   uint32_t outSize);
  // Localizes a translation token ("Vote_DefaultYes") through the game
  // language table. Returns 1 and writes the translation (at most outSize-1
  // bytes plus terminator); 0 when the token is unknown (out untouched).
  int (*translate)(const char *token, char *out, uint32_t outSize);
  // Escapes server-derived text for elementSetInnerRML (the built-in panels'
  // TextUtil::escapeRml). Returns the byte count the escaped text needs
  // (terminator excluded) and writes at most outSize-1 bytes plus the
  // terminator; pass out == null to measure first.
  int (*escapeRml)(const char *text, char *out, uint32_t outSize);
  // Display name for an engine keynum (IGameUIFuncs::Key_NameForKey); empty
  // when the engine interface is missing. Same measure-then-copy contract
  // as escapeRml.
  int (*keyNameForKey)(int keynum, char *out, uint32_t outSize);
  // True while the panel's document is actually on screen (the session gate
  // included) - the built-in panels' isVisible().
  int (*documentVisible)(AbcUiDocument *doc);
  // Appends one child element to `parent`, built from `rml` (the element's
  // inner markup) with `className` applied when non-empty. Returns a handle
  // that stays valid until the element is removed or its document closes -
  // the dynamic-row pattern (kill feed, chat log). Do not cache handles
  // across a context rebuild.
  AbcUiElement *(*elementAppendRml)(AbcUiElement *parent, const char *rml,
                                    const char *className);
  // Removes and destroys an element appended through elementAppendRml.
  void (*elementRemove)(AbcUiElement *element);
  // Reads an element's inner RML; same measure-then-copy contract as
  // escapeRml.
  int (*elementInnerRML)(AbcUiElement *element, char *out, uint32_t outSize);
  // Writes the RCSS "rgb(r, g, b)" text for a 0..255 RGB triple
  // (ColorUtil::format).
  void (*formatColor)(const float rgb[3], char *out, uint32_t outSize);
  // Old ColorUtil::differColor: the hue of `base` pulled toward `danger` by
  // `ratio` (1 = base, 0 = danger; saturation/value stay base's), written as
  // RCSS text. Both inputs are 0..255 RGB triples.
  void (*differColor)(const float base[3], const float danger[3], float ratio,
                      char *out, uint32_t outSize);
  // Sets an HTML attribute ("src", "class", ...); a null value removes it.
  void (*elementSetAttribute)(AbcUiElement *element, const char *name,
                              const char *value);
  // Toggles one class token on an element.
  void (*elementSetClass)(AbcUiElement *element, const char *name, int enabled);
  // hud.txt sprite lookup (the sprite resource tables): fills the atlas name
  // and crop rect; 1 when the name resolves.
  int (*hudSprite)(const char *name, char *atlas, uint32_t atlasSize,
                   int32_t rect[4]);
  // Restarts an <animimg> element's frame sequence (no-op on other
  // elements).
  void (*elementRestartAnimation)(AbcUiElement *element);
  // Writes the RCSS "rgba(r, g, b, a)" text (alpha as a 0..255 integer).
  void (*formatRgba)(int r, int g, int b, int a, char *out, uint32_t outSize);
  // World -> screen projection in Rml pixel coordinates. Returns the raw
  // engine result: nonzero means the point is behind the camera.
  int (*project)(const float world[3], float screen[2]);
  // Document/context pixel dimensions; 0 when the document has no context.
  int (*documentSize)(AbcUiDocument *doc, int *width, int *height);
  // Dynamic crosshair geometry (the single implementation the settings
  // preview also uses). Fills core[5][4] and outline[5][4] as x, y, width,
  // height relative to the centre, plus visible[5] (0 for the parts a switch
  // hides). Part order: center, top, bottom, left, right.
  void (*crosshairLayout)(const AbcUiCrosshairLayout *layout, float core[5][4],
                          float outline[5][4], int32_t visible[5]);
  // HUD density scale (framework::Theme::scale) - the old GetWide/GetTall
  // proportional scale pixel geometry multiplies by.
  float (*scale)(void);
  // Nth child element (0-based); null when out of range. Stable for children
  // appended through elementAppendRml until one is removed.
  AbcUiElement *(*elementChild)(AbcUiElement *element, int32_t index);
  // Subtree-scoped id lookup (document-scoped lookups would collapse
  // repeated per-tile ids into the first tile).
  AbcUiElement *(*elementFind)(AbcUiElement *element, const char *id);
  // DOM listener on one element; the callback receives the event's TARGET
  // element (walk up with elementParent to find the owning row). Returns a
  // non-zero id, 0 on failure; remove it in onShutdown/onUnload.
  uint64_t (*elementAddListener)(AbcUiElement *element, const char *type,
                                 AbcUiElementListener listener, void *userdata);
  void (*elementRemoveListener)(AbcUiElement *element, const char *type,
                                uint64_t id);
  // Attribute readers/helpers for the delegated-row pattern.
  int32_t (*elementAttributeInt)(AbcUiElement *element, const char *name,
                                 int32_t fallback);
  AbcUiElement *(*elementParent)(AbcUiElement *element);
  int32_t (*elementClassSet)(AbcUiElement *element, const char *name);
  const char *(*elementId)(AbcUiElement *element);
  // Text entry (the RmlUi form control the module focuses): focus/blur and
  // the UTF-8 value. elementValue follows the measure-then-copy contract.
  void (*elementFocus)(AbcUiElement *element);
  void (*elementBlur)(AbcUiElement *element);
  int32_t (*elementValue)(AbcUiElement *element, char *out, uint32_t outSize);
  void (*elementSetValue)(AbcUiElement *element, const char *text);
  // Scrolling: the log windows use these to follow the tail and to browse
  // older lines (values in Rml pixels).
  float (*elementScrollTop)(AbcUiElement *element);
  void (*elementSetScrollTop)(AbcUiElement *element, float top);
  float (*elementScrollHeight)(AbcUiElement *element);
  float (*elementClientHeight)(AbcUiElement *element);
  // Text caret range (the chat editor parks it at the end after a recall).
  void (*elementSetSelection)(AbcUiElement *element, int32_t start,
                              int32_t end);
  // Document focus (focused root: keys reach the document, the page keeps its
  // last control focus) and focus release (removes the document from the
  // context's focus history). Safe on panel and page handles.
  void (*documentFocus)(AbcUiDocument *doc);
  void (*documentBlur)(AbcUiDocument *doc);
  // Attribute reader (delegated rows carry their intent in data-* values).
  // Same measure-then-copy contract as escapeRml; an absent attribute is
  // empty (0 bytes).
  int32_t (*elementAttribute)(AbcUiElement *element, const char *name,
                              char *out, uint32_t outSize);
  // Attribute-value variant of escapeRml: additionally escapes the quote
  // characters that would close a double-quoted attribute. A server-derived
  // string placed inside an attribute (data-*, title, value) needs this one,
  // or a quote in the text escapes its attribute and injects markup.
  int32_t (*escapeRmlAttribute)(const char *text, char *out, uint32_t outSize);
} AbcUiUiApi;

// ------------------------------------------------------------- commands ----

typedef struct AbcUiCommandsApi {
  uint32_t structSize;
  // Subscribes a chained console command from the framework bus
  // ("slot1".."slot10", "missionbriefing", "invnext", "invprev",
  // "+/-showscores", "votemenu", "+attack", "+/-commandmenu",
  // "messagemode", "messagemode2", "inventory", "+/-inventory"). Every sink
  // runs in priority order regardless of the other sinks' results;
  // returning nonzero consumes the command (the engine's original path is
  // skipped).
  AbcUiSubscription (*subscribeCommand)(const char *name, int priority,
                                        AbcUiEventVeto sink, void *userdata);
  // The shared number-menu-open flag: the text-menu module writes it, other
  // modules and the framework gate on it.
  int (*textMenuOpen)(void);
  void (*setTextMenuOpen)(int open);
} AbcUiCommandsApi;

// -------------------------------------------------------------- weapons ----

#define ABCUI_WEAPON_NAME_MAX 64
#define ABCUI_WEAPON_ATLAS_MAX 96
// Registry slot count (old MAX_WEAPON_SLOT / weaponres::kMaxWeaponSlot).
#define ABCUI_WEAPON_SLOT_COUNT 10

// Old ITEM_FLAGS bits the selection logic keys on.
#define ABCUI_ITEM_FLAG_SELECT_ON_EMPTY (1 << 0)
#define ABCUI_ITEM_FLAG_EXHAUSTIBLE (1 << 4)

// One sprite crop ("sprites/<atlas>.txt" entry). invalid means the weapon's
// sprite list had no matching entry, so the caller falls back to plain text.
typedef struct AbcUiWeaponSprite {
  int32_t valid;
  char atlas[ABCUI_WEAPON_ATLAS_MAX];
  int32_t rect[4]; // left, top, right, bottom
} AbcUiWeaponSprite;

// Point-in-time snapshot of one registry weapon. Identity is (slot, slotPos):
// the server's WeaponList assigns both and the registry keeps them stable.
typedef struct AbcUiWeaponInfo {
  uint32_t structSize;
  int32_t slot;
  int32_t slotPos;
  int32_t ammoType;
  int32_t ammo2Type;
  int32_t max1;
  int32_t max2;
  int32_t flags;   // ABCUI_ITEM_FLAG_*
  int32_t owned;   // owned by the local player right now
  int32_t hasAmmo; // usable right now (unlimited weapons always true)
  int32_t clip;    // primary magazine (-1 when the weapon has none)
  int32_t clip2;   // secondary magazine (-1 when the weapon has none)
  int32_t state;   // CurWeapon state bits (valid/onTarget/uzi)
  char name[ABCUI_WEAPON_NAME_MAX];
  AbcUiWeaponSprite active;          // "weapon_s" sprite (highlighted item)
  AbcUiWeaponSprite inactive;        // "weapon" sprite
  AbcUiWeaponSprite ammo;            // "ammo" sprite (ammo bar / pickup stack)
  AbcUiWeaponSprite ammo2;           // "ammo2" sprite (falls back to "ammo")
  AbcUiWeaponSprite crosshair;       // per-weapon crosshair ("crosshair")
  AbcUiWeaponSprite autoaim;         // on-target crosshair ("autoaim")
  AbcUiWeaponSprite zoomedCrosshair; // zoomed crosshair
  AbcUiWeaponSprite zoomedAutoaim;   // zoomed on-target crosshair
} AbcUiWeaponInfo;

typedef struct AbcUiWeaponsApi {
  uint32_t structSize;
  // Fills up to `capacity` snapshots of every registered weapon; returns the
  // total registry size (a larger total means the list was truncated).
  int32_t (*list)(AbcUiWeaponInfo *out, int32_t capacity);
  // Snapshot by identity; 1 when the weapon exists.
  int32_t (*get)(int32_t slot, int32_t slotPos, AbcUiWeaponInfo *out);
  int32_t (*selectedSlot)(void);
  // Snapshot of the highlighted weapon; 1 when there is one.
  int32_t (*selected)(AbcUiWeaponInfo *out);
  int32_t (*countAmmo)(int32_t ammoType);
  // hud_fastswitch: the engine command already went out, the bar stays down.
  int32_t (*fastSwitch)(void);
  // Old SelectSlot: a slot key enters/cycles one slot, invnext/invprev walk
  // slots and positions (`wheel` = the walk flavor).
  void (*selectSlot)(int32_t slot, int32_t advance, int32_t wheel);
  // ResourceLimits::weaponCommandName - the console-token gate for the
  // server-provided weapon name (defense in depth; the registry already
  // rejects non-identifiers).
  int32_t (*validCommandName)(const char *name);
  // Snapshot of the current (CurWeapon) weapon; 1 when known.
  int32_t (*current)(AbcUiWeaponInfo *out);
  // Snapshot by the engine/server weapon id; 1 when the registry knows it.
  int32_t (*getById)(int32_t id, AbcUiWeaponInfo *out);
  // Icon of an ammo type, taken from any weapon carrying it; 1 when found.
  int32_t (*ammoIcon)(int32_t ammoType, AbcUiWeaponSprite *out);
} AbcUiWeaponsApi;

// -------------------------------------------------------------- players ----

#define ABCUI_MAX_PLAYERS 32

// One player-resource row (ScoreInfo wire values + engine mirror).
typedef struct AbcUiPlayerInfo {
  uint32_t structSize;
  int32_t index; // 1..32 entindex
  int32_t valid; // connected AND named
  int32_t spectate;
  int32_t team;  // teamres::TeamId
  int32_t admin; // 0 none, 1 operator, 2 server owner
  int32_t donor; // old native donor kind (0x10000 + kind)
  int32_t topColor;
  int32_t bottomColor;
  int32_t ping;
  int32_t loss;
  float frags;
  float health;
  float armor;
  uint64_t steamId64;
  char name[64];
  char model[96];
  int32_t deaths;
  char realName[64];
} AbcUiPlayerInfo;

// Engine entity snapshot (GetEntityByIndex + curstate/baseline fields the HUD
// panels gate on).
typedef struct AbcUiEntityState {
  uint32_t structSize;
  int32_t valid;
  int32_t messageNum;
  int32_t isPlayer;
  int32_t effects;
  float origin[3];
  float angles[3];
  float mins[3];
  float maxs[3];
} AbcUiEntityState;

typedef struct AbcUiPlayersApi {
  uint32_t structSize;
  // The local player's row; valid == 0 when there is none.
  void (*local)(AbcUiPlayerInfo *out);
  // One slot's row; 1 when the player resource knows the slot at all.
  int32_t (*get)(int32_t index, AbcUiPlayerInfo *out);
  // Engine entity snapshot; 1 when live.
  int32_t (*entity)(int32_t index, AbcUiEntityState *out);
  // Client dll per-player tint (GetClientColor, 0..1 RGB); 1 when available.
  int32_t (*clientColor)(int32_t index, float out[3]);
  // Avatar cache-bust generation for a SteamID64 (append to the avatar://
  // uri as "?gen=N"); 0 when the avatar service has no entry yet.
  int32_t (*avatarGeneration)(uint64_t steamId64);
  // Localization token for a team id ("Team_Red", ...); same
  // measure-then-copy contract as ui->escapeRml.
  int32_t (*teamNameToken)(int32_t team, char *out, uint32_t outSize);
  // Re-reads the engine mirrors (identity + live ping/loss). `full` = the
  // heavy identity pass (old per-row update()); 0 = ping/loss only.
  void (*refresh)(int32_t full);
} AbcUiPlayersApi;

// ---------------------------------------------------------------- radar ----

// The framework's world-to-FBO producer (the render half of the radar).
// The module owns the document, the plate geometry and the avatars; the
// framework renders the world from above when the module says the plate is
// on screen.
#define ABCUI_RADAR_ORTHO_BASE 4096.0f

typedef struct AbcUiRadarApi {
  uint32_t structSize;
  // 1 when the FBO colour texture exists and is registered.
  int32_t (*ready)(void);
  // Bumped whenever the FBO texture handle changes; build the map uri as
  // "radarfbo://map?gen=<generation>" (RmlUi caches textures per source).
  int32_t (*generation)(void);
  // The plate is on screen: render the world pass on the next pre-view.
  void (*setActive)(int32_t active);
  // Plate shape (square/round) - selects the mask the draw applies.
  void (*setMaskRound)(int32_t round);
  // hud_radar_zoom: visible span = 2 * (ABCUI_RADAR_ORTHO_BASE / zoom).
  void (*setZoom)(float zoom);
} AbcUiRadarApi;

// --------------------------------------------------------------- score ----

// The framework's scoreboard interaction seat (see framework/ScoreSeat.h):
// the +/-showscores latch that becomes usercmd IN_SCORE, the intermission
// flag HUD_Redraw feeds, and the interactive-HUD pointer tier (MOUSE2
// unlocks the cursor; the router token gates Rml mouse input).
typedef struct AbcUiScoreApi {
  uint32_t structSize;
  // The module holds the scores key: nonzero = CL_CreateMove reports
  // IN_SCORE so the server streams every player's ping/loss.
  void (*setHeld)(int32_t held);
  int32_t (*held)(void);
  // The module's table is on screen (pointer mode needs it).
  void (*setVisible)(int32_t visible);
  int32_t (*pointerMode)(void);
  // Refused while the table is not visible or the intermission board is up.
  void (*setPointerMode)(int32_t enabled);
  int32_t (*wantsWheel)(void);
  int32_t (*intermission)(void);
} AbcUiScoreApi;

// --------------------------------------------------------------- input ----

// One ownership claim on the UI input router (the framework's single input
// truth). The callbacks run on the engine main thread while the token is
// top-of-stack; a null callback behaves like the default (no claim / no
// block / nothing to do).
typedef struct AbcUiInputCallbacks {
  uint32_t structSize;
  void *userdata;
  // Swallow the edge from the engine bindings and feed it to the UI.
  int32_t (*claimsKey)(void *userdata, int32_t down, int32_t keynum);
  int32_t (*claimsWheel)(void *userdata);
  // The game must not act on its own input state this frame (a text field
  // owns the keyboard: the chat line keeps the player standing still).
  int32_t (*blocksGameplay)(void *userdata);
  // Exactly once per physical press of a claimed key.
  void (*onClaimedKeyDown)(void *userdata, int32_t keynum);
  // A claimed wheel pulse offered before the scroll claim (key capture).
  int32_t (*onClaimedWheelPulse)(void *userdata, int32_t keynum);
  // Force-release (focus loss / reset): the token is already stale.
  void (*onOwnershipLost)(void *userdata);
} AbcUiInputCallbacks;

typedef uint64_t AbcUiInputToken; // 0 is never issued.

typedef struct AbcUiInputApi {
  uint32_t structSize;
  // Takes an interactive-HUD token. Callbacks are copied; the module keeps
  // the userdata alive until release.
  AbcUiInputToken (*takeToken)(const AbcUiInputCallbacks *callbacks);
  void (*releaseToken)(AbcUiInputToken token);
  int32_t (*tokenHeld)(AbcUiInputToken token);
  // True while some owner holds UI input (the engine must not act on keys).
  int32_t (*exclusive)(void);
  // An IME composition is live (the editor must not act on Enter/ESC; the
  // input method consumes them) / drop the live composition.
  int32_t (*textComposing)(void);
  void (*cancelComposition)(void);
  // The native pause menu is up (a focused editor must close).
  int32_t (*menuUp)(void);
  // Active input-method short tag (CN/TW/HK/JP/KR), "" when none. Same
  // measure-then-copy contract as ui->escapeRml.
  int32_t (*imeLanguageTag)(char *out, uint32_t outSize);
} AbcUiInputApi;

// --------------------------------------------------------------- shell ----

// OS-shell seams the interactive panels need (profile links, copy actions).
// Text is UTF-8; both are no-ops on null/empty input.
typedef struct AbcUiShellApi {
  uint32_t structSize;
  // Opens the URL in the default browser (framework::OpenExternalUrl).
  void (*openUrl)(const char *url);
  // Puts the text on the system clipboard (Rml system interface).
  void (*copyText)(const char *text);
} AbcUiShellApi;

// --------------------------------------------------------------- voice ----

// The client's per-player voice blocking service (mute list + server vban
// sync). Identity comes from the player resource's SteamID64: players
// without one answer false and the local player can never be muted.
typedef struct AbcUiVoiceApi {
  uint32_t structSize;
  int32_t (*available)(void);
  int32_t (*isBlocked)(int32_t index);
  // Sets the ban and syncs the server; 0 when the toggle was refused.
  int32_t (*setBlocked)(int32_t index, int32_t blocked);
} AbcUiVoiceApi;

// ----------------------------------------------------------- documents ----

// Interactive (menu / dialogue) documents. A HUD panel is passive: pointer
// events off, no focus. A module page is the complement - one Rml document
// shown with document focus, pulled to the front of the context z-order,
// hit-testable, and closable. Register the descriptor once (the package
// image owns the document assets); the framework loads it on every Rml
// context cycle, because documents die with the context.
//
// The page keeps its DOM work through the ui section: root() is the document
// element for delegated listeners (querySelector resolves ids inside it),
// and every element handle follows the same per-callback lifetime rule.
#define ABCUI_DOC_FLAG_BACKDROP                                                \
  (1 << 0) // wall-sized surface: attach the
           // shared blurred backdrop
#define ABCUI_DOC_FLAG_MENU                                                    \
  (1 << 1) // part of the menu layer: hidden
           // when the menu lowers and honored
           // by the focus guard

typedef struct AbcUiDocumentDesc {
  uint32_t structSize;
  const char *name; // unique document name (diagnostics, lookup)
  const char *path; // document path ("abc/abcui/...", "mod://<id>/...")
  uint32_t flags;   // ABCUI_DOC_FLAG_*
  int32_t zIndex;   // cross-document tier; 0 = default
  void *userdata;   // passed back to every callback
  // The main document loaded (never fired for the built-in emergency page;
  // use documents->degraded to detect that case). Bind listeners here.
  void (*onReady)(void *userdata);
  void (*onShow)(void *userdata);
  void (*onHide)(void *userdata);
  // Document closed: a context shutdown or documents->destroy. Handles are
  // already invalid; drop anything the owner cached.
  void (*onClose)(void *userdata);
} AbcUiDocumentDesc;

typedef struct AbcUiDocumentsApi {
  uint32_t structSize;
  // Registers a page. The descriptor strings are copied; legal at any time
  // (a document created after contextReady loads immediately). Returns null
  // on a duplicate name or an incomplete descriptor.
  AbcUiDocument *(*create)(const AbcUiDocumentDesc *desc);
  // Unregisters and closes (onClose fires while the module is still loaded).
  void (*destroy)(AbcUiDocument *doc);
  int32_t (*loaded)(AbcUiDocument *doc);
  int32_t (*degraded)(AbcUiDocument *doc);
  int32_t (*visible)(AbcUiDocument *doc);
  void (*show)(AbcUiDocument *doc);
  void (*hide)(AbcUiDocument *doc);
  // The document element, for delegated listeners; null while unloaded.
  AbcUiElement *(*root)(AbcUiDocument *doc);
  // Recovery for a degraded document: re-attempt the main document. Returns
  // the load state afterwards.
  int32_t (*retry)(AbcUiDocument *doc);
} AbcUiDocumentsApi;

// ---------------------------------------------------------------- menu ----

// The replacement GameUI's navigation state, published after every
// presentation sync as the payload of "menu.state" (transitions only) and
// "menu.paint" (every presentation beat while a page layer is up). Page
// modules present their own document when the reported top page is theirs;
// the framework owns the takeover, the input claim and the page stack.
//
// Numeric ids are part of the ABI: they mirror gameui::menu::MenuPage and
// BackendPhase.
#define ABCUI_MENU_PAGE_NONE (-1)
#define ABCUI_MENU_PAGE_MAIN 0
#define ABCUI_MENU_PAGE_PAUSE 1
#define ABCUI_MENU_PAGE_SETTINGS 2
#define ABCUI_MENU_PAGE_SERVERS 3
#define ABCUI_MENU_PAGE_CREATE_GAME 4
#define ABCUI_MENU_PAGE_PLAYER_LIST 5
#define ABCUI_MENU_PAGE_VOTE 6
#define ABCUI_MENU_PAGE_COMMAND 7
#define ABCUI_MENU_PAGE_INVENTORY 8
#define ABCUI_MENU_PAGE_LOADING 9

#define ABCUI_MENU_PHASE_BOOTING 0
#define ABCUI_MENU_PHASE_DISCONNECTED 1
#define ABCUI_MENU_PHASE_CONNECTING 2
#define ABCUI_MENU_PHASE_LOADING 3
#define ABCUI_MENU_PHASE_IN_GAME 4
#define ABCUI_MENU_PHASE_SHUTTING_DOWN 5
#define ABCUI_MENU_PHASE_FATAL 6

typedef struct AbcUiMenuState {
  uint32_t structSize;
  int32_t phase;       // ABCUI_MENU_PHASE_*
  int32_t page;        // top page, ABCUI_MENU_PAGE_NONE when the stack is empty
  int32_t up;          // the menu layer is presented
  int32_t loading;     // a load attempt owns the screen
  int32_t consoleOpen; // the console overlay is up
  int32_t hasLevel;    // a level is loaded
  int32_t canClose;    // closing the menu resumes gameplay
  uint32_t generation; // live connect/local attempt id, 0 = none
} AbcUiMenuState;

typedef struct AbcUiMenuApi {
  uint32_t structSize;
  void (*state)(AbcUiMenuState *out);
  // Full open intent of one page (ensure the menu is up, reconcile the
  // phase, push the page, sync the presentation). 1 when the page is on top
  // afterwards. `page` is an ABCUI_MENU_PAGE_* value.
  int32_t (*openPage)(int32_t page);
  // One back intent plus the presentation sync (the page's Cancel action).
  void (*back)(void);
  // Raise the menu without pushing a page (idempotent); 1 when it is up.
  int32_t (*ensureUp)(void);
  // Close the menu and resume gameplay (in-game only); 1 on a decided close.
  int32_t (*resume)(void);
  // The shell's remaining fixed intents (the same typed controller events the
  // built-in shell sends; never raw engine commands):
  // - open the quit confirmation (the page module presents the overlay);
  // - leave the current game through the backend's disconnect path;
  // - toggle the replacement console overlay (close when it is up, otherwise
  //   open through the native bridge, then sync).
  void (*quit)(void);
  void (*disconnect)(void);
  void (*toggleConsole)(void);
} AbcUiMenuApi;

// ------------------------------------------------------------- votemenu ----

// The vote-creation page's model (ban / kick / kill / map, the filtered list
// and the validated submit). The model, the roster seam and the server map
// list stay in the framework; a page module projects it and sends the
// returned command through console->serverCommand.
#define ABCUI_VOTE_KIND_BAN 0
#define ABCUI_VOTE_KIND_KICK 1
#define ABCUI_VOTE_KIND_KILL 2
#define ABCUI_VOTE_KIND_MAP 3

#define ABCUI_VOTE_NAME_MAX 64
#define ABCUI_VOTE_ID_MAX 24

// One visible row. `selected` is computed against the model's selection, so
// the page never keeps a second selection truth. `steamId` is the decimal
// SteamID64 (empty when unavailable); map rows carry `name` only.
typedef struct AbcUiVoteCandidate {
  uint32_t structSize;
  int32_t kind; // ABCUI_VOTE_KIND_*
  int32_t slot; // player slot (players only; 0 for maps)
  int32_t selected;
  char name[ABCUI_VOTE_NAME_MAX];     // game name / map token
  char realName[ABCUI_VOTE_NAME_MAX]; // Steam persona
  char steamId[ABCUI_VOTE_ID_MAX];
} AbcUiVoteCandidate;

typedef struct AbcUiVoteMenuApi {
  uint32_t structSize;
  // Fresh opening: Ban page, empty filter, no selection, live roster.
  void (*prepare)(void);
  // Re-reads the live roster (no-op when the change gate says unchanged).
  void (*refresh)(void);
  // Model revision; the page re-projects only when it moved.
  uint32_t (*revision)(void);
  int32_t (*kind)(void);
  void (*setKind)(int32_t kind);
  void (*setFilter)(const char *filter);
  int32_t (*visibleCount)(void);
  // Copies row `index`; 0 when out of range (structSize honored on write).
  int32_t (*visibleAt)(int32_t index, AbcUiVoteCandidate *out);
  // Index of the selected visible row, -1 when nothing is selected.
  int32_t (*selectedIndex)(void);
  void (*select)(int32_t index);
  // Revalidates the selection against the live roster and writes the exact
  // server command (e.g. "voteban <name>"). Returns 1; on refusal returns 0
  // and writes a localization token (or text) into `error`. Both buffers
  // follow the measure-then-copy contract; either may be null/0.
  int32_t (*submit)(char *command, uint32_t commandSize, char *error,
                    uint32_t errorSize);
} AbcUiVoteMenuApi;

// ----------------------------------------------------------- playerlist ----

// The player-list page's model (roster snapshot, identity-keyed selection
// and the read-back mute toggle). The roster seam and the voice mute
// backend stay in the framework; a page module projects the snapshot and
// reports the select/toggle/back intents. Selection is keyed by the row's
// identity (SteamID64 when present, slot+name otherwise), so "same slot,
// new player" invalidates the selection instead of retargeting the action.
#define ABCUI_PL_FLAG_LOCAL 0x1
#define ABCUI_PL_FLAG_SPECTATING 0x2
#define ABCUI_PL_FLAG_BOT 0x4
#define ABCUI_PL_FLAG_MUTED 0x8

#define ABCUI_PL_NAME_MAX 64

// One roster row. `selected` is computed against the model's selection, so
// the page never keeps a second selection truth. `steamId64` is 0 for bots
// and LAN placeholders (their key is slot+name).
typedef struct AbcUiPlayerRow {
  uint32_t structSize;
  int32_t slot;       // 1..32 entity slot
  int32_t selected;   // computed against the model's selection
  uint32_t flags;     // ABCUI_PL_FLAG_*
  uint64_t steamId64; // 0 = no SteamID (bot / LAN placeholder)
  char name[ABCUI_PL_NAME_MAX];
} AbcUiPlayerRow;

// toggleMute outcomes (the model's revalidated toggle discipline: success
// only when the immediate backend read-back agrees with the request).
#define ABCUI_PL_TOGGLE_OK 0
#define ABCUI_PL_TOGGLE_UNAVAILABLE 1
#define ABCUI_PL_TOGGLE_NO_SELECTION 2
#define ABCUI_PL_TOGGLE_STALE 3
#define ABCUI_PL_TOGGLE_IDENTITY_CHANGED 4
#define ABCUI_PL_TOGGLE_LOCAL_PLAYER 5
#define ABCUI_PL_TOGGLE_READBACK_FAILED 6

typedef struct AbcUiPlayerListApi {
  uint32_t structSize;
  // Re-reads the live roster and revalidates the selection (idempotent per
  // frame; the page calls it before projecting).
  void (*refresh)(void);
  // Model revision; the page re-projects only when it moved.
  uint32_t (*revision)(void);
  int32_t (*rosterCount)(void);
  // Copies row `index`; 0 when out of range (structSize honored on write).
  int32_t (*rosterAt)(int32_t index, AbcUiPlayerRow *out);
  // Selects by the row's identity (the same values the row carried: a
  // SteamID64, or slot+name when the id is 0). A key without a live
  // occupant is refused, not remembered.
  void (*select)(uint64_t steamId64, int32_t slot, const char *name);
  int32_t (*muteAvailable)(void);
  // Revalidated toggle for the current selection. Returns an
  // ABCUI_PL_TOGGLE_* code and writes the read-back state on OK (either
  // argument may be null/absent).
  int32_t (*toggleMute)(int32_t *mutedAfter);
} AbcUiPlayerListApi;

// ------------------------------------------------- module descriptor ------

typedef struct AbcUiModuleDescriptor {
  uint32_t structSize;
  const char *id;      // must equal the manifest id
  const char *version; // informational
  // Called right after the entry returns: register panels here. The Rml
  // context does not exist yet - never touch the DOM.
  void (*onLoad)(void);
  // HUD_Init one-shot block: cvar/command registration only.
  void (*onInit)(void);
  // The Rml context exists; re-fired after every shutdown cycle because
  // module documents die with the context and must be rebuilt.
  void (*onContextReady)(void);
  // HUD_Frame, after the panel data pump, before the context update.
  void (*onFrame)(double frameTime);
  // HUD_VidInit, after the context resize.
  void (*onVidInit)(void);
  // While the context is still alive: release documents, listeners and
  // borrowed handles. Not re-fired until the next init/contextReady.
  void (*onShutdown)(void);
  // Process teardown: every service is gone; free everything else.
  void (*onUnload)(void);
} AbcUiModuleDescriptor;

// ---------------------------------------------------------------- table ---

typedef struct AbcUiModuleApi {
  uint32_t structSize; // sizeof(AbcUiModuleApi) of the running framework
  uint32_t abiVersion; // ABCUI_ABI_VERSION of the running framework
  const AbcUiCoreApi *core;
  const AbcUiConsoleApi *console;
  const AbcUiEventsApi *events;
  const AbcUiVfsApi *vfs;
  const AbcUiPanelsApi *panels;
  const AbcUiDataApi *data;
  const AbcUiUiApi *ui;
  const AbcUiCommandsApi *commands;
  const AbcUiWeaponsApi *weapons;
  const AbcUiPlayersApi *players;
  const AbcUiRadarApi *radar;
  const AbcUiShellApi *shell;
  const AbcUiVoiceApi *voice;
  const AbcUiScoreApi *score;
  const AbcUiInputApi *input;
  const AbcUiDocumentsApi *documents;
  const AbcUiMenuApi *menu;
  const AbcUiVoteMenuApi *votemenu;
  const AbcUiPlayerListApi *playerlist;
} AbcUiModuleApi;

#ifdef __cplusplus
} // extern "C"
#endif
