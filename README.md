# AbcUi

HUD and menu plugin for Sven Co-op / MetaHookSv (`AbcUi.dll`): rebuilds the entire ABCEnchance interface on RmlUi, drawing through a loadable render backend (issue #24: `AbcUi.dll` itself has zero Renderer headers and zero OpenGL calls; the shipped `renderer` backend is a DI-loaded DLL in `abc/abcui/render_backend/` that wraps IMetaRenderer, and textures upload through the VGUI2 surface channel). The shared [AbcCore](https://github.com/DrAbcOfficial/AbcCore) library is loaded as `AbcCore.dll` from an external source tree (`ABC_CORE_DIR`, sibling `../AbcCore` by default); AbcUi no longer vendors AbcCore as a submodule. The DLL exposes a Valve-style, versioned C ABI factory (`AbcCore_CreateInterface`). The `abcui_render_renderer` backend DLL owns the direct Renderer interface dependency, so both AbcCore and the AbcUi framework stay renderer-free.

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

- All UI resources ship inside module ZIPs. There is no loose framework UI asset tree, embedded page, default image or global language table. Remove obsolete loose assets from previous installations; they are not consulted. The only loose tree is native Studio content: the main menu's action-group models go to `<game>/svencoop/abc/abcui/models/menu_action/` because an engine Studio load needs a real file in the game search path (it cannot read a module ZIP).
- `AbcUi.dll` goes to `<game>/svencoop/metahook/plugins/`.
- The render backend `abcui_render_renderer.dll` goes to `<game>/svencoop/abc/abcui/render_backend/` (loaded by the `abc_ui_render_backend` cvar; a missing or unusable selected backend is fatal at startup - there is no built-in fallback).
- The UI-owned layout and model-preview catalog goes to
  `<game>/svencoop/metahook/gamedata/abc/abcui-layout/`. Deploy the complete
  catalog together with the plugin; its Windows engine/client records are
  required for model preview at the main menu as well as in a level.
- UI module packages go to `<game>/svencoop/abc/abcui/modules/*.zip` (built by the `pack_modules` target; `deploy` installs them too when `ABC_GAME_DIR` is set).
- The main menu's native Studio content goes to `<game>/svencoop/abc/abcui/models/menu_action/` (`display_male.mdl` and `display_female.mdl` ship there; add further action groups, or point `abc_ui_menu_action_model` at any `.mdl` the engine can load).
- `FreeImage.dll` and `Mile.FFmpeg.dll` are provided by MetaHookSV/Renderer; AbcUi does not install or package a second copy.

## Packaging

```bash
cmake --build --preset win32-release --target dist
```

Produces `AbcUi-<version>.zip` under `build/<preset>/dist/` (plugin DLL,
the packed UI modules under `abc/abcui/modules/`, the loose Studio action
groups under `abc/abcui/models/`, the UI-owned
`metahook/gamedata/abc/abcui-layout/` catalog, README).
The module archives themselves are produced by `--target pack_modules`
(`build/<preset>/dist/modules/<id>-<version>.zip`) from the manifests and the
built module DLLs. `AbcCore.dll` and the shared gamedata catalog are
provided by the aggregate `ABCEnchance-<tag>.zip` / `AbcCore-<version>.zip`,
not by this module package. The archive contains `svencoop/` at its root: extract it directly into
the Sven Co-op root, with no extra version folder to copy.
The build validates the UI-owned catalog's snapshot hashes and required
Windows view symbols offline; Linux records do not satisfy that check.

## Data-driven menu

The main menu and settings categories are driven by JSON in `modules/gameui/assets/gameui/config/` (inside the `abcui.gameui` package): adding a file under `settings/` extends the pages and ordinary settings, loaded automatically at startup; labels may be literal text or `#abcui.gameui/token` lookups in that module's `lang/*.txt`. Limits: 1 MiB per page, 64 JSON files, 512 items per page.

Settings sections may declare widget components alongside their rows; a card with no rows and no widget is not drawn at all. The multiplayer page's player section uses `model-preview` (the live model render) and `model-picker`: a list of every directory under `models/player` with a case-insensitive text filter, a favorites-only toggle and a per-row favorite star; double-clicking a row or picking it in the row's right-click menu also toggles the favorite. Clicking a row writes the `model` cvar immediately and the preview follows; the current engine value stays selected even across restarts. Favorites persist in `<mod>/abc/abcui/model_favorites.json` (`{"favorites": [...]}`, atomic writes); the picker never reads legacy files, and a damaged store loads read-only instead of being overwritten.

The **Menu** settings page includes an action-model dropdown
(`abc_ui_menu_action_model`, source `menu-action-models`) scanning loose `.mdl`
files in `abc/abcui/models/menu_action/`, an action-sequence dropdown
(`abc_ui_menu_model_sequence`, source `menu-action-sequences`) showing each
sequence's index and actual MDL label, and a weapon-path text field
(`abc_ui_menu_weapon_model`). Changing the action-model draft immediately
rebuilds the sequence choices, including labels even when the indices are
unchanged. A valid selected index is retained; an index absent from the new
model falls back to its first sequence. These changes remain drafts until
**Apply**; **Cancel** restores the engine model and its sequence choices.
The weapon field accepts only one complete path relative to the game content
directory, including the directory and `.mdl` extension, for example
`models/p_m16.mdl`. The path is used exactly as entered: no directory/extension
is added and no alternative path is tried. Bare names (`p_m16`, `p_m16.mdl`),
absolute machine paths and parent traversal are rejected by the settings
validator. The same rules apply to the `<model weapon>` attribute. The default
is `models/p_m16.mdl`, and empty hides the weapon.
Existing saved values are preserved; resetting the Menu settings defaults
selects the M16 again. The weapon and player override fields
accept empty text (`"allowEmpty": true` on a `text` row; other text rows keep
their non-empty validation), with the existing 63-byte text-field limit.

The ABC page's crosshair section uses `crosshair-preview` for the live shape and `crosshair-color` for two RGBA pickers: each picker edits one colour's red, green, blue and alpha channels (hue strip, saturation/value square and alpha strip) and commits the four drafts, replacing the eight per-channel sliders while the eight cvar rows stay declared as the apply/reset backbone. The section declares `"layout": "two-up"`: the preview and the pickers share a widget band above the rows, and the rows themselves are paired two per line, so the card stays compact and the preview is a fixed 220dp window instead of a full-height column. The options page is grouped into section cards; the advanced page's items are the game's own `user.scr` entries (the shipped `MultiplayerAdvancedDialog.res` only hosts the list), so a live script supplies the domains and defaults while the JSON keeps the card grouping and labels.

The in-game `+commandmenu` binding is also data-driven by
`abcui.gameui`'s `gameui/config/commandmenu.json`; the legacy `commandmenu.txt` is not read.
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
  "title": "#abcui.gameui/CommandMenu_Title",
  "items": [
    { "label": "Mission Briefing", "command": "missionbriefing" },
    { "label": "Player Model", "items": [
      { "label": "Gordon", "command": "model gordon" }
    ] }
  ]
}
```

## UI modules

AbcUi 0.9.1 uses **ABI 2**, with no ABI-1 compatibility layer. Rebuild all
modules against `sdk/abcui/`; replace old module ZIPs rather than keeping two
versions of the same id. Module ids are lowercase dotted identifiers, and
versions are dot-separated decimal segments. When one id is packaged more than
once (an upgrade extracted over an older install), the package with the highest
manifest version is used and the rest are skipped with a console note; two
packages claiming the same version are ambiguous and the id is skipped entirely.

Every UI resource belongs to a module under `modules/<id>/assets/` and ships
inside its ZIP at `svencoop/abc/abcui/modules/`. The framework provides RmlUi,
input, localization lookup, VFS, decoders, texture upload, rendering and engine
data, but no shipped UI resources or hard-coded resource defaults.

A code package contains `manifest.json`, its entry DLL and its resource tree.
The manifest declares `id`, `name`, `description`, `author`, `version`, `abi`,
`minAbcUi`, `entry`, `dependencies` and `conflicts`. `type` defaults to `module`.
A **resource-only** package declares `"type": "resources"` and must omit
`entry`; it participates in validation, dependency ordering and unmounting but
has no DLL or callbacks. `abcui.base` is such a package: only the default
modules' shared `rcss/base.rcss`, `rcss/theme.rcss` and associated shared art.
The framework neither requires nor automatically loads those styles. Default
modules explicitly depend on and link `mod://abcui.base/rcss/base.rcss`.
A third-party module may use completely independent styles and dependencies.

