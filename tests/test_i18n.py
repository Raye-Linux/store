# SPDX-License-Identifier: GPL-3.0-or-later
"""Every string the Store shows a person must be translatable, and French.

This is the pilot of the translation sweep: one test per repository that
fails on any visible string that is not translated. It is written to be
copied into the other repositories -- see ADOPTING THIS TEST below.

There are TWO defects. They fail for different reasons and neither one
catches the other.

  (a) A string is MARKED translatable and has no French.
      Two halves, because each hides the other:
        a1  the catalogue holds the string with an empty translation;
        a2  the string is in the code and not in the catalogue at all.
      a1 on its own passes happily while a hundred new strings sit in
      the code unseen: a catalogue can only be complete about what it
      has heard of.

  (b) A string is VISIBLE and never marked at all.
      No translation tool will ever report this one. msgfmt, the
      translators and every web platform read the catalogue, and the
      string is not in it. It is the common defect and the silent one.
      Two nets, because shape and position each catch what the other
      misses:
        b1  a literal sitting in a slot that renders text (text:,
            title:, message:...). Position is the evidence, so the
            filter is narrow on purpose and a lone word like
            "Uninstall" is caught.
        b2  a literal shaped like human prose, whatever slot it is in.
            Catches a sentence that reaches the screen through a
            property this file has never heard of. Position tells us
            nothing here, so the filter is wide: in this code base a
            single token ("ActionGame", "Inter", "Game") is an
            identifier, and only several real words count as prose.

  (c) The strings that live outside the catalogue: .desktop, .notifyrc
      and the polkit .policy. gettext never reads them; they carry
      their own Name[fr]= and xml:lang="fr".

WHAT THIS TEST CANNOT SEE, said plainly rather than left to be assumed:

  * A key built while the program runs -- i18n(prefix + suffix) -- is
    invisible to any reader of the source. Nothing here can follow it.
  * Text that arrives from outside: AppStream and Flathub (translated
    upstream, per language), and Raye's own data/extras.json, which is
    NOT translated. extras.json carries name, summary and description
    of the apps Raye offers itself; src/catalog.cpp (parseExtras) reads
    those keys flat. Giving them French needs language variants in the
    file AND a reader that picks the user's language -- a catalogue
    cannot help, and translating the file alone would show nothing.
  * Whether a translation is RIGHT, or still matches the English.
    "Afficher moins" could say anything; only a person reading the
    Store can tell.
  * The Windows plumbing, on purpose: windows/lib parses PE files, .lnk
    and the registry, and windows/cli/main.cpp prints English for
    whoever runs raye-windows at a terminal while Settings and the
    Start menu read it with --json. Neither links KF6::I18n. The part
    of the Windows layer a person looks at is windows/ui/, which does
    not exist yet; INTERFACE_DIRS already names it, so it is covered
    the day it lands.
  * A string marked, translated, and then shown in the wrong place.

ADOPTING THIS TEST IN ANOTHER REPOSITORY
  Edit the CONFIG block, nothing else:
    DOMAIN          the catalogue's name: po/<lang>/<DOMAIN>.po
    SOURCE_DIRS     every folder gettext must read
    INTERFACE_DIRS  the subset a person actually looks at (defect b)
    DATA_GLOBS      the .desktop/.notifyrc/.policy files it installs
    FLOORS          lower bounds, so an empty scan cannot pass
  Then make sure debian/control Build-Depends on gettext: a2 needs
  xgettext. raye-store does. raye-settings does NOT, although it
  already ships po/fr/raye-settings.po -- adopting this test there
  means adding gettext first.

🚨 WHY THE FLOORS AND THE POOL GUARDS EXIST. A test that walks a list
and asserts something about every item PASSES -- green, silent -- when
the list is empty: zero items, zero assertions, nothing said. (The same
shape under pytest is reported as a SKIP, "got empty parameter set",
which reads as harmless and is not.) A renamed folder, a regex that
stops matching, a catalogue cut down to its header: each one empties a
list, and this whole file would then report success while protecting
nothing. So every collection this file reasons about is asserted
non-empty, and the large ones against a floor well below what is there
today. The guards are tests of their own, in class Guards.
"""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

