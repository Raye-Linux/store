# SPDX-License-Identifier: GPL-3.0-or-later
"""raye-store-games: Steam Play on for every game once the Store has
installed Steam, and nothing the person chose ever changed."""
import importlib.machinery
import importlib.util
import os
import pathlib
import tempfile
import unittest

HELPER = pathlib.Path(__file__).resolve().parent.parent / "src" / "raye-store-games"
loader = importlib.machinery.SourceFileLoader("raye_store_games", str(HELPER))
spec = importlib.util.spec_from_loader("raye_store_games", loader)
rsg = importlib.util.module_from_spec(spec)
loader.exec_module(rsg)

STEAM = "com.valvesoftware.Steam"

# what Steam itself writes after a first sign-in, cut down
WRITTEN_BY_STEAM = '''"InstallConfigStore"
{
\t"Software"
\t{
\t\t"Valve"
\t\t{
\t\t\t"Steam"
\t\t\t{
\t\t\t\t"AutoUpdateWindowEnabled"\t\t"0"
\t\t\t\t"SourceModInstallPath"\t\t"/home/a/.local/share/Steam/steamapps/sourcemods"
\t\t\t}
\t\t}
\t}
\t"Music"
\t{
\t\t"LocalLibrary"\t\t"// not a comment \\"quoted\\""
\t}
}
'''


def mapping(tree):
    steam = rsg.child(rsg.child(rsg.child(rsg.child(tree, "InstallConfigStore"), "Software"), "Valve"), "Steam")
    return rsg.child(steam, "CompatToolMapping")


class TestVdf(unittest.TestCase):
    def test_round_trip(self):
        tree = rsg.parse_vdf(WRITTEN_BY_STEAM)
        self.assertEqual(rsg.parse_vdf(rsg.dump_vdf(tree)), tree)
        music = rsg.child(rsg.child(tree, "InstallConfigStore"), "Music")
        self.assertEqual(rsg.child(music, "LocalLibrary"), '// not a comment "quoted"')

    def test_comments_and_bare_words(self):
        tree = rsg.parse_vdf('// top\n"a"\n{\n  b "c" // end of line\n  "d" { }\n}\n')
        self.assertEqual(tree, [("a", [("b", "c"), ("d", [])])])

    def test_keys_ignore_case(self):
        tree = rsg.parse_vdf('"Root" { "software" { } }')
        self.assertEqual(rsg.child(rsg.child(tree, "root"), "Software"), [])

    def test_broken_files_are_refused(self):
        for text in ('"a" {', '"a" "b" }', '"a', '{ "a" "b" }', '"a" { "b" }'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                rsg.parse_vdf(text)


class TestSteamPlay(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = pathlib.Path(self.tmp.name)
        self.config = self.home / ".var/app" / STEAM / ".local/share/Steam/config/config.vdf"
        self.state = self.home / "state" / "games-setup"

    def tearDown(self):
        self.tmp.cleanup()

    def setup(self):
        return rsg.setup(STEAM, home=self.home, state=self.state)

    def test_new_install_gets_proton_for_every_game(self):
        self.assertEqual(self.setup(), "done")
        tree = rsg.parse_vdf(self.config.read_text())
        default = rsg.child(mapping(tree), "0")
        self.assertEqual(rsg.child(default, "name"), "proton_experimental")
        self.assertEqual(rsg.child(default, "config"), "")
        self.assertEqual(rsg.child(default, "priority"), "75")
        self.assertIn(STEAM, self.state.read_text().split())

    def test_keeps_what_steam_wrote(self):
        self.config.parent.mkdir(parents=True)
        self.config.write_text(WRITTEN_BY_STEAM)
        self.assertEqual(self.setup(), "done")
        tree = rsg.parse_vdf(self.config.read_text())
        steam = rsg.child(rsg.child(rsg.child(rsg.child(tree, "InstallConfigStore"), "Software"), "Valve"), "Steam")
        self.assertEqual(rsg.child(steam, "AutoUpdateWindowEnabled"), "0")
        self.assertEqual(rsg.child(rsg.child(mapping(tree), "0"), "name"), "proton_experimental")
        self.assertEqual(rsg.child(rsg.child(rsg.child(tree, "InstallConfigStore"), "Music"), "LocalLibrary"),
                         '// not a comment "quoted"')

    def test_never_changes_a_chosen_tool(self):
        chosen = WRITTEN_BY_STEAM.replace('"AutoUpdateWindowEnabled"',
                                          '"CompatToolMapping" { "0" { "name" "proton_9" "config" "" "priority" "75" } }\n'
                                          '"AutoUpdateWindowEnabled"')
        self.config.parent.mkdir(parents=True)
        self.config.write_text(chosen)
        self.assertEqual(self.setup(), "kept")
        self.assertEqual(self.config.read_text(), chosen)

    def test_other_games_mapped_get_the_default_too(self):
        one_game = WRITTEN_BY_STEAM.replace('"AutoUpdateWindowEnabled"',
                                            '"CompatToolMapping" { "730" { "name" "proton_8" "config" "" "priority" "250" } }\n'
                                            '"AutoUpdateWindowEnabled"')
        self.config.parent.mkdir(parents=True)
        self.config.write_text(one_game)
        self.assertEqual(self.setup(), "done")
        tools = mapping(rsg.parse_vdf(self.config.read_text()))
        self.assertEqual(rsg.child(rsg.child(tools, "730"), "name"), "proton_8")
        self.assertEqual(rsg.child(rsg.child(tools, "0"), "name"), "proton_experimental")

    def test_once_only(self):
        self.assertEqual(self.setup(), "done")
        # the person turned it off in Steam; a second install leaves that
        self.config.write_text('"InstallConfigStore" { }')
        self.assertEqual(self.setup(), "kept")
        self.assertEqual(self.config.read_text(), '"InstallConfigStore" { }')

    def test_unreadable_file_left_alone(self):
        self.config.parent.mkdir(parents=True)
        self.config.write_text('"InstallConfigStore" { "Software" {')
        self.assertEqual(self.setup(), "kept")
        self.assertEqual(self.config.read_text(), '"InstallConfigStore" { "Software" {')
        self.assertFalse(self.state.exists())

    def test_link_is_not_followed(self):
        target = self.home / "elsewhere.vdf"
        target.write_text("x")
        self.config.parent.mkdir(parents=True)
        os.symlink(target, self.config)
        self.assertEqual(self.setup(), "kept")
        self.assertEqual(target.read_text(), "x")

    def test_other_apps_untouched(self):
        self.assertEqual(rsg.setup("org.gimp.GIMP", home=self.home, state=self.state), "unknown")
        self.assertFalse((self.home / ".var").exists())

    def test_command_line(self):
        env = {"HOME": str(self.home), "XDG_STATE_HOME": str(self.home / "st")}
        self.assertEqual(rsg.main(["setup", STEAM], env), 0)
        self.assertTrue(self.config.is_file())
        self.assertEqual(rsg.main(["setup"], env), 2)
        self.assertEqual(rsg.main(["explode", STEAM], env), 2)


if __name__ == "__main__":
    unittest.main()
