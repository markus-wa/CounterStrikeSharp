#!/usr/bin/env python3
"""Builds and signs a Gamedata Feed (ADR-0006) that the core verifies natively (src/core/gamedata_feed.h).

  sign_feed.py pubkey KEY.pem
      Prints the Ed25519 public key (64 hex digits, as GamedataFeedPublicKeys takes it) and its key id.

  sign_feed.py sign --key KEY.pem --seq N [--issued-at TIME] --out DIR SPEC...
      SPEC is SOURCEREVISION:PATCHVERSION:OS:SNAPSHOT, e.g. 11268945:1.42.0.1:linux:gamedata.json. Writes
      DIR/snapshots/<SourceRevision>/<os>.json (the snapshot's bytes), DIR/index.json and DIR/index.json.sig.

Index bytes are deterministic for the same input. The signature file is the key id (first 8 bytes of sha256 of the
public key) followed by the Ed25519 signature over b"cssharp-gamedata-index-v1\\0" + the index bytes.

Keys are PEM Ed25519 private keys (openssl genpkey -algorithm ed25519). test-key.pem next to this script is the test
key the core trusts until production keys ship; it is public, never use it for a real feed.
"""

import argparse
import datetime
import hashlib
import json
import pathlib
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat, load_pem_private_key

INDEX_SIGNATURE_PREFIX = b"cssharp-gamedata-index-v1\0"
OPERATING_SYSTEMS = ("linux", "windows")


def load_key(path):
    key = load_pem_private_key(pathlib.Path(path).read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise SystemExit(f"{path}: not an Ed25519 private key")
    return key


def public_key_bytes(key):
    return key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)


def key_id(public_key):
    return hashlib.sha256(public_key).digest()[:8]


def sign_index(key, index):
    """The signature file's bytes for the index bytes."""
    return key_id(public_key_bytes(key)) + key.sign(INDEX_SIGNATURE_PREFIX + index)


def parse_spec(spec):
    parts = spec.split(":", 3)
    if len(parts) != 4 or not parts[0].isdigit() or parts[2] not in OPERATING_SYSTEMS:
        raise SystemExit(f"bad snapshot spec {spec!r}: expected SOURCEREVISION:PATCHVERSION:OS:SNAPSHOT, OS one of {OPERATING_SYSTEMS}")
    return parts


def build_feed(key, seq, issued_at, specs, out):
    """Writes the snapshots, index.json and index.json.sig into out."""
    out = pathlib.Path(out)
    builds = {}
    for source_revision, patch_version, os_name, snapshot_path in map(parse_spec, specs):
        snapshot = pathlib.Path(snapshot_path).read_bytes()
        json.loads(snapshot)  # a snapshot must be JSON; the core rejects anything else anyway
        file = f"snapshots/{source_revision}/{os_name}.json"
        (out / file).parent.mkdir(parents=True, exist_ok=True)
        (out / file).write_bytes(snapshot)
        build = builds.setdefault(source_revision, {"patchVersion": patch_version})
        build[os_name] = {"file": file, "size": len(snapshot), "sha256": hashlib.sha256(snapshot).hexdigest()}

    index = (json.dumps({"seq": seq, "issued_at": issued_at, "builds": builds}, indent=2, sort_keys=True) + "\n").encode()
    (out / "index.json").write_bytes(index)
    (out / "index.json.sig").write_bytes(sign_index(key, index))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    pubkey = commands.add_parser("pubkey")
    pubkey.add_argument("key")
    sign = commands.add_parser("sign")
    sign.add_argument("--key", required=True)
    sign.add_argument("--seq", type=int, required=True)
    sign.add_argument("--issued-at", default=datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"))
    sign.add_argument("--out", required=True)
    sign.add_argument("specs", nargs="+", metavar="SPEC")
    args = parser.parse_args(argv)

    key = load_key(args.key)
    if args.command == "pubkey":
        print(public_key_bytes(key).hex(), key_id(public_key_bytes(key)).hex())
    else:
        if args.seq < 0:
            raise SystemExit("--seq must not be negative")
        build_feed(key, args.seq, args.issued_at, args.specs, args.out)


if __name__ == "__main__":
    main(sys.argv[1:])
