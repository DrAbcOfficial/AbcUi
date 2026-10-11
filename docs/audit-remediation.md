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

## Resource/registry stage

- UIR-01 fixed: production ResourceLimits 4096-axis/64MiB image budget, subtraction-based span checks, intersection-before-crop allocation, safe URI dimensions and bounded reads. Boundary tests include Win32 overflow sizes and INT_MIN/INT_MAX crop coordinates.
- UIR-02 fixed: atlas and standalone reject group frames consistently, validate format/palette/frame budget; malformed trailing frames keep only preceding valid type0 frames.
- UIR-03 partial: VidInit now refreshes hud.txt when width crosses resolution band. Existing Rml cached spr:// URI invalidation still needs renderer lifetime integration below; not claimed fully fixed yet.
- UIR-S02 addressed: bounded file/memory reads, RAII FILE/FreeImage converted bitmap, image and stream multiplication checks. FreeImage internal decoder allocation before output inspection not fuzzed.
- UIR-S04 partial: sibling download/addon search now handles absolute game roots. Engine filesystem search integration deferred; projection policy treated separately.
- UID-01 fixed: trusted-character/length checks and std::string path assembly avoid secure CRT termination; malformed names rejected.
- UID-02 fixed: duplicate IDs update name and slot indexes, resolve slot collisions, preserve object address and selected slot.
- UID-03 fixed: shared weapon limit derived from SDK local_state_t weapondata extent; AmmoX supports byte domain rather than arbitrary 32 cap. No assertion that current server uses extended IDs.
- UIP-10 fixed: drop-all clears selection and emits event; confirm rechecks shouldDraw and ownership; global command pointer cleared in destructor.

## Steam/texture ownership stage

- UID-04 fixed: Native requests user information before interpreting the small-avatar handle; pending/unknown persona is retryable, dimensions bounded. Real Steam downloads not exercised.
- UID-05 fixed: avatar service pumped from RmlUiModule every frame independent of Scoreboard; registered Radar interests retry with 1-second throttle.
- UID-06 fixed: decoded-but-unuploaded entries retry uploads; sprite/avatar caches explicitly own and release all native IDs after Rml shutdown, including IDs never delivered to Rml. Upload wrapper has void API: backend driver success cannot be detected beyond createTexture failure.
- UIR-S01 addressed: per-load opaque texture records separate logical handle from engine ID and ownership; cached sprite/avatar IDs borrowed, file/font IDs owned, external prefix resolved live. Shared URIs cannot free cache IDs or collide with regenerated external IDs. GPU failure/double-release runtime test not performed.
- UIR-03 completed: VidInit releases only spr:// Rml sources through pinned ReleaseTexture, so resolution-dependent metadata resolves again. No blanket file/font texture invalidation.
- UID-S06 addressed: Native disconnect clears caches/interfaces without SteamAPI_Shutdown, service/native entry caps 256. Decoded pixel duplication remains bounded, intentional for retry; no background-thread Steam use.

## Panel/data state stage

- UIP-04 fixed: per-panel DomState suppresses identical text/style/attribute writes, PlayerBoard and ItemHighlight text cached; Radar retains per-index img nodes and diffs source/position instead of rebuilding every frame. No measured FPS claim.
- UIP-05 fixed: PlayerBoard fill image width and clipping basis both 67dp.
- UIP-06 fixed: AmmoBar refresh and Flashlight suit changes share hide visibility gate.
- UIP-07 fixed: grenade markers revalidate cvar/model/message/visibility/expiry and current range; no stale non-grenade slot marker. Same-model reuse before expiry lacks engine generation token (remaining limitation).
- UIP-08 fixed: PopNum explicitly hides failed/front-negative projections, re-shows when valid. 128-element cap.
- UIP-09 fixed: each MOTD reopen re-arms HasContent/countdown; retained Page not confused with active timer.
- UIP-11 fixed: menu clipping scales 180/96 design units by density and never inverts clamp bounds.
- UIP-12 implemented: size 0 auto/1 normal/2 compact, positive spacing overrides bounded 8..128 raw px; 0 defaults normal28/compact18. Auto currently selects compact above 12 players (documented approximation to legacy measured-overflow choice). Real size/spacing visual parity remains unverified.
- UID-07 fixed: protocol enums unchanged, Team1..4 mapped to shipped color tokens, remaining class/unknown translations supplied in all three languages.
- UID-08 fixed: reset clears debugPinned; fake players cannot retain update suppression across maps.
- UIF-S02 addressed for stable health/armor/flashlight/server/nextmap snapshots; lazy panels hydrate from data. HudState owns copied clientdata and resets on VidInit (UID-S01). Transient preassembly events are not queued.
- UIF-S03 addressed: passive documents pointer-events none, Show uses FocusFlag::None. Combined hover/wheel/modal behavior still needs game acceptance.
- UIF-S05 partially addressed: ProbeDoc explicitly closes/resets before Rml shutdown. Developer symbol/log path cleanup remains tracked below.
- UID-S02 finite bounded ScoreInfo validation before state mutation; native handler still receives original messages. UID-S03 disconnected name/changed Steam identity resets records; exact SvEngine userdata identity still not dynamically inspected.
- UID-S05 unknown #game tokens pass to original handler instead of swallowing untranslated keys; English killfeed regex semantics retained.
- UIP-S01 bounded menu16KiB/MOTD64KiB/pop128; timeout for indefinitely fragmented messages remains follow-up (bounds already prevent unbounded accumulation).
- UIP-S03 item markers validate current modelindex and EF_NODRAW each redraw.
- UIP-S05 live vote/deathnotice-off hides current content; crosshair center updated before zoom return and missing document guarded.
- UIR-S04 projection now preserves negative screen coordinates for consumer clipping instead of artificial edge pinning.

All panel visual/hover/GPU/map transitions remain offline-unverified; build and helper tests alone are not equivalent to game acceptance.
