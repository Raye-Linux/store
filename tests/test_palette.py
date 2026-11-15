# SPDX-License-Identifier: GPL-3.0-or-later
"""The Store's greys are Raye Night and Day's, not the violet ones of 0.2.

Theme.qml mirrors theme/tokens/raye-tokens.json. A colour copied by hand
into a page outlives the next palette change, so the old violet greys are
looked for everywhere under src/qml.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
QML = sorted((ROOT / "src/qml").rglob("*.qml"))
THEME = (ROOT / "src/qml/ui/Theme.qml").read_text()

# bg.base, text and secondary text of the 0.2 palette, Night then Day
OLD = ["130e1f", "f3eaff", "b9a8d6", "f6f3fb", "1a1028", "574a6d"]


def color(name):
    m = re.search(r"readonly property color %s:\s*(.+)" % name, THEME)
    return m.group(1).strip()


class Palette(unittest.TestCase):
    def test_no_old_greys_in_qml(self):
        hits = []
        for path in QML:
            for n, line in enumerate(path.read_text().splitlines(), 1):
                for hexv in OLD:
                    if re.search(r"#(?:[0-9a-f]{2})?%s\b" % hexv, line, re.I):
                        hits.append("%s:%d %s" % (path.relative_to(ROOT), n, line.strip()))
        self.assertEqual(hits, [])

    def test_greys_match_the_tokens(self):
        self.assertEqual(color("backdropSolid"), 'night ? "#141318" : "#f5f4f7"')
        self.assertEqual(color("text"), 'night ? "#f1f0f4" : "#17151c"')
        self.assertEqual(color("textSecondary"), 'night ? "#b5b3bc" : "#54525b"')

    def test_glow_is_the_accent(self):
        self.assertEqual(color("glow"), "Qt.alpha(accent, night ? 0.22 : 0.14)")
        self.assertEqual(color("accentGlowEnd"), "accent")
        self.assertNotIn("brandMagenta", color("subtleSelected"))

    def test_magenta_only_named(self):
        uses = [p.name for p in QML if "c040ff" in p.read_text().lower()]
        self.assertEqual(uses, ["Theme.qml"])


if __name__ == "__main__":
    unittest.main()
