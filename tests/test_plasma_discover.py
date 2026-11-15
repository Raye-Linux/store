# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for data/plasma-discover: Discover's command line, sent to the Store.

The forms below are not guessed. They were read out of the image of
2026-11-06 (var/tmp/raye-build/chroot), caller by caller, and every one of
them is a *measured* command line, not a plausible one:

  libKF6KIOWidgets.so.6.24.0  the "Get more Apps from Discover" button of
                              the Open with dialog:
                              KIO::CommandLauncherJob("plasma-discover",
                                                      {"--mime", mimeType})
  xdg-desktop-portal-kde      the portal's app chooser, same two arguments
  kcm_feedback.so             System Settings / User Feedback: --feedback
                              (a FLAG: it carries no value)
  gwenview_importer           plasma-discover --search <protocol>, and a
                              QStandardPaths::findExecutable("plasma-discover")
                              to decide whether to offer the button at all

🚨 --feedback is why this file grew. The shim treated it as an option with
a value, shifted past the end of the argument list, and dash killed it:
"shift: can't shift that many", exit 2, and the Store never opened. Five
tests passed because not one of them put an option last.

The rule the shim now follows, and that these tests pin down:

  * a form it honours             -- the right program, nothing on stderr,
                                     exit 0
  * a form it understands but
    cannot act on (--mime, a
    category, a search)           -- the Store's front page, silently: the
                                     request asked for nothing the Store
                                     can be opened on
  * a form it does NOT understand -- the Store's front page AND a line on
                                     stderr naming what was dropped AND
                                     exit 3. Exit 0 here is the whole
                                     defect: a button that does nothing
                                     and no trace anywhere.
