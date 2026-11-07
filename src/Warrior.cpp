#include "PCH.h"
#include "Warrior.h"

#include "BuildState.h"
#include "Classification.h"
#include "DataCache.h"
#include "DamageRules.h"
#include "PlayerState.h"

#include "RE/G/GameSettingCollection.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using NodeID =
        TULLBUNG::BuildState::NodeID;

    using SpellID =
        TULLBUNG::DataCache::WarriorSpellID;


    using PerkID =
        TULLBUNG::DataCache::WarriorPerkID;

    using WC =
        TULLBUNG::Classification::WeaponClass;

    std::unordered_set<
        RE::FormID>
        g_participants{};

    std::unordered_map<
        RE::FormID,
        std::uint64_t>
        g_enemyActionGeneration{};

    std::unordered_map<
        RE::FormID,
        std::uint64_t>
        g_ironWallConsumedGeneration{};


struct ActionRuntime final
{
    bool warMachineArmed{};
    bool hammerAnvilArmed{};
    bool warMachineRewarded{};
    bool hammerAnvilApplied{};
};

std::unordered_map<
    std::uint64_t,
    ActionRuntime>
    g_actionRuntime{};

std::uint64_t
    g_lastBashGameplayActionID{};

struct BattleSprintRuntime final
{
    RE::Setting* setting{};
    float original{};
    float applied{};
    bool active{};
};

BattleSprintRuntime
    g_battleSprint{};

    bool IsMeleeClass(
        WC a_class) noexcept
    {
        switch (a_class) {
        case WC::kOneHandSword:
        case WC::kOneHandDagger:
        case WC::kOneHandAxe:
        case WC::kOneHandMace:
        case WC::kTwoHandSword:
        case WC::kTwoHandAxe:
            return true;

        default:
            return false;
        }
    }

    bool IsTwoHandedClass(
        WC a_class) noexcept
    {
        return
            a_class == WC::kTwoHandSword ||
            a_class == WC::kTwoHandAxe;
    }

    bool HasMelee(
        const TULLBUNG::PlayerState::
            EquipmentState& a_equipment) noexcept
    {
        return
            IsMeleeClass(
                a_equipment.right.weaponClass) ||
            IsMeleeClass(
                a_equipment.left.weaponClass);
    }

    bool HasTwoHanded(
        const TULLBUNG::PlayerState::
            EquipmentState& a_equipment) noexcept
    {
        return
            IsTwoHandedClass(
                a_equipment.right.weaponClass) ||
            IsTwoHandedClass(
                a_equipment.left.weaponClass);
    }

    bool NodeActive(
        NodeID a_node) noexcept
    {
        return
            TULLBUNG::BuildState::
                IsNodeActive(
                    a_node);
    }

    RE::PlayerCharacter* Player() noexcept
    {
        return
            RE::PlayerCharacter::
                GetSingleton();
    }


float ApplyCurrentValueDelta(
    RE::Actor* a_actor,
    RE::ActorValue a_actorValue,
    float a_delta) noexcept
{
    if (!a_actor ||
        a_delta == 0.0f) {

        return 0.0f;
    }

    auto* owner =
        a_actor->AsActorValueOwner();

    if (!owner) {
        return 0.0f;
    }

    const auto before =
        TULLBUNG::Snapshot::
            CaptureResource(
                a_actor,
                a_actorValue);

    float applied{};

    if (a_delta < 0.0f) {
        applied =
            (std::min)(
                before.current,
                -a_delta);

        if (applied <= 0.0f) {
            return 0.0f;
        }

        owner->ModActorValue(
            RE::ACTOR_VALUE_MODIFIERS::
                kDamage,
            a_actorValue,
            -applied);

        return -applied;
    }

    const float missing =
        (std::max)(
            0.0f,
            before.maximum -
                before.current);

    applied =
        (std::min)(
            missing,
            a_delta);

    if (applied <= 0.0f) {
        return 0.0f;
    }

    owner->ModActorValue(
        RE::ACTOR_VALUE_MODIFIERS::
            kDamage,
        a_actorValue,
        applied);

    return applied;
}

float PayStaminaCost(
    RE::PlayerCharacter* a_player,
    float a_requested) noexcept
{
    if (!a_player ||
        a_requested <= 0.0f) {

        return 0.0f;
    }

    return
        -(std::min)(
            0.0f,
            ApplyCurrentValueDelta(
                a_player,
                RE::ActorValue::kStamina,
                -a_requested));
}

