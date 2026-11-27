# 01: Bundle-cut follow-ups (placeholder)

Source: a deep dive into which options A–K cut PGL's emergency bundles fastest. The backtest is the "CSharspy Hardening Backtest" artifact. Its findings were checked by agents against `hardening/impl`, the PGL monorepo plugin and git history. PGL-side levers are in PGL-ESPORTS/esportal-monorepo#1420 (needs-triage). This is a placeholder for the rest; split it into real tickets during triage.

**Status:** needs-triage

## Candidates

1. **F: Cut a fork release from `hardening/impl`** (spec A and spec B, all tickets resolved).
   - `hardening/impl` sits on v1.0.380+3 and lacks v1.0.381 (the s2sdk patch for 1.42.0.1). Merge upstream first.
   - Human step: tag and publish.
   - The fastest real cut: B removes #22 and #39 (and likely #37, a slot shift labelled C): −2 to −3 bundles once PGL pins it.
   - Size S · risk low-med.
2. **G: The compiled `IGameSystem` vtable no longer crashes on an SDK slot shift.** #21, #24, #28 and #29 were hl2sdk `igamesystem.h` virtual inserts (dbf150f1, 0f84fcf1, 3aa19369, 2e4e31a7). The signature still matched, so spec A's game-system-events check did not trip and the server crashed. Spec B scopes this out (`hardening-b-boot-verify/spec.md:167`); only spec C's Kill Switch catches it, by hand.
   - **Cheap:** compare the compiled `CGameSystem` vtable length with a game RTTI vtable (`CAutoGameSystem`?). On a mismatch, turn game-system-events off and don't register. Size S · risk low-med. Unknown: whether a usable reference vtable exists.
   - **Full:** drop the compiled `CGameSystem`. Hook the game's `CGameRulesGameSystem` vtable for precache and pre/post entity think as B Slot Bindings, as ModSharp does. Size M · risk med: event order, base-class stub slots B can't fingerprint, live map-change check.
   - Effect: −2 to −4 bundles. #21 and #28 are solid. #24 also needs the `UTIL_ClientPrintAll` prototype change handled. #29 also needs spec E's entity-listener struct.
   - OnMapStart is not involved. It comes from the StartupServer hook (`src/mm_plugin.cpp:177,218-232`), which B already heals.
3. **H: plugin gamedata in the feed.** Not ticketed, and out of scope in spec C. It needs a separate trust root for plugin keys, because under ADR-0006 operator keys replace the built-in keys. For PGL the cheaper route is dropping the `ConnectClient` hook in the plugin. Likely wontfix here.
4. **J: Correct the backtest artifact:**
   - G is misattributed: map start doesn't come from `IGameSystem`. Credit #21/#24/#28/#29 to item 2 or C's Kill Switch, and re-run the "11 of 16 blocked by map start" paragraph and card F4.
   - #14 is not gamedata-only. `cdd2a827` changed `getGlobalVars` in native code, so D is −1, A–E leaves about 12, and core is 7.
   - #37 belongs to B, not C (IVEngineServer2 +1 slot).
   - H (#38) needs C's Kill Switch, not just A1/A3, plus plugin catches.
   - E is credited with 2 Metamod-only bundles, but the AC plugin kept Metamod loaded then.
   - Rename its option letters A–K and incident letters to the registry in `incremental-hardening/README.md` (workstream letters; categories as words).
   - The events lane has 49 entries and 31 routine, not 48 and 28.

## Not here

- Spec C (signed feed, −5 to −6 bundles, the largest structural lever) is already ticketed in `.scratch/hardening-c-fast-fix/`.
- Backtest D (tier0 + structs, −1: #16) is repo spec E, investigation ticket `.scratch/hardening-e-tier0-structs/issues/`.
- Backtest E (own loader) is repo spec D. It cuts 0 bundles, so it doesn't belong on a bundle-cut list.
