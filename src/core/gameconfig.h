#pragma once

#include "core/binding_resolver.h"
#include "core/globals.h"
#include "core/memory_module.h"
#include <tier0/keyvalues.h>

#include <string>
#include <unordered_map>

#undef snprintf
#include <nlohmann/json.hpp>

namespace counterstrikesharp {

class CGameConfig
{
  public:
    using json = nlohmann::json;
    CGameConfig(const std::string& path);
    ~CGameConfig();

    // Reads the Gamedata file at the constructor's path.
    bool Init(char* conf_error, int conf_error_size);
    // From a Gamedata file's bytes already in memory; the constructor's path then only names it.
    bool InitFromBytes(std::string_view bytes, char* conf_error, int conf_error_size);
    const std::string GetPath();
    // Gamedata as the Binding resolver reads it, for this OS. Core code never looks keys up itself: it asks the
    // Resolution Report (core/core_bindings.h).
    const bindings::Gamedata& GetGamedata() const { return m_gamedata; }

    static std::string GetDirectoryName(const std::string& directoryPathInput);
    static std::vector<int16_t> HexToByte(std::string_view src);

  private:
    std::string m_sPath;
    bindings::Gamedata m_gamedata;
};

} // namespace counterstrikesharp
