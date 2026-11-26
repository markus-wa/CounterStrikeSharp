# 05: Cache, carry-forward and rollback protection

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Gamedata Source, Gamedata Feed format). ADR-0006.

**What to build:** Verified snapshots are kept per build and reused.

- **Cache:** one immutable file per build and OS, plus the latest accepted index and signature. Writes are atomic (temp file, fsync, rename). Every file is re-verified on load. The last 20 builds are kept and older ones pruned at boot.
- **Selection:**
  1. A verified snapshot for exactly this build (fresh or cached).
  2. Otherwise the newest of the cached snapshots and the bundled file, by `SourceRevision`; the bundled file carries a `sourceRevision` stamp. Reported as "carried forward from X".
  3. Otherwise bundled.
- **Rollback:** the highest accepted `seq` is stored, and a lower one is rejected. Each release embeds a `seq` floor, so deleting the state file cannot go below it.
- **Legacy cleanup:** `gamedata.etag` is deleted, and `AutoUpdateEnabled` and `AutoUpdateURL` log one deprecation line.

**Blocked by:** 03

**Status:** ready-for-agent

**Tier:** 2 add-on · must

- [ ] Doctest (fake transport, temp dir):
  - exact build beats carry-forward
  - newer bundled beats older cache
  - a tampered cache file is rejected and re-fetched or skipped
  - pruning keeps 20
  - an interrupted write leaves no partial file
- [ ] Doctest: lower `seq` rejected; `seq` below the release floor rejected after the state file is deleted
- [ ] Bundled `gamedata.json` gains a `sourceRevision` stamp; a file without the stamp counts as oldest
- [ ] Doctest: `gamedata.etag` removed; one deprecation line for the legacy keys
- [ ] End to end: feed down at second boot → cached exact-build snapshot used, reported as cache
