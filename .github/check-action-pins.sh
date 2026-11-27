#!/usr/bin/env bash
# Fails unless every `uses:` in the workflow files names a local action (./...) or pins a full commit SHA
# (owner/repo@<40 hex>). The same check as the Gamedata data repo's .github/check-action-pins.sh.
#   .github/check-action-pins.sh [WORKFLOW...]    (default: the release workflow, build-and-publish.yml)
# shortcut: only the release workflow is checked (it builds the bundled Gamedata and the seq floor); upstream's other
# workflows still use tags, check them too once they are pinned.
set -euo pipefail

(($#)) || set -- "$(dirname "$0")/workflows/build-and-publish.yml"
unpinned=$(grep -HnE '^[[:space:]]*(-[[:space:]]+)?uses:' "$@" |
    sed -E 's/^([^:]+:[0-9]+):[[:space:]]*(-[[:space:]]+)?uses:[[:space:]]*["'\'']?([^"'\''[:space:]]*).*/\1 \3/' |
    grep -vE ' (\./[^[:space:]]*|[^[:space:]@]+@[0-9a-f]{40})$' || true)
if [ -n "$unpinned" ]; then
    echo "Actions not pinned by commit SHA:" >&2
    echo "$unpinned" >&2
    exit 1
fi