# --------------------------------------------------------------- CONFIG
ROOT = pathlib.Path(__file__).resolve().parent.parent
DOMAIN = "raye-store"
# everything gettext reads (defect a)
SOURCE_DIRS = ("src", "runner", "windows")
# what a person looks at (defect b); windows/ui does not exist yet
INTERFACE_DIRS = ("src", "runner", "windows/ui")
DATA_GLOBS = ("data/*.desktop", "data/krunner/*.desktop",
              "data/*.notifyrc", "data/*.policy")
QML_SUFFIXES = (".qml", ".js", ".mjs")
CODE_SUFFIXES = (".cpp", ".h")

# Today: 238 catalogue entries, 39 interface files, 1191 literals,
# 4 data files, 232 marked literals. A floor catches a collapse to
# nothing; it is not meant to be raised each time a string is added.
FLOORS = {"catalogue": 180, "interface_files": 25, "literals": 500,
          "marked": 150, "visible_slots": 20, "data_files": 4}

# Slots that put their value on the screen.
VISIBLE_SLOTS = {
    "text", "title", "subtitle", "message", "caption", "actionText",
    "placeholderText", "description", "notice", "toolTip", "displayText",
    "label", "header", "footer", "errorText", "hint", "progressText",
    "ToolTip.text", "Accessible.name", "Accessible.description",
}
# Slots whose value is a technical name, never prose: "Inter Display" is
# a typeface, so b2 must not ask anyone to translate it.
FONT_SLOTS = {"family", "font.family", "fontFamily"}

# Calls whose text is for a log or a developer, never for a person.
LOG_CALLS = ("qDebug", "qInfo", "qWarning", "qCritical", "qFatal",
             "qmlWarning", "qmlInfo", "Q_ASSERT", "Q_ASSERT_X")
# Calls that MATCH a string instead of showing it: the literal is a
# value being looked for, like the right-hand side of ===.
# Qt macros whose strings are names for the metaobject, not text.
META_CALLS = ("Q_CLASSINFO", "Q_PROPERTY", "Q_ENUM", "Q_ENUMS", "Q_INVOKABLE",
              "qRegisterMetaType", "qDBusRegisterMetaType", "Q_DECLARE_METATYPE",
              "setApplicationDomain", "setApplicationName", "setOrganizationName")
MATCH_CALLS = ("indexOf", "lastIndexOf", "startsWith", "endsWith",
               "includes", "contains", "localeCompare", "split", "search",
               "compare", "hasOwnProperty", "section",
               # C++: the needles classify() looks for in apt and flatpak output
               "any", "any_of", "none_of", "all_of", "matches")

# Every exception carries its reason. A long list here means a rule is
# wrong and should be fixed instead of papered over.
ALLOWED_UNMARKED = {
    # (path, literal): reason it is not shown to anyone
    ("src/installer.cpp", "no flatpak installation"):
        "goes into `message`, which Installer::classify() reads to pick a "
        "Reason; what a person then sees is reasonText(), which is i18n",
    ("src/installer.cpp", "%1 not found"):
        "goes into JobResult::detail, documented in installer.h as \"the "
        "backend's own words, for the logs\": classify() and qWarning read "
        "it, nobody shows it",
    ("src/softwaresources.cpp", "Raye Linux"):
        "the name of a software source, the same in every language "
        "(Ubuntu and Flathub next to it are not prose)",
}

KEYWORDS = [
    "-ki18n:1", "-ki18nc:1c,2", "-ki18np:1,2", "-ki18ncp:1c,2,3",
    "-ki18nd:2", "-ki18ndc:2c,3", "-ki18ndp:2,3", "-ki18ndcp:2c,3,4",
    "-kki18n:1", "-kki18nc:1c,2", "-kki18np:1,2", "-kki18ncp:1c,2,3",
    "-kxi18n:1", "-kxi18nc:1c,2", "-kxi18np:1,2", "-kxi18ncp:1c,2,3",
    "-kkxi18n:1", "-kkxi18nc:1c,2", "-kkxi18np:1,2", "-kkxi18ncp:1c,2,3",
    "-kI18N_NOOP:1", "-kI18NC_NOOP:1c,2",
]

