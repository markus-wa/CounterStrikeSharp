using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CounterStrikeSharp.API.Core.Bindings;

/// <summary>How a Binding is found (mirrors <c>bindings::BindingKind</c> in native).</summary>
internal enum BindingKind
{
    Signature,
    Offset,
    RttiVtable,
    SchemaField,
    Slot,
}

/// <summary>
/// A Slot Binding's Vtable Check (mirrors <c>bindings::SlotEvidence</c>): the class whose vtable holds the slot, the
/// Gamedata index, how the fingerprint window matched (Window, OwnFingerprint, Heal or None), the live and recorded
/// vtable sizes, and the fingerprint window Gamedata recorded (null entries: neighbours outside the vtable).
/// </summary>
internal sealed record SlotEvidence(
    string ClassName,
    long StatedIndex,
    string Match,
    long VtableSize,
    long? RecordedVtableSize,
    IReadOnlyList<string?>? Window = null)
{
    /// <summary>A Game update changed the class's vtable size: a warning, never a failure by itself.</summary>
    public bool SizeChanged => VtableSize != 0 && RecordedVtableSize is { } recorded && recorded != VtableSize;
}

/// <summary>
/// One Binding in the Resolution Report: resolved (with its address or offset) or failed (with a reason). A Slot
/// Binding's offset is its slot index; <paramref name="Unverified"/> marks one used at its Gamedata index without a
/// Vtable Check, <paramref name="Healed"/> one found at another index than Gamedata states (its offset is the new
/// index, <see cref="SlotEvidence.StatedIndex"/> the old one). Both still count as resolved.
/// </summary>
internal sealed record BindingResult(
    string Key,
    BindingKind Kind,
    bool Resolved,
    BindingFailureReason Reason,
    int MatchCount,
    nint Address,
    long Offset,
    string Library,
    bool Unverified = false,
    SlotEvidence? Slot = null,
    bool Healed = false);

/// <summary>
/// A Kill Switch on a whole Engine Feature (mirrors <c>bindings::EngineFeatureKill</c>): Gamedata's
/// <c>disabledEngineFeatures</c> names it with a reason. <paramref name="Forced"/> when the operator's
/// <c>ForceEnableEngineFeatures</c> keeps it available anyway. The id is not checked against any catalog; whether it was
/// carried forward from another build is the provenance's (<see cref="ResolutionReport.Gamedata"/>).
/// </summary>
internal sealed record EngineFeatureKill(string Id, string Reason, bool Forced = false);

/// <summary>
/// The immutable Resolution Report for the running build (GLOSSARY.md): the build id, every core Binding as native
/// resolved it at boot, and the native-owned Engine Features (name + Binding keys). Native keeps the authoritative
/// copy; managed reads it once through <see cref="FromJson"/>. Tests build it by hand.
/// </summary>
internal sealed class ResolutionReport
{
    private readonly Dictionary<string, BindingResult> _bindings;

    public ResolutionReport(string buildId, IEnumerable<BindingResult> bindings,
        IEnumerable<EngineFeatureDeclaration> nativeFeatures, bool healEnabled = true,
        IEnumerable<EngineFeatureKill>? killSwitches = null)
    {
        BuildId = buildId;
        HealEnabled = healEnabled;
        KillSwitches = killSwitches?.ToList() ?? [];
        Bindings = bindings.ToList();
        NativeFeatures = nativeFeatures.ToList();
        _bindings = new Dictionary<string, BindingResult>();
        foreach (var binding in Bindings) _bindings.TryAdd(binding.Key, binding);
    }

    public string BuildId { get; }

    /// <summary>False when the operator turned Heal off (<c>HealEnabled: false</c>): a Slot Binding that moved failed.</summary>
    public bool HealEnabled { get; }

    public IReadOnlyList<BindingResult> Bindings { get; }

    /// <summary>Engine Features declared by native code; their Bindings are keys into this report.</summary>
    public IReadOnlyList<EngineFeatureDeclaration> NativeFeatures { get; }

    /// <summary>Gamedata's feature-level Kill Switches, in Gamedata order.</summary>
    public IReadOnlyList<EngineFeatureKill> KillSwitches { get; }

    /// <summary>Where the Gamedata the Bindings were resolved against came from; null in reports built without it.</summary>
    public GamedataProvenance? Gamedata { get; init; }

    /// <summary>null when native did not resolve the key (it is not in core Gamedata and no native feature declares it).</summary>
    public BindingResult? Find(string key) => _bindings.GetValueOrDefault(key);

    /// <summary>Reads the document written by native <c>bindings::ResolutionReportToJson</c>.</summary>
    public static ResolutionReport FromJson(string json)
    {
        var dto = JsonSerializer.Deserialize<ReportDto>(json, JsonOptions)
                  ?? throw new JsonException("Resolution Report is empty");

        return new ResolutionReport(
            dto.BuildId,
            dto.Bindings.Select(b => new BindingResult(b.Key, b.Kind, b.Resolved, b.Reason, checked((int)b.MatchCount),
                unchecked((nint)b.Address), b.Offset, b.Library, b.Status == "Unverified",
                b.Slot == null
                    ? null
                    : new SlotEvidence(b.Slot.Class, b.Slot.StatedIndex, b.Slot.Match, b.Slot.VtableSize, b.Slot.RecordedVtableSize,
                        b.Slot.Window),
                b.Status == "Healed")),
            dto.Features.Select(f => new EngineFeatureDeclaration(f.Name, f.Bindings.Select(BindingDeclaration.Any).ToList())),
            dto.HealEnabled ?? true,
            dto.KillSwitches) { Gamedata = dto.Gamedata };
    }

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        Converters = { new JsonStringEnumConverter() },
    };

    private sealed record ReportDto(string BuildId, List<BindingDto> Bindings, List<FeatureDto> Features, bool? HealEnabled = null,
        List<EngineFeatureKill>? KillSwitches = null, GamedataProvenance? Gamedata = null);

    private sealed record BindingDto(
        string Key,
        BindingKind Kind,
        bool Resolved,
        BindingFailureReason Reason,
        ulong MatchCount,
        ulong Address,
        long Offset,
        string Library,
        string? Status,
        SlotDto? Slot);

    private sealed record SlotDto(
        string Class,
        long StatedIndex,
        string Match,
        long VtableSize,
        long? RecordedVtableSize,
        List<string?>? Window);

    private sealed record FeatureDto(string Name, List<string> Bindings);
}