float RestoreStamina(
    RE::PlayerCharacter* a_player,
    float a_requested) noexcept
{
    if (!a_player ||
        a_requested <= 0.0f) {

        return 0.0f;
    }

    return
        (std::max)(
            0.0f,
            ApplyCurrentValueDelta(
                a_player,
                RE::ActorValue::kStamina,
                a_requested));
}

bool PayHealthCostNoKill(
    RE::PlayerCharacter* a_player,
    float a_requested,
    float& a_applied) noexcept
{
    a_applied = 0.0f;

    if (!a_player ||
        a_requested <= 0.0f) {

        return false;
    }

    const auto current =
        TULLBUNG::Snapshot::
            CaptureResource(
                a_player,
                RE::ActorValue::kHealth);

    // Locked Berserker rule:
    // the cost itself must never kill the player.
    if (current.current <=
        a_requested) {

        return false;
    }

    a_applied =
        -(std::min)(
            0.0f,
            ApplyCurrentValueDelta(
                a_player,
                RE::ActorValue::kHealth,
                -a_requested));

    return
        a_applied + 0.001f >=
        a_requested;
}


bool ApplySecondary(
    RE::PlayerCharacter* a_player,
    RE::Actor* a_target,
    float a_resolvedDamage,
    std::string_view a_name)
{
    const bool applied =
        TULLBUNG::DamageRules::
            ApplyResolvedHealthDamage(
                a_player,
                a_target,
                a_resolvedDamage,
                TULLBUNG::DamageRules::
                    FirewallOrigin::
                        kSecondaryDamage);

    logger::info(
        "[S12SECONDARY] {} | Target={:08X} | Resolved={:.3f} | Applied={}",
        a_name,
        a_target ?
            a_target->GetFormID() :
            0,
        a_resolvedDamage,
        applied);

    return applied;
}

bool CastHammerAnvilBuff(
    RE::PlayerCharacter* a_player,
    std::string_view a_source)
{
    if (!a_player) {
        return false;
    }

    auto* spell =
        TULLBUNG::DataCache::
            GetWarriorSpell(
                SpellID::kHammerAnvil);

    auto* caster =
        a_player->GetMagicCaster(
            RE::MagicSystem::
                CastingSource::kInstant);

    if (!spell ||
        !caster) {

        logger::error(
            "[S12HAMMER] Buff cast failed | Source={} | Spell={} | Caster={}",
            a_source,
            spell != nullptr,
            caster != nullptr);

        return false;
    }


    caster->CastSpellImmediate(
        spell,
        false,
        a_player,
        1.0f,
        false,
        50.0f,
        a_player);


    logger::info(
        "[S12HAMMER] Buff cast | Source={} | Armor=50 | Duration=3",
        a_source);

    return true;
}

