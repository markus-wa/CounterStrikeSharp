using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Core.Attributes;
using CounterStrikeSharp.API.Core.Bindings;
using CounterStrikeSharp.API.Core.Plugin;
using Microsoft.Extensions.Logging;

namespace CounterStrikeSharp.API.Tests;

// Kill Switches (ADR-0006) through the query API, the startup report, Required Engine Features and FailMode, from
// hand-built Resolution Reports (no native calls). Uses static state (EngineFeatures.Current, StopServer).
[Collection(nameof(EngineFeaturesApiTests))]
public class KillSwitchTests : IDisposable
{
    private const string BuildId = "1.42.0.1/2000950";

    private readonly CapturingLogger _logger = new();
    private readonly Action _originalStop = EngineFeatures.StopServer;
    private int _stops;

    public KillSwitchTests()
    {
        EngineFeatures.StopServer = () => _stops++;
    }

    public void Dispose()
    {
        EngineFeatures.StopServer = _originalStop;
        EngineFeatures.Reset();
    }

    private static readonly EngineFeatureDeclaration SayHooks = new("cssharp.chat/say-hooks", [BindingDeclaration.Signature("Host_Say")]);

    private static readonly EngineFeatureDeclaration GameSystem =
        new("cssharp.server/game-system-events", [BindingDeclaration.Signature("IGameSystem_InitAllSystems_pFirst")]);

    private static readonly EngineFeatureDeclaration Respawn = new("cssharp.player/respawn", [BindingDeclaration.Signature("CBasePlayerPawn_Respawn")]);

    private static BindingResult Resolved(string key) => new(key, BindingKind.Signature, true, BindingFailureReason.None, 1, 0x1000, 0, "server");

    private static BindingResult Blank(string key) =>
        new(key, BindingKind.Signature, false, BindingFailureReason.DisabledByGamedata, 0, 0, 0, "server");

    private static readonly BindingResult[] AllResolved =
        [Resolved("Host_Say"), Resolved("IGameSystem_InitAllSystems_pFirst"), Resolved("CBasePlayerPawn_Respawn")];

    private static EngineFeatureSet Set(BindingResult[] bindings, params EngineFeatureKill[] kills) =>
        new(new ResolutionReport(BuildId, bindings, [SayHooks, GameSystem], killSwitches: kills), [Respawn]);

    private static readonly EngineFeatureKill GameSystemKilled = new("cssharp.server/game-system-events", "crashes on map change");

    [Fact]
    public void AKilledEngineFeatureIsDisabledByGamedataWithItsReason()
    {
        var features = Set(AllResolved, GameSystemKilled);

        var status = features.GetStatus("cssharp.server/game-system-events");
        Assert.False(status.IsAvailable);
        Assert.True(status.IsDeclared);
        Assert.Null(status.FailedBinding);
        Assert.Equal(BindingFailureReason.DisabledByGamedata, status.Reason);
        Assert.Equal("crashes on map change", status.KillSwitchReason);
        Assert.False(status.ForcedOn);
        Assert.Equal("disabled by gamedata: crashes on map change", status.Describe());
        Assert.Equal("cssharp.server/game-system-events disabled (disabled by gamedata: crashes on map change)", status.ToString());

        Assert.True(features.IsAvailable("cssharp.chat/say-hooks"));
        Assert.True(features.IsAvailable("cssharp.player/respawn"));
    }

    [Fact]
    public void AManagedEngineFeatureCanBeKilledToo()
    {
        Assert.False(Set(AllResolved, new EngineFeatureKill("cssharp.player/respawn", "teleports to origin")).IsAvailable("cssharp.player/respawn"));
    }

    [Fact]
    public void CallingIntoAKilledEngineFeatureThrowsTheTypedExceptionWithTheReason()
    {
        EngineFeatures.Current = Set(AllResolved, GameSystemKilled);

        var ex = Assert.Throws<EngineFeatureUnavailableException>(() => EngineFeatures.Current.ThrowIfUnavailable(GameSystem.Name));

        Assert.Equal("Engine Feature cssharp.server/game-system-events is disabled (disabled by gamedata: crashes on map change)", ex.Message);
        Assert.Equal(BindingFailureReason.DisabledByGamedata, ex.Reason);
        Assert.Null(ex.Binding);
    }

