#pragma once

#include "Combat.h"
#include "DamageRules.h"

namespace TULLBUNG::Classes::Priest
{
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsAvailable() noexcept;

    void ResetTransientState() noexcept;
    void OnEquipmentChanged() noexcept;

    void AccumulateWeaponDirectDamage(
        const Combat::CombatContext& a_context,
        DamageRules::DirectDamageAccumulator& a_accumulator) noexcept;

    void AccumulateSpellDirectDamage(
        const RE::SpellItem* a_spell,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot,
        DamageRules::DirectDamageAccumulator& a_accumulator) noexcept;

    [[nodiscard]] float ResolveHealingBonusPercent(
        const RE::SpellItem* a_spell,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot) noexcept;

    void SyncRuntimePassives() noexcept;
    void RemoveRuntimePassives() noexcept;
}