Packages are memory-mapped; `tools/pack.py` stores all entries uncompressed
for zero-copy access. Third-party DEFLATED entries are inflated into owned
buffers. Nothing is extracted to a temporary directory, including DLLs,
video and audio. Debug builds also package the existing development probes'
documents; Release packages omit those development resources.

### Resource VFS and ownership

- A resource URI is `mod://<id>/<path>`, case-sensitive and read-only. Relative
  document references stay in the owning package. `.` and `..` normalize
  without escaping the package root; absolute disk paths, backslashes,
  control bytes and query/fragment syntax are not package paths.
- `vfs->open(uri, &view)` returns an `ABCUI_*` resource result. Views expose
  immutable bytes through `viewData`/`viewSize` and are released with
  `viewFree`. A view pins its ZIP mapping, even if the provider is unmounted.
  Views are reclaimed on owner failure/unload. Packages are limited to 1 GiB and individual entries to 128 MiB. The creator alone can use or
  free its handles.
- `vfs->resolve` canonicalizes a resource reference; `enumerate` visits sorted
  recursive package entries. SDK access to another package requires a direct
  manifest dependency. Document links and custom media elements apply the
  same dependency rule. No global overlay/search order or loose-file fallback
  exists. Explicit engine-content reads remain separate (`readEngineFile`,
  `engine://` images and native sprite/model services); they do not expose the
  former loose framework resource tree.
