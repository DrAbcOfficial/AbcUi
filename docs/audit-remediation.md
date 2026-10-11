# P0–P8 UI remediation

No game launch, deployment, live Steam/GPU test or actual user configuration change. Core pinned to 7f1a4de; Core HTTP-02/03/04 remain blocked by explicit enable gate.

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

| UIR-01 | Fixed: bounded dimensions/spans, clipped crop; production ResourceLimits tests. |
| UIR-02 | Fixed: both sprite paths enforce type0/format/palette/frame bounds; group assets rejected, not supported. |
| UIR-03 | Fixed: resolution reload plus pinned Rml spr:// source invalidation; video-mode runtime unverified. |
| UID-01 | Fixed: validated resource names and bounded std::string paths; production predicate tests. |
| UID-02 | Fixed: atomic name/slot index updates; directly compiled WeaponResource.cpp tests. |
| UID-03 | Fixed: SDK prediction capacity64 and byte ammo map; production IDs31/32/63/64 tests, actual server allocations not claimed. |
| UID-04 | Fixed: request Steam information before zero-handle interpretation; real Steam unverified. |
| UID-05 | Fixed: service-level frame pump, not scoreboard-dependent; real Steam unverified. |
| UID-06 | Fixed: decoded upload retry and cache-owned IDs cleaned after Rml; GPU failures not dynamically injected. |
| UID-07 | Fixed: complete language token sets, enum mapping unchanged; automated token coverage. |
| UID-08 | Fixed: reset debugPinned and stale slot state; no real server identity test. |
| UIP-04 | Fixed: per-element delta cache and retained Radar img nodes; no FPS claim. |
| UIP-05 | Fixed: image and clip both67dp; no screenshot claim. |
| UIP-06 | Fixed: all AmmoBar/Flashlight show paths share visibility gate; live message sequence unverified. |
| UIP-07 | Fixed: marker live identity/visibility/cvar/range checks; same-model same-slot epoch unavailable. |
| UIP-08 | Fixed: failed/front-negative projection explicitly hides retained DOM. |
| UIP-09 | Fixed: reopening retained MOTD page rearms countdown. |
| UIP-10 | Fixed: explicit/predicted drop-all clears selection and emits; confirmation rechecks ownership and shouldDraw. Production registry tests. |
| UIP-11 | Fixed: density-aware nonnegative menu clamp; resolutions not visually tested. |
| UIP-12 | Fixed: size mode and live spacing applied to rows/images; documented supported bounded spacing. Visual parity unverified. |

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
- UIP-12 implemented: size 0 auto/1 normal/2 compact, positive spacing overrides bounded 8..128 raw px; 0 uses resolution-aware defaults. Auto uses predicted normal-row content height against the 430dp viewport budget, independent of current compact layout (avoids mode oscillation); defaults normal28dp and legacy screen-height compact18/22/24/26/28px. Real size/spacing visual parity remains unverified.
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

## Final review, suggestions and boundaries

- UIF-S01: host delta-time contract retained, no double-differencing. Static client evidence from original audit; engine caller runtime not revalidated.
- UIF-S04: truthful explicit factory assembly comment, restartable instance construction; shared Health/Flashlight ColorUtil delegates to corrected Core. Registry/factory metaprogramming intentionally not introduced. Remaining fixed-format scoreboard helpers are bounded owned strings, consolidation deferred rather than broad visual rewrite.
- UIF-S05: debug logs use system temp directory and DbgHelp default symbol path; probe close/reset before context shutdown; SourceSpew comment states continue vs fatal SysError accurately.
- UIR-S03: supported rendering subset is 2D affine transforms and rectangular viewport scissor. CSS perspective/3D transforms, clip masks, filters remain unsupported; assets do not currently require them. No direct GL workaround added.
- UID-S04: consolidated with UIP-10; predicted last-weapon loss also emits drop-all.
- UID-S07: main-thread engine/Steam APIs remain main-thread; player traces resolve entity index into physent info mapping when available, world-only traces use -1. Correct target runtime physent pool/index semantics NOT dynamically validated; inherited feet endpoint/hull2 retained per D-19.
- UIP-S02: passive HUD show gates hideAll, stacks/indicator/playerboard explicitly hide retained documents; scoreboard/MOTD/textmenu/vote remain interaction exceptions. Spectator behavior kept per-panel; full cross-panel visibility matrix still requires user/game acceptance.
- UIP-S04: Effect resolves shared engine danger cvar rather than Health header; menu-open state lives in framework Commands, not concrete TextMenu include. Formatting consolidation deferred where it would change visual protocol; no public persisted names changed.
- UIP-S06: legacy english PopNumber_B is intentionally still a 1e8 suffix despite conventional English B=1e9; approved numeric/visual baseline retained and discrepancy documented, not silently relabeled. TextMenu dp calibration retained despite legacy raw-px explanatory comments; underwater baseline128 retained. These are documented compatibility choices, not claimed numeric convention fixes.
- Fragmented-message timeouts remain deferred; hard budgets prevent unbounded memory. Asset fuzzing/FreeImage internals, actual Steam retry, GDI/render context, engine command/input reinitialization, full DOM performance/visuals remain unverified. D-16 GameUI arbitration intentionally deferred as approved.

### Independent self-review of changed lifetime paths

1. Scoreboard detaches mousedown/click before Listener delete; panel cleanup removes data subscriptions before document close. Probe closes while context still exists. WeaponChoose/TextMenu live command state cleared on destruction.
2. Radar unregisters external prefix before destroying FBO/masks; opaque records draw only while prefix remains registered, no stale raw-ID ownership inference. Renderer never deletes external or cache-owned IDs.
3. Rml::Shutdown releases logical records first; sprite/avatar caches then delete every native ID (including never-delivered avatar uploads), then Native backend caches disconnect, then render/file/system/VGUI bridges release. Renderer destructor catches leftover owned records if Rml did not deliver ReleaseTexture. No live GPU proof claimed.
4. DOM caches forget removed children; Radar retains only current entity index nodes. Item markers validate model/visibility and no longer share stale text cache on address reuse.
5. Core strict NUL exposed formerly implicit optional TextMsg tails. Production handler now explicitly reads only present optional fields through remaining(); a present unterminated field still rejects state update.
6. Weapon ownership is set/map-based, no 32-bit mask shift for IDs32..63; actual SC parser reads WeaponList/CurWeapon ID as short and prediction uses SDK64 extent. This proves accepted representation/capacity, not any server allocation claim.
7. ScoreInfo frags/deaths bounded to1e6 per player so 32-player integer aggregates remain in range; finite health/armor bounded before float-to-int rendering.

### Exact final offline commands

```
cmake -S D:/Desktop/migration/AbcUi -B D:/Desktop/migration/AbcUi/build/win32-debug -DABC_UI_BUILD_TESTS=ON
cmake --build D:/Desktop/migration/AbcUi/build/win32-debug --config Debug
ctest --test-dir D:/Desktop/migration/AbcUi/build/win32-debug -C Debug --output-on-failure
cmake -S D:/Desktop/migration/AbcUi -B D:/Desktop/migration/AbcUi/build/win32-release -DABC_UI_BUILD_TESTS=ON
cmake --build D:/Desktop/migration/AbcUi/build/win32-release --config Release
ctest --test-dir D:/Desktop/migration/AbcUi/build/win32-release -C Release --output-on-failure
```

Production tests: TextUtil/ResourceLimits/Format headers plus directly compiled WeaponResource.cpp with fake engine boundary only (no copied production algorithms). CHECK remains active in Release. Final suite: 2/2 per configuration.