void PruneActionRuntime(
    std::uint64_t a_currentActionID)
{
    if (a_currentActionID <= 64) {
        return;
    }

    const auto minimum =
        a_currentActionID - 64;

    for (auto it =
             g_actionRuntime.begin();
         it !=
             g_actionRuntime.end();) {

        if (it->first <
            minimum) {

            it =
                g_actionRuntime.erase(
                    it);
        } else {
            ++it;
        }
    }
}

    bool BattleSprintEligible() noexcept
    {
        if (!NodeActive(
                NodeID::kWarriorBattleSprint)) {

            return false;
        }

        return
            HasMelee(
                TULLBUNG::PlayerState::
                    GetEquipment());
    }

    RE::Setting* GetSprintDrainSetting() noexcept
    {
        auto* collection =
            RE::GameSettingCollection::
                GetSingleton();

        if (!collection) {
            return nullptr;
        }

        return
            collection->GetSetting(
                "fSprintStaminaDrainMult");
    }

    void RestoreBattleSprintSetting(
        std::string_view a_reason) noexcept
    {
        if (!g_battleSprint.active) {
            return;
        }

        auto* setting =
            g_battleSprint.setting;

        if (!setting) {
            logger::warn(
                "[S12SPRINT] RESTORE skipped | Reason={} | Setting=MISSING",
                a_reason);

            g_battleSprint = {};
            return;
        }

        const float current =
            setting->GetFloat();

        const float tolerance =
            (std::max)(
                0.001f,
                std::abs(
                    g_battleSprint.applied) *
                    0.001f);

        if (std::abs(
                current -
                g_battleSprint.applied) <=
            tolerance) {

            setting->SetFloat(
                g_battleSprint.original);

            logger::info(
                "[S12SPRINT] RESTORE | Reason={} | Current={:.6f} | Restored={:.6f}",
                a_reason,
                current,
                g_battleSprint.original);
        } else {
            // Another runtime changed the GMST while our temporary
            // override was active. Do not clobber its newer value.
            logger::warn(
                "[S12SPRINT] RESTORE CONFLICT | Reason={} | Current={:.6f} | ExpectedApplied={:.6f} | Original={:.6f} | Action=LEAVE_CURRENT",
                a_reason,
                current,
                g_battleSprint.applied,
                g_battleSprint.original);
        }

        g_battleSprint = {};
    }

    void ApplyBattleSprintSetting() noexcept
    {
        if (g_battleSprint.active) {
            return;
        }

        if (!BattleSprintEligible()) {
            logger::info(
                "[S12SPRINT] START ignored | Eligible=false");

            return;
        }

        auto* setting =
            GetSprintDrainSetting();

        if (!setting) {
            logger::error(
                "[S12SPRINT] fSprintStaminaDrainMult = MISSING");

            return;
        }

        const float original =
            setting->GetFloat();

        const float applied =
            original * 0.5f;

        setting->SetFloat(
            applied);

        g_battleSprint.setting =
            setting;
        g_battleSprint.original =
            original;
        g_battleSprint.applied =
            applied;
        g_battleSprint.active =
            true;

        logger::info(
            "[S12SPRINT] APPLY | Original={:.6f} | Applied={:.6f} | Multiplier=0.500",
            original,
            applied);
    }

    void ValidateBattleSprintSetting() noexcept
    {
        if (g_battleSprint.active &&
            !BattleSprintEligible()) {

            RestoreBattleSprintSetting(
                "ELIGIBILITY_LOST");
        }
    }

    bool SetPerk(
        RE::PlayerCharacter* a_player,
        PerkID a_id,
        bool a_enable,
        std::string_view a_name)
    {
        if (!a_player) {
            return false;
        }

        auto* perk =
            TULLBUNG::DataCache::
                GetWarriorPerk(
                    a_id);

        if (!perk) {
            logger::error(
                "[S12PERK] {} = MISSING",
                a_name);

            return false;
        }

        const bool hasPerk =
            a_player->HasPerk(
                perk);

        if (a_enable &&
            !hasPerk) {

            a_player->AddPerk(
                perk);

            logger::info(
                "[S12PERK] {} = ON",
                a_name);

            return true;
        }

        if (!a_enable &&
            hasPerk) {

            a_player->RemovePerk(
                perk);

            logger::info(
                "[S12PERK] {} = OFF",
                a_name);

            return true;
        }

        return false;
    }

    void RefreshArmorAfterShieldMasteryChange(
        RE::PlayerCharacter* a_player,
        std::string_view a_reason)
    {
        if (!a_player) {
            return;
        }

        // ShieldMastery is a PERK entry-point effect. The engine does not
        // rebuild the perk-adjusted armor AV from AddPerk/RemovePerk alone.
        // Keep the validated +1 temporary AV nudge across one SKSE task,
        // then release it with -1.
        a_player->WornArmorChanged();
        a_player->OnArmorActorValueChanged();

        auto* owner =
            a_player->AsActorValueOwner();

        if (!owner) {
            logger::warn(
                "[S12PERK] ShieldMastery armor refresh skipped | Reason={} | ActorValueOwner=MISSING",
                a_reason);
            return;
        }

        owner->ModActorValue(
            RE::ACTOR_VALUE_MODIFIERS::kTemporary,
            RE::ActorValue::kDamageResist,
            1.0f);

        auto* taskInterface =
            SKSE::GetTaskInterface();

        if (!taskInterface) {
            // Never leave the temporary +1 applied if task dispatch is
            // unavailable. This is only a failure-path rollback.
            owner->ModActorValue(
                RE::ACTOR_VALUE_MODIFIERS::kTemporary,
                RE::ActorValue::kDamageResist,
                -1.0f);

            logger::warn(
                "[S12PERK] ShieldMastery armor refresh task unavailable | Reason={} | NudgeRolledBack=true",
                a_reason);
            return;
        }

        taskInterface->AddTask(
            []() {
                auto* player =
                    RE::PlayerCharacter::GetSingleton();

                if (!player) {
                    return;
                }

                if (auto* owner =
                        player->AsActorValueOwner()) {

                    owner->ModActorValue(
                        RE::ACTOR_VALUE_MODIFIERS::kTemporary,
                        RE::ActorValue::kDamageResist,
                        -1.0f);
                }
            });
    }

    void SyncPerks()
    {
        if (!TULLBUNG::DataCache::
                IsWarriorReady()) {

            return;
        }

        auto* player = Player();

        if (!player) {
            return;
        }

        const bool changed =
            SetPerk(
                player,
                PerkID::kShieldMastery,
                NodeActive(
                    NodeID::kGuardianShieldMastery),
                "ShieldMastery");

        if (changed) {
            RefreshArmorAfterShieldMasteryChange(
                player,
                "SYNC");
        }
    }

    void RemoveRuntimePerks()
    {
        auto* player = Player();

        if (!player ||
            !TULLBUNG::DataCache::
                IsWarriorReady()) {

            return;
        }

        const bool changed =
            SetPerk(
                player,
                PerkID::kShieldMastery,
                false,
                "ShieldMastery");

        if (changed) {
            RefreshArmorAfterShieldMasteryChange(
                player,
                "REMOVE_RUNTIME");
        }
    }

    void SetAbility(
        RE::PlayerCharacter* a_player,
        SpellID a_id,
        bool a_enable,
        std::string_view a_name)
    {
        if (!a_player) {
            return;
        }

        auto* spell =
            TULLBUNG::DataCache::
                GetWarriorSpell(
                    a_id);

        if (!spell) {
            logger::error(
                "[S12ABILITY] {} = MISSING",
                a_name);

            return;
        }

        const bool hasSpell =
            a_player->HasSpell(
                spell);

        if (a_enable &&
            !hasSpell) {

            a_player->AddSpell(
                spell);

            logger::info(
                "[S12ABILITY] {} = ON",
                a_name);

        } else if (
            !a_enable &&
            hasSpell) {

            a_player->RemoveSpell(
                spell);

            logger::info(
                "[S12ABILITY] {} = OFF",
                a_name);
        }
    }

    void SyncStruggle(
        RE::PlayerCharacter* a_player)
    {
        constexpr std::array<
            SpellID,
            5>
            spells{{
                SpellID::kStruggle1,
                SpellID::kStruggle2,
                SpellID::kStruggle3,
                SpellID::kStruggle4,
                SpellID::kStruggle5
            }};

        const std::size_t tier =
            NodeActive(
                NodeID::
                    kBerserkerFightingSpirit) ?
                (std::min<std::size_t>)(
                    g_participants.size(),
                    5) :
                0;

        for (std::size_t i = 0;
             i < spells.size();
             ++i) {

            SetAbility(
                a_player,
                spells[i],
                tier > 0 &&
                    i == tier - 1,
                "Struggle");
        }

        logger::info(
            "[S12PART] Participants={} | AppliedTier={}",
            g_participants.size(),
            tier);
    }

    void SyncCombatBreath(
        RE::PlayerCharacter* a_player,
        bool a_melee)
    {
        if (!a_player) {
            return;
        }

        SetAbility(
            a_player,
            SpellID::kCombatBreath,
            NodeActive(
                NodeID::
                    kWarriorCombatBreathing) &&
                a_melee &&
                a_player->IsInCombat(),
            "CombatBreath");
    }

    void SyncAbilities()
    {
        if (!TULLBUNG::DataCache::
                IsWarriorReady()) {

            return;
        }

        auto* player = Player();

        if (!player) {
            return;
        }

        const auto& equipment =
            TULLBUNG::PlayerState::
                GetEquipment();

        const bool melee =
            HasMelee(
                equipment);

        const bool twoHanded =
            HasTwoHanded(
                equipment);

        SetAbility(
            player,
            SpellID::kWarriorVigor,
            NodeActive(
                NodeID::kWarriorVigor) &&
                melee,
            "WarriorVigor");

        SyncCombatBreath(player, melee);

        // <=25% Stamina is handled by the MGEF condition.
        SetAbility(
            player,
            SpellID::kAdrenaline,
            NodeActive(
                NodeID::
                    kWarriorAdrenaline) &&
                melee,
            "Adrenaline");

        SetAbility(
            player,
            SpellID::kTwoHandMastery,
            NodeActive(
                NodeID::
                    kBerserkerTwoHandedMastery) &&
                twoHanded,
            "TwoHandMastery");

        // <=50% Health is handled by the MGEF condition.
        SetAbility(
            player,
            SpellID::kSurvivalDesire,
            NodeActive(
                NodeID::
                    kBerserkerSurvivalInstinct),
            "SurvivalDesire");

        SyncStruggle(
            player);
    }

    void RemoveRuntimeAbilities()
    {
        auto* player = Player();

        if (!player ||
            !TULLBUNG::DataCache::
                IsWarriorReady()) {

            return;
        }

        SetAbility(
            player,
            SpellID::kCombatBreath,
            false,
            "CombatBreath");

        SetAbility(
            player,
            SpellID::kWarriorVigor,
            false,
            "WarriorVigor");

        SetAbility(
            player,
            SpellID::kAdrenaline,
            false,
            "Adrenaline");

        SetAbility(
            player,
            SpellID::kTwoHandMastery,
            false,
            "TwoHandMastery");

        SetAbility(
            player,
            SpellID::kSurvivalDesire,
            false,
            "SurvivalDesire");

        constexpr std::array<
            SpellID,
            5>
            struggle{{
                SpellID::kStruggle1,
                SpellID::kStruggle2,
                SpellID::kStruggle3,
                SpellID::kStruggle4,
                SpellID::kStruggle5
            }};

        for (const auto id :
             struggle) {

            SetAbility(
                player,
                id,
                false,
                "Struggle");
        }
    }

    void RefreshRuntimeState()
    {
        SyncAbilities();
        SyncPerks();
        ValidateBattleSprintSetting();
    }

    void AddParticipant(
        RE::Actor* a_actor,
        std::string_view a_source)
    {
        auto* player =
            Player();

        if (!a_actor ||
            !player ||
            a_actor == player) {

            return;
        }

        const auto id =
            a_actor->GetFormID();

        if (g_participants.
                insert(id).
                second) {

            logger::info(
                "[S12PART] ADD | Actor={:08X} | Source={} | Count={}",
                id,
                a_source,
                g_participants.size());

            SyncStruggle(
                player);
        }
    }

    void RemoveParticipant(
        RE::Actor* a_actor,
        std::string_view a_source)
    {
        if (!a_actor) {
            return;
        }

        const auto id =
            a_actor->GetFormID();

        const auto erased =
            g_participants.erase(
                id);

        g_enemyActionGeneration.
            erase(id);

        g_ironWallConsumedGeneration.
            erase(id);

        if (erased > 0) {
            logger::info(
                "[S12PART] REMOVE | Actor={:08X} | Source={} | Count={}",
                id,
                a_source,
                g_participants.size());

            SyncStruggle(
                Player());
        }
    }
}

