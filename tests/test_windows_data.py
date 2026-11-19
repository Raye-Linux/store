# SPDX-License-Identifier: GPL-3.0-or-later
"""windows-programs.json: every program installable from its maker's
installer, in both languages, with an icon, under a file name its profile
recognizes; and no word of the Store gives the Windows layer away.

The profile checks need the iso and wine repositories next to this one,
as they are for an image build."""
import fnmatch
import json
import pathlib
import re
import struct
import sys
import unittest
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "data" / "windows-programs.json"
ICONS = ROOT / "data" / "icons"
PROFILE_DIRS = [ROOT.parent / "iso" / "packages" / "raye-base" / "release" / "windows" / "profiles",
                ROOT.parent / "wine" / "release" / "windows" / "profiles"]
TITLES = ROOT.parent / "wine" / "release" / "windows" / "titles"
VISIBLE = [ROOT / "data", ROOT / "src" / "qml", ROOT / "po", ROOT / "src" / "raye-store-windows"]


def programs():
    return json.loads(DATA.read_text(encoding="utf-8"))["programs"]


def profiles():
    """{name: {key: value}}; raye-wine's file replaces raye-base's of the same name."""
    out = {}
    for d in PROFILE_DIRS:
        for p in sorted(d.glob("*.profile")) if d.is_dir() else []:
            keys = {}
            for line in p.read_text(encoding="utf-8").splitlines():
                m = re.match(r'^([a-z]+)="(.*)"$', line)
                if m:
                    keys[m.group(1)] = m.group(2)
            out[p.stem] = keys
    return out


def offered(p):
    """an entry the Store installs from the maker's installer"""
    return "installer" in p


def file_of(p):
    """the name the download carries: the Store's, or the maker's page's"""
    return p["installer"]["file"] if offered(p) else p["download"]["file"]


def png_size(path):
    head = path.read_bytes()[:24]
    return struct.unpack(">II", head[16:24])


