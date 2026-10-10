#!/usr/bin/env python3
"""AbcUi in-game acceptance driver (plans_docs 05 §9 Phase 3 / 08 §3).

Same harness as acceptance_essential.py: pipe-stdio game launch (halflife-cli
bridge), RCON request/response, stdout mirror assertions, screenshot frame
read-back. Phase 3 checks:

  plugin load (Renderer/AbcEssential/AbcUi), RmlUi assembly banner, panels
  loaded, demo panel ready, abc_ui_debugger toggle echo, errors.log
  quiescence, menu + in-map screenshot pixel evidence of the demo document,
  in-map survival, clean quit.

Pixel evidence: the demo window paints #101418e0 (dark) with a #7fd28f
title; we scan the top-left quadrant for near matches of both. It is a smoke
signal, not a layout test - human confirmation of legibility stays with the
user (04 §5 pattern).

Usage: python scripts/acceptance_ui.py [--game <path>] [--map <name>] [--skip-map]
"""

import argparse
import glob
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def halflife_mcp_dir():
    override = os.environ.get("ABC_HALFLIFE_CLI_MCP")
    if override:
        return override
    sibling = os.path.normpath(os.path.join(REPO, "..", "halflife-cli", "mcp"))
    if os.path.isdir(sibling):
        return sibling
    raise SystemExit("halflife-cli/mcp not found; set ABC_HALFLIFE_CLI_MCP")


sys.path.insert(0, halflife_mcp_dir())

from game_process import GameProcess, build_game_argv  # noqa: E402
from halflifecli.plugin_config import read_endpoint_file  # noqa: E402
from rcon_client import RconConnection  # noqa: E402

GAME_DIR_HINTS = ("--game argument", "GAME_DIR", "halflife-cli/mcp/game_dir.txt")

# Demo document evidence colors (demo.rcss): title green + window background.
DEMO_TARGETS = [(0x7F, 0xD2, 0x8F, 24), (0x10, 0x14, 0x18, 18)]


def resolve_game_dir(override):
    if override:
        return override
    env = os.environ.get("GAME_DIR")
    if env and os.path.isfile(os.path.join(env, "svencoop.exe")):
        return env
    for hint in (os.path.join(halflife_mcp_dir(), "game_dir.txt"),
                 os.path.join(os.path.dirname(halflife_mcp_dir()), "scripts", "game_dir.txt")):
        if os.path.isfile(hint):
            with open(hint, encoding="utf-8") as stream:
                path = stream.read().strip().strip('"')
            if path and os.path.isfile(os.path.join(path, "svencoop.exe")):
                return path
    return None


class Check:
    def __init__(self):
        self.failures = []
        self.count = 0

    def ok(self, name, condition, detail=""):
        self.count += 1
        mark = "PASS" if condition else "FAIL"
        print(f"[{mark}] {name}" + (f" ({detail})" if detail else ""))
        if not condition:
            self.failures.append(name)
        return bool(condition)


def latest_screenshot(shots_dir, before_names):
    candidates = [p for p in glob.glob(os.path.join(shots_dir, "*"))
                  if p.lower().endswith((".tga", ".bmp", ".png", ".jpg", ".jpeg"))
                  and os.path.basename(p) not in before_names]
    if not candidates:
        return None
    return max(candidates, key=os.path.getmtime)


