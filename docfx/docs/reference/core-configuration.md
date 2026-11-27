---
title: Core Configuration
description: Summary for core configuration values
---

# Core Configuration

Summary for core configuration values

## PublicChatTrigger

List of characters to use for public chat triggers.

## SilentChatTrigger

List of characters to use for silent chat triggers.

## FollowCS2ServerGuidelines

Per [CS2 Server Guidelines](https://blog.counter-strike.net/index.php/server_guidelines/), certain plugin
functionality will trigger all of the game server owner's Game Server Login Tokens
(GSLTs) to get banned when executed on a Counter-Strike 2 game server.

Enabling this option will block plugins from using functionality that is known to cause this.
This option only has any effect on CS2. Note that this does NOT guarantee that you cannot
receive a ban.

> [!NOTE]
> Disable this option at your own risk.

## PluginHotReloadEnabled

When enabled, plugins are automatically reloaded when their .dll file is updated.

## PluginAutoLoadEnabled

When enabled, plugins are automatically loaded from the plugins directory on server start.

## ServerLanguage

Configures the default language to use for server commands & messages. The format for the culture name based on RFC 4646 is `languagecode2-country`/`regioncode2`, where `languagecode2` is the two-letter language code and `country/regioncode2` is the two-letter subculture code. Examples include `ja-JP` for Japanese (Japan) and `en-US` for English (United States). Defaults to "en".

## UnlockConCommands

When enabled, will remove the `FCVAR_HIDDEN`,`FCVAR_DEVELOPMENTONLY`, `FCVAR_MISSING0`, `FCVAR_MISSING1`, `FCVAR_MISSING2`, `FCVAR_MISSING3` flags from all console commands.

## UnlockConVars

When enabled, will remove the `FCVAR_HIDDEN`,`FCVAR_DEVELOPMENTONLY`, `FCVAR_MISSING0`, `FCVAR_MISSING1`, `FCVAR_MISSING2`, `FCVAR_MISSING3` flags from all console variables.

## GamedataFeedEnabled

When enabled (the default, on Linux and Windows), CS# fetches the Gamedata for exactly the CS2 build the server runs from the signed Gamedata Feed at boot. The feed's index must carry a valid Ed25519 signature and the snapshot must match the size and sha256 it lists, or CS# boots on the bundled `gamedata.json` and logs why. The bundled file is never modified. The startup report has a `Gamedata:` line that says which was used. Set to `false` to always boot on the bundled file without any network access.

Verified snapshots are cached in `gamedata/cache/` (the newest 20 builds) and re-verified on every boot, so a server that restarts while the feed is unreachable boots on its cached snapshot. When the feed has no snapshot for the running build yet, CS# carries forward the newest of the cached snapshots and the bundled file (by `SourceRevision`). An index with a lower `seq` than one already accepted, or than the release was built with, is rejected.

This replaces `AutoUpdateEnabled` and `AutoUpdateURL`, which are ignored with a deprecation warning; the legacy `gamedata.etag` file is deleted.

## GamedataFeedUrl

The feed's base URL (`http://host[:port][/path]`). CS# reads `index.json`, `index.json.sig` and the snapshot files under it. Only plain `http://` is supported; integrity comes from the signature, not the transport. An `https://` or malformed URL logs a warning and CS# boots on the bundled file.

## GamedataFeedPublicKeys

Optional list of Ed25519 public keys (64 hex digits each) that may sign the feed's index, for operators who run their own feed. When set, it replaces the built-in keys.

## GamedataFeedPollMinutes

Minutes between checks for newer Gamedata while the server runs (default `15`; `0` turns the check off; ignored when `GamedataFeedEnabled` is `false`). Gamedata applies at boot only: when the feed has a newer verified snapshot for the running build, CS# stores it in `gamedata/cache/`, logs once that a restart will apply it, and raises the `OnGamedataUpdateAvailable` listener (build, seq and the Engine Features whose Gamedata changes) so an admin plugin can schedule the restart. CS# never restarts the server itself. The check runs off the game thread under the same limits and verification as the boot fetch.

## MaximumFrameTasksExecutedPerTick
The maximum amount of `NextFrame` and `NextWorldUpdate` tasks that can be executed on a single game frame. The queue totals are tracked separately. Defaults to 1024. This value can be reduced to prevent long frame burstiness.