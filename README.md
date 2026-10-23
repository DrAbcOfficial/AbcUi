# AbcUi

HUD and menu plugin for Sven Co-op / MetaHookSv (`AbcUi.dll`): rebuilds the entire ABCEnchance interface on RmlUi, drawing exclusively through IMetaRenderer with zero direct OpenGL (textures upload through the VGUI2 surface channel). The shared [AbcCore](https://github.com/DrAbcOfficial/AbcCore) library is loaded as `AbcCore.dll` from an external source tree (`ABC_CORE_DIR`, sibling `../AbcCore` by default); AbcUi no longer vendors AbcCore as a submodule. The DLL exposes a Valve-style, versioned C ABI factory (`AbcCore_CreateInterface`). AbcUi owns the direct Renderer interface dependency and its render bridge, so AbcCore stays renderer-free.

## Build

Windows + MSVC + CMake >= 4.2, Win32 x86 only, C++20, dynamic CRT (/MD):

```bash
git clone --recursive https://github.com/DrAbcOfficial/AbcUi.git
git clone --recursive https://github.com/DrAbcOfficial/AbcCore.git ../AbcCore
cmake --preset win32-release
cmake --build --preset win32-release
```

The default `ABC_CORE_DIR` is the sibling `../AbcCore` checkout. If AbcCore is
elsewhere, pass `-DABC_CORE_DIR=<path>` when configuring. If AbcUi's own
submodules were not fetched by `--recursive`, run
`git submodule update --init --recursive` once at the repository root.

The pinned Renderer commit is currently published in the `DrAbcOfficial/Renderer` fork; when `MetaHookSv/Renderer` does not yet contain it, use the URL rewrite configured in `.github/workflows/build.yml` before fetching submodules.

## Deploy

Set the `ABC_GAME_DIR` environment variable to the game root before configuring (or pass `-DABC_GAME_DIR=...`); nothing is deployed when it is unset:

```bash
cmake --build --preset win32-release --target deploy
```

- UI assets go to `<game>/svencoop/abc/abcui/`, split by surface: GameUI documents, page styles, config and menu media live under `gameui/`; HUD documents, sprites and page art live under `hud/`; the cross-surface stylesheets (`rcss/base.rcss`, `rcss/theme.rcss`), language tables and shared art stay at the `abcui/` root.
- `AbcUi.dll` goes to `<game>/svencoop/metahook/plugins/`.
- UI module packages go to `<game>/svencoop/abc/abcui/modules/*.zip` (built by the `pack_modules` target; `deploy` installs them too when `ABC_GAME_DIR` is set).
- `FreeImage.dll` and `Mile.FFmpeg.dll` are provided by MetaHookSV/Renderer; AbcUi does not install or package a second copy.

## Packaging

```bash
cmake --build --preset win32-release --target dist
```

Produces `AbcUi-<version>.zip` under `build/<preset>/dist/` (plugin DLL,
`abc/abcui` assets, the packed UI modules under `abc/abcui/modules/`, README).
The module archives themselves are produced by `--target pack_modules`
(`build/<preset>/dist/modules/<id>-<version>.zip`) from the manifests and the
built module DLLs. `AbcCore.dll` and the shared gamedata catalog are
provided by the aggregate `ABCEnchance-<tag>.zip` / `AbcCore-<version>.zip`,
not by this module package. The archive contains `svencoop/` at its root: extract it directly into
the Sven Co-op root, with no extra version folder to copy.

## Data-driven menu

The main menu and settings categories are driven by JSON in `assets/abc/abcui/gameui/config/` (installed to `<mod>/abc/abcui/gameui/config/`): adding a file under `settings/` extends the pages and ordinary settings, loaded automatically at startup; labels may be literal text or `#token` lookups in `abcui/lang/*.txt`. Limits: 1 MiB per page, 64 JSON files, 512 items per page.

Settings sections may declare widget components alongside their rows; a card with no rows and no widget is not drawn at all. The multiplayer page's player section uses `model-preview` (the live model render) and `model-picker`: a list of every directory under `models/player` with a case-insensitive text filter, a favorites-only toggle and a per-row favorite star; double-clicking a row or picking it in the row's right-click menu also toggles the favorite. Clicking a row writes the `model` cvar immediately and the preview follows; the current engine value stays selected even across restarts. Favorites persist in `<mod>/abc/abcui/model_favorites.json` (`{"favorites": [...]}`, atomic writes); the picker never reads legacy files, and a damaged store loads read-only instead of being overwritten.

The ABC page's crosshair section uses `crosshair-preview` for the live shape and `crosshair-color` for two RGBA pickers: each picker edits one colour's red, green, blue and alpha channels (hue strip, saturation/value square and alpha strip) and commits the four drafts, replacing the eight per-channel sliders while the eight cvar rows stay declared as the apply/reset backbone. The section declares `"layout": "two-up"`: the preview and the pickers share a widget band above the rows, and the rows themselves are paired two per line, so the card stays compact and the preview is a fixed 220dp window instead of a full-height column. The options page is grouped into section cards; the advanced page's items are the game's own `user.scr` entries (the shipped `MultiplayerAdvancedDialog.res` only hosts the list), so a live script supplies the domains and defaults while the JSON keeps the card grouping and labels.

The in-game `+commandmenu` binding is also data-driven by
`gameui/config/commandmenu.json`; the legacy `commandmenu.txt` is not read.
Each entry has a `label` and either a `command` leaf or nested `items`; array
order supplies the one-based number shown in the left gutter. Numeric keys
activate entries directly, mouse/arrow/Enter navigation is supported, and an
optional `maps` array limits an entry to named BSPs (case-insensitive, without
the path or `.bsp`). Commands use the engine's normal client-command path, so
existing client cvars and server verbs keep working. `+commandmenu` opens or
closes the translucent RmlUi page and `-commandmenu` is consumed; command
entries and Esc dismiss the page. Literal labels are looked up in the active
language table, so the shipped English labels have complete Simplified
Chinese and Spanish translations; explicit `#token` labels also work.
The page uses compact fixed-width columns, keeps only the needed one to three
columns on screen and caps column height so it does not cover the play area.
Limits: 1 MiB per file, 100 items per menu and 40 nested levels.

```json
{
  "version": 1,
  "title": "#CommandMenu_Title",
  "items": [
    { "label": "Mission Briefing", "command": "missionbriefing" },
    { "label": "Player Model", "items": [
      { "label": "Gordon", "command": "model gordon" }
    ] }
  ]
}
```

## UI modules

AbcUi is a UI-free framework plus loadable UI-module packages. Every HUD panel
that has been migrated lives in `modules/<id>/` in this repository and ships as
one zip package installed at `svencoop/abc/abcui/modules/<id>-<version>.zip`
(real files, never inside a VPK). Packages are mapped with
`CreateFileMapping`; entries are stored uncompressed so the manifest, RML,
RCSS, art and the entry DLL are served as zero-copy views (DEFLATED entries
from third-party packages are inflated through miniz).

A package contains:

```text
manifest.json            id, name, description, author, version, abi,
                         minAbcUi, entry, dependencies, conflicts
entry/<abcui_id>.dll     the module DLL (Win32 x86, /MD)
<assets>/...             the module's assets tree: rml, rcss, img, spr and
                         the optional lang/<language>.txt tables
```

The framework scans the module directory once per process during `HUD_Init`,
validates every manifest (ABI, `minAbcUi`, dependencies, conflicts), loads the
packages in deterministic dependency order (ties broken by id), maps each entry
DLL by hand (no `LoadLibrary`) and drives it through the same lifecycle points
the built-in modules used: `onLoad`, `onInit`, `onContextReady`, `onFrame`,
`onVidInit`, `onShutdown`, and `onUnload` in reverse order at process
teardown. A module that fails validation is skipped together with its
dependents; the rest keep working.

Entries and callbacks must not throw across the C ABI: cross-image C++
unwinding is not guaranteed for manually mapped DLLs. Host guards defensively
handle exceptions that reach the host, not native memory faults or arbitrary
module failures. Report expected startup errors with
`core->failInitialization(reason)` and return normally; a zero entry return
still rejects the module. Reported entry/`onLoad` failures roll back framework
registrations before releasing the image. A failed `onInit` also retires its
dependents before they initialize. Optional shutdown/unload callbacks must
handle partial initialization and a context that may not exist yet; a null
`onLoad` is a valid no-op. Caught runtime exceptions have a neutral return,
not transactional rollback of module state.

Framework registrations retain their creating module's identity across nested
event, command, cvar, panel, document, DOM and input callbacks. Failure and final
unload revoke registrations even when a module omitted manual cleanup: events,
command handlers, cvar watches, panels, documents, file views and the vote gate
are process-scoped; DOM listeners and input tokens are also reclaimed at each
context shutdown. Commands/cvars already registered with the engine remain
process-owned (removed handlers become inert); engine commands and cvar value
changes are not undone. A command or cvar watch owned by another live module
cannot be replaced. All module callback gates close before final unload starts,
so one module's cleanup cannot call another module after its teardown. HUD
shutdown does not remove process-scoped subscriptions or re-install usermsg
hooks. Modules must still honor borrowed-element lifetimes and may explicitly
release their own resources earlier.
Service calls are main-thread-only; worker results must be handed off through
the module's main-thread callbacks rather than invoking the SDK from a worker.
Module-created workers and arbitrary allocations remain module-owned and must
be stopped/freed before `onUnload` returns.
New DOM listeners and input tokens are refused before context assembly or
during teardown. Documents created from a shutdown callback stay unloaded
until the next context instead of retaining a pointer into the dying one.
Automatic registration reclamation and initialization rollback require
AbcUi 0.8.1 or later: modules relying on these guarantees must declare
`"minAbcUi": "0.8.1"` (or newer). The module ABI remains version 1.
The appended `core->failInitialization` returns 1 when accepted during
Main/onLoad/onInit, or 0 outside initialization/already reported. It closes
new registrations immediately, with rollback after the current callback
returns. Use `ABCUI_API_HAS_MEMBER` before accessing it on an older host;
`core->error` is diagnostic output only, not a failure report.

### Native fallback and explicit hooks

From AbcUi 0.8.3, modules explicitly own native interception; an empty module
set leaves the original HUD, menu and commands available. Modules using these
semantics must declare `"minAbcUi": "0.8.3"` (ABI remains 1).

- `commands->subscribeCommand(name, priority, sink, userdata)` lazily installs
  a bridge for any existing callback-backed engine command, including commands
  from other plugins/modules. From 0.8.4 there is no command whitelist, fixed
  slot count or initialization-only restriction: hooks may be registered and
  revoked at runtime. ASCII case variants share one bridge; registered names
  containing punctuation/non-ASCII bytes and names longer than 63 bytes are
  accepted too. Supply a single name, not arguments or a command line
  (whitespace/control bytes, quotes and semicolons are rejected).
  Invalid/missing names, null native callbacks or unavailable engine seams
  return zero. Aliases, cvars and server-only verbs do not have a client
  callback to hook; use `console->registerCommand` first for a new client
  command. Every handler runs in priority order; only a nonzero return blocks
  the original. Revoke with `events->unsubscribe`; failure/unload also revokes
  automatically. During dispatch removed handlers are skipped and new ones
  join the next dispatch. Bridges remain process-owned pass-throughs after
  the last handler is removed: physically removing and reinstalling them
  could break a later plugin's saved predecessor and create a hook cycle.
  Modules requiring unrestricted dynamic hooks must set
  `"minAbcUi": "0.8.4"`; the existing C ABI and subscription tokens are unchanged.
- Subscribe with `events->subscribeVeto(ABCUI_CHANNEL_HUD_NATIVE_DRAW, ...)`
  to claim native HUD rendering. The `AbcUiNativeHudEvent::element` identifies
  health, battery, ammo, flashlight or crosshair (`ABCUI_NATIVE_HUD_*`). Return
  nonzero only while the replacing document is available. Health claims health
  and battery; ammo and weaponchoose each claim the combined native ammo/weapon
  selector element; flashlight and crosshair claim their respective elements.
  Native elements cannot be split into smaller draws (health also contains its
  damage indicators). The train is never suppressed. The framework clears only
  the active bit for the duration of native redraw and restores it afterwards.
- `netmsg.showmenu`, `netmsg.votemenu`, `textfeed.killfeed` and
  `textfeed.notice` have observer and veto subscriptions. Data subscribers do
  not consume messages; replacing modules explicitly subscribe with
  `subscribeVeto` and return nonzero when they can handle the payload. Missing
  modules/documents or no consuming handler chain the original message.
  The vote cvar gate and chat's existing per-message veto still apply.
- The gameui module explicitly claims `menu->setNativeTakeover(1)` during
  initialization (Main/onLoad/onInit only) and declares
  its six command hooks. Its handlers call `menu->dispatchCommand(name)` to
  run the framework-owned vote, command-menu and inventory flows; the framework
  no longer binds consuming handlers of its own. The takeover is exclusive
  and is revoked automatically on initialization failure/final unload.
  Check appended menu functions with `ABCUI_API_HAS_MEMBER` on older hosts.

Hook subscriptions retain module ownership and follow the existing automatic
rollback/unload cleanup. HUD shutdown does not drop process-scoped declarations;
replacement-document checks let the native HUD render between context cycles.

### Module collaboration and recovery UI

From AbcUi 0.8.5, modules collaborate through exact-version services and
module-owned data channels; the framework keeps no cross-module state. Modules
using these semantics must declare `"minAbcUi": "0.8.5"` (ABI remains 1).

- `core->registerService(name, version, handler, userdata)` publishes a
  byte-level request/response endpoint. Local names are `[a-z0-9_]+` (1..64)
  and the version is mandatory; a provider registers from Main/onLoad/onInit
  only. `core->serviceAvailable(provider, name, version)` and
  `core->callService(...)` address it by manifest id and exact version. Self
  calls are allowed; every other caller must list the provider in its manifest
  `dependencies`, so authorization follows the declared load order. Requests
  and responses are opaque byte buffers (at most 1 MiB each) whose layout is
  owned by the service version: no pointers, C++ objects or ownership
  transfers cross the boundary. Dispatch is synchronous, main-thread only and
  bounded to 16 nested calls; negative results are the reserved host errors
  (`ABCUI_SERVICE_UNAVAILABLE`, `_FORBIDDEN`, `_INVALID`, `_DEPTH`,
  `_FAILED`). Registrations are process-scoped and revoked on failure/unload.
- `events->publish(name, schemaVersion, payload, payloadSize)` publishes on
  `module.<id>.<name>` (local name `[a-z0-9_]+`, 1..64). The payload is
  borrowed for the dispatch only and travels as an immutable
  `AbcUiModuleEvent` envelope (structSize, schemaVersion, payloadSize,
  payload); consumers subscribe to that channel with `events->subscribe`.
  Engine and veto channels cannot be published.
- `documents->registerRecovery(desc)` names a module-owned recovery document
  that the framework loads whenever a registered page fails; the framework
  embeds no fallback page of its own. One provider per process (onLoad only)
  and its path must point into its own package. `onReady`/`onClose` run with
  the provider identity and a borrowed document handle;
  `documents->requestRetry` defers a retry to the frame pump, so the recovery
  document is never closed from inside its own event dispatch. The provider is
  optional: without one a broken page simply stays unloaded
  (`documents->degraded` still reports it), and it exits through
  `menu->quitNow`, the confirmed exit that does not depend on the
  quit-dialog resources.
- A package may ship `lang/<language>.txt`; keys must be prefixed with
  `<id>/` and are served through the same `ui->translate` lookup as the
  shared table, so a module localizes its own RML without touching framework
  assets. Retired packages stop being consulted immediately.

Module ids are lowercase alphanumeric segments separated by dots. Versions
(`version` and `minAbcUi`) are non-empty dot-separated decimal segments, with
no suffixes or empty segments; leading zeroes are numeric and omitted trailing
segments compare as zero. The entry is a relative package path, with no empty,
`.` or `..` segments; backslashes are normalized to `/`. `dependencies` and
`conflicts` are optional arrays of module ids. Packaging and startup both check
these fields; startup also checks the requested ABI and framework version.
Duplicate ids are rejected rather than selected by scan order. Conflicts are
mutual even if declared by only one module: the first successfully loaded
module in dependency-resolution order wins, and the conflicting module and
its dependents are skipped.

Modules talk to the framework through the pure-C `AbcUiModuleApi` table
(`sdk/abcui/`): versioned, append-only sections for console/cvars, the
priority event bus, package files, HUD panels, interactive page documents,
the replacement menu's presentation state, the HUD data snapshot, Rml
documents and elements, chained commands, the weapon registry, the player
resource and the settings page's row/bind/display/microphone models. Modules include `<abcui/module_api.h>`, export exactly one symbol
(`AbcUiModule_Main`), must not include RmlUi headers or link the framework's
static libraries, and must avoid static TLS and delay-loaded imports.
Veto callbacks return an `int`: every nonzero value consumes the event,
including values other than `1`; aggregate-veto channels still run every sink.
An entry DLL with no PE initialization entry point is supported. Failed import
binding or initialization releases the partial image without invoking its
detach routine; detach runs only after a successful process attach.

For output snapshots, initialize `structSize` to the size of your own SDK
type. A nonzero size is the caller's capacity: reads preserve it across
repeated calls, copy only the known prefix and leave unknown tails untouched.
A size smaller than the four-byte header is ignored. Zero retains the legacy
full-current-size write; it is not safe for cross-version callers and must
not be used by new modules.

The ABI v1 layouts of `AbcUiWeaponSprite`, `AbcUiWeaponInfo`,
`AbcUiConsoleLine` and `AbcUiConsoleSuggestion` are frozen. Legacy array
functions use those fixed element sizes, not each row's `structSize` as a
stride. New code should prefer the appended `weapons->listEx`,
`consolepage->copySinceEx` and `consolepage->completeEx` functions. Check
`ABCUI_API_HAS_MEMBER(SectionType, section, function)` before accessing an
appended function; older frameworks can still use the legacy paths.
The Ex functions take the caller's explicit byte stride and require every
returned row's nonzero capacity to be between four bytes and that stride.
They return `-1` without writing any rows for invalid output layouts;
stride padding and unknown tails remain untouched. Null outputs and zero
counts keep the corresponding legacy query/no-op behavior. A larger record
needs a new type and the stride-aware path, not an enlarged legacy array element.

First-party modules in this repository: flashlight, textmenu, vote, motd,
weaponchoose, killfeed, textfeed, sidetext, health, ammo, hudeffects,
crosshair, damage, itemhighlight, playerboard, radar, scoreboard, chat and
`abcui.gameui` (every replacement game-menu surface: the shell, the console,
the server browser, the connection surfaces and the vote creation, player
list, command menu, inventory, create-game and settings pages, the
display-mode confirmation and the quit confirmation); `demo` is the minimal
example. `tools/pack.py` validates each manifest and writes the archives.
`AbcUi.dll` is UI-free: it ships no page, stub, HUD panel or panel catalog,
and every menu document, its stylesheet and the settings page tree live in
the module packages.

## Feature overview

- HUD panels: Health / Flashlight / AmmoBar / AmmoStack / ItemStack / WeaponStack / Notice / DeadMsg / SideText / TextMenu / DamageTiles / PopNum / Indicator / player board / scoreboard / vote / MOTD / weapon choose / item highlight / Radar / chat and more; the HUD palette is the Half-Life amber (`#ffb000`, the old scheme's "Orange") defined once as `--abc-hud-*` tokens in `rcss/base.rcss`, matching the amber menu accent; the health/armor vitals follow the Half-Life 2 readout - an amber icon plus one large value drawn straight on the scene - while the bar meters under each value are kept as the at-a-glance indicator; the value ink and the meters are additive (`texadd://` glyph and meter textures composed by `framework::glyphs`, styled by the reusable `.hud-glyph` / `.hud-meter` classes in `base.rcss`), so they glow into the scene exactly like the HUD sprite icons and need no outline or black meter well; the ammo readout is the matching HL2 pool line - a large clip value, a stepped-down `/reserve`, the ammo sprite standing on the same baseline, and the pools packed into the bottom-right corner; data layer with NetworkMessages / WeaponResource / HudState / TextFeed, and the resource layer under `src/rmlui/texture/` with SpriteResource (GoldSrc .spr decode + hud.txt), animated images through the generic `<animimg>` element (`src/rmlui/animated/`: a format-backend table decodes the container into one dynamic texture per element - animated WebP today, APNG for Steam animated avatars later), which the water-exit splash uses (two additive animated WebP files at 10 fps, a fraction of the old .spr size); the original HUD list is resolved through gamedata and only module-claimed elements are hidden during the chained HUD_Redraw call.
- Chat: the message-mode input and the player chat log are an RmlUi HUD module (`modules/chat/`), with the native chat input and `CHudSayText` both retired. The chained `messagemode`/`messagemode2` commands open the input line instead of the native one (the command bridge carries no argv, so an argumented `messagemode` also opens the line; send text through `say`/`say_team` directly when a script needs it), Enter submits through `say "<text>"` / `say_team "<text>"` with the payload stripped of quotes and control characters before assembly, Esc closes, and Up/Down walk a 32-entry send history; text, IME and the caret ride the shared input bridge, so a CJK composition commits into the same editor, and the composition/candidate panel is drawn by `src/rmlui/ImeOverlay.cpp` at the caret: RmlUi underlines the composing string inline while the overlay supplies the composition header and the candidate page the engine gives no window for, read from the IME's own list (`ImmGetCandidateListW`) as one horizontal row of at most nine entries, each prefixed with the number key that picks it and the selected row in amber; the open line also carries the active input method's short tag (`CN`/`TW`/`HK`/`JP`/`KR`) at its right edge and hides it when the layout has no IME. `SayText` is parsed in the data layer and swallowed so exactly one log is drawn: rows keep the server's colour control bytes (normal text, per-player name tint from the client's `GetClientColor`, location), split the client's `(TEAM)` / `*DEAD*` / `*SPEC*` decorations into their own run, honour positional `%s`/`%sN` substitution on HLSDK-shaped servers, and escape every server string before it reaches the DOM. The log sits bottom-left in the HUD palette (translucent row plates, amber channel ink), wraps long lines inside a 190dp column, grows upward from the input row, and shows a compact window of at most six rows (capped from `hud_saytext_lines`) over a 256-message scrollback: the thin scrollbar and the mouse wheel browse older lines while the input line is open (the wheel never switches weapons then), the tail follows new lines unless the reader scrolled up, and the whole block fades out one second after `hud_saytext_time` elapses; every line is also mirrored into the console the way the native handler did, and emoji render in colour through the Segoe UI Emoji fallback face (RmlUi asks FreeType for FT_LOAD_COLOR and uploads the RGBA glyph atlas); `hud_saytext 0` disables it and `hud_saytext_block_muted 1` drops lines from voice-muted players. Ownership is the input router's: the open input line holds an interactive-HUD token, and the engine is told the UI consumed the key at the CBaseUI key seam (`IVGUI2Extension_BaseUICallbacks::Key_Event`), which is the same route the native chat entry and CaptionMod's VGUI dialog use - the binding simply never runs, so WASD stops and no usercmd filtering is involved. Edges the router passes through for clean pairing (keys held when the line opened) are left to the engine. The token is released on Esc, focus loss, the pause menu or a map change.
- Resource files (sprites, images and RML documents) resolve through the engine `IFileSystem`, the same mounted view VGUI2Extension uses. Custom server content in `<mod>_downloads`, `<mod>_addon` and other engine search paths therefore participates without CWD-relative path guessing.
- Menu: main menu, settings (general/input/mouse/video/audio/voice/captions/multiplayer/advanced/menu/renderer), server browser, create game, player list, vote creation (ban/kick/kill/map with the server's map list), connect/loading/error pages, console, direct-connect/password dialogs, quit confirmation, and the nested in-game command menu; the loading page stays above the page shells and the console while a load is in progress, while the connection dialogs and the quit confirmation remain above it; a server `changelevel` also raises the loading page (map-load title/status, no connect address) instead of dropping to the no-map main menu and its backdrop movie. Text input goes through the IME-aware input bridge. The pause menu's **Vote Menu** row and the original `votemenu` command both open the RmlUi page, replacing the legacy GameUI vote dialog. The original `+commandmenu` VGUI window is replaced by a translucent, left-anchored RmlUi menu that leaves the game view visible. The original `inventory` / `+inventory` / `-inventory` viewport window is replaced by an RmlUi inventory window: the chained commands swallow the native `CInventoryPanel`, and the page projects the server's `InvAdd`/`InvRemove` item stream with the native captions and fallbacks (`You haven't got any inventory`, `No item selected`, `Nameless item`, `No item description`). The dialog is a centred panel over a dimmed scrim (clicking outside also closes it): the item list with its count on the left, a detail card on the right showing the item name, description and the server's can-use/can-drop flags as tags, and the action row at the bottom. A row click selects, Up/Down walk the list, Enter activates, Delete drops, and the three action buttons send the native `activateitem "#<id>"` / `dropitem "#<id>"` / `dropitem "*"` verbs; any action, Esc, the close button, the scrim, `-inventory` or the bound key closes the window. Actions are read-through: a row disappears only when the server's `InvRemove` arrives, so a refused action never rewrites the list optimistically. The console wraps output to the current window width, supports mouse selection with Ctrl+C copy or a right-click Copy menu (no selection copies the visible output), and the console hotkey returns straight to gameplay from the in-game pause page (the no-map main menu stays open). Tab completes the editor against the client's cvar/command catalog: a unique match completes in place, several matches extend to their common prefix and open a candidate list that further Tab presses cycle (Enter runs the highlighted candidate, Esc closes only that list); Up/Down keep walking the submission history.
- Menu backdrop: `abc_ui_menu_video` picks the looping movie behind the no-map menu (blurred by `abc_ui_menu_backdrop_blur` gaussian rounds, 0..4, default 2; 0 shows the unblurred capture); in a level the pause menu blurs the live frame instead.
- Voice mute: the player list's Mute button, the scoreboard's right-click **Mute Voice** entry and the per-row muted badge share one persisted mute list at `<mod>/abc/abcui/voice_ban.dt` (JSON `{"bans": ["<steamid64>"...]}`, atomic writes; the legacy files are never read). The list is mirrored to the server as a 32-slot `vban` mask with `VModEnable`, re-synced on the server's ReqState, on every toggle and once per second, so state survives restarts and map changes. Bots, the local player and SteamID-less rows are not mutable; every surface reads the state back from the service, so a refused toggle never shows success.
- Retained cvars: `hud_danger_health`, `hud_danger_armor`, `hud_deathnotice(+_time/_count)`, `hud_sideinfo`, `abc_ui_chat` (0 = native chat, 1 = AbcUi chat unless CaptionMod's `cap_newchat` owns it; set `cap_newchat 0` to hand chat to AbcUi); in-game diagnostics: `abc_ui_debugger` (RmlUi debugger) and `abc_ui_probe` (font/panel/element state dump).
- Avatars and real names come from the dynamically bound game steam_api.dll (Bridge/Native backend abstraction, no steam_api.lib import dependency); fonts are loaded from the system font directory through `Rml::LoadFontFace` (Verdana/Tahoma, Microsoft YaHei for CJK, Consolas, and Segoe UI Emoji/Symbol as the colour-emoji and symbol fallbacks, in that order so colour glyphs win over the monochrome symbols).

## Runtime requirements

`plugins.lst` order (AbcUi must load after Renderer; it performs a fatal check on Renderer at startup):

```text
VGUI2Extension.dll
Renderer.dll
AbcEssential.dll
AbcUi.dll
```

`AbcCore.dll` is a shared dependency, not a plugin: place it at
`svencoop/metahook/dlls/AbcCore/AbcCore.dll` (the aggregate package does this).
The Windows loader resolves it when `AbcUi.dll` loads; the plugin-facing
contract is the versioned `AbcCore_CreateInterface` table, not exported C++
symbols.

## Submodules

| Path | Upstream | Pin |
|---|---|---|
| `thirdparty/Renderer` | https://github.com/MetaHookSv/Renderer | `ee7c1bd601f3cecd03fa0496db2cf0c4a919a452` (public interfaces and `gl_common.h` sync) |
| `thirdparty/RmlUi` | https://github.com/mikke89/RmlUi | tag `6.3` (`ba95ffe8bfb6370efb2cdcca927eaad4710c5413`) |
| `thirdparty/freetype` | https://github.com/freetype/freetype | tag `VER-2-13-3` (`42608f77f20749dd6ddc9e0536788eaad70ea4b5`) |
| `thirdparty/FreeImage` | https://github.com/hzqst/FreeImage_clone | `007c9e4c5d4198a1646b6c5274fd855be9cca7ef` |
| `thirdparty/VGUI2Extension` | https://github.com/MetaHookSv/VGUI2Extension | `01f9ad225aef5815eb1c02539667a9872cc2a511` (headers only) |
| `thirdparty/miniz` | https://github.com/richgel999/miniz | tag `3.1.2` (`77d0dce8627735138c51770d1799a1ef48f2117d`, read-only zip packages) |
| `thirdparty/SteamSDK` | https://github.com/MetaHookSv/SteamSDK | `3c1abaf6277f9f99fd16ef40557d6852820b848f` |

## Shared dependency

AbcCore is managed by the aggregate
[ABCEnchance](https://github.com/DrAbcOfficial/ABCEnchance) repository. Set
`ABC_CORE_DIR` to its source tree when building AbcUi standalone; the default is
`../AbcCore`. When building from the aggregate repository, point
`ABC_CORE_DIR` at its root `AbcCore/` checkout.

## License

Licensed under the [BSD 3-Clause License](LICENSE); each dependency keeps its own license.
