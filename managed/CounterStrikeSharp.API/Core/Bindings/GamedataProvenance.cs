namespace CounterStrikeSharp.API.Core;

/// <summary>Where the core Gamedata of this boot came from (mirrors native <c>feed::GamedataSource</c>).</summary>
public enum GamedataSource
{
    /// <summary>The bundled <c>gamedata.json</c>, made for the running build (or unstamped).</summary>
    Bundled,

    /// <summary>The Gamedata Feed's snapshot for exactly the running build.</summary>
    ExactBuild,

    /// <summary>
    /// The newest Gamedata of another build, from the Gamedata cache or the bundled <c>gamedata.json</c>: the feed has
    /// no snapshot for the running build (yet).
    /// </summary>
    CarriedForward,
}

/// <summary>A Kill Switch on a whole Engine Feature in the chosen Gamedata (<c>disabledEngineFeatures</c>).</summary>
/// <param name="Id">The Engine Feature's id, e.g. <c>cssharp.server/game-system-events</c> (not checked against any catalog).</param>
/// <param name="Reason">Why Gamedata disables it.</param>
public sealed record GamedataKillSwitch(string Id, string Reason);

/// <summary>
/// The Gamedata provenance of this boot (ADR-0006), read through <see cref="EngineFeatures.Gamedata"/>.
/// </summary>
/// <param name="Source">Bundled, exact build or carried forward.</param>
/// <param name="Cached">Read from the Gamedata cache, not fetched this boot.</param>
/// <param name="Build">
/// The build label the Gamedata was made for: the running build (<see cref="GamedataSource.ExactBuild"/>) or the one it
/// was carried forward from; null for <see cref="GamedataSource.Bundled"/>.
/// </param>
/// <param name="Seq">The seq of the signed index that vouches for the snapshot; null when no index does (bundled).</param>
/// <param name="KeyId">The id of the key that signed that index; null when no index does (bundled).</param>
/// <param name="KillSwitches">
/// The Gamedata's Kill Switches on whole Engine Features; carried forward with it. Whether one is in force (or forced on
/// by the operator) is in <see cref="EngineFeatures.GetStatus(string)"/>.
/// </param>
/// <param name="Warnings">Why the feed was not used, or what was off with it.</param>
/// <param name="Line">The startup report's Gamedata line, also returned by <see cref="ToString"/>.</param>
public sealed record GamedataProvenance(
    GamedataSource Source,
    bool Cached,
    string? Build,
    ulong? Seq,
    string? KeyId,
    IReadOnlyList<GamedataKillSwitch> KillSwitches,
    IReadOnlyList<string> Warnings,
    string Line)
{
    /// <summary>The build the Gamedata was carried forward from; null unless <see cref="Source"/> is carried forward.</summary>
    internal string? CarriedForwardFrom => Source == GamedataSource.CarriedForward ? Build : null;

    /// <inheritdoc cref="Line"/>
    public override string ToString() => Line;
}
