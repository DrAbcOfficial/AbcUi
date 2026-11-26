# AGENTS.md

Guidance for AI/automation collaborators working in this repository. Read `README.md` first; it is the only document distributed with the release.

## Hard rules

- The language is C++ 20 (see Build). Write to that standard; never add a
  pre-C++20 fallback or lower it.
- Prefer the standard library. When C++ 20 already provides a facility (the
  containers, `<algorithm>`, `<ranges>`, `<filesystem>`, `<format>`, the
  string views), use it instead of writing an equivalent by hand.
- No C-style code outside the inter-module C ABI, here the module ABI tables
  under `sdk/abcui/` with `AbcUiModule_Main`, the `AbcCore_CreateInterface`
  table the framework consumes, and the engine, client and MetaHook
  interfaces they bind to. Everywhere else the code is idiomatic C++ 20:
  named casts and never `(T)x`, standard containers and algorithms and never
  raw buffers or `memcpy` loops in their place.
- A translation with arguments is a `{0}` / `{1}` / `{2}` template, filled
  only through `abc::Format` (`abc/Format.h`, header-only; modules get the
  include path from `abcui_add_module` and never link AbcCore). Call it as
  `abc::Format(template, argument0, argument1, ...)`: arguments in index
  order, every `{N}` takes argument N, `{{` writes a literal `{`. An
  argument is inserted verbatim and never rescanned, so a server-derived
  name needs no marker escaping; a malformed or out-of-range marker stays
  visible verbatim. The template is table data: never a printf format,
  never a `std::format` pattern, never filled by a hand-written
  `find`/`replace` or `moduleui::replaceAll`. In `lang/*.txt` a template
  carries `{N}` markers only, numbered from 0 without gaps and the same in
  every language; `%s`, `%d`, `%s1`, `%%NAME%%` and `{name}` are not
  translation placeholders (the `{SCORE}` of the sidetext RML is a document
  token). Good: table `Unknown command: {0}`, code
  `abc::Format(translate("Console_UnknownCommand"), verb)`. Bad: table
  `Unknown command: %s` or `Press [%%USE_BINDKEY%%] pick up
  %%PICKUP_ITEM%%`, or code `snprintf(buffer, size, text.c_str(),
  verb.c_str())`. The `abc_ui_format_templates` test rejects the bad forms,
  per-language drift and a filled template that does not use its call
  site's arguments.
- Nothing committed (code, comments, docs, commit messages, tags) may contain local machine paths, personal directories, session logs or any credentials; the deployment destination is provided through the `ABC_GAME_DIR` environment variable (optional; unset means no deploy).
- The committed tests are the hermetic offline suite under `tests/` (see Build). Do not commit ad-hoc acceptance scripts, evidence screenshots, plans or progress logs; those are session artifacts that do not belong in the release.
- The README is the single public document; UI, config-format or cvar behaviour changes must update it in the same commit.
- AbcUi user preferences belong in `abc/abcui/config.json` through
  `services::userconfig`. Section updates preserve other settings and unknown
  fields; do not restore independent preference-file writers or the render
  backend selection cvar. Renderer availability falls back to `gl` without
  rewriting the configured preference.
- AbcCore is managed by the aggregate `ABCEnchance` repository and consumed
  through `ABC_CORE_DIR`; do not add a `thirdparty/AbcCore` submodule back. The
  plugin-facing contract is the versioned C ABI `AbcCore_CreateInterface`
  table, not exported C++ symbols.
