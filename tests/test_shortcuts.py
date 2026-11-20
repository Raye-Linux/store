# SPDX-License-Identifier: GPL-3.0-or-later
"""The Store's keys and its scroll bar, read from the QML.

* Every key the "Keyboard shortcuts" list shows does something: it is a
  Shortcut in Main.qml, or the item that has the focus takes it (Esc in
  the search box and the windows, Enter on a card).
* No key is bound twice: Qt calls two enabled Shortcuts on one key
  ambiguous and runs neither (Backspace did nothing because of that).
* The rail's tooltips name the keys that really go there.
* The scroll bar of every page is seen when the page can scroll, not
  only while it moves.

What this cannot see: whether a key reaches the window at all in a
running Store (a focused control may eat it). That is tried by hand.
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
QML = ROOT / "src" / "qml"
MAIN = (QML / "Main.qml").read_text(encoding="utf-8")
DIALOG = (QML / "ui" / "ShortcutsDialog.qml").read_text(encoding="utf-8")

# keys a focused item handles itself, and where
HANDLED_BY_FOCUS = {
    "Esc": [(QML / "ui" / "SearchBox.qml", "Keys.onEscapePressed"), (QML / "ui" / "ConfirmDialog.qml", "CloseOnEscape"),
            (QML / "ui" / "ShortcutsDialog.qml", "CloseOnEscape")],
    "Enter": [(QML / "ui" / "AppCard.qml", "Keys.onReturnPressed")],
}


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0] if not line.strip().startswith("//") else "" for line in text.splitlines())


def blocks(text, name):
    """The bodies of every `name { ... }` item, braces matched."""
    out = []
    for m in re.finditer(r"\b%s\s*\{" % name, text):
        depth, i = 1, m.end()
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        out.append(text[m.end():i - 1])
    return out


def shortcuts():
    """[(sequences, action)] of the Shortcut items of Main.qml."""
    out = []
    for body in blocks(strip_comments(MAIN), "Shortcut"):
        seqs = re.findall(r'"([^"]+)"', re.search(r"sequences?\s*:\s*(\[[^\]]*\]|\"[^\"]*\")", body).group(1))
        action = re.search(r"onActivated\s*:\s*([^\n;]*)", body).group(1).strip()
        out.append((seqs, action))
    return out


def listed_keys():
    """The key sequences the dialog lists (its `keys:` arrays)."""
    keys = []
    for m in re.finditer(r"keys:\s*\[([^\]]*)\]", DIALOG):
        keys += re.findall(r'"([^"]+)"', m.group(1))
    return keys


class Keys(unittest.TestCase):
    def test_no_key_is_bound_twice(self):
        seen = {}
        for seqs, action in shortcuts():
            for seq in seqs:
                self.assertNotIn(seq, seen, f"{seq} runs both {seen.get(seq)} and {action}: Qt runs neither")
                seen[seq] = action

    def test_every_listed_key_does_something(self):
        bound = {seq for seqs, _ in shortcuts() for seq in seqs}
        keys = listed_keys()
        self.assertGreaterEqual(len(keys), 15)
        for key in keys:
            with self.subTest(key):
                if key in HANDLED_BY_FOCUS:
                    for path, needle in HANDLED_BY_FOCUS[key]:
                        self.assertIn(needle, path.read_text(encoding="utf-8"), f"{key}: {path.name}")
                else:
                    self.assertIn(key, bound)

    def test_the_owner_asked_for_these(self):
        actions = {seq: action for seqs, action in shortcuts() for seq in seqs}
        for seq, does in (("Ctrl+F", "searchBox.forceActiveFocus"), ("/", "searchBox.forceActiveFocus"),
                          ("Alt+Left", "window.back"), ("Backspace", "window.back"), ("Alt+Right", "window.forward"),
                          ("F5", "window.refresh"), ("Ctrl+R", "window.refresh"),
                          ("Ctrl+1", 'goSection("home")'), ("Ctrl+2", 'goSection("apps")'),
                          ("Ctrl+3", 'goSection("games")'), ("Ctrl+4", 'goSection("library")'),
                          ("Ctrl+,", 'goSection("settings")'), ("F1", "shortcutsDialog.open"),
                          ("Ctrl+/", "shortcutsDialog.open"), ("Ctrl+W", "window.close"), ("Ctrl+Q", "window.close")):
            with self.subTest(seq):
                self.assertIn(does, actions.get(seq, ""))

    def test_kirigamis_own_back_keys_are_turned_off(self):
        # the page stack of Kirigami.ApplicationWindow binds StandardKey.Back
        # and Forward (Alt+Left, Alt+Right in Plasma): with ours, two
        # Shortcuts on one key, and Qt ran neither (VM 130, 2026-11-20)
        text = strip_comments(MAIN)
        self.assertIn("Kirigami.ApplicationWindow", text)
        body = re.search(r"function quietPageStackKeys\(\)\s*\{(.*?)\n    \}", text, re.S)
        self.assertTrue(body, "no quietPageStackKeys()")
        self.assertIn("window.pageStack.resources", body.group(1))
        self.assertIn("enabled = false", body.group(1))
        start = re.search(r"Component\.onCompleted:\s*\{(.*?)\n    \}", text, re.S)
        self.assertIn("quietPageStackKeys()", start.group(1))

    def test_the_page_takes_the_keys_when_the_window_opens(self):
        # opened from the taskbar, no item held the keys: Page Down and
        # the arrows did nothing until a click in the page
        text = strip_comments(MAIN)
        self.assertRegex(text, r"onActiveChanged:\s*if\s*\(active\)\s*Qt\.callLater\(focusPage\)")
        body = re.search(r"function focusPage\(\)\s*\{(.*?)\n    \}", text, re.S)
        self.assertTrue(body, "no focusPage()")
        self.assertIn("pages.current()", body.group(1))
        self.assertIn("forceActiveFocus()", body.group(1))
        # a field or a dialog that has the keys keeps them
        self.assertIn("within(f, mainRow)", body.group(1))
        self.assertIn("within(f, window.overlay)", body.group(1))

    def test_keys_that_type_wait_for_no_field(self):
        text = strip_comments(MAIN)
        for seq in ("/", "Backspace"):
            with self.subTest(seq):
                m = re.search(r'Shortcut\s*\{\s*sequence:\s*"%s";\s*enabled:\s*!window\.typing' % re.escape(seq), text)
                self.assertTrue(m, f"{seq} would be taken from the search box")

    def test_rail_tooltips_name_the_right_keys(self):
        actions = {seq: action for seqs, action in shortcuts() for seq in seqs}
        text = strip_comments(MAIN)
        found = re.findall(r'shortcut:\s*"([^"]+)"\s*\n\s*onClicked:\s*window\.goSection\("(\w+)"\)', text)
        self.assertEqual(len(found), 5)
        for key, section in found:
            with self.subTest(section):
                self.assertIn(f'goSection("{section}")', actions.get(key, ""))


class ScrollBar(unittest.TestCase):
    def test_the_bar_stays_seen_at_rest(self):
        bar = (QML / "ui" / "ThinScrollBar.qml").read_text(encoding="utf-8")
        rest = re.search(r"restOpacity:\s*([0-9.]+)", bar)
        self.assertTrue(rest, "the thumb fades to nothing when the page stops")
        self.assertGreaterEqual(float(rest.group(1)), 0.4)
        self.assertNotRegex(strip_comments(bar), r"opacity:\s*bar\.active[^\n]*\?\s*1\s*:\s*0\b")

    def test_every_page_has_the_bar(self):
        for name in ("PageScroll.qml", "AppPage.qml", "ListPage.qml"):
            path = QML / name if (QML / name).exists() else QML / "ui" / name
            with self.subTest(name):
                self.assertRegex(path.read_text(encoding="utf-8"), r"ScrollBar\.vertical:\s*ThinScrollBar")
        for name in ("HomePage.qml", "BrowsePage.qml", "LibraryPage.qml", "SettingsPage.qml"):
            with self.subTest(name):
                self.assertIn("PageScroll {", (QML / name).read_text(encoding="utf-8"))

    def test_the_keys_scroll_the_page(self):
        text = strip_comments(MAIN)
        self.assertIn("Keys.onPressed: event => window.scrollKey(event)", text)
        self.assertIn("KeyNav.pageTakes", text)
        self.assertIn("KeyNav.scrollTo", text)
        grid = strip_comments((QML / "ListPage.qml").read_text(encoding="utf-8"))
        self.assertIn("KeyNav.gridMove", grid)
        self.assertIn("positionViewAtIndex", grid)


if __name__ == "__main__":
    sys.exit(unittest.main())
