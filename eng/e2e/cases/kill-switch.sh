#!/usr/bin/env bash
# End-to-end cases for Kill Switches (ADR-0006) in the bundled Gamedata: a blank Binding and a disabledEngineFeatures
# entry each disable only their Engine Feature; FailMode crash stops the server on a killed core feature unless the
# operator forces it on (ForceEnableEngineFeatures).
#
#   eng/e2e/cases/kill-switch.sh [run.sh options...]     e.g. --no-build
#
# blank binding:  Host_Say's Linux signature is "": it fails as disabled by gamedata, only say-hooks is disabled, the
#                 Kill Switch block names it, and the server stays up.
# feature kill:   disabledEngineFeatures names game-system-events: every Binding resolves, only game-system-events is
#                 disabled with its reason, the game system is not registered, and the server stays up.
# crash, killed:  the same kill with FailMode crash: the startup report, then the fatal error naming the kill, exit 1.
# crash, forced:  the same kill with FailMode crash and the feature forced on: native warns, the report marks it forced
#                 on by operator, the game system is registered, and the server keeps running.
# Exits non-zero if any case fails; each case's output dir is printed by run.sh.
set -uo pipefail

run=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/run.sh

kill_game_system='.disabledEngineFeatures = [{ "id": "cssharp.server/game-system-events", "reason": "e2e kill" }]'
status=0

echo "== kill-switch: blank Host_Say"
"$run" "$@" --no-smoke --exec css_engine_features \
    --gamedata-jq '.Host_Say.signatures.linux = ""' \
    --expect-log 'Bindings for build [^:]+: 1 of [0-9]+ failed: Host_Say \(disabled by gamedata\)' \
    --expect-log 'Engine Features for build [^:]+: [0-9]+ of [0-9]+ available, 1 disabled:' \
    --expect-log '^ +cssharp\.chat/say-hooks disabled \(Host_Say: disabled by gamedata\)' \
    --expect-log '^ +Kill Switches in Gamedata: 1' \
    --expect-log '^ +Host_Say: blank \(disabled by gamedata\)' \
    --expect-log 'Initialized GameSystem' ||
    status=1

echo "== kill-switch: game-system-events in disabledEngineFeatures"
"$run" "$@" --no-build --no-smoke --exec css_engine_features \
    --gamedata-jq "$kill_game_system" \
    --expect-log 'Bindings for build [^:]+: all [0-9]+ resolved' \
    --expect-log 'Engine Features for build [^:]+: [0-9]+ of [0-9]+ available, 1 disabled:' \
    --expect-log '^ +cssharp\.server/game-system-events disabled \(disabled by gamedata: e2e kill\)' \
    --expect-log '^ +Kill Switches in Gamedata: 1' \
    --expect-log '^ +cssharp\.server/game-system-events: e2e kill$' \
    --expect-log '\[EngineFeatures\] cssharp\.server/game-system-events disabled' \
    --expect-no-log 'Initialized GameSystem' \
    --expect-no-log 'names no Engine Feature' ||
    status=1

echo "== kill-switch: FailMode crash, game-system-events killed"
out=$(mktemp -d)
"$run" "$@" --no-build --out "$out" --no-css-ready \
    --core-jq '.FailMode = "crash"' \
    --gamedata-jq "$kill_game_system" \
    --expect-log 'FailMode is crash and 1 of [0-9]+ core Engine Features is disabled on build [^,]+, stopping the server:' \
    --expect-log '^ +cssharp\.server/game-system-events disabled \(disabled by gamedata: e2e kill\)' \
    --expect-log 'FailMode is crash and a core Engine Feature is disabled: stopping the server' \
    --expect-no-log 'Finished loading plugin|Host activate: Loading'
rc=$?
if [ "$rc" != 2 ] || ! grep -q '^server_exit_code: 1$' "$out/summary.txt"; then
    echo "kill-switch: expected a boot failure with server exit code 1 (run.sh exit $rc)"; status=1
fi

echo "== kill-switch: FailMode crash, game-system-events killed and forced on"
"$run" "$@" --no-build --no-smoke --exec css_engine_features \
    --core-jq '.FailMode = "crash" | .ForceEnableEngineFeatures = ["cssharp.server/game-system-events"]' \
    --gamedata-jq "$kill_game_system" \
    --expect-log 'Engine Feature cssharp\.server/game-system-events is forced on by operator \(ForceEnableEngineFeatures\) although Gamedata disables it: e2e kill' \
    --expect-log 'Engine Features for build [^:]+: all [0-9]+ available' \
    --expect-log '^ +cssharp\.server/game-system-events: e2e kill, forced on by operator \(ForceEnableEngineFeatures\)' \
    --expect-log 'Initialized GameSystem' \
    --expect-no-log 'FailMode is crash' ||
    status=1

exit $status
