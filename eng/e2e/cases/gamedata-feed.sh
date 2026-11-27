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
# cache:      a second start after exact (its Gamedata cache installed) with the feed down: boots on the cached snapshot
#             and reports it as cache; managed reads it too.
# tampered+cache: the tampered feed on a second start: falls back to the cached snapshot and logs why.
# carried-forward: the feed lists only the previous build X (SourceRevision - 1), whose snapshot kills
#             game-system-events; X's snapshot is cached and the bundled file is unstamped (oldest): boots on X's Gamedata,
#             and the kill stays in force, reported as carried forward from X.
# https, malformed: an https:// or malformed GamedataFeedUrl boots on bundled with a warning, never aborts (#931).
# blackhole:  a feed host that accepts connections and never answers: the feed step gives up at its 10 s deadline
#             (CoreConfig loaded -> Gamedata line in the native log) and boots on bundled.
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
httpd= blackhole=
trap 'kill $httpd $blackhole 2>/dev/null; rm -rf "$work"' EXIT

# A one-build data repo: this build's Linux snapshot is the bundled Gamedata (without its bundled-only sourceRevision
# stamp, which the publisher refuses) plus a marker entry.
snapshots=$work/data/snapshots/$revision
mkdir -p "$snapshots"
jq -n --arg patch "$patch" '{patchVersion: $patch}' >"$snapshots/build.json"
jq 'del(.sourceRevision) + {"E2EFeedMarker": {"offsets": {"linux": 1, "windows": 1}}}' \
    "$repo/configs/addons/counterstrikesharp/gamedata/gamedata.json" >"$snapshots/linux.json" || exit 1
entries=$(jq '[.[] | objects] | length' "$snapshots/linux.json")
python3 -I "$gamedata_repo/publisher/publish.py" publish --key "$gamedata_repo/publisher/test-key.pem" \
    --out "$work/feed" "$work/data" || exit 1 # first publish: seq 1
cp -r "$work/feed" "$work/tampered"

# The previous build's data repo, published alone; its snapshot cached as a server that ran on that build would have it.
previous=$((revision - 1))
mkdir -p "$work/data-previous/snapshots/$previous" "$work/carried-cache"
echo '{"patchVersion": "0.0.0.1"}' >"$work/data-previous/snapshots/$previous/build.json"
jq 'del(.sourceRevision) | .disabledEngineFeatures = [{"id": "cssharp.server/game-system-events", "reason": "e2e kill"}]' \
    "$repo/configs/addons/counterstrikesharp/gamedata/gamedata.json" >"$work/data-previous/snapshots/$previous/linux.json" || exit 1
python3 -I "$gamedata_repo/publisher/publish.py" publish --key "$gamedata_repo/publisher/test-key.pem" \
    --out "$work/carried" "$work/data-previous" || exit 1
cat "$work/carried/index.json.sig" "$work/carried/index.json" >"$work/carried-cache/index.signed"
cp "$work/carried/snapshots/$previous/linux.json" "$work/carried-cache/$previous.linux.json"
python3 -I -c 'import sys; p = sys.argv[1]; b = bytearray(open(p, "rb").read()); b[len(b) // 2] ^= 1; open(p, "wb").write(b)' \
    "$work/tampered/index.json"

python3 -I -u -m http.server --bind 127.0.0.1 --directory "$work" "$port" >"$work/httpd.out" 2>"$work/httpd.log" &
httpd=$!
# Accepts and never answers, on a free port it prints.
python3 -I -u -c '
import socket
s = socket.create_server(("127.0.0.1", 0))
print("Serving HTTP (blackhole) port", s.getsockname()[1])
held = []
while True:
    held.append(s.accept()[0])
' >"$work/blackhole.out" 2>"$work/blackhole.log" &
blackhole=$!
# Wait for these servers, not just the ports: a port taken by another run (e.g. a concurrent agent's) would serve its feed.
for server in httpd blackhole; do
    until grep -q '^Serving HTTP' "$work/$server.out"; do
        kill -0 "${!server}" 2>/dev/null ||
            { echo "gamedata-feed: $server failed (set CSS_E2E_FEED_PORT if port $port is taken):"; cat "$work/$server.log"; exit 1; }
        sleep 0.1
    done
