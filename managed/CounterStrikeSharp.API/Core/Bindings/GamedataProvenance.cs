namespace CounterStrikeSharp.API.Core;

/// <summary>Where the core Gamedata of this boot came from (mirrors native <c>feed::GamedataSource</c>).</summary>
public enum GamedataSource
{
    /// <summary>The bundled <c>gamedata.json</c>.</summary>
    Bundled,

    /// <summary>The Gamedata Feed's snapshot for exactly the running build.</summary>
    ExactBuild,

    /// <summary>The newest cached snapshot of an older build: the feed has none for the running build (yet).</summary>
    CarriedForward,
}

/// <summary>
/// The Gamedata provenance of this boot (ADR-0006), read through <see cref="EngineFeatures.Gamedata"/>.
/// </summary>
/// <param name="Source">Bundled, exact build or carried forward.</param>
/// <param name="Cached">Read from the Gamedata cache, not fetched this boot.</param>
/// <param name="Build">The build label of an exact-build snapshot; null otherwise.</param>
/// <param name="CarriedFrom">The build label a carried-forward snapshot was made for; null otherwise.</param>
/// <param name="Seq">The seq of the signed index that vouches for the snapshot; null for bundled.</param>
/// <param name="KeyId">The id of the key that signed that index; null for bundled.</param>
/// <param name="Warnings">Why the feed was not used, or what was off with it.</param>
/// <param name="Line">The startup report's Gamedata line, also returned by <see cref="ToString"/>.</param>
public sealed record GamedataProvenance(
    GamedataSource Source,
    bool Cached,
    string? Build,
    string? CarriedFrom,
    ulong? Seq,
    string? KeyId,
    IReadOnlyList<string> Warnings,
    string Line)
{
    /// <inheritdoc cref="Line"/>
    public override string ToString() => Line;
}
