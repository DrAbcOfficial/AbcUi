# AbcUi

HUD and menu plugin for Sven Co-op / MetaHookSv (`AbcUi.dll`): rebuilds the entire ABCEnchance interface on RmlUi, rendering exclusively through IMetaRenderer with zero direct OpenGL. The shared [AbcCore](https://github.com/DrAbcOfficial/AbcCore) library is linked statically as a submodule; AbcUi owns the direct Renderer interface dependency and its render bridge, so AbcCore stays renderer-free.

## Build

Windows + MSVC + CMake >= 4.2, Win32 x86 only:

```bash
git clone --recursive https://github.com/DrAbcOfficial/AbcUi.git
cmake --preset win32-release
cmake --build --preset win32-release
```

If the submodules were not fetched by `--recursive`, run `git submodule update --init --recursive` once at the repository root.

The pinned Renderer commit is currently published in the `DrAbcOfficial/Renderer` fork; when `MetaHookSv/Renderer` does not yet contain it, use the URL rewrite configured in `.github/workflows/build.yml` before fetching submodules.

## Deploy

Set the `ABC_GAME_DIR` environment variable to the game root before configuring (or pass `-DABC_GAME_DIR=...`); nothing is deployed when it is unset:

```bash
cmake --build --preset win32-release --target deploy
```

- UI assets go to `<game>/svencoop/abc/abcui/`, split by surface: GameUI documents, page styles, config and menu media live under `gameui/`; HUD documents, sprites and page art live under `hud/`; the cross-surface stylesheets (`rcss/base.rcss`, `rcss/theme.rcss`), language tables and shared art stay at the `abcui/` root.
- `AbcUi.dll` goes to `<game>/svencoop/metahook/plugins/`; `FreeImage.dll` goes to `<game>/svencoop/metahook/dlls/FreeImage/`.

## Packaging

```bash
cmake --build --preset win32-release --target dist
```

Produces `AbcUi-<version>.zip` under `build/<preset>/dist/`, laid out to unpack into the Sven Co-op root (plugin DLL, FreeImage / Mile.FFmpeg runtimes, gamedata, `abc/abcui` assets, README).

## Data-driven menu

The main menu and settings categories are driven by JSON in `assets/abc/abcui/gameui/config/` (installed to `<mod>/abc/abcui/gameui/config/`): adding a file under `settings/` extends the pages and ordinary settings, loaded automatically at startup; labels may be literal text or `#token` lookups in `abcui/lang/*.txt`. Limits: 1 MiB per page, 64 JSON files, 512 items per page.

## Feature overview

- HUD panels: Health / Flashlight / AmmoBar / AmmoStack / ItemStack / WeaponStack / Notice / DeadMsg / SideText / TextMenu / DamageTiles / PopNum / Indicator / player board / scoreboard / vote / MOTD / weapon choose / item highlight / Radar and more; data layer with NetworkMessages / WeaponResource / SpriteResource (GoldSrc .spr decode + hud.txt) / HudState / TextFeed; the original HUD is suppressed by sig-scan on the HUD_Redraw chain.
- Menu: main menu, settings (general/input/mouse/video/audio/voice/captions/multiplayer/advanced), server browser, create game, player list, vote creation (ban/kick/kill/map with the server's map list), connect/loading/error pages, console, direct-connect/password dialogs, quit confirmation; text input goes through the IME-aware input bridge. The pause menu's **Vote Menu** row and the original `votemenu` command both open the RmlUi page, replacing the legacy GameUI vote dialog.
- Menu backdrop: `abc_ui_menu_video` picks the looping movie behind the no-map menu (blurred by `abc_ui_menu_backdrop_blur` gaussian rounds, 0..4, default 2; 0 shows the unblurred capture); in a level the pause menu blurs the live frame instead.
- Retained cvars: `hud_danger_health`, `hud_danger_armor`, `hud_deathnotice(+_time/_count)`, `hud_sideinfo`; in-game diagnostics: `abc_ui_debugger` (RmlUi debugger) and `abc_ui_probe` (font/panel/element state dump).
- Avatars and real names come from the dynamically bound game steam_api.dll (Bridge/Native backend abstraction, no steam_api.lib import dependency); fonts are loaded from the system font directory through `Rml::LoadFontFace`.

## Runtime requirements

`plugins.lst` order (AbcUi must load after Renderer; it performs a fatal check on Renderer at startup):

```text
VGUI2Extension.dll
Renderer.dll
AbcEssential.dll
AbcUi.dll
```

## Submodules

| Path | Upstream | Pin |
|---|---|---|
| `thirdparty/AbcCore` | https://github.com/DrAbcOfficial/AbcCore | `d6240cb` (Renderer-free) |
| `thirdparty/Renderer` | https://github.com/MetaHookSv/Renderer | `ee7c1bd601f3cecd03fa0496db2cf0c4a919a452` (public interfaces and `gl_common.h` sync) |
| `thirdparty/RmlUi` | https://github.com/mikke89/RmlUi | tag `6.3` (`ba95ffe8bfb6370efb2cdcca927eaad4710c5413`) |
| `thirdparty/freetype` | https://github.com/freetype/freetype | tag `VER-2-13-3` (`42608f77f20749dd6ddc9e0536788eaad70ea4b5`) |
| `thirdparty/FreeImage` | https://github.com/hzqst/FreeImage_clone | `007c9e4c5d4198a1646b6c5274fd855be9cca7ef` |
| `thirdparty/VGUI2Extension` | https://github.com/MetaHookSv/VGUI2Extension | `01f9ad225aef5815eb1c02539667a9872cc2a511` (headers only) |
| `thirdparty/SteamSDK` | https://github.com/MetaHookSv/SteamSDK | `3c1abaf6277f9f99fd16ef40557d6852820b848f` |

## License

Licensed under the [BSD 3-Clause License](LICENSE); each dependency keeps its own license.
