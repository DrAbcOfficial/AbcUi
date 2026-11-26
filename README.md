# AbcUi

HUD and menu replacement for Sven Co-op. It rebuilds the game interface, from the HUD to the main menu and settings, in a single consistent style. It requires [AbcCore](https://github.com/DrAbcOfficial/AbcCore), MetaHookSV and VGUI2Extension.

## Features

- **HUD** — health, armor, ammo, flashlight, notices, damage numbers, kill feed, side text, text menus, player board, scoreboard, vote, MOTD, radar, crosshair and item highlight. Health, armor and magazine numbers briefly grow when they change.
  Weapon slots open the selector even when empty. Left-click confirms a
  highlighted weapon once; after it closes, left-click returns to firing.
  Weapons can be regrouped into other selector slots from the console.
  While a text menu is open, number keys 0–9 select menu items instead of weapons.
- **Chat** — new chat input and message log with full input method support.
  Enter sends or closes chat without activating its gameplay binding.
- **Menu** — main menu, settings, server browser, create game, player list, vote creation, console, MOTD, inventory window, command menu, connect and loading screens, and quit confirmation. Pages fade in and out while preserving dimmed controls, and stop accepting input as soon as they start closing. Create game remembers the last server name, password, player limit and options. The in-game menu opens the server browser as well; joining from it leaves the current server first. Menu entries can also run a console command of your choice, for example joining a server directly.
- **Settings** — video, audio, voice, captions, mouse, input, multiplayer, advanced, renderer and menu pages, all applied from inside the game.
- **Character preview** — live player model facing the screen at a slight angle on the main menu, plus a model picker with favorites in the settings. The main menu character's camera, position, rotation and light can be adjusted in Settings > Menu.
- **Media** — menu music, background movie and a media player for the game soundtrack.
- **Voice mute** — per-player mute list shared by the player list and the scoreboard.
- **Voice speakers** — team-voice speaker list with avatars and a status badge when your microphone or voice receive is off, replacing the native speaker icons.
- **Spectator bar** — mode, target and target health instead of the native bar.
- **Modules** — view installed UI modules and enable or disable them from Settings; changes are saved when you exit the game.

## Install

Extract the archive into the Sven Co-op folder. It contains `svencoop` at its root, so there is no extra folder to copy.

Character previews require the current [StandaloneView](https://github.com/MetaHookSv/StandaloneView).
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

The default drawing backend is `renderer`. If Renderer.dll or its drawing
backend is missing, AbcUi automatically uses `gl`. If neither backend is
available, startup reports the missing backend.

The `gl` backend requires OpenGL 3.3 or newer and cannot run while Renderer.dll
is loaded. It supports menu blur, background movies and character previews.
Menus also render when no map is loaded and during loading screens.
The radar shows player markers without a map image. Other startup failures
report their cause rather than switching backends.

Renderer-free installations still need FreeImage.dll in the MetaHook DLL search
path. Install Mile.FFmpeg.dll there as well for background movies and video.
These DLLs are not included in the AbcUi archive.

Character previews follow your selected installed player model after leaving or
rejoining a game, including models from downloaded and addon content.
The two menu animation models ship inside the gameui module package and are
automatically extracted to
`svencoop/abc/abcui/custom/abcui.gameui/models/menu_action/` when it loads.
No separate model installation is needed.

## Third-person HUD

The archive ships both the classic HUD and the third-person HUD group. The
third-person group keeps the health, armor, ammo and flashlight readouts on
your character in third person and returns them to a fixed bottom row in first
person; every other element stays where it is. It replaces the classic health,
ammo and flashlight readouts, so enable one of the two sets in Settings >
Modules.

## Settings

All options are available in the in-game Settings menu. Changing the menu action
model updates its animation choices without refreshing unrelated settings.
Live character previews stay animated while you adjust multiplayer settings.
Applying a new resolution or display mode restarts the game and then asks you to
keep or revert the change.

AbcUi stores its backend selection, model favorites, create-game settings,
muted players, blocked servers, pending video confirmation and main menu
character settings together in
`svencoop/abc/abcui/config.json`. When this file does not exist, AbcUi creates
it with the default settings. Older separate configuration files are not read.
Unreadable files are not overwritten. Other game settings remain in the
game's own configuration.

Set `render_backend` to `renderer` or `gl` in this file, then fully restart the
game to apply it:

```json
{
  "render_backend": "renderer"
}
```

Edit only that field in an existing file to retain your other settings.
The old `abc_ui_render_backend` console variable is no longer used.

Useful console commands and variables:

| Name | Description |
|---|---|
| `abc_ui_render_backend_list` | List installed backends, their drawing features and the active selection. |
| `abc_ui_chat` | `0` restores the native chat. |
| `abc_ui_spectator` | `0` restores the native spectator bar. |
| `abc_ui_menu_music` | Menu music track. |
| `abc_ui_menu_video` | Menu background movie. |
| `abc_ui_menu_backdrop_blur` | Menu background blur, from 0 to 4. |
| `abc_ui_menu_action_model` | Base model that drives the main menu character's animation. |
| `abc_ui_menu_model_sequence` | Animation sequence played by that model. |
| `abc_ui_menu_weapon_model` | Weapon held by the main menu character. |
| `abc_ui_division_follow` | `1` follows your character in third person (default); `0` keeps the readouts fixed. |
| `abc_ui_division_side` | `0` keeps the readouts on your character's left (default); `1` puts them on the right. |
| `abc_ui_weapon_filter` | `1` applies your weapon selector slot rules; `0` shows the server layout. |

## Main menu character

Settings > Menu has a **Main menu character** card for the character on the
main menu. Each value is a slider with its current value; angles are in
degrees.

| Control | Range | Default |
|---|---|---|
| Camera position X / Y / Z | -1024 to 1024 | 121 / -70 / 55 |
| Camera angles pitch / yaw / roll | -180 to 180 | 16 / 150 / -3 |
| Model position X / Y / Z | -256 to 256 | 0 / 0 / 0 |
| Model rotation pitch / yaw / roll | -180 to 180 | 0 / -8 / 0 |
| Light angle horizontal / pitch | -179 to 180 / -89 to 89 | 172 / 0 |
| Light color | color picker | 255, 245, 230 |
| Ambient / Shade | 0 to 255 | 96 / 110 |

A positive light pitch shines downwards. **Restore defaults** in the card
returns every value to the defaults above.

Click **Apply** to save the values. They show on the main menu as soon as you
return to it; no restart is needed. **Return** discards changes you have not
applied. **Restore Defaults** at the bottom of the page also returns this card
to its defaults.

The values are saved in `svencoop/abc/abcui/config.json`:

```json
{
  "module.abcui.gameui.menu_preview": {
    "version": 1,
    "camera_origin": [121, -70, 55],
    "camera_angles": [16, 150, -3],
    "model_origin": [0, 0, 0],
    "model_angles": [0, -8, 0],
    "light_direction": [-0.990268, 0.139173, 0],
    "light_color": [255, 245, 230],
    "ambient": 96,
    "shade": 110
  }
}
```

You can also edit this section by hand. AbcUi creates it the first time you
change a value and click Apply. Values outside their range are limited to it.
If the section or the file cannot be read, the defaults are used, the file is
left unchanged, and your changes apply to the current session only.

## Weapon selector layout

Weapons normally appear in the slots the server assigns. Set
`abc_ui_weapon_filter 1` to use your own slot and position rules instead;
`0` restores the server layout. The rules are stored in
`svencoop/abc/abcui/config.json`:

```json
{
  "module.abcui.weaponfilter.weapon_filter": {
    "version": 1,
    "weapons": [
      { "name": "weapon_crowbar", "slot": 0, "position": 0 },
      { "name": "weapon_ins2coach", "slot": 3, "position": 2 }
    ]
  }
}
```

Edit the rules from the console; each change applies immediately and is
saved:

| Command | Description |
|---|---|
| `abc_ui_weapon_slot <weapon> <slot> <position>` | Places a weapon in a slot from 0 to 9 at a position from 0 to 255. |
| `abc_ui_weapon_slot_remove <weapon>` | Removes a rule. |
| `abc_ui_weapon_slot_list` | Lists the rules and whether the filter is on. |
| `abc_ui_weapon_slot_clear` | Removes every rule. |
| `abc_ui_weapon_slot_reload` | Re-reads the rules after editing the file. |

The rules only change where the selector shows a weapon; the weapon you
confirm is still the one that reaches the server. Disabling the Weapon Filter
module in Settings > Modules restores the server layout as well.

## Modules

Settings > Modules lists the installed UI modules with their versions, authors,
dependencies and conflicts. Disable a module by changing its package extension
from `.zip` to `.disable`; enable it by changing the extension back.

Changes are recorded during the session and written when you exit the game
normally. They take effect after a full restart. The Modules page offers a
restart button that exits and relaunches the game so the pending changes
apply right away. A forced exit loses the pending changes. Disabling a module
that others depend on can also disable those modules on the next launch.
Disabling `abcui.gameui` removes the Modules page itself.

## Custom resources

You can add or replace the files of a UI module without repacking it. Put them
in `svencoop/abc/abcui/custom/<module id>/`, with the module's id as Settings >
Modules lists it and the file's path inside the module's `.zip` file:

```text
svencoop/abc/abcui/custom/abcui.gameui/gameui/scence/video/myclip.mp4
svencoop/abc/abcui/custom/abcui.gameui/gameui/rml/mainmenu.rml
svencoop/abc/abcui/custom/abcui.gameui/models/menu_action/mymodel.mdl
```

A file with the same path as one inside the module replaces it; any other file
is added. A background movie or menu music track placed there appears in
Settings > Menu, and an action model placed in `models/menu_action` appears in
the action model list. Restart the game after adding, replacing or removing
files.

A file that cannot be read is skipped and the module's own file is used
instead. The module's manifest and program file always come from the package.
Links to folders outside are not followed, and the files of a module that is
not loaded are not used.

The menu action models are extracted only when their files do not exist yet.
A model you placed there is never overwritten, so when a module update changes
a packaged model, delete the old file to receive the new one.

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

The archive is written to `build/win32-release/dist/AbcUi-<version>.zip` and
ships the full module set, including both the classic and third-person HUD
groups. The third-person group ships disabled; enable it in Settings >
Modules.

If AbcCore is checked out elsewhere, pass `-DABC_CORE_DIR=<path>` when configuring. The Renderer redirect is needed because AbcUi uses a Renderer version from the DrAbcOfficial fork.

## License

Licensed under the [BSD 3-Clause License](LICENSE).
