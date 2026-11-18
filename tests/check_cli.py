#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What Settings > Apps starts: raye-store --uninstall, the real program.

Settings ran it for months while the program did not know the option: it
said "Unknown option" and every Uninstall failed. So this runs the binary.
Only names that cannot be installed are used, nothing is ever removed.
"""
import json
import subprocess
import sys

store = sys.argv[1]


def run(*arguments):
    return subprocess.run([store, *arguments], capture_output=True, text=True, timeout=60)


def check(ok, text):
    if not ok:
        sys.exit(f"FAIL: {text}")
    print(f"ok: {text}")


help_text = run("--help").stdout
# the .deb window too: its launcher passes --install-file
for option in ("--open", "--uninstall", "--install-file"):
    check(option in help_text, f"--help lists {option}")

for target in ("org.raye.not.installed.anywhere", "../../etc/passwd", "app/../x/y"):
    done = run("--uninstall", target)
    check(done.returncode == 1, f"--uninstall {target} exits 1 (got {done.returncode}: {done.stderr[-200:]})")
    lines = [json.loads(line) for line in done.stdout.splitlines()]
    check(lines and lines[-1]["state"] == "failed", f"--uninstall {target} ends on state failed: {done.stdout!r}")
    check(lines[-1].get("message"), f"--uninstall {target} says why")
