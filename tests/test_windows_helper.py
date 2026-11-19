# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for raye-store-windows, without network or a Windows program."""
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
import pathlib
import subprocess
import tempfile
import unittest
import urllib.request
import zipfile

HELPER = pathlib.Path(__file__).resolve().parent.parent / "src" / "raye-store-windows"
loader = importlib.machinery.SourceFileLoader("raye_store_windows", str(HELPER))
spec = importlib.util.spec_from_loader("raye_store_windows", loader)
rsw = importlib.util.module_from_spec(spec)
loader.exec_module(rsw)

PAYLOAD = b"MZ a pretend installer"
GOOD = hashlib.sha256(PAYLOAD).hexdigest()


class FakeResponse(io.BytesIO):
    def __init__(self, data):
        super().__init__(data)
        self.headers = {"Content-Length": str(len(data))}

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class FakeOpener:
    def __init__(self, data):
        self.data = data
        self.urls = []

    def open(self, request, timeout=0):
        self.urls.append(request)
        return FakeResponse(self.data)


def program(sha=GOOD, size=len(PAYLOAD), file="winrar-x64-723.exe"):
    return {"profile": "winrar", "name": "WinRAR",
            "installer": {"url": "https://example.org/" + file, "file": file, "size": size, "sha256": sha}}