PO = ROOT / "po" / "fr" / f"{DOMAIN}.po"


# ------------------------------------------------------- reading the .po
def parse_po(text):
    """(msgctxt, msgid) -> the whole entry, for the live entries.

    The value keeps every field as it was written: msgid, msgid_plural
    and one msgstr per plural form. msgid_plural is needed to know which
    placeholders a plural call can supply (see a3).

    Obsolete entries (#~) are left out: they describe strings the code
    no longer has, so an empty one is not a missing translation.
    """
    entries = {}
    cur = {}
    field = None

    def flush():
        if "msgid" in cur:
            entries[(cur.get("msgctxt", ""), cur["msgid"])] = dict(cur)
        cur.clear()

    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("#~"):
            flush()
            field = None
            continue
        if not line:
            field = None   # the blank line that separates two entries
            continue
        if line.startswith("#"):
            continue
        head = re.match(r'(msgctxt|msgid_plural|msgid|msgstr(?:\[\d+\])?)\s+"(.*)"$', line)
        if head:
            key, value = head.group(1), head.group(2)
            # a new entry begins at msgctxt, or at msgid without one
            if (key == "msgctxt" and cur) or (key == "msgid" and "msgid" in cur):
                flush()
            cur[key] = value
            field = key
            continue
        more = re.match(r'"(.*)"$', line)
        if more and field:
            cur[field] = cur.get(field, "") + more.group(1)
    flush()
    entries.pop(("", ""), None)   # the header: an empty msgid, not a string
    return entries


def french_forms(entry):
    """Every msgstr of an entry: one, or one per plural form."""
    return [v for k, v in sorted(entry.items()) if k.startswith("msgstr")]


def placeholders(text):
    return set(re.findall(r"%\d+", text))


def source_files(dirs):
    return sorted(
        p for d in dirs if (ROOT / d).exists()
        for p in (ROOT / d).rglob("*")
        if p.suffix in QML_SUFFIXES + CODE_SUFFIXES
    )


def xgettext_keys():
    """(msgctxt, msgid) for every string the code marks translatable."""
    files = [str(p.relative_to(ROOT)) for p in source_files(SOURCE_DIRS)]
    with tempfile.TemporaryDirectory() as tmp:
        listing = pathlib.Path(tmp) / "files"
        listing.write_text("\n".join(files), encoding="utf-8")
        pot = pathlib.Path(tmp) / "out.pot"
        subprocess.run(
            ["xgettext", "--from-code=UTF-8", "-C", "--kde", "-ci18n",
             *KEYWORDS, f"--package-name={DOMAIN}",
             "-f", str(listing), "-o", str(pot)],
            cwd=ROOT, check=True, capture_output=True,
        )
        return set(parse_po(pot.read_text(encoding="utf-8"))), files


# ------------------------------------------- reading the interface code
I18N_CALL = re.compile(r"\b[kx]{0,2}i18n[dcp]{0,4}\s*\(")
LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"' r"|'(?:[^'\\\n]|\\.)*'")
SLOT = re.compile(r"\b([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\s*:(?!:)")
COMPARE_BEFORE = re.compile(r"(===|!==|==|!=)\s*$")
COMPARE_AFTER = re.compile(r"^\s*(===|!==|==|!=)")


