// <auto-generated />
#nullable enable
#pragma warning disable CS1591

using System;
using System.Diagnostics;
using System.Drawing;
using CounterStrikeSharp;
using CounterStrikeSharp.API.Modules.Events;
using CounterStrikeSharp.API.Modules.Entities;
using CounterStrikeSharp.API.Modules.Memory;
using CounterStrikeSharp.API.Modules.Utils;
using CounterStrikeSharp.API.Core.Attributes;

namespace CounterStrikeSharp.API.Core;

public partial class CPhysicsProp : CBreakableProp
{
    public CPhysicsProp (IntPtr pointer) : base(pointer) {}

	// m_MotionEnabled
	[SchemaMember("CPhysicsProp", "m_MotionEnabled")]
	public CEntityIOOutput MotionEnabled => Schema.GetDeclaredClass<CEntityIOOutput>(this.Handle, "CPhysicsProp", "m_MotionEnabled");

	// m_OnAwakened
	[SchemaMember("CPhysicsProp", "m_OnAwakened")]
	public CEntityIOOutput OnAwakened => Schema.GetDeclaredClass<CEntityIOOutput>(this.Handle, "CPhysicsProp", "m_OnAwakened");

	// m_OnAwake
	[SchemaMember("CPhysicsProp", "m_OnAwake")]
	public CEntityIOOutput OnAwake => Schema.GetDeclaredClass<CEntityIOOutput>(this.Handle, "CPhysicsProp", "m_OnAwake");

	// m_OnAsleep
	[SchemaMember("CPhysicsProp", "m_OnAsleep")]
	public CEntityIOOutput OnAsleep => Schema.GetDeclaredClass<CEntityIOOutput>(this.Handle, "CPhysicsProp", "m_OnAsleep");

	// m_OnPlayerUse
	[SchemaMember("CPhysicsProp", "m_OnPlayerUse")]
	public CEntityIOOutput OnPlayerUse => Schema.GetDeclaredClass<CEntityIOOutput>(this.Handle, "CPhysicsProp", "m_OnPlayerUse");

	// m_OnPlayerPickup
	[SchemaMember("CPhysicsProp", "m_OnPlayerPickup")]
	public CEntityIOOutput OnPlayerPickup => Schema.GetDeclaredClass<CEntityIOOutput>(this.Handle, "CPhysicsProp", "m_OnPlayerPickup");

	// m_OnOutOfWorld
	[SchemaMember("CPhysicsProp", "m_OnOutOfWorld")]
	public CEntityIOOutput OnOutOfWorld => Schema.GetDeclaredClass<CEntityIOOutput>(this.Handle, "CPhysicsProp", "m_OnOutOfWorld");

	// m_massScale
	[SchemaMember("CPhysicsProp", "m_massScale")]
	public ref float MassScale => ref Schema.GetRef<float>(this.Handle, "CPhysicsProp", "m_massScale");

	// m_inertiaScale
	[SchemaMember("CPhysicsProp", "m_inertiaScale")]
	public ref float InertiaScale => ref Schema.GetRef<float>(this.Handle, "CPhysicsProp", "m_inertiaScale");

	// m_buoyancyScale
	[SchemaMember("CPhysicsProp", "m_buoyancyScale")]
	public ref float BuoyancyScale => ref Schema.GetRef<float>(this.Handle, "CPhysicsProp", "m_buoyancyScale");

	// m_damageType
	[SchemaMember("CPhysicsProp", "m_damageType")]
	public ref Int32 DamageType => ref Schema.GetRef<Int32>(this.Handle, "CPhysicsProp", "m_damageType");

	// m_damageToEnableMotion
	[SchemaMember("CPhysicsProp", "m_damageToEnableMotion")]
	public ref Int32 DamageToEnableMotion => ref Schema.GetRef<Int32>(this.Handle, "CPhysicsProp", "m_damageToEnableMotion");

	// m_flForceToEnableMotion
	[SchemaMember("CPhysicsProp", "m_flForceToEnableMotion")]
	public ref float ForceToEnableMotion => ref Schema.GetRef<float>(this.Handle, "CPhysicsProp", "m_flForceToEnableMotion");

	// m_bThrownByPlayer
	[SchemaMember("CPhysicsProp", "m_bThrownByPlayer")]
	public ref bool ThrownByPlayer => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bThrownByPlayer");

	// m_bDroppedByPlayer
	[SchemaMember("CPhysicsProp", "m_bDroppedByPlayer")]
	public ref bool DroppedByPlayer => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bDroppedByPlayer");