- `menuconfig->setResourceRoot` lets the menu module declare its configuration
  root during startup. The default gameui module supplies its packaged menu,
  settings, command-menu and server-option definitions. The framework keeps no
  fallback menu definition. Media dropdowns enumerate the package, not a disk
  directory; to add menu media, update/repack the gameui ZIP.
- Each module owns `lang/<language>.txt` (`key = value`, UTF-8). Keys carry
  `<id>/` prefixes; English loads first and the active language overrides it.
  SDK lookups without a prefix use the current module's namespace. RML uses
  fully qualified `#<id>/<key>` tokens. There is no shared/global translation
  table, and unmounted packages stop supplying translations.

Resource APIs use `ABCUI_OK` (0) and negative errors: INVALID, NOT_FOUND,
FORBIDDEN, UNAVAILABLE, DECODE, LIMIT, PENDING and UPLOAD. Errors never select
an image or substitute UI on a module's behalf.

### Textures, avatars, radar and input

`textures->upload` accepts straight-alpha RGBA8; `textures->load` decodes an
image from an authorized package URI. Both produce module-owned,
context-lifetime handles. `textures->uri` exposes a generated texture source;
`release` frees it early. The additive upload flag selects **src*alpha + dst**
through the render backend, never source-over or direct OpenGL.

Flashlight, ammo-pickup and sidetext modules generate their own HUD digit
textures at runtime. The fixed glyph cell, baseline, tint, opacity and additive
composition remain; there is no framework glyph atlas, pre-generated glyph
path or `ui->inkMarkup` API. Shared module-side code is in `modules/common/`;
it is compiled into each consumer, not linked into the framework.

`players->avatar` fetches/polls the real Steam avatar and returns a status and
upload generation. `players->avatarAnimated` is the appended animated-avatar
surface: it probes the Steam equipped-profile items, downloads the APNG through
ISteamHTTP without blocking the HUD thread, and caches it per SteamID under the
same 256-entry bound plus an 8 MiB compressed-byte budget. The bytes are
mirrored to `<mod>/abc/abcui/avatarcache/<steamid64>.apng` behind a versioned
header with atomic replaces, so reopening the scoreboard does not refetch.
The scoreboard emits the framework's generic `<animimg>` for a ready
animation (`avataranim://<steamid64>?gen=<n>`) and the ordinary
`avatar://` `<img>` otherwise; rows without a Steam identity (bots) play the
module's bundled APNG default through `<animimg>` as well, so the fallback is
a live animation instead of a frozen first frame, and a shared playback clock
keeps a rebuilt row at the animation position its predecessor had reached; a
closed scoreboard starts no profile or
network work, and every failure degrades to the static avatar or the module's
bundled default. Each consumer chooses its own default image on any error;
the framework bundles no avatar fallback. Weapon sprite records likewise
carry resource status; ammo and weaponchoose own their fallback atlases, while
crosshair owns its zoom/autoaim substitution policy. Radar uploads its own
square/round masks through `textures` and selects them with `radar->setMask`;
the framework only produces the world FBO and borrows the selected texture.

IME remains in the input bridge only as platform composition/candidate/caret
state. `input->imeState` exposes that snapshot **only to the module owning the
focused editor**. Chat and gameui maintain their own candidate-window RML,
styles and presentation. Decoration documents never steal focus or input.

### Memory media playback

Video uses a pinned package view and FFmpeg custom read/seek callbacks.
Decode work runs on a bounded worker; close joins it before releasing the
view. Demuxer/protocol restrictions reject playlists and external file or network references. Video elements remain silent. There are no filename remaps or default
movies in the framework.

