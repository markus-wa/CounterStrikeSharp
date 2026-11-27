#include "core/core_gamedata.h"

#include <fstream>
#include <iterator>

#include "core/core_bindings.h"
#include "core/coreconfig.h"
#include "core/gameconfig.h"
#include "core/globals.h"
#include "core/http_client.h"
#include "core/log.h"
#include "core/utils.h"

namespace counterstrikesharp {
namespace {

feed::ChosenGamedata chosen;

} // namespace

bool LoadCoreGamedata(std::string& error)
{
    const auto bundledPath = utils::GamedataDirectory() + "/gamedata.json";
    std::ifstream bundled(bundledPath, std::ios::binary);
    if (!bundled)
    {
        error = "Could not read '" + bundledPath + "'. Error: Gamedata file not found.";
        return false;
    }

    const http::HttpTransport transport;
    feed::GamedataSourceInputs inputs;
    inputs.transport = globals::coreConfig->GamedataFeedEnabled ? &transport : nullptr;
    inputs.feedUrl = globals::coreConfig->GamedataFeedUrl;
    inputs.publicKeys = globals::coreConfig->GamedataFeedPublicKeys;
    inputs.build = bindings::ReadBuildIdentity();
#ifdef _WIN32
    inputs.os = "windows";
#else
    inputs.os = "linux";
#endif
    inputs.bundled.assign(std::istreambuf_iterator<char>(bundled), std::istreambuf_iterator<char>());
    chosen = feed::ChooseGamedata(inputs);

    const auto provenance = feed::FormatProvenance(chosen.provenance);
    if (chosen.provenance.warnings.empty()) CSSHARP_CORE_INFO("{}", provenance);
    else
        CSSHARP_CORE_WARN("{}", provenance);

    globals::gameConfig = new CGameConfig(bundledPath);
    char parseError[255] = "";
    if (!globals::gameConfig->InitFromBytes(chosen.bytes, parseError, sizeof(parseError)))
    {
        error = "Could not read '" + bundledPath + "'. Error: " + parseError;
        return false;
    }
    return true;
}

const feed::ChosenGamedata& CoreGamedata() { return chosen; }

} // namespace counterstrikesharp