	// m_bTouchedByPlayer
	[SchemaMember("CPhysicsProp", "m_bTouchedByPlayer")]
	public ref bool TouchedByPlayer => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bTouchedByPlayer");

	// m_bFirstCollisionAfterLaunch
	[SchemaMember("CPhysicsProp", "m_bFirstCollisionAfterLaunch")]
	public ref bool FirstCollisionAfterLaunch => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bFirstCollisionAfterLaunch");

	// m_iExploitableByPlayer
	[SchemaMember("CPhysicsProp", "m_iExploitableByPlayer")]
	public ref Int32 ExploitableByPlayer => ref Schema.GetRef<Int32>(this.Handle, "CPhysicsProp", "m_iExploitableByPlayer");

	// m_bHasBeenAwakened
	[SchemaMember("CPhysicsProp", "m_bHasBeenAwakened")]
	public ref bool HasBeenAwakened => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bHasBeenAwakened");

	// m_bIsOverrideProp
	[SchemaMember("CPhysicsProp", "m_bIsOverrideProp")]
	public ref bool IsOverrideProp => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bIsOverrideProp");

	// m_fNextCheckDisableMotionContactsTime
	[SchemaMember("CPhysicsProp", "m_fNextCheckDisableMotionContactsTime")]
	public ref float NextCheckDisableMotionContactsTime => ref Schema.GetRef<float>(this.Handle, "CPhysicsProp", "m_fNextCheckDisableMotionContactsTime");

	// m_iInitialGlowState
	[SchemaMember("CPhysicsProp", "m_iInitialGlowState")]
	public ref Int32 InitialGlowState => ref Schema.GetRef<Int32>(this.Handle, "CPhysicsProp", "m_iInitialGlowState");

	// m_nGlowRange
	[SchemaMember("CPhysicsProp", "m_nGlowRange")]
	public ref Int32 GlowRange => ref Schema.GetRef<Int32>(this.Handle, "CPhysicsProp", "m_nGlowRange");

	// m_nGlowRangeMin
	[SchemaMember("CPhysicsProp", "m_nGlowRangeMin")]
	public ref Int32 GlowRangeMin => ref Schema.GetRef<Int32>(this.Handle, "CPhysicsProp", "m_nGlowRangeMin");

	// m_glowColor
	[SchemaMember("CPhysicsProp", "m_glowColor")]
	public Color GlowColor
	{
		get { return Schema.GetCustomMarshalledType<Color>(this.Handle, "CPhysicsProp", "m_glowColor"); }
		set { Schema.SetCustomMarshalledType<Color>(this.Handle, "CPhysicsProp", "m_glowColor", value); }
	}

	// m_bForceNavIgnore
	[SchemaMember("CPhysicsProp", "m_bForceNavIgnore")]
	public ref bool ForceNavIgnore => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bForceNavIgnore");

	// m_bNoNavmeshBlocker
	[SchemaMember("CPhysicsProp", "m_bNoNavmeshBlocker")]
	public ref bool NoNavmeshBlocker => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bNoNavmeshBlocker");

	// m_bForceNpcExclude
	[SchemaMember("CPhysicsProp", "m_bForceNpcExclude")]
	public ref bool ForceNpcExclude => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bForceNpcExclude");

	// m_bShouldAutoConvertBackFromDebris
	[SchemaMember("CPhysicsProp", "m_bShouldAutoConvertBackFromDebris")]
	public ref bool ShouldAutoConvertBackFromDebris => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bShouldAutoConvertBackFromDebris");

	// m_bMuteImpactEffects
	[SchemaMember("CPhysicsProp", "m_bMuteImpactEffects")]
	public ref bool MuteImpactEffects => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bMuteImpactEffects");

	// m_bAcceptDamageFromHeldObjects
	[SchemaMember("CPhysicsProp", "m_bAcceptDamageFromHeldObjects")]
	public ref bool AcceptDamageFromHeldObjects => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bAcceptDamageFromHeldObjects");

	// m_bEnableUseOutput
	[SchemaMember("CPhysicsProp", "m_bEnableUseOutput")]
	public ref bool EnableUseOutput => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bEnableUseOutput");

	// m_bAwake
	[SchemaMember("CPhysicsProp", "m_bAwake")]
	public ref bool Awake => ref Schema.GetRef<bool>(this.Handle, "CPhysicsProp", "m_bAwake");

	// m_nCollisionGroupOverride
	[SchemaMember("CPhysicsProp", "m_nCollisionGroupOverride")]
	public ref Int32 CollisionGroupOverride => ref Schema.GetRef<Int32>(this.Handle, "CPhysicsProp", "m_nCollisionGroupOverride");

}