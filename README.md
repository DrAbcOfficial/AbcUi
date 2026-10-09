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

DLL → `<game>/svencoop/metahook/plugins/`，UI 资产（RML/RCSS/图片）→ `<game>/svencoop/abcui/`。

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

RmlUi / freetype / FreeImage 尚未接入构建（Phase 3 RmlUi 集成时接线）。
SteamAPIBridge 子模块在记分板批次（Phase 6）前引入。