done
blackhole_port=$(sed -n 's/^Serving HTTP (blackhole) port //p' "$work/blackhole.out")

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
    --expect-log "Successfully loaded $entries game data entries from core Gamedata" \
    --check "cp -r gamedata-cache '$work/cache'"

case_run tampered "$@" --no-smoke --core-jq "$(feed true "http://127.0.0.1:$port/tampered")" \
    --expect-log 'Gamedata: bundled gamedata\.json; warnings: index signature invalid \(key de4a068ed7d94def\)' \
    --expect-log '^ +Gamedata: bundled gamedata\.json; warnings: index signature invalid'

cached="exact build $patch \\(SourceRevision $revision\\) from the Gamedata cache, seq 1, key de4a068ed7d94def"
case_run cache "$@" --no-smoke --gamedata-cache "$work/cache" --core-jq "$(feed true "http://127.0.0.1:$port/down")" \
    --expect-log "Gamedata: $cached; warnings: Gamedata Feed unreachable: http://127.0.0.1:$port/down/index\\.json: HTTP 404" \
    --expect-log "^ +Gamedata: $cached" \
    --expect-log "Successfully loaded $entries game data entries from core Gamedata"

case_run tampered+cache "$@" --no-smoke --gamedata-cache "$work/cache" --core-jq "$(feed true "http://127.0.0.1:$port/tampered")" \
    --expect-log "Gamedata: $cached; warnings: index signature invalid \\(key de4a068ed7d94def\\)"

previous_label="0\\.0\\.0\\.1 \\(SourceRevision $previous\\)"
case_run carried-forward "$@" --no-smoke --gamedata-cache "$work/carried-cache" --gamedata-jq 'del(.sourceRevision)' \
    --core-jq "$(feed true "http://127.0.0.1:$port/carried")" \
    --expect-log "Gamedata: carried forward from build $previous_label in the Gamedata cache, seq 1, key de4a068ed7d94def; warnings: the Gamedata Feed has no snapshot for build $patch \\(SourceRevision $revision\\) on linux yet" \
    --expect-log "^ +cssharp\\.server/game-system-events disabled \\(disabled by gamedata: e2e kill\\)" \
    --expect-log "^ +cssharp\\.server/game-system-events: e2e kill, carried forward from $previous_label\$" \
    --expect-no-log 'Initialized GameSystem'

for url in "https://127.0.0.1:$port/feed" "ftp://gamedata.cssharp.dev:99999999999"; do
    case_run "$url" "$@" --no-smoke --core-jq "$(feed true "$url")" \
        --expect-log "Gamedata: bundled gamedata\\.json; warnings: Gamedata Feed unreachable: ${url%%:*}://" \
        --expect-log 'Engine Features for build [0-9.]+ \(SourceRevision [0-9]+\): '
done

# Seconds from "CoreConfig loaded." to the Gamedata line in the native log ([hh:mm:ss.mmm] stamps): the feed step.
feed_step_within_budget='awk -F"[][]" "
    function t(s,  a) { split(s, a, \":\"); return a[1] * 3600 + a[2] * 60 + a[3] }
    /CoreConfig loaded\\./ { start = t(\$2) }
    / Gamedata: / && start { took = t(\$2) - start; print \"feed step took \" took \" s\"; exit !(took <= 10.5) }
    END { if (!took) exit 1 }" counterstrikesharp-native.log'
case_run blackhole "$@" --no-smoke --core-jq "$(feed true "http://127.0.0.1:$blackhole_port/feed")" \
    --expect-log "Gamedata: bundled gamedata\\.json; warnings: Gamedata Feed unreachable: http://127\\.0\\.0\\.1:$blackhole_port/feed/index\\.json: deadline of 10000 ms exceeded" \
    --check "$feed_step_within_budget"

exit $status