`audio->open` starts asynchronous package-memory decoding through the existing
Mile.FFmpeg runtime. PCM is resampled to 44.1 kHz stereo signed 16-bit and
queued to an independently owned device through the **game's loaded SDL2**
exports. No FMOD signatures, extra SDL DLL, engine `mp3` command, temporary
files or changes to the engine's audio callbacks are required. CPU decode
backpressure and a roughly quarter-second playback queue bound buffering.
The channel starts paused; modules poll `status`, set linear volume, pause or
close it. Context shutdown/failure/unload joins workers and closes only the
module's audio device/subsystem reference, never the engine's SDL runtime.
Unavailable backends return errors without an implicit disk fallback.

The gameui module owns `abc_ui_menu_music`, `abc_ui_menu_video` and
`abc_ui_menu_backdrop_blur`. Defaults are package URIs under
`mod://abcui.gameui/gameui/scence/`. Music plays only for the no-level menu,
pauses while hidden/in-game/map-loading, and follows `mp3volume`. The engine's
own menu track (`media/gamestartup*.mp3`, played on the engine mp3 stream) is
stopped once per no-level menu presentation, so it does not keep sounding
underneath the module's channel (the legacy engine-track music used to replace
it simply by sharing the stream). Changing the music source replaces the
channel. Previously archived loose-media paths are not compatible: reset those
cvars or select a packaged track/movie.

### Media player

`abcui.mediaplayer` replaces the client's VGUI "Sven Co-op Media Player"
window (HOME, command `MediaPlayer`, issue 11). The module chains that command,
swallows the native window and presents its own Rml document; ESC or the close
button dismisses it, and the native window never appears.

Playback goes through the engine's own mp3 stream (`mp3 playfile`,
`mp3 pause`, `mp3 resume`, `mp3 stop`), never the client's FMOD media player
and never a new decoder. The engine path is the only one that can start an
arbitrary playlist entry by path and still pause/resume, so the page owns the
transport. Volume is the engine's `MP3Volume` cvar, which the volume slider
writes (the level's CD soundtrack shares that stream). A map change stops the
player and closes the page because the level owns the engine stream again;
closing the page leaves the music running, and quitting stops it.

The playlist is `playlist.txt` (or `playlist.m3u`, with nested `.m3u`
expansion) from the game configuration directory, read the same way the native
window's `RelPls` does. When neither file exists the page lists the built-in
Half-Life, Prospero and Suspense soundtrack instead of starting empty.
Add/Remove/Up/Down edit the session list only: the module ABI has no
filesystem write path, so the playlist file is not rewritten (the native
playlist editor tool owns that). Only MPEG audio is played; the engine appends
`.mp3` to a path without the extension and cannot open other containers, and
its 200-entry per-process track table caps the playlist at 160 entries.
Progress is estimated from the MP3 frame headers because the engine exposes no
stream position; that estimate drives the automatic next-track hand-off.
Server-driven Playlist messages are not routed to modules, so a playlist the
server names is picked up after the next map change or a manual Reload.

### Lifecycle and C API

Code modules export only `AbcUiModule_Main` and use pure-C tables and POD
payloads. No RmlUi headers, framework static-library linkage, static TLS or
delay imports are allowed. Calls are main-thread-only; the host's decode
workers never invoke SDK/engine APIs. Callbacks must not throw across the C
boundary; report startup failure with `core->failInitialization`.

The loader validates ABI, minimum version, dependencies and mutual conflicts,
then loads in deterministic topological/id order. Failed modules and their
dependents are skipped. Code callbacks are `onLoad`, `onInit`,
`onContextReady`, `onFrame`, `onVidInit`, `onShutdown` and `onUnload`; final
teardown reverses load order. HUD context cycles rebuild panel wrappers and
module documents without repeating process-scoped command/cvar/event hooks.
Registrations retain module identity through nested callbacks and are revoked
on failure/unload; context-owned listeners, input claims, textures and audio
are also reclaimed on each HUD shutdown. Initialize output `structSize` to
the SDK record size.

Modules explicitly intercept native commands/HUD/netmessages; an empty module
set leaves native UI available. `commands->subscribeCommand` installs a
process-owned pass-through bridge for an existing callback-backed engine
command; nonzero veto results consume it, and unsubscribe revokes only the
subscriber. HUD interception is per native element. No unavailable replacement
should consume the native path. Usermsg hooks install once and re-chain only
after a client DLL reload, not every HUD context cycle. A replacement feature
that competes with another plugin over a shared surface resolves it at the
competing plugin's own switches (the chat forces CaptionMod's `cap_newchat`
off while it is enabled) instead of fighting for the hook-chain head.

`core->registerService`/`callService` provide dependency-authorized,
exact-version byte services. Module events use `module.<id>.<name>` envelopes;
priorities preserve deterministic registration order. An optional
module-owned recovery document can handle failed pages; the framework embeds
no fallback page. See `sdk/abcui/events.h` and `module_api.h` for contracts.

