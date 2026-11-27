# 11: CI per build tracer

Spec: `.scratch/hardening-c-fast-fix/spec.md` (CI per build). ADR-0001.

**What to build:** When the build archiver (PoC repo) archives a new CS2 build, it emits a new-build event, and a CI job on the laptop picks it up. The job:

1. Boots a headless Linux dedicated server on the new build with the latest fork release and the feed's current Gamedata.
2. Collects the Resolution Report and runs the native xUnit suite.
3. Opens a PR on the data repo that adds a snapshot entry for the new `SourceRevision` (the carried-forward Gamedata) with the report diff against the previous build and the test results in the body. The PR is opened even when nothing changed, labelled "no change".

A human merges. Nothing is auto-merged. The PR is the notification.

**Blocked by:** 08; external: the PoC archiver's new-build event (agree on the event shape with that repo's ticket)

**Status:** resolved

**Tier:** 3 · must

- [x] The event contract (marker file or hook script: build id, paths) is documented in both repos
- [x] Dry run on the archived pair 1.41.9.7 → 1.42.0.1 produces the expected PR payload (report diff shows the ISource2Server Heals), skipped when the archive is absent
- [x] A no-change build produces a "no change" PR
- [x] A boot failure or test failure is reported in the PR body, not swallowed
- [x] Runbook: how to start, stop and re-run the job on the laptop; Markus named as owner

## Comments

- 2026-11-27: Resolved. Data repo `tools/per_build.py` (carry_forward → ensure_server → boot_release → propose), systemd user timer, README runbook (owner: Markus). New-build event = `~/dev/cs2-builds/<id>/archived.json` written last; text for the PoC archiver ticket in `~/dev/other/cssharp-hardening-research/spec-c-notes/poc-archiver-event-text.md` (not placed: PoC repo has uncommitted edits). Fork: `run.sh` refreshes the server copy on build change and no longer leaks `build.lock`. Dry run 1.41.9.7 → 1.42.0.1 shows the 10 ISource2Server Heals. Not verified live: steamcmd update, real `gh pr create`, systemd units.
