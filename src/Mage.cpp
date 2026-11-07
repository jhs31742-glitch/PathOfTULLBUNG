#include "PCH.h"
#include "Mage.h"

#include "BuildState.h"
#include "Classification.h"
#include "DataCache.h"
#include "DamageRules.h"
#include "Inquisitor.h"
#include "Persistence.h"
#include "Snapshot.h"
#include "Targeting.h"

#include "RE/P/ProcessLists.h"
#include "RE/B/BGSExplosion.h"
#include "RE/B/BGSHazard.h"
#include "RE/T/TESObjectREFR.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
    bool g_available{};

    std::size_t
        g_learnedSpellCount{};

    inline constexpr std::size_t
        kSpellsPerStage = 20;

    inline constexpr std::size_t
        kMaxCountedSpells = 200;


    // Skyrim world-space convention used by existing Stage14 runtime:
    // 128 units ~= 6 feet. 30 feet = 640 units.
    inline constexpr float
        kMagickaCollapseRadiusUnits = 640.0f;


    [[nodiscard]] float
        ResolveCollapseDamageAgainstMagicResistance(
            RE::Actor* a_target,
            float a_rawDamage) noexcept
    {
        if (!a_target ||
            !std::isfinite(a_rawDamage) ||
            a_rawDamage <= 0.0f) {

            return 0.0f;
        }

        auto* targetAVOwner =
            a_target->AsActorValueOwner();

        if (!targetAVOwner) {
            return 0.0f;
        }

        const float magicResistance =
            std::clamp(
                targetAVOwner->GetActorValue(
                    RE::ActorValue::kResistMagic),
                -100.0f,
                85.0f);

        return
            a_rawDamage *
            (1.0f - magicResistance / 100.0f);
    }

    [[nodiscard]] float
        DamageTargetMagicka(
            RE::Actor* a_target,
            float a_requested) noexcept
    {
        if (
            !a_target ||
            !std::isfinite(
                a_requested) ||
            a_requested <= 0.0f
        ) {
            return 0.0f;
        }

        auto* owner =
            a_target->
                AsActorValueOwner();

        if (!owner) {
            return 0.0f;
        }

        const auto magicka =
            TULLBUNG::Snapshot::
                CaptureResource(
                    a_target,
                    RE::ActorValue::
                        kMagicka);

        const float applied =
            (std::min)(
                (std::max)(
                    magicka.current,
                    0.0f),
                a_requested);

        if (!std::isfinite(applied) ||
            applied <= 0.0f) {

            return 0.0f;
        }

        owner->ModActorValue(
            RE::ACTOR_VALUE_MODIFIERS::
                kDamage,
            RE::ActorValue::
                kMagicka,
            -applied);

        return applied;
    }

    [[nodiscard]]
    bool ApplyVoidErosion(
        RE::Actor* a_target,
        const TULLBUNG::Snapshot::
            ActionSnapshot&
                a_snapshot,
        std::uint64_t a_actionID,
        RE::FormID a_sourceFormID,
        const char* a_route) noexcept
    {
        if (
            !a_target ||
            a_target->IsDead() ||
            !a_snapshot.valid ||
            !a_snapshot.extended.
                internalStateKnown ||
            a_snapshot.extended.
                voidCount < 1 ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kVoidMageVoidErosion) ||
            TULLBUNG::DamageRules::
                IsTriggerSuppressed()
        ) {
            return false;
        }

        // SoT:
        // Void 0 cannot newly apply or refresh Erosion.
        //
        // ActionSnapshot preserves action-start eligibility,
        // while current persistence is the commit-time resource
        // authority. Both must still say Void is available.
        const std::uint8_t currentVoid =
            TULLBUNG::Persistence::
                GetVoidProgress().
                    voidCount;

        if (currentVoid < 1) {
            logger::info(
                "[S15VOID-EROSION] ActionID={} | Route={} | Result=VOID_ZERO | SnapshotVoid={} | CurrentVoid=0",
                a_actionID,
                a_route,
                static_cast<std::uint32_t>(
                    a_snapshot.extended.
                        voidCount));

            return false;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        auto* erosionSpell =
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kVoidErosionSpell);

        auto* caster =
            player ?
                player->GetMagicCaster(
                    RE::MagicSystem::
                        CastingSource::
                            kInstant) :
                nullptr;

        if (
            !player ||
            !erosionSpell ||
            !caster
        ) {
            logger::error(
                "[S15VOID-EROSION] ActionID={} | Route={} | Result=NO_RUNTIME_AUTHORITY | Player={} | Spell={} | Caster={}",
                a_actionID,
                a_route,
                player != nullptr,
                erosionSpell != nullptr,
                caster != nullptr);

            return false;
        }

        caster->CastSpellImmediate(
            erosionSpell,
            false,
            a_target,
            1.0f,
            false,
            50.0f,
            player);

        logger::info(
            "[S15VOID-EROSION] ActionID={} | Route={} | Result=APPLIED | Source={:08X} | Target={:08X} | SnapshotVoid={} | CurrentVoid={} | Regen=-50 | Duration=5",
            a_actionID,
            a_route,
            a_sourceFormID,
            a_target->GetFormID(),
            static_cast<std::uint32_t>(
                a_snapshot.extended.
                    voidCount),
            static_cast<std::uint32_t>(
                currentVoid));

        return true;
    }

    struct SiegeRuneSlot final
    {
        bool valid{};

        RE::NiPointer<
            RE::TESObjectREFR>
                ref{};

        RE::NiPointer<
            RE::TESObjectREFR>
                visualRef{};

        RE::NiPointer<
            RE::TESObjectREFR>
                slowRef{};

        RE::NiPoint3 center{};

        std::uint64_t actionID{};

        std::chrono::steady_clock::time_point
            createdAt{};

        std::jthread expiryWorker{};
    };


    inline constexpr std::size_t
        kSiegeRuneStorage = 5;

    inline constexpr std::size_t
        kBaseSiegeRuneCap = 3;

    inline constexpr std::size_t
        kMasterSiegeRuneCap = 5;

    // Stage15 authority: rune radius = 10 ft, diameter/width = 20 ft.
    // Preserve the established conversion: 128 units ~= 6 ft.
    inline constexpr float
        kSiegeRuneRadiusUnits =
            128.0f * (10.0f / 6.0f);

    inline constexpr float
        kSiegeRuneRadiusSquared =
            kSiegeRuneRadiusUnits *
            kSiegeRuneRadiusUnits;

    inline constexpr auto
        kSiegeRuneLifetime =
            std::chrono::seconds(30);


    std::array<
        SiegeRuneSlot,
        kSiegeRuneStorage>
            g_siegeRunes{};
    struct SiegeCastState final
    {
        bool valid{};

        TULLBUNG::Snapshot::
            CastSnapshot
                snapshot{};

        bool magickaReturnConsumed{};

        std::chrono::steady_clock::time_point
            createdAt{};
    };


    inline constexpr std::size_t
        kSiegeCastStateSlots = 16;

    


    std::array<
        SiegeCastState,
        kSiegeCastStateSlots>
            g_siegeCastStates{};

    std::size_t
        g_siegeCastStateWrite{};


    void ResetSiegeCastState() noexcept
    {
        g_siegeCastStates = {};
        g_siegeCastStateWrite = 0;
    }


    void RememberSiegeCastSnapshot(
        const TULLBUNG::Snapshot::
            CastSnapshot&
                a_snapshot) noexcept
    {
        if (
            !a_snapshot.valid ||
            !a_snapshot.actionID ||
            !a_snapshot.sourceFormID
        ) {
            return;
        }


        const auto now =
            std::chrono::steady_clock::
                now();


        // A concentration / repeated fire event may finalize
        // the same ActionID more than once.
        //
        // Update the latest snapshot but never resurrect the
        // once-per-cast Magicka Return after it was consumed.
        for (auto& slot :
             g_siegeCastStates) {

            if (!slot.valid) {
                continue;
            }

            

            if (
                slot.snapshot.actionID ==
                    a_snapshot.actionID &&
                slot.snapshot.sourceFormID ==
                    a_snapshot.sourceFormID
            ) {
                const bool consumed =
                    slot.
                        magickaReturnConsumed;

                slot.snapshot =
                    a_snapshot;

                slot.createdAt =
                    now;

                slot.magickaReturnConsumed =
                    consumed;

                return;
            }
        }


        auto& slot =
            g_siegeCastStates[
                g_siegeCastStateWrite %
                kSiegeCastStateSlots];

        ++g_siegeCastStateWrite;

        slot = {};

        slot.valid = true;

        slot.snapshot =
            a_snapshot;

        slot.magickaReturnConsumed =
            false;

        slot.createdAt =
            now;
    }


    [[nodiscard]]
    SiegeCastState*
        FindSiegeCastState(
            std::uint64_t a_actionID,
            RE::FormID a_sourceFormID) noexcept
    {
        if (
            !a_actionID ||
            !a_sourceFormID
        ) {
            return nullptr;
        }


        


        for (std::size_t offset = 0;
             offset <
                 kSiegeCastStateSlots;
             ++offset) {

            const auto index =
                (g_siegeCastStateWrite +
                 kSiegeCastStateSlots -
                 1 -
                 offset) %
                kSiegeCastStateSlots;

            auto& slot =
                g_siegeCastStates[
                    index];

            if (!slot.valid) {
                continue;
            }

            

            if (
                slot.snapshot.actionID !=
                    a_actionID ||
                slot.snapshot.sourceFormID !=
                    a_sourceFormID
            ) {
                continue;
            }

            return
                std::addressof(
                    slot);
        }

        return nullptr;
    }


    void ReleaseSiegeRuneSlot(
        SiegeRuneSlot& a_slot,
        bool a_disable) noexcept
    {
        if (a_slot.expiryWorker.joinable()) {
            a_slot.expiryWorker.request_stop();
        }

        if (
            a_slot.valid &&
            a_slot.ref &&
            a_disable
        ) {
            // Terminal world-reference cleanup.
            // Do not retain disabled HAZD refs across repeated placements.
            a_slot.ref->SetCollision(false);
            a_slot.ref->Disable();
            a_slot.ref->SetDelete(true);
        }

        if (
            a_slot.visualRef &&
            a_disable
        ) {
            a_slot.visualRef->Disable();
            a_slot.visualRef->SetDelete(true);
        }

        if (
            a_slot.slowRef &&
            a_disable
        ) {
            a_slot.slowRef->SetCollision(false);
            a_slot.slowRef->Disable();
            a_slot.slowRef->SetDelete(true);
        }

        a_slot = {};
    }


    void ResetSiegeRuneState() noexcept
    {
        std::size_t cleanupCount{};

        for (const auto& slot :
             g_siegeRunes) {

            if (slot.valid) {
                ++cleanupCount;
            }
        }

        if (cleanupCount > 0) {
            logger::info(
                "[RUNE] CLEANUP | Count={}",
                cleanupCount);
        }

        for (auto& slot :
             g_siegeRunes) {

            ReleaseSiegeRuneSlot(
                slot,
                true);
        }
    }


    void PruneExpiredSiegeRunes(
        std::chrono::steady_clock::
            time_point a_now) noexcept
    {
        for (auto& slot :
             g_siegeRunes) {

            if (!slot.valid) {
                continue;
            }

            if (
                a_now - slot.createdAt >=
                kSiegeRuneLifetime
            ) {
                // The 30-second lifetime is complete.
                // Retire the placed world reference as well as our pointer
                // so repeated rune placement cannot accumulate stale refs.
                logger::info(
                    "[RUNE] EXPIRE | ActionID={}",
                    slot.actionID);

                ReleaseSiegeRuneSlot(
                    slot,
                    true);
            }
        }
    }


    void ScheduleSiegeRuneExpiry(
        std::size_t a_slotIndex) noexcept
    {
        if (a_slotIndex >=
            g_siegeRunes.size()) {
            return;
        }

        auto& slot =
            g_siegeRunes[
                a_slotIndex];

        if (!slot.valid ||
            !slot.actionID) {
            return;
        }

        const auto actionID =
            slot.actionID;

        const auto createdAt =
            slot.createdAt;

        try {
            slot.expiryWorker =
                std::jthread(
                    [a_slotIndex,
                     actionID,
                     createdAt](
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
                            createdAt +
                                kSiegeRuneLifetime,
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
                                "[RUNE] EXPIRY_DISPATCH_SKIPPED | ActionID={} | Slot={} | Reason=NO_TASK_INTERFACE",
                                actionID,
                                a_slotIndex);
                            return;
                        }

                        taskInterface->AddTask(
                            [a_slotIndex,
                             actionID,
                             createdAt]() {

                                if (a_slotIndex >=
                                    g_siegeRunes.size()) {
                                    return;
                                }

                                auto& currentSlot =
                                    g_siegeRunes[
                                        a_slotIndex];

                                if (!currentSlot.valid ||
                                    currentSlot.actionID !=
                                        actionID ||
                                    currentSlot.createdAt !=
                                        createdAt) {
                                    return;
                                }

                                const auto now =
                                    std::chrono::
                                        steady_clock::
                                            now();

                                if (now -
                                        currentSlot.createdAt <
                                    kSiegeRuneLifetime) {
                                    return;
                                }

                                logger::info(
                                    "[RUNE] EXPIRE | ActionID={}",
                                    currentSlot.actionID);

                                ReleaseSiegeRuneSlot(
                                    currentSlot,
                                    true);
                            });
                    });
        } catch (...) {
            logger::error(
                "[RUNE] EXPIRY_SCHEDULE_FAILED | ActionID={} | Slot={}",
                actionID,
                a_slotIndex);
        }
    }
    [[nodiscard]]
    std::size_t CountActiveSiegeRunes() noexcept
    {
        std::size_t count{};

        for (const auto& slot :
             g_siegeRunes) {

            if (slot.valid) {
                ++count;
            }
        }

        return count;
    }


    [[nodiscard]]
    std::size_t FindOldestSiegeRune() noexcept
    {
        std::size_t oldest =
            g_siegeRunes.size();

        for (std::size_t i = 0;
             i < g_siegeRunes.size();
             ++i) {

            const auto& slot =
                g_siegeRunes[i];

            if (!slot.valid) {
                continue;
            }

            if (
                oldest ==
                    g_siegeRunes.size() ||
                slot.createdAt <
                    g_siegeRunes[
                        oldest].
                            createdAt
            ) {
                oldest = i;
            }
        }

        return oldest;
    }


    [[nodiscard]]
    std::size_t ResolveSiegeRuneCap() noexcept
    {
        return
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kSiegeMageMaster) ?
                kMasterSiegeRuneCap :
                kBaseSiegeRuneCap;
    }


    void TrimSiegeRunesToCap(
        std::size_t a_cap) noexcept
    {
        while (
            CountActiveSiegeRunes() >
            a_cap
        ) {
            const auto oldest =
                FindOldestSiegeRune();

            if (
                oldest >=
                g_siegeRunes.size()
            ) {
                return;
            }

            const auto oldActionID =
                g_siegeRunes[
                    oldest].
                        actionID;

            logger::info(
                "[RUNE] EVICT | Reason=CAP_TRIM | ActionID={}",
                oldActionID);

            ReleaseSiegeRuneSlot(
                g_siegeRunes[
                    oldest],
                true);

            logger::info(
                "[S15SIEGE-RUNE] Result=TRIM_CAP | OldActionID={} | Cap={} | Active={}",
                oldActionID,
                a_cap,
                CountActiveSiegeRunes());
        }
    }


    [[nodiscard]]
    bool IsTargetInsideActiveSiegeRune(
        RE::Actor* a_target,
        std::uint64_t a_excludedActionID) noexcept
    {
        if (
            !a_target ||
            a_target->IsDead()
        ) {
            return false;
        }

        PruneExpiredSiegeRunes(
            std::chrono::
                steady_clock::
                    now());

        const auto targetPosition =
            a_target->GetPosition();

        for (const auto& slot :
             g_siegeRunes) {

            if (!slot.valid) {
                continue;
            }

            // The current impact creates a rune before
            // Spell Direct resolution. Never allow that rune
            // (or another projectile from the same ActionID)
            // to satisfy the same cast.
            if (
                a_excludedActionID &&
                slot.actionID ==
                    a_excludedActionID
            ) {
                continue;
            }

            const float dx =
                targetPosition.x -
                slot.center.x;

            const float dy =
                targetPosition.y -
                slot.center.y;

            const float dz =
                targetPosition.z -
                slot.center.z;

            const float distanceSquared =
                dx * dx +
                dy * dy +
                dz * dz;

            if (
                std::isfinite(
                    distanceSquared) &&
                distanceSquared <=
                    kSiegeRuneRadiusSquared
            ) {
                return true;
            }
        }

        return false;
    }
    [[nodiscard]]
    bool PlaceSiegeRune(
        std::uint64_t a_actionID,
        RE::FormID a_sourceFormID,
        RE::TESObjectREFR* a_impactRef,
        const RE::NiPoint3&
            a_targetLoc) noexcept
    {
        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player) {
            return false;
        }


        const bool gravityShackle =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kSiegeMageGravityShackle);


        const auto hazardID =
            gravityShackle ?
                TULLBUNG::DataCache::
                    MageFamilyHazardID::
                        kSiegeRuneGravity :
                TULLBUNG::DataCache::
                    MageFamilyHazardID::
                        kSiegeRuneBase;


        auto* hazardForm =
            TULLBUNG::DataCache::
                GetMageFamilyHazard(
                    hazardID);

        auto* hazard =
            hazardForm ?
                hazardForm->
                    As<RE::BGSHazard>() :
                nullptr;

        if (!hazard) {
            logger::error(
                "[S15SIEGE-RUNE] ActionID={} | Result=NO_HAZARD | GravityShackle={}",
                a_actionID,
                gravityShackle);

            return false;
        }


        const auto slowHazardID =
            gravityShackle ?
                TULLBUNG::DataCache::
                    MageFamilyHazardID::
                        kSiegeRuneGameplaySlowGravity :
                TULLBUNG::DataCache::
                    MageFamilyHazardID::
                        kSiegeRuneGameplaySlowBase;

        auto* slowHazardForm =
            TULLBUNG::DataCache::
                GetMageFamilyHazard(
                    slowHazardID);

        auto* slowHazard =
            slowHazardForm ?
                slowHazardForm->
                    As<RE::BGSHazard>() :
                nullptr;

        if (!slowHazard) {
            logger::error(
                "[S15SIEGE-SLOW] ActionID={} | Result=NO_SLOW_HAZARD | GravityShackle={}",
                a_actionID,
                gravityShackle);

            return false;
        }


        auto* visualBase =
            TULLBUNG::DataCache::
                GetSiegeRuneVFXActivator();

        if (!visualBase) {
            logger::error(
                "[S15SIEGE-RUNE] ActionID={} | Result=NO_VISUAL_ACTI | EditorID={}",
                a_actionID,
                "TULL_SiegeMage_SiegeRune_VFX_ACTI");

            return false;
        }

        const auto now =
            std::chrono::steady_clock::
                now();

        PruneExpiredSiegeRunes(
            now);


        const auto runeCap =
            ResolveSiegeRuneCap();

        TrimSiegeRunesToCap(
            runeCap);


        if (
            CountActiveSiegeRunes() >=
            runeCap
        ) {
            const auto oldest =
                FindOldestSiegeRune();

            if (
                oldest <
                g_siegeRunes.size()
            ) {
                const auto oldActionID =
                    g_siegeRunes[
                        oldest].
                            actionID;

                logger::info(
                    "[RUNE] EVICT | Reason=CAP_REPLACE | ActionID={}",
                    oldActionID);

                ReleaseSiegeRuneSlot(
                    g_siegeRunes[
                        oldest],
                    true);

                logger::info(
                    "[S15SIEGE-RUNE] ActionID={} | Result=REPLACE_OLDEST | OldActionID={} | Cap={}",
                    a_actionID,
                    oldActionID,
                    runeCap);
            }
        }


        std::size_t freeSlot =
            g_siegeRunes.size();

        for (std::size_t i = 0;
             i < g_siegeRunes.size();
             ++i) {

            if (!g_siegeRunes[i].valid) {
                freeSlot = i;
                break;
            }
        }

        if (
            freeSlot >=
            g_siegeRunes.size()
        ) {
            logger::error(
                "[S15SIEGE-RUNE] ActionID={} | Result=NO_FREE_SLOT | Cap={}",
                a_actionID,
                runeCap);

            return false;
        }


        RE::TESObjectREFR* anchor =
            a_impactRef ?
                a_impactRef :
                player;

        // Visual-only ACTI may safely be repositioned after creation.
        // Put it at the authoritative impact point first, then use that
        // already-positioned reference as the gameplay HAZD spawn anchor.
        auto visualPlaced =
            anchor->
                PlaceObjectAtMe(
                    visualBase,
                    false);

        if (!visualPlaced) {
            logger::error(
                "[S15SIEGE-RUNE] ActionID={} | Result=VISUAL_PLACE_FAILED | Source={:08X}",
                a_actionID,
                a_sourceFormID);

            return false;
        }

        visualPlaced->SetTemporary();

        visualPlaced->SetPosition(
            a_targetLoc);

        visualPlaced->Update3DPosition(
            true);

        // Do not create a Hazard elsewhere and move it afterward.
        // Its runtime target/spatial state must be initialized at the
        // actual rune position.
        auto placed =
            visualPlaced->
                PlaceObjectAtMe(
                    hazard,
                    false);

        if (!placed) {
            visualPlaced->SetCollision(false);
            visualPlaced->Disable();
            visualPlaced->SetDelete(true);

            logger::error(
                "[S15SIEGE-RUNE] ActionID={} | Result=PLACE_FAILED | Source={:08X}",
                a_actionID,
                a_sourceFormID);

            return false;
        }

        placed->SetTemporary();


        // Gameplay slow is independent from the visible HAZD/ACTI path.
        // The visual ACTI is already at the authoritative rune center.
        // Spawn the slow HAZD from that reference so Hazard runtime state
        // is born at the final position instead of being moved afterward.
        auto slowPlaced =
            visualPlaced->
                PlaceObjectAtMe(
                    slowHazard,
                    false);

        if (!slowPlaced) {
            placed->SetCollision(false);
            placed->Disable();
            placed->SetDelete(true);

            visualPlaced->SetCollision(false);
            visualPlaced->Disable();
            visualPlaced->SetDelete(true);

            logger::error(
                "[S15SIEGE-SLOW] ActionID={} | Result=SLOW_PLACE_FAILED | Source={:08X}",
                a_actionID,
                a_sourceFormID);

            return false;
        }

        slowPlaced->SetTemporary();


        auto& slot =
            g_siegeRunes[
                freeSlot];

        slot = {};

        slot.valid = true;

        slot.ref =
            placed;

        slot.visualRef =
            visualPlaced;

        slot.slowRef =
            slowPlaced;

        slot.center =
            a_targetLoc;

        slot.actionID =
            a_actionID;

        slot.createdAt =
            now;

        ScheduleSiegeRuneExpiry(
            freeSlot);

        logger::info(
            "[RUNE] GAMEPLAY_CREATED | ActionID={} | Slot={} | WidthFt=20 | RadiusFt=10",
            a_actionID,
            freeSlot);

        logger::info(
            "[RUNE] VFX_CREATED | ActionID={} | Ref={:08X} | EditorID={} | Loc=({:.3f},{:.3f},{:.3f})",
            a_actionID,
            visualPlaced->GetFormID(),
            "TULL_SiegeMage_SiegeRune_VFX_ACTI",
            a_targetLoc.x,
            a_targetLoc.y,
            a_targetLoc.z);

        logger::info(
            "[RUNE] SLOW_CREATED | ActionID={} | Ref={:08X} | EditorID={} | Loc=({:.3f},{:.3f},{:.3f})",
            a_actionID,
            slowPlaced->GetFormID(),
            gravityShackle ?
                "TULL_SiegeMage_SiegeRune_GameplaySlow_Gravity_HAZD" :
                "TULL_SiegeMage_SiegeRune_GameplaySlow_Base_HAZD",
            a_targetLoc.x,
            a_targetLoc.y,
            a_targetLoc.z);


        logger::info(
            "[S15SIEGE-RUNE] ActionID={} | Result=PLACED | Source={:08X} | Slot={} | Active={} | Cap={} | SlowPct={} | GravityShackle={} | Lifetime=30 | WidthFt=20 | RadiusFt=10 | RadiusUnits={:.1f} | Loc=({:.3f},{:.3f},{:.3f})",
            a_actionID,
            a_sourceFormID,
            freeSlot,
            CountActiveSiegeRunes(),
            runeCap,
            gravityShackle ?
                70 :
                50,
            gravityShackle,
            kSiegeRuneRadiusUnits,
            a_targetLoc.x,
            a_targetLoc.y,
            a_targetLoc.z);

        return true;
    }
    class LearnedSpellVisitor final :
        public RE::Actor::
            ForEachSpellVisitor
    {
    public:
        RE::BSContainer::
            ForEachResult Visit(
                RE::SpellItem*
                    a_spell) override
        {
            if (
                a_spell &&
                a_spell->GetSpellType() ==
                    RE::MagicSystem::
                        SpellType::kSpell
            ) {
                ++count;
            }

            return
                RE::BSContainer::
                    ForEachResult::
                        kContinue;
        }

        std::size_t count{};
    };


    [[nodiscard]]
    std::size_t CountLearnedSpells(
        RE::Actor* a_actor) noexcept
    {
        if (!a_actor) {
            return 0;
        }

        LearnedSpellVisitor
            visitor{};

        a_actor->VisitSpells(
            visitor);

        return visitor.count;
    }


    void SetRuntimeSpell(
        RE::SpellItem* a_spell,
        bool a_enable,
        const char* a_name) noexcept
    {
        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (
            !player ||
            !a_spell
        ) {
            return;
        }

        const bool hasSpell =
            player->HasSpell(
                a_spell);

        if (
            a_enable &&
            !hasSpell
        ) {
            player->AddSpell(
                a_spell);

            logger::info(
                "[S15MAGE-PASSIVE] {} = ON",
                a_name);

        } else if (
            !a_enable &&
            hasSpell
        ) {
            player->RemoveSpell(
                a_spell);

            logger::info(
                "[S15MAGE-PASSIVE] {} = OFF",
                a_name);
        }
    }


    constexpr std::array<
        TULLBUNG::DataCache::
            MageFamilySpellID,
        10>
        kArcaneStudyAbilities{{
            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies1,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies2,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies3,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies4,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies5,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies6,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies7,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies8,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies9,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArcaneStudies10
        }};


    constexpr std::array<
        TULLBUNG::DataCache::
            MageFamilySpellID,
        10>
        kArchmageAbilities{{
            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage1,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage2,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage3,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage4,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage5,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage6,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage7,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage8,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage9,

            TULLBUNG::DataCache::
                MageFamilySpellID::
                    kArchmage10
        }};


    void SyncBarrierState(
        RE::Actor* a_player) noexcept
    {
        bool enable{};

        if (
            a_player &&
            TULLBUNG::DataCache::
                IsMageReady() &&
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kMageBarrier)
        ) {
            const auto casting =
                TULLBUNG::Snapshot::
                    CaptureCastingState(
                        a_player);

            enable =
                casting.known &&
                casting.anySpellCasting;
        }

        // Persistent Barrier presentation is owned by the constant-effect
        // MGEF lifecycle (dedicated Hit Art + FX Persist). Native code only
        // controls the existing Barrier ability state; no reference-effect
        // pointer or polling lifecycle is maintained here.
        SetRuntimeSpell(
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kBarrierAbility),
            enable,
            "Barrier");

        logger::debug(
            "[S15BARRIER-STATE] Active={}",
            enable);
    }

    void SelectTierAbility(
        const std::array<
            TULLBUNG::DataCache::
                MageFamilySpellID,
            10>& a_abilities,
        std::size_t a_tier,
        bool a_enabled,
        const char* a_name) noexcept
    {
        for (
            std::size_t i = 0;
            i < a_abilities.size();
            ++i
        ) {
            const bool enable =
                a_enabled &&
                a_tier > 0 &&
                (i + 1) == a_tier;

            SetRuntimeSpell(
                TULLBUNG::DataCache::
                    GetMageFamilySpell(
                        a_abilities[i]),
                enable,
                a_name);
        }
    }
}


