#!/usr/bin/env python3
"""The bundled gamedata.json is generated from the Gamedata data repo; Gamedata is edited there only.

  bundle.py gamedata DATA_REPO OS   print the bundled gamedata.json for OS (linux | windows)
  bundle.py seq-floor INDEX        print the seq floor a release embeds: the seq of the published index.json, read
                                   only once INDEX.sig verifies against the core's built-in keys
  bundle.py check DATA_REPO         exit 1 when this repo's copies differ from the data repo

The bundled file for an OS is the data repo's newest snapshot for that OS (highest SourceRevision under
snapshots/<SourceRevision>/<os>.json), stamped with that SourceRevision as the first top-level key. Each OS's
release artifact carries its own; a snapshot is published per OS and its kills and stamp are per OS, so one merged
file could not say both. This repo's committed gamedata.json (what local builds, native tests and e2e use) is the
Linux one.

`check` also compares the native tests' copy of the publisher's cross-implementation fixtures with the data repo's.

The built-in keys are read from the core's source (kBuiltinKeys in src/core/gamedata_feed.cpp), so the release trusts
for its floor exactly what its core trusts. Signatures are checked with the openssl CLI (OpenSSL 3, for Ed25519 -rawin).
"""

import base64
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import tempfile

OSES = ("linux", "windows")
FORK = pathlib.Path(__file__).resolve().parents[2]
BUNDLED = "configs/addons/counterstrikesharp/gamedata/gamedata.json"
FIXTURES = "tests/native/fixtures/gamedata-feed"
DATA_REPO_FIXTURES = "publisher/fixtures/feed"
FEED_SOURCE = "src/core/gamedata_feed.cpp"
INDEX_SIGNATURE_PREFIX = b"cssharp-gamedata-index-v1\0"  # feed::kIndexSignaturePrefix
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")  # DER SubjectPublicKeyInfo of a raw Ed25519 key


def newest_snapshot(data_repo, os_name):
    revisions = [
        int(path.parent.name)
        for path in pathlib.Path(data_repo, "snapshots").glob(f"*/{os_name}.json")
        if path.parent.name.isdigit()
    ]
    if not revisions:
        raise ValueError(f"no {os_name} snapshot in {data_repo}/snapshots")
    return str(max(revisions))


def generate(data_repo, os_name):
    revision = newest_snapshot(data_repo, os_name)
    snapshot = json.loads(pathlib.Path(data_repo, "snapshots", revision, f"{os_name}.json").read_text("utf-8"))
    return json.dumps({"sourceRevision": revision, **snapshot}, indent=2, ensure_ascii=False) + "\n"


def builtin_keys(fork=FORK):
    """The core's built-in Gamedata Feed public keys, raw bytes, from kBuiltinKeys in its source."""
    listed = re.search(r"kBuiltinKeys\[\] = \{(.*?)\};", pathlib.Path(fork, FEED_SOURCE).read_text("utf-8"), re.S)
    keys = re.findall(r'"([0-9a-f]{64})"', listed.group(1)) if listed else []
    if not keys:
        raise ValueError(f"no kBuiltinKeys in {FEED_SOURCE}")
    return [bytes.fromhex(key) for key in keys]


def verifies(public_key, message, signature):
    with tempfile.TemporaryDirectory() as directory:
        key, signed, sig = (pathlib.Path(directory, name) for name in ("key.pem", "message", "signature"))
        pem = base64.encodebytes(ED25519_SPKI_PREFIX + public_key).decode()
        key.write_text(f"-----BEGIN PUBLIC KEY-----\n{pem}-----END PUBLIC KEY-----\n")
        signed.write_bytes(message)
        sig.write_bytes(signature)
        return subprocess.run(["openssl", "pkeyutl", "-verify", "-pubin", "-inkey", key, "-rawin", "-in", signed,
                               "-sigfile", sig], capture_output=True).returncode == 0


def seq_floor(index_bytes, signature, public_keys):
    """The seq of a published index, read as the core reads it: only after its signature file (8-byte key id + Ed25519
    over the domain prefix and the index) verifies against one of public_keys."""
    key = next((key for key in public_keys if hashlib.sha256(key).digest()[:8] == signature[:8]), None)
    if len(signature) != 72 or key is None or not verifies(key, INDEX_SIGNATURE_PREFIX + index_bytes, signature[8:]):
        raise ValueError("published index signature does not verify against the core's built-in keys")
    try:
        seq = json.loads(index_bytes)["seq"]
    except (ValueError, TypeError, KeyError) as error:
        raise ValueError(f"not a published index.json: {error!r}") from None
    if type(seq) is not int or not 0 <= seq < 2**63:
        raise ValueError(f"published index seq is not an integer in [0, 2^63): {seq!r}")
    return seq


def files(root):
    return {path.relative_to(root).as_posix(): path.read_bytes() for path in root.rglob("*") if path.is_file()}


def drift(data_repo, fork):
    problems = []
    if pathlib.Path(fork, BUNDLED).read_bytes() != generate(data_repo, "linux").encode("utf-8"):
        problems.append(
            f"{BUNDLED} is not snapshots/{newest_snapshot(data_repo, 'linux')}/linux.json plus its stamp; regenerate: "
            f"python3 eng/gamedata/bundle.py gamedata {data_repo} linux > {BUNDLED}"
        )
    ours, theirs = files(pathlib.Path(fork, FIXTURES)), files(pathlib.Path(data_repo, DATA_REPO_FIXTURES))
    for name in sorted(ours.keys() | theirs.keys()):
        if ours.get(name) != theirs.get(name):
            state = "missing" if name not in ours else "not in the data repo" if name not in theirs else "differs"
            problems.append(
                f"{FIXTURES}/{name} {state} (data repo {DATA_REPO_FIXTURES}); replace the copy: "
                f"rm -r {FIXTURES} && cp -r {data_repo}/{DATA_REPO_FIXTURES} {FIXTURES}"
            )
    return problems


def main(argv):
    command, *args = argv or [""]
    if command == "gamedata" and len(args) == 2 and args[1] in OSES:
        sys.stdout.buffer.write(generate(*args).encode("utf-8"))
        return 0
    if command == "seq-floor" and len(args) == 1:
        index = pathlib.Path(args[0])
        print(seq_floor(index.read_bytes(), pathlib.Path(f"{index}.sig").read_bytes(), builtin_keys()))
        return 0
    if command == "check" and len(args) == 1:
        problems = drift(args[0], FORK)
        print("\n".join(problems) or "bundled gamedata.json and feed fixtures match the data repo")
        return 1 if problems else 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except ValueError as error:
        sys.exit(f"bundle.py: {error}")
