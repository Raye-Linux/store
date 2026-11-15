# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for raye-store-extra, without network or root."""
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
import pathlib
import tempfile
import unittest

HELPER = pathlib.Path(__file__).resolve().parent.parent / "src" / "raye-store-extra"
loader = importlib.machinery.SourceFileLoader("raye_store_extra", str(HELPER))
spec = importlib.util.spec_from_loader("raye_store_extra", loader)
rse = importlib.util.module_from_spec(spec)
loader.exec_module(rse)

PAYLOAD = b"a pretend deb"
GOOD = hashlib.sha256(PAYLOAD).hexdigest()


class FakeResponse(io.BytesIO):
    def __init__(self, data, length=True):
        super().__init__(data)
        self.headers = {"Content-Length": str(len(data))} if length else {}

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class FakeOpener:
    def __init__(self, data, length=True):
        self.data = data
        self.length = length

    def open(self, url, timeout=0):
        return FakeResponse(self.data, self.length)


def catalog(tmp, apps):
    path = os.path.join(tmp, "extras.json")
    with open(path, "w") as f:
        json.dump({"apps": apps}, f)
    return path


class TestExtra(unittest.TestCase):
    def test_only_packages_in_the_catalog(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = catalog(tmp, [{"package": "artcraft-launcher", "url": "https://x/a.deb", "sha256": GOOD}])
            self.assertEqual(rse.entry_for("artcraft-launcher", path)["sha256"], GOOD)
            with self.assertRaises(LookupError):
                rse.entry_for("bash", path)

    def test_refuses_http_and_missing_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = catalog(tmp, [{"package": "a", "url": "http://x/a.deb", "sha256": GOOD},
                                 {"package": "b", "url": "https://x/b.deb", "sha256": "12"}])
            with self.assertRaises(ValueError):
                rse.entry_for("a", path)
            with self.assertRaises(ValueError):
                rse.entry_for("b", path)

    def test_download_kept_when_the_hash_matches(self):
        with tempfile.TemporaryDirectory() as tmp:
            deb = rse.download("https://x/a.deb", GOOD, tmp, FakeOpener(PAYLOAD))
            with open(deb, "rb") as f:
                self.assertEqual(f.read(), PAYLOAD)

    def test_download_thrown_away_when_the_hash_differs(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                rse.download("https://x/a.deb", GOOD, tmp, FakeOpener(b"something else"))
            self.assertEqual(os.listdir(tmp), [])

    def test_download_reports_progress(self):
        data = b"x" * (5 * 65536)
        lines = []
        with tempfile.TemporaryDirectory() as tmp:
            rse.download("https://x/a.deb", hashlib.sha256(data).hexdigest(), tmp, FakeOpener(data),
                         report=lines.append)
        self.assertIn(f"bytes {len(data)} {len(data)}", lines)
        self.assertEqual(lines[-1], "progress 100")
        percents = [int(l.split()[1]) for l in lines if l.startswith("progress ")]
        self.assertEqual(percents, sorted(percents))
        self.assertEqual(len(percents), len(set(percents)))  # once per value

    def test_download_without_length_reports_bytes_only(self):
        lines = []
        with tempfile.TemporaryDirectory() as tmp:
            rse.download("https://x/a.deb", GOOD, tmp, FakeOpener(PAYLOAD, length=False), report=lines.append)
        self.assertEqual(lines, [f"bytes {len(PAYLOAD)} 0"])

    def test_announced_size_too_large(self):
        class Huge(FakeOpener):
            def open(self, url, timeout=0):
                r = FakeResponse(b"x")
                r.headers = {"Content-Length": str(rse.MAX_SIZE + 1)}
                return r
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                rse.download("https://x/a.deb", GOOD, tmp, Huge(b""))
            self.assertEqual(os.listdir(tmp), [])

    def test_cancel_on_stdin(self):
        read_end, write_end = os.pipe()
        try:
            os.write(write_end, b"cancel\n")
            watch = rse.CancelWatch(read_end)
            with tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(rse.Cancelled):
                    rse.download("https://x/a.deb", GOOD, tmp, FakeOpener(PAYLOAD), watch=watch)
                self.assertEqual(os.listdir(tmp), [])
        finally:
            os.close(read_end)
            os.close(write_end)

    def test_cancel_watch_ignores_other_words_and_eof(self):
        read_end, write_end = os.pipe()
        watch = rse.CancelWatch(read_end)
        self.assertFalse(watch.requested())
        os.write(write_end, b"hello\ncan")
        self.assertFalse(watch.requested())
        os.write(write_end, b"cel\n")
        self.assertTrue(watch.requested())
        os.close(write_end)
        watch2_read, watch2_write = os.pipe()
        os.close(watch2_write)
        eof = rse.CancelWatch(watch2_read)
        self.assertFalse(eof.requested())  # the Store went away: finish the job
        self.assertFalse(eof.requested())
        os.close(read_end)
        os.close(watch2_read)
        self.assertFalse(rse.CancelWatch(None).requested())

    def test_apt_cache_emptied_after_install(self):
        for code in (0, 100):
            calls = []

            def run(argv, **kwargs):
                calls.append(argv)

                class Done:
                    returncode = code if argv[1] == "install" else 0
                return Done()
            self.assertEqual(rse.install_deb("/var/cache/raye-store/x.deb", {}, run=run), code)
            self.assertEqual(calls[0][:2], ["apt-get", "install"])
            self.assertEqual(calls[-1], ["apt-get", "clean"])

    def test_bad_arguments(self):
        self.assertEqual(rse.main(["x", "install", "rm -rf"]), 2)
        self.assertEqual(rse.main(["x", "explode", "a"]), 2)
        self.assertEqual(rse.main(["x"]), 2)


if __name__ == "__main__":
    unittest.main()
