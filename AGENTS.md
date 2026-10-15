# AGENTS.md

Guidance for AI/automation collaborators working in this repository. Read `README.md` first; it is the only document distributed with the release.

## Hard rules

- Nothing committed (code, comments, docs, commit messages, tags) may contain local machine paths, personal directories, session logs or any credentials; the game directory is always provided through the `ABC_GAME_DIR` environment variable.
- Do not commit test scaffolding, acceptance scripts, evidence screenshots, plans or progress logs; those are session artifacts that do not belong in the release.
- The README is the single public document; UI, config-format or cvar behaviour changes must update it in the same commit.
- Submodule pins may only point at commits that are already public upstream; confirm the commit is pushed before bumping a pin.
- Rendering goes exclusively through IMetaRenderer: no direct OpenGL calls and no VGUI2 drawing dependencies.

## Build

Win32 x86 only:

```bash
cmake --preset win32-debug
cmake --build --preset win32-debug    # Release: win32-release
```

## Conventions

- Naming follows the LLVM style; interface structure is orchestrated through the GameUI composition root, and page assets live under `assets/abc/abcui/` (rml/rcss/lang/config).
- RmlUi 6.3 RCSS constraints: `font-family` accepts a single family name (CJK falls back through the fallback face), and transition easing names use the `linear-in` family.
- Main-menu and settings page structure is driven by `assets/abc/abcui/config/*.json`; new pages are added through JSON rather than hard-coded.
- Commit messages use `type(scope): summary` (feat / fix / refactor / docs / build / chore) with motivation and verification in the body.
- Before handing off: no local paths or credentials; README matches actual behaviour; Debug and Release both build.