"""
import os
import pathlib
import subprocess
import tempfile
import unittest

LAUNCHER = pathlib.Path(__file__).resolve().parent.parent / "data" / "plasma-discover"

# what the shim says when it gives up on an argument
PUZZLED = 3


class Launched:
    """What one run of the shim did: the programs it started, with their
    arguments, plus what it said and the code it left behind."""

    def __init__(self, code, log, said):
        self.code = code
        self.log = log
        self.said = said

    def __repr__(self):
        return f"Launched(code={self.code!r}, log={self.log!r}, said={self.said!r})"


class PlasmaDiscover(unittest.TestCase):
    def launch(self, *args):
        with tempfile.TemporaryDirectory() as tmp:
            log = pathlib.Path(tmp) / "log"
            fake = pathlib.Path(tmp) / "fake"
            fake.write_text('#!/bin/sh\nprintf "%s" "$(basename "$0")" >> "$LOG"\n'
                            'for a in "$@"; do printf " %s" "$a" >> "$LOG"; done\n')
            fake.chmod(0o755)
            store, settings = pathlib.Path(tmp) / "raye-store", pathlib.Path(tmp) / "raye-settings"
            store.symlink_to(fake)
            settings.symlink_to(fake)
            env = dict(os.environ, LOG=str(log), RAYE_STORE=str(store), RAYE_SETTINGS=str(settings))
            done = subprocess.run(["sh", str(LAUNCHER), *args], env=env, timeout=10,
                                  stderr=subprocess.PIPE, text=True)
            return Launched(done.returncode, log.read_text() if log.exists() else "",
                            done.stderr)

    def run_launcher(self, *args):
        """A form the shim is supposed to handle: it must leave no code and
        say nothing. A warning on a form that works is noise, and noise is
        how a real warning stops being read."""
        done = self.launch(*args)
        self.assertEqual(done.code, 0, done)
        self.assertEqual(done.said, "", done)
        return done.log

    # ---------------------------------------------------- what it honours

    def test_plain_start_opens_the_store(self):
        self.assertEqual(self.run_launcher(), "raye-store")

    def test_find_more_apps_for_a_file_type_opens_the_store(self):
        # the Open with dialog's button, and the portal's: --mime <type>
        self.assertEqual(self.run_launcher("--mime", "image/png"), "raye-store")
        self.assertEqual(self.run_launcher("--mime=image/png"), "raye-store")

    def test_an_app_opens_its_page(self):
        self.assertEqual(self.run_launcher("--application", "appstream://org.kde.kamera"),
                         "raye-store appstream://org.kde.kamera")
        self.assertEqual(self.run_launcher("--application=org.gimp.GIMP"), "raye-store org.gimp.GIMP")
        self.assertEqual(self.run_launcher("appstream://org.kde.kamera"),
                         "raye-store appstream://org.kde.kamera")

    def test_updates_open_raye_update(self):
        self.assertEqual(self.run_launcher("--mode", "update"), "raye-settings --page update")
        self.assertEqual(self.run_launcher("--mode=Update"), "raye-settings --page update")

    def test_other_options_do_not_become_an_app(self):
        self.assertEqual(self.run_launcher("--search", "paint", "--mode", "installed"), "raye-store")
        self.assertEqual(self.run_launcher("--category", "Games", "--backends", "flatpak"), "raye-store")

    def test_user_feedback_opens_the_store(self):
        """System Settings / User Feedback runs `plasma-discover --feedback`.
        It is a flag: nothing follows it. The shim used to shift past the
        end of the list here and die before opening anything."""
        self.assertEqual(self.run_launcher("--feedback"), "raye-store")

    def test_the_lists_discover_prints_just_open_the_store(self):
        self.assertEqual(self.run_launcher("--listbackends"), "raye-store")
        self.assertEqual(self.run_launcher("--listmodes"), "raye-store")

    def test_every_form_the_image_really_sends(self):
        """One case per caller measured in the image. A caller that gains a
        form the shim has never seen is exactly what the stderr line and the
        exit code below are for."""
        for args in (["--mime", "image/png"],                  # KIOWidgets, portal
                     ["--feedback"],                           # kcm_feedback
                     ["--search", "camera"],                   # gwenview_importer
                     []):                                      # the plain click
            with self.subTest(args=args):
                done = self.launch(*args)
                self.assertEqual(done.code, 0, done)
                self.assertEqual(done.said, "", done)
                self.assertTrue(done.log.startswith("raye-store"), done)

    def test_help_and_version_are_the_stores_own(self):
        """`plasma-discover --help` printing nothing and opening a window
        would be its own small silence."""
        for flag in ("--help", "--version"):
            with self.subTest(flag=flag):
                self.assertEqual(self.run_launcher(flag), "raye-store " + flag)

    # ------------------------------------------- what it refuses to drop

    def assertPuzzled(self, named, *args):
        """It fell back to the front page, said which argument it could not
        honour, and left a code behind."""
        done = self.launch(*args)
        self.assertEqual(done.code, PUZZLED, done)
        self.assertEqual(done.log, "raye-store", done)
        self.assertIn(named, done.said, done)
        self.assertIn("plasma-discover", done.said, done)
        return done

    def test_an_option_left_without_its_value_is_said_out_loud(self):
        """The --feedback crash came from this shape. Opening the front page
        is a fine answer; doing it without a word is not."""
        for flag in ("--mime", "--search", "--category", "--mode",
                     "--backends", "--application"):
            with self.subTest(flag=flag):
                self.assertPuzzled(flag, flag)

    def test_an_unknown_option_is_said_out_loud(self):
        """Discover's own command line grows. A form the shim has never seen
        must not be swallowed by a catch-all."""
        self.assertPuzzled("--offline-updates-now", "--offline-updates-now")
        self.assertPuzzled("--install-ref", "--install-ref", "something")

    def test_a_link_the_store_cannot_open_is_said_out_loud(self):
        """Only appstream: becomes an app page. apt://, snap:// and a local
        package file are real Discover arguments and the Store has nowhere to
        put them -- so it says so instead of showing the front page as if
        that had been the request."""
        for argument in ("apt://gimp", "snap://core", "/tmp/nothing.flatpakref",
                         "flatpak+https://example.invalid/x.flatpakref"):
            with self.subTest(argument=argument):
                self.assertPuzzled(argument, argument)

    def test_a_local_file_to_install_is_said_out_loud(self):
        """--local-filename asks for something the Store cannot do at all."""
        self.assertPuzzled("/tmp/nothing.flatpakref",
                           "--local-filename", "/tmp/nothing.flatpakref")
        self.assertPuzzled("/tmp/nothing.flatpakref",
                           "--local-filename=/tmp/nothing.flatpakref")

    def test_an_unknown_option_does_not_cost_the_app(self):
        """What it could honour, it honours: the code and the warning are
        about the fallback, not about every line of the command."""
        done = self.launch("--brand-new", "x", "appstream://org.kde.kate")
        self.assertEqual(done.code, 0, done)
        self.assertEqual(done.log, "raye-store appstream://org.kde.kate", done)
        self.assertIn("--brand-new", done.said, done)

    def test_it_never_dies_before_opening_something(self):
        """Whatever it is handed, one of the two programs starts. The shim is
        reached by a click; a click must never land on nothing."""
        for args in ([], ["--mime"], ["--feedback"], ["-x"], ["--"], ["apt://x"],
                     ["--mode"], ["--mode", "update"], ["--application"],
                     ["appstream://org.kde.kate"], ["--help"], ["--search", "-"]):
            with self.subTest(args=args):
                done = self.launch(*args)
                self.assertTrue(done.log.startswith(("raye-store", "raye-settings")), done)
                self.assertNotIn("shift", done.said, done)


class TheButtonThatNamesDiscover(unittest.TestCase):
    """🚨 The shim answers the command; it does not answer the wording.

    Measured in libKF6KIOWidgets.so.6.24.0 of the image (strings are
    QStringLiteral, so UTF-16: `strings -a` alone finds none of them and
    that is how this was first read wrong):

      .rodata 0x117f28  "org.kde.discover"   KService::serviceByDesktopName
      .rodata 0x117f50  "plasma-discover"    the command of the job
      .rodata 0x115220  "--mime"             its only argument
      .rodata 0x11524e  "plasmadiscover"     the icon
      .rodata 0x115c5a  "Get more Apps from Discover"   the label, domain kio6

    and in the code at 0x8aed9 the button is built only after
    serviceByDesktopName() returned something. The image ships no
    org.kde.discover.desktop, so the button is not there to be read --
    which is why no translation of that label is needed, and why adding
    such an entry to make the button appear would put the word "Discover"
    in front of every user of a beta instead.

    Changing the label itself is not possible from this repository:
    /usr/share/locale/fr/LC_MESSAGES/kio6.mo belongs to the kio6 package
    (it already translates that very message), and English has no catalog
    to override. That leaves a patched kio6, which is the owner's call,
    not this repository's.
    """

    def test_no_desktop_entry_of_ours_claims_to_be_discover(self):
        """CMakeLists.txt installs data/*.desktop by glob, so a file
        dropped in data/ is shipped without any edit -- including one that
        turns that button on."""
        for path in sorted((LAUNCHER.parent).rglob("*.desktop")):
            with self.subTest(file=path.name):
                self.assertNotIn("discover", path.name.lower(),
                                 f"{path.name} would switch on KIO's "
                                 '"Get more Apps from Discover" button')

    def test_the_command_keeps_being_installed_under_that_name(self):
        """Gwenview decides whether to offer its install button with
        QStandardPaths::findExecutable("plasma-discover"). Stop installing
        the shim and that button goes quiet -- no error, just gone."""
        cmake = (LAUNCHER.parent.parent / "CMakeLists.txt").read_text()
        line = "install(PROGRAMS data/plasma-discover"
        # a short message: a failure that prints the whole file is unreadable
        self.assertTrue(line in cmake, f"CMakeLists.txt no longer has {line}...)")


if __name__ == "__main__":
    unittest.main()
