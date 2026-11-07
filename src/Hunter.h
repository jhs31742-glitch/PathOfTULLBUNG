#pragma once

#include "Combat.h"
#include "DamageRules.h"

#include "RE/H/HitData.h"

#include <cstdint>

namespace TULLBUNG::Classes::Hunter
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

    void OnPlayerMovementStateChanged(
        bool a_moving) noexcept;

    void OnPlayerActionStart(
        const Combat::AttackActionState& a_action) noexcept;

    void OnPlayerRangedLaunch(
        std::uint64_t a_actionID,
        RE::TESObjectWEAP* a_weapon,
        float a_x,
        float a_y,
        float a_z) noexcept;

    // Freeze Hunter-owned target state before same-hit side effects.
    void PopulateHitState(
        RE::Actor* a_target,
        Snapshot::HitStateInput& a_hitState) noexcept;

    // PRE-engine contribution only.
    void AccumulateWeaponDirectDamage(
        const Combat::CombatContext& a_context,
        DamageRules::DirectDamageAccumulator&
            a_accumulator) noexcept;

    void AccumulateIncomingDirectDR(
        RE::Actor* a_aggressor,
        const RE::HitData& a_data,
        const Snapshot::ActorResources& a_playerBefore,
        DamageRules::ConditionalDRAccumulator&
            a_accumulator) noexcept;

    // PRE-engine direct-hit side effects / request generation.
    void OnPlayerWeaponDirectHit(
        const Combat::CombatContext& a_context,
        RE::HitData& a_data) noexcept;

    // POST-engine direct-hit side effects.
    void OnPlayerWeaponDirectHitPost(
        std::uint64_t a_actionID,
        RE::Actor* a_target) noexcept;

    void OnPlayerIncomingHit(
        RE::Actor* a_aggressor,
        const RE::HitData& a_data,
        const Snapshot::ActorResources& a_playerBefore) noexcept;

    // Stage13 final production.
    // Actual-death authority for Quiet Processing.
    void OnDeathEvent(
        RE::Actor* a_dying,
        RE::Actor* a_killer) noexcept;

    // Called only from the validated bounded P6 candidate scan.
    [[nodiscard]]
    bool CommitEmberCandidate(
        std::uint64_t a_actionID,
        RE::FormID a_directTargetFormID,
        RE::Actor* a_candidate) noexcept;

    void FinalizeEmberRequest(
        std::uint64_t a_actionID,
        RE::FormID a_directTargetFormID) noexcept;

    [[nodiscard]] bool IsComposureReady() noexcept;

    [[nodiscard]] bool IsQuietProcessingReady() noexcept;

    void RestoreQuietProcessingReady(
        bool a_ready) noexcept;
}