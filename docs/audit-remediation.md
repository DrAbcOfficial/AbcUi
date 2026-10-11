# P0–P8 UI remediation

No game launch, deployment, live Steam/GPU test or actual user configuration change. Core pinned to 8482893; Core HTTP-02/03/04 remain blocked by explicit enable gate.

## Status

| ID | Status / evidence / validation |
|---|---|
| UIF-01 | Fixed: Rml wheel up=-1/down=+1; pinned backend sign contract. Real input/scroll not dynamically validated. |
| UIF-02 | Fixed: session instances recreated from factory at each init after shutdown. Engine restart sequence not dynamically validated. |
| UIF-03 | Fixed: shared production parseTranslationLine for newline and EOF, English table per-token fallback. |
| UIF-04 | Fixed: failed SetClipboardData frees retained HGLOBAL, empty clipboard text accepted. Actual clipboard deliberately untouched; failure injection not run. |
| UIP-01 | Fixed: detach both scoreboard event listeners before delete/Document Close. Pinned Rml lifetime contract checked; real engine unload not run. |
| UIP-02 | Fixed: Format result passed as c_str to %s. |
| UIP-03 | Fixed: production trimNewlines uses indices/single substring, including all-newline. |

Remaining UIR-01..03, UID-01..08, UIP-04..12 and suggestions are in progress, not implicitly passed.

## Validation commands

```
cmake --build D:/Desktop/migration/AbcUi/build/win32-debug --config Debug
cmake --build D:/Desktop/migration/AbcUi/build/win32-release --config Release
```

Both configurations compiled against production sources; production TextUtil/Format helper regression CTest passed 1/1 in each configuration (ABC_UI_BUILD_TESTS=ON). No runtime FPS or visual correctness claims.
