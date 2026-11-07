#pragma once

#include "Combat.h"
#include "DamageRules.h"

namespace RE
{
    class EffectSetting;
}

namespace TULLBUNG::Classes::Inquisitor
{
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsAvailable() noexcept;

    void OnCombatStateChanged(
        RE::Actor* a_actor,
        RE::Actor* a_target,
        bool a_enteredCombat,
        bool a_leftCombat) noexcept;
    void OnPlayerMovementStateChanged(
        bool a_moving) noexcept;
    void ResetTransientState() noexcept;

    void SyncRuntimePassives() noexcept;
    void RemoveRuntimePassives() noexcept;

    void AccumulateSpellDirectDamage(
        const RE::SpellItem* a_spell,
        RE::Actor* a_target,
        DamageRules::DirectDamageAccumulator& a_accumulator) noexcept;

    void AccumulateIncomingElementalDR(
        RE::Actor* a_caster,
        RE::Actor* a_target,
        const RE::EffectSetting* a_effect,
        DamageRules::ConditionalDRAccumulator& a_accumulator) noexcept;

    void OnPlayerVoiceFire(
        RE::Actor* a_player) noexcept;

    void AccumulateWeaponDirectDamage(
        const Combat::CombatContext& a_context,
        DamageRules::DirectDamageAccumulator& a_accumulator) noexcept;

    void AccumulateIncomingDirectDR(
        RE::Actor* a_aggressor,
        const RE::HitData& a_data,
        const Snapshot::ActorResources& a_playerBefore,
        DamageRules::ConditionalDRAccumulator& a_accumulator) noexcept;

    void OnDeathEvent(
        RE::Actor* a_dying,
        RE::Actor* a_killer) noexcept;

    [[nodiscard]] bool
        DispatchSharedVFX(
            RE::Actor* a_player,
            RE::Actor* a_target,
            RE::SpellItem* a_vfxSpell) noexcept;

    [[nodiscard]] std::uint32_t
        GetSmiteDamage() noexcept;
}