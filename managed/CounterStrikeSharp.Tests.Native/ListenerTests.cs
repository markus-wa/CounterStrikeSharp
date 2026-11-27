using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using CounterStrikeSharp.API;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Modules.Cvars;
using CounterStrikeSharp.API.Modules.Memory;
using Moq;
using Xunit;

public class ListenerTests
{
    [Fact]
    public async Task CanRegisterAndDeregisterListeners()
    {
        int callCount = 0;
        var callback = FunctionReference.Create((int playerSlot, string name, string ipAddress) =>
        {
            Assert.NotNull(ipAddress);
            Assert.NotEmpty(name);
            Assert.Equal("127.0.0.1", ipAddress);
            callCount++;
        });

        await ExecuteCommandAsync("bot_quota 0; bot_quota_mode normal");

        NativeAPI.AddListener("OnClientConnect", callback);

        // Test hooking
        NativeAPI.IssueServerCommand("bot_kick");
        await ExecuteCommandAsync("bot_add");

        Assert.Equal(1, callCount);
        NativeAPI.RemoveListener("OnClientConnect", callback);

        // Test unhooking
        NativeAPI.IssueServerCommand("bot_kick");
        await ExecuteCommandAsync("bot_add");
        Assert.Equal(1, callCount);

        NativeAPI.IssueServerCommand("bot_quota 1");
    }

    [Fact]
    public async Task EntityListenersAreFired()
    {
        // The listeners fire for every entity: count only the test's own, other entities come and go in the same frames.
        var created = new List<IntPtr>();
        var deleted = new List<IntPtr>();
        var createCallback = FunctionReference.Create((IntPtr entityPtr) => { created.Add(entityPtr); });
        var deleteCallback = FunctionReference.Create((IntPtr entityPtr) => { deleted.Add(entityPtr); });

        try
        {
            NativeAPI.AddListener("OnEntityCreated", createCallback);
            NativeAPI.AddListener("OnEntityDeleted", deleteCallback);

            var ent = Utilities.CreateEntityByName<CBaseModelEntity>("prop_dynamic")!;
            var handle = ent.Handle;
            await WaitUntilAsync(() => created.Contains(handle), "OnEntityCreated for the new entity");
            Assert.Single(created, handle);

            ent.Remove();
            await WaitUntilAsync(() => deleted.Contains(handle), "OnEntityDeleted for the removed entity");
            Assert.Single(deleted, handle);
        }
        finally
        {
            NativeAPI.RemoveListener("OnEntityCreated", createCallback);
            NativeAPI.RemoveListener("OnEntityDeleted", deleteCallback);
        }
    }

    [Fact(Skip = "Damage func broken")]
    public async Task TakeDamageListenersAreFired()
    {
        int preCallCount = 0;
        int postCallCount = 0;

        var preCallback = FunctionReference.Create((IntPtr entityPtr, IntPtr damageInfoPtr) => { preCallCount++; });

        var postCallback = FunctionReference.Create((IntPtr entityPtr, IntPtr damageInfoPtr) => { postCallCount++; });

        try
        {
            NativeAPI.AddListener("OnEntityTakeDamagePre", preCallback);
            NativeAPI.AddListener("OnEntityTakeDamagePost", postCallback);

            // Spawn a bot and deal damage to it
            NativeAPI.IssueServerCommand("bot_kick");
            NativeAPI.IssueServerCommand("bot_add");
            await WaitOneFrame();

            var player = Utilities.GetPlayers().FirstOrDefault(p => p.IsBot);
            var playerHealth = player.PlayerPawn.Value.Health;
            DealDamageFunc(player, player, 10);

            await WaitOneFrame();
            Assert.Equal(playerHealth - 10, player.PlayerPawn.Value.Health);

            Assert.Equal(1, preCallCount);
            Assert.Equal(1, postCallCount);
        }
        finally
        {
            NativeAPI.RemoveListener("OnEntityTakeDamagePre", preCallback);
            NativeAPI.RemoveListener("OnEntityTakeDamagePost", postCallback);
        }
    }

    [Fact]
    public async Task Listener_ConVarChanged()
    {
        var convar = new ConVar<float>($"css_itest_cvar_{Guid.NewGuid():N}", "Test ConVar", 0);

        var mock = new Mock<Action>();
        // The listener fires for every ConVar; commands left over from other tests change theirs too.
        var methodCallback = FunctionReference.Create((ushort changedIndex) =>
        {
            if (new ConVarBase(changedIndex).AccessIndex == convar.AccessIndex) mock.Object.Invoke();
        });

        try
        {
            NativeAPI.AddListener("OnConVarChanged", methodCallback);
            await WaitOneFrame();
            await ExecuteCommandAsync($"{convar.Name} 1");
            Assert.Single(mock.Invocations);
        }
        finally
        {
            NativeAPI.RemoveListener("OnConVarChanged", methodCallback);
            convar.Delete();
        }
    }