namespace TULLBUNG::Classes::Warrior
{
    bool Initialize() noexcept
    {
        if (!IsAvailable()) {
            logger::error(
                "[Stage12] Warrior runtime unavailable | DataCache Warrior forms incomplete");

            return false;
        }

        logger::info(
            "[Stage12] Warrior runtime = READY");

        ResetTransientState();

        return true;
    }

    bool IsAvailable() noexcept
    {
        return
            DataCache::
                IsWarriorReady();
    }

    void ResetTransientState() noexcept
    {
        RestoreBattleSprintSetting(
            "RESET");

        RemoveRuntimeAbilities();
        RemoveRuntimePerks();


        g_participants.clear();
        g_enemyActionGeneration.clear();
        g_ironWallConsumedGeneration.clear();
        g_actionRuntime.clear();

        g_lastBashGameplayActionID = 0;


        logger::info(
            "[Stage12] Transient state reset");

        RefreshRuntimeState();
    }

    void OnEquipmentChanged() noexcept
    {
        if (IsAvailable()) {
            RefreshRuntimeState();
        }
    }

    void OnCombatStateChanged(
        RE::Actor* a_actor,
        RE::Actor* a_target,
        bool a_enteredCombat,
        bool a_leftCombat) noexcept
    {
        if (!IsAvailable()) {
            return;
        }

        auto* player =
            Player();

        if (!player) {
            return;
        }

        if (a_actor == player) {
            logger::info(
                "[S12COMBAT] Player | Entered={} | Left={} | IsInCombat={}",
                a_enteredCombat,
                a_leftCombat,
                player->IsInCombat());

            SyncAbilities();

            return;
        }

        if (!a_actor) {
            return;
        }

        const bool playerRelevantTransition =
            (a_enteredCombat &&
             a_target == player) ||
            (a_leftCombat &&
             g_participants.contains(
                 a_actor->GetFormID()));

        if (a_enteredCombat &&
            a_target == player) {

            AddParticipant(
                a_actor,
                "TESCombatEvent");
        }

        if (a_leftCombat) {
            RemoveParticipant(
                a_actor,
                "TESCombatEvent");
        }

        if (playerRelevantTransition) {
            SyncCombatBreath(
                player,
                HasMelee(
                    TULLBUNG::PlayerState::
                        GetEquipment()));
        }
    }