    [Fact]
    public void ABlankBindingDisablesItsEngineFeatureAsDisabledByGamedata()
    {
        var status = Set([Blank("Host_Say"), Resolved("IGameSystem_InitAllSystems_pFirst")]).GetStatus("cssharp.chat/say-hooks");

        Assert.False(status.IsAvailable);
        Assert.Equal("Host_Say", status.FailedBinding);
        Assert.Equal(BindingFailureReason.DisabledByGamedata, status.Reason);
        Assert.Equal("Host_Say: disabled by gamedata", status.Describe());
    }

    [Fact]
    public void AForcedOnEngineFeatureIsAvailableAndSaysSo()
    {
        var status = Set(AllResolved, GameSystemKilled with { Forced = true }).GetStatus("cssharp.server/game-system-events");

        Assert.True(status.IsAvailable);
        Assert.True(status.ForcedOn);
        Assert.Equal(BindingFailureReason.None, status.Reason);
        Assert.Equal("available, forced on by operator (disabled by gamedata: crashes on map change)", status.Describe());
    }

    [Fact]
    public void ForcingOnNeverHidesAFailedBinding()
    {
        var failed = new BindingResult("IGameSystem_InitAllSystems_pFirst", BindingKind.Signature, false, BindingFailureReason.NotFound, 0, 0,
            0, "server");

        var status = Set([Resolved("Host_Say"), failed], GameSystemKilled with { Forced = true }).GetStatus(GameSystem.Name);

        Assert.False(status.IsAvailable);
        Assert.Equal("IGameSystem_InitAllSystems_pFirst: not found", status.Describe());
    }

    [RequiredEngineFeatures("cssharp.server/game-system-events")]
    private class RequiresGameSystem;

    [Fact]
    public void APluginRequiringAKilledEngineFeatureIsRefused()
    {
        var refusal = RequiredEngineFeatureCheck.GetRefusal(typeof(RequiresGameSystem), "Plugin.dll", Set(AllResolved, GameSystemKilled));

        Assert.Equal(
            "Plugin \"Plugin.dll\" requires Engine Features that are not available on build 1.42.0.1/2000950: " +
            "cssharp.server/game-system-events disabled (disabled by gamedata: crashes on map change).", refusal);
        Assert.Null(RequiredEngineFeatureCheck.GetRefusal(typeof(RequiresGameSystem), "Plugin.dll",
            Set(AllResolved, GameSystemKilled with { Forced = true })));
    }

    [Fact]
    public void FailModeCrashStopsTheServerForAKilledCoreEngineFeatureUnlessForcedOn()
    {
        EngineFeatures.Current = Set(AllResolved, GameSystemKilled);
        Assert.True(EngineFeatures.ApplyFailMode(_logger, FailMode.Crash));
        Assert.Equal(1, _stops);
        Assert.Contains("cssharp.server/game-system-events disabled (disabled by gamedata: crashes on map change)",
            Assert.Single(_logger.Entries).Message);

        EngineFeatures.Current = Set(AllResolved, GameSystemKilled with { Forced = true });
        Assert.False(EngineFeatures.ApplyFailMode(_logger, FailMode.Crash));
        Assert.Equal(1, _stops);
    }

    [Fact]
    public void TheStartupReportHasAKillSwitchBlock()
    {
        var report = Set([Blank("Host_Say"), Resolved("IGameSystem_InitAllSystems_pFirst"), Resolved("CBasePlayerPawn_Respawn")],
            GameSystemKilled, new EngineFeatureKill("cssharp.player/respawn", "teleports to origin", true)).FormatStartupReport();

        Assert.Equal(
            "Engine Features for build 1.42.0.1/2000950: 1 of 3 available, 2 disabled:\n" +
            "  cssharp.chat/say-hooks disabled (Host_Say: disabled by gamedata)\n" +
            "  cssharp.server/game-system-events disabled (disabled by gamedata: crashes on map change)\n" +
            "  Kill Switches in Gamedata: 3\n" +
            "    Host_Say: blank (disabled by gamedata)\n" +
            "    cssharp.server/game-system-events: crashes on map change\n" +
            "    cssharp.player/respawn: teleports to origin, forced on by operator (ForceEnableEngineFeatures)", report);
        Assert.DoesNotContain("Kill Switches", Set(AllResolved).FormatStartupReport());
    }

