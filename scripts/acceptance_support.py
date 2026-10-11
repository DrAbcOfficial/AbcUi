"""Offline-testable acceptance checks and owned-session cleanup."""
import glob
import os


class Check:
    def __init__(self):
        self.failures = []
        self.skipped = []
        self.count = 0

    def ok(self, name, condition, detail=""):
        self.count += 1
        print(f"[{'PASS' if condition else 'FAIL'}] {name}" + (f" ({detail})" if detail else ""))
        if not condition:
            self.failures.append(name)
        return bool(condition)

    def skip(self, name, reason):
        self.skipped.append(name)
        print(f"[SKIP] {name} ({reason})")


def clean_exit(proc, stop):
    return not proc.alive and stop.exit_code == 0 and not stop.killed


def latest_screenshot(directory, before_names):
    candidates = [path for path in glob.glob(os.path.join(directory, "*"))
                  if os.path.isfile(path)
                  and path.lower().endswith((".tga", ".bmp", ".png", ".jpg", ".jpeg"))
                  and os.path.basename(path) not in before_names]
    return max(candidates, key=os.path.getmtime) if candidates else None


def cleanup_session(proc, rcon):
    try:
        if proc.alive:
            proc.stop(quit_command=(lambda: rcon.command("quit")) if rcon else None)
    finally:
        if rcon is not None:
            rcon.close()