- Submodule pins may only point at commits that are already public upstream; confirm the commit is pushed before bumping a pin.
- Rendering draws go exclusively through `abc::render::IRenderBackend`
  (issue #24): the framework spine (`src/`, `AbcUi.dll`) has zero Renderer
  headers and zero direct OpenGL calls. Renderer headers live only inside
  `backends/renderer/` (the `renderer` backend DLL behind
  `sdk/abcui/render_backend.h`), which also owns the two fixed-pipeline GL
  capture niches with no IMetaRenderer equivalent (scissor viewport clear +
  copy-out). The independent `backends/gl/` backend uses OpenGL without
  Renderer headers, include directories or interfaces. All GL stays inside
  backend DLLs. VGUI2 `ISurface2` stays confined to the texture upload channel
  (`DrawSetTextureRGBA`), cursor handling and engine UI seams, never to draw a
  panel. Model previews use the independent `StandaloneView.dll` plugin through
  API_003. `thirdparty/StandaloneView` supplies public headers only; do not
  compile its manager or engine hooks into AbcUi or duplicate its studio,
  lighting or empty-world logic. A missing plugin or API_003 disables previews,
  never the rest of the UI. List StandaloneView first so its callbacks are
  outermost and its manager outlives the consumers.
- AbcCore must stay renderer-free. AbcUi owns `thirdparty/Renderer` and the renderer-facing bridge in `src/render/`; do not move either back into AbcCore.
- The `<model>` camera is explicit: `camera-origin` / `camera-angles` are
  used as written and nothing fits the view to the model. Do not bring back
  an auto-framing path, a fit-on-invalid-config fallback or pose-vertex
  sampling for framing (issue #36) - the defaults are the only fallback. The
  engine keeps its own projection fov, so the `fov` attribute does not move
  the framing; a camera is placed by its origin and angles.
- The main menu character card's camera, model placement and light are the
  `abcui.gameui` module's `menu_preview` config section
  (`module.abcui.gameui.menu_preview`, reached only through the `config`
  section). Its pure model - defaults, control ranges, the strict read gate,
  the serialization and the light angle <-> direction conversion - is
  `modules/gameui/src/settings/MenuPreviewSettings.*`, linked alone by
  `tests/MenuPreviewSettings.cpp`. Every control range stays inside its
  `<model>` attribute domain, so each value projects onto `#player-model`
  unchanged. A malformed section or an unreadable file makes a read-only
  session: the defaults are shown, nothing is written and Apply reports the
  failure; an out-of-range value is clamped. Edits are drafts of the settings
  transaction - a slider or picker change never writes the config, Apply
  does, `isDirty()` counts the draft and Reset stages the defaults.

## README

- The README is the single public document and is written for end users.
- Keep it short: features, installation and user-facing commands or settings
  only. Do not document architecture, module layout, ABI or interface
  contracts, design conventions or internal implementation, and never add
  parenthesised technical remarks.
- Do not describe changelogs, TODOs or FIXMEs.
- The only technical content is how to build the dist package, including its
  prerequisites.

## Known structural exceptions

These are deliberate, audited exceptions rather than unfinished cleanups:

- The connection/loading state machine (attempt kind, password prompts, error
  and direct-connect overlays) stays in `GameUiModule`. Its page, controller
  and loading-feed ordering is one interleaved state machine; each attempted
  split needed more host callbacks than the extracted flow removed, so the
  remaining boundary is documented instead of duplicated.
- Parse-boundary exceptions: `ValidationError` is private to the JSON
  validators (`CommandMenuModel`, `UiDefinitions`) and is caught by the same
  translation unit's public parse API. rapidjson parse *errors* are checked via
  `HasParseError`; only `ValidationError` is caught, so allocation failures are
  not claimed as handled. New code must use `std::optional`/error
  out-parameters, not add throw sites.
- RTTI is limited to RmlUi widget downcasts (`ConsolePage`, `TextInput`), the
  Debug probe/video test tools, and RmlUi's own `rmlui_dynamic_cast` for
  types implementing its RTTI macros. New production code should prefer
  `rmlui_dynamic_cast` where the RmlUi RTTI macros exist.
- `src/render/RenderCommon.h` is vendor-synced and excluded from the
  clang-format check; `src/rmlui/TextInput.h` deliberately `#undef Assert`
  after `windows.h` to keep RmlUi's `Rml::Assert` declaration visible.
- `tests/` is the committed offline CTest suite: production translation
  units linked against recording seams, no game installation, network, local
  path or environment variable. Keep every test on the shared
  `tests/framework/` harness and the `abcui_add_test` CMake helper; the suite
  is opt-in (`ABCUI_BUILD_TESTS=ON`) and never part of the dist package.
- Process-lifetime services (`consoleBuffer`, `menuBackend`, `steamFavorites`,
  `friendsModel`, `nativeUiBridge`, `loadingFeed`) are function-local statics:
  the engine calls back through bare C function pointers and the DLL lifetime
  is the service lifetime. Consume them through their named accessor; do not
  add new ad-hoc file-level statics. Module and panel lookup goes through
  `modules::registry()` / `PanelManager::find`, not reverse pointers.
- HUD_Init/HUD_Shutdown repeat within one engine process (the client re-runs
  its HUD cycle on map init and the teardown path runs on the way out).
  Everything HUD_Shutdown tears down must be re-armed by H_HUD_Init: the
  panel list (the panel install when `PanelManager` is empty) and module
  documents (`onContextReady`). Cvars, commands and module registration stay
  process-scoped and are guarded by a one-shot latch; do not move them into
  the per-cycle path. Usermsg hooks are installed once and re-chained only by
  `netmsg::refreshAfterClientLoad()` after a client DLL reload (detected via a
  Ui-exclusive message); re-hooking every cycle would make AbcEssential's and
  this plugin's hooks point at each other and recurse forever.
- The player hull traces run only while `hasLevel() && !levelLoading()`. A
  listen-server changelevel keeps the session connected, while
  `Host_ClearMemory` in `SV_SpawnServer` already freed the world before any
  client disconnect/reconnect reaches `DisconnectFromServer`; `hasLevel()`
  alone therefore stays true over a replaced world. `levelLoading()` is the
  engine loading-plaque fact driven by the GameUI `LoadingStarted/Finished`
  "level" and "transition" resources (`services/loading/LevelLoadGate.h`)
  and brackets that window (issue #139). Never gate world reads on
  `hasLevel()` alone.
- GameUiModule's data subscriptions (InvAdd/InvRemove, MapList) are installed
  once in `onInit` and guarded by `*Bound` flags; the votemenu/command-menu/
  inventory commands are process-lifetime arbitration claims owned by the
  module. The module object and the buses are process-lifetime, so they are
  deliberately not unsubscribed in `onShutdown`; never re-append an unguarded
  `[this]` subscription from a per-cycle callback.
- Server weapon names are untrusted. They may enter the weapon registry only
  as `[A-Za-z0-9_]` identifiers, and only a `weapon_*` token may be sent to
  `pfnServerCmd` (`ResourceLimits::weaponCommandName`). A bare identifier is
  not enough: `kill` is a legal identifier and a real console command.
- The weapon registry keeps the raw server placement
  (`Weapon.serverSlot`/`serverSlotPos`) apart from the effective selector
  placement (`slot`/`slotPos`); every snapshot and the selection walk read
  the effective pair. `weaponres::rebuildSlots` applies the client rules
  through the pure `src/data/WeaponSlotLayout.*` unit and publishes
  `hud.weaponLayout`. The `CustWeapon` sprite directory (`Weapon.spriteDir`,
  svencoop_sdk `WEAPON.szCustomDirectory`) is registry state next to the
  sprite name and must survive every reload: `WeaponSpr` (the script
  `LoadSprites` call custom weapons make when they change state) carries only
  the new name, and dropping the directory resolves the list outside it and
  falls back to the error art for the whole weapon (issue #138). A reload
  publishes `hud.weaponSprites`. The server sprite paths (`WeaponSpr` name,
  `CustWeapon` directory, the atlas names a list names) may carry `..`
  segments - the stock `REPLACE_WEAPON_SPRITES` map script sends
  `../<dir>/<name>` to leave the custom directory (issue #141) - and are
  refused only when `std::filesystem::path::lexically_normal` still shows a
  root or a `..` (`WeaponSpritePath.h`); a refused path is reported once per
  reason and never reaches `SPR_GetList`. `WeaponSpr`/`CustWeapon` for an id
  without a `WeaponList` yet are staged and applied when it registers (the
  shipped client forgets them there). `HUD_VidInit` on the same level and
  `HUD_Reset` only re-resolve the sprites (`weaponres::reloadSprites`, the
  shipped client's `LoadAllWeaponSprites`); the registry starts from scratch
  on another level (`weaponres::reset(levelName)`) and on disconnect
  (`weaponres::clear`). The rule map is process-lifetime (a map
  transition clears the registry, not the rules), capped at 256 rules, keyed
  by `[A-Za-z0-9_]{1,64}` names and never sent to the server. Collisions
  place overrides first, then server entries, then by name, and shift the
  later weapon to the next free position inside the same slot; the filter's
  enabled state only selects which placement is applied. Server weapon ids
  span the engine's full id domain (Sven Co-op 5.x: `ItemInfoArray` and the
  `weapon_data[]` prediction array hold 256 entries; the shipped client
  indexes `weapon_data` by weapon id unbounded), so prediction sync must
  mirror that - never add a client-side capacity gate on the weapon id, and
  never pin shifted positions at a ceiling.
- Custom Rml elements that are downcast after lookup declare
  `RMLUI_RTTI_DeclareWithParent`/`RMLUI_RTTI_Define` and are reached through
  `rmlui_dynamic_cast`; raw `static_cast` between element types is not
  allowed in production or debug tools.
- `<colorpicker>` queues `change` events and dispatches them from
  `ElementColorPicker::pumpChanges()` in the frame pump, never from the
  drag/render call stack (a page handler may destroy the picker).

## UI module architecture

The plugin is a UI-free framework plus loadable UI modules (zip packages). The
implemented rules the transition must keep:

- AbcUi.dll is UI-free: RmlUi runtime, render bridge, input bridge, IME,
  theme/localization, texture services (sprites/avatars/custom elements), the
  data layer (NetworkMessages / WeaponResource / HudState / PlayerResource /
  PlayerTrace / TextFeed) and the module machinery. No HUD panel, menu page,
  stub page, quit overlay, panel catalog or page stylesheet ships inside the
  framework.
- UI features live in modules under `modules/<id>/`: `manifest.json` + `src/`
  + `assets/` + a CMake `add_library(... MODULE)` target. Migrated so far:
  flashlight, textmenu, vote, motd, weaponchoose, killfeed, textfeed, sidetext,
  health, ammo, hudeffects, crosshair, damage, itemhighlight, playerboard,
  radar, scoreboard, spectator and chat; `abcui.gameui` has every replacement menu
  page (the shell, the console, the server browser, the connection
  surfaces, the vote creation, player-list, command-menu, inventory,
  create-game and settings pages, plus the display-mode confirmation and the
  quit confirmation); `demo` is the minimal example. modeltest/videotest stay
  development-only.
- The module ABI is pure C, versioned struct tables under `sdk/abcui/`
  (same pattern as `AbcCore_CreateInterface`). A module DLL exports exactly
  one entry, `AbcUiModule_Main(abiVersion, api, outDescriptor)`; everything
  else crosses the boundary as C function pointers and POD payloads. Modules
  must not include RmlUi headers or link the framework's static libraries.
  Sections: core, console, events, vfs, panels, documents, data, ui, weapons,
  players, radar, shell, voice, score, input, menu, votemenu, playerlist,
  commandmenu, menuconfig, social, inventory, creategame, consolepage,
  serverbrowser, connection, settings, textures, audio, modules, claims,
  facts, config. Adding a function means appending it to the section end (and
  its initializer in `ApiTable.cpp` / `ApiSections.cpp` / `ApiWeapons.cpp` /
  `ApiPlayers.cpp` / `ApiDocuments.cpp` / `ApiMenu.cpp` / `ApiVoteMenu.cpp` /
  `ApiPlayerList.cpp` / `ApiCommandMenu.cpp` / `ApiMenuConfig.cpp` /
  `ApiSocial.cpp` / `ApiInventory.cpp` / `ApiCreateGame.cpp` /
  `ApiConsolePage.cpp` / `ApiServerBrowser.cpp` / `ApiConnection.cpp` /
  `ApiSettings.cpp` / `ApiModules.cpp` / `ApiClaims.cpp` / `ApiConfig.cpp` -
  a missing initializer is silently a null pointer; the table audit in
  `ApiTable::get` catches it at boot), and appending to snapshot structs means
  the writer must honor the caller's `structSize`.
- Module config lives in `abc/abcui/config.json` under
  `module.<manifest-id>.<local-name>` and is reached only through the
  `config` section: local names are `[a-z0-9_]{1,32}` and the host composes
  the key, so a module can never read or write another module's section or a
  top-level field. Sections are JSON objects of at most 64 KiB; `userconfig`
  preserves other sections and unknown top-level fields and never overwrites a
  damaged file. The pure key helpers live in
  `src/services/userconfig/ModuleConfig.*`.
- Module packages are zip files at `svencoop/abc/abcui/modules/` (real files,
  never inside a VPK): `manifest.json`, the entry DLL and the module's
  rml / rcss / img / lang trees. Manifest fields: `id`, `name`, `description`,
  `author`, `version`, `abi`, `minAbcUi`, `entry`, `dependencies`,
  `conflicts`. Packaging stores every entry uncompressed (STORED): the loader
  maps the zip with `CreateFileMapping` and serves STORED entries as zero-copy
  views; DEFLATED entries are inflated once through miniz for third-party
  compatibility. `tools/pack.py` (the `pack_modules` target) validates the
  manifests and writes `<id>-<version>.zip`; `dist` includes the archives.
- Loading happens once per process in the HUD_Init one-shot block: scan,
  validate (abi / minAbcUi / dependencies / conflicts), topologically sort
  (ties broken by id), map each package, load the entry DLL from memory
  (manual PE map; imports resolve through `GetModuleHandle` /
  `GetProcAddress`), call the entry, then drive
  `onLoad / onInit / onContextReady / onFrame / onVidInit / onShutdown /
  onUnload` in load order (reverse order for shutdown). A failed module is
  skipped together with its dependents; the process keeps running. Modules
  must avoid static TLS (`__declspec(thread)`) and delayimp.
- A package is disabled by renaming its `.zip` to `.disable`; the loader scans
  only `.zip`. `abcui.gameui`'s Modules settings page lists both forms and
  `ModuleManager::unloadAll` applies the process-local pending renames after
  `ModuleVfs::unmountAll`; never rename while a package is mounted. The cache
  is intentionally not persisted (an abnormal exit loses it) and a full process
  restart is required. Do not add `FILE_SHARE_DELETE` or live module reloading.
- Each mounted module layers `abc/abcui/custom/<module-id>/<package path>`
  over its package: a file there replaces the package entry of the same
  `mod://<module-id>/<path>` and any other is a new entry, for every reader
  alike (`ModuleVfs::open`/`openPath`/`readText`/`list`, and through them
  documents, textures, media, localization and the module VFS section). Code
  is never overlaid: `manifest.json` and the manifest `entry` are
  `ModuleVfs::mount`'s `codePaths` and always come from the zip.
  `ModuleManager::boot` names the root once (`ModuleVfs::setCustomRoot` with
  `abc::ModPath`); framework code never scans the directory itself - read
  through the VFS, or take a file's game-relative spelling from
  `ModuleVfs::customPath`.
- The overlay is `src/modsys/ModuleCustom.*`: one immutable index per mount,
  built when the module mounts. No file watching and no hot reload - a file
  added or removed later counts after a restart - while the bytes of a listed
  file are read at open into a `PackageEntry` the caller owns, so decoder
  threads share no state. Only regular files reached without a reparse point
  are indexed, every open re-checks through `GetFinalPathNameByHandleW` that
  the file still lies inside the module's custom directory, and a directory
  name must be the module id exactly. An unreadable custom file is reported
  once and the package entry is served instead. Reports are queued
  (`ModuleCustom::report`) and printed by the main thread in
  `modmanager::boot` and `frameAll`, never from a decoder thread.
- The packaged menu action models are materialized into the menu module's
  custom directory because that is the one place the user and the extraction
  share. `modelpreview::actionModelDir()` is their one path source:
  `apiSetResourceRoot` derives it from the resource root's module id through
  `ModuleVfs::customPath`, and the extraction target (`ActionModels.cpp`) and
  the settings scan (`SettingsResources.cpp`) both read it - nowhere else does
  the framework spell the module id for it. `prepareActionModels` writes a
  model only when its file is missing and renames without replacement: a file
  already there, the user's or one an earlier run wrote, is never rewritten,
  and through the overlay it shadows the packaged model, so a packaged model
  changed by a module upgrade stays shadowed until that file is deleted. The
  old `abc/abcui/models/menu_action/` is neither read nor written; its files
  are left alone.
- The engine keys its model cache by the full game-relative name and caps it
  at `MAX_MODEL_NAME` (64 bytes). A model under the action model directory is
  already 65+ bytes (`abc/abcui/custom/abcui.gameui/models/menu_action/` alone
  is 49), so the engine never loads from it: `modelpreview::mirrorActionModels`
  keeps `abc/abcui/cache/menu_action/` - short enough for a 31-character stem -
  in step with the directory on every start (copy when missing or different,
  remove a copy whose model is gone, report a stem that still does not fit).
  A bare spelling resolves into the mirror (`Options.cpp`,
  `actionModelLoadPath`), a resolved path longer than `kActionModelNameMax` is
  not presented, and the settings scan lists only stems the engine can name.
  Do not point the engine at the custom directory directly again; do not add
  a second mirror or a second loader.
- The settings page's player model preview keeps an explicit camera (issue 36
  forbids fitting it to the model). Its distance is the page's own value and
  follows the card's shape (`modules/gameui/src/settings/ModelPreviewCamera.h`):
  a fixed number of visible units from the card's top to its bottom keeps the
  default player model at the same share of the card whatever its aspect
  ratio. The document's `camera-origin` is only the value before the first
  layout.
- Module panel descriptors are kept for the whole process and every
  HUD_Init cycle builds fresh wrappers: `PanelManager::shutdown` destroys the
  panel list on every HUD_Shutdown, so taking the wrappers once would lose
  every module panel after the first map change.
- Interactive pages are module-owned documents (`documents->create`), never
  framework pages: the framework loads them on every context cycle, forwards
  ready/show/hide/close, and keeps the menu layer's focus guard and hide-all
  rule for descriptors carrying `ABCUI_DOC_FLAG_MENU`. Page modules present
  themselves from `menu.state` (transitions) and `menu.paint` (the per-frame
  presentation beat), and navigate through the `menu` section
  (`openPage`/`back`/`ensureUp`/`resume`) - never through a native GameUI
  window. The page stack, phase machine and takeover stay framework-side;
  every page is a module document.
- Page show/hide run a framework-owned opacity dissolve (InteractiveDocument:
  the ramp is inherited by the page's elements, pumped once per frame by
  `module_documents::pump` before the context update). `hide()` flips
  `visible()` immediately and only then ramps out, so the menu layer's
  hide-all rule, the focus guard and every page flow keep their synchronous
  truth while the page is still on screen for ~0.1 s. An element that sets its
  own `opacity` does not inherit the ramp (the backdrop's dimmer therefore
  rides its background alpha, not its opacity).
- Engine entry points are owned through `framework::arbitration`
  (`src/modsys/Arbitration.h`): one domain per command / user message / key
  route / native-draw query, and claims with Owner, Fallback, Observer and
  Gate roles. Predicates are pure and evaluated on a frozen pass before the
  single winning handler runs, so no claim can observe another claim's
  mid-dispatch state. Ties are resolved by explicit priority and the stable
  owner id, never by registration order or module load order; equal-priority
  Owner overlap is reported as a conflict. The command bridge
  (`src/framework/Commands.cpp`) creates a bridge on the first claim and calls
  the displaced predecessor only when nothing claimed the occurrence.
- Observation is event-bus based (`src/modsys/EventBus.h`): data-only string
  channels backed by priority buckets of `abc::CallbackList` (eventpp reused
  from AbcCore). Priority only orders observers; it never decides ownership.
  Channels and payloads are documented in `sdk/abcui/events.h`.
- Framework facts (`src/modsys/Facts.h`) are read-only for modules: the
  framework derives them from the wire (the text-menu fact from ShowMenu), so
  a missing or failed renderer never changes input ownership. Modules never
  write a framework fact.
- Hook contention is observable: `src/modsys/HookRegistry.h` records each
  broken command/message entry's installed thunk, predecessor, generation and
  dispatch counters; `abc_ui_hooks` prints the table and the claim list.
  A foreign thunk above ours is reported, never displaced.
- Transition rules: each migrated feature moves its sources, assets and cvars
  into `modules/<id>/` in one commit and registers through the new ABI, and
  the built-in copy is deleted in that same commit (the framework must not
  keep a duplicate implementation). Existing lifecycle red lines (one-shot
  latches, usermsg install-once + re-chain, export chaining) keep applying to
  the framework spine unchanged.

## Build

Win32 x86 only / C++20 / dynamic CRT (/MD). AbcCore is consumed as
`AbcCore.dll` from `ABC_CORE_DIR` (sibling `../AbcCore` by default); the plugin
links the import library and the DLL must be deployed under
`metahook/dlls/AbcCore/`. `ABC_GAME_DIR` is optional: unset means the plugin
and assets are built but not deployed.

```bash
cmake --preset win32-debug
cmake --build --preset win32-debug    # Release: win32-release

# Offline tests (Debug): configure, build and run the CTest suite.
cmake --preset win32-debug-tests
cmake --build --preset win32-debug-tests
ctest --preset win32-debug-tests
```

## Conventions

- Public headers may expose their contract types (AIGCJson for `Config`,
  eventpp for `Events`, SteamSDK for `SteamId`, HLSDK for `WeaponResource`,
  IMetaRenderer for `Render`, RmlUi for `AbcRenderInterface` and Rml-facing
  headers). New headers forward-declare or PIMPL anything outside the contract
  and include the standard headers they use directly.
- Formatting follows LLVM (2 spaces, 80 columns, attached braces). Identifier
  casing is per-subsystem and sometimes per-header: types use UpperCamelCase
  and constants use the `k` prefix; AbcCore's public API is UpperCamelCase,
  while AbcUi implementation and framework helpers are mixed (for example
  `PanelManager::installPanels` versus `Localization::load`). Follow the
  surrounding declarations. The legacy UpperCamelCase framework namespaces
  (`Assets`, `ColorUtil`, `ImageLoader`, `ResourceFile`, `TextUtil`, `Theme`)
  and ABCEnchance-derived identifiers are kept; do not mass-rename.
- C-style casts in the ABCEnchance-derived code are legacy, not a precedent:
  replace them with `static_cast` / `const_cast` / `reinterpret_cast` when
  touching that code, and never add new ones.
- Interface structure is orchestrated through the GameUI composition root.
  Page documents and their stylesheets live in the module packages; the
  configuration JSON and the shared theme stay under `assets/abc/abcui/`:
  `gameui/{rcss,config,img,scence}`, `hud/{rml,rcss,img,spr}`, plus the
  shared `lang`, `rcss` and `img`.
- RmlUi 6.3 RCSS constraints: `font-family` accepts a single family name (CJK
  falls back through the fallback face), and transition easing names use the
  `linear-in` family.
- Main-menu and settings structure is driven by
  `assets/abc/abcui/gameui/config/mainmenu.json` and
  `assets/abc/abcui/gameui/config/settings/*.json`; new menu/settings pages are
  added through JSON rather than hard-coded.
- `using namespace` is confined to `.cpp` implementation files: the subsystem
  directives are `engine`/`navigation`/`settings`/`detail`, plus a function-local
  `std::chrono` in `ServerQuery.cpp`. Never add one to a header or use it to
  reach an unrelated subsystem.
- CI pins the CMake download (4.4.4 + published SHA-256) and action commit
  SHAs, and uses the permission-scoped default token instead of a personal
  PAT. Keep those properties when editing the workflow.
- An empty `ABCUI_MILE_FFMPEG_ROOT` downloads the pinned, hash-verified SDK;
  accepting a local dev-tree copy requires
  `-DABCUI_ALLOW_LOCAL_MILE_FFMPEG=ON`. Do not restore the implicit local
  search.
- RML class tokens must have a rule in an RCSS file; an unstyled class is
  either given a rule or removed (ids used by C++ lookups stay).
- Commit messages use `type(scope): summary` (feat / fix / refactor / docs / build / chore) with motivation and verification in the body.
- Before handing off: no local paths or credentials; README matches actual behaviour; Debug and Release both build.
