# 11: CI per build tracer

Spec: `.scratch/hardening-c-fast-fix/spec.md` (CI per build). ADR-0001.

**What to build:** When the build archiver (PoC repo) archives a new CS2 build, it emits a new-build event, and a CI job on the laptop picks it up. The job:

1. Boots a headless Linux dedicated server on the new build with the latest fork release and the feed's current Gamedata.
2. Collects the Resolution Report and runs the native xUnit suite.
3. Opens a PR on the data repo that adds a snapshot entry for the new `SourceRevision` (the carried-forward Gamedata) with the report diff against the previous build and the test results in the body. The PR is opened even when nothing changed, labelled "no change".

A human merges. Nothing is auto-merged. The PR is the notification.

**Blocked by:** 08; external: the PoC archiver's new-build event (agree on the event shape with that repo's ticket)

**Status:** ready-for-agent

**Tier:** 3 · must

- [ ] The event contract (marker file or hook script: build id, paths) is documented in both repos
- [ ] Dry run on the archived pair 1.41.9.7 → 1.42.0.1 produces the expected PR payload (report diff shows the ISource2Server Heals), skipped when the archive is absent
- [ ] A no-change build produces a "no change" PR
- [ ] A boot failure or test failure is reported in the PR body, not swallowed
- [ ] Runbook: how to start, stop and re-run the job on the laptop; Markus named as owner