First-party packages: base, recovery, flashlight, textmenu, vote, motd,
weaponchoose, killfeed, textfeed, sidetext, health, ammo, hudeffects, crosshair,
damage, itemhighlight, playerboard, radar, scoreboard, spectator, chat and
gameui. `demo` is the opt-in SDK example, not part of the default distribution.

## Feature overview

- HUD panels: Health / Flashlight / AmmoBar / AmmoStack / ItemStack / WeaponStack / Notice / DeadMsg / SideText / TextMenu / DamageTiles / PopNum / Indicator / player board / scoreboard / vote / MOTD / weapon choose / item highlight / Radar / chat and more; the HUD palette is the Half-Life amber (`#ffb000`, the old scheme's "Orange") defined once as `--abc-hud-*` tokens in `rcss/base.rcss`, matching the amber menu accent; the health/armor vitals follow the Half-Life 2 readout - an amber icon plus one large value drawn straight on the scene - while the bar meters under each value are kept as the at-a-glance indicator; flashlight percentages, pickup counts and numeric sidetext use module-generated additive glyph textures (`moduletex://`, styled by `.hud-glyph` in the base module); the ammo readout is the matching HL2 pool line - a large clip value, a stepped-down `/reserve`, the ammo sprite standing on the same baseline, and the pools packed into the bottom-right corner; data layer with NetworkMessages / WeaponResource / HudState / TextFeed, and the resource layer under `src/rmlui/texture/` with SpriteResource (GoldSrc .spr decode + hud.txt), animated images through the generic `<animimg>` element (`src/rmlui/animated/`: a format-backend table decodes the container into one dynamic texture per element - animated WebP, plus an APNG backend that assembles each frame as a single-frame PNG for FreeImage and applies the APNG dispose/blend model, used by the water-exit splash and by Steam animated avatars in the scoreboard), which the water-exit splash uses (two additive animated WebP files at 10 fps, a fraction of the old .spr size); the grenade proximity marker keeps the legacy 24x24 icon but no longer vanishes when the grenade leaves the view - it pins itself to the screen edge with a direction triangle (behind the player included) and stays inside the viewport; the original HUD list is resolved through gamedata and only module-claimed elements are hidden during the chained HUD_Redraw call.
- Spectator HUD: the native viewport `SpectatorPanel` bottom bar is retired in favour of `modules/spectator/` (mode title, target name, health and armor). The replacement draws as an opaque full-width 39dp bottom strip (the native strip's 26 YRES units in dp) that masks the native panel outright - the client's observer panel is a VGUI subtree the vgui2 paint gate's traversal never reaches, so the per-element `ABCUI_NATIVE_HUD_SPECTATOR` claim through `hud.native.draw` is kept as the seam for engines that do expose the subtree, not as the suppression mechanism. The bar only shows while the local player's observer mode (`entity_state.iuser1`) is non-zero. Modes follow the client's own mapping (`1` Free Chase-Camera, `2` Free Roaming, `3` Locked Chase-Camera; `4` uses a generic Spectator Mode label because the client ships no `Spec_Mode4` string). Health/armor come from the player resource (the same `g_PlayerExtraInfo` rows the native panel reads); a dead target shows `Dead` and hides the armor cell, and a missing target hides both vital cells. `abc_ui_spectator 0` restores the native bar.
- Chat: the message-mode input and the player chat log are an RmlUi HUD module (`modules/chat/`), with the native chat input and `CHudSayText` both retired. The chained `messagemode`/`messagemode2` commands open the input line instead of the native one (the command bridge carries no argv, so an argumented `messagemode` also opens the line; send text through `say`/`say_team` directly when a script needs it), Enter submits through `say "<text>"` / `say_team "<text>"` with the payload stripped of quotes and control characters before assembly, Esc closes, and Up/Down walk a 32-entry send history; text, IME and the caret ride the shared input bridge, so a CJK composition commits into the same editor, and the composition/candidate panel is drawn by each editor module at the caret: RmlUi underlines the composing string inline while the overlay supplies the composition header and the candidate page the engine gives no window for, read from the IME's own list (`ImmGetCandidateListW`) as one horizontal row of at most nine entries, each prefixed with the number key that picks it and the selected row in amber; the open line also carries the active input method's short tag (`CN`/`TW`/`HK`/`JP`/`KR`) at its right edge and hides it when the layout has no IME. `SayText` is parsed in the data layer and swallowed so exactly one log is drawn: rows keep the server's colour control bytes (normal text, per-player name tint from the client's `GetClientColor`, location), split the client's `(TEAM)` / `*DEAD*` / `*SPEC*` decorations into their own run, honour positional `%s`/`%sN` substitution on HLSDK-shaped servers, and escape every server string before it reaches the DOM. The server console's own `say` reaches the client as `TextMsg`/`HUD_PRINTTALK` (client byte 0, the wire CaptionMod draws into its log), so the owning chat also parses that destination through the same run pipeline with no sender tint. The log sits bottom-left in the HUD palette (translucent row plates, amber channel ink), wraps long lines inside a 190dp column, grows upward from the input row, and shows a compact window of at most six rows (capped from `hud_saytext_lines`) over a 256-message scrollback: the thin scrollbar and the mouse wheel browse older lines while the input line is open (the wheel never switches weapons then), the tail follows new lines unless the reader scrolled up, and the whole block fades out one second after `hud_saytext_time` elapses; every line is also mirrored into the console the way the native handler did, and emoji render in colour through the Segoe UI Emoji fallback face (RmlUi asks FreeType for FT_LOAD_COLOR and uploads the RGBA glyph atlas); `hud_saytext 0` disables it and `hud_saytext_block_muted 1` drops lines from voice-muted players. Chat ownership is unconditional: while `abc_ui_chat` is nonzero (the default), CaptionMod's `cap_newchat` is forced to `0` - CaptionMod loads after AbcUi and its hooks swallow `SayText`/`TextMsg`/`messagemode` after handling them, while with the switch off they decline and forward every wire - and the user's value is restored the moment `abc_ui_chat` goes back to `0`; one console note names the behaviour at the first force, and with CaptionMod absent nothing is forced. Input ownership is the input router's: the open input line holds an interactive-HUD token, and the engine is told the UI consumed the key at the CBaseUI key seam (`IVGUI2Extension_BaseUICallbacks::Key_Event`), which is the same route the native chat entry and CaptionMod's VGUI dialog use - the binding simply never runs, so WASD stops and no usercmd filtering is involved. Edges the router passes through for clean pairing (keys held when the line opened) are left to the engine. The token is released on Esc, focus loss, the pause menu or a map change.
- Module UI resources resolve only inside package ZIPs. Native game content (sprites, models and server files) still uses the engine `IFileSystem`, including mounted downloads/addon content.
- Menu: main menu, settings (general/input/mouse/video/audio/voice/captions/multiplayer/advanced/menu/renderer), server browser, create game, player list, vote creation (ban/kick/kill/map with the server's map list), connect/loading/error pages, console, direct-connect/password dialogs, quit confirmation, and the nested in-game command menu; the loading page stays above the page shells and the console while a load is in progress, while the connection dialogs and the quit confirmation remain above it; a server `changelevel` also raises the loading page (map-load title/status, no connect address) instead of dropping to the no-map main menu and its backdrop movie. Text input goes through the IME-aware input bridge. The pause menu's **Vote Menu** row and the original `votemenu` command both open the RmlUi page, replacing the legacy GameUI vote dialog. The original `+commandmenu` VGUI window is replaced by a translucent, left-anchored RmlUi menu that leaves the game view visible. The original `inventory` / `+inventory` / `-inventory` viewport window is replaced by an RmlUi inventory window: the chained commands swallow the native `CInventoryPanel`, and the page projects the server's `InvAdd`/`InvRemove` item stream with the native captions and fallbacks (`You haven't got any inventory`, `No item selected`, `Nameless item`, `No item description`). The dialog is a centred panel over a dimmed scrim (clicking outside also closes it): the item list with its count on the left, a detail card on the right showing the item name, description and the server's can-use/can-drop flags as tags, and the action row at the bottom. A row click selects, Up/Down walk the list, Enter activates, Delete drops, and the three action buttons send the native `activateitem "#<id>"` / `dropitem "#<id>"` / `dropitem "*"` verbs; any action, Esc, the close button, the scrim, `-inventory` or the bound key closes the window. Actions are read-through: a row disappears only when the server's `InvRemove` arrives, so a refused action never rewrites the list optimistically. The console wraps output to the current window width, supports mouse selection with Ctrl+C copy or a right-click Copy menu (no selection copies the visible output), and the console hotkey returns straight to gameplay from the in-game pause page (the no-map main menu stays open). Tab completes the editor against the client's cvar/command catalog: a unique match completes in place, several matches extend to their common prefix and open a candidate list that further Tab presses cycle (Enter runs the highlighted candidate, Esc closes only that list); Up/Down keep walking the submission history.
- Menu backdrop: `abc_ui_menu_video` picks the looping movie behind the no-map menu (blurred by `abc_ui_menu_backdrop_blur` gaussian rounds, 0..4, default 2; 0 shows the unblurred capture); in a level the pause menu blurs the live frame instead.
- Voice mute: the player list's Mute button, the scoreboard's right-click **Mute Voice** entry and the per-row muted badge share one persisted mute list at `<mod>/abc/abcui/voice_ban.dt` (JSON `{"bans": ["<steamid64>"...]}`, atomic writes; the legacy files are never read). The list is mirrored to the server as a 32-slot `vban` mask with `VModEnable`, re-synced on the server's ReqState, on every toggle and once per second, so state survives restarts and map changes. Bots, the local player and SteamID-less rows are not mutable; every surface reads the state back from the service, so a refused toggle never shows success.
- Render backends (issue #24): the `abc_ui_render_backend` cvar selects the drawing backend (`renderer`, the default, wraps Renderer.dll's IMetaRenderer; the value applies only after a full game restart - `_restart` is not enough). Backends are loose DLLs in `<mod>/abc/abcui/render_backend/`; `abc_ui_render_backend_list` prints the installed backends and their capabilities. An unknown value is refused and rolled back to the active backend, and a missing/unusable selected backend stops the game with a `SysError` diagnosis instead of falling back. Capability gaps degrade per feature (a backend without the world pass disables the radar world render, without the blur pass the menu backdrop blur, without offscreen targets the model preview).
- Font atlas rebuilds synchronize the Renderer texture-binding cache before
  returning to the VGUI2 upload channel. Reused texture IDs must not skip
  the upload binding and leave text as solid black rectangles (issue #23);
  no font-family, font-size or layout workaround is required.
  The shared surface channel ends active UI 2D state before uploads or
  deletion, including module textures and animated media, not just fonts.
- Model preview (`<model>`): rendered through the engine's own `V_RenderView` (the StandaloneView route, in-house - no Renderer `IEntityViewRenderer` involved), captured into a backend offscreen target. The route needs engine/client gamedata symbols (`V_RenderView`, `VGui_ViewportPaintBackground`, `cls.state`, `cls.signon`, `cl_worldmodel`, `r_worldentity`, `CL_FxBlend`, `currententity`, `r_blend`, plus the Sven client's portal manager pair); on an unknown build the symbols do not resolve and the preview quietly degrades to its disabled branch instead of failing startup. Preview lighting uses a draw-scoped override at the engine's `StudioSetupLighting` API, and auto-framing reads the engine's `scr_fov_value` because the engine owns the pass projection.
  The main menu card is a three-entity chain: an action-group entity
  (`abc_ui_menu_action_model`, default `display_male`, resolved to
  `abc/abcui/models/menu_action/<name>.mdl`; a `.mdl` path is used verbatim)
  owns sequence/frame/controllers and is never drawn, while the displayed
  player model (`src`, from the player's `model` or
  `abc_ui_menu_model_path`) and the weapon model
  (`abc_ui_menu_weapon_model`, default `models/p_m16.mdl`, empty = no weapon;
  only a complete game-relative `.mdl` directory path is accepted, verbatim)
  are private
  `MOVETYPE_FOLLOW` entities. Immediately before **each** follower draw, the
  preview evaluates the action model through `StudioDrawModel(0)` (bones
  only, no mesh or events), then draws the follower with `STUDIO_RENDER`.
  Studio bone merging consumes that saved pose; it does not resolve private
  entities through `aiment`, and no fake engine entity ids are allocated.
  Matching bone names share the action pose; model-specific extra bones retain
  the displayed model's own animation, so compatible skeletons are required.
  `abc_ui_menu_model_sequence` picks the group's sequence (the bundled
  models have pistol, rifle, heavy and grenade idles at indices 0..3;
  `display_female` additionally has `idle1`, `idle2` and `idle3` at 4..6).
  A missing action model falls back to the player's own animation; a missing
  weapon is omitted with a console warning instead of drawing an ERROR model.
  A separate `R_StudioLoadExternalFile ... studio_bone` warning comes from
  Renderer external model settings referencing a bone absent from that model;
  it is not the preview's missing-weapon-path diagnosis.
  Auto-framing first evaluates the Studio pose without meshes/events or bbox
  culling, then reads the selected bodygroups' mesh vertices through the
  engine's bone matrices. The player and **bone-merged** held weapon are fitted
  together; the invisible action group, unmerged weapon bounds and loose
  sequence boxes never enlarge the final frame. The completed pose feeds the
  next frame; a model/pose/placement change discards the old sample. Before
  the first pose, or if the bone API/data is unavailable, only the displayed
  player's bounding box bootstraps the camera.
  The perspective fit preserves `camera-angles`, centres the actual projected
  silhouette and moves the camera as close as possible while fitting both
  viewport axes and respecting the near plane. This avoids padding introduced
  by a world-space bounding box, especially around a diagonal held weapon.
  The main-menu preview uses a wide stage (44% screen width, 424dp high,
  starting at 20dp on the height-scaled 480dp layout), with its feet above the
  footer and a tight 1.02 framing margin so the character nearly fills the
  viewport. It uses a camera elevated for a 15-degree downward view, and
  turns the character only 15 degrees horizontally. The settings preview's
  layout and viewing angles are unchanged.
  The friends board remains above the transparent preview stage.
  The main-menu key light is directly in front of the character (the
  world-space light-ray direction is `-0.965926 0.258819 0`, opposite its
  -15-degree yaw), with the existing warm tint and ambient fill. Each
  `<model>` now passes its `ambient`, `shade`, `light-color` and normalized
  `light-direction` attributes to Studio lighting; `lighting="false"` selects
  unshaded white illumination. The override is scoped to the individual
  preview draw, shared by its player and weapon, and never changes world
  lighting. If the lighting hook cannot be installed, engine lighting remains
  in use and the console reports the degradation.
  Portal hooks re-arm on each HUD
  cycle, while the engine-owned Studio interface slot
  survives ordinary map changes and is cleared only for a new client DLL. A
  delayed Studio API retries next frame instead of permanently marking the
  model missing; unresolved view symbols are named in the console diagnosis.
  Before the first map loads, the Renderer backend supplies the missing
  camera/scene/light buffers with neutral preview constants and restores the
  previous bindings after capture. It captures the completed scene before
  composition onto the opaque final buffer, preserving transparent model
  backgrounds, and normalizes that capture to straight alpha so the ordinary
  2D alpha blend composites it. Sven's first-person local-player routing is
  bypassed only during the preview pass; the normal view and camera settings
  are unchanged.
- Retained cvars: `hud_danger_health`, `hud_danger_armor`, `hud_deathnotice(+_time/_count)`, `hud_sideinfo`, `abc_ui_chat` (0 = off, any other value = on, forcing CaptionMod's `cap_newchat` off while on); in-game diagnostics: `abc_ui_debugger` (RmlUi debugger) and `abc_ui_probe` (font/panel/element state dump).
- Steam server lists (Internet/LAN/favorites/history/spectator/friends),
  server pings and player details bind directly to the game's loaded
  `steam_api.dll` through `ISteamMatchmakingServers`; favorites/history use
  `ISteamMatchmaking` as their authoritative store. Identity, real names,
  friends and avatars use that same direct runtime access path. There is no
  `serverbrowser.dll` dependency, `SteamAPIBridge.dll` probe or alternate
  Steam provider, and no `steam_api.lib` import dependency. The engine owns
  Steam initialization, shutdown and callback pumping; unavailable Steam
  interfaces disable their features without loading another DLL.
- Server-browser tabs query the selected Steam list. The action-bar and
  right-click **Add/Remove Favorite** actions update the selected server's
  Steam favorite record without connecting; **Connect** is a separate action.
  Browser models and intents remain wired across HUD/context recreation.
- Fonts are loaded from the system font directory through `Rml::LoadFontFace` (Verdana/Tahoma, Microsoft YaHei for CJK, Consolas, and Segoe UI Emoji/Symbol as the colour-emoji and symbol fallbacks, in that order so colour glyphs win over the monochrome symbols).

## Runtime requirements

`plugins.lst` order (AbcUi must load after Renderer while the default `renderer` backend is selected; the backend performs the fatal Renderer check at startup):

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
| `thirdparty/Renderer` | https://github.com/MetaHookSv/Renderer | `7225f589548fcc664c4d0e540eb91ccff22c3650` (public interfaces and `gl_common.h` sync) |
| `thirdparty/RmlUi` | https://github.com/mikke89/RmlUi | tag `6.3` (`ba95ffe8bfb6370efb2cdcca927eaad4710c5413`) |
| `thirdparty/freetype` | https://github.com/freetype/freetype | tag `VER-2-13-3` (`42608f77f20749dd6ddc9e0536788eaad70ea4b5`) |
| `thirdparty/FreeImage` | https://github.com/hzqst/FreeImage_clone | `bf9dedd558d9891bc12d37feb3021fac857df4cc` |
| `thirdparty/VGUI2Extension` | https://github.com/MetaHookSv/VGUI2Extension | `dbcf03c649c395f9820f328f1060959c927295c9` (headers only) |
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
