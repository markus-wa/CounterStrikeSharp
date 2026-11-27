#!/usr/bin/env python3
"""Tests for bundle.py: python3 -m unittest discover -s eng/gamedata"""

import hashlib
import pathlib
import subprocess
import tempfile
import unittest

import bundle

FIXTURES = bundle.FORK / bundle.FIXTURES


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


class Signer:
    """A throwaway Ed25519 key that signs like the data repo's publisher (key id + signature over prefix + index)."""

    def __init__(self, directory):
        self.key = pathlib.Path(directory, "key.pem")
        subprocess.run(["openssl", "genpkey", "-algorithm", "ed25519", "-out", self.key], check=True, capture_output=True)
        der = subprocess.run(["openssl", "pkey", "-in", self.key, "-pubout", "-outform", "DER"], check=True,
                             capture_output=True).stdout
        self.public_key = der[-32:]

    def sign(self, index, prefix=bundle.INDEX_SIGNATURE_PREFIX):
        message = self.key.with_name("message")
        message.write_bytes(prefix + index)
        signature = subprocess.run(["openssl", "pkeyutl", "-sign", "-inkey", self.key, "-rawin", "-in", message],
                                   check=True, capture_output=True).stdout
        return hashlib.sha256(self.public_key).digest()[:8] + signature


class SeqFloor(unittest.TestCase):
    INDEX = b'{"builds": {}, "issued_at": "2026-11-27T00:00:00Z", "seq": 7}\n'

    def setUp(self):
        self.signer = Signer(self.enterContext(tempfile.TemporaryDirectory()))

    def floor(self, index, signature=None):
        return bundle.seq_floor(index, self.signer.sign(index) if signature is None else signature, [self.signer.public_key])

    def test_the_floor_is_the_seq_of_a_published_index_signed_by_a_built_in_key(self):
        index, signature = (FIXTURES / "index.json").read_bytes(), (FIXTURES / "index.json.sig").read_bytes()
        self.assertEqual(bundle.seq_floor(index, signature, bundle.builtin_keys()), 1)
        self.assertEqual(self.floor(self.INDEX), 7)

    def test_the_built_in_keys_are_read_from_the_core(self):
        self.assertEqual([key.hex() for key in bundle.builtin_keys()],
                         ["7a8dad7c818602e6381074af43f1bf44903323425627485d51125e2db4f28b5b"])

    def test_an_index_whose_signature_does_not_verify_is_refused_before_its_seq_is_read(self):
        signature = self.signer.sign(self.INDEX)
        refused = {
            "unsigned": b"",
            "flipped index": None,
            "flipped signature": signature[:20] + bytes([signature[20] ^ 1]) + signature[21:],
            "unknown key id": bytes(8) + signature[8:],
            "no domain prefix": self.signer.sign(self.INDEX, prefix=b""),
        }
        for name, bad in refused.items():
            with self.subTest(name), self.assertRaisesRegex(ValueError, "signature"):
                index = self.INDEX.replace(b"7", b"8") if bad is None else self.INDEX
                bundle.seq_floor(index, signature if bad is None else bad, [self.signer.public_key])

    def test_a_signed_index_without_a_usable_seq_is_refused(self):
        for index in (b"<html>", b"[]", b'{"builds": {}}', b'{"seq": "7"}', b'{"seq": true}', b'{"seq": -1}',
                      b'{"seq": 9223372036854775808}'):
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, "published index"):
                self.floor(index)
        self.assertEqual(self.floor(b'{"seq": 9223372036854775807}'), 2**63 - 1)


class Drift(unittest.TestCase):
    BUNDLED = "configs/addons/counterstrikesharp/gamedata/gamedata.json"
    FIXTURES = "tests/native/fixtures/gamedata-feed"

    def setUp(self):
        temp = pathlib.Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.data, self.fork = temp / "data", temp / "fork"
        write(self.data / "snapshots/5/linux.json", '{"A": {"offsets": {"linux": 1}}}')
        write(self.data / "snapshots/7/windows.json", '{"A": {"offsets": {"windows": 2}}}')
        write(self.data / "publisher/fixtures/feed/index.json", '{"seq": 1}\n')
        write(self.data / "publisher/fixtures/feed/snapshots/5/linux.json", "{}")
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
        (self.data / "publisher/fixtures/feed/snapshots/5/linux.json").rename(self.data / "publisher/fixtures/feed/moved.json")
        problems = "\n".join(bundle.drift(self.data, self.fork))
        for name in ("index.json", "extra.json", "moved.json", "snapshots/5/linux.json"):
            self.assertIn(f"{self.FIXTURES}/{name}", problems)


if __name__ == "__main__":
    unittest.main()
