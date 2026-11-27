using System;
using System.Diagnostics;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Modules.Timers;
using Microsoft.Extensions.Logging;

namespace RestartNoticeTestPlugin;

/// <summary>
/// End-to-end fixture for the Gamedata Feed poll (eng/e2e/cases/restart-notice.sh): logs the Gamedata provenance at load,
/// every OnGamedataUpdateAvailable with its payload, and every 10 s how many ticks ran and the longest gap between two,
/// so a poll that blocked the game thread would show as a gap.
/// </summary>
public class RestartNoticeTestPlugin : BasePlugin
{
    public override string ModuleName => "Restart Notice Test";
    public override string ModuleVersion => "v1.0.0";

    private readonly Stopwatch _sinceTick = new();
    private long _ticks;
    private double _maxGapMs;

    public override void Load(bool hotReload)
    {
        var gamedata = EngineFeatures.Gamedata;
        Logger.LogInformation("RestartNoticeTest: provenance source={Source} seq={Seq}", gamedata?.Source, gamedata?.Seq);

        RegisterListener<Listeners.OnGamedataUpdateAvailable>((build, seq, changed) =>
            Logger.LogInformation("RestartNoticeTest: OnGamedataUpdateAvailable build={Build} seq={Seq} changed={Changed}", build, seq,
                changed.ToString()));

        RegisterListener<Listeners.OnTick>(() =>
        {
            if (_sinceTick.IsRunning) _maxGapMs = Math.Max(_maxGapMs, _sinceTick.Elapsed.TotalMilliseconds);
            _sinceTick.Restart();
            _ticks++;
        });
        AddTimer(10, () =>
        {
            Logger.LogInformation("RestartNoticeTest: {Ticks} ticks, max gap {MaxGap:F0} ms", _ticks, _maxGapMs);
            _ticks = 0;
            _maxGapMs = 0;
        }, TimerFlags.REPEAT);
    }
}
