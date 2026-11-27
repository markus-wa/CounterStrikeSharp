using CounterStrikeSharp.API.Core.Bindings;

namespace CounterStrikeSharp.API.Core;

/// <summary>
/// Whether an Engine Feature is available on the running build, and if not, which Binding failed and why, or why a
/// Gamedata Kill Switch disables it.
/// Obtain one from <see cref="EngineFeatures.GetStatus(string)"/> or <see cref="EngineFeatures.GetAll"/>.
/// </summary>
public sealed class EngineFeatureStatus
{
    internal EngineFeatureStatus(string name, bool isDeclared, string? failedBinding, BindingFailureReason reason,
        int matchCount, IReadOnlyList<SlotHeal>? heals = null, EngineFeatureKill? killSwitch = null)
    {
        Name = name;
        IsDeclared = isDeclared;
        FailedBinding = failedBinding;
        Reason = reason;
        MatchCount = matchCount;
        Heals = heals ?? [];
        KillSwitchReason = killSwitch?.Reason;
        ForcedOn = killSwitch is { Forced: true };
    }

    internal static EngineFeatureStatus Unknown(string name) => new(name, false, null, BindingFailureReason.None, 0);

    /// <summary>The Engine Feature's stable, namespaced name, e.g. <c>cssharp.chat/say-hooks</c>.</summary>
    public string Name { get; }

    /// <summary>False if this CounterStrikeSharp version does not declare an Engine Feature by that name.</summary>
    public bool IsDeclared { get; }

    /// <summary>
    /// True if the Engine Feature is declared, every Binding it needs resolved, and no Kill Switch disables it (or the
    /// operator forced it on).
    /// </summary>
    public bool IsAvailable => IsDeclared && FailedBinding == null && Reason == BindingFailureReason.None;

    /// <summary>The (first) Binding that failed, as its Gamedata key; null when available, not declared or killed.</summary>
    public string? FailedBinding { get; }

    /// <summary>
    /// Why <see cref="FailedBinding"/> failed, or <see cref="BindingFailureReason.DisabledByGamedata"/> when a Kill Switch
    /// disables the whole Engine Feature (<see cref="FailedBinding"/> is then null); <see cref="BindingFailureReason.None"/>
    /// when available.
    /// </summary>
    public BindingFailureReason Reason { get; }

    /// <summary>
    /// Why Gamedata disables this Engine Feature (its Kill Switch in <c>disabledEngineFeatures</c>); null when no Kill Switch
    /// names it. Also set when the operator forced it on (<see cref="ForcedOn"/>).
    /// </summary>
    public string? KillSwitchReason { get; }

    /// <summary>
    /// True when a Kill Switch names this Engine Feature but the operator keeps it on (<c>ForceEnableEngineFeatures</c> in
    /// core.json). Forcing on never hides a failed Binding.
    /// </summary>
    public bool ForcedOn { get; }

    /// <summary>Signature matches of <see cref="FailedBinding"/> (relevant for <see cref="BindingFailureReason.Ambiguous"/>).</summary>
    public int MatchCount { get; }

    /// <summary>
    /// The Engine Feature's Slot Bindings that were healed at boot (found at another vtable index than Gamedata states).
    /// A healed Engine Feature is available; empty when nothing moved.
    /// </summary>
    public IReadOnlyList<SlotHeal> Heals { get; }

    /// <summary>
    /// Why not, in the wording of the startup report: <c>"Host_Say: not found"</c>,
    /// <c>"ClientPrint: ambiguous (2 matches)"</c>, <c>"available"</c>,
    /// <c>"available, healed (ISource2Server::GameFrame: slot 19 -> 20)"</c>, <c>"disabled by gamedata: crashes on map
    /// change"</c>, <c>"available, forced on by operator (disabled by gamedata: crashes on map change)"</c> or
    /// <c>"unknown Engine Feature"</c>.
    /// </summary>
    public string Describe()
    {
        if (!IsDeclared) return "unknown Engine Feature";
        if (IsAvailable)
        {
            var text = "available";
            if (ForcedOn) text += $", forced on by operator ({DescribeKillSwitch()})";
            if (Heals.Count > 0) text += $", healed ({string.Join(", ", Heals)})";
            return text;
        }

        return FailedBinding == null ? DescribeKillSwitch() : $"{FailedBinding}: {Reason.Describe(MatchCount)}";
    }

    // Same wording as native bindings::DescribeUnavailable.
    private string DescribeKillSwitch() => $"disabled by gamedata: {KillSwitchReason}";

    /// <inheritdoc />
    public override string ToString() => IsAvailable ? $"{Name} {Describe()}" : $"{Name} disabled ({Describe()})";
}
