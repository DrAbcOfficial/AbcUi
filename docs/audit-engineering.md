# Engineering and acceptance remediation

- ENG-01/07: clean exit requires exit_code==0, not alive and not killed; all owned sessions close through finally. Python failure-path tests pass; no game was started.
- ENG-02: Essential no longer counts manual event trigger as PASS; UI skip accounting uses the same rule. A live event mutation test needs isolated config and is not fabricated here.
- ENG-03: fresh screenshot helper rejects old directory entries. UI already used freshness; now shared within each independently cloned repository.
- ENG-04: deploy depends on AbcUi and FreeImage. CMake configure passed; no install/deploy was run against the user's game.
- ENG-05: default VS2026 workflow requires CMake4.2; presets and README updated. ABC_GAME_DIR is environment supplied, not a developer drive path.
- ENG-06: verified PNG is staged and published with no-overwrite hard link; same-name targets are preserved, batch failure prevents TGA deletion, zero successes do not divide by zero. Four temp-directory regressions pass. No real assets converted. Partial successful PNG outputs intentionally remain available for inspection on a failed batch.
- ENG-A01: legacy P6 fragment regression uses thirdparty/AbcCore actually pinned by Ui. It remains a supplemental fragment test; direct production tests added under tests are preferred. P6 regression passed after Core update.
- ENG-A02: support helpers are identical in independently cloned repos; no hidden sibling imports. A versioned cross-repository test package is deferred rather than added without a distribution contract.
- ENG-A03/04: portable presets and local CMakeUserPresets ignored; AbcCommon byte identity retained. Inherited SDK warning suppression is not broadly changed without a separate baseline.
- ENG-A05: README describes P0-P8 panels and actual Steam dual backend.
- ENG-A06: tests now include production helpers in addition to smoke scripts, but no claim of full live-server, GPU or Steam validation.

Validation: `python -m unittest discover -s scripts/tests -v` — 9/9 pass; expected FAIL text inside invalid-image tests is their asserted error-path output, not a failed test suite. `python scripts/regression_p6.py` — PASS. CMake configure — success, with existing FreeType compatibility/PkgConfig warnings. See audit-remediation.md for C++ stages and their validation.
