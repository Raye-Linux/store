# SPDX-License-Identifier: GPL-3.0-or-later
"""Two versions live here and they must not be confused.

The release ("0.5 Beta") belongs to Raye Linux. The Store never writes it
down: whoever shows it reads os-release. ./VERSION records which release
this checkout is for and mirrors iso/VERSION, where the number is decided.

The Store's own version is the package's, taken from debian/changelog by
CMakeLists.txt, the way raye-settings does it. Spelling it in main.cpp is
how About came to say 0.1 while the project said 0.2.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
VERSION = (ROOT / "VERSION").read_text().strip()
CMAKE = (ROOT / "CMakeLists.txt").read_text()
MAIN = (ROOT / "src/main.cpp").read_text()

SKIP_DIRS = {".git", "tests", "data", "windows", "__pycache__", "po"}
SKIP_SUFFIXES = {".png", ".jpg", ".jpeg", ".svg", ".ico", ".lnk", ".exe", ".tsv",
                 ".gz", ".pyc", ".qm", ".json"}


def without_comments(text, suffix):
    """Comments go first, so a comment explaining an older version cannot
    fail a test whose job is to protect that documentation."""
    if suffix in (".qml", ".js", ".cpp", ".h"):
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        return re.sub(r"(?m)(^|\s)//.*$", "", text)
    if suffix == ".md":
        return ""
    return re.sub(r"(?m)(^|\s)#.*$", "", text)


class Release(unittest.TestCase):
    def test_version_is_the_beta(self):
        self.assertRegex(VERSION, r"^\d+(?:\.\d+)+ Beta(?: \d+)?$")

    def test_mirrors_the_iso_version(self):
        """iso/VERSION decides the number; this repo only follows it. The
        image is built with every repo side by side, so this compares for
        real there; a lone clone says so out loud instead of passing
        quietly."""
        source = ROOT.parent / "iso" / "VERSION"
        if not source.is_file():
            print(f"no sibling {source}: cross-check skipped, ./VERSION not compared")
            return
        self.assertEqual(source.read_text(), (ROOT / "VERSION").read_text())

    def test_markdown_names_only_this_release(self):
        """README.md explains the rule by quoting the number, so the number
        is in it. Checked, not skipped: documentation left on an older
        release is how people learn the wrong version."""
        for page in sorted(ROOT.rglob("*.md")):
            if ".git" in page.parts:
                continue
            for found in re.findall(r"\d+(?:\.\d+)+ B[eê]ta", page.read_text()):
                with self.subTest(file=str(page.relative_to(ROOT))):
                    self.assertEqual(found.replace("Bêta", "Beta"), VERSION)

    def test_the_store_never_spells_the_release_out(self):
        """It has no business claiming the release: os-release says it."""
        number = re.escape(VERSION.split()[0])
        spelt = re.compile(r"(?:Raye(?: Linux)?[ _-]+)?%s[ _-]+Beta" % number, re.I)
        for path in sorted(ROOT.rglob("*")):
            rel = path.relative_to(ROOT)
            if (not path.is_file() or rel.name == "VERSION"
                    or SKIP_DIRS & set(rel.parts) or path.suffix in SKIP_SUFFIXES):
                continue
            try:
                text = path.read_text()
            except (UnicodeDecodeError, OSError):
                continue
            for line in without_comments(text, path.suffix).splitlines():
                with self.subTest(file=str(rel)):
                    self.assertIsNone(spelt.search(line),
                                      f"{rel}: the release is spelt out: {line.strip()}")


class StoreOwnVersion(unittest.TestCase):
    def test_about_takes_the_package_version(self):
        about = re.search(r"KAboutData about\((.*?)\);", MAIN, re.S)
        self.assertIsNotNone(about, "main.cpp has no KAboutData")
        self.assertIn("QStringLiteral(RAYE_STORE_VERSION)", about.group(1))
        # no literal number in there: that is the bug this replaced
        self.assertNotRegex(about.group(1), r'QStringLiteral\("\d')

    def test_cmake_reads_the_changelog(self):
        block = re.search(r"set\(RAYE_STORE_VERSION \$\{PROJECT_VERSION\}\)(.*?)endif\(\)",
                          CMAKE, re.S)
        self.assertIsNotNone(block, "CMakeLists.txt does not derive RAYE_STORE_VERSION")
        self.assertIn("debian/changelog", block.group(1))
        # a short message: a failure that prints the whole file is unreadable
        define = 'RAYE_STORE_VERSION="${RAYE_STORE_VERSION}"'
        self.assertTrue(define in CMAKE,
                        f"CMakeLists.txt never defines {define} for raye-store")

    def test_project_version_matches_the_release_number(self):
        """The fallback when there is no changelog must not contradict the
        release: project 0.2 against a 0.1 Beta release is what happened."""
        found = re.search(r"project\(raye-store VERSION (\S+)", CMAKE)
        self.assertIsNotNone(found)
        self.assertEqual(found.group(1), VERSION.split()[0])

    def test_nothing_outside_the_changelog_spells_the_package_version(self):
        """debian/changelog is the one place the package version is decided,
        so it is the one place it may be written. A comment that quotes the
        current value -- CMakeLists.txt did -- is right the day it is typed
        and wrong at the next upload, and nothing fails when it goes stale.
        Same reasoning as the release number above, one level down."""
        series = re.escape(VERSION.split()[0])
        spelt = re.compile(r"%s~[A-Za-z0-9.+~-]+" % series)
        current = re.search(r"\((\S+)\)",
                            (ROOT / "debian/changelog").read_text()).group(1)
        for path in sorted(ROOT.rglob("*")):
            rel = path.relative_to(ROOT)
            if (not path.is_file() or ".git" in rel.parts
                    or rel.as_posix() == "debian/changelog"
                    or path.suffix in SKIP_SUFFIXES):
                continue
            try:
                text = path.read_text()
            except (UnicodeDecodeError, OSError):
                continue
            for found in spelt.findall(text):
                with self.subTest(file=str(rel)):
                    self.assertEqual(found, current,
                                     f"{rel} names {found}, the package is {current}")

    def test_changelog_stays_in_the_release_series(self):
        first = (ROOT / "debian/changelog").read_text().splitlines()[0]
        found = re.search(r"\((\S+)\)", first)
        self.assertIsNotNone(found, first)
        self.assertTrue(found.group(1).startswith(VERSION.split()[0]),
                        f"{found.group(1)} is not in the {VERSION} series")


if __name__ == "__main__":
    unittest.main()
