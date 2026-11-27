#include "core/core_gamedata.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

#include "core/core_bindings.h"
#include "core/coreconfig.h"
#include "core/gameconfig.h"
#include "core/globals.h"
#include "core/http_client.h"
#include "core/log.h"
#include "core/tick_scheduler.h"
#include "core/utils.h"
#include "scripting/callback_manager.h"

namespace counterstrikesharp {
namespace {

feed::GamedataSourceInputs inputs; // this boot's, without a transport: the poll asks the same feed for the same build
feed::ChosenGamedata chosen;

ScriptCallback* updateAvailable = nullptr;

// The poll thread and everything it touches. On the heap and deleted only by StopGamedataPoll: a process that exits
// without unloading the plugin (the server's quit) ends the thread with it, and must not run the destructors of a
// joinable thread (std::terminate) or of a condition variable it waits on (blocks the exit).
struct Poll
{
    feed::GamedataSourceInputs inputs; // without a transport: each poll makes its own
    std::string running; // the Gamedata this boot runs on
    std::chrono::minutes interval;
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    std::thread thread;
};
Poll* poll = nullptr;

std::string Join(const std::vector<std::string>& items)
{
    std::string joined;
    for (const auto& item : items)
        joined += (joined.empty() ? "" : ", ") + item;
    return joined.empty() ? "none" : joined;
}

// On the game thread.
void RaiseUpdateAvailable(const feed::GamedataUpdate& update)
{
    if (updateAvailable == nullptr || updateAvailable->GetFunctionCount() == 0) return;
    // Managed maps the keys and Kill Switches to Engine Features of both catalogs (ChangedEngineFeatures).
    const auto changes = nlohmann::json{ { "keys", update.changes.keys }, { "killSwitches", update.changes.killSwitches } }.dump();
    updateAvailable->ScriptContext().Reset();
    updateAvailable->ScriptContext().Push(update.build.c_str());
    updateAvailable->ScriptContext().Push(update.seq);
    updateAvailable->ScriptContext().Push(changes.c_str());
    updateAvailable->Execute();
}

void PollLoop(Poll& state)
{
    std::string etag;
    std::unique_lock lock(state.mutex);
    while (!state.wake.wait_for(lock, state.interval, [&] { return state.stop; }))
    {
        lock.unlock();
        auto inputs = state.inputs;
        const http::HttpTransport transport; // one fetch: its own deadline and byte budget
        inputs.transport = &transport;
        const auto result = feed::PollGamedataFeed(inputs, state.running, etag);
        etag = result.etag;
        for (const auto& warning : result.warnings)
            CSSHARP_CORE_WARN("Gamedata Feed poll: {}", warning);
        if (const auto& update = result.update)
        {
            CSSHARP_CORE_INFO("Newer Gamedata for build {} (seq {}) is staged in the Gamedata cache: a restart will apply it. Changed "
                              "Gamedata keys: {}; changed Kill Switches: {}",
                              update->build, update->seq, Join(update->changes.keys), Join(update->changes.killSwitches));
            // shortcut: game frames drain this queue, and a hibernating server may not run them, so the event can wait until
            // the server wakes; drain it from UpdateWhenNotInGame too if admins need it on an empty server.
            globals::tickScheduler.schedule(0, [update = *update] { RaiseUpdateAvailable(update); });
        }
        lock.lock();
    }
}

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

    inputs.feedUrl = globals::coreConfig->GamedataFeedUrl;
    inputs.publicKeys = globals::coreConfig->GamedataFeedPublicKeys;
    inputs.build = bindings::ReadBuildIdentity();
#ifdef _WIN32
    inputs.os = "windows";
#else
    inputs.os = "linux";
#endif
    inputs.bundled.assign(std::istreambuf_iterator<char>(bundled), std::istreambuf_iterator<char>());
    inputs.gamedataDir = utils::GamedataDirectory();
    inputs.seqFloor = CSSHARP_GAMEDATA_SEQ_FLOOR;
    inputs.legacyConfigKeys = globals::coreConfig->LegacyConfigKeys;
    {
        const http::HttpTransport transport;
        auto bootInputs = inputs;
        bootInputs.transport = globals::coreConfig->GamedataFeedEnabled ? &transport : nullptr;
        chosen = feed::ChooseGamedata(bootInputs);
    }

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

void StartGamedataPoll()
{
    updateAvailable = globals::callbackManager.CreateCallback("OnGamedataUpdateAvailable");
    const auto minutes = globals::coreConfig->GamedataFeedPollMinutes;
    if (!globals::coreConfig->GamedataFeedEnabled || minutes <= 0) return;
    poll = new Poll{ inputs, chosen.bytes, std::chrono::minutes(minutes) };
    poll->thread = std::thread(PollLoop, std::ref(*poll));
}

void StopGamedataPoll()
{
    if (poll != nullptr)
    {
        {
            const std::lock_guard lock(poll->mutex);
            poll->stop = true;
        }
        poll->wake.notify_all();
        poll->thread.join();
        delete poll;
        poll = nullptr;
    }
    if (updateAvailable != nullptr) globals::callbackManager.ReleaseCallback(updateAvailable);
    updateAvailable = nullptr;
}

} // namespace counterstrikesharp