class TestWindowsData(unittest.TestCase):
    def test_enough_programs(self):
        offered = [p for p in programs() if "installer" in p]
        self.assertGreaterEqual(len(offered), 5)
        self.assertGreaterEqual(len([p for p in programs() if "native" in p]), 5)

    def test_installers_are_pinned(self):
        for p in programs():
            if not offered(p):
                continue
            inst = p["installer"]
            with self.subTest(p["profile"]):
                self.assertTrue(inst["url"].startswith("https://"))
                self.assertRegex(inst["sha256"], r"^[0-9a-f]{64}$")
                self.assertRegex(inst["file"], r"^[A-Za-z0-9][A-Za-z0-9._+-]*\.(exe|msi)$")
                # the download keeps its maker's name; a space in it becomes "-"
                name = urllib.parse.unquote(inst["url"].rsplit("/", 1)[1]).replace(" ", "-")
                self.assertTrue(name.endswith(inst["file"]), name)
                self.assertGreater(inst["size"], 0)

    def test_download_pages(self):
        # a maker who gives its setup only to signed-in accounts: the Store
        # opens its page, and the file it hands out must still find its profile
        pages = [p for p in programs() if "download" in p]
        self.assertTrue(pages)
        for p in pages:
            with self.subTest(p["profile"]):
                self.assertNotIn("installer", p)
                self.assertTrue(p["download"]["page"].startswith("https://"))
                self.assertRegex(p["download"]["file"], r"^[A-Za-z0-9][A-Za-z0-9._+-]*\.(exe|msi)$")
                self.assertTrue(p.get("developer"))

    def test_both_languages(self):
        for p in programs():
            if "native" in p:
                continue
            for key in ("summary", "description"):
                with self.subTest(p["profile"], key=key):
                    self.assertTrue(p[key]["en"].strip())
                    self.assertTrue(p[key]["fr"].strip())
                    self.assertNotEqual(p[key]["en"], p[key]["fr"])

    def test_icons(self):
        for p in programs():
            if "native" in p:
                continue
            path = ICONS / p["icon"]
            with self.subTest(p["profile"]):
                self.assertTrue(path.is_file(), path)
                self.assertGreaterEqual(min(png_size(path)), 64)
        self.assertTrue((ICONS / "windows" / "game.svg").is_file())

    def test_setups_say_how_they_run(self):
        # a quiet setup accepts its terms for the person: the Store shows
        # them first. Every setup says where it writes and how much, so
        # the bar can move while it runs; the main program names the entry
        # that gets the Store's picture when its own icons are small
        switch = re.compile(r"^[/-][A-Za-z][A-Za-z0-9_-]*(=[A-Za-z0-9._-]+)?$")
        for p in programs():
            if not offered(p):
                continue
            inst = p["installer"]
            with self.subTest(p["profile"]):
                for word in inst.get("silent", []):
                    self.assertRegex(word, switch)
                if inst.get("silent"):
                    self.assertTrue(p.get("license_url", "").startswith("https://"))
                folders = inst["folder"] if isinstance(inst["folder"], list) else [inst["folder"]]
                for folder in folders:
                    self.assertNotIn("..", folder)
                    self.assertFalse(folder.startswith("/"))
                self.assertGreater(inst["installed_size"], 0)
                self.assertRegex(p["main"], r"^[A-Za-z0-9][A-Za-z0-9 ._+-]*\.exe$")
                self.assertGreaterEqual(min(png_size(ICONS / p["icon"])), 128)

    def test_creative_programs(self):
        by = {p["profile"]: p for p in programs()}
        self.assertIn("installer", by["fl-studio"])
        # Affinity's setup has no quiet switch: it opens its own window
        self.assertEqual(by["affinity"]["installer"].get("silent", []), [])
        self.assertEqual(by["affinity"]["main"], "Affinity.exe")
        self.assertTrue(by["tiktok-live-studio"]["download"]["page"].startswith("https://www.tiktok.com/"))

    def test_no_layer_name(self):
        hits = []
        for base in VISIBLE:
            files = [base] if base.is_file() else [f for f in base.rglob("*") if f.is_file() and f.suffix not in (".png", ".tsv")]
            for f in files:
                for n, line in enumerate(f.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                    if re.search(r"\bwine\b", line, re.I):
                        hits.append("%s:%d" % (f.relative_to(ROOT), n))
        self.assertEqual(hits, [])

    def test_profiles_recognize_the_downloads(self):
        known = profiles()
        if not known:
            print("profiles not next to this repository: not checked here", file=sys.stderr)
            return
        order = sorted(known)
        for p in programs():
            with self.subTest(p["profile"]):
                self.assertIn(p["profile"], known)
                prof = known[p["profile"]]
                if "native" in p:
                    self.assertEqual(p["native"], prof.get("native"))
                    continue
                self.assertFalse(prof.get("native"), "a program with a native version is offered as Windows")
                lower = file_of(p).lower()
                # the runner takes the first profile, by file name, whose pattern fits
                first = next((name for name in order
                              if any(fnmatch.fnmatchcase(lower, pat) for pat in known[name].get("match", "").split())), None)
                self.assertEqual(first, p["profile"])

    def test_no_blocked_title_offered(self):
        if not TITLES.is_dir():
            print("titles not next to this repository: not checked here", file=sys.stderr)
            return
        text = "\n".join(f.read_text(encoding="utf-8") for f in sorted(TITLES.glob("*.titles")))
        blocked = set(re.findall(r"^\[([^\]]+)\]\n(?:(?!\[).*\n)*?state\s*=\s*blocked", text, re.M))
        self.assertGreaterEqual(len(blocked), 53)
        known = profiles()
        for p in programs():
            if "native" in p:
                continue
            self.assertNotIn(known.get(p["profile"], {}).get("title", ""), blocked)


if __name__ == "__main__":
    unittest.main()
