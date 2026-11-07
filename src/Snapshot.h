#pragma once

#include "Classification.h"
#include "PlayerState.h"

#include "RE/E/EffectSetting.h"
#include "RE/M/MagicCaster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace TULLBUNG::Snapshot
{
    struct ResourceValue final
    {
        float current{};
        float maximum{};
        float percent{};
    };

    struct ActorResources final
    {
        ResourceValue health{};
        ResourceValue magicka{};
        ResourceValue stamina{};
    };

    struct CastingState final
    {
        bool known{};

        bool anySpellCasting{};
        bool leftSpellCasting{};
        bool rightSpellCasting{};

        bool charging{};
        bool casting{};
        bool concentrating{};

        bool leftHealingSpellCasting{};

        std::uint32_t leftCasterState{};
        std::uint32_t rightCasterState{};

        RE::FormID leftSpellFormID{};
        RE::FormID rightSpellFormID{};
    };

    struct ExtendedActionState final
    {
        std::uint8_t voidCount{};
        std::uint8_t overcastStage{};

        bool quietProcessingReady{};
        bool precisionReady{};
        bool siegeDeploymentActive{};

        bool priestStance{};
        bool leftHealingSpellCasting{};
        bool anySpellCasting{};

        bool spellCharging{};
        bool spellCasting{};
        bool spellConcentrating{};

        std::uint32_t leftCasterState{};
        std::uint32_t rightCasterState{};

        RE::FormID leftCastingSpellFormID{};
        RE::FormID rightCastingSpellFormID{};

        // 내부 직업 자원 producer는 후속 클래스 단계에서 연결.
        bool internalStateKnown{};

        // 아래 상태들은 서로 독립된 producer이므로 Known도 분리한다.
        bool castingStateKnown{};
        bool priestStanceKnown{};
        bool precisionStateKnown{};
        bool siegeStateKnown{};

        // 구 Stage 10 호환 필드. 모든 관련 condition이 권위값인 경우에만 true.
        bool conditionStateKnown{};
    };

    struct ActionSnapshot final
    {
        bool valid{};

        std::uint64_t actionID{};

        ActorResources player{};

        float totalArmor{};

        bool shieldEquipped{};
        bool twoHandedWeaponEquipped{};
        bool oneHandedBluntEquipped{};
        bool boundWeaponEquipped{};
        bool bowOrCrossbowEquipped{};

        std::uint8_t lightArmorPieces{};

        bool sneaking{};
        bool inCombat{};

        ExtendedActionState extended{};
    };

    struct HitStateInput final
    {
        bool bloodPoisonActive{};
        std::uint8_t bloodPoisonStage{};
        bool bloodPoisonStateKnown{};

        bool insideSiegeRune{};
        bool siegeRuneStateKnown{};

        float distanceFt{};
        bool distanceKnown{};

        float directDamage{};
        bool directDamageKnown{};
    };

    struct HitSnapshot final
    {
        bool valid{};

        std::uint64_t actionID{};
        RE::FormID targetFormID{};

        ActorResources target{};

        bool monster{};

        bool bloodPoisonActive{};
        std::uint8_t bloodPoisonStage{};
        bool bloodPoisonStateKnown{};

        bool insideSiegeRune{};
        bool siegeRuneStateKnown{};

        float distanceFt{};
        bool distanceKnown{};

        float directDamage{};
        bool directDamageKnown{};
    };

    struct LaunchSnapshot final
    {
        bool valid{};

        std::uint64_t actionID{};
        RE::FormID sourceFormID{};

        ActorResources player{};

        bool stationaryAtLaunch{};
        bool stationaryStateKnown{};

        bool precisionReady{};
        bool precisionStateKnown{};

        bool siegeDeploymentActive{};
        bool siegeStateKnown{};
    };

    struct CastSnapshot final
    {
        bool valid{};

        std::uint64_t actionID{};
        RE::FormID sourceFormID{};

        ActorResources playerAtCast{};

        float magickaAtCastEvent{};
        float magickaAtFireEvent{};

        float observedMagickaDelta{};
        bool observedMagickaDeltaKnown{};

        float casterReportedCost{};
        bool casterReportedCostKnown{};

        float calculatedCost{};
        bool calculatedCostKnown{};

        bool fireAndForget{};
        bool concentration{};

        std::uint32_t casterState{};

        bool siegeDeploymentActive{};
        bool siegeStateKnown{};
    };

    struct IncomingSnapshot final
    {
        bool valid{};

        std::uint64_t enemyActionID{};

        RE::FormID attackerFormID{};

        // 반드시 원본 Hit 처리 전 상태.
        ActorResources playerBefore{};

        float playerHealthAfter{};
        float observedHealthLost{};
        bool observedHealthLostKnown{};

        float attackerMaxHealth{};
        bool attackerMonster{};

        CastingState castingBefore{};

        bool blocked{};

        float physicalDamage{};
        float totalDamage{};
        bool damageKnown{};
    };

    struct AbilitySnapshot final
    {
        bool valid{};

        ActorResources player{};

        std::uint8_t voidCount{};
        bool internalStateKnown{};
    };

    struct CandidateSnapshot final
    {
        bool valid{};

        RE::FormID actorFormID{};

        float currentMagicka{};
        float maxHealth{};

        float distanceFt{};

        bool alive{};
        bool hostile{};
        bool monster{};
        bool lineOfSight{};
    };

    inline float CalculatePercent(
        float a_current,
        float a_maximum)
    {
        if (a_maximum <= 0.0f) {
            return 0.0f;
        }

        return std::clamp(
            (a_current / a_maximum) * 100.0f,
            0.0f,
            100.0f);
    }

    inline ResourceValue MakeResourceValue(
        float a_current,
        float a_maximum)
    {
        ResourceValue result{};

        result.current =
            a_current;

        result.maximum =
            a_maximum;

        result.percent =
            CalculatePercent(
                a_current,
                a_maximum);

        return result;
    }

    inline ResourceValue CaptureResource(
        const RE::Actor* a_actor,
        RE::ActorValue a_actorValue)
    {
        ResourceValue result{};

        if (!a_actor) {
            return result;
        }

        auto* owner =
            a_actor->AsActorValueOwner();

        if (!owner) {
            return result;
        }

        const float current =
            owner->GetActorValue(
                a_actorValue);

        const float maximum =
            a_actor->GetActorValueMax(
                a_actorValue);

        return MakeResourceValue(
            current,
            maximum);
    }

    inline ActorResources CaptureResources(
        const RE::Actor* a_actor)
    {
        ActorResources result{};

        if (!a_actor) {
            return result;
        }

        result.health =
            CaptureResource(
                a_actor,
                RE::ActorValue::kHealth);

        result.magicka =
            CaptureResource(
                a_actor,
                RE::ActorValue::kMagicka);

        result.stamina =
            CaptureResource(
                a_actor,
                RE::ActorValue::kStamina);

        return result;
    }

    inline float CaptureArmor(
        const RE::Actor* a_actor)
    {
        if (!a_actor) {
            return 0.0f;
        }

        auto* owner =
            a_actor->AsActorValueOwner();

        if (!owner) {
            return 0.0f;
        }

        return owner->GetActorValue(
            RE::ActorValue::kDamageResist);
    }



    inline bool IsCasterChargingState(
        RE::MagicCaster::State a_state)
    {
        using State =
            RE::MagicCaster::State;

        switch (a_state) {
        case State::kUnk01:
        case State::kUnk02:
        case State::kReady:
        case State::kUnk04:
        case State::kCharging:
            return true;

        default:
            return false;
        }
    }

    inline CastingState CaptureCastingState(
        RE::Actor* a_actor)
    {
        CastingState result{};

        if (!a_actor) {
            return result;
        }

        result.known =
            true;

        const auto inspect =
            [&](RE::MagicSystem::CastingSource a_source,
                bool a_left) {

                auto* caster =
                    a_actor->GetMagicCaster(
                        a_source);

                if (!caster) {
                    return;
                }

                const auto state =
                    caster->state.get();

                const auto rawState =
                    static_cast<std::uint32_t>(
                        state);

                auto* currentSpell =
                    caster->currentSpell;

                const bool engineCasting =
                    currentSpell &&
                    a_actor->IsCasting(
                        currentSpell);

                const bool charging =
                    IsCasterChargingState(
                        state);

                const bool casting =
                    state ==
                        RE::MagicCaster::State::
                            kCasting ||
                    engineCasting;

                const bool concentrating =
                    currentSpell &&
                    currentSpell->GetCastingType() ==
                        RE::MagicSystem::
                            CastingType::
                                kConcentration &&
                    (charging || casting);

                const bool active =
                    charging ||
                    casting ||
                    concentrating;

                if (a_left) {
                    result.leftCasterState =
                        rawState;

                    result.leftSpellFormID =
                        currentSpell ?
                            currentSpell->
                                GetFormID() :
                            0;

                    result.leftSpellCasting =
                        active;

                    auto* currentSpellItem =
                        currentSpell ?
                            currentSpell->As<
                                RE::SpellItem>() :
                            nullptr;

                    result.leftHealingSpellCasting =
                        active &&
                        Classification::
                            IsHealthRestorationSpell(
                                currentSpellItem);
                } else {
                    result.rightCasterState =
                        rawState;

                    result.rightSpellFormID =
                        currentSpell ?
                            currentSpell->
                                GetFormID() :
                            0;

                    result.rightSpellCasting =
                        active;
                }

                result.charging =
                    result.charging ||
                    charging;

                result.casting =
                    result.casting ||
                    casting;

                result.concentrating =
                    result.concentrating ||
                    concentrating;
            };

        inspect(
            RE::MagicSystem::CastingSource::
                kLeftHand,
            true);

        inspect(
            RE::MagicSystem::CastingSource::
                kRightHand,
            false);

        result.anySpellCasting =
            result.leftSpellCasting ||
            result.rightSpellCasting;

        return result;
    }

    inline bool IsTwoHandedWeapon(
        const RE::TESObjectWEAP* a_weapon)
    {
        if (!a_weapon) {
            return false;
        }

        using WC =
            Classification::WeaponClass;

        const auto weaponClass =
            Classification::
                ClassifyWeapon(
                    a_weapon);

        return
            weaponClass ==
                WC::kTwoHandSword ||
            weaponClass ==
                WC::kTwoHandAxe;
    }

    inline ActionSnapshot CaptureAction(
        RE::Actor* a_actor,
        std::uint64_t a_actionID,
        const ExtendedActionState&
            a_extended = {})
    {
        ActionSnapshot result{};

        if (!a_actor ||
            !a_actionID) {

            return result;
        }

        result.valid = true;

        result.actionID =
            a_actionID;

        result.player =
            CaptureResources(
                a_actor);

        result.totalArmor =
            CaptureArmor(
                a_actor);

        const auto& equipment =
            PlayerState::GetEquipment();

        result.shieldEquipped =
            equipment.hasShield;

        result.lightArmorPieces =
            equipment.lightArmorPieces;

        const auto* rightWeapon =
            equipment.right.weapon;

        const auto* leftWeapon =
            equipment.left.weapon;

        result.twoHandedWeaponEquipped =
            IsTwoHandedWeapon(
                rightWeapon) ||
            IsTwoHandedWeapon(
                leftWeapon);

        result.oneHandedBluntEquipped =
            Classification::
                IsOneHandedBlunt(
                    rightWeapon) ||
            Classification::
                IsOneHandedBlunt(
                    leftWeapon);

        result.boundWeaponEquipped =
            equipment.right.boundWeapon ||
            equipment.left.boundWeapon;

        result.bowOrCrossbowEquipped =
            Classification::
                IsBowOrCrossbow(
                    rightWeapon) ||
            Classification::
                IsBowOrCrossbow(
                    leftWeapon);

        result.sneaking =
            a_actor->IsSneaking();

        result.inCombat =
            a_actor->IsInCombat();

        result.extended =
            a_extended;

        return result;
    }

    inline HitSnapshot CaptureHit(
        RE::Actor* a_target,
        std::uint64_t a_actionID,
        const HitStateInput&
            a_input = {})
    {
        HitSnapshot result{};

        if (!a_target) {
            return result;
        }

        result.valid = true;

        result.actionID =
            a_actionID;

        result.targetFormID =
            a_target->GetFormID();

        result.target =
            CaptureResources(
                a_target);

        result.monster =
            Classification::
                IsMonster(
                    a_target);

        result.bloodPoisonActive =
            a_input.bloodPoisonActive;

        result.bloodPoisonStage =
            a_input.bloodPoisonStage;

        result.bloodPoisonStateKnown =
            a_input.bloodPoisonStateKnown;

        result.insideSiegeRune =
            a_input.insideSiegeRune;

        result.siegeRuneStateKnown =
            a_input.siegeRuneStateKnown;

        result.distanceFt =
            a_input.distanceFt;

        result.distanceKnown =
            a_input.distanceKnown;

        result.directDamage =
            a_input.directDamage;

        result.directDamageKnown =
            a_input.directDamageKnown;

        return result;
    }

    inline LaunchSnapshot CaptureLaunch(
        RE::Actor* a_actor,
        std::uint64_t a_actionID,
        RE::FormID a_sourceFormID,
        const ExtendedActionState&
            a_extended = {})
    {
        LaunchSnapshot result{};

        if (!a_actor ||
            !a_actionID) {

            return result;
        }

        result.valid = true;

        result.actionID =
            a_actionID;

        result.sourceFormID =
            a_sourceFormID;

        result.player =
            CaptureResources(
                a_actor);

        result.stationaryAtLaunch =
            !a_actor->IsMoving();

        result.stationaryStateKnown =
            true;

        result.precisionReady =
            a_extended.precisionReady;

        result.precisionStateKnown =
            a_extended.precisionStateKnown;

        result.siegeDeploymentActive =
            a_extended.siegeDeploymentActive;

        result.siegeStateKnown =
            a_extended.siegeStateKnown;

        return result;
    }

    inline CastSnapshot CaptureCastStart(
        RE::Actor* a_actor,
        std::uint64_t a_actionID,
        RE::SpellItem* a_spell,
        RE::MagicSystem::CastingSource
            a_castingSource,
        const ExtendedActionState&
            a_extended = {})
    {
        CastSnapshot result{};

        if (!a_actor ||
            !a_actionID ||
            !a_spell) {

            return result;
        }

        result.valid = true;

        result.actionID =
            a_actionID;

        result.sourceFormID =
            a_spell->GetFormID();

        result.playerAtCast =
            CaptureResources(
                a_actor);

        result.magickaAtCastEvent =
            result.playerAtCast.
                magicka.current;

        result.fireAndForget =
            a_spell->GetCastingType() ==
                RE::MagicSystem::
                    CastingType::
                        kFireAndForget;

        result.concentration =
            a_spell->GetCastingType() ==
                RE::MagicSystem::
                    CastingType::
                        kConcentration;

        result.calculatedCost =
            (std::max)(
                0.0f,
                a_spell->
                    CalculateMagickaCost(
                        a_actor));

        result.calculatedCostKnown =
            true;

        if (auto* caster =
                a_actor->GetMagicCaster(
                    a_castingSource)) {

            result.casterState =
                static_cast<std::uint32_t>(
                    caster->state.get());

            if (caster->currentSpell ==
                a_spell) {

                result.casterReportedCost =
                    (std::max)(
                        0.0f,
                        caster->
                            GetCurrentSpellCost());

                result.casterReportedCostKnown =
                    true;
            }
        }

        result.siegeDeploymentActive =
            a_extended.siegeDeploymentActive;

        result.siegeStateKnown =
            a_extended.siegeStateKnown;

        return result;
    }

    inline void FinalizeCastAtFire(
        CastSnapshot& a_snapshot,
        RE::Actor* a_actor)
    {
        if (!a_snapshot.valid ||
            !a_actor) {

            return;
        }

        const auto resources =
            CaptureResources(
                a_actor);

        a_snapshot.magickaAtFireEvent =
            resources.magicka.current;

        a_snapshot.observedMagickaDelta =
            (std::max)(
                0.0f,
                a_snapshot.magickaAtCastEvent -
                a_snapshot.magickaAtFireEvent);

        a_snapshot.observedMagickaDeltaKnown =
            true;
    }

    inline IncomingSnapshot CaptureIncoming(
        RE::Actor* a_player,
        RE::Actor* a_attacker,
        const ActorResources&
            a_playerBefore,
        const CastingState&
            a_castingBefore,
        float a_playerHealthAfter,
        float a_physicalDamage,
        float a_totalDamage,
        bool a_damageKnown,
        bool a_blocked)
    {
        IncomingSnapshot result{};

        if (!a_player ||
            !a_attacker) {

            return result;
        }

        result.valid = true;

        result.attackerFormID =
            a_attacker->GetFormID();

        result.playerBefore =
            a_playerBefore;

        result.playerHealthAfter =
            a_playerHealthAfter;

        result.observedHealthLost =
            (std::max)(
                0.0f,
                a_playerBefore.
                    health.current -
                a_playerHealthAfter);

        result.observedHealthLostKnown =
            true;

        result.attackerMaxHealth =
            CaptureResource(
                a_attacker,
                RE::ActorValue::kHealth).
                    maximum;

        result.attackerMonster =
            Classification::
                IsMonster(
                    a_attacker);

        result.castingBefore =
            a_castingBefore;

        result.blocked =
            a_blocked;

        result.physicalDamage =
            a_physicalDamage;

        result.totalDamage =
            a_totalDamage;

        result.damageKnown =
            a_damageKnown;

        return result;
    }

    inline AbilitySnapshot CaptureAbility(
        RE::Actor* a_player,
        std::uint8_t a_voidCount,
        bool a_internalStateKnown)
    {
        AbilitySnapshot result{};

        if (!a_player) {
            return result;
        }

        result.valid = true;

        result.player =
            CaptureResources(
                a_player);

        result.voidCount =
            a_voidCount;

        result.internalStateKnown =
            a_internalStateKnown;

        return result;
    }

    inline void DebugLogAction(
        const ActionSnapshot& a_snapshot)
    {
        if (!a_snapshot.valid) {
            return;
        }

        logger::info(
            "[SnapshotAction] Capture=ACTION_START | ActionID={} | HP={:.2f}/{:.2f}({:.2f}%) | MP={:.2f}/{:.2f}({:.2f}%) | SP={:.2f}/{:.2f}({:.2f}%) | Armor={:.2f} | Void={} | Overcast={} | InternalKnown={} | Shield={} | TwoHanded={} | Blunt={} | Bound={} | Ranged={} | LightArmor={} | Sneak={} | InCombat={} | PriestStance={} | PriestKnown={} | LeftHealCasting={} | AnySpellCasting={} | Charging={} | Casting={} | Concentrating={} | LeftCasterState={} | RightCasterState={} | LeftSpell={:08X} | RightSpell={:08X} | CastingKnown={} | PrecisionReady={} | PrecisionKnown={} | SiegeActive={} | SiegeKnown={}",
            a_snapshot.actionID,
            a_snapshot.player.health.current,
            a_snapshot.player.health.maximum,
            a_snapshot.player.health.percent,
            a_snapshot.player.magicka.current,
            a_snapshot.player.magicka.maximum,
            a_snapshot.player.magicka.percent,
            a_snapshot.player.stamina.current,
            a_snapshot.player.stamina.maximum,
            a_snapshot.player.stamina.percent,
            a_snapshot.totalArmor,
            a_snapshot.extended.voidCount,
            a_snapshot.extended.overcastStage,
            a_snapshot.extended.internalStateKnown,
            a_snapshot.shieldEquipped,
            a_snapshot.twoHandedWeaponEquipped,
            a_snapshot.oneHandedBluntEquipped,
            a_snapshot.boundWeaponEquipped,
            a_snapshot.bowOrCrossbowEquipped,
            a_snapshot.lightArmorPieces,
            a_snapshot.sneaking,
            a_snapshot.inCombat,
            a_snapshot.extended.priestStance,
            a_snapshot.extended.priestStanceKnown,
            a_snapshot.extended.leftHealingSpellCasting,
            a_snapshot.extended.anySpellCasting,
            a_snapshot.extended.spellCharging,
            a_snapshot.extended.spellCasting,
            a_snapshot.extended.spellConcentrating,
            a_snapshot.extended.leftCasterState,
            a_snapshot.extended.rightCasterState,
            a_snapshot.extended.leftCastingSpellFormID,
            a_snapshot.extended.rightCastingSpellFormID,
            a_snapshot.extended.castingStateKnown,
            a_snapshot.extended.precisionReady,
            a_snapshot.extended.precisionStateKnown,
            a_snapshot.extended.siegeDeploymentActive,
            a_snapshot.extended.siegeStateKnown);
    }

    inline void DebugLogHit(
        const HitSnapshot& a_snapshot)
    {
        if (!a_snapshot.valid) {
            return;
        }

        logger::info(
            "[SnapshotHit] ActionID={} | Target={:08X} | HP={:.2f}/{:.2f}({:.2f}%) | MP={:.2f}/{:.2f}({:.2f}%) | SP={:.2f}/{:.2f}({:.2f}%) | Monster={} | BloodPoison={} | BloodStage={} | BloodKnown={} | SiegeRune={} | SiegeKnown={} | DistanceFt={:.2f} | DistanceKnown={} | DirectDamage={:.3f} | DamageKnown={}",
            a_snapshot.actionID,
            a_snapshot.targetFormID,
            a_snapshot.target.health.current,
            a_snapshot.target.health.maximum,
            a_snapshot.target.health.percent,
            a_snapshot.target.magicka.current,
            a_snapshot.target.magicka.maximum,
            a_snapshot.target.magicka.percent,
            a_snapshot.target.stamina.current,
            a_snapshot.target.stamina.maximum,
            a_snapshot.target.stamina.percent,
            a_snapshot.monster,
            a_snapshot.bloodPoisonActive,
            a_snapshot.bloodPoisonStage,
            a_snapshot.bloodPoisonStateKnown,
            a_snapshot.insideSiegeRune,
            a_snapshot.siegeRuneStateKnown,
            a_snapshot.distanceFt,
            a_snapshot.distanceKnown,
            a_snapshot.directDamage,
            a_snapshot.directDamageKnown);
    }

    inline void DebugLogLaunch(
        const LaunchSnapshot& a_snapshot)
    {
        if (!a_snapshot.valid) {
            return;
        }

        logger::info(
            "[SnapshotLaunch] ActionID={} | Source={:08X} | MP={:.2f}/{:.2f} | Stationary={} | StationaryKnown={} | PrecisionReady={} | PrecisionKnown={} | SiegeActive={} | SiegeKnown={}",
            a_snapshot.actionID,
            a_snapshot.sourceFormID,
            a_snapshot.player.magicka.current,
            a_snapshot.player.magicka.maximum,
            a_snapshot.stationaryAtLaunch,
            a_snapshot.stationaryStateKnown,
            a_snapshot.precisionReady,
            a_snapshot.precisionStateKnown,
            a_snapshot.siegeDeploymentActive,
            a_snapshot.siegeStateKnown);
    }

    inline void DebugLogCast(
        const CastSnapshot& a_snapshot,
        std::string_view a_phase)
    {
        if (!a_snapshot.valid) {
            return;
        }

        logger::info(
            "[SnapshotCast] Phase={} | ActionID={} | Source={:08X} | MPAtCast={:.2f} | MPAtFire={:.2f} | ObservedDelta={:.2f} | DeltaKnown={} | CasterCost={:.2f} | CasterCostKnown={} | CalculatedCost={:.2f} | CalculatedKnown={} | FireAndForget={} | Concentration={} | CasterState={} | SiegeActive={} | SiegeKnown={}",
            a_phase,
            a_snapshot.actionID,
            a_snapshot.sourceFormID,
            a_snapshot.magickaAtCastEvent,
            a_snapshot.magickaAtFireEvent,
            a_snapshot.observedMagickaDelta,
            a_snapshot.observedMagickaDeltaKnown,
            a_snapshot.casterReportedCost,
            a_snapshot.casterReportedCostKnown,
            a_snapshot.calculatedCost,
            a_snapshot.calculatedCostKnown,
            a_snapshot.fireAndForget,
            a_snapshot.concentration,
            a_snapshot.casterState,
            a_snapshot.siegeDeploymentActive,
            a_snapshot.siegeStateKnown);
    }

    inline void DebugLogIncoming(
        const IncomingSnapshot& a_snapshot)
    {
        if (!a_snapshot.valid) {
            return;
        }

        logger::info(
            "[SnapshotIncoming] Attacker={:08X} | AttackerMaxHP={:.2f} | Monster={} | HPBefore={:.2f}/{:.2f} | HPAfter={:.2f} | ObservedLost={:.3f} | LostKnown={} | AnyCasting={} | LeftCasting={} | RightCasting={} | Charging={} | Casting={} | Concentrating={} | LeftHealCasting={} | LeftState={} | RightState={} | LeftSpell={:08X} | RightSpell={:08X} | CastingKnown={} | Blocked={} | Physical={:.3f} | Total={:.3f} | DamageKnown={}",
            a_snapshot.attackerFormID,
            a_snapshot.attackerMaxHealth,
            a_snapshot.attackerMonster,
            a_snapshot.playerBefore.health.current,
            a_snapshot.playerBefore.health.maximum,
            a_snapshot.playerHealthAfter,
            a_snapshot.observedHealthLost,
            a_snapshot.observedHealthLostKnown,
            a_snapshot.castingBefore.anySpellCasting,
            a_snapshot.castingBefore.leftSpellCasting,
            a_snapshot.castingBefore.rightSpellCasting,
            a_snapshot.castingBefore.charging,
            a_snapshot.castingBefore.casting,
            a_snapshot.castingBefore.concentrating,
            a_snapshot.castingBefore.leftHealingSpellCasting,
            a_snapshot.castingBefore.leftCasterState,
            a_snapshot.castingBefore.rightCasterState,
            a_snapshot.castingBefore.leftSpellFormID,
            a_snapshot.castingBefore.rightSpellFormID,
            a_snapshot.castingBefore.known,
            a_snapshot.blocked,
            a_snapshot.physicalDamage,
            a_snapshot.totalDamage,
            a_snapshot.damageKnown);
    }

    inline bool NearlyEqual(
        float a_lhs,
        float a_rhs,
        float a_epsilon = 0.001f)
    {
        return
            std::fabs(
                a_lhs - a_rhs) <=
            a_epsilon;
    }

    inline bool RunSelfTest()
    {
        bool allPass = true;

        const auto check =
            [&allPass](
                std::string_view a_name,
                bool a_pass) {

                logger::info(
                    "[SnapshotSelfTest] {} = {}",
                    a_name,
                    a_pass ?
                        "PASS" :
                        "FAIL");

                allPass =
                    allPass &&
                    a_pass;
            };

        // ----------------------------------------------------
        // Resource copy / threshold contracts
        // ----------------------------------------------------

        ActorResources source{};

        source.health =
            MakeResourceValue(
                490.0f,
                1000.0f);

        source.magicka =
            MakeResourceValue(
                300.0f,
                1000.0f);

        source.stamina =
            MakeResourceValue(
                250.0f,
                1000.0f);

        ActionSnapshot action{};

        action.valid = true;
        action.actionID = 100;
        action.player = source;
        action.totalArmor = 777.0f;

        source.health.current =
            1.0f;

        source.magicka.current =
            1.0f;

        source.stamina.current =
            1.0f;

        check(
            "ResourceCopy",
            NearlyEqual(
                action.player.health.current,
                490.0f) &&
            NearlyEqual(
                action.player.magicka.current,
                300.0f) &&
            NearlyEqual(
                action.player.stamina.current,
                250.0f));

        check(
            "HealthThreshold49_50_51",
            CalculatePercent(
                490.0f,
                1000.0f) < 50.0f &&
            NearlyEqual(
                CalculatePercent(
                    500.0f,
                    1000.0f),
                50.0f) &&
            CalculatePercent(
                510.0f,
                1000.0f) > 50.0f);

        check(
            "HealthThreshold79_80_81",
            CalculatePercent(
                790.0f,
                1000.0f) < 80.0f &&
            NearlyEqual(
                CalculatePercent(
                    800.0f,
                    1000.0f),
                80.0f) &&
            CalculatePercent(
                810.0f,
                1000.0f) > 80.0f);

        check(
            "StaminaThreshold24_25_26",
            CalculatePercent(
                240.0f,
                1000.0f) < 25.0f &&
            NearlyEqual(
                CalculatePercent(
                    250.0f,
                    1000.0f),
                25.0f) &&
            CalculatePercent(
                260.0f,
                1000.0f) > 25.0f);

        check(
            "MagickaCross25",
            CalculatePercent(
                260.0f,
                1000.0f) > 25.0f &&
            NearlyEqual(
                CalculatePercent(
                    250.0f,
                    1000.0f),
                25.0f) &&
            CalculatePercent(
                240.0f,
                1000.0f) < 25.0f);

        // ----------------------------------------------------
        // Internal state immutability
        // ----------------------------------------------------

        ExtendedActionState extended{};

        extended.voidCount = 1;
        extended.overcastStage = 4;
        extended.quietProcessingReady = true;
        extended.precisionReady = true;
        extended.siegeDeploymentActive = true;
        extended.priestStance = true;
        extended.leftHealingSpellCasting = true;
        extended.anySpellCasting = true;
        extended.internalStateKnown = true;
        extended.castingStateKnown = true;
        extended.priestStanceKnown = true;
        extended.precisionStateKnown = true;
        extended.siegeStateKnown = true;
        extended.conditionStateKnown = true;

        extended.spellCharging = true;
        extended.spellCasting = true;
        extended.spellConcentrating = true;

        extended.leftCasterState = 5;
        extended.rightCasterState = 6;

        extended.leftCastingSpellFormID = 0x111;
        extended.rightCastingSpellFormID = 0x222;

        action.extended =
            extended;

        extended.voidCount = 0;
        extended.overcastStage = 1;
        extended.quietProcessingReady = false;

        check(
            "VoidImmutableAfterConsume",
            action.extended.voidCount == 1);

        check(
            "OvercastImmutableAfterStep",
            action.extended.overcastStage == 4);

        check(
            "RelatedStateImmutable",
            action.extended.quietProcessingReady &&
            action.extended.precisionReady &&
            action.extended.siegeDeploymentActive &&
            action.extended.priestStance &&
            action.extended.leftHealingSpellCasting &&
            action.extended.anySpellCasting &&
            action.extended.spellCharging &&
            action.extended.spellCasting &&
            action.extended.spellConcentrating &&
            action.extended.castingStateKnown &&
            action.extended.priestStanceKnown);

        // ----------------------------------------------------
        // Armor pre-buff contract
        // ----------------------------------------------------

        float currentArmor =
            777.0f;

        action.totalArmor =
            currentArmor;

        currentArmor +=
            50.0f;

        check(
            "ArmorPreBuffImmutable",
            NearlyEqual(
                action.totalArmor,
                777.0f) &&
            NearlyEqual(
                currentArmor,
                827.0f));

        // ----------------------------------------------------
        // Target pre-effect / multi-target contracts
        // ----------------------------------------------------

        HitSnapshot targetA{};

        targetA.valid = true;
        targetA.actionID = 100;
        targetA.targetFormID = 0x100;
        targetA.target.health =
            MakeResourceValue(
                200.0f,
                1000.0f);

        targetA.bloodPoisonActive =
            false;

        targetA.bloodPoisonStage =
            0;

        targetA.bloodPoisonStateKnown =
            true;

        bool currentBloodPoison =
            true;

        check(
            "TargetStatePreEffect",
            !targetA.bloodPoisonActive &&
            targetA.bloodPoisonStage == 0 &&
            currentBloodPoison);

        HitSnapshot targetB =
            targetA;

        targetB.targetFormID =
            0x200;

        targetB.target.health =
            MakeResourceValue(
                900.0f,
                1000.0f);

        targetB.bloodPoisonActive =
            true;

        targetB.bloodPoisonStage =
            2;

        check(
            "MultiTargetIndependent",
            targetA.targetFormID !=
                targetB.targetFormID &&
            targetA.target.health.percent <
                50.0f &&
            targetB.target.health.percent >
                50.0f &&
            !targetA.bloodPoisonActive &&
            targetB.bloodPoisonActive);

        // ----------------------------------------------------
        // Same action shares, different action refreshes
        // ----------------------------------------------------

        ActionSnapshot sharedRight =
            action;

        ActionSnapshot sharedLeft =
            action;

        check(
            "SameActionSharedSnapshot",
            sharedRight.actionID ==
                sharedLeft.actionID &&
            NearlyEqual(
                sharedRight.player.health.current,
                sharedLeft.player.health.current) &&
            sharedRight.extended.voidCount ==
                sharedLeft.extended.voidCount);

        ActionSnapshot fresh =
            action;

        fresh.actionID =
            101;

        fresh.player.health =
            MakeResourceValue(
                900.0f,
                1000.0f);

        fresh.extended.voidCount =
            0;

        check(
            "DifferentActionFreshSnapshot",
            fresh.actionID !=
                action.actionID &&
            !NearlyEqual(
                fresh.player.health.current,
                action.player.health.current) &&
            fresh.extended.voidCount !=
                action.extended.voidCount);

        // ----------------------------------------------------
        // Launch / Cast / Incoming / Ability contracts
        // ----------------------------------------------------

        LaunchSnapshot launch{};

        launch.valid = true;
        launch.actionID = 200;
        launch.stationaryAtLaunch = true;
        launch.stationaryStateKnown = true;
        launch.precisionReady = true;
        launch.precisionStateKnown = true;

        check(
            "LaunchContract",
            launch.valid &&
            launch.stationaryAtLaunch &&
            launch.stationaryStateKnown &&
            launch.precisionReady &&
            launch.precisionStateKnown);

        CastSnapshot cast{};

        cast.valid = true;
        cast.actionID = 300;
        cast.magickaAtCastEvent = 500.0f;
        cast.magickaAtFireEvent = 420.0f;
        cast.observedMagickaDelta = 80.0f;
        cast.observedMagickaDeltaKnown = true;
        cast.casterReportedCost = 80.0f;
        cast.casterReportedCostKnown = true;
        cast.calculatedCost = 80.0f;
        cast.calculatedCostKnown = true;
        cast.fireAndForget = true;

        check(
            "CastTransactionContract",
            cast.valid &&
            NearlyEqual(
                cast.observedMagickaDelta,
                80.0f) &&
            cast.observedMagickaDeltaKnown &&
            NearlyEqual(
                cast.casterReportedCost,
                80.0f) &&
            cast.casterReportedCostKnown &&
            NearlyEqual(
                cast.calculatedCost,
                80.0f) &&
            cast.calculatedCostKnown &&
            cast.fireAndForget);

        IncomingSnapshot incoming{};

        incoming.valid = true;
        incoming.attackerMaxHealth = 2000.0f;
        incoming.playerBefore.health =
            MakeResourceValue(
                1000.0f,
                1000.0f);
        incoming.playerHealthAfter = 905.0f;
        incoming.observedHealthLost = 95.0f;
        incoming.observedHealthLostKnown = true;
        incoming.castingBefore.known = true;
        incoming.castingBefore.anySpellCasting = true;
        incoming.castingBefore.concentrating = true;
        incoming.physicalDamage = 120.0f;
        incoming.totalDamage = 95.0f;
        incoming.damageKnown = true;

        check(
            "IncomingContract",
            incoming.valid &&
            NearlyEqual(
                incoming.attackerMaxHealth,
                2000.0f) &&
            NearlyEqual(
                incoming.observedHealthLost,
                95.0f) &&
            incoming.observedHealthLostKnown &&
            incoming.castingBefore.known &&
            incoming.castingBefore.anySpellCasting &&
            incoming.castingBefore.concentrating &&
            NearlyEqual(
                incoming.physicalDamage,
                120.0f) &&
            NearlyEqual(
                incoming.totalDamage,
                95.0f) &&
            incoming.damageKnown);

        AbilitySnapshot ability{};

        ability.valid = true;
        ability.player.magicka =
            MakeResourceValue(
                640.0f,
                1000.0f);

        ability.voidCount =
            4;

        ability.internalStateKnown =
            true;

        check(
            "AbilityContract",
            ability.valid &&
            NearlyEqual(
                ability.player.magicka.current,
                640.0f) &&
            ability.voidCount == 4 &&
            ability.internalStateKnown);

        CandidateSnapshot candidate{};

        candidate.valid = true;
        candidate.actorFormID = 0x300;
        candidate.currentMagicka = 900.0f;
        candidate.maxHealth = 5000.0f;
        candidate.distanceFt = 40.0f;
        candidate.alive = true;
        candidate.hostile = true;
        candidate.monster = true;
        candidate.lineOfSight = true;

        check(
            "AbilityCandidateContract",
            candidate.valid &&
            candidate.currentMagicka >
                0.0f &&
            candidate.maxHealth >
                0.0f &&
            candidate.distanceFt <=
                60.0f &&
            candidate.alive &&
            candidate.hostile &&
            candidate.monster &&
            candidate.lineOfSight);

        logger::info(
            "[SnapshotSelfTest] Overall = {}",
            allPass ?
                "PASS" :
                "FAIL");

        return allPass;
    }
}