    [Fact(Skip = "Damage func broken")]
    public async Task TakeDamageListenerCanBeCancelled()
    {
        int preCallCount = 0;
        int postCallCount = 0;

        Listeners.OnEntityTakeDamagePre preCallback = (entityPtr, damageInfoPtr) =>
        {
            preCallCount++;
            return HookResult.Stop;
        };

        Listeners.OnEntityTakeDamagePre secondCallback = (entityPtr, damageInfoPtr) =>
        {
            preCallCount++;
            return HookResult.Continue;
        };

        Listeners.OnEntityTakeDamagePost postCallback = (entity, damageInfo, damageResult) => { postCallCount++; };

        try
        {
            NativeAPI.AddListener("OnEntityTakeDamagePre", preCallback);
            NativeAPI.AddListener("OnEntityTakeDamagePre", secondCallback);
            NativeAPI.AddListener("OnEntityTakeDamagePost", postCallback);

            // Spawn a bot and deal damage to it
            NativeAPI.IssueServerCommand("bot_kick");
            NativeAPI.IssueServerCommand("bot_add");
            await WaitOneFrame();

            var player = Utilities.GetPlayers().FirstOrDefault(p => p.IsBot);
            var playerHealth = player.PlayerPawn.Value.Health;
            DealDamageFunc(player, player, 10);

            await WaitOneFrame();
            Assert.Equal(player.PlayerPawn.Value.Health, playerHealth);

            Assert.Equal(1, preCallCount);
            Assert.Equal(0, postCallCount);
        }
        finally
        {
            NativeAPI.RemoveListener("OnEntityTakeDamagePre", preCallback);
            NativeAPI.RemoveListener("OnEntityTakeDamagePre", secondCallback);
            NativeAPI.RemoveListener("OnEntityTakeDamagePost", postCallback);
        }
    }

    private static void DealDamageFunc(CCSPlayerController attacker, CCSPlayerController victim, int damage,
        object data = null, DamageTypes_t type = DamageTypes_t.DMG_ENERGYBEAM)
    {
        var size = Schema.GetClassSize("CTakeDamageInfo");
        var ptr = Marshal.AllocHGlobal(size);

        for (var i = 0; i < size; i++)
            Marshal.WriteByte(ptr, i, 0);

        var damageInfo = new CTakeDamageInfo(ptr);
        var attackerInfo = new AttackerInfo_t()
        {
            AttackerPawn = attacker.Pawn.Raw, AttackerPlayerSlot = attacker.Slot,
            IsPawn = true, NeedInit = true,
        };

        Marshal.StructureToPtr(attackerInfo, new IntPtr(ptr.ToInt64() + 0x88), false);

        if (attacker.Team == victim.Team)
            attacker = victim;

        Schema.SetSchemaValue(damageInfo.Handle, "CTakeDamageInfo", "m_hInflictor",
            attacker.PawnIsAlive ? attacker.Pawn.Raw : attacker.PlayerPawn.Raw);
        Schema.SetSchemaValue(damageInfo.Handle, "CTakeDamageInfo", "m_hAttacker", attacker.Pawn.Raw);

        damageInfo.Damage = damage;
        damageInfo.BitsDamageType = type;
        if (type == DamageTypes_t.DMG_ENERGYBEAM)
            damageInfo.DamageFlags = TakeDamageFlags_t.DFLAG_IGNORE_ARMOR;

        size = Schema.GetClassSize("CTakeDamageResult");
        var ptr2 = Marshal.AllocHGlobal(size);

        for (var i = 0; i < size; i++)
            Marshal.WriteByte(ptr2, i, 0);

        var damageResult = new CTakeDamageResult(ptr2);
        Schema.SetSchemaValue(damageResult.Handle, "CTakeDamageResult", "m_pOriginatingInfo", damageInfo.Handle);

        damageResult.HealthBefore = victim.PlayerPawn.Value.Health;
        damageResult.HealthLost = damage;
        damageResult.DamageDealt = damage;
        damageResult.PreModifiedDamage = damage;
        damageResult.TotalledHealthLost = damage;
        damageResult.TotalledDamageDealt = damage;
        damageResult.WasDamageSuppressed = false;

        VirtualFunctions.CBaseEntity_TakeDamageOldFunc.Invoke(victim.Pawn.Value, damageInfo, damageResult);
        Marshal.FreeHGlobal(ptr);
        Marshal.FreeHGlobal(ptr2);
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct AttackerInfo_t
{
    public bool NeedInit;
    public bool IsPawn;
    public bool IsWorld;
    public uint AttackerPawn;
    public int AttackerPlayerSlot;
    public int TeamChecked;
    public int Team;
};