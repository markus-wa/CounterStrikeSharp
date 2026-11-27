#!/usr/bin/env bash
# End-to-end cases for a bad AutoUpdateURL in core.json (upstream #931: an https:// URL aborted the server).
#
#   eng/e2e/cases/auto-update-url.sh [run.sh options...]     e.g. --no-build
#
# https:      AutoUpdateEnabled with an https:// URL: the server boots and CounterStrikeSharp loads. The startup report
#             names the build by PatchVersion and SourceRevision.
# malformed:  the same with a malformed URL.
# On Linux the legacy updater does not run (it logs that it is unsupported); the guarded client it would use on
# Windows is covered by tests/native/http_client_test.cpp.
# Exits non-zero if any case fails; each case's output dir is printed by run.sh.
set -uo pipefail

run=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/run.sh

status=0

for url in "https://gamedata.cssharp.dev" "ftp://gamedata.cssharp.dev:99999999999"; do
    echo "== auto-update-url: $url"
    "$run" "$@" --no-smoke \
        --core-jq ".AutoUpdateEnabled = true | .AutoUpdateURL = \"$url\"" \
        --expect-log 'Bindings for build [0-9.]+ \(SourceRevision [0-9]+\): ' \
        --expect-log 'Engine Features for build [0-9.]+ \(SourceRevision [0-9]+\): ' \
        --expect-log 'Auto-update is not currently supported on this platform' ||
        { echo "auto-update-url: $url did not boot cleanly"; status=1; }
done

exit $status
