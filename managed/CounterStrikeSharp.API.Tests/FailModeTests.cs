using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Core.Bindings;
using CounterStrikeSharp.API.Core.Commands;
using CounterStrikeSharp.API.Core.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using System.Text.Json;

namespace CounterStrikeSharp.API.Tests;

// The operator settings in core.json: FailMode (what a disabled core Engine Feature does to the server) and
// SchemaMissMode. Uses static state (CoreConfig, EngineFeatures.Current).
[Collection(nameof(EngineFeaturesApiTests))]
public class FailModeTests : IDisposable
{
    private readonly CapturingLogger _logger = new();
    private readonly Action _originalStop = EngineFeatures.StopServer;
    private int _stops;

    public FailModeTests()
    {
        EngineFeatures.StopServer = () => _stops++;
    }

    public void Dispose()
    {
        EngineFeatures.StopServer = _originalStop;
        EngineFeatures.Reset();
        CoreConfig.ApplyModes(new CoreConfigData(), NullLogger.Instance);
    }

    private static readonly EngineFeatureDeclaration SayHooks = new("cssharp.chat/say-hooks", [BindingDeclaration.Signature("Host_Say")]);

    private static readonly EngineFeatureDeclaration ClientPrint =
        new("cssharp.chat/client-print", [BindingDeclaration.Signature("ClientPrint")]);

    private static BindingResult HostSay(bool resolved) =>
        new("Host_Say", BindingKind.Signature, resolved, resolved ? BindingFailureReason.None : BindingFailureReason.NotFound,
            resolved ? 1 : 0, resolved ? 0x1000 : 0, 0, "server");

    private static BindingResult ClientPrintResolved() =>
        new("ClientPrint", BindingKind.Signature, true, BindingFailureReason.None, 1, 0x9000, 0, "server");

    private static EngineFeatureSet Set(params BindingResult[] bindings) =>
        new(new ResolutionReport("1.42.0.1/2000950", bindings, [SayHooks, ClientPrint]), []);

    // --- core.json ---

    // Loads core.json with the given content, as CounterStrikeSharp does at startup.
    private static void LoadCoreConfig(string json, ILogger<CoreConfig>? logger = null)
    {
        var directory = Directory.CreateTempSubdirectory();
        try
        {
            File.WriteAllText(Path.Join(directory.FullName, "core.json"), json);
            var host = new Mock<IScriptHostConfiguration>();
            host.Setup(h => h.ConfigsPath).Returns(directory.FullName);

            new CoreConfig(host.Object, Mock.Of<ICommandManager>(), logger ?? NullLogger<CoreConfig>.Instance).Load();
        }
        finally
        {
            directory.Delete(true);
        }
    }

    [Fact]
    public void ExistingConfigsWithoutTheKeysKeepTodaysBehaviour()
    {
        LoadCoreConfig("""{ "PublicChatTrigger": [ "!" ], "ServerLanguage": "en" }""");

        Assert.Equal(FailMode.Disable, CoreConfig.FailMode);
        Assert.Equal(SchemaMissMode.Warn, CoreConfig.SchemaMissMode);
    }

    [Fact]
    public void TheKeysAreReadFromCoreJson()
    {
        LoadCoreConfig("""{ "FailMode": "crash", "SchemaMissMode": "throw" }""");

        Assert.Equal(FailMode.Crash, CoreConfig.FailMode);
        Assert.Equal(SchemaMissMode.Throw, CoreConfig.SchemaMissMode);
    }

    [Fact]
    public void AWrongTypeFallsBackForThatKeyOnlyAndKeepsTheRestOfTheFile()
    {
        var logger = new CapturingLogger<CoreConfig>();

        LoadCoreConfig("""{ "FailMode": 1, "SchemaMissMode": "throw", "PluginHotReloadEnabled": false }""", logger);

        Assert.Equal(FailMode.Disable, CoreConfig.FailMode);
        Assert.Equal(SchemaMissMode.Throw, CoreConfig.SchemaMissMode);
        Assert.False(CoreConfig.PluginHotReloadEnabled);
        Assert.Contains(logger.Entries, e => e.Level == LogLevel.Warning && e.Message.Contains("FailMode 1 is not one of"));
        LoadCoreConfig("{}");
    }

