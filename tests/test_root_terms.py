# SPDX-License-Identifier: GPL-3.0-or-later
"""What the Store shows says root and sudo, never administrator."""
import pathlib
import re
import sys
import unittest

REPO = pathlib.Path(__file__).resolve().parent.parent
OLD = re.compile(r"administrat|\badmin\b", re.I)
CALL = re.compile(r'\bi18n[a-z]*\((?:"[^"]*",\s*)?"([^"]*)"')


def files(folder, *patterns):
    for pattern in patterns:
        yield from (p for p in sorted((REPO / folder).rglob(pattern)) if p.is_file())


class RootTerms(unittest.TestCase):
    def test_i18n_strings(self):
        bad = [f"{p.relative_to(REPO)}: {s}" for p in files("src", "*.qml", "*.js", "*.cpp")
               for s in CALL.findall(p.read_text(encoding="utf-8", errors="replace")) if OLD.search(s)]
        self.assertEqual(bad, [])

    def test_translations(self):
        bad = [f"{p.relative_to(REPO)}: {s}" for p in files("po", "*.po")
               for s in re.findall(r'^msg(?:id|str) "(.*)"$', p.read_text(encoding="utf-8"), re.M)
               if OLD.search(s)]
        self.assertEqual(bad, [])

    def test_polkit_messages(self):
        bad = [line.strip() for p in files("data", "*.policy*")
               for line in p.read_text(encoding="utf-8").splitlines()
               if "<message" in line and OLD.search(line)]
        self.assertEqual(bad, [])

    def test_not_allowed_says_root(self):
        source = (REPO / "src/installer.cpp").read_text(encoding="utf-8")
        self.assertIn('"You need root access to do this."', source)


if __name__ == "__main__":
    sys.exit(not unittest.main(exit=False).result.wasSuccessful())
