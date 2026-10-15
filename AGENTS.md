# AGENTS.md

面向在本仓库中工作的 AI/自动化协作者。开始前先读 `README.md`（本仓库唯一随发布分发的说明文档）。

## 硬性约束

- 提交内容（代码、注释、文档、提交信息、标签）不得出现本机路径、个人目录、会话记录或任何凭证；游戏目录一律通过 `ABC_GAME_DIR` 环境变量提供。
- 不提交测试脚手架、验收脚本、证据截图、计划与进度记录等会话产物。
- README 是唯一对外文档；界面、配置格式或 cvar 行为变化必须同步更新 README。
- 子模块 pin 只指向对应上游已公开的提交；升级 pin 前先确认该提交已推送。
- 渲染只经 IMetaRenderer：不得直接调用 OpenGL，不得引入 VGUI2 绘制依赖。

## 构建

仅 Win32 x86：

```bash
cmake --preset win32-debug
cmake --build --preset win32-debug    # Release：win32-release
```

## 约定

- 命名遵循 LLVM 风格；界面结构经 GameUI 组合根编排，页面资源放 `assets/abc/abcui/`（rml/rcss/lang/config）。
- RmlUi 6.3 的 RCSS 语法限制：`font-family` 只接受单族名（CJK 回退走 fallback face），transition 缓动名使用 `linear-in` 系列。
- 设置与主菜单的页面结构由 `assets/abc/abcui/config/*.json` 驱动，新增页面通过 JSON 扩展而非硬编码。
- 提交信息使用 `type(scope): 摘要`（feat / fix / refactor / docs / build / chore），正文说明动机与验证方式。
- 交付前自查：无本机路径与凭证；README 与实际行为一致；Debug 与 Release 均可构建。