namespace TULLBUNG::Classes::Mage
{
    bool Initialize() noexcept
    {
        g_available =
            TULLBUNG::DataCache::
                IsMageReady() &&
            TULLBUNG::DataCache::
                IsVoidMageReady() &&
            TULLBUNG::DataCache::
                IsSiegeMageReady();

        logger::info(
            "[Stage15] Mage family runtime = {}",
            g_available ?
                "READY" :
                "DISABLED");

        return g_available;
    }


    bool IsAvailable() noexcept
    {
        return g_available;
    }


    void ResetTransientState() noexcept
    {
        ResetSiegeRuneState();
        ResetSiegeCastState();
        RemoveRuntimePassives();

        g_learnedSpellCount = 0;

        // VOD1 is persistent.
        // Do not clear Void at load/transient boundaries.

        logger::info(
            "[Stage15] Mage family transient state reset");
    }


    void SyncRuntimePassives() noexcept
    {
        if (!IsAvailable()) {
            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player) {
            return;
        }

        if (
            TULLBUNG::BuildState::
                HasClass(
                    TULLBUNG::BuildState::
                        ClassID::
                            kSiegeMage) &&
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kSiegeMageDeployment)
        ) {
            PruneExpiredSiegeRunes(
                std::chrono::
                    steady_clock::
                        now());

            TrimSiegeRunesToCap(
                ResolveSiegeRuneCap());

        } else {
            ResetSiegeRuneState();
        }
        const bool voidRootActive =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kVoidMageVoid);

        if (!voidRootActive &&
            TULLBUNG::Persistence::
                GetVoidProgress().
                    voidCount != 0) {

            const auto previousVoid =
                TULLBUNG::Persistence::
                    GetVoidProgress().
                        voidCount;

            TULLBUNG::Persistence::
                SetVoidCount(0);

            logger::info(
                "[S15VOID-ROOT] Active=false | Void={} -> 0 | ExistingErosion=UNCHANGED",
                static_cast<std::uint32_t>(
                    previousVoid));
        }

        g_learnedSpellCount =
            CountLearnedSpells(
                player);

        const std::size_t
            countedSpells =
                (std::min)(
                    g_learnedSpellCount,
                    kMaxCountedSpells);

        const std::size_t tier =
            countedSpells /
            kSpellsPerStage;


        const bool arcaneStudies =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kMageArcaneStudies);

        const bool archmage =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kMageMaster);


        SelectTierAbility(
            kArcaneStudyAbilities,
            tier,
            arcaneStudies,
            "ArcaneStudies");

        SelectTierAbility(
            kArchmageAbilities,
            tier,
            archmage,
            "Archmage");


        // Barrier +150 is conditional on actual spell casting.
        // B2 must not latch it permanently.
        SetRuntimeSpell(
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kBarrierAbility),
            false,
            "Barrier");

        SyncBarrierState(
            player);


        const bool collapse =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kMageMagickaCollapse);

        SetRuntimeSpell(
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kMagickaCollapsePower),
            collapse,
            "MagickaCollapsePower");


        const bool voidDevour =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kVoidMageVoidDevour);

        SetRuntimeSpell(
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kVoidDevourPower),
            voidDevour,
            "VoidDevourPower");


        const bool voidMaster =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kVoidMageMaster);

        SetRuntimeSpell(
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kVoidVesselAbility),
            voidMaster,
            "VoidVessel");


        logger::info(
            "[S15MAGE-PASSIVE] LearnedSpells={} | Counted={} | Tier={} | ArcaneStudies={} | Archmage={} | CollapsePower={} | VoidDevourPower={} | VoidVessel={} | Void={}",
            g_learnedSpellCount,
            countedSpells,
            tier,
            arcaneStudies,
            archmage,
            collapse,
            voidDevour,
            voidMaster,
            static_cast<std::uint32_t>(
                GetVoidCount()));
    }


    void RemoveRuntimePassives() noexcept
    {
        if (!TULLBUNG::DataCache::
                IsMageReady()) {

            return;
        }

        for (const auto id :
             kArcaneStudyAbilities) {

            SetRuntimeSpell(
                TULLBUNG::DataCache::
                    GetMageFamilySpell(id),
                false,
                "ArcaneStudies");
        }

        for (const auto id :
             kArchmageAbilities) {

            SetRuntimeSpell(
                TULLBUNG::DataCache::
                    GetMageFamilySpell(id),
                false,
                "Archmage");
        }

        SetRuntimeSpell(
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kBarrierAbility),
            false,
            "Barrier");

        SetRuntimeSpell(
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kMagickaCollapsePower),
            false,
            "MagickaCollapsePower");

        SetRuntimeSpell(
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kVoidDevourPower),
            false,
            "VoidDevourPower");

        SetRuntimeSpell(
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kVoidVesselAbility),
            false,
            "VoidVessel");
    }


    void OnSpellLearned(
        RE::SpellItem* a_spell) noexcept
    {
        if (
            !IsAvailable() ||
            !a_spell ||
            a_spell->GetSpellType() !=
                RE::MagicSystem::
                    SpellType::kSpell
        ) {
            return;
        }

        logger::info(
            "[S15MAGE-LEARNED] Spell={:08X}",
            a_spell->GetFormID());

        SyncRuntimePassives();
    }


    void OnPlayerWeaponDirectHit(
        const Combat::CombatContext&
            a_context,
        RE::HitData& a_data) noexcept
    {
        if (
            !IsAvailable() ||
            !a_context.playerAttacker ||
            !a_context.target ||
            !a_context.weapon ||
            !a_context.weaponDirectCandidate ||
            !a_context.directDamageKnown ||
            !std::isfinite(
                a_data.totalDamage) ||
            a_data.totalDamage <= 0.0f ||
            TULLBUNG::DamageRules::
                IsTriggerSuppressed()
        ) {
            return;
        }

        const float directDamage =
            a_data.totalDamage;

        const auto& snapshot =
            a_context.actionSnapshot;


        // ----------------------------------------------------
        // 공허 침식 — Weapon Direct
        //
        // No target search:
        // CombatContext actual hit target is authoritative.
        //
        // Do this before Void Strike commits its resource so
        // the current hit uses the same pre-commit Void state.
        // ----------------------------------------------------

        (void)ApplyVoidErosion(
            a_context.target,
            snapshot,
            a_context.attackActionID,
            a_context.weapon->
                GetFormID(),
            "WEAPON");


        // ----------------------------------------------------
        // 공허강타
        //
        // Eligibility is sampled from the immutable action
        // snapshot. firstValidDirectHit is the existing common
        // once-per-attack-action authority.
        // ----------------------------------------------------

        const bool voidStrikeActive =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kVoidMageVoidStrike);

        const bool meleePowerHit =
            a_context.range ==
                Combat::RangeKind::
                    kMelee &&
            a_context.attackKind ==
                Combat::AttackKind::
                    kPower &&
            TULLBUNG::Classification::
                IsMelee(
                    a_context.weapon);

        if (
            voidStrikeActive &&
            meleePowerHit &&
            a_context.firstValidDirectHit
        ) {
            const bool snapshotVoidReady =
                snapshot.valid &&
                snapshot.extended.
                    internalStateKnown &&
                snapshot.extended.
                    voidCount >= 1;

            if (!snapshotVoidReady) {
                logger::info(
                    "[S15VOID-STRIKE] ActionID={} | Result=NO_SNAPSHOT_VOID | SnapshotValid={} | InternalKnown={} | SnapshotVoid={}",
                    a_context.attackActionID,
                    snapshot.valid,
                    snapshot.extended.
                        internalStateKnown,
                    static_cast<std::uint32_t>(
                        snapshot.extended.
                            voidCount));
            } else {
                const std::uint8_t
                    currentVoid =
                        GetVoidCount();

                if (currentVoid < 1) {
                    logger::info(
                        "[S15VOID-STRIKE] ActionID={} | Result=COMMIT_NO_VOID | SnapshotVoid={} | CurrentVoid=0",
                        a_context.attackActionID,
                        static_cast<std::uint32_t>(
                            snapshot.extended.
                                voidCount));
                } else {
                    TULLBUNG::Persistence::
                        SetVoidCount(
                            static_cast<
                                std::uint8_t>(
                                    currentVoid -
                                    1));

                    const float applied =
                        DamageTargetMagicka(
                            a_context.target,
                            directDamage);

                    logger::info(
                        "[S15VOID-STRIKE] ActionID={} | Result=APPLIED | Target={:08X} | Direct={:.3f} | RequestedBurn={:.3f} | AppliedBurn={:.3f} | Void={} -> {}",
                        a_context.attackActionID,
                        a_context.target->
                            GetFormID(),
                        directDamage,
                        directDamage,
                        applied,
                        static_cast<std::uint32_t>(
                            currentVoid),
                        static_cast<std::uint32_t>(
                            currentVoid - 1));
                }
            }
        }


        // ----------------------------------------------------
        // 공허 무구
        //
        // Independent from Void Strike:
        // a bound melee power attack may satisfy both nodes.
        // Void Armament itself consumes no Void.
        // ----------------------------------------------------

        const bool voidArmamentActive =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kVoidMageVoidArmament);

        if (
            voidArmamentActive &&
            TULLBUNG::Classification::
                IsBound(
                    a_context.weapon)
        ) {
            const float applied =
                DamageTargetMagicka(
                    a_context.target,
                    directDamage);

            logger::info(
                "[S15VOID-ARMAMENT] ActionID={} | Target={:08X} | Direct={:.3f} | RequestedBurn={:.3f} | AppliedBurn={:.3f}",
                a_context.attackActionID,
                a_context.target->
                    GetFormID(),
                directDamage,
                directDamage,
                applied);
        }
    }


    void OnPlayerCastFinalized(
        const TULLBUNG::Snapshot::
            CastSnapshot&
                a_snapshot) noexcept
    {
        if (
            !IsAvailable() ||
            !a_snapshot.valid ||
            !a_snapshot.actionID ||
            !a_snapshot.sourceFormID ||
            !a_snapshot.siegeStateKnown ||
            !a_snapshot.siegeDeploymentActive ||
            !TULLBUNG::BuildState::
                HasClass(
                    TULLBUNG::BuildState::
                        ClassID::
                            kSiegeMage)
        ) {
            return;
        }

        RememberSiegeCastSnapshot(
            a_snapshot);

        logger::debug(
            "[S15SIEGE-CAST] ActionID={} | Spell={:08X} | ActualSpentKnown={} | ActualSpent={:.3f}",
            a_snapshot.actionID,
            a_snapshot.sourceFormID,
            a_snapshot.
                observedMagickaDeltaKnown,
            a_snapshot.
                observedMagickaDelta);
    }


    float ResolveSiegeSpellDirectBonusPercent(
        const RE::SpellItem* a_spell,
        RE::Actor* a_target,
        const TULLBUNG::Snapshot::
            ActionSnapshot&
                a_snapshot) noexcept
    {
        if (
            !IsAvailable() ||
            !a_spell ||
            !a_target ||
            !a_snapshot.valid ||
            !a_snapshot.actionID ||
            !TULLBUNG::BuildState::
                HasClass(
                    TULLBUNG::BuildState::
                        ClassID::
                            kSiegeMage) ||
            !TULLBUNG::Classification::
                IsProjectileDestructionSpell(
                    a_spell) ||
            TULLBUNG::DamageRules::
                IsTriggerSuppressed()
        ) {
            return 0.0f;
        }


        float additivePercent{};


        const bool siegeDeployment =
            a_snapshot.extended.
                siegeStateKnown &&
            a_snapshot.extended.
                siegeDeploymentActive;


        const bool ballistics =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kSiegeMageBallistics);


        if (
            siegeDeployment &&
            ballistics
        ) {
            additivePercent +=
                20.0f;
        }


        const bool insideExistingRune =
            IsTargetInsideActiveSiegeRune(
                a_target,
                a_snapshot.actionID);


        const bool focusedBarrage =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kSiegeMageFocusedBarrage);


        if (
            focusedBarrage &&
            insideExistingRune
        ) {
            additivePercent +=
                30.0f;
        }


        if (additivePercent > 0.0f) {
            logger::info(
                "[S15SIEGE-DIRECT] ActionID={} | Spell={:08X} | Target={:08X} | SiegeDeployment={} | ExistingRune={} | Ballistics={} | FocusedBarrage={} | Additive={:.1f}",
                a_snapshot.actionID,
                a_spell->GetFormID(),
                a_target->GetFormID(),
                siegeDeployment,
                insideExistingRune,
                ballistics,
                focusedBarrage,
                additivePercent);
        }


        return additivePercent;
    }


    static void TryApplySiegeMagickaReturn(
        const RE::SpellItem* a_spell,
        RE::Actor* a_target,
        const TULLBUNG::Snapshot::
            ActionSnapshot&
                a_snapshot) noexcept
    {
        if (
            !IsAvailable() ||
            !a_spell ||
            !a_target ||
            !a_snapshot.valid ||
            !a_snapshot.actionID ||
            !TULLBUNG::BuildState::
                HasClass(
                    TULLBUNG::BuildState::
                        ClassID::
                            kSiegeMage) ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kSiegeMageMagickaReturn) ||
            !TULLBUNG::Classification::
                IsProjectileDestructionSpell(
                    a_spell) ||
            TULLBUNG::DamageRules::
                IsTriggerSuppressed()
        ) {
            return;
        }


        // 마력 환류 requires the cast to have entered
        // 공성 전개 at the immutable action boundary.
        if (
            !a_snapshot.extended.
                siegeStateKnown ||
            !a_snapshot.extended.
                siegeDeploymentActive
        ) {
            return;
        }


        // The current Action's newly-created rune is excluded.
        if (
            !IsTargetInsideActiveSiegeRune(
                a_target,
                a_snapshot.actionID)
        ) {
            return;
        }


        auto* castState =
            FindSiegeCastState(
                a_snapshot.actionID,
                a_spell->GetFormID());


        if (!castState) {
            logger::info(
                "[S15SIEGE-RETURN] ActionID={} | Spell={:08X} | Target={:08X} | Result=NO_CAST_SNAPSHOT",
                a_snapshot.actionID,
                a_spell->GetFormID(),
                a_target->GetFormID());

            return;
        }


        if (
            castState->
                magickaReturnConsumed
        ) {
            return;
        }


        const auto& castSnapshot =
            castState->snapshot;


        if (
            !castSnapshot.valid ||
            castSnapshot.actionID !=
                a_snapshot.actionID ||
            castSnapshot.sourceFormID !=
                a_spell->GetFormID() ||
            !castSnapshot.siegeStateKnown ||
            !castSnapshot.
                siegeDeploymentActive ||
            !castSnapshot.
                observedMagickaDeltaKnown ||
            !std::isfinite(
                castSnapshot.
                    observedMagickaDelta) ||
            castSnapshot.
                observedMagickaDelta <=
                    0.0f
        ) {
            return;
        }


        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player) {
            return;
        }


        auto* owner =
            player->
                AsActorValueOwner();

        if (!owner) {
            return;
        }


        const auto magicka =
            TULLBUNG::Snapshot::
                CaptureResource(
                    player,
                    RE::ActorValue::
                        kMagicka);


        const float actualSpent =
            castSnapshot.
                observedMagickaDelta;

        const float requestedRefund =
            actualSpent *
            0.50f;


        const float availableRoom =
            (std::max)(
                0.0f,
                magicka.maximum -
                magicka.current);

        const float appliedRefund =
            (std::min)(
                requestedRefund,
                availableRoom);


        if (
            !std::isfinite(
                requestedRefund) ||
            requestedRefund <=
                0.0f ||
            !std::isfinite(
                appliedRefund) ||
            appliedRefund <
                0.0f
        ) {
            return;
        }


        // Once-per-cast is committed by the first otherwise
        // valid qualifying hit, even if current MP is already
        // full and actual applied refund becomes zero.
        castState->
            magickaReturnConsumed =
                true;


        if (appliedRefund > 0.0f) {
            // Existing Mage resource code damages Magicka by
            // applying a negative kDamage delta.
            // Positive kDamage therefore restores that damage.
            owner->
                ModActorValue(
                    RE::ACTOR_VALUE_MODIFIERS::
                        kDamage,
                    RE::ActorValue::
                        kMagicka,
                    appliedRefund);
        }


        const auto after =
            TULLBUNG::Snapshot::
                CaptureResource(
                    player,
                    RE::ActorValue::
                        kMagicka);


        logger::info(
            "[S15SIEGE-RETURN] ActionID={} | Spell={:08X} | Target={:08X} | ActualSpent={:.3f} | Requested50={:.3f} | Applied={:.3f} | MP={:.3f}->{:.3f} | Result=COMMIT",
            a_snapshot.actionID,
            a_spell->GetFormID(),
            a_target->GetFormID(),
            actualSpent,
            requestedRefund,
            appliedRefund,
            magicka.current,
            after.current);
    }
    void OnPlayerSpellDirectHit(
        const RE::SpellItem* a_spell,
        RE::Actor* a_target,
        Combat::Hand a_hand,
        const TULLBUNG::Snapshot::
            ActionSnapshot&
                a_snapshot) noexcept
    {
        (void)a_hand;

        if (
            !IsAvailable() ||
            !a_spell ||
            !a_target
        ) {
            return;
        }

        // This route is invoked only after Stage14
        // SpellImpactAuthority has authorized the actual
        // player Spell Direct target and ActionSnapshot.
        (void)ApplyVoidErosion(
            a_target,
            a_snapshot,
            a_snapshot.actionID,
            a_spell->GetFormID(),
            "SPELL");
        TryApplySiegeMagickaReturn(
            a_spell,
            a_target,
            a_snapshot);
    }

    void OnPlayerProjectileImpact(
        std::uint64_t a_actionID,
        RE::FormID a_sourceFormID,
        RE::TESObjectREFR* a_impactRef,
        const RE::NiPoint3& a_targetLoc,
        const TULLBUNG::Snapshot::
            ActionSnapshot&
                a_snapshot) noexcept
    {
        if (
            !IsAvailable() ||
            !a_actionID ||
            !a_sourceFormID ||
            !a_snapshot.valid ||
            a_snapshot.actionID !=
                a_actionID ||
            !a_snapshot.extended.
                siegeStateKnown ||
            !a_snapshot.extended.
                siegeDeploymentActive ||
            TULLBUNG::DamageRules::
                IsTriggerSuppressed()
        ) {
            return;
        }

        // Commit-time class authority.
        // ActionSnapshot already preserves the stationary state
        // sampled when the spell action began.
        if (
            !TULLBUNG::BuildState::
                HasClass(
                    TULLBUNG::BuildState::
                        ClassID::
                            kSiegeMage)
        ) {
            return;
        }

        if (
            !std::isfinite(
                a_targetLoc.x) ||
            !std::isfinite(
                a_targetLoc.y) ||
            !std::isfinite(
                a_targetLoc.z)
        ) {
            logger::info(
                "[S15SIEGE-RUNE] ActionID={} | Result=INVALID_IMPACT_LOCATION",
                a_actionID);

            return;
        }

        (void)PlaceSiegeRune(
            a_actionID,
            a_sourceFormID,
            a_impactRef,
            a_targetLoc);
    }
    void OnPlayerVoiceFire(
        RE::Actor* a_player) noexcept
    {
        if (
            !IsAvailable() ||
            !a_player ||
            TULLBUNG::DamageRules::
                IsTriggerSuppressed()
        ) {
            return;
        }

        auto* selectedPower =
            a_player->
                GetActorRuntimeData().
                    selectedPower;

        if (!selectedPower) {
            return;
        }


        // ====================================================
        // 마력 붕괴
        // ====================================================

        auto* collapsePower =
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kMagickaCollapsePower);

        if (
            collapsePower &&
            selectedPower ==
                collapsePower
        ) {
            if (
                !TULLBUNG::BuildState::
                    IsNodeActive(
                        TULLBUNG::BuildState::
                            NodeID::
                                kMageMagickaCollapse)
            ) {
                return;
            }

            auto* playerAVOwner =
                a_player->
                    AsActorValueOwner();

            auto* processLists =
                RE::ProcessLists::
                    GetSingleton();

            if (
                !playerAVOwner ||
                !processLists
            ) {
                logger::error(
                    "[S15COLLAPSE] Result=NO_RUNTIME_AUTHORITY");

                return;
            }

            const float magickaSpent =
                playerAVOwner->
                    GetActorValue(
                        RE::ActorValue::
                            kMagicka);

            if (
                !std::isfinite(
                    magickaSpent) ||
                magickaSpent <= 0.0f
            ) {
                logger::info(
                    "[S15COLLAPSE] Result=NO_MAGICKA | MPSpent=0");

                return;
            }

            playerAVOwner->
                ModActorValue(
                    RE::ACTOR_VALUE_MODIFIERS::
                        kDamage,
                    RE::ActorValue::
                        kMagicka,
                    -magickaSpent);


            std::uint32_t
                candidateCount{};

            std::uint32_t
                appliedCount{};

            processLists->
                ForEachHighActor(
                    [&](RE::Actor*
                            a_candidate) {
                        if (
                            !a_candidate ||
                            a_candidate ==
                                a_player ||
                            a_candidate->
                                IsDead() ||
                            !a_candidate->
                                IsHostileToActor(
                                    a_player)
                        ) {
                            return
                                RE::BSContainer::
                                    ForEachResult::
                                        kContinue;
                        }

                        const float distance =
                            a_player->
                                GetDistance(
                                    a_candidate);

                        if (
                            !std::isfinite(
                                distance) ||
                            distance >
                                kMagickaCollapseRadiusUnits
                        ) {
                            return
                                RE::BSContainer::
                                    ForEachResult::
                                        kContinue;
                        }

                        ++candidateCount;

                        const float
                            resolvedDamage =
                                ResolveCollapseDamageAgainstMagicResistance(
                                    a_candidate,
                                    magickaSpent);

                        if (
                            TULLBUNG::
                                DamageRules::
                                    ApplyResolvedHealthDamage(
                                        a_player,
                                        a_candidate,
                                        resolvedDamage,
                                        TULLBUNG::
                                            DamageRules::
                                                FirewallOrigin::
                                                    kSpecialDamage)
                        ) {
                            ++appliedCount;
                        }

                        return
                            RE::BSContainer::
                                ForEachResult::
                                    kContinue;
                    });

            // Magicka Collapse VFX:
            // Spawn a fresh Explosion reference for every successful use.
            // Never retain or reuse the created reference.
            //
            // This intentionally avoids the shared remote-caster path used
            // by Smite/Void VFX so consecutive Collapse activations cannot
            // depend on replaying one caster/effect instance.
            auto* collapseExplosion =
                RE::TESForm::
                    LookupByEditorID<
                        RE::BGSExplosion>(
                            "TULL_Mage_MagickaCollapse_Explosion");

            RE::NiPointer<
                RE::TESObjectREFR>
                    collapseExplosionRef{};

            if (collapseExplosion) {
                collapseExplosionRef =
                    a_player->
                        PlaceObjectAtMe(
                            collapseExplosion,
                            false);
            }

            logger::info(
                "[S15COLLAPSE-VFX] Result={} | Base={:08X} | Ref={:08X}",
                collapseExplosionRef ?
                    "PLACED" :
                    collapseExplosion ?
                        "PLACE_FAILED" :
                        "BASE_MISSING",
                collapseExplosion ?
                    collapseExplosion->
                        GetFormID() :
                    0,
                collapseExplosionRef ?
                    collapseExplosionRef->
                        GetFormID() :
                    0);


            // R5: one-shot VFX observation; no gameplay mutation.
            if (collapseExplosion) {
                const char* modelPath = collapseExplosion->GetModel();
                logger::info(
                    "[S15COLLAPSE-R5] Phase=MODEL | Base={:08X} | Model={}",
                    collapseExplosion->GetFormID(),
                    modelPath ? modelPath : "NULL");
            }

            if (collapseExplosionRef) {
                const auto placedPosition =
                    collapseExplosionRef->GetPosition();
                const auto playerPosition = a_player->GetPosition();
                const auto* placedCell =
                    collapseExplosionRef->GetParentCell();
                const auto placedID =
                    collapseExplosionRef->GetFormID();

                logger::info(
                    "[S15COLLAPSE-R5] Phase=IMMEDIATE | Ref={:08X} | Cell={:08X} | RefZ={:.2f} | PlayerZ={:.2f} | Has3D={}",
                    placedID,
                    placedCell ? placedCell->GetFormID() : 0,
                    placedPosition.z,
                    playerPosition.z,
                    collapseExplosionRef->Get3D() != nullptr);

                const auto weakHandle =
                    collapseExplosionRef->CreateRefHandle();

                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask(
                        [weakHandle, placedID]() {
                            auto deferredRef = weakHandle.get();

                            if (!deferredRef) {
                                logger::info(
                                    "[S15COLLAPSE-R5] Phase=DEFERRED | Ref={:08X} | Resolved=false",
                                    placedID);
                                return;
                            }

                            const auto deferredPosition =
                                deferredRef->GetPosition();
                            const auto* deferredCell =
                                deferredRef->GetParentCell();

                            logger::info(
                                "[S15COLLAPSE-R5] Phase=DEFERRED | Ref={:08X} | Resolved=true | Cell={:08X} | RefZ={:.2f} | Has3D={}",
                                placedID,
                                deferredCell ?
                                    deferredCell->GetFormID() : 0,
                                deferredPosition.z,
                                deferredRef->Get3D() != nullptr);
                        });
                } else {
                    logger::info(
                        "[S15COLLAPSE-R5] Phase=DEFERRED | Ref={:08X} | Result=NO_TASK_INTERFACE",
                        placedID);
                }
            }

            logger::info(
                "[S15COLLAPSE] Result=APPLIED | MPSpent={:.2f} | RadiusUnits={:.1f} | Candidates={} | Applied={}",
                magickaSpent,
                kMagickaCollapseRadiusUnits,
                candidateCount,
                appliedCount);

            return;
        }


        // ====================================================
        // 공허 포식
        //
        // 60 ft = 1280 Skyrim units.
        //
        // Target:
        // 1. alive hostile within radius
        // 2. highest CURRENT Magicka
        // 3. tie -> nearest
        //
        // No LOS condition is added because SoT does not
        // specify one for Void Devour.
        // ====================================================

        auto* voidDevourPower =
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kVoidDevourPower);

        if (
            !voidDevourPower ||
            selectedPower !=
                voidDevourPower
        ) {
            return;
        }

        if (
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kVoidMageVoidDevour)
        ) {
            return;
        }

        const std::uint8_t
            currentVoid =
                GetVoidCount();

        if (currentVoid == 0) {
            logger::info(
                "[S15VOID-DEVOUR] Result=NO_VOID");

            return;
        }

        constexpr float
            kVoidDevourRadiusUnits =
                1280.0f;

        const auto targetSelection =
            TULLBUNG::Targeting::
                FindHighestCurrentMagickaHostile(
                    a_player,
                    kVoidDevourRadiusUnits);

        if (!targetSelection.
                processListsAvailable) {

            logger::error(
                "[S15VOID-DEVOUR] Result=NO_PROCESS_LISTS");

            return;
        }

        auto* bestTarget =
            targetSelection.actor;

        const float bestMagicka =
            targetSelection.score;

        const float bestDistance =
            targetSelection.distance;

        if (!bestTarget) {
            // No valid target:
            // no gameplay effect was committed, therefore the
            // persistent Void resource is left unchanged.
            logger::info(
                "[S15VOID-DEVOUR] Result=NO_TARGET | Void={}",
                static_cast<
                    std::uint32_t>(
                        currentVoid));

            return;
        }


        const std::uint8_t
            effectiveVoid =
                (std::min)(
                    currentVoid,
                    static_cast<
                        std::uint8_t>(
                            4));

        const float percent =
            static_cast<float>(
                effectiveVoid) *
            0.25f;

        const float
            uncappedAmount =
                bestMagicka *
                percent;

        const float
            healthDamage =
                (std::min)(
                    uncappedAmount,
                    5000.0f);


        // Commit the complete Void resource exactly once.
        TULLBUNG::Persistence::
            SetVoidCount(
                0);


        // Magicka burn intentionally uses the UN-CAPPED
        // amount. The 5000 cap belongs only to pure Health
        // damage.
        const float
            appliedBurn =
                DamageTargetMagicka(
                    bestTarget,
                    uncappedAmount);


        bool healthApplied{};

        if (
            std::isfinite(
                healthDamage) &&
            healthDamage > 0.0f
        ) {
            healthApplied =
                TULLBUNG::DamageRules::
                    ApplyResolvedHealthDamage(
                        a_player,
                        bestTarget,
                        healthDamage,
                        TULLBUNG::DamageRules::
                            FirewallOrigin::
                                kSpecialDamage);
        }

        auto* devourTargetVfxSpell =
            TULLBUNG::DataCache::
                GetMageFamilySpell(
                    TULLBUNG::DataCache::
                        MageFamilySpellID::
                            kVoidDevourTargetVFX);

        const bool devourTargetVfx =
            TULLBUNG::Classes::
                Inquisitor::DispatchSharedVFX(
                    a_player,
                    bestTarget,
                    devourTargetVfxSpell);

        logger::info(
            "[S15MAGE-VFX] Kind=VOID_DEVOUR | Cast=ESP_MGEF | TargetImpact={} | Target={:08X}",
            devourTargetVfx,
            bestTarget->GetFormID());


        logger::info(
            "[S15VOID-DEVOUR] Result=APPLIED | Target={:08X} | Void={} -> 0 | TargetMP={:.3f} | Percent={:.2f} | RawAmount={:.3f} | HealthRequested={:.3f} | HealthApplied={} | MagickaBurn={:.3f} | Distance={:.2f}",
            bestTarget->
                GetFormID(),
            static_cast<
                std::uint32_t>(
                    currentVoid),
            bestMagicka,
            percent,
            uncappedAmount,
            healthDamage,
            healthApplied,
            appliedBurn,
            bestDistance);
    }

    void OnPlayerCastingStateChanged(
        RE::Actor* a_actor) noexcept
    {
        if (!IsAvailable() ||
            !a_actor) {

            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            a_actor != player) {

            return;
        }

        SyncBarrierState(
            player);
    }


    void OnPlayerMagickaMutation(
        float a_beforeCurrent,
        float a_beforeMaximum,
        float a_afterCurrent,
        float a_afterMaximum) noexcept
    {
        if (!TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kVoidMageVoid)) {

            return;
        }

        if (
            !IsAvailable() ||
            !TULLBUNG::BuildState::
                HasClass(
                    TULLBUNG::BuildState::
                        ClassID::
                            kVoidMage)
        ) {
            return;
        }

        if (
            !std::isfinite(
                a_beforeCurrent) ||
            !std::isfinite(
                a_beforeMaximum) ||
            !std::isfinite(
                a_afterCurrent) ||
            !std::isfinite(
                a_afterMaximum) ||
            a_beforeMaximum <= 0.0f ||
            a_afterMaximum <= 0.0f
        ) {
            return;
        }

        // A <=25% crossing grants Void only when current Magicka actually decreased.
        // Max-Magicka changes alone must not create the resource.
        if (a_afterCurrent >= a_beforeCurrent) {
            return;
        }

        const float beforeFraction =
            a_beforeCurrent /
            a_beforeMaximum;

        const float afterFraction =
            a_afterCurrent /
            a_afterMaximum;

        // Void is gained only on an actual transition:
        //
        //   >25%  ->  <=25%
        //
        // Staying below the threshold cannot repeatedly grant Void.
        if (
            beforeFraction <= 0.25f ||
            afterFraction > 0.25f
        ) {
            return;
        }

        const std::uint8_t current =
            GetVoidCount();

        if (current >= 4) {
            logger::info(
                "[S15VOID-GAIN] Result=CAP | Before={:.3f} | After={:.3f} | Void=4",
                beforeFraction,
                afterFraction);

            return;
        }

        const bool vessel =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kVoidMageMaster);

        const std::uint8_t gain =
            vessel ?
                2 :
                1;

        const std::uint8_t next =
            static_cast<std::uint8_t>(
                (std::min)(
                    static_cast<unsigned int>(
                        current) +
                    static_cast<unsigned int>(
                        gain),
                    4u));

        TULLBUNG::Persistence::
            SetVoidCount(
                next);

        logger::info(
            "[S15VOID-GAIN] Result=GAIN | Before={:.3f} | After={:.3f} | Gain={} | Void={} -> {} | Vessel={}",
            beforeFraction,
            afterFraction,
            static_cast<std::uint32_t>(
                gain),
            static_cast<std::uint32_t>(
                current),
            static_cast<std::uint32_t>(
                next),
            vessel);
    }


    void PopulateExtendedActionState(
        RE::Actor* a_actor,
        TULLBUNG::Snapshot::
            ExtendedActionState&
                a_state) noexcept
    {
        if (!IsAvailable() ||
            !a_actor) {

            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            a_actor != player) {

            return;
        }

        a_state.voidCount =
            GetVoidCount();

        a_state.internalStateKnown =
            true;

        a_state.siegeDeploymentActive =
            TULLBUNG::BuildState::
                HasClass(
                    TULLBUNG::BuildState::
                        ClassID::
                            kSiegeMage) &&
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kSiegeMageDeployment) &&
            !player->IsMoving();

        a_state.siegeStateKnown =
            true;
    }

    std::size_t
        GetLearnedSpellCount() noexcept
    {
        return g_learnedSpellCount;
    }


    std::uint8_t
        GetVoidCount() noexcept
    {
        return
            TULLBUNG::Persistence::
                GetVoidProgress().
                    voidCount;
    }
}
