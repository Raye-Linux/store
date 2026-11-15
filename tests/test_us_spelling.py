# SPDX-License-Identifier: GPL-3.0-or-later
"""What the Store shows in English is American English: license, canceled,
color. Internal words (the "cancelled" history result stored on disk, the
Cancelled enum) are not shown to anyone and stay as they are."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
british = re.compile(r"\b(licen[cs]e?s?|colou?rs?|cancell(?:ed|ing)|favourites?|centres?|behaviours?|programmes?|catalogues?)\b", re.I)
only_british = re.compile(r"licence|colour|cancell|favourite|centre|behaviour|programme|catalogue", re.I)

bad = []
# 1. every msgid of the French catalogue (the English the person reads)
for n, line in enumerate((root / "po/fr/raye-store.po").read_text(encoding="utf-8").splitlines(), 1):
    if line.startswith(("msgid ", "msgctxt ")) and only_british.search(line):
        bad.append(f"po:{n}: {line}")
# 2. every literal inside an i18n call
call = re.compile(r"\bi18n[cp]{0,2}\s*\(([^;]*?)\)\s*[;,)}:?]|\bi18n[cp]{0,2}\s*\(([^\n]*)")
for path in list((root / "src").rglob("*.qml")) + list((root / "src").rglob("*.cpp")) + list((root / "src").rglob("*.js")):
    text = path.read_text(encoding="utf-8")
    for m in call.finditer(text):
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(0)):
            if only_british.search(lit):
                bad.append(f"{path.relative_to(root)}: {lit}")
assert (root / "po/fr/raye-store.po").read_text(encoding="utf-8").count("msgid ") > 100, "the check does not read the catalogue"
if bad:
    print("British spelling shown to people:\n" + "\n".join(bad))
    sys.exit(1)
print("US spelling: ok")
