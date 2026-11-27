#!/usr/bin/env python3
"""The bundled gamedata.json is generated from the Gamedata data repo; Gamedata is edited there only.

  bundle.py gamedata DATA_REPO OS   print the bundled gamedata.json for OS (linux | windows)
  bundle.py seq-floor INDEX        print the seq floor a release embeds: the seq of the published index.json
  bundle.py check DATA_REPO         exit 1 when this repo's copies differ from the data repo

The bundled file for an OS is the data repo's newest snapshot for that OS (highest SourceRevision under
snapshots/<SourceRevision>/<os>.json), stamped with that SourceRevision as the first top-level key. Each OS's
release artifact carries its own; a snapshot is published per OS and its kills and stamp are per OS, so one merged
file could not say both. This repo's committed gamedata.json (what local builds, native tests and e2e use) is the
Linux one.

`check` also compares the native tests' copy of the publisher's cross-implementation fixtures with the data repo's.
"""

import json
import pathlib
import sys

OSES = ("linux", "windows")
FORK = pathlib.Path(__file__).resolve().parents[2]
BUNDLED = "configs/addons/counterstrikesharp/gamedata/gamedata.json"
FIXTURES = "tests/native/fixtures/gamedata-feed"


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


def seq_floor(index_bytes):
    try:
        seq = json.loads(index_bytes)["seq"]
    except (ValueError, TypeError, KeyError) as error:
        raise ValueError(f"not a published index.json: {error!r}") from None
    if type(seq) is not int or seq < 0:
        raise ValueError(f"published index seq is not a non-negative integer: {seq!r}")
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
    ours, theirs = files(pathlib.Path(fork, FIXTURES)), files(pathlib.Path(data_repo, "publisher/fixtures"))
    for name in sorted(ours.keys() | theirs.keys()):
        if ours.get(name) != theirs.get(name):
            state = "missing" if name not in ours else "not in the data repo" if name not in theirs else "differs"
            problems.append(
                f"{FIXTURES}/{name} {state} (data repo publisher/fixtures); replace the copy: "
                f"rm -r {FIXTURES} && cp -r {data_repo}/publisher/fixtures {FIXTURES}"
            )
    return problems


def main(argv):
    command, *args = argv or [""]
    if command == "gamedata" and len(args) == 2 and args[1] in OSES:
        sys.stdout.buffer.write(generate(*args).encode("utf-8"))
        return 0
    if command == "seq-floor" and len(args) == 1:
        print(seq_floor(pathlib.Path(args[0]).read_bytes()))
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
