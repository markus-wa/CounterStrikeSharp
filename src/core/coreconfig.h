/*
 *  This file is part of CounterStrikeSharp.
 *  CounterStrikeSharp is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  CounterStrikeSharp is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CounterStrikeSharp.  If not, see <https://www.gnu.org/licenses/>. *
 */

#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "core/globals.h"

namespace counterstrikesharp {

class CCoreConfig
{
  public:
    std::vector<std::string> PublicChatTrigger = { std::string("!") };
    std::vector<std::string> SilentChatTrigger = { std::string("/") };
    bool FollowCS2ServerGuidelines = true;
    bool PluginHotReloadEnabled = true;
    bool PluginAutoLoadEnabled = true;
    std::string ServerLanguage = "en";
    bool UnlockConCommands = true;
    bool UnlockConVars = true;
    // The Gamedata Feed (ADR-0006, core/gamedata_feed.h): whether boot fetches a signed snapshot for this build, from
    // where, and which public keys (64 hex digits each) replace the built-in ones when set.
    bool GamedataFeedEnabled = true;
    // shortcut: a placeholder that never resolves (RFC 2606 .invalid), so the default boots on bundled with a warning;
    // ticket 09 sets the fork's feed URL.
    std::string GamedataFeedUrl = std::string("http://gamedata-feed.invalid");
    std::optional<std::vector<std::string>> GamedataFeedPublicKeys;
    // The removed legacy updater's keys (AutoUpdateEnabled, AutoUpdateURL) present in the file: ignored, reported once.
    std::vector<std::string> LegacyConfigKeys;
    // What happens when a core Engine Feature is disabled after boot resolution: "disable" (fail soft, ADR-0002) or
    // "crash" (stop the server after the startup report). Unknown values fall back to "disable" with a warning.
    std::string FailMode = std::string("disable");
    // Whether a Slot Binding whose window does not match at its Gamedata index may Heal (Linux; Windows has no Vtable Check).
    bool HealEnabled = true;
    // Engine Features the operator keeps available although a Gamedata Kill Switch disables them (never a failed Binding).
    std::vector<std::string> ForceEnableEngineFeatures;

    using json = nlohmann::json;
    CCoreConfig(const std::string& path);
    ~CCoreConfig();

    bool Init(char* conf_error, int conf_error_size);
    const std::string GetPath() const;

    bool IsFailModeCrash() const { return FailMode == "crash"; }

    bool IsSilentChatTrigger(const std::string& message, std::string& prefix) const;
    bool IsPublicChatTrigger(const std::string& message, std::string& prefix) const;

  private:
    void ReadOperatorSettings();
    bool IsTriggerInternal(std::vector<std::string> triggers, const std::string& message, std::string& prefix) const;

  private:
    std::string m_sPath;
    json m_json;
};

} // namespace counterstrikesharp
