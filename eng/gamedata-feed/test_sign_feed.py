#!/usr/bin/env python3
"""Tests for sign_feed.py: python3 -m unittest discover -s eng/gamedata-feed (needs the cryptography package)."""

import hashlib
import json
import pathlib
import tempfile
import unittest

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

import sign_feed

HERE = pathlib.Path(__file__).resolve().parent
TEST_KEY = HERE / "test-key.pem"
NATIVE_FIXTURES = HERE.parent.parent / "tests" / "native" / "fixtures" / "gamedata-feed"
# The key the core trusts until production keys ship (src/core/gamedata_feed.cpp).
BUILTIN_TEST_PUBLIC_KEY = "7a8dad7c818602e6381074af43f1bf44903323425627485d51125e2db4f28b5b"


class SignFeedTest(unittest.TestCase):
    def setUp(self):
        self.key = sign_feed.load_key(TEST_KEY)
        self.dir = pathlib.Path(tempfile.mkdtemp())
        self.snapshot = self.dir / "gamedata.json"
        self.snapshot.write_bytes(b'{ "Foo": { "offsets": { "linux": 1 } } }\n')

    def build(self, out, seq=1):
        sign_feed.build_feed(self.key, seq, "2026-11-27T00:00:00Z", [f"11268945:1.42.0.1:linux:{self.snapshot}"], out)
        return out

    def test_the_test_key_is_the_one_the_core_trusts(self):
        self.assertEqual(sign_feed.public_key_bytes(self.key).hex(), BUILTIN_TEST_PUBLIC_KEY)

    def test_signature_file_is_key_id_and_a_signature_over_prefix_and_index(self):
        out = self.build(self.dir / "feed")
        index = (out / "index.json").read_bytes()
        signature = (out / "index.json.sig").read_bytes()
        public = Ed25519PublicKey.from_public_bytes(bytes.fromhex(BUILTIN_TEST_PUBLIC_KEY))

        self.assertEqual(len(signature), 72)
        self.assertEqual(signature[:8], hashlib.sha256(bytes.fromhex(BUILTIN_TEST_PUBLIC_KEY)).digest()[:8])
        public.verify(signature[8:], b"cssharp-gamedata-index-v1\0" + index)
        with self.assertRaises(InvalidSignature):
            public.verify(signature[8:], index)

    def test_index_lists_each_snapshot_with_its_size_and_sha256(self):
        out = self.build(self.dir / "feed", seq=5)
        index = json.loads((out / "index.json").read_bytes())
        snapshot = self.snapshot.read_bytes()

        self.assertEqual(index["seq"], 5)
        build = index["builds"]["11268945"]
        self.assertEqual(build["patchVersion"], "1.42.0.1")
        self.assertEqual(build["linux"]["size"], len(snapshot))
        self.assertEqual(build["linux"]["sha256"], hashlib.sha256(snapshot).hexdigest())
        self.assertEqual((out / build["linux"]["file"]).read_bytes(), snapshot)

    def test_same_input_gives_the_same_index_bytes(self):
        first = self.build(self.dir / "a")
        second = self.build(self.dir / "b")
        self.assertEqual((first / "index.json").read_bytes(), (second / "index.json").read_bytes())
        self.assertEqual((first / "index.json.sig").read_bytes(), (second / "index.json.sig").read_bytes())

    def test_a_snapshot_that_is_not_json_is_refused(self):
        self.snapshot.write_bytes(b"not json")
        with self.assertRaises(ValueError):
            self.build(self.dir / "feed")

    def test_native_fixtures_are_what_the_signer_produces(self):
        # The native cross-implementation test (tests/native/gamedata_feed_test.cpp) verifies these files.
        specs = [f"11268945:1.42.0.1:{os_name}:{NATIVE_FIXTURES / 'snapshots' / '11268945' / (os_name + '.json')}"
                 for os_name in ("linux", "windows")]
        out = self.dir / "fixtures"
        sign_feed.build_feed(self.key, 3, "2026-11-27T00:00:00Z", specs, out)
        for name in ("index.json", "index.json.sig"):
            self.assertEqual((out / name).read_bytes(), (NATIVE_FIXTURES / name).read_bytes(), name)


if __name__ == "__main__":
    unittest.main()
