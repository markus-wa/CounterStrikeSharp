// Kill Switches (ADR-0006): Gamedata deliberately disables a Binding (blank value) or a whole Engine Feature
// ("disabledEngineFeatures"), and the operator can force a killed Engine Feature back on (ForceEnableEngineFeatures).
#include <doctest.h>

#include <string>
#include <vector>

#include "core/binding_resolver.h"

using namespace counterstrikesharp::bindings;
using nlohmann::json;

namespace {
BindingResult ResolveOne(const json& gamedata, const BindingDeclaration& declaration, Platform platform, const char* os)
{
    ModuleImage server;
    server.findSymbol = [](std::string_view) -> std::uintptr_t { return 0x1234; };
    return BuiltinResolver(platform)
        .Resolve("b", ParseGamedata(gamedata, os), { { "server", server } }, std::span(&declaration, 1))
        .Bindings()
        .at(0);
}

BindingResult Resolved(std::string key)
{
    BindingResult result;
    result.key = std::move(key);
    result.status = BindingStatus::Resolved;
    return result;
}

BindingResult Failed(std::string key, FailureReason reason)
{
    BindingResult result;
    result.key = std::move(key);
    result.status = BindingStatus::Failed;
    result.reason = reason;
    return result;
}

const EngineFeatureDeclaration kSayHooks{ "cssharp.chat/say-hooks", { { "Host_Say", BindingKind::Signature } } };
const EngineFeatureDeclaration kGameSystem{ "cssharp.server/game-system-events",
                                            { { "IGameSystem_InitAllSystems_pFirst", BindingKind::Signature } } };
} // namespace

TEST_CASE("A blank Binding fails as disabled by Gamedata, distinct from missing, on Linux and Windows")
{
    const json gamedata = {
        { "Host_Say", { { "signatures", { { "library", "server" }, { "linux", "" }, { "windows", "" } } } } },
        { "Teleport", { { "offsets", { { "linux", nullptr }, { "windows", nullptr } } } } },
        { "ISource2Server::GameFrame",
          { { "offsets", { { "linux", nullptr }, { "windows", nullptr } } },
            { "vtable", { { "class", "CSource2Server" }, { "library", "server" } } } } },
    };
    const BindingDeclaration headerSlot{ "ISource2Server::GameFrame", BindingKind::Slot };
    const BindingDeclaration classSlot{ "ISource2Server::GameFrame", BindingKind::Slot, {}, false };

    for (const auto& [platform, os] : { std::pair{ Platform::Linux, "linux" }, std::pair{ Platform::Windows, "windows" } })
    {
        CAPTURE(os);
        CHECK(ResolveOne(gamedata, { "Host_Say", BindingKind::Signature }, platform, os).reason == FailureReason::DisabledByGamedata);
        CHECK(ResolveOne(gamedata, { "Teleport", BindingKind::Offset }, platform, os).reason == FailureReason::DisabledByGamedata);
        const auto slot = ResolveOne(gamedata, headerSlot, platform, os);
        CHECK(slot.status == BindingStatus::Failed);
        CHECK(slot.reason == FailureReason::DisabledByGamedata);
        CHECK(ResolveOne(gamedata, classSlot, platform, os).reason == FailureReason::DisabledByGamedata);
        // A key or per-OS entry that is absent is still missing, not disabled.
        CHECK(ResolveOne(json::object(), { "Host_Say", BindingKind::Signature }, platform, os).reason ==
              FailureReason::MissingFromGamedata);
    }

    const BindingResult blank = ResolveOne(gamedata, { "Host_Say", BindingKind::Signature }, Platform::Linux, "linux");
    CHECK(DescribeFailure(blank) == "disabled by gamedata");
    CHECK(nlohmann::json::parse(ResolutionReportToJson(ResolutionReport("b", { blank }), {}))["bindings"][0]["reason"] ==
          "DisabledByGamedata");
}

TEST_CASE("disabledEngineFeatures is read as Kill Switches, not as a Binding")
{
    const json gamedata = { { "Host_Say", { { "signatures", { { "library", "server" }, { "linux", "@Host_Say" } } } } },
                            { "disabledEngineFeatures",
                              { { { "id", "cssharp.server/game-system-events" }, { "reason", "crashes on map change" } },
                                { { "id", "cssharp.chat/say-hooks" } },
                                { { "reason", "no id: ignored" } },
                                "not an object" } } };

    CHECK(ParseGamedata(gamedata, "linux").count("disabledEngineFeatures") == 0);

    const auto kills = ParseKillSwitches(gamedata);
    REQUIRE(kills.size() == 2);
    CHECK(kills[0].id == "cssharp.server/game-system-events");
    CHECK(kills[0].reason == "crashes on map change");
    CHECK_FALSE(kills[0].forced);
    CHECK(kills[1].id == "cssharp.chat/say-hooks");
    CHECK(kills[1].reason == "no reason given");

    CHECK(ParseKillSwitches(json::object()).empty());
    CHECK(ParseKillSwitches({ { "disabledEngineFeatures", "oops" } }).empty());
    CHECK(ParseKillSwitches(json::array()).empty());
}

