#include "PCH.h"
#include "ClassRuntime.h"
#include "BuildState.h"
#include "TitleRuntime.h"

#include "DamageRules.h"
#include "Hunter.h"
#include "Inquisitor.h"
#include "Mage.h"
#include "Paladin.h"
#include "Priest.h"
#include "RE/T/TESDeathEvent.h"
#include "RE/S/SpellsLearned.h"
#include "Warrior.h"

#include <cmath>

namespace
{
    bool g_registered{};

    class CombatEventSink final :
        public RE::BSTEventSink<RE::TESCombatEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESCombatEvent* a_event,
            RE::BSTEventSource<RE::TESCombatEvent>*) override
        {
            if (!a_event) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* actorRef =
                a_event->actor.get();

            auto* targetRef =
                a_event->targetActor.get();

            auto* actor =
                actorRef ?
                    actorRef->As<RE::Actor>() :
                    nullptr;

            auto* target =
                targetRef ?
                    targetRef->As<RE::Actor>() :
                    nullptr;

            const auto state =
                a_event->newState.get();

            const bool enteredCombat =
                state ==
                    RE::ACTOR_COMBAT_STATE::kCombat;

            const bool leftCombat =
                state ==
                    RE::ACTOR_COMBAT_STATE::kNone;

            TULLBUNG::Classes::Warrior::
                OnCombatStateChanged(
                    actor,
                    target,
                    enteredCombat,
                    leftCombat);

            TULLBUNG::Classes::Hunter::
                OnCombatStateChanged(
                    actor,
                    target,
                    enteredCombat,
                    leftCombat);

            TULLBUNG::Classes::Inquisitor::
                OnCombatStateChanged(
                    actor,
                    target,
                    enteredCombat,
                    leftCombat);

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    CombatEventSink g_combatEventSink{};
}

namespace
{
    class DeathEventSink final :
        public RE::BSTEventSink<
            RE::TESDeathEvent>
    {
    public:
        RE::BSEventNotifyControl
            ProcessEvent(
                const RE::TESDeathEvent*
                    a_event,
                RE::BSTEventSource<
                    RE::TESDeathEvent>*)
                override
        {
            if (!a_event ||
                !a_event->dead) {

                return
                    RE::BSEventNotifyControl::
                        kContinue;
            }

            auto* dyingRef =
                a_event->
                    actorDying.get();

            auto* killerRef =
                a_event->
                    actorKiller.get();

            auto* dying =
                dyingRef ?
                    dyingRef->
                        As<RE::Actor>() :
                    nullptr;

            auto* killer =
                killerRef ?
                    killerRef->
                        As<RE::Actor>() :
                    nullptr;

            TULLBUNG::Classes::Hunter::
                OnDeathEvent(
                    dying,
                    killer);

            TULLBUNG::Classes::Inquisitor::
                OnDeathEvent(
                    dying,
                    killer);

            return
                RE::BSEventNotifyControl::
                    kContinue;
        }
    };

    DeathEventSink
        g_deathEventSink{};
}
namespace
{
    class SpellsLearnedEventSink final :
        public RE::BSTEventSink<
            RE::SpellsLearned::Event>
    {
    public:
        RE::BSEventNotifyControl
            ProcessEvent(
                const RE::SpellsLearned::Event*
                    a_event,
                RE::BSTEventSource<
                    RE::SpellsLearned::Event>*)
                override
        {
            if (!a_event ||
                !a_event->spell) {

                return
                    RE::BSEventNotifyControl::
                        kContinue;
            }

            // Runtime abilities / powers must not recursively
            // affect 마도학/대마도학자 learned-spell count.
            if (
                a_event->spell->
                    GetSpellType() !=
                RE::MagicSystem::
                    SpellType::kSpell
            ) {
                return
                    RE::BSEventNotifyControl::
                        kContinue;
            }

            TULLBUNG::Classes::Mage::
                OnSpellLearned(
                    a_event->spell);

            return
                RE::BSEventNotifyControl::
                    kContinue;
        }
    };

    SpellsLearnedEventSink
        g_spellsLearnedEventSink{};
}

