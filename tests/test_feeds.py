"""raye-store-feeds against a fake Flathub and ODRS on 127.0.0.1.

Run: python3 -I -B tests/test_feeds.py
"""

import http.server
import importlib.machinery
import importlib.util
import json
import os
import shutil
import socket
import stat
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPT = os.path.join(ROOT, "src", "raye-store-feeds")
DATA = os.path.join(HERE, "data", "feeds")
# what the Store's own tests read (written to match src/catalog.cpp)
STORE_FIXTURES = os.path.join(HERE, "fixtures", "feeds")

loader = importlib.machinery.SourceFileLoader("raye_store_feeds", SCRIPT)
spec = importlib.util.spec_from_loader(loader.name, loader)
feeds = importlib.util.module_from_spec(spec)
loader.exec_module(feeds)
feeds.log = lambda message: None


class FakeServer:
    """Serves tests/data/feeds by path; misbehaves on request."""

    def __init__(self):
        self.broken = {}     # path prefix -> "500" | "garbage" | "slow" | "huge" | "drip"
        self.requests = []
        server = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                server.requests.append((self.path, self.headers.get("User-Agent")))
                url = urllib.parse.urlsplit(self.path)
                query = urllib.parse.parse_qs(url.query)
                for prefix, how in server.broken.items():
                    if url.path.startswith(prefix):
                        return self.misbehave(how)
                name = self.file_for(url.path, query)
                path = os.path.join(DATA, name) if name else None
                if not path or not os.path.isfile(path):
                    self.send_response(404)
                    self.end_headers()
                    return
                with open(path, "rb") as f:
                    body = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def file_for(self, path, query):
                parts = path.strip("/").split("/")
                if parts[:2] == ["api", "collection"] and len(parts) == 3:
                    if query.get("per_page") != ["250"]:
                        return None
                    return f"{parts[2]}-{query.get('page', ['1'])[0]}.json"
                if parts[:2] == ["api", "app-picks"] and len(parts) == 4:
                    return f"{parts[2]}.json"
                if parts == ["odrs", "ratings"]:
                    return "ratings.json"
                return None

            def misbehave(self, how):
                if how == "500":
                    self.send_response(500)
                    self.end_headers()
                    return
                self.send_response(200)
                if how == "huge":
                    self.send_header("Content-Length", str(64 * 1024 * 1024))
                self.end_headers()
                try:
                    if how == "garbage":
                        self.wfile.write(b'{"hits": [ {"app_id": "org.')
                    elif how == "slow":
                        time.sleep(1.5)
                        self.wfile.write(b'{"hits": []}')
                    elif how == "drip":
                        # every byte in time, the whole too late
                        for _ in range(30):
                            self.wfile.write(b" ")
                            self.wfile.flush()
                            time.sleep(0.1)
                    elif how == "huge":
                        self.wfile.write(b"[" + b"0," * 1024)
                except OSError:
                    pass

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.httpd.daemon_threads = True
        self.base = f"http://127.0.0.1:{self.httpd.server_address[1]}"
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


class FeedsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = FakeServer()

    @classmethod
    def tearDownClass(cls):
        cls.server.close()

    def setUp(self):
        self.server.broken = {}
        self.server.requests = []
        self.tmp = tempfile.mkdtemp(prefix="raye-feeds-test-")
        self.out = os.path.join(self.tmp, "out")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_fetch(self, timeout=2.0, now=1793400000):
        return feeds.fetch(self.out, api=self.server.base + "/api",
                           odrs=self.server.base + "/odrs", today="2026-10-31",
                           timeout=timeout, now=now)

    def load(self, name, directory=None):
        with open(os.path.join(directory or self.out, name), encoding="utf-8") as f:
            return json.load(f)

    def seed(self, name, data):
        os.makedirs(self.out, exist_ok=True)
        with open(os.path.join(self.out, name), "w", encoding="utf-8") as f:
            json.dump(data, f)

    # --- fetch -------------------------------------------------------------

    def test_writes_every_file(self):
        self.assertEqual(self.run_fetch(), 6)
        self.assertEqual(sorted(os.listdir(self.out)), sorted(feeds.FILES))
        for name in os.listdir(self.out):
            mode = os.stat(os.path.join(self.out, name)).st_mode
            self.assertEqual(stat.S_IMODE(mode), 0o644, name)

    def test_two_pages_in_order_without_repeats(self):
        self.run_fetch()
        ids = [e["id"] for e in self.load("popular.json")]
        self.assertEqual(ids, ["org.mozilla.firefox", "org.videolan.VLC",
                               "com.valvesoftware.Steam", "org.gimp.GIMP",
                               "org.kde.krita", "org.kde.kdenlive"])
        pages = [p for p, _ in self.server.requests if "/collection/popular" in p]
        self.assertEqual(len(pages), 2)
        self.assertIn("page=2", pages[1])

    def test_stops_after_the_last_page(self):
        self.run_fetch()
        pages = [p for p, _ in self.server.requests if "/collection/trending" in p]
        self.assertEqual(len(pages), 1)

    def test_entry_fields(self):
        self.run_fetch()
        firefox = self.load("popular.json")[0]
        self.assertEqual(firefox, {
            "id": "org.mozilla.firefox", "installs_last_month": 412000,
            "trending": 1.4, "favorites": 900, "verified": True,
            "updated_at": 1793318400, "added_at": 1500000000})
        steam = self.load("popular.json")[2]
        self.assertEqual(steam["trending"], -2.5)  # falling is a value too

    def test_odd_values_are_cleaned(self):
        self.run_fetch()
        gimp = next(e for e in self.load("popular.json") if e["id"] == "org.gimp.GIMP")
        self.assertIsNone(gimp["trending"])          # was true
        self.assertEqual(gimp["favorites"], 0)       # was null
        self.assertFalse(gimp["verified"])           # was "yes"
        self.assertEqual(gimp["updated_at"], 1793181600)  # ISO text
        self.assertEqual(gimp["added_at"], 1793300000)    # milliseconds

    def test_bad_ids_are_dropped(self):
        self.run_fetch()
        for name in ("popular.json", "trending.json"):
            for e in self.load(name):
                self.assertRegex(e["id"], r"^[A-Za-z0-9_.+-]+$")
                self.assertNotIn("/", e["id"])
        self.assertNotIn("bad id", self.load("picks.json")["week"])

    def test_same_shape_the_store_reads(self):
        self.run_fetch()
        with open(os.path.join(STORE_FIXTURES, "popular.json"), encoding="utf-8") as f:
            store_keys = set().union(*(e.keys() for e in json.load(f) if isinstance(e, dict)))
        for name in feeds.COLLECTIONS:
            for e in self.load(f"{name}.json"):
                self.assertEqual(set(e) - store_keys, set(), name)
        picks = self.load("picks.json")
        self.assertEqual(set(picks), {"day", "week"})

    def test_picks(self):
        self.run_fetch()
        self.assertEqual(self.load("picks.json"), {
            "day": "org.kde.krita",
            "week": ["org.gimp.GIMP", "org.kde.kdenlive", "org.videolan.VLC"]})
        paths = [p for p, _ in self.server.requests if "app-picks" in p]
        self.assertTrue(all(p.endswith("/2026-10-31") for p in paths), paths)

    def test_a_failed_pick_keeps_its_old_value(self):
        self.seed("picks.json", {"day": "org.old.Day", "week": ["org.old.Week"]})
        self.server.broken = {"/api/app-picks/app-of-the-day": "500"}
        self.run_fetch()
        self.assertEqual(self.load("picks.json"), {
            "day": "org.old.Day",
            "week": ["org.gimp.GIMP", "org.kde.kdenlive", "org.videolan.VLC"]})

    def test_ratings(self):
        self.run_fetch()
        ratings = self.load("ratings.json")
        self.assertEqual(sorted(ratings), ["broken", "firefox", "org.gimp.GIMP", "org.videolan.VLC"])
        # "x" and "x.desktop": the one with more votes wins
        self.assertEqual(ratings["org.gimp.GIMP"]["total"], 70)
        self.assertEqual(ratings["org.gimp.GIMP"]["star5"], 10)
        self.assertEqual(ratings["org.videolan.VLC"]["star0"], 0)
        self.assertEqual(ratings["firefox"]["star5"], 0)   # was text
        self.assertEqual(ratings["firefox"]["star4"], 0)   # was below zero

    def test_meta_times(self):
        self.seed("meta.json", {"ratings": 1700000000, "stray": "x"})
        self.server.broken = {"/odrs": "500"}
        self.run_fetch(now=1793400000)
        meta = self.load("meta.json")
        self.assertEqual(meta["popular"], 1793400000)
        self.assertEqual(meta["picks"], 1793400000)
        self.assertEqual(meta["ratings"], 1700000000)  # failed: old time kept
        self.assertNotIn("stray", meta)

    def test_error_keeps_the_last_good_file(self):
        old = [{"id": "org.old.App", "installs_last_month": 1}]
        self.seed("trending.json", old)
        self.seed("ratings.json", {"org.old.App": {"star5": 3, "total": 3}})
        self.server.broken = {"/api/collection/trending": "500", "/odrs": "garbage"}
        self.assertEqual(self.run_fetch(), 4)
        self.assertEqual(self.load("trending.json"), old)
        self.assertEqual(self.load("ratings.json"), {"org.old.App": {"star5": 3, "total": 3}})
        self.assertEqual(len(self.load("popular.json")), 6)

    def test_second_page_failing_keeps_the_last_file(self):
        old = [{"id": "org.old.App", "installs_last_month": 1}]
        self.seed("popular.json", old)
        self.server.broken = {"/api/collection/popular": "500"}
        self.run_fetch()
        self.assertEqual(self.load("popular.json"), old)

    def test_garbled_response(self):
        self.server.broken = {"/api/collection/recently-added": "garbage"}
        self.run_fetch()
        self.assertFalse(os.path.exists(os.path.join(self.out, "recently-added.json")))

    def test_slow_server_gives_up(self):
        self.seed("recently-updated.json", [{"id": "org.old.App"}])
        self.server.broken = {"/api/collection/recently-updated": "slow"}
        started = time.monotonic()
        self.run_fetch(timeout=0.5)
        self.assertLess(time.monotonic() - started, 5)
        self.assertEqual(self.load("recently-updated.json"), [{"id": "org.old.App"}])

    def test_trickling_server_gives_up(self):
        self.server.broken = {"/odrs": "drip"}
        started = time.monotonic()
        self.run_fetch(timeout=1.0)
        self.assertLess(time.monotonic() - started, 6)
        self.assertFalse(os.path.exists(os.path.join(self.out, "ratings.json")))

    def test_oversized_response_is_refused(self):
        self.server.broken = {"/odrs": "huge"}
        self.run_fetch()
        self.assertFalse(os.path.exists(os.path.join(self.out, "ratings.json")))

    def test_offline(self):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
        s.close()
        self.seed("popular.json", [{"id": "org.old.App"}])
        got = feeds.fetch(self.out, api=f"http://127.0.0.1:{port}/api",
                          odrs=f"http://127.0.0.1:{port}/odrs", timeout=1.0)
        self.assertEqual(got, 0)
        self.assertEqual(self.load("popular.json"), [{"id": "org.old.App"}])

    def test_no_temporary_files_left(self):
        self.server.broken = {"/odrs": "garbage", "/api/collection/trending": "500"}
        self.run_fetch()
        self.assertTrue(all(n.endswith(".json") and not n.startswith(".")
                            for n in os.listdir(self.out)), os.listdir(self.out))

    def test_anonymous_requests(self):
        self.run_fetch()
        self.assertTrue(self.server.requests)
        for _, agent in self.server.requests:
            self.assertEqual(agent, feeds.USER_AGENT)

    def test_command_line(self):
        self.assertEqual(feeds.main(["fetch", "--out", self.out,
                                     "--api", self.server.base + "/api/",
                                     "--odrs", self.server.base + "/odrs",
                                     "--today", "2026-10-31"]), 0)
        self.assertEqual(len(self.load("popular.json")), 6)
        with self.assertRaises(SystemExit):
            feeds.main(["fetch", "--out", self.out, "--today", "tomorrow; rm -rf /"])
        env = os.environ.pop("CACHE_DIRECTORY", None)
        try:
            with self.assertRaises(SystemExit):
                feeds.main(["fetch"])
        finally:
            if env is not None:
                os.environ["CACHE_DIRECTORY"] = env

    # --- publish -----------------------------------------------------------

    def test_publish_copies_checked_files(self):
        self.run_fetch()
        dst = os.path.join(self.tmp, "public", "feeds")
        self.assertEqual(feeds.publish(self.out, dst), len(feeds.FILES))
        self.assertEqual(stat.S_IMODE(os.stat(dst).st_mode), 0o755)
        for name in feeds.FILES:
            self.assertEqual(self.load(name, dst), self.load(name))
            self.assertEqual(stat.S_IMODE(os.stat(os.path.join(dst, name)).st_mode), 0o644)

    def test_publish_does_not_follow_links(self):
        secret = os.path.join(self.tmp, "secret.json")
        with open(secret, "w") as f:
            json.dump({"password": "hunter2"}, f)
        os.makedirs(self.out)
        os.symlink(secret, os.path.join(self.out, "ratings.json"))
        dst = os.path.join(self.tmp, "public")
        feeds.publish(self.out, dst)
        self.assertFalse(os.path.exists(os.path.join(dst, "ratings.json")))

    def test_publish_skips_pipes_and_bad_files(self):
        os.makedirs(self.out)
        os.mkfifo(os.path.join(self.out, "popular.json"))
        self.seed("trending.json", [{"id": "../../etc"}])
        self.seed("picks.json", {"day": 42, "week": []})
        with open(os.path.join(self.out, "ratings.json"), "w") as f:
            f.write("{nope")
        self.seed("recently-added.json", [{"id": "org.ok.App", "installs_last_month": 3}])
        dst = os.path.join(self.tmp, "public")
        os.makedirs(dst)
        with open(os.path.join(dst, "trending.json"), "w") as f:
            json.dump([{"id": "org.last.Good"}], f)
        started = time.monotonic()
        self.assertEqual(feeds.publish(self.out, dst), 1)
        self.assertLess(time.monotonic() - started, 5)   # the pipe did not block
        self.assertEqual(sorted(os.listdir(dst)), ["recently-added.json", "trending.json"])
        self.assertEqual(self.load("trending.json", dst), [{"id": "org.last.Good"}])

    def test_publish_refuses_huge_files(self):
        self.seed("popular.json", [])
        with open(os.path.join(self.out, "popular.json"), "r+b") as f:
            f.truncate(feeds.MAX_FILE + 1)
        dst = os.path.join(self.tmp, "public")
        self.assertEqual(feeds.publish(self.out, dst), 0)

    def test_publish_with_nothing_fetched(self):
        dst = os.path.join(self.tmp, "public")
        self.assertEqual(feeds.main(["publish", os.path.join(self.tmp, "none"), dst]), 0)
        self.assertEqual(os.listdir(dst), [])


