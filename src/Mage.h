#pragma once

#include "Combat.h"

#include "RE/H/HitData.h"
#include "RE/N/NiPoint3.h"

#include <cstddef>
#include <cstdint>

namespace RE
{
    class Actor;
    class SpellItem;
    class TESObjectREFR;
}

namespace TULLBUNG::Snapshot
{
    struct ActionSnapshot;
    struct CastSnapshot;
    struct ExtendedActionState;
}

namespace TULLBUNG::Classes::Mage
{
    [[nodiscard]]
    bool Initialize() noexcept;

    [[nodiscard]]
    bool IsAvailable() noexcept;

    void ResetTransientState() noexcept;

    void SyncRuntimePassives() noexcept;

    void RemoveRuntimePassives() noexcept;

    void OnSpellLearned(
        RE::SpellItem* a_spell) noexcept;

    void OnPlayerVoiceFire(
        RE::Actor* a_player) noexcept;

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
        const TULLBUNG::Snapshot::
            ActionSnapshot&
                a_snapshot) noexcept;
    void PopulateExtendedActionState(
        RE::Actor* a_actor,
        TULLBUNG::Snapshot::
            ExtendedActionState&
                a_state) noexcept;

    void OnPlayerWeaponDirectHit(
        const Combat::CombatContext&
            a_context,
        RE::HitData& a_data) noexcept;

    void OnPlayerSpellDirectHit(
        const RE::SpellItem* a_spell,
        RE::Actor* a_target,
        Combat::Hand a_hand,
        const TULLBUNG::Snapshot::
            ActionSnapshot&
                a_snapshot) noexcept;

    void OnPlayerCastFinalized(
        const TULLBUNG::Snapshot::
            CastSnapshot&
                a_snapshot) noexcept;

    [[nodiscard]]
    float ResolveSiegeSpellDirectBonusPercent(
        const RE::SpellItem* a_spell,
        RE::Actor* a_target,
        const TULLBUNG::Snapshot::
            ActionSnapshot&
                a_snapshot) noexcept;
    [[nodiscard]]
    std::size_t
        GetLearnedSpellCount() noexcept;

    [[nodiscard]]
    std::uint8_t
        GetVoidCount() noexcept;
}