namespace TULLBUNG::ClassRuntime
{
    bool Initialize()
    {
        if (g_registered) {
            return true;
        }

        auto* eventHolder =
            RE::ScriptEventSourceHolder::
                GetSingleton();

        if (!eventHolder) {
            logger::error(
                "[ClassRuntime] ScriptEventSourceHolder = MISSING");

            return false;
        }

        auto* spellsLearnedSource =
            RE::SpellsLearned::
                GetEventSource();

        if (!spellsLearnedSource) {
            logger::error(
                "[ClassRuntime] SpellsLearned event source = MISSING");

            return false;
        }
        if (!Classes::Warrior::
                Initialize()) {

            logger::error(
                "[ClassRuntime] Warrior initialization failed");

            return false;
        }

        if (!Classes::Hunter::
                Initialize()) {

            logger::error(
                "[ClassRuntime] Hunter initialization failed | Hunter disabled");
        }

        if (!Classes::Priest::
                Initialize()) {

            logger::error(
                "[ClassRuntime] Priest initialization failed | Priest disabled");
        }

        if (!Classes::Paladin::
                Initialize()) {

            logger::error(
                "[ClassRuntime] Paladin initialization failed | Paladin disabled");
        }

        if (!Classes::Mage::
                Initialize()) {

            logger::error(
                "[ClassRuntime] Mage family initialization failed | Mage family disabled");
        }

        if (!Classes::Inquisitor::
                Initialize()) {

            logger::error(
                "[ClassRuntime] Inquisitor initialization failed | Inquisitor disabled");
        }

        eventHolder->AddEventSink(
            &g_combatEventSink);
        eventHolder->AddEventSink(
            &g_deathEventSink);

        spellsLearnedSource->
            AddEventSink(
                &g_spellsLearnedEventSink);
        g_registered = true;

        // Synchronize current BuildState against the already cached
        // equipment/artifact state. PlayerState refresh events will
        // also resync later when those states change.
        OnBuildStateChanged();

        logger::info(
            "[ClassRuntime] TESCombatEvent sink = OK");

        return true;
    }

    void ResetTransientState() noexcept
    {
        Classes::Warrior::
            ResetTransientState();

        if (Classes::Hunter::
                IsAvailable()) {

            Classes::Hunter::
                ResetTransientState();
        }

        if (Classes::Priest::
                IsAvailable()) {

            Classes::Priest::
                ResetTransientState();
        }

        if (Classes::Paladin::
                IsAvailable()) {

            Classes::Paladin::
                ResetTransientState();
        }

        if (Classes::Mage::
                IsAvailable()) {

            Classes::Mage::
                ResetTransientState();
        }

        if (Classes::Inquisitor::
                IsAvailable()) {

            Classes::Inquisitor::
                ResetTransientState();
        }
    }

    void OnEquipmentChanged() noexcept
    {
        if (!g_registered) {
            return;
        }

        Classes::Warrior::
            OnEquipmentChanged();

        Classes::Hunter::
            OnEquipmentChanged();

        Classes::Priest::
            OnEquipmentChanged();

        Classes::Paladin::
            OnEquipmentChanged();
    }

    void OnArtifactStateChanged() noexcept
    {
        if (!g_registered) {
            return;
        }

        Classes::Inquisitor::
            SyncRuntimePassives();
    }

    void OnActorActionEvent(
        const SKSE::ActionEvent&
            a_event) noexcept
    {
        if (!g_registered ||
            !a_event.actor) {

            return;
        }

        Classes::Warrior::
            OnActorAction(
                a_event.actor,
                a_event.type.get() ==
                    SKSE::ActionEvent::Type::
                        kWeaponSwing);

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();



    }

    void OnPlayerShoutInput() noexcept
    {
        if (!g_registered) {
            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player) {
            return;
        }

        Classes::Mage::
            OnPlayerVoiceFire(
                player);
        Classes::Inquisitor::
            OnPlayerVoiceFire(
                player);
    }
    void OnPlayerCastFinalized(
        const Snapshot::CastSnapshot&
            a_snapshot) noexcept
    {
        if (!g_registered) {
            return;
        }

        Classes::Mage::
            OnPlayerCastFinalized(
                a_snapshot);
    }
    void OnPlayerCastingStateChanged(
        RE::Actor* a_actor) noexcept
    {
        if (!g_registered) {
            return;
        }

        Classes::Mage::
            OnPlayerCastingStateChanged(
                a_actor);
    }


    void OnPlayerMagickaMutation(
        float a_beforeCurrent,
        float a_beforeMaximum,
        float a_afterCurrent,
        float a_afterMaximum) noexcept
    {
        if (!g_registered) {
            return;
        }

        Classes::Mage::
            OnPlayerMagickaMutation(
                a_beforeCurrent,
                a_beforeMaximum,
                a_afterCurrent,
                a_afterMaximum);
    }


