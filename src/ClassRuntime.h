#pragma once

#include "Combat.h"

#include "RE/H/HitData.h"
#include "RE/N/NiPoint3.h"

#include <cstdint>

namespace SKSE
{
    struct ActionEvent;
}

namespace RE
{
    class Actor;
    class EffectSetting;
    class TESObjectREFR;
}

namespace TULLBUNG::Snapshot
{
    struct CastSnapshot;
}
namespace TULLBUNG::ClassRuntime
{
    bool Initialize();

    void ResetTransientState() noexcept;

    void OnEquipmentChanged() noexcept;
    void OnArtifactStateChanged() noexcept;

    void OnActorActionEvent(
        const SKSE::ActionEvent& a_event) noexcept;

    void OnPlayerShoutInput() noexcept;

    void OnPlayerSprintInput(
        bool a_pressed) noexcept;

    void OnPlayerMovementStateChanged(
        bool a_moving) noexcept;

    void OnPlayerCastFinalized(
        const Snapshot::CastSnapshot&
            a_snapshot) noexcept;
    void OnPlayerCastingStateChanged(
        RE::Actor* a_actor) noexcept;

    void OnPlayerMagickaMutation(
        float a_beforeCurrent,
        float a_beforeMaximum,
        float a_afterCurrent,
        float a_afterMaximum) noexcept;

    void OnPlayerProjectileImpact(
        std::uint64_t a_actionID,
        RE::FormID a_sourceFormID,
        RE::TESObjectREFR* a_impactRef,
        const RE::NiPoint3& a_targetLoc,
        const Snapshot::ActionSnapshot&
            a_snapshot) noexcept;
    void PopulateActionExtendedState(
        RE::Actor* a_actor,
        Snapshot::ExtendedActionState&
            a_state) noexcept;

    void OnPlayerActionStart(
        const Combat::AttackActionState& a_action) noexcept;

    void OnPlayerRangedLaunch(
        std::uint64_t a_actionID,
        RE::TESObjectWEAP* a_weapon,
        float a_x,
        float a_y,
        float a_z) noexcept;

    void PopulateHitState(
        RE::Actor* a_target,
        Snapshot::HitStateInput& a_hitState) noexcept;

    void OnPlayerWeaponDirectHit(
        const Combat::CombatContext& a_context,
        RE::HitData& a_data) noexcept;

    void OnPlayerWeaponDirectHitPost(
        std::uint64_t a_actionID,
        RE::Actor* a_target) noexcept;

    void ApplyPlayerIncomingDamageRules(
        RE::Actor* a_aggressor,
        RE::HitData& a_data,
        const Snapshot::ActorResources& a_playerBefore) noexcept;

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

namespace TULLBUNG::ClassRuntime
{
    [[nodiscard]] float ResolvePlayerSpellDirectBonusPercent(
        const RE::SpellItem* a_spell,
        RE::Actor* a_target,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot) noexcept;

    [[nodiscard]] float ResolvePlayerIncomingElementalDamageMultiplier(
        RE::Actor* a_caster,
        RE::Actor* a_target,
        const RE::EffectSetting* a_effect) noexcept;

    [[nodiscard]] float ResolvePlayerHealingBonusPercent(
        const RE::SpellItem* a_spell,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot) noexcept;
}

namespace TULLBUNG::ClassRuntime
{
    void OnBuildStateChanged() noexcept;
}