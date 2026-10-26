# AbcUi

HUD and menu replacement for Sven Co-op. It rebuilds the game interface, from the HUD to the main menu and settings, in a single consistent style. It requires [AbcCore](https://github.com/DrAbcOfficial/AbcCore), MetaHookSV and Renderer.

## Features

- **HUD** — health, armor, ammo, flashlight, notices, damage numbers, kill feed, side text, text menus, player board, scoreboard, vote, MOTD, radar, crosshair and item highlight.
- **Chat** — new chat input and message log with full input method support.
- **Menu** — main menu, settings, server browser, create game, player list, vote creation, console, MOTD, inventory window, command menu, connect and loading screens, and quit confirmation.
- **Settings** — video, audio, voice, captions, mouse, input, multiplayer, advanced, renderer and menu pages, all applied from inside the game.
- **Character preview** — live player model on the main menu, plus a model picker with favorites in the settings.
- **Media** — menu music, background movie and a media player for the game soundtrack.
- **Voice mute** — per-player mute list shared by the player list and the scoreboard.
- **Spectator bar** — mode, target and target health instead of the native bar.

## Install

Extract the archive into the Sven Co-op folder. It contains `svencoop` at its root, so there is no extra folder to copy.

Character previews require the current [StandaloneView](https://github.com/DrAbcOfficial/StandaloneView).
Install its DLL and gamedata, then list it first in
`svencoop/metahook/configs/plugins.lst`. Add `AbcUi.dll` after Renderer and AbcEssential:

```text
StandaloneView.dll
VGUI2Extension.dll
Renderer.dll
AbcEssential.dll
AbcUi.dll
```

AbcCore.dll is required and must be installed under `svencoop/metahook/dlls/AbcCore/`. It is not listed in `plugins.lst`.

Without a compatible StandaloneView installation, character previews are disabled;
the rest of the interface remains available.

Character previews follow your selected installed player model after leaving or
rejoining a game, including models from downloaded and addon content.

## Settings

All options are available in the in-game Settings menu. Changing the menu action
model updates its animation choices without refreshing unrelated settings.

Useful console variables:

| Name | Description |
|---|---|
| `abc_ui_render_backend` | Select the drawing backend. A full game restart is required. |
| `abc_ui_render_backend_list` | List the installed backends. |
| `abc_ui_chat` | `0` restores the native chat. |
| `abc_ui_spectator` | `0` restores the native spectator bar. |
| `abc_ui_menu_music` | Menu music track. |
| `abc_ui_menu_video` | Menu background movie. |
| `abc_ui_menu_backdrop_blur` | Menu background blur, from 0 to 4. |
| `abc_ui_menu_action_model` | Base model that drives the main menu character's animation. |
| `abc_ui_menu_model_sequence` | Animation sequence played by that model. |
| `abc_ui_menu_weapon_model` | Weapon held by the main menu character. |

## Build the dist package

Requires Windows, Visual Studio 2026 with the C++ workload, CMake 4.2 or newer, Python 3 for the UI module packages, and the AbcCore source next to AbcUi.

```bash
git config --global url."https://github.com/DrAbcOfficial/Renderer".insteadOf "https://github.com/MetaHookSv/Renderer"
git clone --recursive https://github.com/DrAbcOfficial/AbcUi.git
git clone --recursive https://github.com/DrAbcOfficial/AbcCore.git ../AbcCore
cd AbcUi
cmake --preset win32-release
cmake --build --preset win32-release --target dist
```

The archive is written to `build/win32-release/dist/AbcUi-<version>.zip`.

If AbcCore is checked out elsewhere, pass `-DABC_CORE_DIR=<path>` when configuring. The Renderer redirect is needed because AbcUi uses a Renderer version from the DrAbcOfficial fork.

## License

Licensed under the [BSD 3-Clause License](LICENSE).