    void OnPlayerProjectileImpact(
        std::uint64_t a_actionID,
        RE::FormID a_sourceFormID,
        RE::TESObjectREFR* a_impactRef,
        const RE::NiPoint3& a_targetLoc,
        const Snapshot::ActionSnapshot&
            a_snapshot) noexcept
    {
        if (!g_registered) {
            return;
        }

        Classes::Mage::
            OnPlayerProjectileImpact(
                a_actionID,
                a_sourceFormID,
                a_impactRef,
                a_targetLoc,
                a_snapshot);
    }
    void PopulateActionExtendedState(
        RE::Actor* a_actor,
        Snapshot::ExtendedActionState&
            a_state) noexcept
    {
        if (!g_registered) {
            return;
        }

        if (a_state.priestStanceKnown) {
            a_state.priestStance =
                a_state.priestStance &&
                TULLBUNG::BuildState::
                    IsNodeActive(
                        TULLBUNG::BuildState::
                            NodeID::
                                kPriestStance);
        }

        Classes::Mage::
            PopulateExtendedActionState(
                a_actor,
                a_state);
    }

    void OnPlayerSprintInput(
        bool a_pressed) noexcept
    {
        if (!g_registered) {
            return;
        }

        Classes::Warrior::
            OnPlayerSprintInput(
                a_pressed);
    }

    void OnPlayerMovementStateChanged(
        bool a_moving) noexcept
    {
        if (!g_registered) {
            return;
        }

        Classes::Hunter::
            OnPlayerMovementStateChanged(
                a_moving);

        Classes::Inquisitor::
            OnPlayerMovementStateChanged(
                a_moving);
    }

    void OnPlayerActionStart(
        const Combat::AttackActionState&
            a_action) noexcept
    {
        Classes::Warrior::
            OnPlayerActionStart(
                a_action);

        Classes::Hunter::
            OnPlayerActionStart(
                a_action);
    }

    void OnPlayerRangedLaunch(
        std::uint64_t a_actionID,
        RE::TESObjectWEAP* a_weapon,
        float a_x,
        float a_y,
        float a_z) noexcept
    {
        Classes::Hunter::
            OnPlayerRangedLaunch(
                a_actionID,
                a_weapon,
                a_x,
                a_y,
                a_z);
    }

    void PopulateHitState(
        RE::Actor* a_target,
        Snapshot::HitStateInput&
            a_hitState) noexcept
    {
        Classes::Hunter::
            PopulateHitState(
                a_target,
                a_hitState);
    }

    void OnPlayerWeaponDirectHit(
        const Combat::CombatContext&
            a_context,
        RE::HitData& a_data) noexcept
    {
        if (a_context.playerAttacker &&
            a_context.weaponDirectCandidate &&
            a_context.directDamageKnown) {

            DamageRules::
                DirectDamageAccumulator
                    accumulator{};

            Classes::Hunter::
                AccumulateWeaponDirectDamage(
                    a_context,
                    accumulator);

            Classes::Priest::
                AccumulateWeaponDirectDamage(
                    a_context,
                    accumulator);

            Classes::Paladin::
                AccumulateWeaponDirectDamage(
                    a_context,
                    accumulator);

            Classes::Inquisitor::
                AccumulateWeaponDirectDamage(
                    a_context,
                    accumulator);

            const float resolvedDamage =
                accumulator.Resolve(
                    DamageRules::
                        DirectChannel::kWeapon,
                    a_context.directDamage);

            if (std::isfinite(
                    resolvedDamage)) {

                a_data.totalDamage =
                    resolvedDamage;
            }
        }

        Classes::Warrior::
            OnPlayerWeaponDirectHit(
                a_context,
                a_data);

        Classes::Mage::
            OnPlayerWeaponDirectHit(
                a_context,
                a_data);

        Classes::Hunter::
            OnPlayerWeaponDirectHit(
                a_context,
                a_data);

        Classes::Paladin::
            OnPlayerWeaponDirectHit(
                a_context,
                a_data);
    }

    void OnPlayerWeaponDirectHitPost(
        std::uint64_t a_actionID,
        RE::Actor* a_target) noexcept
    {
        Classes::Hunter::
            OnPlayerWeaponDirectHitPost(
                a_actionID,
                a_target);
    }

