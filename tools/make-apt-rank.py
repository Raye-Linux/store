#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes data/apt-rank.tsv: how many systems have each archive package,
from the popularity contest, so the Store can rank archive apps next to
Flathub ones. Run once per release:

    python3 -I tools/make-apt-rank.py [--input by_inst.gz] [--output data/apt-rank.tsv]
"""
import argparse
import gzip
import io
import os
import pathlib
import re
import sys
import tempfile
import urllib.request

URL = "https://popcon.debian.org/by_inst.gz"
TIMEOUT = 30
# packages under this count are noise for the Store's order
MIN_INSTALLS = 200
PACKAGE = re.compile(r"^[a-z0-9][a-z0-9+.-]+$")
# never an app the Store lists: keeps the file small
NOT_APPS = re.compile(
    r"^(lib|python3?-|fonts?-|linux-|firmware-|golang-|node-|ruby-|perl-|r-cran-|texlive-)"
    r"|-(dev|doc|dbg|dbgsym|data|common|l10n|locale)(-|$)")


def parse(text):
    """(package, installs) pairs, most installed first."""
    rows = []
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) < 3:
            continue
        name, installs = fields[1], fields[2]
        if not PACKAGE.match(name) or not installs.isdigit():
            continue
        count = int(installs)
        if count >= MIN_INSTALLS and not NOT_APPS.search(name):
            rows.append((name, count))
    rows.sort(key=lambda row: (-row[1], row[0]))
    return rows


def render(rows):
    out = io.StringIO()
    out.write("# package\tinstalls (popularity contest, by installs)\n")
    for name, count in rows:
        out.write(f"{name}\t{count}\n")
    return out.getvalue()


def fetch(url):
    with urllib.request.urlopen(url, timeout=TIMEOUT) as response:
        return response.read()


def main(argv=None):
    here = pathlib.Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--input", help="a saved by_inst or by_inst.gz instead of the download")
    ap.add_argument("--output", default=str(here / "data" / "apt-rank.tsv"))
    args = ap.parse_args(argv)

    raw = pathlib.Path(args.input).read_bytes() if args.input else fetch(URL)
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    rows = parse(raw.decode("utf-8", "replace"))
    if not rows:
        print("make-apt-rank: nothing usable in the input", file=sys.stderr)
        return 1
    target = pathlib.Path(args.output)
    fd, tmp = tempfile.mkstemp(dir=target.parent, prefix=".apt-rank.")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(render(rows))
    os.chmod(tmp, 0o644)
    os.replace(tmp, target)
    print(f"make-apt-rank: {len(rows)} packages written to {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