class UnitsTest(unittest.TestCase):
    """The catalog service and timer say what the spec asks."""

    def read(self, name):
        with open(os.path.join(ROOT, "data", name), encoding="utf-8") as f:
            return [line.strip() for line in f if line.strip() and not line.startswith("#")]

    def test_service(self):
        lines = self.read("raye-store-catalog.service")
        self.assertIn("DynamicUser=yes", lines)
        self.assertIn("CacheDirectory=raye-store-feeds", lines)
        exec_lines = [l for l in lines if l.startswith(("ExecStart=", "ExecStartPost="))]
        self.assertEqual(exec_lines[0],
                         "ExecStart=+-/usr/bin/flatpak update --appstream --system --noninteractive")
        self.assertEqual(exec_lines[1],
                         "ExecStart=/usr/libexec/raye-store-feeds fetch --out /var/cache/raye-store-feeds")
        # only the copy runs as root, and it writes where the Store reads
        self.assertEqual(exec_lines[2], "ExecStartPost=+/usr/libexec/raye-store-feeds publish "
                         "/var/cache/raye-store-feeds /var/cache/raye-store/feeds")
        self.assertIn("After=network-online.target", lines)
        self.assertIn("CapabilityBoundingSet=", lines)

    def test_timer(self):
        lines = self.read("raye-store-catalog.timer")
        self.assertIn("OnBootSec=2min", lines)
        self.assertIn("OnUnitActiveSec=12h", lines)
        self.assertIn("WantedBy=timers.target", lines)

    def test_script_runs_isolated(self):
        with open(SCRIPT, encoding="utf-8") as f:
            self.assertEqual(f.readline().strip(), "#!/usr/bin/python3 -I")
        self.assertTrue(os.access(SCRIPT, os.X_OK))


if __name__ == "__main__":
    unittest.main(verbosity=1)
