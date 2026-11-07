#pragma once

#include "Snapshot.h"

namespace TULLBUNG::Combat
{
    enum class Hand : std::uint8_t
    {
        kUnknown = 0,
        kRight,
        kLeft,
        kBoth
    };

    enum class RangeKind : std::uint8_t
    {
        kUnknown = 0,
        kMelee,
        kRanged
    };

    enum class AttackKind : std::uint8_t
    {
        kNormal = 0,
        kPower,
        kBash
    };

    enum class SourceKind : std::uint8_t
    {
        kNone = 0,
        kWeapon,
        kSpell,
        kOther
    };

    enum class ActionOrigin : std::uint8_t
    {
        kNone = 0,
        kWeaponSwing,
        kDualWeaponSwing,
        kSpellCast,
        kSpellFireFallback,
        kBowRelease,
        kWeaponHitFallback,
        kBashHit
    };

    struct AttackActionState final
    {
        std::uint64_t actionID{};
        std::uint64_t inputGeneration{};

        RE::Actor* actor{};
        RE::TESForm* sourceForm{};

        RE::FormID sourceFormID{};
        RE::FormID rightSourceFormID{};
        RE::FormID leftSourceFormID{};

        Hand hand{ Hand::kUnknown };
        SourceKind sourceKind{ SourceKind::kNone };
        ActionOrigin origin{ ActionOrigin::kNone };

        bool dualAction{};
        bool rightSwingSeen{};
        bool leftSwingSeen{};

        Snapshot::ActionSnapshot actionSnapshot{};
        bool snapshotCaptured{};

        bool spellFireSeen{};
        bool firstValidDirectHitSeen{};
    };

    struct CombatContext final
    {
        RE::Actor* attacker{};
        RE::Actor* target{};

        RE::TESForm* sourceForm{};
        RE::TESObjectWEAP* weapon{};
        RE::SpellItem* spell{};
        RE::BGSProjectile* projectile{};

        RE::FormID sourceFormID{};
        RE::FormID projectileFormID{};

        Hand hand{ Hand::kUnknown };
        RangeKind range{ RangeKind::kUnknown };
        AttackKind attackKind{ AttackKind::kNormal };
        SourceKind sourceKind{ SourceKind::kNone };

        bool sneakAttack{};
        bool blocked{};

        bool playerAttacker{};
        bool playerTarget{};

        bool weaponDirectCandidate{};
        bool spellDirectCandidate{};

        float directDamage{};
        bool directDamageKnown{};

        std::uint64_t rawEventSequence{};

        std::uint64_t attackActionID{};
        bool actionMatched{};
        bool firstValidDirectHit{};

        Snapshot::ActionSnapshot actionSnapshot{};
        Snapshot::HitSnapshot hitSnapshot{};
    };

    bool Initialize();

    void ResetTransientState();

    [[nodiscard]] bool BuildFromHitEvent(
        const RE::TESHitEvent& a_event,
        CombatContext& a_outContext);

    void DebugLogContext(
        const CombatContext& a_context);
}