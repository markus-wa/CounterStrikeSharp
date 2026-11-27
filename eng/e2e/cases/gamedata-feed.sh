#!/usr/bin/env bash
# End-to-end cases for the Gamedata Feed (ADR-0006), against a local feed signed with the built-in test key by the data
# repo's publisher and served by python's http.server.
#
#   eng/e2e/cases/gamedata-feed.sh [run.sh options...]     e.g. --no-build
#   CSS_E2E_GAMEDATA_REPO   the cssharp-gamedata checkout (publisher/publish.py, publisher/test-key.pem);
#                           default ~/dev/markus-wa/cssharp-gamedata
#
# off:        GamedataFeedEnabled false with the feed URL set: boots on bundled and the feed sees no request.
# exact:      the feed lists a snapshot for the server's build (the bundled Gamedata plus a marker entry): native boots
#             on it and reports it, the startup report says so, and managed GameData reads the snapshot, not the file.
# tampered:   the same feed with one index byte flipped: boots on bundled and logs why.
# https, malformed: an https:// or malformed GamedataFeedUrl boots on bundled with a warning, never aborts (#931).
# Every case checks the bundled gamedata.json is left untouched. Exits non-zero if any case fails; each case's output
# dir is printed by run.sh.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
run=$here/run.sh
repo=$(cd "$here/../.." && pwd)
steam_inf=${CSS_E2E_SERVER_SRC:-$HOME/.cache/cs2-14201}/game/csgo/steam.inf
revision=$(sed -n 's/^SourceRevision=//p' "$steam_inf" | tr -d '\r')
patch=$(sed -n 's/^PatchVersion=//p' "$steam_inf" | tr -d '\r')
[ -n "$revision" ] && [ -n "$patch" ] || { echo "gamedata-feed: no SourceRevision/PatchVersion in $steam_inf"; exit 1; }
port=${CSS_E2E_FEED_PORT:-28765} # outside the harness's game port range (27400-27800)
gamedata_repo=${CSS_E2E_GAMEDATA_REPO:-$HOME/dev/markus-wa/cssharp-gamedata}
[ -f "$gamedata_repo/publisher/publish.py" ] ||
    { echo "gamedata-feed: no publisher in $gamedata_repo (set CSS_E2E_GAMEDATA_REPO to the cssharp-gamedata checkout)"; exit 1; }

work=$(mktemp -d)
httpd=
trap '[ -n "$httpd" ] && kill "$httpd" 2>/dev/null; rm -rf "$work"' EXIT

# A one-build data repo: this build's Linux snapshot is the bundled Gamedata plus a marker entry.
snapshots=$work/data/snapshots/$revision
mkdir -p "$snapshots"
jq -n --arg patch "$patch" '{patchVersion: $patch}' >"$snapshots/build.json"
jq '. + {"E2EFeedMarker": {"offsets": {"linux": 1, "windows": 1}}}' \
    "$repo/configs/addons/counterstrikesharp/gamedata/gamedata.json" >"$snapshots/linux.json" || exit 1
entries=$(jq '[.[] | objects] | length' "$snapshots/linux.json")
python3 -I "$gamedata_repo/publisher/publish.py" publish --key "$gamedata_repo/publisher/test-key.pem" \
    --out "$work/feed" "$work/data" || exit 1 # first publish: seq 1
cp -r "$work/feed" "$work/tampered"
python3 -I -c 'import sys; p = sys.argv[1]; b = bytearray(open(p, "rb").read()); b[len(b) // 2] ^= 1; open(p, "wb").write(b)' \
    "$work/tampered/index.json"

python3 -I -u -m http.server --bind 127.0.0.1 --directory "$work" "$port" >"$work/httpd.out" 2>"$work/httpd.log" &
httpd=$!
# Wait for this server, not just the port: a port taken by another run (e.g. a concurrent agent's) would serve its feed.
until grep -q '^Serving HTTP' "$work/httpd.out"; do
    kill -0 "$httpd" 2>/dev/null ||
        { echo "gamedata-feed: feed server failed on port $port (set CSS_E2E_FEED_PORT):"; cat "$work/httpd.log"; exit 1; }
    sleep 0.1
done

feed() { echo ".GamedataFeedEnabled = $1 | .GamedataFeedUrl = \"$2\""; }
status=0
case_run() {
    local name=$1
    shift
    echo "== gamedata-feed: $name"
    "$run" "$@" --check 'cmp gamedata.installed.json gamedata.after-run.json' ||
        { echo "gamedata-feed: $name failed"; status=1; }
}

case_run off "$@" --no-smoke --core-jq "$(feed false "http://127.0.0.1:$port/feed")" \
    --expect-log '^ +Gamedata: bundled gamedata\.json$' \
    --check "! grep -q 'GET ' '$work/httpd.log'"

case_run exact "$@" --no-smoke --core-jq "$(feed true "http://127.0.0.1:$port/feed")" \
    --expect-log "Gamedata: exact build $patch \\(SourceRevision $revision\\) from the Gamedata Feed, seq 1, key de4a068ed7d94def" \
    --expect-log "^ +Gamedata: exact build $patch " \
    --expect-log "Successfully loaded $entries game data entries from core Gamedata"

case_run tampered "$@" --no-smoke --core-jq "$(feed true "http://127.0.0.1:$port/tampered")" \
    --expect-log 'Gamedata: bundled gamedata\.json; warnings: index signature invalid \(key de4a068ed7d94def\)' \
    --expect-log '^ +Gamedata: bundled gamedata\.json; warnings: index signature invalid'

for url in "https://127.0.0.1:$port/feed" "ftp://gamedata.cssharp.dev:99999999999"; do
    case_run "$url" "$@" --no-smoke --core-jq "$(feed true "$url")" \
        --expect-log "Gamedata: bundled gamedata\\.json; warnings: Gamedata Feed unreachable: ${url%%:*}://" \
        --expect-log 'Engine Features for build [0-9.]+ \(SourceRevision [0-9]+\): '
done

exit $status