    void OnActorAction(
        RE::Actor* a_actor,
        bool a_weaponSwing) noexcept
    {
        if (!IsAvailable() ||
            !a_actor ||
            !a_weaponSwing) {

            return;
        }

        auto* player =
            Player();

        if (!player ||
            a_actor == player) {

            return;
        }

        const auto id =
            a_actor->GetFormID();

        auto& generation =
            g_enemyActionGeneration[
                id];

        ++generation;

    }


    void OnPlayerSprintInput(
        bool a_pressed) noexcept
    {
        if (!IsAvailable()) {
            return;
        }

        if (a_pressed) {
            ApplyBattleSprintSetting();
        } else {
            RestoreBattleSprintSetting(
                "INPUT_UP");
        }
    }

void OnPlayerActionStart(
    const Combat::AttackActionState&
        a_action) noexcept
{
    if (!IsAvailable() ||
        !a_action.actor) {

        return;
    }

    auto* player =
        Player();

    if (!player ||
        a_action.actor != player ||
        a_action.sourceKind !=
            Combat::SourceKind::kWeapon) {

        return;
    }

    const bool isPowerAttack =
        player->IsPowerAttacking();

    if (!a_action.actionID ||
        !isPowerAttack) {

        return;
    }

    auto* weapon =
        a_action.sourceForm ?
            a_action.sourceForm->
                As<RE::TESObjectWEAP>() :
            nullptr;

    if (!weapon ||
        !TULLBUNG::Classification::
            IsMelee(
                weapon)) {

        return;
    }

    PruneActionRuntime(
        a_action.actionID);

    const auto [it, inserted] =
        g_actionRuntime.
            try_emplace(
                a_action.actionID);

    if (!inserted) {
        logger::warn(
            "[S12COST] Duplicate ActionStart ignored | ActionID={}",
            a_action.actionID);

        return;
    }

    auto& state =
        it->second;

    state.warMachineArmed =
        NodeActive(
            NodeID::kWarriorMaster);

    state.hammerAnvilArmed =
        NodeActive(
            NodeID::
                kGuardianHammerAndAnvil) &&
        a_action.actionSnapshot.
            shieldEquipped;

    float requested{};

    if (state.warMachineArmed) {
        requested += 30.0f;
    }

    if (state.hammerAnvilArmed) {
        requested += 50.0f;
    }

    if (requested <= 0.0f) {
        return;
    }

    const float applied =
        PayStaminaCost(
            player,
            requested);

    logger::info(
        "[S12COST] ActionID={} | Power=true | WarMachine={} | HammerAnvil={} | Requested={:.2f} | Applied={:.2f}",
        a_action.actionID,
        state.warMachineArmed,
        state.hammerAnvilArmed,
        requested,
        applied);
}


void OnPlayerWeaponDirectHit(
    const Combat::CombatContext&
        a_context,
    RE::HitData& a_data) noexcept
{
    if (!IsAvailable()) {
        return;
    }

    (void)a_data;

    if (!a_context.firstValidDirectHit ||
        !a_context.target ||
        a_context.range !=
            Combat::RangeKind::kMelee) {

        return;
    }

    auto* player =
        Player();

    if (!player) {
        return;
    }

    // ----------------------------------------------------
    // Warrior Master: War Machine
    // Action-start cost was paid even on a miss.
    // First valid hit restores 50 Stamina.
    // ----------------------------------------------------

    if (const auto it =
            g_actionRuntime.find(
                a_context.attackActionID);
        it !=
            g_actionRuntime.end()) {

        auto& state =
            it->second;

        if (state.warMachineArmed &&
            !state.warMachineRewarded) {

            const float restored =
                RestoreStamina(
                    player,
                    50.0f);

            state.warMachineRewarded =
                true;

            logger::info(
                "[S12WAR_MACHINE] ActionID={} | RestoreRequested=50.00 | RestoreApplied={:.2f}",
                a_context.attackActionID,
                restored);
        }
    }

    // ----------------------------------------------------
    // Berserker Blood Strike / Blood Smash
    // The health cost is paid only on the first valid
    // direct melee hit and can never kill the player.
    // Low-health Master uses the pre-cost ActionSnapshot.
    // ----------------------------------------------------

    const bool normalAttack =
        a_context.attackKind ==
            Combat::AttackKind::kNormal;

    const bool powerAttack =
        a_context.attackKind ==
            Combat::AttackKind::kPower;

    const bool bloodStrike =
        normalAttack &&
        NodeActive(
            NodeID::
                kBerserkerBloodStrike);

    const bool bloodSmash =
        powerAttack &&
        NodeActive(
            NodeID::
                kBerserkerBloodSmash);

    if (bloodStrike ||
        bloodSmash) {

        const auto snapshotHealth =
            a_context.actionSnapshot.
                player.health;

        const auto liveHealth =
            TULLBUNG::Snapshot::
                CaptureResource(
                    player,
                    RE::ActorValue::kHealth);

        const float maxHealth =
            snapshotHealth.maximum > 0.0f ?
                snapshotHealth.maximum :
                liveHealth.maximum;

        const float preCostCurrent =
            snapshotHealth.maximum > 0.0f ?
                snapshotHealth.current :
                liveHealth.current;

        const bool lowHealth =
            maxHealth > 0.0f &&
            preCostCurrent <=
                maxHealth * 0.50f;

        const bool master =
            NodeActive(
                NodeID::
                    kBerserkerMaster);

        const float costPercent =
            bloodSmash ?
                5.0f :
                1.0f;

        float damagePercent =
            bloodSmash ?
                10.0f :
                2.0f;

        if (master &&
            lowHealth) {

            damagePercent =
                bloodSmash ?
                    15.0f :
                    3.0f;
        }

        const float cost =
            maxHealth *
            costPercent /
            100.0f;

        float costApplied{};

        const bool paid =
            PayHealthCostNoKill(
                player,
                cost,
                costApplied);

        if (!paid) {
            logger::info(
                "[S12BLOOD] Kind={} | ActionID={} | LowHealth={} | MaxHP={:.2f} | Cost={:.2f} | CanPay=false | Secondary=0",
                bloodSmash ?
                    "SMASH" :
                    "STRIKE",
                a_context.attackActionID,
                lowHealth,
                maxHealth,
                cost);

        } else {
            const float reference =
                (std::min)(
                    maxHealth,
                    2000.0f);

            const float secondary =
                reference *
                damagePercent /
                100.0f;

            const bool applied =
                ApplySecondary(
                    player,
                    a_context.target,
                    secondary,
                    bloodSmash ?
                        "BloodSmash" :
                        "BloodStrike");

            logger::info(
                "[S12BLOOD] Kind={} | ActionID={} | PreCostHP={:.2f}/{:.2f} | LowHealth={} | CostRequested={:.2f} | CostApplied={:.2f} | DamageReference={:.2f} | DamagePercent={:.2f} | Secondary={:.2f} | Applied={}",
                bloodSmash ?
                    "SMASH" :
                    "STRIKE",
                a_context.attackActionID,
                preCostCurrent,
                maxHealth,
                lowHealth,
                cost,
                costApplied,
                reference,
                damagePercent,
                secondary,
                applied);
        }
    }

    // ----------------------------------------------------
    // Guardian: Best Defense
    // Uses the pre-buff ActionSnapshot Armor. Physical
    // resistance is resolved independently from HitData
    // through the common DamageRules resolver.
    // ----------------------------------------------------

    if (normalAttack &&
        NodeActive(
            NodeID::
                kGuardianBestDefense) &&
        a_context.actionSnapshot.
            shieldEquipped) {

        const float armorReference =
            std::clamp(
                a_context.actionSnapshot.
                    totalArmor,
                0.0f,
                1000.0f);

        const float raw =
            armorReference *
            0.15f;

        const auto physical =
            TULLBUNG::DamageRules::
                ResolvePhysicalSecondary(
                    a_context.target,
                    raw);

        const float resolved =
            physical.resolvedDamage;

        const bool applied =
            physical.valid &&
            resolved > 0.0f &&
            ApplySecondary(
                player,
                a_context.target,
                resolved,
                "BestDefense");

        logger::info(
            "[S12GUARDIAN] Kind=BEST_DEFENSE | ActionID={} | ArmorReference={:.2f} | Raw={:.3f} | Resolved={:.3f} | Applied={}",
            a_context.attackActionID,
            armorReference,
            raw,
            resolved,
            applied);
    }

    // ----------------------------------------------------
    // Guardian: Hammer and Anvil
    // Apply after all same-hit armor conversions so the
    // current hit always uses the pre-buff Armor snapshot.
    // ----------------------------------------------------

    if (const auto it =
            g_actionRuntime.find(
                a_context.attackActionID);
        it !=
            g_actionRuntime.end()) {

        auto& state =
            it->second;

        if (state.hammerAnvilArmed &&
            !state.hammerAnvilApplied) {

            state.hammerAnvilApplied =
                CastHammerAnvilBuff(
                    player,
                    "POWER_HIT");

            logger::info(
                "[S12HAMMER] ActionID={} | Trigger=POWER_HIT | Applied={}",
                a_context.attackActionID,
                state.hammerAnvilApplied);
        }

        // First-valid-hit consumers for this action are done.
        g_actionRuntime.erase(
            it);
    }
}


void OnPlayerHitEvent(
    const Combat::CombatContext&
        a_context) noexcept
{
    // Warrior gameplay no longer uses TESHitEvent as authority.
    // Bash gameplay is handled by the authoritative HitData path.
    (void)a_context;
}

void OnPlayerBashHitData(
    const Combat::CombatContext&
        a_context,
    RE::HitData& a_data) noexcept
{
    if (!IsAvailable() ||
        !a_context.playerAttacker ||
        !a_context.target ||
        !a_context.attackActionID ||
        !a_context.actionSnapshot.
            shieldEquipped) {

        return;
    }

    (void)a_data;

    // HitData and TESHitEvent ordering is not assumed.
    // Bash gameplay owns an independent action-ID latch.
    if (g_lastBashGameplayActionID ==
        a_context.attackActionID) {


        return;
    }

    g_lastBashGameplayActionID =
        a_context.attackActionID;

    auto* player =
        Player();

    if (!player) {
        return;
    }

    if (NodeActive(
            NodeID::
                kGuardianShieldBash)) {

        const float armorReference =
            std::clamp(
                a_context.actionSnapshot.
                    totalArmor,
                0.0f,
                1000.0f);

        const float raw =
            armorReference *
            0.30f;

        const auto physical =
            TULLBUNG::DamageRules::
                ResolvePhysicalSecondary(
                    a_context.target,
                    raw);

        const float resolved =
            physical.resolvedDamage;

        const bool applied =
            physical.valid &&
            resolved > 0.0f &&
            ApplySecondary(
                player,
                a_context.target,
                resolved,
                "ShieldBash");

        logger::info(
            "[S12GUARDIAN] Kind=SHIELD_BASH | ActionID={} | ArmorReference={:.2f} | Raw={:.3f} | Resolved={:.3f} | Applied={}",
            a_context.attackActionID,
            armorReference,
            raw,
            resolved,
            applied);
    }

    // Iron Blood: bash refreshes Hammer and Anvil's
    // +50 Armor / 3 sec effect with no extra Stamina cost.
    // This is intentionally after Shield Bash damage
    // resolution, preserving pre-buff Armor conversion.
    if (NodeActive(
            NodeID::
                kGuardianMaster)) {

        CastHammerAnvilBuff(
            player,
            "IRON_BLOOD_BASH");
    }
}


void OnPlayerIncomingHit(
    RE::Actor* a_aggressor,
    const RE::HitData& a_data,
    const Snapshot::ActorResources&
        a_playerBefore) noexcept
{
    if (!IsAvailable() ||
        !a_aggressor) {

        return;
    }

    (void)a_playerBefore;

    AddParticipant(
        a_aggressor,
        "IncomingHit");

    const auto id =
        a_aggressor->GetFormID();

    const auto it =
        g_enemyActionGeneration.
            find(id);

    const auto generation =
        it !=
            g_enemyActionGeneration.end() ?
            it->second :
            0;

    const bool blocked =
        a_data.flags.any(
            RE::HitData::Flag::
                kBlocked);


    if (!blocked ||
        !generation ||
        !NodeActive(
            NodeID::
                kWarriorIronWall) ||
        !PlayerState::
            GetEquipment().
            hasShield) {

        return;
    }

    auto& consumed =
        g_ironWallConsumedGeneration[
            id];

    if (consumed ==
        generation) {

        logger::info(
            "[S12IRON_WALL] Attacker={:08X} | Generation={} | Duplicate=true | Restore=0",
            id,
            generation);

        return;
    }

    consumed =
        generation;

    const float restored =
        RestoreStamina(
            Player(),
            20.0f);

    logger::info(
        "[S12IRON_WALL] Attacker={:08X} | Generation={} | Duplicate=false | RestoreRequested=20.00 | RestoreApplied={:.2f}",
        id,
        generation,
        restored);
}

}
