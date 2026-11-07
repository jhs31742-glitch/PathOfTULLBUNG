#pragma once

#include "Combat.h"
#include "DamageRules.h"

namespace TULLBUNG::Classes::Paladin
{
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsAvailable() noexcept;

    void ResetTransientState() noexcept;
    void OnEquipmentChanged() noexcept;

    void SyncRuntimePassives() noexcept;
    void RemoveRuntimePassives() noexcept;

    void AccumulateWeaponDirectDamage(
        const Combat::CombatContext& a_context,
        DamageRules::DirectDamageAccumulator& a_accumulator) noexcept;

    [[nodiscard]] float ResolveHealingBonusPercent(
        const RE::SpellItem* a_spell,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot) noexcept;

    void OnPlayerWeaponDirectHit(
        const Combat::CombatContext& a_context,
        RE::HitData& a_data) noexcept;

    [[nodiscard]] std::uint8_t
        GetHolyStrikeOvercastIndex() noexcept;

    void RestoreHolyStrikeOvercastIndex(
        std::uint8_t a_index) noexcept;
}