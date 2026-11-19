# SPDX-License-Identifier: GPL-3.0-or-later
"""extras.json and windows-alternatives.json: every download pinned, in
both languages, with its icon; every Windows name pointing at real ids."""
import json
import pathlib
import re
import struct
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
APP_ID = re.compile(r"^[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)+$")


def extras():
    return json.loads((DATA / "extras.json").read_text(encoding="utf-8"))["apps"]


def alternatives():
    return json.loads((DATA / "windows-alternatives.json").read_text(encoding="utf-8"))["alternatives"]


def png_size(path):
    head = path.read_bytes()[:24]
    assert head[:8] == b"\x89PNG\r\n\x1a\n", path
    return struct.unpack(">II", head[16:24])


class TestExtras(unittest.TestCase):
    def test_enough(self):
        self.assertGreaterEqual(len(extras()), 4)

    def test_downloads_are_pinned(self):
        for app in extras():
            with self.subTest(app["id"]):
                self.assertTrue(app["url"].startswith("https://"))
                self.assertTrue(app["url"].endswith(".deb"))
                self.assertRegex(app["sha256"], r"^[0-9a-f]{64}$")
                self.assertRegex(app["package"], r"^[a-z0-9][a-z0-9+.-]+$")
                # the file of a version, never a "latest" link that moves
                self.assertNotIn("latest", app["url"])
                self.assertIn(app["version"], app["url"])

    def test_one_entry_per_package(self):
        packages = [a["package"] for a in extras()]
        self.assertEqual(len(packages), len(set(packages)))
        ids = [a["id"] for a in extras()]
        self.assertEqual(len(ids), len(set(ids)))

    def test_icons(self):
        for app in extras():
            with self.subTest(app["id"]):
                self.assertTrue(app["icon"].startswith("/usr/share/raye-store/icons/"))
                path = DATA / "icons" / app["icon"].removeprefix("/usr/share/raye-store/icons/")
                self.assertTrue(path.is_file(), path)
                self.assertGreaterEqual(min(png_size(path)), 128)

    def test_says_what_it_is(self):
        for app in extras():
            with self.subTest(app["id"]):
                self.assertRegex(app["id"], APP_ID)
                self.assertTrue(app["name"].strip())
                self.assertTrue(app["categories"])
                if "desktop" in app:
                    self.assertRegex(app["desktop"], r"^[A-Za-z0-9][A-Za-z0-9._-]*\.desktop$")
                if "homepage" in app:
                    self.assertTrue(app["homepage"].startswith("https://"))

    def test_both_languages(self):
        # ArtCraft came before the file had French
        for app in extras():
            if isinstance(app["summary"], str):
                continue
            for key in ("summary", "description"):
                with self.subTest(app["id"], key=key):
                    self.assertTrue(app[key]["en"].strip())
                    self.assertTrue(app[key]["fr"].strip())
                    self.assertNotEqual(app[key]["en"], app[key]["fr"])


class TestAlternatives(unittest.TestCase):
    def test_enough(self):
        self.assertGreaterEqual(len(alternatives()), 50)

    def test_entries(self):
        names = set()
        for alt in alternatives():
            with self.subTest(alt.get("windows")):
                self.assertTrue(alt["windows"].strip())
                self.assertNotIn(alt["windows"].lower(), names)
                names.add(alt["windows"].lower())
                self.assertTrue(alt["apps"])
                self.assertEqual(len(alt["apps"]), len(set(alt["apps"])))
                for app in alt["apps"]:
                    self.assertRegex(app, APP_ID)
                for word in alt.get("words", []):
                    self.assertTrue(word.strip())

    def test_raye_entries_exist(self):
        # an id that is not on Flathub must be one Raye offers itself
        offered = {a["id"] for a in extras()}
        for alt in alternatives():
            for app in alt["apps"]:
                if app.startswith(("io.balena.", "com.teamviewer.", "net.mullvad.")):
                    self.assertIn(app, offered)

    def test_game_stores_lead_to_launchers(self):
        found = {alt["windows"]: alt["apps"] for alt in alternatives()}
        self.assertEqual(found["GOG Galaxy"][0], "com.heroicgameslauncher.hgl")
        self.assertEqual(found["Amazon Games"][0], "com.heroicgameslauncher.hgl")
        for store in ("Battle.net", "EA app", "Ubisoft Connect"):
            self.assertEqual(found[store][0], "net.lutris.Lutris")

    def test_dj_software_leads_to_mixxx(self):
        found = {alt["windows"]: alt for alt in alternatives()}
        for name in ("rekordbox", "Serato DJ", "Virtual DJ", "Traktor"):
            with self.subTest(name):
                self.assertEqual(found[name]["apps"][0], "org.mixxx.Mixxx")
        words = {w.lower() for name in found for w in [name] + found[name].get("words", [])
                 if found[name]["apps"][0] == "org.mixxx.Mixxx"}
        for typed in ("rekordbox", "serato", "virtual dj", "virtualdj", "traktor"):
            self.assertIn(typed, words)


if __name__ == "__main__":
    unittest.main()
