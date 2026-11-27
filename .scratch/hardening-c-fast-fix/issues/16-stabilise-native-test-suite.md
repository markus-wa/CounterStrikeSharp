# 16: Stabilise the native xUnit suite on the laptop server

Spec: `.scratch/hardening-c-fast-fix/spec.md` (CI per build). Found while building tickets 11 and 12.

**Problem:** The native xUnit suite (`managed/CounterStrikeSharp.Tests.Native`, run by `eng/e2e/run.sh` as the smoke test) fails a few tests at random on an unbroken boot. During ticket 12's dry run, the same checkout booted twice failed 2 tests on one boot and 6 different tests on the other. Recent e2e runs show 0–5 random failures each. As a result:

- CI per build (tickets 11/12) cannot treat a failed test as a failure, so failed tests are listed in the PR body but never set a label (`shortcut:` in `Boot.failed`, data repo `tools/per_build.py`).
- Failed tests are part of each boot record, so the "no change" label almost never fires: two boots of the same build rarely fail the same tests.
- Every e2e case that uses `--require-smoke-pass` is unreliable.

**Evidence:** failed-test counts across the 364 runs in `~/.cache/cssharp-e2e/runs/*/smoke-results.json`. This includes runs that broke Gamedata on purpose, so the counts overstate the flakes. The top failures are:

- `RealConVarTests.CreatedConVarIsAccessibleFromServerConsole`: 209
- `ListenerTests.Listener_ConVarChanged`: 145. Ticket 11's dry run saw it fail on its own, so it may fail every time rather than at random.
- `CommandTests.CanTriggerCommandsWithSilentChatTrigger`: 121
- `GameEventTests.CanRegisterAndDeregisterEventHandlers`: 112
- `CommandTests.IssueServerCommand`: 52
- `NativeObjectsTests.EnsureNativeHandle_IsFreed_Vector3`: 33

The ConVar, Command, GameEvent and Listener tests dominate. Many of them wait on a server frame, a console command or an event round trip, so timing is the likely cause.

**What to build:**

1. Classify. Boot an unbroken checkout N times (say 10) on the laptop server and record the pass rate of each test. Split the failures into always-failing (a real bug or a broken test), flaky (pass rate between 0 and 100 %), and failing only under a given e2e case. Record the run summary `summary.txt` with the e2e flags so later runs can be filtered (today it does not record them).
2. Fix the always-failing tests, or skip each one with its reason and a link to this ticket.
3. Fix the flaky tests at their cause. That means waiting on the frame, event or command result the test needs, with a bounded timeout, instead of a fixed delay. Do not hide flakes with retries.
4. Once the suite is stable, take failed tests back into CI per build: a failed test counts as a boot failure for the labels, and remove the `Boot.failed` shortcut. Until then, leave failed tests out of the boot record, so that "no change" compares only the Resolution Report.

**Blocked by:** None

**Status:** ready-for-agent

**Tier:** 4 add-on · later

- [ ] Per-test pass rates over 10 unbroken boots recorded in this ticket's comments, and e2e `summary.txt` records the run's flags
- [ ] Always-failing tests fixed or skipped with a reason
- [ ] 10 consecutive unbroken boots with 0 failed tests
- [ ] CI per build: failed tests count for the labels; a no-change build gets the "no change" label in the dry run on 1.41.9.7 → 1.42.0.1 when booted twice