    [Theory]
    [InlineData(null, FailMode.Disable)]
    [InlineData("", FailMode.Disable)]
    [InlineData("disable", FailMode.Disable)]
    [InlineData("crash", FailMode.Crash)]
    [InlineData("Crash", FailMode.Crash)]
    [InlineData(" CRASH ", FailMode.Crash)]
    public void FailModeParsesCaseInsensitivelyAndDefaultsToDisable(string? value, FailMode expected)
    {
        Assert.Equal(expected, CoreConfig.ParseMode(value, FailMode.Disable));
    }

    [Theory]
    [InlineData(null, SchemaMissMode.Warn)]
    [InlineData("warn", SchemaMissMode.Warn)]
    [InlineData("throw", SchemaMissMode.Throw)]
    [InlineData("Throw", SchemaMissMode.Throw)]
    public void SchemaMissModeParsesCaseInsensitivelyAndDefaultsToWarn(string? value, SchemaMissMode expected)
    {
        Assert.Equal(expected, CoreConfig.ParseMode(value, SchemaMissMode.Warn));
    }

    [Theory]
    [InlineData("crahs")]
    [InlineData("1")]
    [InlineData("fail")]
    public void AnUnknownModeFallsBackToTheDefaultForThatKeyOnlyWithAWarning(string value)
    {
        CoreConfig.ApplyModes(Data($$"""{ "FailMode": "{{value}}", "SchemaMissMode": "throw" }"""), _logger);

        Assert.Equal(FailMode.Disable, CoreConfig.FailMode);
        Assert.Equal(SchemaMissMode.Throw, CoreConfig.SchemaMissMode);
        var warning = Assert.Single(_logger.Entries);
        Assert.Equal(LogLevel.Warning, warning.Level);
        Assert.Contains($"FailMode \"{value}\"", warning.Message);
        Assert.Contains("\"disable\", \"crash\"", warning.Message);
    }

    private static CoreConfigData Data(string json) => JsonSerializer.Deserialize<CoreConfigData>(json)!;

    // --- FailMode ---

    [Fact]
    public void DisableKeepsTheServerRunningWithADisabledEngineFeature()
    {
        EngineFeatures.Current = Set(HostSay(false), ClientPrintResolved());

        Assert.Null(EngineFeatures.Current.FormatFailModeError(FailMode.Disable));
        Assert.False(EngineFeatures.ApplyFailMode(_logger, FailMode.Disable));
        Assert.Equal(0, _stops);
        Assert.Empty(_logger.Entries);
    }

    [Fact]
    public void CrashStopsTheServerWithAFatalErrorNamingTheEngineFeatureAndItsBinding()
    {
        EngineFeatures.Current = Set(HostSay(false), ClientPrintResolved());

        Assert.True(EngineFeatures.ApplyFailMode(_logger, FailMode.Crash));

        Assert.Equal(1, _stops);
        var fatal = Assert.Single(_logger.Entries);
        Assert.Equal(LogLevel.Critical, fatal.Level);
        Assert.Equal(
            "FailMode is crash and 1 of 2 core Engine Features is disabled on build 1.42.0.1/2000950, " +
            "stopping the server:\n" +
            "  cssharp.chat/say-hooks disabled (Host_Say: not found)", fatal.Message);
    }

    [Fact]
    public void CrashKeepsRunningWhenEveryEngineFeatureIsAvailable()
    {
        EngineFeatures.Current = Set(HostSay(true), ClientPrintResolved());

        Assert.Null(EngineFeatures.Current.FormatFailModeError(FailMode.Crash));
        Assert.False(EngineFeatures.ApplyFailMode(_logger, FailMode.Crash));
        Assert.Equal(0, _stops);
    }

    [Fact]
    public void CrashThatCannotStopTheServerLogsWhyAndKeepsRunning()
    {
        EngineFeatures.Current = Set(HostSay(false), ClientPrintResolved());
        EngineFeatures.StopServer = () => throw new NativeException("FailMode is \"disable\", not \"crash\": not stopping the server");

        Assert.False(EngineFeatures.ApplyFailMode(_logger, FailMode.Crash));
        Assert.Equal([LogLevel.Critical, LogLevel.Error], _logger.Entries.Select(e => e.Level));
    }

    private sealed class CapturingLogger<T> : CapturingLogger, ILogger<T>;

    private class CapturingLogger : ILogger
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
