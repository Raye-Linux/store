"""The status line of a Windows install must never be cut off.

The first line keeps the plain "Installing…" every install uses; the hint
that the program finishes in its own setup window goes on a line of its
own that wraps instead of eliding.
"""
import pathlib
import re
import unittest

QML = pathlib.Path(__file__).resolve().parent.parent / "src" / "qml" / "JobStatus.qml"


class InstallLabel(unittest.TestCase):
    def setUp(self):
        self.qml = QML.read_text(encoding="utf-8")

    def test_no_long_text_on_the_eliding_line(self):
        # every string stateText() returns stays short
        body = self.qml.split("function stateText()", 1)[1].split("\n    }\n", 1)[0]
        for text in re.findall(r'i18n\("([^"]+)"', body):
            self.assertLessEqual(len(text), 24, text)

    def test_windows_hint_wraps(self):
        hint = self.qml.split('i18n("Finish in the setup window that opened.")', 1)
        self.assertEqual(len(hint), 2, "the Windows hint is missing")
        block = hint[1].split("}", 1)[0]
        self.assertIn("wrapMode: Text.WordWrap", block)
        self.assertNotIn("elide", block)
        self.assertIn('source === "windows"', block)
        # only when the setup really opened a window: a quiet one opens none
        self.assertIn("setupWindow === true", block)

    def test_install_shows_how_far(self):
        self.assertIn('i18n("Installing · %1%", status.percent)', self.qml)
        bar = self.qml.split("readonly property bool indeterminate:", 1)[1].split("\n\n", 1)[0]
        self.assertNotIn("jobState === 3", bar)


if __name__ == "__main__":
    unittest.main()
