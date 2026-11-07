#include "PCH.h"
#include "Hunter.h"

#include "BuildState.h"
#include "Classification.h"
#include "DamageRules.h"
#include "DataCache.h"
#include "PlayerState.h"
#include "Prisma/PrismaBridge.h"
#include "Snapshot.h"

#include "RE/E/ExtraPoison.h"
#include "RE/I/InventoryEntryData.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace
{
    bool g_initialized{};
    bool g_quietProcessingReady{};
    bool g_tacticalRetreatUsedThisCombat{};

    bool g_poisonTrackingNextB{};

    struct PoisonActionFact final
    {
        bool known{};
        bool poisoned{};

        RE::FormID weaponFormID{};
        RE::FormID poisonFormID{};

        std::uint32_t count{};
    };

    struct PendingBloodRequest final
    {
        bool requested{};

        bool refinedAtRequest{};
        bool quietReadyAtRequest{};

        std::uint8_t preStage{};
        std::uint8_t requestedStage{};
    };

    struct PendingEmberRequest final
    {
        bool requested{};

        RE::NiPoint3 impactPosition{};
        float launchDistanceFt{};
    };

    struct HunterActionRuntime final
    {
        PoisonActionFact rightPoison{};
        PoisonActionFact leftPoison{};

        bool rightPoisonConsumed{};
        bool leftPoisonConsumed{};

        bool rangedLaunchKnown{};
        RE::NiPoint3 rangedLaunchPosition{};

        bool composureReadyAtRelease{};
        bool composureConsumed{};

        std::unordered_map<
            RE::FormID,
            bool>
            quickHandsPending{};

        std::unordered_map<
            RE::FormID,
            PendingEmberRequest>
            pendingEmber{};

        // One request per ActionID + Target.
        std::unordered_map<
            RE::FormID,
            PendingBloodRequest>
            pendingBlood{};

        // Terminal Blood authority for this ActionID.
        // Once an ActionID + Target either performs a lethal
        // skip or commits Blood, later duplicate HitData for
        // the same pair must never request Blood again.
        std::unordered_map<
            RE::FormID,
            bool>
            bloodHandledTargets{};
    };

    using BloodClock =
        std::chrono::steady_clock;

    struct BloodPoisonState final
    {
        std::uint8_t stage{};

        BloodClock::time_point expiresAt{};

        // Used by the future S3 refresh commit:
        // same-MGEF recast stacks, therefore S3A/S3B must alternate.
        bool nextS3B{};
    };

    std::unordered_map<
        std::uint64_t,
        HunterActionRuntime>
        g_actionRuntime{};

    // Minimal authoritative Blood state.
    // Expiry is evaluated lazily only when the target is touched.
    std::unordered_map<
        RE::FormID,
        BloodPoisonState>
        g_bloodState{};

    struct QuietKillCandidate final
    {
        std::uint64_t actionID{};
        BloodClock::time_point expiresAt{};
    };

    struct EmberState final
    {
        std::uint8_t stage{};
        BloodClock::time_point expiresAt{};
        bool nextS3B{};
    };

    // Sneak weapon-direct lethal candidates wait for an actual
    // TESDeathEvent before QuietReady is granted.
    std::unordered_map<
        RE::FormID,
        QuietKillCandidate>
        g_quietKillCandidates{};

    // Minimal authoritative Ember stack state.
    // Expiry is evaluated lazily when the target is touched.
    std::unordered_map<
        RE::FormID,
        EmberState>
        g_emberState{};


    bool g_movementKnown{};
    bool g_playerMoving{};

    BloodClock::time_point
        g_stationarySince{};

    std::jthread
        g_composureHudRefreshWorker{};

    std::uint64_t
        g_composureHudRefreshGeneration{};

    inline constexpr auto
        kComposureHudRefreshDelay =
            std::chrono::seconds(3);

    constexpr std::array<
        TULLBUNG::DataCache::HunterEffectID,
        12>
        kFirewallEffects{{
            TULLBUNG::DataCache::HunterEffectID::kBloodBaseS1,
            TULLBUNG::DataCache::HunterEffectID::kBloodBaseS2,
            TULLBUNG::DataCache::HunterEffectID::kBloodBaseS3A,
            TULLBUNG::DataCache::HunterEffectID::kBloodBaseS3B,

            TULLBUNG::DataCache::HunterEffectID::kBloodRefinedS1,
            TULLBUNG::DataCache::HunterEffectID::kBloodRefinedS2,
            TULLBUNG::DataCache::HunterEffectID::kBloodRefinedS3A,
            TULLBUNG::DataCache::HunterEffectID::kBloodRefinedS3B,

            TULLBUNG::DataCache::HunterEffectID::kEmberS1,
            TULLBUNG::DataCache::HunterEffectID::kEmberS2,
            TULLBUNG::DataCache::HunterEffectID::kEmberS3A,
            TULLBUNG::DataCache::HunterEffectID::kEmberS3B
        }};

    [[nodiscard]]
    bool NodeActive(
        TULLBUNG::BuildState::NodeID a_node) noexcept
    {
        return
            TULLBUNG::BuildState::
                IsNodeActive(a_node);
    }

    [[nodiscard]]
    RE::PlayerCharacter* Player() noexcept
    {
        return
            RE::PlayerCharacter::
                GetSingleton();
    }

    [[nodiscard]]
    TULLBUNG::Snapshot::ResourceValue
        CurrentHealth(
            RE::Actor* a_actor) noexcept
    {
        if (!a_actor) {
            return {};
        }

        return
            TULLBUNG::Snapshot::
                CaptureResource(
                    a_actor,
                    RE::ActorValue::kHealth);
    }

    [[nodiscard]]
    PoisonActionFact CaptureEquippedPoison(
        RE::PlayerCharacter* a_player,
        bool a_leftHand) noexcept
    {
        PoisonActionFact result{};

        if (!a_player) {
            return result;
        }

        auto* entry =
            a_player->
                GetEquippedEntryData(
                    a_leftHand);

        if (!entry ||
            !entry->object) {

            return result;
        }

        auto* weapon =
            entry->object->
                As<RE::TESObjectWEAP>();

        if (!weapon) {
            return result;
        }

        result.known = true;

        result.weaponFormID =
            weapon->GetFormID();

        result.poisoned =
            entry->IsPoisoned();

        if (!entry->extraLists) {
            return result;
        }

        for (auto* extraList :
             *entry->extraLists) {

            if (!extraList) {
                continue;
            }

            auto* extraPoison =
                extraList->
                    GetByType<
                        RE::ExtraPoison>();

            if (!extraPoison) {
                continue;
            }

            result.poisonFormID =
                extraPoison->poison ?
                    extraPoison->
                        poison->
                        GetFormID() :
                    0;

            result.count =
                extraPoison->count;

            break;
        }

        return result;
    }

    void PruneActionRuntime(
        std::uint64_t a_currentActionID) noexcept
    {
        constexpr std::uint64_t
            kKeepWindow = 64;

        if (a_currentActionID <=
            kKeepWindow) {

            return;
        }

        const auto minimum =
            a_currentActionID -
            kKeepWindow;

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

    [[nodiscard]]
    BloodPoisonState* FindActiveBloodState(
        RE::FormID a_targetFormID) noexcept
    {
        if (!a_targetFormID) {
            return nullptr;
        }

        const auto it =
            g_bloodState.find(
                a_targetFormID);

        if (it ==
            g_bloodState.end()) {

            return nullptr;
        }

        auto& state =
            it->second;

        const bool invalidStage =
            state.stage < 1 ||
            state.stage > 3;

        const bool expired =
            BloodClock::now() >=
                state.expiresAt;

        if (invalidStage ||
            expired) {

            g_bloodState.erase(it);

            return nullptr;
        }

        return
            &state;
    }

    [[nodiscard]]
    std::uint8_t ResolveRequestedBloodStage(
        std::uint8_t a_preStage,
        bool a_quietReadyAtRequest) noexcept
    {
        if (a_quietReadyAtRequest) {
            return 3;
        }

        if (a_preStage >= 3) {
            return 3;
        }

        return static_cast<std::uint8_t>(
            a_preStage + 1);
    }

    void QueueBloodRequest(
        const TULLBUNG::Combat::
            CombatContext& a_context,
        bool a_refinedAtRequest,
        bool a_quietReadyAtRequest) noexcept
    {
        if (!a_context.attackActionID ||
            !a_context.target) {

            return;
        }

        PruneActionRuntime(
            a_context.attackActionID);

        auto& action =
            g_actionRuntime[
                a_context.attackActionID];

        const auto targetFormID =
            a_context.target->
                GetFormID();

        auto [it, inserted] =
            action.pendingBlood.
                try_emplace(
                    targetFormID);

        // Same ActionID + same target is one Blood application,
        // regardless of how many Hunter nodes requested it.
        if (!inserted &&
            it->second.requested) {

            logger::info(
                "[S13BLOOD-PENDING] Coalesce duplicate | ActionID={} | Target={:08X}",
                a_context.attackActionID,
                targetFormID);

            return;
        }

        auto& request =
            it->second;

        request.requested = true;
        request.refinedAtRequest =
            a_refinedAtRequest;
        request.quietReadyAtRequest =
            a_quietReadyAtRequest;

        if (a_context.hitSnapshot.
                bloodPoisonStateKnown &&
            a_context.hitSnapshot.
                bloodPoisonActive) {

            request.preStage =
                a_context.hitSnapshot.
                    bloodPoisonStage;
        } else {
            request.preStage = 0;
        }

        request.requestedStage =
            ResolveRequestedBloodStage(
                request.preStage,
                request.quietReadyAtRequest);

        logger::info(
            "[S13BLOOD-PENDING] ActionID={} | Target={:08X} | PreStage={} | RequestedStage={} | Refined={} | QuietReadyAtRequest={} | Commit=POST_FINAL",
            a_context.attackActionID,
            targetFormID,
            request.preStage,
            request.requestedStage,
            request.refinedAtRequest,
            request.quietReadyAtRequest);
    }
    void PruneQuietKillCandidates() noexcept
    {
        const auto now =
            BloodClock::now();

        for (auto it =
                 g_quietKillCandidates.begin();
             it !=
                 g_quietKillCandidates.end();) {

            if (now >=
                it->second.expiresAt) {

                it =
                    g_quietKillCandidates.erase(
                        it);
            } else {
                ++it;
            }
        }
    }

    [[nodiscard]]
    bool CastHunterSpellToTarget(
        TULLBUNG::DataCache::HunterSpellID a_spellID,
        RE::Actor* a_target,
        float a_magnitude,
        std::string_view a_source) noexcept
    {
        auto* player =
            Player();

        if (!player ||
            !a_target ||
            a_target->IsDead()) {

            return false;
        }

        auto* spell =
            TULLBUNG::DataCache::
                GetHunterSpell(
                    a_spellID);

        auto* caster =
            player->GetMagicCaster(
                RE::MagicSystem::
                    CastingSource::kInstant);

        if (!spell ||
            !caster) {

            logger::error(
                "[Stage13][{}] Cast failed | Target={:08X} | Spell={} | Caster={}",
                a_source,
                a_target->GetFormID(),
                spell != nullptr,
                caster != nullptr);

            return false;
        }

        caster->CastSpellImmediate(
            spell,
            false,
            a_target,
            1.0f,
            false,
            a_magnitude,
            player);

        return true;
    }

    [[nodiscard]]
    TULLBUNG::DataCache::HunterSpellID
        ResolveBloodSpell(
            bool a_refined,
            std::uint8_t a_stage,
            bool a_useS3B) noexcept
    {
        using ID =
            TULLBUNG::DataCache::
                HunterSpellID;

        if (a_refined) {
            if (a_stage <= 1) {
                return ID::kBloodRefinedS1;
            }

            if (a_stage == 2) {
                return ID::kBloodRefinedS2;
            }

            return
                a_useS3B ?
                    ID::kBloodRefinedS3B :
                    ID::kBloodRefinedS3A;
        }

        if (a_stage <= 1) {
            return ID::kBloodBaseS1;
        }

        if (a_stage == 2) {
            return ID::kBloodBaseS2;
        }

        return
            a_useS3B ?
                ID::kBloodBaseS3B :
                ID::kBloodBaseS3A;
    }

    [[nodiscard]]
    float ResolveBloodMagnitude(
        bool a_refined,
        std::uint8_t a_stage) noexcept
    {
        if (a_refined) {
            if (a_stage <= 1) {
                return 22.5f;
            }

            if (a_stage == 2) {
                return 45.0f;
            }

            return 67.5f;
        }

        if (a_stage <= 1) {
            return 15.0f;
        }

        if (a_stage == 2) {
            return 30.0f;
        }

        return 45.0f;
    }

    [[nodiscard]]
    bool CommitBloodRequest(
        RE::Actor* a_target,
        const PendingBloodRequest&
            a_request) noexcept
    {
        if (!a_target ||
            a_target->IsDead() ||
            !a_request.requested ||
            a_request.requestedStage < 1 ||
            a_request.requestedStage > 3) {

            return false;
        }

        const auto targetFormID =
            a_target->GetFormID();

        auto* oldState =
            FindActiveBloodState(
                targetFormID);

        bool useS3B{};

        if (a_request.requestedStage == 3 &&
            oldState &&
            oldState->stage == 3) {

            useS3B =
                oldState->nextS3B;
        }

        const auto spellID =
            ResolveBloodSpell(
                a_request.refinedAtRequest,
                a_request.requestedStage,
                useS3B);

        const float magnitude =
            ResolveBloodMagnitude(
                a_request.refinedAtRequest,
                a_request.requestedStage);

        if (!CastHunterSpellToTarget(
                spellID,
                a_target,
                0.0f,
                "S13BLOOD")) {

            return false;
        }

        auto& state =
            g_bloodState[
                targetFormID];

        state.stage =
            a_request.requestedStage;

        state.expiresAt =
            BloodClock::now() +
            std::chrono::seconds(6);

        state.nextS3B =
            state.stage == 3 ?
                !useS3B :
                false;

        if (a_request.
                quietReadyAtRequest) {

            g_quietProcessingReady =
                false;
            TULLBUNG::PrismaBridge::NotifyHudStateChanged();
        }

        logger::info(
            "[S13BLOOD-COMMIT] Target={:08X} | Stage={} | Refined={} | Variant={} | Magnitude={:.2f} | QuietConsumed={}",
            targetFormID,
            state.stage,
            a_request.refinedAtRequest,
            state.stage == 3 ?
                (useS3B ? "S3B" : "S3A") :
                (state.stage == 2 ?
                    "S2" :
                    "S1"),
            magnitude,
            a_request.quietReadyAtRequest);

        return true;
    }

    [[nodiscard]]
    EmberState* FindActiveEmberState(
        RE::FormID a_targetFormID) noexcept
    {
        if (!a_targetFormID) {
            return nullptr;
        }

        const auto it =
            g_emberState.find(
                a_targetFormID);

        if (it ==
            g_emberState.end()) {

            return nullptr;
        }

        auto& state =
            it->second;

        if (state.stage < 1 ||
            state.stage > 3 ||
            BloodClock::now() >=
                state.expiresAt) {

            g_emberState.erase(it);
            return nullptr;
        }

        return &state;
    }

    [[nodiscard]]
    bool ConsumePoisonFact(
        const TULLBUNG::Combat::
            CombatContext& a_context,
        PoisonActionFact& a_outFact) noexcept
    {
        a_outFact = {};

        if (!a_context.attackActionID ||
            !a_context.weapon ||
            !a_context.weaponDirectCandidate) {

            return false;
        }

        const auto it =
            g_actionRuntime.find(
                a_context.attackActionID);

        if (it ==
            g_actionRuntime.end()) {

            return false;
        }

        auto& state =
            it->second;

        const auto weaponFormID =
            a_context.weapon->
                GetFormID();

        const auto tryConsume =
            [&](PoisonActionFact& a_fact,
                bool& a_consumed)
                -> bool
            {
                if (a_consumed ||
                    !a_fact.known ||
                    !a_fact.weaponFormID ||
                    a_fact.weaponFormID !=
                        weaponFormID) {

                    return false;
                }

                // Consume the action-local fact exactly once
                // for this hand, even if the weapon was not
                // poisoned. This prevents duplicate HitData
                // from re-evaluating the same delivery.
                a_consumed = true;
                a_outFact = a_fact;

                return true;
            };

        bool matched{};

        switch (a_context.hand) {
        case TULLBUNG::Combat::Hand::kRight:
            matched =
                tryConsume(
                    state.rightPoison,
                    state.rightPoisonConsumed);
            break;

        case TULLBUNG::Combat::Hand::kLeft:
            matched =
                tryConsume(
                    state.leftPoison,
                    state.leftPoisonConsumed);
            break;

        case TULLBUNG::Combat::Hand::kBoth:
        case TULLBUNG::Combat::Hand::kUnknown:
        default:
            // Resolve by weapon FormID if HitData hand is
            // ambiguous. Right is attempted first, then left.
            matched =
                tryConsume(
                    state.rightPoison,
                    state.rightPoisonConsumed);

            if (!matched) {
                matched =
                    tryConsume(
                        state.leftPoison,
                        state.leftPoisonConsumed);
            }
            break;
        }

        if (!matched) {
            return false;
        }

        // P1 contract:
        // poison authority is the action-boundary snapshot.
        // Do NOT inspect current ExtraPoison at HitData time.
        return
            a_outFact.known &&
            a_outFact.poisoned;
    }

    void SetAbility(
        RE::PlayerCharacter* a_player,
        TULLBUNG::DataCache::
            HunterSpellID a_spellID,
        bool a_enable,
        std::string_view a_name)
    {
        if (!a_player) {
            return;
        }

        auto* spell =
            TULLBUNG::DataCache::
                GetHunterSpell(
                    a_spellID);

        if (!spell) {
            logger::error(
                "[S13ABILITY] {} = MISSING",
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
                "[S13ABILITY] {} = ON",
                a_name);

        } else if (
            !a_enable &&
            hasSpell) {

            a_player->RemoveSpell(
                spell);

            logger::info(
                "[S13ABILITY] {} = OFF",
                a_name);
        }
    }

    void SyncRuntimeAbilities() noexcept
    {
        if (!TULLBUNG::DataCache::
                IsHunterReady()) {

            return;
        }

        auto* player = Player();

        if (!player) {
            return;
        }

        const auto& equipment =
            TULLBUNG::PlayerState::
                GetEquipment();

        const bool daggerEquipped =
            TULLBUNG::Classification::
                IsDagger(
                    equipment.right.weapon) ||
            TULLBUNG::Classification::
                IsDagger(
                    equipment.left.weapon);

        const bool rangedEquipped =
            TULLBUNG::Classification::
                IsBowOrCrossbow(
                    equipment.right.weapon) ||
            TULLBUNG::Classification::
                IsBowOrCrossbow(
                    equipment.left.weapon);

        SetAbility(
            player,
            TULLBUNG::DataCache::
                HunterSpellID::
                    kDaggerMastery,
            NodeActive(
                TULLBUNG::BuildState::
                    NodeID::
                        kAssassinDaggerMastery) &&
                daggerEquipped,
            "DaggerMastery");

        SetAbility(
            player,
            TULLBUNG::DataCache::
                HunterSpellID::
                    kQuickHands,
            NodeActive(
                TULLBUNG::BuildState::
                    NodeID::
                        kMarksmanQuickHands) &&
                rangedEquipped,
            "QuickHands");
    }

    void RemoveRuntimeAbilities() noexcept
    {
        auto* player = Player();

        if (!player ||
            !TULLBUNG::DataCache::
                IsHunterReady()) {

            return;
        }

        SetAbility(
            player,
            TULLBUNG::DataCache::
                HunterSpellID::
                    kDaggerMastery,
            false,
            "DaggerMastery");

        SetAbility(
            player,
            TULLBUNG::DataCache::
                HunterSpellID::
                    kQuickHands,
            false,
            "QuickHands");
    }

    [[nodiscard]]
    bool CastPoisonTrackingBuff(
        RE::PlayerCharacter* a_player) noexcept
    {
        if (!a_player) {
            return false;
        }

        const auto spellID =
            g_poisonTrackingNextB ?
                TULLBUNG::DataCache::
                    HunterSpellID::
                        kPoisonTrackingB :
                TULLBUNG::DataCache::
                    HunterSpellID::
                        kPoisonTrackingA;

        auto* spell =
            TULLBUNG::DataCache::
                GetHunterSpell(
                    spellID);

        auto* caster =
            a_player->GetMagicCaster(
                RE::MagicSystem::
                    CastingSource::kInstant);

        if (!spell ||
            !caster) {

            logger::error(
                "[S13POISON_TRACK] Cast failed | Variant={} | Spell={} | Caster={}",
                g_poisonTrackingNextB ?
                    "B" :
                    "A",
                spell != nullptr,
                caster != nullptr);

            return false;
        }

        const bool usedB =
            g_poisonTrackingNextB;

        // P5 contract:
        // A/B share one Dispel-with-Keywords group.
        // Alternating MGEFs refreshes duration without scanning
        // ActiveEffect lists and without same-MGEF stacking.
        caster->CastSpellImmediate(
            spell,
            false,
            a_player,
            1.0f,
            false,
            15.0f,
            a_player);

        g_poisonTrackingNextB =
            !g_poisonTrackingNextB;

        logger::info(
            "[S13POISON_TRACK] Applied | Variant={} | MoveSpeed=15 | Duration=5",
            usedB ?
                "B" :
                "A");

        return true;
    }

    inline constexpr float
        kWorldUnitsPerFoot =
            128.0f / 6.0f;

    [[nodiscard]]
    float DistanceFeet(
        const RE::NiPoint3& a_from,
        const RE::NiPoint3& a_to) noexcept
    {
        const float dx =
            a_to.x - a_from.x;

        const float dy =
            a_to.y - a_from.y;

        const float dz =
            a_to.z - a_from.z;

        return
            std::sqrt(
                dx * dx +
                dy * dy +
                dz * dz) /
            kWorldUnitsPerFoot;
    }

    [[nodiscard]]
    std::int64_t StationaryForMs() noexcept
    {
        if (!g_movementKnown ||
            g_playerMoving ||
            g_stationarySince ==
                BloodClock::time_point{}) {

            return 0;
        }

        return
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    BloodClock::now() -
                    g_stationarySince).
                count();
    }

    void CancelComposureHudRefresh() noexcept
    {
        ++g_composureHudRefreshGeneration;

        if (g_composureHudRefreshWorker.joinable()) {
            g_composureHudRefreshWorker.request_stop();
        }
    }

    void ScheduleComposureHudRefresh(
        BloodClock::time_point a_stationarySince) noexcept
    {
        CancelComposureHudRefresh();

        const auto generation =
            g_composureHudRefreshGeneration;

        try {
            g_composureHudRefreshWorker =
                std::jthread(
                    [generation,
                     a_stationarySince](
                        std::stop_token a_stopToken) {

                        std::mutex waitMutex;
                        std::condition_variable_any
                            waitCondition;

                        std::unique_lock<
                            std::mutex>
                                waitLock(
                                    waitMutex);

                        waitCondition.wait_until(
                            waitLock,
                            a_stopToken,
                            a_stationarySince +
                                kComposureHudRefreshDelay,
                            []() {
                                return false;
                            });

                        if (a_stopToken.
                                stop_requested()) {
                            return;
                        }

                        waitLock.unlock();

                        auto* taskInterface =
                            SKSE::GetTaskInterface();

                        if (!taskInterface) {
                            logger::warn(
                                "[S18HUD] Composure refresh dispatch skipped | Reason=NO_TASK_INTERFACE");
                            return;
                        }

                        taskInterface->AddTask(
                            [generation,
                             a_stationarySince]() {

                                if (generation !=
                                    g_composureHudRefreshGeneration) {
                                    return;
                                }

                                if (!g_movementKnown ||
                                    g_playerMoving ||
                                    g_stationarySince !=
                                        a_stationarySince) {
                                    return;
                                }

                                if (BloodClock::now() -
                                        g_stationarySince <
                                    kComposureHudRefreshDelay) {
                                    return;
                                }

                                TULLBUNG::PrismaBridge::
                                    NotifyHudStateChanged();
                            });
                    });
        } catch (...) {
            logger::error(
                "[S18HUD] Composure refresh schedule failed");
        }
    }

    [[nodiscard]]
    float DamageStamina(
        RE::Actor* a_target,
        float a_requested) noexcept
    {
        if (!a_target ||
            a_requested <= 0.0f) {

            return 0.0f;
        }

        auto* owner =
            a_target->
                AsActorValueOwner();

        if (!owner) {
            return 0.0f;
        }

        const auto stamina =
            TULLBUNG::Snapshot::
                CaptureResource(
                    a_target,
                    RE::ActorValue::kStamina);

        const float applied =
            (std::min)(
                (std::max)(
                    stamina.current,
                    0.0f),
                a_requested);

        if (applied <= 0.0f) {
            return 0.0f;
        }

        owner->ModActorValue(
            RE::ACTOR_VALUE_MODIFIERS::
                kDamage,
            RE::ActorValue::kStamina,
            -applied);

        return applied;
    }

    [[nodiscard]]
    bool CastTacticalRetreat(
        RE::PlayerCharacter* a_player) noexcept
    {
        if (!a_player) {
            return false;
        }

        auto* spell =
            TULLBUNG::DataCache::
                GetHunterSpell(
                    TULLBUNG::DataCache::
                        HunterSpellID::
                            kTacticalRetreat);

        auto* caster =
            a_player->GetMagicCaster(
                RE::MagicSystem::
                    CastingSource::kInstant);

        if (!spell ||
            !caster) {

            logger::error(
                "[S13TACTICAL_RETREAT] Cast failed | Spell={} | Caster={}",
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
            0.0f,
            a_player);

        return true;
    }

    bool RegisterFirewallEffects() noexcept
    {
        std::size_t registered = 0;

        for (const auto id :
             kFirewallEffects) {

            auto* effect =
                TULLBUNG::DataCache::
                    GetHunterEffect(id);

            if (!effect) {
                logger::error(
                    "[Stage13][Firewall] Hunter MGEF missing | Index={}",
                    static_cast<std::uint32_t>(id));

                continue;
            }

            if (!TULLBUNG::DamageRules::
                    RegisterFirewallSource(
                        effect->GetFormID())) {

                logger::error(
                    "[Stage13][Firewall] Register FAIL | MGEF={:08X} | Index={}",
                    effect->GetFormID(),
                    static_cast<std::uint32_t>(id));

                continue;
            }

            ++registered;
        }

        if (registered !=
            kFirewallEffects.size()) {

            logger::error(
                "[Stage13][Firewall] Incomplete | Registered={}/{}",
                registered,
                kFirewallEffects.size());

            return false;
        }

        logger::info(
            "[Stage13][Firewall] Hunter sources registered | Count={}",
            registered);

        return true;
    }
}

namespace TULLBUNG::Classes::Hunter
{
    bool Initialize() noexcept
    {
        if (!DataCache::IsHunterReady()) {
            logger::error(
                "[Stage13] Hunter runtime unavailable | DataCache Hunter forms incomplete");

            return false;
        }

        if (!RegisterFirewallEffects()) {
            logger::error(
                "[Stage13] Hunter runtime unavailable | Trigger Firewall registration failed");

            return false;
        }

        g_initialized = true;

        ResetTransientState();

        logger::info(
            "[Stage13] Hunter runtime = READY");

        return true;
    }

    bool IsAvailable() noexcept
    {
        return
            g_initialized &&
            DataCache::
                IsHunterReady();
    }

    void ResetTransientState() noexcept
    {
        g_quietProcessingReady = false;
        g_tacticalRetreatUsedThisCombat = false;
        g_poisonTrackingNextB = false;

        g_actionRuntime.clear();
        g_bloodState.clear();

        g_quietKillCandidates.clear();
        g_emberState.clear();

        g_movementKnown = false;
        g_playerMoving = false;
        g_stationarySince = {};

        CancelComposureHudRefresh();

        RemoveRuntimeAbilities();
        SyncRuntimeAbilities();

        logger::info(
            "[Stage13] Transient state reset");
    }

    void OnEquipmentChanged() noexcept
    {
        if (IsAvailable()) {
            SyncRuntimeAbilities();
        }
    }

    void OnCombatStateChanged(
        RE::Actor* a_actor,
        RE::Actor* a_target,
        bool a_enteredCombat,
        bool a_leftCombat) noexcept
    {
        (void)a_target;
        (void)a_enteredCombat;
        (void)a_leftCombat;

        if (!IsAvailable() ||
            !a_actor ||
            !g_tacticalRetreatUsedThisCombat) {
            return;
        }

        auto* player = Player();
        if (player && !player->IsInCombat()) {
            g_tacticalRetreatUsedThisCombat = false;
            logger::info(
                "[S13TACTICAL_RETREAT] Rearmed | CombatEndVerified=true | Source=TESCombatEvent | Actor={:08X}",
                a_actor->GetFormID());
        }
    }

    void OnPlayerActionStart(
        const Combat::AttackActionState&
            a_action) noexcept
    {
        if (!IsAvailable() ||
            !a_action.actionID ||
            !a_action.actor ||
            a_action.sourceKind !=
                Combat::SourceKind::kWeapon) {

            return;
        }

        auto* player = Player();

        if (!player ||
            a_action.actor != player) {

            return;
        }

        // P1 production authority:
        // Only action boundaries proven to precede poison
        // consumption are accepted.
        //
        // WeaponHitFallback occurs at HitData time and therefore
        // must NEVER be used to infer ordinary poison delivery.
        const bool trustedBoundary =
            a_action.origin ==
                Combat::ActionOrigin::
                    kWeaponSwing ||
            a_action.origin ==
                Combat::ActionOrigin::
                    kBowRelease;

        if (!trustedBoundary) {
            return;
        }

        PruneActionRuntime(
            a_action.actionID);

        HunterActionRuntime state{};

        // Capture BOTH equipped hands once.
        // This also covers a later single->dual promotion,
        // which intentionally keeps the original ActionID and
        // does not create a second ActionStart.
        state.rightPoison =
            CaptureEquippedPoison(
                player,
                false);

        state.leftPoison =
            CaptureEquippedPoison(
                player,
                true);

        if (a_action.origin ==
                Combat::ActionOrigin::
                    kBowRelease &&
            NodeActive(
                BuildState::NodeID::
                    kMarksmanComposure)) {

            const auto stationaryMs =
                StationaryForMs();

            state.composureReadyAtRelease =
                stationaryMs >= 3000;

            logger::info(
                "[S13COMPOSURE] ReleaseSnapshot | ActionID={} | StationaryMs={} | Ready={}",
                a_action.actionID,
                stationaryMs,
                state.composureReadyAtRelease);
        }

        g_actionRuntime[
            a_action.actionID] =
                state;

        logger::info(
            "[S13POISON-SNAPSHOT] ActionID={} | Origin={} | RightKnown={} | RightWeapon={:08X} | RightPoisoned={} | RightPoison={:08X} | RightCount={} | LeftKnown={} | LeftWeapon={:08X} | LeftPoisoned={} | LeftPoison={:08X} | LeftCount={}",
            a_action.actionID,
            a_action.origin ==
                    Combat::ActionOrigin::
                        kBowRelease ?
                "BOW_RELEASE" :
                "WEAPON_SWING",
            state.rightPoison.known,
            state.rightPoison.weaponFormID,
            state.rightPoison.poisoned,
            state.rightPoison.poisonFormID,
            state.rightPoison.count,
            state.leftPoison.known,
            state.leftPoison.weaponFormID,
            state.leftPoison.poisoned,
            state.leftPoison.poisonFormID,
            state.leftPoison.count);
    }

    void OnPlayerMovementStateChanged(
        bool a_moving) noexcept
    {
        if (!IsAvailable()) {
            return;
        }

        // Event-only observation: no frame polling.
        // Do not rearm on an individual NPC leaving combat.
        auto* player = Player();
        if (g_tacticalRetreatUsedThisCombat &&
            player &&
            !player->IsInCombat()) {
            g_tacticalRetreatUsedThisCombat = false;
            logger::info(
                "[S13TACTICAL_RETREAT] Rearmed | CombatEndVerified=true | Source=MovementChanged");
        }

        if (g_movementKnown &&
            g_playerMoving ==
                a_moving) {

            return;
        }

        g_movementKnown = true;
        g_playerMoving = a_moving;

        if (a_moving) {
            g_stationarySince = {};
            CancelComposureHudRefresh();
        } else {
            g_stationarySince =
                BloodClock::now();
            ScheduleComposureHudRefresh(
                g_stationarySince);
        }

        TULLBUNG::PrismaBridge::NotifyHudStateChanged();

        logger::info(
            "[S13COMPOSURE] Movement | Moving={} | ChargeReset={}",
            a_moving,
            true);
    }

    void OnPlayerRangedLaunch(
        std::uint64_t a_actionID,
        RE::TESObjectWEAP* a_weapon,
        float a_x,
        float a_y,
        float a_z) noexcept
    {
        if (!IsAvailable() ||
            !a_actionID ||
            !Classification::
                IsBowOrCrossbow(
                    a_weapon)) {

            return;
        }

        auto& state =
            g_actionRuntime[
                a_actionID];

        state.rangedLaunchKnown = true;

        state.rangedLaunchPosition.x =
            a_x;

        state.rangedLaunchPosition.y =
            a_y;

        state.rangedLaunchPosition.z =
            a_z;

        logger::info(
            "[S13RANGED-LAUNCH] ActionID={} | Weapon={:08X} | Position=({:.3f},{:.3f},{:.3f})",
            a_actionID,
            a_weapon->GetFormID(),
            a_x,
            a_y,
            a_z);
    }

    void PopulateHitState(
        RE::Actor* a_target,
        Snapshot::HitStateInput&
            a_hitState) noexcept
    {
        a_hitState.
            bloodPoisonActive = false;

        a_hitState.
            bloodPoisonStage = 0;

        a_hitState.
            bloodPoisonStateKnown =
                a_target != nullptr;

        if (!a_target) {
            return;
        }

        auto* state =
            FindActiveBloodState(
                a_target->
                    GetFormID());

        if (!state) {
            return;
        }

        a_hitState.
            bloodPoisonActive = true;

        a_hitState.
            bloodPoisonStage =
                state->stage;

        logger::info(
            "[S13BLOOD-PRE] Target={:08X} | Active=true | Stage={}",
            a_target->GetFormID(),
            state->stage);
    }

    void AccumulateWeaponDirectDamage(
        const Combat::CombatContext&
            a_context,
        DamageRules::DirectDamageAccumulator&
            a_accumulator) noexcept
    {
        if (!IsAvailable() ||
            !a_context.playerAttacker ||
            !a_context.target ||
            !a_context.weapon ||
            !a_context.weaponDirectCandidate ||
            !a_context.directDamageKnown) {

            return;
        }

        auto* player = Player();

        if (!player) {
            return;
        }

        float submittedPercent{};

        const auto submit =
            [&](float a_percent)
            {
                a_accumulator.AddPercent(
                    DamageRules::
                        DirectChannel::kWeapon,
                    a_percent);

                submittedPercent +=
                    a_percent;
            };

        const auto& equipment =
            PlayerState::
                GetEquipment();

        const bool dagger =
            Classification::
                IsDagger(
                    a_context.weapon);

        const bool bowOrCrossbow =
            Classification::
                IsBowOrCrossbow(
                    a_context.weapon);

        // Hunter: Light Armor Mastery
        if (NodeActive(
                BuildState::NodeID::
                    kHunterLightArmorMastery) &&
            equipment.lightArmorPieces >= 2) {

            submit(10.0f);
        }

        // Assassin: Dagger Mastery
        if (NodeActive(
                BuildState::NodeID::
                    kAssassinDaggerMastery) &&
            dagger) {

            submit(10.0f);
        }

        // Assassin: Quiet Processing
        // Direct component only.
        if (NodeActive(
                BuildState::NodeID::
                    kAssassinSilentHandling) &&
            a_context.sneakAttack) {

            submit(15.0f);
        }

        // Assassin Master: Wound Gouge
        //
        // MUST use frozen pre-hit Blood state.
        // The hit which creates S1 therefore never receives
        // the +25% bonus retroactively.
        if (NodeActive(
                BuildState::NodeID::
                    kAssassinMaster) &&
            a_context.hitSnapshot.
                bloodPoisonStateKnown &&
            a_context.hitSnapshot.
                bloodPoisonActive &&
            a_context.hitSnapshot.
                bloodPoisonStage >= 1) {

            submit(25.0f);
        }

        auto playerHealth =
            CurrentHealth(player);

        auto targetHealth =
            a_context.hitSnapshot.
                target.health;

        if (targetHealth.maximum <= 0.0f) {
            targetHealth =
                CurrentHealth(
                    a_context.target);
        }

        // Hunter Master: Rivalry
        if (NodeActive(
                BuildState::NodeID::
                    kHunterMaster) &&
            playerHealth.maximum > 0.0f &&
            targetHealth.maximum >
                playerHealth.maximum) {

            submit(25.0f);
        }

        // Marksman: Open Battle
        if (NodeActive(
                BuildState::NodeID::
                    kMarksmanOpenBattle) &&
            bowOrCrossbow &&
            !a_context.sneakAttack) {

            submit(15.0f);
        }

        // Marksman: Composure
        //
        // Readiness is frozen at BowRelease.
        // A miss does not consume it because consumption happens
        // only after an actual ranged direct HitData reaches us.
        if (NodeActive(
                BuildState::NodeID::
                    kMarksmanComposure) &&
            bowOrCrossbow &&
            a_context.attackActionID) {

            const auto actionIt =
                g_actionRuntime.find(
                    a_context.attackActionID);

            if (actionIt !=
                    g_actionRuntime.end() &&
                actionIt->second.
                    composureReadyAtRelease &&
                !actionIt->second.
                    composureConsumed) {

                submit(50.0f);
            }
        }

        // Marksman Master: Execution
        if (NodeActive(
                BuildState::NodeID::
                    kMarksmanMaster) &&
            bowOrCrossbow &&
            targetHealth.maximum > 0.0f &&
            targetHealth.current <=
                targetHealth.maximum *
                    0.50f) {

            submit(25.0f);
        }

        // Wound Gouge is added after Blood pre-hit state exists.
        // Composure is added after its stationary-ready state exists.

        if (submittedPercent > 0.0f) {
            logger::info(
                "[S13DIRECT] ActionID={} | Target={:08X} | Weapon={:08X} | Dagger={} | Ranged={} | Sneak={} | LightPieces={} | Additive={:.2f}",
                a_context.attackActionID,
                a_context.target->GetFormID(),
                a_context.weapon->GetFormID(),
                dagger,
                bowOrCrossbow,
                a_context.sneakAttack,
                equipment.lightArmorPieces,
                submittedPercent);
        }
    }

    void AccumulateIncomingDirectDR(
        RE::Actor* a_aggressor,
        const RE::HitData& a_data,
        const Snapshot::ActorResources&
            a_playerBefore,
        DamageRules::ConditionalDRAccumulator&
            a_accumulator) noexcept
    {
        (void)a_data;

        if (!IsAvailable() ||
            !a_aggressor ||
            !NodeActive(
                BuildState::NodeID::
                    kHunterVigilance)) {

            return;
        }

        float playerMax =
            a_playerBefore.health.maximum;

        if (playerMax <= 0.0f) {
            if (auto* player = Player()) {
                playerMax =
                    CurrentHealth(player).
                        maximum;
            }
        }

        const float aggressorMax =
            CurrentHealth(
                a_aggressor).
                    maximum;

        if (playerMax <= 0.0f ||
            aggressorMax <=
                playerMax) {

            return;
        }

        a_accumulator.
            AddReductionPercent(
                20.0f);

        logger::info(
            "[S13DR] HunterVigilance | Aggressor={:08X} | AggressorMaxHP={:.2f} | PlayerMaxHP={:.2f} | Reduction=20.00",
            a_aggressor->GetFormID(),
            aggressorMax,
            playerMax);
    }

    void OnPlayerWeaponDirectHit(
        const Combat::CombatContext&
            a_context,
        RE::HitData& a_data) noexcept
    {
        (void)a_data;

        if (!IsAvailable() ||
            !a_context.playerAttacker ||
            !a_context.target ||
            !a_context.weapon ||
            !a_context.weaponDirectCandidate) {

            return;
        }

        auto* player = Player();

        if (!player) {
            return;
        }

        PoisonActionFact poisonFact{};

        const bool ordinaryPoisonDelivered =
            ConsumePoisonFact(
                a_context,
                poisonFact);

        if (ordinaryPoisonDelivered) {
            logger::info(
                "[S13POISON-DELIVERY] ActionID={} | Target={:08X} | Weapon={:08X} | Hand={} | Poison={:08X} | CountAtBoundary={}",
                a_context.attackActionID,
                a_context.target->GetFormID(),
                a_context.weapon->GetFormID(),
                static_cast<std::uint32_t>(
                    a_context.hand),
                poisonFact.poisonFormID,
                poisonFact.count);
        }

        // ----------------------------------------------------
        // Hunter: Poison Tracking
        //
        // Trigger is ordinary poison delivery itself.
        // Poison immunity/resistance does not suppress this
        // after delivery has occurred.
        // ----------------------------------------------------
        if (ordinaryPoisonDelivered &&
            NodeActive(
                BuildState::NodeID::
                    kHunterPoisonTracking)) {

            (void)CastPoisonTrackingBuff(
                player);
        }

        const bool dagger =
            Classification::
                IsDagger(
                    a_context.weapon);

        const bool ranged =
            Classification::
                IsBowOrCrossbow(
                    a_context.weapon);
        PruneQuietKillCandidates();

        auto targetHealthBefore =
            a_context.hitSnapshot.
                target.health.current;

        if (targetHealthBefore <=
            0.0f) {

            targetHealthBefore =
                CurrentHealth(
                    a_context.target).
                    current;
        }

        const float damageSent =
            a_data.totalDamage > 0.0f ?
                a_data.totalDamage :
                0.0f;

        // Session-1 P7B contract:
        // same-hit Blood skip uses the PRE authoritative
        // lethal candidate, while QuietReady waits for
        // TESDeathEvent actual death.
        const bool lethalCandidate =
            targetHealthBefore > 0.0f &&
            damageSent >=
                targetHealthBefore;

        if (lethalCandidate &&
            a_context.sneakAttack &&
            NodeActive(
                BuildState::NodeID::
                    kAssassinSilentHandling)) {

            auto& pendingKill =
                g_quietKillCandidates[
                    a_context.target->
                        GetFormID()];

            pendingKill.actionID =
                a_context.attackActionID;

            pendingKill.expiresAt =
                BloodClock::now() +
                std::chrono::seconds(10);

            logger::info(
                "[S13QUIET-PENDING] ActionID={} | Target={:08X} | HPBefore={:.3f} | DamageSent={:.3f}",
                a_context.attackActionID,
                a_context.target->
                    GetFormID(),
                targetHealthBefore,
                damageSent);
        }


        HunterActionRuntime*
            actionState = nullptr;

        if (a_context.attackActionID) {
            PruneActionRuntime(
                a_context.attackActionID);

            actionState =
                &g_actionRuntime[
                    a_context.attackActionID];
        }

        // ----------------------------------------------------
        // Composure consumption
        // ----------------------------------------------------
        if (ranged &&
            actionState &&
            NodeActive(
                BuildState::NodeID::
                    kMarksmanComposure) &&
            actionState->
                composureReadyAtRelease &&
            !actionState->
                composureConsumed) {

            actionState->
                composureConsumed = true;

            // "발동 후 다시 3초 준비".
            if (g_movementKnown &&
                !g_playerMoving) {

                g_stationarySince =
                    BloodClock::now();
                ScheduleComposureHudRefresh(
                    g_stationarySince);
            }

            TULLBUNG::PrismaBridge::NotifyHudStateChanged();

        logger::info(
                "[S13COMPOSURE] Consumed | ActionID={} | Target={:08X} | Bonus=50",
                a_context.attackActionID,
                a_context.target->GetFormID());
        }

        // ----------------------------------------------------
        // Quick Hands
        //
        // Damage itself is resolved first. Stamina destruction
        // is committed from the POST-original callback.
        // ----------------------------------------------------
        if (ranged &&
            actionState &&
            NodeActive(
                BuildState::NodeID::
                    kMarksmanQuickHands)) {

            actionState->
                quickHandsPending[
                    a_context.target->
                        GetFormID()] =
                            true;
        }

        // ----------------------------------------------------
        // Ember Spread production request
        //
        // P6 still owns final LOS acceptance.
        // No Ember MGEF is cast in B4.
        // ----------------------------------------------------
        if (ranged &&
            actionState &&
            actionState->
                rangedLaunchKnown &&
            NodeActive(
                BuildState::NodeID::
                    kMarksmanEmberSpread)) {

            const float distanceFt =
                DistanceFeet(
                    actionState->
                        rangedLaunchPosition,
                    a_data.hitPosition);

            if (distanceFt >= 50.0f) {
                auto& request =
                    actionState->
                        pendingEmber[
                            a_context.target->
                                GetFormID()];

                if (!request.requested) {
                    request.requested = true;

                    request.impactPosition =
                        a_data.hitPosition;

                    request.launchDistanceFt =
                        distanceFt;

                    logger::info(
                        "[S13EMBER-REQUEST] ActionID={} | Target={:08X} | DistanceFt={:.3f} | Impact=({:.3f},{:.3f},{:.3f}) | Commit=FINAL_RADIUS",
                        a_context.attackActionID,
                        a_context.target->GetFormID(),
                        distanceFt,
                        a_data.hitPosition.x,
                        a_data.hitPosition.y,
                        a_data.hitPosition.z);
                }
            }
        }

        const bool normalAttack =
            a_context.attackKind ==
                Combat::AttackKind::
                    kNormal;

        const bool powerAttack =
            a_context.attackKind ==
                Combat::AttackKind::
                    kPower;

        // ----------------------------------------------------
        // Blood Poison REQUEST generation only.
        //
        // B3A deliberately does NOT cast a Blood spell.
        // Actual commit is deferred until the outgoing
        // post-direct kill authority is locked by P7B.
        //
        // Same hit + same target requests are coalesced into
        // one boolean request here.
        // ----------------------------------------------------

        const bool requestFromPoisonedDagger =
            ordinaryPoisonDelivered &&
            dagger &&
            normalAttack &&
            NodeActive(
                BuildState::NodeID::
                    kAssassinBloodPoison);

        const bool requestFromBloodInfusion =
            dagger &&
            powerAttack &&
            NodeActive(
                BuildState::NodeID::
                    kAssassinBloodInfusion);

        const bool requestFromRangedDelivery =
            ranged &&
            NodeActive(
                BuildState::NodeID::
                    kMarksmanPoisonDelivery);

        const bool anyBloodTrigger =
            requestFromPoisonedDagger ||
            requestFromBloodInfusion ||
            requestFromRangedDelivery;

        const auto bloodTargetFormID =
            a_context.target->
                GetFormID();

        PruneActionRuntime(
            a_context.attackActionID);

        auto& bloodAction =
            g_actionRuntime[
                a_context.attackActionID];

        const auto handledIt =
            bloodAction.
                bloodHandledTargets.
                find(
                    bloodTargetFormID);

        const bool bloodAlreadyHandled =
            handledIt !=
                bloodAction.
                    bloodHandledTargets.
                    end() &&
            handledIt->second;

        if (anyBloodTrigger &&
            bloodAlreadyHandled) {

            logger::info(
                "[S13BLOOD-DEDUPE] ActionID={} | Target={:08X} | Reason=ACTION_TARGET_ALREADY_HANDLED",
                a_context.attackActionID,
                bloodTargetFormID);

            return;
        }

        const bool bloodRequested =
            anyBloodTrigger &&
            !lethalCandidate;

        if (anyBloodTrigger &&
            lethalCandidate) {

            bloodAction.
                bloodHandledTargets[
                    bloodTargetFormID] =
                        true;

            logger::info(
                "[S13BLOOD-SKIP] ActionID={} | Target={:08X} | Reason=SAME_HIT_LETHAL | QuietPreserved={}",
                a_context.attackActionID,
                bloodTargetFormID,
                g_quietProcessingReady);
        }

        if (bloodRequested) {
            const bool refinedAtRequest =
                NodeActive(
                    BuildState::NodeID::
                        kAssassinRefinedPoison);

            const bool quietReadyAtRequest =
                g_quietProcessingReady;

            QueueBloodRequest(
                a_context,
                refinedAtRequest,
                quietReadyAtRequest);

            logger::info(
                "[S13BLOOD-REQUEST] ActionID={} | Target={:08X} | PoisonedDagger={} | BloodInfusion={} | RangedDelivery={} | Refined={} | QuietReady={} | Commit=POST_FINAL",
                a_context.attackActionID,
                bloodTargetFormID,
                requestFromPoisonedDagger,
                requestFromBloodInfusion,
                requestFromRangedDelivery,
                NodeActive(
                    BuildState::NodeID::
                        kAssassinRefinedPoison),
                g_quietProcessingReady);
        }
    }

    void OnPlayerWeaponDirectHitPost(
        std::uint64_t a_actionID,
        RE::Actor* a_target) noexcept
    {
        if (!IsAvailable() ||
            !a_actionID ||
            !a_target) {

            return;
        }

        const auto actionIt =
            g_actionRuntime.find(
                a_actionID);

        if (actionIt ==
            g_actionRuntime.end()) {

            return;
        }

        const auto targetFormID =
            a_target->GetFormID();

        // Blood and Quick Hands are independent POST channels.
        auto& bloodPending =
            actionIt->second.pendingBlood;

        const auto bloodIt =
            bloodPending.find(
                targetFormID);

        if (bloodIt !=
                bloodPending.end() &&
            bloodIt->second.requested) {

            const auto request =
                bloodIt->second;

            bloodPending.erase(
                bloodIt);

            actionIt->second.
                bloodHandledTargets[
                    targetFormID] =
                        true;

            (void)CommitBloodRequest(
                a_target,
                request);
        }

        auto& quickPending =
            actionIt->second.
                quickHandsPending;

        const auto quickIt =
            quickPending.find(
                targetFormID);

        if (quickIt !=
                quickPending.end() &&
            quickIt->second) {

            quickPending.erase(
                quickIt);

            const float applied =
                DamageStamina(
                    a_target,
                    30.0f);

            logger::info(
                "[S13QUICK_HANDS] ActionID={} | Target={:08X} | Requested=30.00 | Applied={:.2f}",
                a_actionID,
                targetFormID,
                applied);
        }
    }

    void OnPlayerIncomingHit(
        RE::Actor* a_aggressor,
        const RE::HitData& a_data,
        const Snapshot::ActorResources&
            a_playerBefore) noexcept
    {
        (void)a_playerBefore;

        if (!IsAvailable() ||
            !a_aggressor ||
            !NodeActive(
                BuildState::NodeID::
                    kHunterTacticalRetreat) ||
            g_tacticalRetreatUsedThisCombat) {

            return;
        }

        auto* player = Player();

        if (!player ||
            !player->IsInCombat() ||
            !a_aggressor->
                IsHostileToActor(
                    player)) {

            return;
        }

        const bool bash =
            a_data.flags.any(
                RE::HitData::Flag::
                    kBash);

        // WeaponHitHook physical path:
        // null weapon covers unarmed / shield-bash cases.
        const bool meleeDirect =
            bash ||
            !a_data.weapon ||
            Classification::
                IsMelee(
                    a_data.weapon);

        if (!meleeDirect ||
            !std::isfinite(
                a_data.totalDamage) ||
            a_data.totalDamage <= 0.0f) {

            return;
        }

        if (!CastTacticalRetreat(
                player)) {

            return;
        }

        g_tacticalRetreatUsedThisCombat =
            true;

        logger::info(
            "[S13TACTICAL_RETREAT] Triggered | Aggressor={:08X} | Bash={} | WeaponNull={} | Damage={:.3f} | Ethereal=2 | Invisible=5",
            a_aggressor->GetFormID(),
            bash,
            a_data.weapon == nullptr,
            a_data.totalDamage);
    }

    void OnDeathEvent(
        RE::Actor* a_dying,
        RE::Actor* a_killer) noexcept
    {
        if (!IsAvailable() ||
            !a_dying) {

            return;
        }

        const auto targetFormID =
            a_dying->GetFormID();

        PruneQuietKillCandidates();

        const auto pendingIt =
            g_quietKillCandidates.find(
                targetFormID);

        if (pendingIt !=
            g_quietKillCandidates.end()) {

            auto* player =
                Player();

            const bool playerKill =
                player &&
                a_killer ==
                    player;

            const auto actionID =
                pendingIt->second.
                    actionID;

            if (playerKill) {
                g_quietProcessingReady =
                    true;

                TULLBUNG::PrismaBridge::NotifyHudStateChanged();

                logger::info(
                    "[S13QUIET-READY] ActionID={} | Target={:08X} | Killer=PLAYER | Ready=true",
                    actionID,
                    targetFormID);
            } else {
                logger::info(
                    "[S13QUIET-CANCEL] ActionID={} | Target={:08X} | Reason=NON_PLAYER_KILL",
                    actionID,
                    targetFormID);
            }

            g_quietKillCandidates.erase(
                pendingIt);
        }

        // Dead actors no longer need transient stack state.
        g_bloodState.erase(
            targetFormID);

        g_emberState.erase(
            targetFormID);
    }

    bool CommitEmberCandidate(
        std::uint64_t a_actionID,
        RE::FormID a_directTargetFormID,
        RE::Actor* a_candidate) noexcept
    {
        if (!IsAvailable() ||
            !a_actionID ||
            !a_directTargetFormID ||
            !a_candidate ||
            a_candidate->IsDead()) {

            return false;
        }

        auto* player =
            Player();

        if (!player ||
            a_candidate ==
                player ||
            !a_candidate->
                IsHostileToActor(
                    player)) {

            return false;
        }

        const auto actionIt =
            g_actionRuntime.find(
                a_actionID);

        if (actionIt ==
            g_actionRuntime.end()) {

            return false;
        }

        const auto requestIt =
            actionIt->second.
                pendingEmber.find(
                    a_directTargetFormID);

        if (requestIt ==
                actionIt->second.
                    pendingEmber.end() ||
            !requestIt->second.
                requested) {

            return false;
        }

        const auto targetFormID =
            a_candidate->GetFormID();

        auto* oldState =
            FindActiveEmberState(
                targetFormID);

        std::uint8_t stage = 1;

        if (oldState) {
            stage =
                oldState->stage >= 3 ?
                    3 :
                    static_cast<
                        std::uint8_t>(
                            oldState->stage +
                            1);
        }

        bool useS3B{};

        if (stage == 3 &&
            oldState &&
            oldState->stage == 3) {

            useS3B =
                oldState->nextS3B;
        }

        using SpellID =
            DataCache::
                HunterSpellID;

        SpellID spellID =
            SpellID::kEmberS1;

        if (stage == 2) {
            spellID =
                SpellID::kEmberS2;
        } else if (stage == 3) {
            spellID =
                useS3B ?
                    SpellID::kEmberS3B :
                    SpellID::kEmberS3A;
        }

        const float magnitude =
            stage == 1 ?
                30.0f :
                (stage == 2 ?
                    60.0f :
                    90.0f);

        if (!CastHunterSpellToTarget(
                spellID,
                a_candidate,
                magnitude,
                "S13EMBER")) {

            return false;
        }

        auto& state =
            g_emberState[
                targetFormID];

        state.stage =
            stage;

        state.expiresAt =
            BloodClock::now() +
            std::chrono::seconds(5);

        state.nextS3B =
            stage == 3 ?
                !useS3B :
                false;

        logger::info(
            "[S13EMBER-COMMIT] ActionID={} | ImpactTarget={:08X} | Candidate={:08X} | Stage={} | Variant={} | Magnitude={:.2f} | Resistance=FireResist",
            a_actionID,
            a_directTargetFormID,
            targetFormID,
            stage,
            stage == 3 ?
                (useS3B ?
                    "S3B" :
                    "S3A") :
                (stage == 2 ?
                    "S2" :
                    "S1"),
            magnitude);

        return true;
    }

    void FinalizeEmberRequest(
        std::uint64_t a_actionID,
        RE::FormID a_directTargetFormID) noexcept
    {
        const auto actionIt =
            g_actionRuntime.find(
                a_actionID);

        if (actionIt ==
            g_actionRuntime.end()) {

            return;
        }

        actionIt->second.
            pendingEmber.erase(
                a_directTargetFormID);
    }
    bool IsComposureReady() noexcept
    {
        return
            IsAvailable() &&
            NodeActive(
                BuildState::NodeID::
                    kMarksmanComposure) &&
            StationaryForMs() >= 3000;
    }


    bool IsQuietProcessingReady() noexcept
    {
        return g_quietProcessingReady;
    }


    void RestoreQuietProcessingReady(
        bool a_ready) noexcept
    {
        g_quietProcessingReady =
            a_ready;

        TULLBUNG::PrismaBridge::NotifyHudStateChanged();

        logger::info(
            "[S17PERSIST] QuietProcessing restored | Ready={}",
            g_quietProcessingReady);
    }
}