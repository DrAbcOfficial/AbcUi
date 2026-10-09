# AbcUi

Sven Co-op / MetaHookSv HUD 与菜单插件（`AbcUi.dll`）：以 RmlUi 重建 ABCEnchance 的全部界面，渲染只经 IMetaRenderer，零直接 OpenGL。共享库 [AbcCore](https://github.com/DrAbcOfficial/AbcCore) 以 submodule 静态链入。

## 构建

Windows + MSVC（VS 2026）+ CMake ≥ 4.2，仅 Win32 x86：

```bash
git clone --recursive https://github.com/DrAbcOfficial/AbcUi.git
cmake --preset win32-release
cmake --build --preset win32-release
```

嵌套子模块未随 `--recursive` 拉取时，在 `thirdparty/AbcCore` 内再执行一次 `git submodule update --init --recursive`。

## 部署

配置时设置环境变量 `ABC_GAME_DIR` 为游戏根目录（或传 `-DABC_GAME_DIR=...`）；未指定时不创建部署目标：

```bash
cmake --build --preset win32-release --target deploy
```

- UI 资产（RML/RCSS/图片/语言表/配置）→ `<game>/svencoop/abc/abcui/`；菜单视频/音乐在 `abc/abcui/scence/video|audio`，音乐由插件启动时镜像到引擎声音树 `svencoop/sound/abc/abcui/scence/audio/`。
- `AbcUi.dll` → `<game>/svencoop/metahook/plugins/`；`FreeImage.dll` → `<game>/svencoop/metahook/dlls/FreeImage/`。

## 数据驱动菜单

主菜单与设置分类由 `assets/abc/abcui/config/`（安装为 `<mod>/abc/abcui/config/`）内 JSON 驱动：新增 `settings/*.json` 可扩展页面与普通配置项，启动时自动加载；标签可用字面文本或 `#本地化 token`（`abcui/lang/*.txt`）。页面上限 1 MiB / 64 个 JSON / 单页 512 项。

## 功能概览

- HUD 面板：Health / Flashlight / AmmoBar / AmmoStack / ItemStack / WeaponStack / Notice / DeadMsg / SideText / TextMenu / DamageTiles / PopNum / Indicator / 玩家板 / 计分板 / 投票 / MOTD / 武器选择 / 物品高亮 / Radar 等；数据层 NetworkMessages / WeaponResource / SpriteResource（GoldSrc .spr 解码 + hud.txt）/ HudState / TextFeed；原版 HUD 经 sig-scan 在 HUD_Redraw 链上抑制。
- 菜单：主菜单、设置（general/input/mouse/video/audio/voice/captions/multiplayer/advanced）、服务器浏览器、创建游戏、连接/加载/错误页、控制台、直连/密码对话框、退出确认；文本输入走 IME 感知的输入桥。
- 保留 cvar：`hud_danger_health`、`hud_danger_armor`、`hud_deathnotice(+_time/_count)`、`hud_sideinfo`；游戏内诊断：`abc_ui_debugger`（RmlUi 调试器）、`abc_ui_probe`（字体/面板/元素状态转储）。
- 头像与真名经动态绑定的游戏 steam_api.dll 提供（Bridge/Native 双后端抽象，无 steam_api.lib 导入依赖）；字体从系统字体目录经 `Rml::LoadFontFace` 加载。

## 运行依赖

`plugins.lst` 顺序（AbcUi 必须在 Renderer 之后，启动时对 Renderer 做致命检查）：

```text
VGUI2Extension.dll
Renderer.dll
AbcEssential.dll
AbcUi.dll
```

## 子模块

| 路径 | 上游 | pin |
|---|---|---|
| `thirdparty/AbcCore` | https://github.com/DrAbcOfficial/AbcCore | `ca3b25f221a2173f24ee1a40521223d622cb63d2` |
| `thirdparty/RmlUi` | https://github.com/mikke89/RmlUi | tag `6.3`（`ba95ffe8bfb6370efb2cdcca927eaad4710c5413`） |
| `thirdparty/freetype` | https://github.com/freetype/freetype | tag `VER-2-13-3`（`42608f77f20749dd6ddc9e0536788eaad70ea4b5`） |
| `thirdparty/FreeImage` | https://github.com/hzqst/FreeImage_clone | `007c9e4c5d4198a1646b6c5274fd855be9cca7ef` |
| `thirdparty/VGUI2Extension` | https://github.com/MetaHookSv/VGUI2Extension | `01f9ad225aef5815eb1c02539667a9872cc2a511`（仅取头） |
| `thirdparty/SteamSDK` | https://github.com/MetaHookSv/SteamSDK | `3c1abaf6277f9f99fd16ef40557d6852820b848f` |