def strip_comments(text):
    """Comments go first: a comment quoting an English string must not
    fail a test whose job is to protect that documentation."""
    # the newlines of a block comment are kept, so the line numbers this
    # test reports are the line numbers of the file
    text = re.sub(r"/\*.*?\*/",
                  lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    out = []
    for line in text.split("\n"):
        cut, i, n, quote = None, 0, len(line), None
        while i < n:
            c = line[i]
            if quote:
                if c == "\\":
                    i += 1
                elif c == quote:
                    quote = None
            elif c in "\"'":
                quote = c
            elif c == "/" and i + 1 < n and line[i + 1] == "/":
                cut = i
                break
            i += 1
        out.append(line if cut is None else line[:cut])
    return "\n".join(out)


def call_spans(text, names):
    """Character ranges covered by a call to one of `names`."""
    if isinstance(names, re.Pattern):
        heads = [(m.start(), m.end() - 1) for m in names.finditer(text)]
    else:
        heads = [(m.start(), m.end() - 1)
                 for m in re.finditer(r"\b(?:%s)\s*\(" % "|".join(names), text)]
    spans, n = [], len(text)
    for begin, paren in heads:
        i, depth = paren, 0
        while i < n:
            c = text[i]
            if c in "\"'":
                quote, i = c, i + 1
                while i < n and text[i] != quote:
                    i += 2 if text[i] == "\\" else 1
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        spans.append((begin, i))
    return spans


class Found:
    """One string literal, where it is and what it lands in."""
    __slots__ = ("path", "line", "value", "slot")

    def __init__(self, path, line, value, slot):
        self.path, self.line, self.value, self.slot = path, line, value, slot

    def __str__(self):
        return f"{self.path}:{self.line}: {self.slot or '(no slot)'}: {self.value!r}"


def scan_interface():
    """Every literal of the interface code, with its slot, split into
    the ones a translation call covers and the ones it does not."""
    marked, unmarked, files = [], [], []
    for path in source_files(INTERFACE_DIRS):
        rel = str(path.relative_to(ROOT))
        files.append(rel)
        text = strip_comments(path.read_text(encoding="utf-8"))
        translated = call_spans(text, I18N_CALL)
        ignored = (call_spans(text, LOG_CALLS) + call_spans(text, MATCH_CALLS)
                   + call_spans(text, META_CALLS))
        slots = [(m.start(), m.group(1)) for m in SLOT.finditer(text)]
        for m in LITERAL.finditer(text):
            at, end = m.start(), m.end()
            slot = ""
            for pos, name in slots:
                if pos >= at:
                    break
                slot = name
            item = Found(rel, text.count("\n", 0, at) + 1, m.group(0)[1:-1], slot)
            if any(a <= at <= b for a, b in translated):
                marked.append(item)
                continue
            if any(a <= at <= b for a, b in ignored):
                continue
            # a literal being compared is a value, not something shown
            if COMPARE_BEFORE.search(text[max(0, at - 40):at]) or \
               COMPARE_AFTER.match(text[end:end + 8]):
                continue
            unmarked.append(item)
    return marked, unmarked, files


# A literal that is plainly not text, whatever slot it sits in.
NOT_TEXT = [
    r"^\s*$",                       # empty, blank
    r"^.$",                         # one character
    r"^[^A-Za-z]*$",                # punctuation, digits, %1, arrows
    r"^#[0-9a-fA-F]{3,8}$",         # hex colour
    r"^[a-z][a-z0-9+.-]*:(//|$)",   # https://, appstream:, file://
    r"/",                           # a path, or a fragment of one
    r"^\.[A-Za-z0-9]+$",            # .svg
    r"[<>]",                        # markup: <a href=", </a>
]
NOT_TEXT = [re.compile(p) for p in NOT_TEXT]

# A single token is an identifier here, never a sentence. Prose also
# never carries the punctuation of a regex, a Windows path or a
# key=value line: those are what a machine matches, not what a person
# reads.
NOT_PROSE = [
    r"^[A-Za-z_]\w*$",                   # Office, Inter, star, Game
    r"^-?[a-z0-9]+(-[a-z0-9]+)*$",       # -filled, kebab-case
    r"^[A-Za-z_][\w-]*(\.[\w+-]+)+$",    # org.jitsi.jitsi-meet
    r"^[A-Za-z][\w.]*-[\w.]+$",          # Apache-2.0, LicenseRef-proprietary
    r"^(Ctrl|Alt|Shift|Meta)\+",         # Ctrl+F
    r"[\\|=^$*\[\]{}]",                  # regex, glob, Windows path, Key=value
]
NOT_PROSE = [re.compile(p) for p in NOT_PROSE]
WORD = re.compile(r"[A-Za-z]{2,}")


def not_text(value):
    return any(p.search(value) for p in NOT_TEXT)


def looks_like_prose(value):
    """Several real words: what a person reads, not what a machine matches."""
    if not_text(value) or any(p.search(value) for p in NOT_PROSE):
        return False
    return len(WORD.findall(value)) >= 2


def allowed(item):
    return (item.path, item.value) in ALLOWED_UNMARKED


def lines_of(items):
    return sorted(str(i) for i in items)


CATALOGUE = parse_po(PO.read_text(encoding="utf-8")) if PO.exists() else {}
MARKED, UNMARKED, FILES = scan_interface()
IN_VISIBLE_SLOT = [i for i in MARKED + UNMARKED
                   if i.path.endswith(QML_SUFFIXES) and i.slot in VISIBLE_SLOTS]


class Guards(unittest.TestCase):
    """What stops this file from passing by having looked at nothing."""

    def test_the_catalogue_was_read(self):
        self.assertTrue(PO.exists(), f"{PO} is missing: French is the first language")
        self.assertGreaterEqual(
            len(CATALOGUE), FLOORS["catalogue"],
            f"only {len(CATALOGUE)} entries read from po/fr/{DOMAIN}.po: the "
            f"catalogue or parse_po collapsed, and defect (a) would pass on nothing")

    def test_the_interface_was_scanned(self):
        self.assertGreaterEqual(
            len(FILES), FLOORS["interface_files"],
            f"only {len(FILES)} interface files found under {INTERFACE_DIRS}")
        self.assertGreaterEqual(
            len(MARKED) + len(UNMARKED), FLOORS["literals"],
            f"the literal scanner found {len(MARKED) + len(UNMARKED)} literals: "
            f"LITERAL no longer matches")

    def test_translation_calls_are_recognised(self):
        # If I18N_CALL stopped matching, every marked string would look
        # unmarked and b1/b2 would drown in noise. If it matched too much,
        # they would go quiet instead -- that is the dangerous direction.
        self.assertGreaterEqual(
            len(MARKED), FLOORS["marked"],
            f"only {len(MARKED)} literals seen inside a translation call")

    def test_the_slot_reader_works(self):
        # b1 says nothing at all if no literal is ever attributed to a
        # slot that shows text.
        self.assertGreaterEqual(
            len(IN_VISIBLE_SLOT), FLOORS["visible_slots"],
            f"only {len(IN_VISIBLE_SLOT)} literals landed in one of "
            f"{sorted(VISIBLE_SLOTS)}: SLOT or VISIBLE_SLOTS is wrong, and b1 "
            f"is asserting about an empty set")

    def test_b2_has_something_to_look_at(self):
        pool = [i for i in UNMARKED if i.slot not in FONT_SLOTS]
        self.assertTrue(
            pool, "no unmarked literal outside a font slot: b2 is asserting "
                  "about an empty set")

    def test_the_data_files_are_all_checked(self):
        found = sorted({str(p.relative_to(ROOT))
                        for g in DATA_GLOBS for p in ROOT.glob(g)})
        self.assertGreaterEqual(
            len(found), FLOORS["data_files"],
            f"only {len(found)} data files matched {DATA_GLOBS}: {found}")


class MarkedButNotFrench(unittest.TestCase):
    """Defect (a): the string is translatable and the French is missing."""

    def test_a1_every_catalogue_entry_has_french(self):
        empty = sorted(
            f"{ctx + '|' if ctx else ''}{msgid}"
            for (ctx, msgid), entry in CATALOGUE.items()
            if not french_forms(entry) or any(not s for s in french_forms(entry))
        )
        self.assertEqual(
            [], empty,
            f"{len(empty)} string(s) marked translatable with no French in "
            f"po/fr/{DOMAIN}.po:\n" + "\n".join("    " + e for e in empty))

    def test_a2_the_catalogue_covers_the_code(self):
        self.assertTrue(
            shutil.which("xgettext"),
            "xgettext is missing: install gettext, which this package already "
            "Build-Depends on. Skipping instead would hide every string the "
            "code has gained and the catalogue never heard of.")
        in_code, scanned = xgettext_keys()
        self.assertGreaterEqual(
            len(scanned), FLOORS["interface_files"],
            f"xgettext was given {len(scanned)} files: SOURCE_DIRS is wrong")
        self.assertGreaterEqual(
            len(in_code), FLOORS["catalogue"],
            f"xgettext returned {len(in_code)} strings: the keyword list is wrong, "
            f"and this check would pass on nothing")
        missing = sorted(f"{ctx + '|' if ctx else ''}{msgid}"
                         for ctx, msgid in in_code - set(CATALOGUE))
        self.assertEqual(
            [], missing,
            f"{len(missing)} string(s) marked in the code and absent from "
            f"po/fr/{DOMAIN}.po: they reach the screen in English and no "
            f"translator is ever told. Rebuild the catalogue, then translate:\n"
            + "\n".join("    " + m for m in missing))


    def test_a3_french_fills_only_placeholders_the_code_supplies(self):
        """A French form using %2 where the call passes one argument shows
        "%2" to the reader. msgfmt does not check this: KDE's %1 is not a
        printf placeholder, so --check-format sees nothing to check."""
        bad = []
        for (ctx, msgid), entry in CATALOGUE.items():
            # a plural call supplies the placeholders of either form
            supplied = placeholders(msgid) | placeholders(entry.get("msgid_plural", ""))
            for key in sorted(k for k in entry if k.startswith("msgstr")):
                extra = placeholders(entry[key]) - supplied
                if extra:
                    bad.append(f"{msgid!r} {key}={entry[key]!r} uses "
                               f"{sorted(extra)}, which nothing supplies")
        self.assertEqual(
            [], sorted(bad),
            f"{len(bad)} French string(s) asking for an argument the code does "
            f"not pass:\n" + "\n".join("    " + b for b in sorted(bad)))


class VisibleButNeverMarked(unittest.TestCase):
    """Defect (b): no translation tool will ever report these."""

    def test_b1_a_literal_in_a_slot_that_shows_text(self):
        bad = [i for i in UNMARKED
               if i.path.endswith(QML_SUFFIXES) and i.slot in VISIBLE_SLOTS
               and not not_text(i.value) and not allowed(i)]
        self.assertEqual(
            [], lines_of(bad),
            f"{len(bad)} literal(s) dropped straight into a slot that renders "
            f'text. Wrap each one in i18nc("@context", ...) and add it to '
            f"po/fr/{DOMAIN}.po:\n" + "\n".join("    " + l for l in lines_of(bad)))

    def test_b2_prose_outside_any_translation_call(self):
        bad = [i for i in UNMARKED
               if i.slot not in FONT_SLOTS and looks_like_prose(i.value) and not allowed(i)]
        self.assertEqual(
            [], lines_of(bad),
            f"{len(bad)} string(s) shaped like a sentence and never marked. If a "
            f"person reads it, wrap it in i18n; if nobody does, add it to "
            f"ALLOWED_UNMARKED with the reason:\n"
            + "\n".join("    " + l for l in lines_of(bad)))


class Reachable(unittest.TestCase):
    """A test the package build never runs is a test the image can ship
    without respecting. Measured on another Raye repository this week:
    debian/rules listed the test folders by hand, missed two, and 346
    tests were never run by the build -- green, with nothing executed.
    So the chain that reaches this very file is asserted, not assumed.

    ⚠️ One link is outside this repository and cannot be asserted from
    here: iso/tools/raye_tests_suites.py names the store python tests by
    hand -- ("extra", "feeds", "plasma_discover", "version") -- instead
    of reading the glob below. Until "i18n" is added there, `raye-tests
    --repo store` does not run this file, although the package build
    does."""

    def test_cmake_registers_every_python_test_by_glob(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            "tests/test_*.py", cmake,
            "CMakeLists.txt no longer globs tests/test_*.py: a test added here "
            "would never be registered, and ctest would not miss it out loud")
        here = pathlib.Path(__file__).name
        self.assertTrue(
            here.startswith("test_") and here.endswith(".py"),
            f"{here} does not match tests/test_*.py, so nothing runs it")

    def test_the_package_build_runs_the_tests(self):
        rules = (ROOT / "debian/rules").read_text(encoding="utf-8")
        self.assertNotIn(
            "override_dh_auto_test", rules,
            "debian/rules overrides dh_auto_test: check by hand that it still "
            "reaches tests/test_*.py, because the default ctest run is gone")
        self.assertIn(
            "BUILD_TESTING=ON", rules,
            "debian/rules no longer configures with BUILD_TESTING=ON: the "
            "if(BUILD_TESTING) block of CMakeLists.txt registers nothing")
        self.assertIn(
            "CTEST_NO_TESTS_ACTION", rules,
            "debian/rules lost CTEST_NO_TESTS_ACTION=error: ctest exits 0 when "
            "it finds no test at all, so the build would go green empty")


class StringsOutsideTheCatalogue(unittest.TestCase):
    """Defect (c): the files gettext never reads."""

    DESKTOP_KEYS = ("Name", "GenericName", "Comment", "Keywords")

    @staticmethod
    def has_french(text, key):
        return re.search(r"(?m)^%s\[fr\]\s*=\s*\S" % re.escape(key), text)

    def files_matching(self, suffix):
        found = sorted(p for g in DATA_GLOBS if g.endswith(suffix)
                       for p in ROOT.glob(g))
        self.assertTrue(found, f"no {suffix} file found: DATA_GLOBS is wrong")
        return found

    def test_desktop_entries_have_french(self):
        missing = []
        for path in self.files_matching(".desktop"):
            text = path.read_text(encoding="utf-8")
            for key in self.DESKTOP_KEYS:
                if re.search(r"(?m)^%s\s*=\s*\S" % key, text) and not self.has_french(text, key):
                    missing.append(f"{path.relative_to(ROOT)}: {key}[fr]= is missing")
        self.assertEqual([], sorted(missing),
                         f"{len(missing)} untranslated desktop key(s):\n"
                         + "\n".join("    " + m for m in sorted(missing)))

    def test_notifyrc_has_french(self):
        missing = []
        for path in self.files_matching(".notifyrc"):
            text = path.read_text(encoding="utf-8")
            # one section at a time: each [Event/x] has its own Name=
            bounds = [m.start() for m in re.finditer(r"(?m)^\[", text)] + [len(text)]
            for start, stop in zip(bounds, bounds[1:]):
                block = text[start:stop]
                head = block.splitlines()[0]
                for key in ("Name", "Comment"):
                    if re.search(r"(?m)^%s\s*=\s*\S" % key, block) and not self.has_french(block, key):
                        missing.append(f"{path.relative_to(ROOT)} {head}: {key}[fr]= is missing")
        self.assertEqual([], sorted(missing),
                         f"{len(missing)} untranslated notification string(s):\n"
                         + "\n".join("    " + m for m in sorted(missing)))

    def test_polkit_policy_has_french(self):
        missing = []
        for path in self.files_matching(".policy"):
            text = path.read_text(encoding="utf-8")
            for tag in ("description", "message"):
                plain = len(re.findall(r"<%s>" % tag, text))
                french = len(re.findall(r'<%s xml:lang="fr">' % tag, text))
                if plain > french:
                    missing.append(
                        f"{path.relative_to(ROOT)}: {plain - french} <{tag}> "
                        f'without <{tag} xml:lang="fr">')
        self.assertEqual([], sorted(missing),
                         f"{len(missing)} untranslated polkit string(s):\n"
                         + "\n".join("    " + m for m in sorted(missing)))


if __name__ == "__main__":
    unittest.main(verbosity=1)
