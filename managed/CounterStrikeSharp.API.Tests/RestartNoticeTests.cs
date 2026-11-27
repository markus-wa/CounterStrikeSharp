using System.Reflection;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Core.Attributes;
using CounterStrikeSharp.API.Core.Bindings;

namespace CounterStrikeSharp.API.Tests;

// OnGamedataUpdateAvailable: native raises it with the build, the seq and what the staged snapshot changes (Gamedata keys
// and Kill Switch ids); plugins receive the Engine Features of both catalogs that it touches.
[Collection(nameof(EngineFeaturesApiTests))]
public class RestartNoticeTests : IDisposable
{
    public RestartNoticeTests()
    {
        var sayHooks = new EngineFeatureDeclaration("cssharp.chat/say-hooks", [BindingDeclaration.Signature("Host_Say")]);
        var report = new ResolutionReport("1.42.0.1 (SourceRevision 11268945)", [], [sayHooks]);
        EngineFeatures.Current = new EngineFeatureSet(report, ManagedEngineFeatures.All);
    }

    public void Dispose() => EngineFeatures.Reset();

    // The arguments as RegisterListener builds them from what native pushed.
    private static object?[] Dispatch<T>(params object[] pushed) where T : Delegate
    {
        var parameters = typeof(T).GetMethod("Invoke")!.GetParameters();
        return parameters.Select((p, i) => p.GetCustomAttribute<CastFromAttribute>() is { } castFrom
                ? BasePlugin.CastListenerArgument(p.ParameterType, Convert.ChangeType(pushed[i], castFrom.Type))
                : pushed[i])
            .ToArray();
    }

    [Fact]
    public void TheEventIsDispatchedWithTheBuildSeqAndTheEngineFeaturesTheUpdateChanges()
    {
        string? build = null;
        ulong seq = 0;
        IReadOnlyList<string>? changed = null;
        Listeners.OnGamedataUpdateAvailable handler = (b, s, c) => (build, seq, changed) = (b, s, c);

        handler.DynamicInvoke(Dispatch<Listeners.OnGamedataUpdateAvailable>("1.42.0.1 (SourceRevision 11268945)", 8ul,
            """{"keys":["Host_Say","CBaseEntity_Teleport","NotInAnyEngineFeature"],"killSwitches":["cssharp.player/respawn"]}"""));

        Assert.Equal("1.42.0.1 (SourceRevision 11268945)", build);
        Assert.Equal(8ul, seq);
        Assert.Equal(["cssharp.chat/say-hooks", "cssharp.entity/teleport", "cssharp.player/respawn"], changed);
        Assert.Equal("cssharp.chat/say-hooks, cssharp.entity/teleport, cssharp.player/respawn", changed!.ToString());
    }

    [Fact]
    public void AnUpdateThatTouchesNoEngineFeatureHasAnEmptyList()
    {
        var args = Dispatch<Listeners.OnGamedataUpdateAvailable>("b", 1ul, """{"keys":["NotInAnyEngineFeature"],"killSwitches":[]}""");

        Assert.Empty(Assert.IsAssignableFrom<IReadOnlyList<string>>(args[2]));
    }
}