def demo_pixels_present(shot_path):
    from PIL import Image
    with Image.open(shot_path) as im:
        im = im.convert("RGB")
        w, h = im.size
        pixels = im.crop((0, 0, w // 2, h // 2)).getdata()
    found = [False] * len(DEMO_TARGETS)
    for px in pixels:
        for i, (r, g, b, tol) in enumerate(DEMO_TARGETS):
            if not found[i] and abs(px[0] - r) <= tol and abs(px[1] - g) <= tol and abs(px[2] - b) <= tol:
                found[i] = True
        if all(found):
            return True
    return all(found)


def take_screenshot(proc, shots_dir, before):
    proc.send_stdin("screenshot")
    time.sleep(2.5)
    return latest_screenshot(shots_dir, before)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", help="game root (contains svencoop.exe)")
    parser.add_argument("--map", default="osprey", help="small map for the in-map smoke (default: osprey)")
    parser.add_argument("--skip-map", action="store_true", help="menu-only run (no map load)")
    args = parser.parse_args()

    game_dir = resolve_game_dir(args.game)
    if not game_dir:
        raise SystemExit(f"no game directory resolved ({', '.join(GAME_DIR_HINTS)})")
    print(f"[test] game dir: {game_dir}")

    check = Check()
    errors_log = os.path.join(game_dir, "svencoop", "errors.log")
    errors_size_before = os.path.getsize(errors_log) if os.path.isfile(errors_log) else 0
    shots_dir = os.path.join(game_dir, "svencoop", "screenshots")

    proc = GameProcess(build_game_argv(game_dir), cwd=game_dir)
    proc.start()
    print(f"[test] launched pid={proc.pid}, waiting for RCON banner...")
    try:
        host, port = proc.wait_for_banner(120)
        print(f"[test] RCON endpoint {host}:{port}")
    except Exception as exc:
        print(f"FAIL: {exc}. Game stdout:")
        for line in proc.tail(40):
            print("   |", line)
        proc.kill()
        return 1

    endpoint = read_endpoint_file(game_dir, expected_pid=proc.pid)
    check.ok("endpoint metadata ready (pid/filetime validated)", endpoint is not None)

    # Assembly logs flow through the console bridge into the stdout mirror at
    # the first frames after the banner - wait for the ready line, then scan
    # the full buffer for the rest of the init sequence.
    try:
        proc.wait_for(re.compile(r"\[AbcUi\] RmlUi .+ ready \(context \d+x\d+\)"), 30)
        check.ok("stdout: RmlUi context ready banner", True)
    except TimeoutError:
        check.ok("stdout: RmlUi context ready banner", False)
    buffer = "\n".join(proc.lines_since(0).lines)
    check.ok("stdout: panels loaded line", re.search(r"\[AbcUi\] \d+ panel\(s\) loaded", buffer) is not None)
    check.ok("stdout: demo panel ready", "[AbcUi] demo panel ready" in buffer)
    check.ok("stdout: no font/asset warnings",
             "no system font loaded" not in buffer and "missing" not in buffer)

    rcon = RconConnection(host, port, "", protocol="goldsrc-udp")
    rcon.open()
    check.ok("RCON version probe", "Protocol version" in rcon.command("version"))

    listing = rcon.command("mh_pluginlist")
    # Renderer may load as Renderer_AVX2.dll (MetaHook picks the CPU variant).
    check.ok("mh_pluginlist shows Renderer + AbcEssential + AbcUi (v4)",
             all(re.search(rf"v4\|\s*{name}\.dll", listing) for name in ("Renderer(?:_AVX2)?", "AbcEssential", "AbcUi")),
             listing.replace("\n", " / "))

    # Menu screenshot first (clean shot, debugger off by default), then the
    # abc_ui_debugger toggle: the echo is async -> assert on the stdout mirror.
    before = {os.path.basename(p) for p in glob.glob(os.path.join(shots_dir, "*"))} if os.path.isdir(shots_dir) else set()
    shot = take_screenshot(proc, shots_dir, before)
    check.ok("menu screenshot written", shot is not None)
    if shot:
        try:
            check.ok("menu screenshot shows demo document pixels", demo_pixels_present(shot), os.path.basename(shot))
        except Exception as exc:
            check.ok("menu screenshot shows demo document pixels", False, f"read failed: {exc}")

    # Input bridge reachability, BEFORE the debugger toggles (Rml dispatches
    # keys along the focus chain, and the debugger document steals focus when
    # shown - refocus arbitration is batch A/C, plan 05 §3.5). Mouse position
    # is polled from the VGUI cursor, visible at the menu; injected KEYS are
    # swallowed by the GameUI menu (verified 2026-10-10: same trapkey fires
    # HUD_Key_Event + Rml keydown in-map), so the keydown check runs in-map.
    cursor = proc.next_cursor
    rcon.command("cli.mousemove absolute 300 300")
    proc.send_stdin("cli.mousemove absolute 320 300")
    try:
        proc.wait_for(re.compile(r"\[AbcUi\] demo mouse \d+,\d+"), 10, since=cursor)
        check.ok("input bridge: mousemove reaches Rml (menu)", True)
    except TimeoutError:
        check.ok("input bridge: mousemove reaches Rml (menu)", False)

    cursor = proc.next_cursor
    proc.send_stdin("abc_ui_debugger")
    try:
        proc.wait_for(re.compile(r"\[AbcUi\] debugger enabled"), 10, since=cursor)
        check.ok("abc_ui_debugger toggles on", True)
    except TimeoutError:
        check.ok("abc_ui_debugger toggles on", False)
    proc.send_stdin("abc_ui_debugger")
    try:
        proc.wait_for(re.compile(r"\[AbcUi\] debugger disabled"), 8, since=cursor)
        check.ok("abc_ui_debugger toggles off", True)
    except TimeoutError:
        check.ok("abc_ui_debugger toggles off", False)

    if not args.skip_map:
        proc.send_stdin(f"map {args.map}")
        time.sleep(28)
        check.ok(f"in-map smoke: game alive after map {args.map}", proc.alive)

        # Keydown reachability in the real render loop (see menu-note above):
        # engine key injection -> HUD_Key_Event -> KeyMap -> Rml keydown.
        cursor = proc.next_cursor
        rcon.command("cli.trapkey t 1")
        time.sleep(0.2)
        rcon.command("cli.trapkey t 0")
        try:
            proc.wait_for(re.compile(r"\[AbcUi\] demo key T\b"), 10, since=cursor)
            check.ok("input bridge: keydown reaches Rml (in-map)", True)
        except TimeoutError:
            check.ok("input bridge: keydown reaches Rml (in-map)", False)

        before = {os.path.basename(p) for p in glob.glob(os.path.join(shots_dir, "*"))}
        shot = take_screenshot(proc, shots_dir, before)
        check.ok("in-map screenshot written", shot is not None)
        if shot:
            try:
                check.ok("in-map screenshot shows demo document pixels", demo_pixels_present(shot), os.path.basename(shot))
            except Exception as exc:
                check.ok("in-map screenshot shows demo document pixels", False, f"read failed: {exc}")

    errors_size_after = os.path.getsize(errors_log) if os.path.isfile(errors_log) else 0
    check.ok("errors.log quiet", errors_size_after == errors_size_before,
             f"{errors_size_before} -> {errors_size_after}")

    rcon.command("quit")
    time.sleep(6)
    check.ok("clean quit via RCON", not proc.alive)

    print(f"\n[test] {check.count - len(check.failures)}/{check.count} passed")
    if check.failures:
        print("[test] FAILED:", ", ".join(check.failures))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
