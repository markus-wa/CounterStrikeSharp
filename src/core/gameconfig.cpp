#include "core/gameconfig.h"

#include "core/signature.h"

namespace counterstrikesharp {

CGameConfig::CGameConfig(const std::string& path) { m_sPath = path; }

CGameConfig::~CGameConfig() = default;

bool CGameConfig::InitFromBytes(std::string_view bytes, char* conf_error, int conf_error_size)
{
#if _WIN32
    constexpr auto platform = "windows";
#else
    constexpr auto platform = "linux";
#endif

    try
    {
        // Core Bindings are resolved from this alone (core/core_bindings.h), which also reports any key it cannot use.
        m_gamedata = bindings::ParseGamedataBytes(bytes, platform);
    }
    catch (const std::exception& ex)
    {
        V_snprintf(conf_error, conf_error_size, "Failed to parse gamedata file: %s", ex.what());
        return false;
    }
    return true;
}

const std::string CGameConfig::GetPath() { return m_sPath; }

std::string CGameConfig::GetDirectoryName(const std::string& directoryPathInput)
{
    std::string directoryPath = std::string(directoryPathInput);

    size_t found = std::string(directoryPath).find_last_of("/\\");
    if (found != std::string::npos)
    {
        return std::string(directoryPath, found + 1);
    }
    return "";
}

std::vector<int16_t> CGameConfig::HexToByte(std::string_view src) { return ParseSignature(src); }
} // namespace counterstrikesharp
