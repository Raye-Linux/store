# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the .deb files people open: raye-store-extra info and
install-file, without root, network or apt."""
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
import pathlib
import re
import stat
import tarfile
import tempfile
import unittest

HELPER = pathlib.Path(__file__).resolve().parent.parent / "src" / "raye-store-extra"
loader = importlib.machinery.SourceFileLoader("raye_store_extra", str(HELPER))
spec = importlib.util.spec_from_loader("raye_store_extra", loader)
rse = importlib.util.module_from_spec(spec)
loader.exec_module(rse)

PNG = b"\x89PNG\r\n\x1a\n" + b"pretend picture"
CONTROL = {
    "Package": "hello-world",
    "Version": "1:2.4-1",
    "Architecture": "amd64",
    "Maintainer": "Jane Maker <jane@example.org>",
    "Installed-Size": "2048",
    "Description": "Says hello\n A longer text.",
}
DESKTOP = (b"[Desktop Entry]\nType=Application\nName=Hello World\nName[fr]=Bonjour le monde\n"
           b"Exec=hello\nIcon=hello\n\n[Desktop Action new]\nName=New\n")


def tar_bytes(files, compression):
    out = io.BytesIO()
    mode = "w:" + compression if compression else "w"
    with tarfile.open(fileobj=out, mode=mode) as tar:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            if data is None:
                info.type = tarfile.DIRTYPE
                tar.addfile(info)
                continue
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    return out.getvalue()


def ar_bytes(members):
    out = bytearray(b"!<arch>\n")
    for name, data in members:
        out += f"{name:<16}{0:<12}{0:<6}{0:<6}{100644:<8}{len(data):<10}".encode() + b"`\n"
        out += data
        if len(data) % 2:
            out += b"\n"
    return bytes(out)


def make_deb(folder, control=None, files=None, compression="xz", name="app.deb", members=None):
    control = dict(CONTROL if control is None else control)
    text = "".join(f"{k}: {v}\n" for k, v in control.items()).encode()
    files = {"./usr/share/applications/hello.desktop": DESKTOP,
             "./usr/share/icons/hicolor/32x32/apps/hello.png": PNG + b"small",
             "./usr/share/icons/hicolor/128x128/apps/hello.png": PNG + b"big",
             "./usr/bin/hello": b"#!/bin/sh\n"} if files is None else files
    if members is None:
        members = [("debian-binary", b"2.0\n"),
                   ("control.tar." + compression, tar_bytes({"./control": text}, compression)),
                   ("data.tar." + compression, tar_bytes(files, compression))]
    path = os.path.join(folder, name)
    with open(path, "wb") as f:
        f.write(ar_bytes(members))
    return path


class Paths(unittest.TestCase):
    def test_plain_path(self):
        p = "/home/jane/Downloads/hello_2.4_amd64.deb"
        self.assertEqual(rse.checked_path(p), p)
        self.assertEqual(rse.checked_path("/tmp/HELLO.DEB"), "/tmp/HELLO.DEB")

    def test_path_tricks(self):
        for bad in ["hello.deb", "./hello.deb", "/home/jane/../root/x.deb", "//etc/x.deb",
                    "/tmp/x.deb/", "/tmp/./x.deb", "/tmp/x.txt", "/tmp/x.deb\n", "/tmp/a\0.deb",
                    "/tmp/" + "a" * 5000 + ".deb", "", "/tmp/.deb", 42]:
            with self.subTest(bad=bad):
                with self.assertRaises(rse.FileProblem):
                    rse.checked_path(bad)

    def test_only_regular_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            real = make_deb(tmp)
            link = os.path.join(tmp, "link.deb")
            os.symlink(real, link)
            fifo = os.path.join(tmp, "pipe.deb")
            os.mkfifo(fifo)
            folder = os.path.join(tmp, "folder.deb")
            os.mkdir(folder)
            fd, size = rse.open_regular(real)
            os.close(fd)
            self.assertEqual(size, os.path.getsize(real))
            for bad, code in [(link, "notfile"), (fifo, "notfile"), (folder, "notfile"),
                              (os.path.join(tmp, "gone.deb"), "notfound")]:
                with self.subTest(bad=bad), self.assertRaises(rse.FileProblem) as caught:
                    rse.open_regular(bad)
                self.assertEqual(caught.exception.code, code)

    @unittest.skipUnless(os.geteuid() == 0, "needs root to drop to another user")
    def test_root_opens_as_the_caller(self):
        with tempfile.TemporaryDirectory() as tmp:
            os.chmod(tmp, 0o755)
            path = make_deb(tmp)
            os.chmod(path, 0o600)
            with self.assertRaises(rse.FileProblem) as caught:
                rse.open_as_caller(path, {"PKEXEC_UID": "65534"})
            self.assertEqual(caught.exception.code, "noaccess")
            self.assertEqual(os.geteuid(), 0)
            self.assertEqual(os.getegid(), 0)

    def test_root_without_pkexec_caller_refused(self):
        if os.geteuid() != 0:
            self.skipTest("only root asks who called")
        with self.assertRaises(rse.FileProblem):
            rse.open_as_caller("/tmp/x.deb", {})


class Reading(unittest.TestCase):
    def read(self, path, **kw):
        with open(path, "rb") as f:
            return rse.read_deb(f, **kw)

    def test_fields_name_and_icon(self):
        for compression in ("gz", "xz", "zst", ""):
            with self.subTest(compression=compression), tempfile.TemporaryDirectory() as tmp:
                info = self.read(make_deb(tmp, compression=compression), icon_dir=tmp)
                self.assertEqual(info["package"], "hello-world")
                self.assertEqual(info["version"], "1:2.4-1")
                self.assertEqual(info["architecture"], "amd64")
                self.assertEqual(info["summary"], "Says hello")
                self.assertEqual(info["maintainer"], "Jane Maker <jane@example.org>")
                self.assertEqual(info["installedSize"], 2048 * 1024)
                self.assertEqual(info["names"], {"": "Hello World", "fr": "Bonjour le monde"})
                self.assertEqual(info["desktop"], "hello.desktop")
                # the biggest picture of the icon
                self.assertEqual(pathlib.Path(info["icon"]).read_bytes(), PNG + b"big")
                self.assertEqual(os.path.dirname(info["icon"]), tmp)

    def test_scalable_icon_wins(self):
        svg = b"<svg xmlns='http://www.w3.org/2000/svg'/>"
        with tempfile.TemporaryDirectory() as tmp:
            files = {"./usr/share/applications/hello.desktop": DESKTOP,
                     "./usr/share/icons/hicolor/256x256/apps/hello.png": PNG,
                     "./usr/share/icons/hicolor/scalable/apps/hello.svg": svg}
            info = self.read(make_deb(tmp, files=files), icon_dir=tmp)
            self.assertTrue(info["icon"].endswith(".svg"))

    def test_pixmap_and_hidden_entries(self):
        hidden = b"[Desktop Entry]\nType=Application\nName=Helper\nNoDisplay=true\nIcon=helper\n"
        with tempfile.TemporaryDirectory() as tmp:
            files = {"./usr/share/applications/a-helper.desktop": hidden,
                     "./usr/share/applications/hello.desktop": DESKTOP,
                     "./usr/share/pixmaps/hello.png": PNG}
            info = self.read(make_deb(tmp, files=files), icon_dir=tmp)
            self.assertEqual(info["names"][""], "Hello World")
            self.assertEqual(pathlib.Path(info["icon"]).read_bytes(), PNG)

    def test_icon_outside_the_theme(self):
        with tempfile.TemporaryDirectory() as tmp:
            files = {"./usr/share/applications/hello.desktop": DESKTOP,
                     "./opt/hello/resources/hello.png": PNG + b"opt",
                     "./opt/hello/resources/other.png": PNG}
            info = self.read(make_deb(tmp, files=files), icon_dir=tmp)
            self.assertEqual(pathlib.Path(info["icon"]).read_bytes(), PNG + b"opt")

    def test_no_desktop_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            info = self.read(make_deb(tmp, files={"./usr/bin/hello": b"x"}), icon_dir=tmp)
            self.assertEqual(info["names"], {})
            self.assertEqual(info["icon"], "")
            self.assertEqual(info["desktop"], "")

    def test_icon_is_never_written_where_the_package_says(self):
        with tempfile.TemporaryDirectory() as tmp:
            files = {"./usr/share/applications/hello.desktop": DESKTOP,
                     "../../hello.png": PNG,
                     "./usr/share/pixmaps/hello.png": PNG}
            icons = os.path.join(tmp, "icons")
            os.mkdir(icons)
            info = self.read(make_deb(tmp, files=files), icon_dir=icons)
            self.assertEqual(os.listdir(icons), [os.path.basename(info["icon"])])

    def test_control_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            info = self.read(make_deb(tmp), data=False)
            self.assertEqual(info["package"], "hello-world")
            self.assertEqual(info["names"], {})

    def test_not_a_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "x.deb")
            pathlib.Path(path).write_bytes(b"PK\x03\x04 a zip file")
            with self.assertRaises(rse.FileProblem) as caught:
                self.read(path)
            self.assertEqual(caught.exception.code, "notdeb")
            # an ar archive that is not a package
            path = make_deb(tmp, members=[("readme", b"hi")])
            with self.assertRaises(rse.FileProblem) as caught:
                self.read(path)
            self.assertEqual(caught.exception.code, "notdeb")

    def test_damaged_packages(self):
        cases = {
            "no control": [("debian-binary", b"2.0\n"), ("data.tar.gz", tar_bytes({}, "gz"))],
            "cut short": None,
            "control is noise": [("debian-binary", b"2.0\n"), ("control.tar.gz", b"\x1f\x8bnoise")],
        }
        for label, members in cases.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as tmp:
                path = make_deb(tmp, members=members)
                if members is None:
                    data = pathlib.Path(path).read_bytes()
                    pathlib.Path(path).write_bytes(data[:120])
                with self.assertRaises(rse.FileProblem) as caught:
                    self.read(path)
                self.assertEqual(caught.exception.code, "broken")

    def test_bad_fields(self):
        for key, value in [("Package", "Bad Name"), ("Package", "hello\n "), ("Version", "1 2"), ("Architecture", "amd64; rm"),
                           ("Version", None), ("Architecture", None)]:
            control = dict(CONTROL)
            if value is None:
                del control[key]
            else:
                control[key] = value
            with self.subTest(key=key, value=value), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(rse.FileProblem) as caught:
                    self.read(make_deb(tmp, control=control))
                self.assertEqual(caught.exception.code, "broken")


class Architecture(unittest.TestCase):
    def test_arch(self):
        self.assertTrue(rse.arch_ok("amd64", ["amd64"]))
        self.assertTrue(rse.arch_ok("all", ["amd64"]))
        self.assertTrue(rse.arch_ok("i386", ["amd64", "i386"]))
        self.assertFalse(rse.arch_ok("arm64", ["amd64", "i386"]))


class Apt(unittest.TestCase):
    def test_reasons(self):
        cases = [
            ("E: Could not get lock /var/lib/dpkg/lock-frontend. It is held by process 12 (apt-get)", ("busy", "")),
            (" hello-world : Depends: libfoo3 (>= 2) but it is not installable\n"
             "E: Unable to correct problems, you have held broken packages.", ("dependencies", "libfoo3")),
            (" hello-world : Conflicts: plasma-desktop but 6.6 is to be installed", ("conflict", "plasma-desktop")),
            (" hello-world : Breaks: kwin (< 7) but 6.6 is to be installed", ("conflict", "kwin")),
            ("E: Packages need to be removed but remove is disabled.", ("conflict", "")),
            ("E: Packages were downgraded and -y was used without --allow-downgrades.", ("older", "")),
            ("E: You don't have enough free space in /var/cache/apt/archives/.", ("space", "")),
            ("E: Failed to fetch http://x/y.deb  Temporary failure resolving 'x'", ("network", "")),
            ("dpkg: error processing package hello-world (--configure):\n"
             "E: Sub-process /usr/bin/dpkg returned an error code (1)", ("setup", "")),
            ("E: something nobody planned for", ("unknown", "")),
        ]
        for text, expected in cases:
            with self.subTest(text=text):
                self.assertEqual(rse.classify(text), expected)

    def test_status_lines(self):
        self.assertEqual(rse.status_line("pmstatus:hello-world:42.8571:Installing hello-world"), ("installing", 42))
        self.assertEqual(rse.status_line("dlstatus:1:12.5:Retrieving file 1 of 3"), ("downloading", 12))
        self.assertEqual(rse.status_line("pmerror:x:10:failed"), None)
        self.assertEqual(rse.status_line("pmstatus:x:abc:y"), None)
        self.assertEqual(rse.status_line("pmstatus:x:250:y"), ("installing", 100))


class FakeApt:
    """Stands in for apt-get: records the command, writes status lines on
    the fd it was given and prints what the test asks."""

    def __init__(self, code=0, output="", status=()):
        self.code = code
        self.output = output
        self.status = status
        self.commands = []
        self.seen_copy = None

    def __call__(self, cmd, status_fd):
        self.commands.append(cmd)
        copy = cmd[-1]
        self.seen_copy = (copy, os.path.exists(copy), stat.S_IMODE(os.stat(copy).st_mode))
        for line in self.status:
            os.write(status_fd, (line + "\n").encode())
        return self.code, self.output


class InstallFile(unittest.TestCase):
    def run_install(self, tmp, apt, deb=None, architectures=("amd64",), launchers=(), expected=None,
                    protected=lambda package: False, plan=None):
        cache = os.path.join(tmp, "cache")
        said = []
        deb = deb or make_deb(tmp)
        if expected is None:
            try:
                expected = hashlib.sha256(pathlib.Path(deb).read_bytes()).hexdigest()
            except OSError:
                expected = "0" * 64
        code = rse.install_file(deb, expected, say=said.append, apt=apt, cache=cache,
                                architectures=lambda: list(architectures),
                                opener=rse.open_regular, launchers=lambda p: list(launchers),
                                clean=lambda: said.append("clean"), protected=protected,
                                plan=plan or (lambda copy: (0, "Inst hello-world (1:2.4-1 local-deb [amd64])\n")),
                                hook="/usr/libexec/raye-store-extra")
        return code, said, cache

    def test_installs_a_copy_and_says_how_far(self):
        with tempfile.TemporaryDirectory() as tmp:
            apt = FakeApt(status=["dlstatus:1:50:Retrieving", "pmstatus:hello-world:30:Unpacking",
                                  "pmstatus:hello-world:100:Done"])
            code, said, cache = self.run_install(tmp, apt, launchers=["/usr/share/applications/hello.desktop"])
            self.assertEqual(code, 0)
            copy, existed, mode = apt.seen_copy
            self.assertTrue(existed)
            self.assertEqual(mode, 0o644)
            self.assertTrue(copy.startswith(cache + "/"))
            cmd = apt.commands[0]
            self.assertEqual(cmd[:3], ["apt-get", "install", "-y"])
            self.assertIn("--no-remove", cmd)
            self.assertNotIn("--allow-downgrades", cmd)
            self.assertIn("package hello-world", said)
            self.assertIn("version 1:2.4-1", said)
            self.assertIn("phase downloading", said)
            self.assertIn("progress 50", said)
            self.assertIn("phase installing", said)
            self.assertIn("progress 100", said)
            self.assertIn("launcher /usr/share/applications/hello.desktop", said)
            self.assertEqual(said[-1], "clean")
            # the copy is gone once it is installed
            self.assertEqual(os.listdir(cache), [])

    def test_wrong_architecture_never_reaches_apt(self):
        with tempfile.TemporaryDirectory() as tmp:
            control = dict(CONTROL, Architecture="arm64")
            apt = FakeApt()
            code, said, cache = self.run_install(tmp, apt, deb=make_deb(tmp, control=control))
            self.assertEqual(code, 1)
            self.assertEqual(apt.commands, [])
            self.assertIn("reason architecture arm64", said)
            self.assertEqual(os.listdir(cache), [])

    def test_broken_file_never_reaches_apt(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "x.deb")
            pathlib.Path(path).write_bytes(b"!<arch>\nnot really")
            apt = FakeApt()
            code, said, _ = self.run_install(tmp, apt, deb=path)
            self.assertEqual(code, 1)
            self.assertEqual(apt.commands, [])
            self.assertIn("reason broken", said)

    def test_file_swapped_after_it_was_shown(self):
        with tempfile.TemporaryDirectory() as tmp:
            apt = FakeApt()
            code, said, cache = self.run_install(tmp, apt, expected="ab" * 32)
            self.assertEqual(code, 1)
            self.assertEqual(apt.commands, [])
            self.assertIn("reason changed", said)
            self.assertEqual(os.listdir(cache), [])
            code, said, _ = self.run_install(tmp, apt, expected="not a hash")
            self.assertIn("reason changed", said)
            self.assertEqual(apt.commands, [])

    def test_system_packages_are_never_replaced(self):
        with tempfile.TemporaryDirectory() as tmp:
            apt = FakeApt()
            code, said, _ = self.run_install(tmp, apt, protected=lambda package: package == "hello-world")
            self.assertEqual(code, 1)
            self.assertIn("reason system hello-world", said)
            essential = make_deb(tmp, control=dict(CONTROL, Essential="yes"), name="e.deb")
            code, said, _ = self.run_install(tmp, apt, deb=essential)
            self.assertIn("reason system hello-world", said)
            self.assertEqual(apt.commands, [])

    def test_stale_copies_go(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = os.path.join(tmp, "cache")
            old = os.path.join(cache, "file-old")
            os.makedirs(old)
            os.utime(old, (0, 0))
            keep = os.path.join(cache, "other")
            os.makedirs(keep)
            os.utime(keep, (0, 0))
            self.run_install(tmp, FakeApt())
            self.assertEqual(os.listdir(cache), ["other"])

    def test_apt_failure_is_explained(self):
        with tempfile.TemporaryDirectory() as tmp:
            apt = FakeApt(code=100, output=" hello-world : Depends: libfoo3 but it is not installable\n")
            code, said, cache = self.run_install(tmp, apt)
            self.assertEqual(code, 1)
            self.assertIn("reason dependencies libfoo3", said)
            self.assertEqual(os.listdir(cache), [])

    def test_bad_path_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            apt = FakeApt()
            code, said, _ = self.run_install(tmp, apt, deb=os.path.join(tmp, "..", "x.deb"))
            self.assertEqual(code, 1)
            self.assertEqual(apt.commands, [])
            self.assertIn("reason badpath", said)


STEAM = dict(CONTROL, Package="steam-launcher", Version="1:1.0.0.87",
             Conflicts="steam-devices, steam-installer", Breaks="steam64",
             Replaces="steam, steam-devices, steam-installer, steam64",
             Provides="steam, steam-devices, steam-installer")
STEAM_PLAN = ("NOTE: This is only a simulation!\n"
              "Inst lsof (4.99.4 Ubuntu:26.04/resolute [amd64])\n"
              "Remv steam-devices [1:1.0.0.85~ds-2build1] [raye-gaming:amd64 ]\n"
              "Inst steam-launcher (1:1.0.0.87 local-deb [amd64])\n"
              "Conf steam-launcher (1:1.0.0.87 local-deb [amd64])\n")


class Swaps(unittest.TestCase):
    """A file that takes the place of a package (Valve's Steam launcher
    and steam-devices): apt may remove that one, never a part of Raye."""

    def test_relation_names(self):
        self.assertEqual(rse.relation_names("steam-devices (<< 2), aa | bb:any, ,Bad Name"),
                         ["steam-devices", "aa", "bb"])
        self.assertEqual(rse.relation_names(""), [])

    def test_swaps_need_a_clash_and_a_replace(self):
        self.assertEqual(rse.swaps_of(STEAM), ["steam-devices", "steam-installer", "steam64"])
        # replacing files of a package is not taking its place
        self.assertEqual(rse.swaps_of({"Package": "x", "Replaces": "kate"}), [])
        self.assertEqual(rse.swaps_of({"Package": "x", "Conflicts": "kate"}), [])
        self.assertEqual(rse.swaps_of({"Package": "x", "Breaks": "kate (<< 2)", "Provides": "kate"}), ["kate"])

    def test_read_deb_says_its_swaps(self):
        with tempfile.TemporaryDirectory() as tmp:
            with open(make_deb(tmp, control=STEAM), "rb") as f:
                self.assertEqual(rse.read_deb(f, data=False)["swaps"], ["steam-devices", "steam-installer", "steam64"])

    def test_removals_in_a_simulation(self):
        self.assertEqual(rse.removals_in(STEAM_PLAN), ["steam-devices"])
        self.assertEqual(rse.removals_in("Remv libfoo:i386 [1]\nRemv libfoo:amd64 [1]\nInst x (1)\n"), ["libfoo"])
        self.assertEqual(rse.removals_in("The following packages will be REMOVED:\n  steam-devices\n"), [])

    def test_judge(self):
        raye = lambda p: p.startswith("raye-")
        self.assertIsNone(rse.judge_removals([], [], raye))
        self.assertIsNone(rse.judge_removals(["steam-devices"], ["steam-devices"], raye))
        self.assertEqual(rse.judge_removals(["steam-devices", "raye-gaming"], ["steam-devices"], raye),
                         ("removes", "raye-gaming"))
        self.assertEqual(rse.judge_removals(["kate"], ["steam-devices"], raye), ("conflict", "kate"))
        # even one it names: a part of Raye stays
        self.assertEqual(rse.judge_removals(["raye-store"], ["raye-store"], raye), ("removes", "raye-store"))

    def test_guard_hook(self):
        def feed(*lines):
            return io.StringIO("VERSION 2\nAPT::Architecture=amd64\nDir::Bin::dpkg=/usr/bin/dpkg\n\n"
                               + "".join(line + "\n" for line in lines))
        raye = lambda p: p.startswith("raye-")
        ok = feed("lsof - < 4.99.4 /var/cache/apt/archives/lsof.deb",
                  "steam-devices 1:1.0.0.85~ds-2build1 > - **REMOVE**",
                  "steam-launcher - < 1:1.0.0.87 /var/cache/raye-store/file-x/package.deb")
        err = io.StringIO()
        self.assertEqual(rse.guard_removals({"steam-devices"}, stdin=ok, err=err, protected=raye), 0)
        self.assertEqual(err.getvalue(), "")
        more = feed("steam-devices 1 > - **REMOVE**", "kate 2 > - **REMOVE**")
        self.assertEqual(rse.guard_removals({"steam-devices"}, stdin=more, err=err, protected=raye), 1)
        self.assertIn("RAYE-REFUSED conflict kate", err.getvalue())
        err = io.StringIO()
        raye_gone = feed("raye-gaming:amd64 1.0 > - **REMOVE**")
        self.assertEqual(rse.guard_removals({"raye-gaming"}, stdin=raye_gone, err=err, protected=raye), 1)
        self.assertIn("RAYE-REFUSED removes raye-gaming", err.getvalue())
        # the configuration part is never read as packages
        self.assertEqual(rse.guard_removals(set(), stdin=feed(), err=io.StringIO(), protected=raye), 0)
        self.assertEqual(rse.classify("RAYE-REFUSED removes raye-gaming\nE: Failure running script x"),
                         ("removes", "raye-gaming"))
        self.assertEqual(rse.main(["x", "guard-removals", "Bad Name"]), 2)


class InstallSwap(InstallFile):
    def test_replaced_package_is_removed_with_the_hook(self):
        with tempfile.TemporaryDirectory() as tmp:
            apt = FakeApt()
            code, said, _ = self.run_install(tmp, apt, deb=make_deb(tmp, control=STEAM),
                                             plan=lambda copy: (0, STEAM_PLAN))
            self.assertEqual(code, 0)
            cmd = apt.commands[0]
            self.assertNotIn("--no-remove", cmd)
            self.assertIn("steam-devices-", cmd)
            self.assertIn("DPkg::Pre-Install-Pkgs::=/usr/libexec/raye-store-extra guard-removals steam-devices", cmd)
            self.assertIn("DPkg::Tools::Options::/usr/libexec/raye-store-extra::Version=2", cmd)
            self.assertTrue(cmd[-1].endswith("/package.deb"))
            self.assertIn("replaces steam-devices", said)

    def test_raye_part_is_never_removed(self):
        with tempfile.TemporaryDirectory() as tmp:
            apt = FakeApt()
            plan = STEAM_PLAN + "Remv raye-gaming [1.0]\n"
            code, said, _ = self.run_install(tmp, apt, deb=make_deb(tmp, control=STEAM), plan=lambda c: (0, plan),
                                             protected=lambda p: p.startswith("raye-"))
            self.assertEqual(code, 1)
            self.assertEqual(apt.commands, [])
            self.assertIn("reason removes raye-gaming", said)

    def test_other_removal_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            apt = FakeApt()
            code, said, _ = self.run_install(tmp, apt, plan=lambda c: (0, "Remv kate [25.12]\nInst hello-world (1)\n"))
            self.assertEqual(code, 1)
            self.assertEqual(apt.commands, [])
            self.assertIn("reason conflict kate", said)

    def test_failed_plan_is_explained(self):
        with tempfile.TemporaryDirectory() as tmp:
            apt = FakeApt()
            code, said, _ = self.run_install(
                tmp, apt, plan=lambda c: (100, " hello-world : Depends: libfoo3 but it is not installable\n"))
            self.assertEqual(code, 1)
            self.assertEqual(apt.commands, [])
            self.assertIn("reason dependencies libfoo3", said)


class Info(unittest.TestCase):
    def test_info_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = make_deb(tmp)
            out = io.StringIO()
            code = rse.info(path, tmp, out=out, architectures=lambda: ["amd64"],
                            installed=lambda pkg: "1:2.3-1", compare=lambda a, b: 1,
                            launchers=lambda pkg: ["/usr/share/applications/hello.desktop"],
                            plan=lambda p: (0, "Inst hello-world (1:2.4-1 local-deb [amd64])\n"))
            self.assertEqual(code, 0)
            data = json.loads(out.getvalue())
            self.assertTrue(data["ok"])
            self.assertEqual(data["package"], "hello-world")
            self.assertTrue(data["archOk"])
            self.assertEqual(data["installed"], "1:2.3-1")
            self.assertEqual(data["newer"], 1)
            self.assertEqual(data["fileSize"], os.path.getsize(path))
            self.assertEqual(data["launchers"], ["/usr/share/applications/hello.desktop"])
            self.assertEqual(data["sha256"], hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest())

    def test_info_says_what_it_replaces(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = make_deb(tmp, control=STEAM)
            for plan, removes, blocked in (
                    (STEAM_PLAN, ["steam-devices"], None),
                    (STEAM_PLAN + "Remv raye-gaming [1]\n", ["steam-devices", "raye-gaming"],
                     {"reason": "removes", "what": "raye-gaming"}),
                    ("E: whatever", [], None)):
                out = io.StringIO()
                rse.info(path, tmp, out=out, architectures=lambda: ["amd64"], installed=lambda pkg: "",
                         plan=lambda p, plan=plan: (0 if plan.startswith("NOTE") else 100, plan),
                         protected=lambda p: p.startswith("raye-"))
                data = json.loads(out.getvalue())
                with self.subTest(plan=plan[-30:]):
                    self.assertEqual(data["swaps"], ["steam-devices", "steam-installer", "steam64"])
                    self.assertEqual(data["removes"], removes)
                    self.assertEqual(data["blocked"], blocked)

    def test_info_problem(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = io.StringIO()
            code = rse.info(os.path.join(tmp, "nope.deb"), tmp, out=out)
            self.assertEqual(code, 1)
            self.assertEqual(json.loads(out.getvalue()), {"ok": False, "error": "notfound"})

    def test_verbs(self):
        self.assertEqual(rse.main(["x", "install-file"]), 2)
        self.assertEqual(rse.main(["x", "install-file", "/tmp/a.deb"]), 2)
        self.assertEqual(rse.main(["x", "info", "/tmp/a.deb"]), 2)
        # reading a file never runs as root through pkexec
        os.environ["PKEXEC_UID"] = "1000"
        try:
            if os.geteuid() == 0:
                self.assertEqual(rse.main(["x", "info", "/tmp/a.deb", "/tmp"]), 2)
        finally:
            del os.environ["PKEXEC_UID"]
        if os.geteuid() != 0:
            self.assertEqual(rse.main(["x", "install-file", "/tmp/a.deb", "0" * 64]), 2)


class Window(unittest.TestCase):
    def test_package_words_are_never_markup(self):
        # a name or a description in a .deb could carry <img src=...>
        qml = (HELPER.parent / "qml" / "DebInstall.qml").read_text(encoding="utf-8")
        blocks = re.split(r"\n\s*Text \{", qml)[1:]
        shown = [b for b in blocks if re.search(r"headline\(\)|deb\.(summary|details|publisher)", b.split("}")[0])]
        self.assertEqual(len(shown), 4)
        for block in shown:
            self.assertIn("textFormat: Text.PlainText", block.split("}")[0])

    def test_info_bars_are_never_markup(self):
        # the Steam advice carries the name the .deb gives itself
        bar = (HELPER.parent / "qml" / "ui" / "InfoBar.qml").read_text(encoding="utf-8")
        block = bar.split("text: bar.text", 1)[1].split("}", 1)[0]
        self.assertIn("textFormat: Text.PlainText", block)
        qml = (HELPER.parent / "qml" / "DebInstall.qml").read_text(encoding="utf-8")
        self.assertIn("deb.replacesText", qml)
        self.assertIn("deb.openInStore()", qml)


if __name__ == "__main__":
    unittest.main()
