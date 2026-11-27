using System.Collections.ObjectModel;
using System.Linq;
using System.Text.Json;

namespace CounterStrikeSharp.API.Core;

/// <summary>
/// The Engine Features whose Gamedata a staged Gamedata Snapshot changes (<see cref="Listeners.OnGamedataUpdateAvailable"/>): those
/// with a Binding whose Gamedata entry changes, and those whose Kill Switch is added, removed or changes, ordered by name.
/// Their state may change on the next boot; whether it does is only known once that boot resolves the new Gamedata.
/// </summary>
public sealed class ChangedEngineFeatures : ReadOnlyCollection<string>
{
    /// <summary>
    /// From the changes native raises <see cref="Listeners.OnGamedataUpdateAvailable"/> with:
    /// <c>{"keys": [Gamedata keys], "killSwitches": [Engine Feature ids]}</c>, mapped to the running Engine Features.
    /// </summary>
    public ChangedEngineFeatures(string changes) : base(Parse(changes))
    {
    }

    private static string[] Parse(string changes)
    {
        var dto = JsonSerializer.Deserialize<ChangesDto>(changes, JsonOptions) ?? throw new JsonException("no Gamedata changes");
        return EngineFeatures.Current.ChangedBy(dto.Keys, dto.KillSwitches).ToArray();
    }

    /// <summary>The ids, comma-separated.</summary>
    public override string ToString() => string.Join(", ", this);

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    private sealed record ChangesDto(string[] Keys, string[] KillSwitches);
}