TEST_CASE("A feature-level Kill Switch disables only that Engine Feature, with its reason")
{
    const ResolutionReport report("b", { Resolved("Host_Say"), Resolved("IGameSystem_InitAllSystems_pFirst") },
                                  true, { { "cssharp.server/game-system-events", "crashes on map change" } });

    const auto killed = EvaluateEngineFeature(report, kGameSystem);
    CHECK_FALSE(killed.available);
    CHECK(killed.failedBinding == nullptr);
    REQUIRE(killed.killSwitch != nullptr);
    CHECK(killed.killSwitch->reason == "crashes on map change");
    CHECK(DescribeUnavailable(report, kGameSystem) ==
          "Engine Feature cssharp.server/game-system-events is disabled (disabled by gamedata: crashes on map change)");

    CHECK(EvaluateEngineFeature(report, kSayHooks).available);
}

TEST_CASE("Forcing on re-enables a killed Engine Feature, never one whose Binding failed")
{
    const json gamedata = { { "disabledEngineFeatures",
                              { { { "id", "cssharp.server/game-system-events" }, { "reason", "crashes" } },
                                { { "id", "cssharp.chat/say-hooks" }, { "reason", "wrong hook" } } } } };
    const std::vector<std::string> forceEnabled{ "cssharp.server/game-system-events", "cssharp.chat/say-hooks" };
    const auto kills = ForceEnable(ParseKillSwitches(gamedata), forceEnabled);
    REQUIRE(kills.size() == 2);
    CHECK(kills[0].forced);
    CHECK(kills[1].forced);
    CHECK_FALSE(ForceEnable(ParseKillSwitches(gamedata), {})[0].forced);

    const ResolutionReport report("b",
                                  { Failed("Host_Say", FailureReason::DisabledByGamedata), Resolved("IGameSystem_InitAllSystems_pFirst") },
                                  true, kills);

    CHECK(EvaluateEngineFeature(report, kGameSystem).available);
    CHECK(DescribeUnavailable(report, kGameSystem) == "Engine Feature cssharp.server/game-system-events is available");

    // Forcing applies to the feature-level kill only: the blank Binding has nothing to call.
    const auto sayHooks = EvaluateEngineFeature(report, kSayHooks);
    CHECK_FALSE(sayHooks.available);
    REQUIRE(sayHooks.failedBinding != nullptr);
    CHECK(sayHooks.failedBinding->reason == FailureReason::DisabledByGamedata);
    CHECK(DescribeUnavailable(report, kSayHooks) == "Engine Feature cssharp.chat/say-hooks is disabled (Host_Say: disabled by gamedata)");
}

TEST_CASE("Every forced Engine Feature warns, and so does a force that names no Kill Switch")
{
    const std::vector<std::string> forceEnabled{ "cssharp.server/game-system-events", "cssharp.chat/say-hooks" };
    const auto kills = ForceEnable(
        ParseKillSwitches({ { "disabledEngineFeatures", { { { "id", "cssharp.server/game-system-events" }, { "reason", "crashes" } } } } }),
        forceEnabled);

    CHECK(FormatForceEnableWarnings(kills, forceEnabled) ==
          std::vector<std::string>{ "Engine Feature cssharp.server/game-system-events is forced on by operator "
                                    "(ForceEnableEngineFeatures) although Gamedata disables it: crashes",
                                    "ForceEnableEngineFeatures names cssharp.chat/say-hooks, which no Kill Switch disables; ignored" });
    CHECK(FormatForceEnableWarnings(kills, {}).empty());
}

TEST_CASE("Kill Switches cross to managed with the report")
{
    const ResolutionReport report("b", {}, true,
                                  { { "cssharp.server/game-system-events", "crashes", true }, { "cssharp.chat/say-hooks", "wrong hook" } });

    const auto json = nlohmann::json::parse(ResolutionReportToJson(report, {}));

    CHECK(json["killSwitches"] ==
          nlohmann::json::array({ { { "id", "cssharp.server/game-system-events" }, { "reason", "crashes" }, { "forced", true } },
                                  { { "id", "cssharp.chat/say-hooks" }, { "reason", "wrong hook" }, { "forced", false } } }));
    CHECK(nlohmann::json::parse(ResolutionReportToJson(ResolutionReport("b", {}), {}))["killSwitches"] == nlohmann::json::array());
}
