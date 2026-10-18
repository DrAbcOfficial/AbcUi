# AGENTS.md

Guidance for AI/automation collaborators working in this repository. Read `README.md` first; it is the only document distributed with the release.

## Hard rules

- Nothing committed (code, comments, docs, commit messages, tags) may contain local machine paths, personal directories, session logs or any credentials; the game directory is always provided through the `ABC_GAME_DIR` environment variable.
- Do not commit test scaffolding, acceptance scripts, evidence screenshots, plans or progress logs; those are session artifacts that do not belong in the release.
- The README is the single public document; UI, config-format or cvar behaviour changes must update it in the same commit.
- Submodule pins may only point at commits that are already public upstream; confirm the commit is pushed before bumping a pin.
- AbcCore is managed by the aggregate `ABCEnchance` repository and consumed through `ABC_CORE_DIR`; do not add a `thirdparty/AbcCore` submodule back to AbcUi.
- Rendering goes exclusively through IMetaRenderer: no direct OpenGL calls and no VGUI2 drawing dependencies.
- AbcCore must stay renderer-free. AbcUi owns `thirdparty/Renderer` and the renderer-facing bridge in `src/render/`; do not move either back into AbcCore.

## Known structural exceptions

These are deliberate, audited exceptions rather than unfinished cleanups:

- The connection/loading state machine (attempt kind, password prompts, error
  and direct-connect overlays) stays in `GameUiModule`. Its page, controller
  and loading-feed ordering is one interleaved state machine; each attempted
  split needed more host callbacks than the extracted flow removed, so the
  remaining boundary is documented instead of duplicated.
- Parse-boundary exceptions: `ValidationError` is private to the JSON
  validators (`CommandMenuModel`, `UiDefinitions`) and is caught by the same
  translation unit's public parse API. No exception crosses a plugin or
  service seam; rapidjson's own throws stay behind the same boundary. New code
  must use `std::optional`/error out-parameters, not add throw sites.
- RTTI is confined to RmlUi's `rmlui_dynamic_cast`/widget casts and the Debug
  probe tooling; first-party production code does not add `dynamic_cast`.
- `src/render/RenderCommon.h` is vendor-synced and excluded from
  `.clang-format`; `src/rmlui/TextInput.h` deliberately `#undef Assert` after
  `windows.h` to keep RmlUi's `Rml::Assert` declaration visible.
- `tests/` is session scaffolding per the hard rule above; the release has no
  committed test target. Unit-test additions require a separate policy change.
- Process-lifetime services (`consoleBuffer`, `menuBackend`, `steamFavorites`,
  `friendsModel`, `nativeUiBridge`, `loadingFeed`) are function-local statics:
  the engine calls back through bare C function pointers and the DLL lifetime
  is the service lifetime. Consume them through their named accessor; do not
  add new ad-hoc file-level statics. Module and panel lookup goes through
  `modules::registry()` / `PanelManager::find`, not reverse pointers.

## Build

Win32 x86 only:

```bash
cmake --preset win32-debug
cmake --build --preset win32-debug    # Release: win32-release
```

## Conventions

- Naming: new code uses lowerCamel functions/variables, UpperCamel types and
  k-prefixed constants. ABCEnchance-derived identifiers in AbcCore's public
  API (UpperCamel functions, `C`-prefixed types, `m_` members) and the
  UpperCamelCase framework namespaces (`Assets`, `ColorUtil`, `ImageLoader`,
  `ResourceFile`, `TextUtil`, `Theme`) are legacy exceptions; do not mass-rename.

- Naming follows the LLVM style; interface structure is orchestrated through the GameUI composition root, and page assets live under `assets/abc/abcui/` (rml/rcss/lang/config).
- RmlUi 6.3 RCSS constraints: `font-family` accepts a single family name (CJK falls back through the fallback face), and transition easing names use the `linear-in` family.
- Main-menu and settings page structure is driven by `assets/abc/abcui/config/*.json`; new pages are added through JSON rather than hard-coded.
- Commit messages use `type(scope): summary` (feat / fix / refactor / docs / build / chore) with motivation and verification in the body.
- Before handing off: no local paths or credentials; README matches actual behaviour; Debug and Release both build.