    void ApplyPlayerIncomingDamageRules(
        RE::Actor* a_aggressor,
        RE::HitData& a_data,
        const Snapshot::ActorResources&
            a_playerBefore) noexcept
    {
        DamageRules::
            ConditionalDRAccumulator
                accumulator{};

        Classes::Hunter::
            AccumulateIncomingDirectDR(
                a_aggressor,
                a_data,
                a_playerBefore,
                accumulator);

        Classes::Inquisitor::
            AccumulateIncomingDirectDR(
                a_aggressor,
                a_data,
                a_playerBefore,
                accumulator);

        const float resolvedDamage =
            accumulator.Apply(
                a_data.totalDamage);

        if (std::isfinite(
                resolvedDamage)) {

            a_data.totalDamage =
                resolvedDamage;
        }
    }

    void OnPlayerHitEvent(
        const Combat::CombatContext&
            a_context) noexcept
    {
        Classes::Warrior::
            OnPlayerHitEvent(
                a_context);
    }

    void OnPlayerBashHitData(
        const Combat::CombatContext&
            a_context,
        RE::HitData& a_data) noexcept
    {
        Classes::Warrior::
            OnPlayerBashHitData(
                a_context,
                a_data);
    }

    void OnPlayerIncomingHit(
        RE::Actor* a_aggressor,
        const RE::HitData& a_data,
        const Snapshot::ActorResources&
            a_playerBefore) noexcept
    {
        Classes::Warrior::
            OnPlayerIncomingHit(
                a_aggressor,
                a_data,
                a_playerBefore);

        Classes::Hunter::
            OnPlayerIncomingHit(
                a_aggressor,
                a_data,
                a_playerBefore);
    }
}

namespace TULLBUNG::ClassRuntime
{
    float ResolvePlayerSpellDirectBonusPercent(
        const RE::SpellItem* a_spell,
        RE::Actor* a_target,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot) noexcept
    {
        // Stage14 Spell Impact Authority has already resolved
        // the real target and immutable ActionSnapshot before
        // this function is entered.
        Classes::Mage::
            OnPlayerSpellDirectHit(
                a_spell,
                a_target,
                a_hand,
                a_snapshot);

        DamageRules::DirectDamageAccumulator accumulator{};
        const float siegeAdditive =
            Classes::Mage::
                ResolveSiegeSpellDirectBonusPercent(
                    a_spell,
                    a_target,
                    a_snapshot);

        if (
            std::isfinite(
                siegeAdditive)
        ) {
            accumulator.
                spellAdditivePercent +=
                    siegeAdditive;
        }

        Classes::Priest::
            AccumulateSpellDirectDamage(
                a_spell,
                a_hand,
                a_snapshot,
                accumulator);

        Classes::Inquisitor::
            AccumulateSpellDirectDamage(
                a_spell,
                a_target,
                accumulator);

        return
            accumulator.
                spellAdditivePercent;
    }

    float ResolvePlayerIncomingElementalDamageMultiplier(
        RE::Actor* a_caster,
        RE::Actor* a_target,
        const RE::EffectSetting* a_effect) noexcept
    {
        DamageRules::
            ConditionalDRAccumulator
                accumulator{};

        Classes::Inquisitor::
            AccumulateIncomingElementalDR(
                a_caster,
                a_target,
                a_effect,
                accumulator);

        return
            accumulator.
                ResolveMultiplier();
    }

    float ResolvePlayerHealingBonusPercent(
        const RE::SpellItem* a_spell,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot) noexcept
    {
        return
            Classes::Priest::
                ResolveHealingBonusPercent(
                    a_spell,
                    a_hand,
                    a_snapshot) +
            Classes::Paladin::
                ResolveHealingBonusPercent(
                    a_spell,
                    a_hand,
                    a_snapshot);
    }
}

namespace TULLBUNG::ClassRuntime
{
    void OnBuildStateChanged() noexcept
    {
        if (!g_registered) {
            return;
        }

        Classes::Priest::
            SyncRuntimePassives();

        Classes::Paladin::
            SyncRuntimePassives();

        Classes::Inquisitor::
            SyncRuntimePassives();

        Classes::Mage::
            SyncRuntimePassives();

        (void)TULLBUNG::TitleRuntime::
            RefreshFromBuildState();
    }
}