def zipped(members, inner="Install_tool_1_0.exe", payload=PAYLOAD):
    """a maker's zip around its setup, and the list entry for it"""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, data in members.items():
            z.writestr(name, data)
    data = buf.getvalue()
    p = program(sha=hashlib.sha256(data).hexdigest(), size=len(data), file="Install_tool_1_0.zip")
    p["installer"]["unpack"] = {"file": inner, "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
    return p, data


class Runner:
    """Stands in for the runner and raye-windows: records what was run,
    and has the space hold a program once the setup ran."""

    def __init__(self, installs=True, rc=0):
        self.installs = installs
        self.rc = rc
        self.calls = []
        self.seen_file = None
        self.uninstalled = []

    def popen(self, argv, **kw):
        self.calls.append(argv)
        self.env = kw.get("env") or {}
        self.seen_file = (os.path.basename(argv[1]), open(argv[1], "rb").read())
        self.ran = True
        return Done(self.rc)

    def __call__(self, argv, **kw):
        self.calls.append(argv)
        if argv[1] == "list":
            ran = getattr(self, "ran", False)
            found = [{"key": "WinRAR archiver", "name": "WinRAR", "removable": True}] if ran and self.installs else []
            return subprocess.CompletedProcess(argv, 0, json.dumps(found), "")
        if argv[1] == "uninstall":
            self.uninstalled.append(argv[3])
            return subprocess.CompletedProcess(argv, 0, "{}", "")
        raise AssertionError(argv)


class Done:
    def __init__(self, rc):
        self.rc = rc

    def wait(self, timeout=None):
        return self.rc


class Slow:
    """a setup that takes a few ticks, writing as it goes"""

    def __init__(self, ticks, rc=0):
        self.ticks = ticks
        self.rc = rc

    def wait(self, timeout=None):
        if self.ticks:
            self.ticks -= 1
            raise subprocess.TimeoutExpired("setup", timeout)
        return self.rc


class Quiet:
    def __enter__(self):
        self.old = rsw.say
        rsw.say = lambda line: None

    def __exit__(self, *exc):
        rsw.say = self.old


class TestWindowsHelper(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        os.environ["XDG_CACHE_HOME"] = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_runs_the_checked_installer_under_its_own_name(self):
        run = Runner()
        with Quiet():
            rc = rsw.install("winrar", program(), run=run, popen=run.popen, opener=FakeOpener(PAYLOAD))
        self.assertEqual(rc, 0)
        self.assertEqual(run.seen_file, ("winrar-x64-723.exe", PAYLOAD))
        self.assertEqual(run.calls[-1], [rsw.TOOL, "list", "winrar", "--json"])

    def test_nothing_runs_when_the_hash_differs(self):
        run = Runner()
        with Quiet(), self.assertRaises(ValueError):
            rsw.install("winrar", program(size=len(PAYLOAD) + 1), run=run, popen=run.popen, opener=FakeOpener(PAYLOAD + b"!"))
        self.assertEqual(run.calls, [])

    def test_bigger_than_the_list_says(self):
        run = Runner()
        with Quiet(), self.assertRaises(ValueError):
            rsw.install("winrar", program(size=4), run=run, popen=run.popen, opener=FakeOpener(PAYLOAD))
        self.assertEqual(run.calls, [])

    def test_installer_is_not_kept(self):
        with Quiet():
            run = Runner()
            rsw.install("winrar", program(), run=run, popen=run.popen, opener=FakeOpener(PAYLOAD))
        left = [f for _, _, fs in os.walk(self.tmp.name) for f in fs]
        self.assertEqual(left, [])

    def test_setup_closed_without_installing(self):
        with Quiet():
            run = Runner(installs=False)
            rc = rsw.install("winrar", program(), run=run, popen=run.popen, opener=FakeOpener(PAYLOAD))
        self.assertEqual(rc, 1)

    def test_remove_uninstalls_what_the_space_holds(self):
        run = Runner()
        run.ran = True
        with Quiet():
            self.assertEqual(rsw.remove("winrar", run=run), 0)
        self.assertEqual(run.uninstalled, ["WinRAR archiver"])

    def test_catalog_entries(self):
        path = os.path.join(self.tmp.name, "w.json")
        with open(path, "w") as f:
            json.dump({"programs": [program(), {"profile": "steam", "name": "Steam", "native": "com.valvesoftware.Steam"},
                                    dict(program(file="../x.exe"), profile="bad"),
                                    dict(program(sha="12"), profile="short"),
                                    {"profile": "live", "name": "Live", "download": {"page": "https://example.org/get", "file": "live.exe"}}]}, f)
        self.assertEqual(rsw.entry_for("winrar", path)["name"], "WinRAR")
        for space, error in (("steam", ValueError), ("bad", ValueError), ("short", ValueError), ("bash", LookupError)):
            with self.assertRaises(error):
                rsw.entry_for(space, path)
        # its maker hands the setup out on its own page: nothing to download here
        with self.assertRaisesRegex(ValueError, "maker's own page"):
            rsw.entry_for("live", path)

    def test_progress_lines(self):
        lines = []
        dest = os.path.join(self.tmp.name, "x.exe")
        rsw.download(program()["installer"], dest, opener=FakeOpener(PAYLOAD), report=lines.append)
        self.assertIn("progress 100", lines)
        self.assertIn("bytes %d %d" % (len(PAYLOAD), len(PAYLOAD)), lines)

    def test_download_looks_like_a_browser(self):
        opener = FakeOpener(PAYLOAD)
        rsw.download(program()["installer"], os.path.join(self.tmp.name, "x.exe"), opener=opener, report=lambda _: None)
        request = opener.urls[0]
        self.assertEqual(request.full_url, "https://example.org/winrar-x64-723.exe")
        agent = request.get_header("User-agent") or ""
        self.assertTrue(agent.startswith("Mozilla/5.0"), agent)
        self.assertNotIn("Python", agent)
        # the agent follows the redirects (GitHub, the makers' mirrors)
        moved = rsw.HttpsOnly().redirect_request(request, None, 302, "Found", {}, "https://mirror.example.org/x.exe")
        self.assertEqual(moved.get_header("User-agent"), agent)

    def test_quiet_setup_gets_its_switches(self):
        run = Runner()
        p = program()
        p["installer"]["silent"] = ["/S"]
        lines = []
        old, rsw.say = rsw.say, lines.append
        try:
            self.assertEqual(rsw.install("winrar", p, run=run, popen=run.popen, opener=FakeOpener(PAYLOAD)), 0)
        finally:
            rsw.say = old
        setup = run.calls[0]
        self.assertEqual(setup[0], rsw.RUNNER)
        self.assertEqual(setup[2:], ["--args", "/S"])
        # what is left once it ends is stopped, so the registry is written
        self.assertEqual(run.env.get("RAYE_WINDOWS_SETTLE_END"), "stop")
        self.assertLessEqual(int(run.env["RAYE_WINDOWS_SETTLE"]), 5)
        self.assertIn("phase installing", lines)
        self.assertNotIn("phase setup", lines)
        self.assertEqual(lines[-1], "progress 100")

    def test_setup_without_switches_opens_its_window(self):
        run = Runner()
        lines = []
        old, rsw.say = rsw.say, lines.append
        try:
            rsw.install("winrar", program(), run=run, popen=run.popen, opener=FakeOpener(PAYLOAD))
        finally:
            rsw.say = old
        self.assertEqual(len(run.calls[0]), 2)
        self.assertIn("phase setup", lines)
        # a program a setup starts at the end is not stopped
        self.assertNotIn("RAYE_WINDOWS_SETTLE_END", run.env)

    def test_odd_switches_or_folders_are_refused(self):
        path = os.path.join(self.tmp.name, "w.json")
        for key, value in (("silent", ["/S; rm -rf ~"]), ("silent", "/S"), ("silent", ["/D=C:\\x"]),
                           ("folder", "../../.ssh"), ("folder", "/etc")):
            p = program()
            p["installer"][key] = value
            with open(path, "w") as f:
                json.dump({"programs": [p]}, f)
            with self.assertRaises(ValueError, msg=value):
                rsw.entry_for("winrar", path)
        p = program()
        p["installer"].update(silent=["/S", "/VERYSILENT", "/SP-", "/qn", "-silent"], folder="Program Files/WinRAR")
        with open(path, "w") as f:
            json.dump({"programs": [p]}, f)
        self.assertEqual(rsw.entry_for("winrar", path)["name"], "WinRAR")
        p["installer"]["folder"] = ["Program Files/Image-Line", "Program Files (x86)/Microsoft"]
        with open(path, "w") as f:
            json.dump({"programs": [p]}, f)
        self.assertEqual(rsw.entry_for("winrar", path)["name"], "WinRAR")

    def test_progress_while_the_setup_writes(self):
        written = iter([0, 300, 600, 600, 600, 600, 600, 600, 2000])
        clock = iter(range(100))
        lines = []
        rc = rsw.watch_setup(Slow(9), "/x", 1000, report=lines.append, tick=0,
                             clock=lambda: next(clock), measure=lambda _: next(written))
        self.assertEqual(rc, 0)
        shown = [int(l.split()[1]) for l in lines]
        self.assertEqual(shown, sorted(shown))      # never goes back
        self.assertIn(30, shown)
        self.assertIn(60, shown)
        self.assertGreater(shown[-2], 60)           # crept on while nothing moved
        self.assertLessEqual(shown[-2], 68)         # but not far
        self.assertEqual(shown[-1], 99)             # never says done before it is

    def test_progress_counts_every_folder(self):
        # FL Studio's setup puts a web view in a second folder at the end
        sizes = {"/a": 400, "/b": 300}
        lines = []
        rsw.watch_setup(Slow(1), ["/a", "/b"], 1000, report=lines.append, tick=0,
                        clock=lambda: 0, measure=sizes.get)
        self.assertEqual(lines, ["progress 70"])

    def test_folder_bytes(self):
        d = pathlib.Path(self.tmp.name, "drive_c", "Program Files", "X")
        (d / "sub").mkdir(parents=True)
        (d / "a.exe").write_bytes(b"x" * 100)
        (d / "sub" / "b.dll").write_bytes(b"y" * 50)
        self.assertEqual(rsw.folder_bytes(str(d)), 150)
        self.assertEqual(rsw.folder_bytes(str(d / "missing")), 0)

    def png(self, path, width):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\x89PNG\r\n\x1a\n" + b"\0\0\0\rIHDR" + width.to_bytes(4, "big") * 2 + b"rest")

    def entry(self, name, target):
        desktop = pathlib.Path(self.tmp.name, "apps", name + ".desktop")
        desktop.parent.mkdir(parents=True, exist_ok=True)
        desktop.write_text("[Desktop Entry]\nIcon=%s\nExec=raye-run-windows --space s --run "
                           "\"C:\\\\Program Files\\\\7-Zip\\\\%s\"\n" % (name, target))
        return str(desktop)

    def test_small_program_icons_get_the_lists_picture(self):
        icons = pathlib.Path(self.tmp.name, "hicolor")
        picture = pathlib.Path(self.tmp.name, "7-zip.png")
        self.png(picture, 128)
        for size in (48, 32):
            self.png(icons / ("%dx%d" % (size, size)) / "apps" / "raye-win-7-zip-fm.png", size)
            self.png(icons / ("%dx%d" % (size, size)) / "apps" / "raye-win-7-zip-help.png", size)
        found = [{"desktopFiles": [self.entry("raye-win-7-zip-fm", "7zFM.exe"),
                                   self.entry("raye-win-7-zip-help", "7-zip.chm")]}]
        program = {"main": "7zFM.exe", "icon": str(picture)}
        placed = rsw.sharpen_icons(program, found, icons=str(icons))
        self.assertEqual(placed, [str(icons / "128x128" / "apps" / "raye-win-7-zip-fm.png")])
        self.assertEqual(rsw.png_size(placed[0]), 128)
        # a program whose own icons are big enough keeps them
        self.png(icons / "256x256" / "apps" / "raye-win-7-zip-help.png", 256)
        program["main"] = "7-zip.chm"
        self.assertEqual(rsw.sharpen_icons(program, found, icons=str(icons)), [])
        # without a main program named, nothing is touched
        self.assertEqual(rsw.sharpen_icons({"icon": str(picture)}, found, icons=str(icons)), [])

    def test_zipped_setup_runs_under_its_own_name(self):
        p, data = zipped({"Install_tool_1_0.exe": PAYLOAD, "readme.txt": b"hi"})
        run = Runner()
        with Quiet():
            rc = rsw.install("winrar", p, run=run, popen=run.popen, opener=FakeOpener(data))
        self.assertEqual(rc, 0)
        self.assertEqual(run.seen_file, ("Install_tool_1_0.exe", PAYLOAD))
        left = [f for _, _, fs in os.walk(self.tmp.name) for f in fs]
        self.assertEqual(left, [])

    def test_zipped_setup_must_match_the_list(self):
        cases = (
            {"Install_tool_1_0.exe": PAYLOAD + b"!"},          # another setup inside
            {"readme.txt": PAYLOAD},                          # not there at all
            {"sub/Install_tool_1_0.exe": PAYLOAD},             # only in a folder
        )
        for members in cases:
            p, data = zipped(members)
            run = Runner()
            with self.subTest(list(members)), Quiet(), self.assertRaises(ValueError):
                rsw.install("winrar", p, run=run, popen=run.popen, opener=FakeOpener(data))
            self.assertEqual(run.calls, [])
        left = [f for _, _, fs in os.walk(self.tmp.name) for f in fs]
        self.assertEqual(left, [])

    def test_zip_entries_in_the_list(self):
        path = os.path.join(self.tmp.name, "w.json")
        p, _ = zipped({})
        with open(path, "w") as f:
            json.dump({"programs": [p]}, f)
        self.assertEqual(rsw.entry_for("winrar", path)["installer"]["unpack"]["file"], "Install_tool_1_0.exe")
        for change in ({"unpack": None}, {"unpack": {"file": "../x.exe", "sha256": "a" * 64}},
                       {"unpack": {"file": "x.exe", "sha256": "12"}}, {"unpack": {"file": "x.zip", "sha256": "a" * 64}}):
            q = json.loads(json.dumps(p))
            q["installer"].update(change)
            if q["installer"]["unpack"] is None:
                del q["installer"]["unpack"]
            with open(path, "w") as f:
                json.dump({"programs": [q]}, f)
            with self.subTest(change), self.assertRaises(ValueError):
                rsw.entry_for("winrar", path)

    def test_bad_arguments(self):
        self.assertEqual(rsw.main(["x", "install", "winrar"]), 2)
        self.assertEqual(rsw.main(["x", "install", "windows:../etc"]), 2)
        self.assertEqual(rsw.main(["x", "purge", "windows:winrar"]), 2)


if __name__ == "__main__":
    unittest.main()
