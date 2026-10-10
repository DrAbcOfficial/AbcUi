# AbcUi

Sven Co-op / MetaHookSv HUD 插件（`AbcUi.dll`）：以 RmlUi 重建 ABCEnchance 全部 HUD UI，
渲染只经 IMetaRenderer（MetaHookSv Renderer 插件 API），零直接 OpenGL。
ABCEnchance 拆分迁移产物之一，共享库为 [AbcCore](https://github.com/DrAbcOfficial/AbcCore)。
迁移计划与进度见 `plans_docs`（不随本仓库分发）。

## 构建

需要 Windows + MSVC（VS 2026）+ CMake ≥ 3.24，**仅 Win32 x86**。

```bash
git clone --recursive https://github.com/DrAbcOfficial/AbcUi.git
cmake --preset win32-release
cmake --build --preset win32-release
```

## 部署

```bash
cmake --build --preset win32-release --target deploy
```

DLL → `<game>/svencoop/metahook/plugins/`，UI 资产（RML/RCSS/图片/语言表）→ `<game>/svencoop/abcui/`，
`FreeImage.dll` → `<game>/svencoop/metahook/dlls/FreeImage/`。

## Phase 3 状态

RmlUi 集成层（RenderInterface/SystemInterface/FileInterface/InputBridge/KeyMap）、HUD 框架骨架
（PanelManager/HudPanel/Theme/Localization/Assets）与演示面板已落地；验收驱动
`scripts/acceptance_ui.py`（管道 stdio + RCON + 截图像素证据，16 项）。

## 运行依赖

plugins.lst 顺序（AbcUi 必须在 Renderer 之后，启动时对 Renderer 做致命检查）：

```
VGUI2Extension.dll
Renderer.dll
AbcEssential.dll
AbcUi.dll
```

## 子模块

| 路径 | 上游 | pin |
|---|---|---|
| `thirdparty/AbcCore` | https://github.com/DrAbcOfficial/AbcCore | 跟随 |
| `thirdparty/RmlUi` | https://github.com/mikke89/RmlUi | tag `6.3` (`ba95ffe8bfb6370efb2cdcca927eaad4710c5413`) |
| `thirdparty/freetype` | https://github.com/freetype/freetype | tag `VER-2-13-3` |
| `thirdparty/FreeImage` | https://github.com/hzqst/FreeImage_clone | `007c9e4c5d4198a1646b6c5274fd855be9cca7ef` |
| `thirdparty/VGUI2Extension` | https://github.com/MetaHookSv/VGUI2Extension | `b290930b9fd3f25623e05e220a26b2be9da21a7f`（仅取头） |

RmlUi 静态链入（freetype 经 add_subdirectory + `Freetype::Freetype` 别名供 RmlUi 消费）；FreeImage 链导入库，
`FreeImage.dll` 随 install 部署到 `svencoop/metahook/dlls/FreeImage/`（与 Renderer 同款，启动器将该目录加入 PATH）。
SteamAPIBridge 子模块在记分板批次（Phase 6）前引入。
