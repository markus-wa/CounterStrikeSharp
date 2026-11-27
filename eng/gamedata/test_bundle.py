#!/usr/bin/env python3
"""Tests for bundle.py: python3 -m unittest discover -s eng/gamedata"""

import pathlib
import tempfile
import unittest

import bundle


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class Generate(unittest.TestCase):
    def setUp(self):
        self.repo = pathlib.Path(self.enterContext(tempfile.TemporaryDirectory()))
        # 900 sorts after 1000 as text; Windows lags Linux by one build.
        write(self.repo / "snapshots/900/linux.json", '{"A": {"offsets": {"linux": 1}}}')
        write(self.repo / "snapshots/900/windows.json", '{"A": {"offsets": {"windows": 2}}}')
        write(self.repo / "snapshots/1000/build.json", '{"patchVersion": "1.0"}')
        write(self.repo / "snapshots/1000/linux.json", '{"B": {"offsets": {"linux": 3}}, "C": {"note": "ü"}}')

    def test_each_os_bundles_its_newest_snapshot_stamped_with_its_source_revision(self):
        self.assertEqual(
            bundle.generate(self.repo, "linux"),
            '{\n  "sourceRevision": "1000",\n  "B": {\n    "offsets": {\n      "linux": 3\n    }\n  },\n'
            '  "C": {\n    "note": "ü"\n  }\n}\n',
        )
        self.assertEqual(
            bundle.generate(self.repo, "windows"),
            '{\n  "sourceRevision": "900",\n  "A": {\n    "offsets": {\n      "windows": 2\n    }\n  }\n}\n',
        )

    def test_an_os_without_any_snapshot_is_refused(self):
        (self.repo / "snapshots/900/windows.json").unlink()
        with self.assertRaisesRegex(ValueError, "no windows snapshot"):
            bundle.generate(self.repo, "windows")


class SeqFloor(unittest.TestCase):
    def test_the_floor_is_the_published_index_seq(self):
        self.assertEqual(bundle.seq_floor(b'{"builds": {}, "issued_at": "2026-11-27T00:00:00Z", "seq": 7}\n'), 7)

    def test_an_index_without_a_usable_seq_is_refused(self):
        for index in (b"<html>", b"[]", b'{"builds": {}}', b'{"seq": "7"}', b'{"seq": true}', b'{"seq": -1}'):
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, "published index"):
                bundle.seq_floor(index)


class Drift(unittest.TestCase):
    BUNDLED = "configs/addons/counterstrikesharp/gamedata/gamedata.json"
    FIXTURES = "tests/native/fixtures/gamedata-feed"

    def setUp(self):
        temp = pathlib.Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.data, self.fork = temp / "data", temp / "fork"
        write(self.data / "snapshots/5/linux.json", '{"A": {"offsets": {"linux": 1}}}')
        write(self.data / "snapshots/7/windows.json", '{"A": {"offsets": {"windows": 2}}}')
        write(self.data / "publisher/fixtures/index.json", '{"seq": 1}\n')
        write(self.data / "publisher/fixtures/snapshots/5/linux.json", "{}")
        write(self.fork / self.BUNDLED, '{\n  "sourceRevision": "5",\n  "A": {\n    "offsets": {\n      "linux": 1\n    }\n  }\n}\n')
        write(self.fork / self.FIXTURES / "index.json", '{"seq": 1}\n')
        write(self.fork / self.FIXTURES / "snapshots/5/linux.json", "{}")

    def test_copies_equal_to_the_data_repo_pass(self):
        self.assertEqual(bundle.drift(self.data, self.fork), [])

    def test_a_bundled_file_that_is_not_the_newest_linux_snapshot_drifts(self):
        write(self.data / "snapshots/6/linux.json", '{"A": {"offsets": {"linux": 9}}}')
        [problem] = bundle.drift(self.data, self.fork)
        self.assertIn(self.BUNDLED, problem)
        self.assertIn("snapshots/6/linux.json", problem)

    def test_a_fixture_copy_that_differs_drifts_per_file(self):
        write(self.fork / self.FIXTURES / "index.json", '{"seq": 2}\n')
        write(self.fork / self.FIXTURES / "extra.json", "{}")
        (self.data / "publisher/fixtures/snapshots/5/linux.json").rename(self.data / "publisher/fixtures/moved.json")
        problems = "\n".join(bundle.drift(self.data, self.fork))
        for name in ("index.json", "extra.json", "moved.json", "snapshots/5/linux.json"):
            self.assertIn(f"{self.FIXTURES}/{name}", problems)


if __name__ == "__main__":
    unittest.main()