    [Fact]
    public void ACarriedForwardKillSwitchNamesItsSourceBuild()
    {
        var carried = GameSystemKilled with { CarriedForwardFrom = "1.41.9.7 (SourceRevision 11228971)" };

        var features = Set(AllResolved, carried);

        Assert.Equal("disabled by gamedata (carried forward from 1.41.9.7 (SourceRevision 11228971)): crashes on map change",
            features.GetStatus(GameSystem.Name).Describe());
        Assert.Equal(
            "Engine Features for build 1.42.0.1/2000950: 2 of 3 available, 1 disabled:\n" +
            "  cssharp.server/game-system-events disabled (disabled by gamedata (carried forward from 1.41.9.7 (SourceRevision 11228971)): " +
            "crashes on map change)\n" +
            "  Kill Switches in Gamedata: 1\n" +
            "    cssharp.server/game-system-events: crashes on map change, carried forward from 1.41.9.7 (SourceRevision 11228971)",
            features.FormatStartupReport());
        Assert.Equal("available, forced on by operator (disabled by gamedata (carried forward from 1.41.9.7 (SourceRevision 11228971)): " +
                     "crashes on map change)",
            Set(AllResolved, carried with { Forced = true }).GetStatus(GameSystem.Name).Describe());
    }

    [Fact]
    public void AKillSwitchNamingNoEngineFeatureLogsAWarning()
    {
        EngineFeatures.Current = Set(AllResolved, new EngineFeatureKill("cssharp.made/up", "typo"));

        EngineFeatures.LogStartupReport(_logger);

        Assert.Contains(_logger.Entries, e => e.Level == LogLevel.Warning &&
                                              e.Message == "Kill Switch for cssharp.made/up names no Engine Feature of this " +
                                              "CounterStrikeSharp version; ignored (typo)");
        Assert.True(EngineFeatures.Current.GetAll().All(s => s.IsAvailable));
        Assert.Contains("\n    cssharp.made/up: typo, names no Engine Feature (ignored)", EngineFeatures.Current.FormatStartupReport());
    }

    [Fact]
    public void TheReportReadsKillSwitchesFromNativeAndDefaultsToNone()
    {
        const string bindings = """
                                "bindings": [ { "key": "Host_Say", "kind": "Signature", "resolved": false, "status": "Failed",
                                  "reason": "DisabledByGamedata", "matchCount": 0, "address": 0, "offset": 0, "library": "server" } ],
                                "features": []
                                """;

        var report = ResolutionReport.FromJson(
            $$"""
              { "buildId": "b", {{bindings}}, "killSwitches": [
                { "id": "cssharp.x/y", "reason": "r", "forced": true, "carriedForwardFrom": null },
                { "id": "cssharp.x/z", "reason": "r", "forced": false, "carriedForwardFrom": "1.41.9.7 (SourceRevision 11228971)" } ] }
              """);

        Assert.Equal([new EngineFeatureKill("cssharp.x/y", "r", true), new EngineFeatureKill("cssharp.x/z", "r", false, "1.41.9.7 (SourceRevision 11228971)")],
            report.KillSwitches);
        Assert.Equal(BindingFailureReason.DisabledByGamedata, report.Find("Host_Say")!.Reason);
        Assert.Empty(ResolutionReport.FromJson($$"""{ "buildId": "b", {{bindings}} }""").KillSwitches);
    }

    private sealed class CapturingLogger : ILogger
    {
        public List<(LogLevel Level, string Message)> Entries { get; } = new();

        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;
        public bool IsEnabled(LogLevel logLevel) => true;

        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception,
            Func<TState, Exception?, string> formatter)
        {
            Entries.Add((logLevel, formatter(state, exception)));
        }
    }
}
