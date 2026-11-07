#pragma once

#include "Combat.h"

#include "RE/H/HitData.h"

namespace TULLBUNG::Classes::Warrior
{
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsAvailable() noexcept;

    void ResetTransientState() noexcept;

    void OnEquipmentChanged() noexcept;

    void OnCombatStateChanged(
        RE::Actor* a_actor,
        RE::Actor* a_target,
        bool a_enteredCombat,
        bool a_leftCombat) noexcept;

    void OnActorAction(
        RE::Actor* a_actor,
        bool a_weaponSwing) noexcept;

    void OnPlayerSprintInput(
        bool a_pressed) noexcept;

    void OnPlayerActionStart(
        const Combat::AttackActionState& a_action) noexcept;

    void OnPlayerWeaponDirectHit(
        const Combat::CombatContext& a_context,
        RE::HitData& a_data) noexcept;

    void OnPlayerHitEvent(
        const Combat::CombatContext& a_context) noexcept;

    void OnPlayerBashHitData(
        const Combat::CombatContext& a_context,
        RE::HitData& a_data) noexcept;

    void OnPlayerIncomingHit(
        RE::Actor* a_aggressor,
        const RE::HitData& a_data,
        const Snapshot::ActorResources& a_playerBefore) noexcept;
}
