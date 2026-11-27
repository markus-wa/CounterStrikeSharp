#!/usr/bin/env bash
# End-to-end cases for the Gamedata Feed poll while the server runs (GamedataFeedPollMinutes), with
# GamedataUpdateTestPlugin, against a local feed signed with the built-in test key by the data repo's publisher.
#
#   eng/e2e/cases/gamedata-update.sh [run.sh options...]     e.g. --no-build
#   CSS_E2E_GAMEDATA_REPO   the cssharp-gamedata checkout (default ~/dev/markus-wa/cssharp-gamedata)
#   CSS_E2E_FEED_PORT       the local feed's port (default 28766)
#
# update:    boots on seq 1 from the feed; once the boot fetched it, seq 2 is published with this build's snapshot
#            changed (a Kill Switch on cssharp.player/respawn and a changed marker entry). Polling every minute for two
#            minutes: one restart notice, one OnGamedataUpdateAvailable with build, seq 2 and the changed Engine Feature;
#            the plugin reads the provenance (exact build, seq 1) through EngineFeatures.Gamedata.
# next boot: the update run's Gamedata cache with the feed down: boots on the staged seq 2 from the cache, respawn killed.
# blackhole: a feed host that accepts connections and never answers: the poll gives up at its 10 s deadline while the
#            game thread keeps ticking (every 10 s window the plugin logs after warm-up has no gap of 500 ms or more).
# Exits non-zero if any case fails; each case's output dir is printed by run.sh.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
run=$here/run.sh
repo=$(cd "$here/../.." && pwd)
plugin=managed/CounterStrikeSharp.Tests.GamedataUpdate/GamedataUpdateTestPlugin.csproj
steam_inf=${CSS_E2E_SERVER_SRC:-$HOME/.cache/cs2-14201}/game/csgo/steam.inf
revision=$(sed -n 's/^SourceRevision=//p' "$steam_inf" | tr -d '\r')
patch=$(sed -n 's/^PatchVersion=//p' "$steam_inf" | tr -d '\r')
[ -n "$revision" ] && [ -n "$patch" ] || { echo "gamedata-update: no SourceRevision/PatchVersion in $steam_inf"; exit 1; }
port=${CSS_E2E_FEED_PORT:-28766} # outside the harness's game port range (27400-27800)
gamedata_repo=${CSS_E2E_GAMEDATA_REPO:-$HOME/dev/markus-wa/cssharp-gamedata}
[ -f "$gamedata_repo/publisher/publish.py" ] ||
    { echo "gamedata-update: no publisher in $gamedata_repo (set CSS_E2E_GAMEDATA_REPO to the cssharp-gamedata checkout)"; exit 1; }

work=$(mktemp -d)
httpd= blackhole= publisher=
trap 'kill $httpd $blackhole $publisher 2>/dev/null; rm -rf "$work"' EXIT

# A one-build data repo published twice: seq 1 = the bundled Gamedata plus a marker entry, seq 2 = that with the marker
# changed and respawn killed.
snapshot=$work/data/snapshots/$revision/linux.json
mkdir -p "$(dirname "$snapshot")"
jq -n --arg patch "$patch" '{patchVersion: $patch}' >"$(dirname "$snapshot")/build.json"
jq 'del(.sourceRevision) + {"E2EFeedMarker": {"offsets": {"linux": 1, "windows": 1}}}' \
    "$repo/configs/addons/counterstrikesharp/gamedata/gamedata.json" >"$snapshot" || exit 1
publish() { python3 -I "$gamedata_repo/publisher/publish.py" publish --key "$gamedata_repo/publisher/test-key.pem" "$@" "$work/data"; }
publish --out "$work/v1" || exit 1
jq '.E2EFeedMarker.offsets.linux = 2 | .disabledEngineFeatures = [{"id": "cssharp.player/respawn", "reason": "e2e update"}]' \
    "$snapshot" >"$snapshot.new" && mv "$snapshot.new" "$snapshot" || exit 1
publish --previous "$work/v1/index.json" --out "$work/v2" || exit 1
ln -s v1 "$work/feed"

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
for server in httpd blackhole; do
    until grep -q '^Serving HTTP' "$work/$server.out"; do
        kill -0 "${!server}" 2>/dev/null ||
            { echo "gamedata-update: $server failed (set CSS_E2E_FEED_PORT if port $port is taken):"; cat "$work/$server.log"; exit 1; }
        sleep 0.1
    done
done
blackhole_port=$(sed -n 's/^Serving HTTP (blackhole) port //p' "$work/blackhole.out")

feed() { echo ".GamedataFeedEnabled = true | .GamedataFeedUrl = \"$1\" | .GamedataFeedPollMinutes = 1"; }
status=0
case_run() {
    local name=$1
    shift
    echo "== gamedata-update: $name"
    "$run" "$@" --check 'cmp gamedata.installed.json gamedata.after-run.json' ||
        { echo "gamedata-update: $name failed"; status=1; }
}

# Publishes seq 2 (an atomic symlink swap) once the boot fetched seq 1's snapshot.
(
    until grep -q 'GET /feed/snapshots/' "$work/httpd.log"; do sleep 0.5; done
    sleep 2
    ln -s v2 "$work/feed.new" && mv -T "$work/feed.new" "$work/feed"
) &
publisher=$!
build="$patch \\(SourceRevision $revision\\)"
case_run update "$@" --plugin-project "$plugin" --no-smoke --settle 120 --core-jq "$(feed "http://127.0.0.1:$port/feed")" \
    --expect-log "Gamedata: exact build $build from the Gamedata Feed, seq 1" \
    --expect-log 'GamedataUpdateTest: provenance source=ExactBuild seq=1' \
    --expect-log "Newer Gamedata for build $build \\(seq 2\\) is staged in the Gamedata cache: a restart will apply it\\. Changed Gamedata keys: E2EFeedMarker; changed Kill Switches: cssharp\\.player/respawn" \
    --expect-log "GamedataUpdateTest: OnGamedataUpdateAvailable build=$build seq=2 changed=cssharp\\.player/respawn" \
    --check "test \$(grep -c 'GET /feed/index.json ' '$work/httpd.log') -ge 3" \
    --check 'test $(grep -c "Newer Gamedata for build" counterstrikesharp-native.log) = 1' \
    --check 'test $(grep -c "GamedataUpdateTest: OnGamedataUpdateAvailable" server-console.log) = 1' \
    --check "cp -r gamedata-cache '$work/cache'"

case_run "next boot" "$@" --no-build --no-smoke --gamedata-cache "$work/cache" --core-jq "$(feed "http://127.0.0.1:$port/down")" \
    --expect-log "Gamedata: exact build $build from the Gamedata cache, seq 2" \
    --expect-log '^ +cssharp\.player/respawn disabled \(disabled by gamedata: e2e update\)'

# The plugin's 10 s windows after the first two (map load): at least 4, none with a tick gap of 500 ms or more.
ticks_unaffected='awk "/GamedataUpdateTest: [0-9]+ ticks, max gap/ { if (++n > 2) { w++; gap = \$(NF - 1); print; if (gap >= 500) bad++ } }
    END { exit !(w >= 4 && !bad) }" server-console.log'
case_run blackhole "$@" --no-build --plugin-project "$plugin" --no-smoke --settle 75 \
    --core-jq "$(feed "http://127.0.0.1:$blackhole_port/feed")" \
    --expect-log "Gamedata Feed poll: Gamedata Feed unreachable: http://127\\.0\\.0\\.1:$blackhole_port/feed/index\\.json: deadline of 10000 ms exceeded" \
    --check "$ticks_unaffected"

exit $status
