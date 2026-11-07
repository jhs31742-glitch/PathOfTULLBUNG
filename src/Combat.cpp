#include "PCH.h"

#include "Combat.h"
#include "Hunter.h"
#include "Classification.h"
#include "Snapshot.h"
#include "DamageRules.h"
#include "ClassRuntime.h"
#include "BuildState.h"
#include "PlayerState.h"
#include "DataCache.h"

#include "RE/B/BGSAttackData.h"
#include "RE/B/BGSCollisionLayer.h"
#include "RE/B/BSAnimationGraphEvent.h"
#include "RE/B/bhkCharacterController.h"
#include "RE/B/bhkPickData.h"
#include "RE/B/bhkWorld.h"
#include "RE/E/ExtraPoison.h"
#include "RE/H/HitData.h"
#include "RE/H/hkpCollidable.h"
#include "RE/H/hkpWorldObject.h"
#include "RE/H/hkpWorld.h"
#include "RE/H/hkpWorldRayCastInput.h"
#include "RE/H/hkpWorldRayCastOutput.h"
#include "RE/I/InventoryEntryData.h"
#include "RE/B/BSAtomic.h"
#include "RE/P/Projectile.h"
#include "RE/B/BeamProjectile.h"
#include "RE/G/GrenadeProjectile.h"
#include "RE/M/MissileProjectile.h"
#include "RE/T/TES.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/A/AccumulatingValueModifierEffect.h"
#include "RE/D/DualValueModifierEffect.h"
#include "RE/V/ValueModifierEffect.h"
#include "RE/V/ValueAndConditionsEffect.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "RE/P/PeakValueModifierEffect.h"

namespace
{
    using namespace TULLBUNG::Combat;
    namespace Snapshot = TULLBUNG::Snapshot;

    inline constexpr std::uint32_t kHitDataOffhandMask =
        1u << 17;

    bool g_registered{};
    bool g_hitHookInstalled{};
    bool g_healthDamageProbeHookInstalled{};
    bool g_castingLifecycleHookInstalled{};
    bool g_playerActorValueHookInstalled{};

    std::uint64_t g_rawEventSequence{};
    std::uint64_t g_healthDamageProbeSequence{};
    std::uint64_t g_nextActionID{ 1 };

    AttackActionState g_rightAction{};
    AttackActionState g_leftAction{};
    AttackActionState g_bothAction{};

    AttackActionState g_eventAction{};
    AttackActionState g_bashAction{};

    enum class S19QaValueKind : std::uint8_t
    {
        kHealing,
        kSpellDirect
    };

    // Diagnostic-only, fixed-size, nonblocking sampling. No engine
    // state or gameplay return value depends on this cache.
    constexpr std::size_t kS19QaValueTraceSlots = 256;
    constexpr std::uint32_t kS19QaValueTraceMaxSamples = 512;
    std::array<std::atomic<std::uint64_t>, kS19QaValueTraceSlots>
        g_s19QaValueTraceKeys{};
    std::atomic<std::uint32_t> g_s19QaHealingTraceCount{};
    std::atomic<std::uint32_t> g_s19QaDirectTraceCount{};

    [[nodiscard]] bool LogS19QaValueFirst(
        S19QaValueKind a_kind,
        std::uint64_t a_actionID,
        RE::FormID a_spellID,
        RE::FormID a_targetID) noexcept
    {
        if (!a_actionID || !a_spellID || !a_targetID) {
            return false;
        }
        auto& count = a_kind == S19QaValueKind::kHealing ?
            g_s19QaHealingTraceCount : g_s19QaDirectTraceCount;
        if (count.load(std::memory_order_relaxed) >=
            kS19QaValueTraceMaxSamples) {
            return false;
        }

        std::uint64_t key = 14695981039346656037ULL;
        const auto mix = [&key](std::uint64_t a_part) noexcept {
            key ^= a_part;
            key *= 1099511628211ULL;
        };
        mix(static_cast<std::uint64_t>(a_kind));
        mix(a_actionID);
        mix(static_cast<std::uint64_t>(a_spellID));
        mix(static_cast<std::uint64_t>(a_targetID));
        if (key == 0) {
            key = 1;
        }

        const auto index = static_cast<std::size_t>(
            key % kS19QaValueTraceSlots);
        if (g_s19QaValueTraceKeys[index].exchange(
                key, std::memory_order_relaxed) == key) {
            return false;
        }

        // Hard session cap; collisions can cause extra samples,
        // therefore this is best-effort, not exhaustive first-only.
        return count.fetch_add(1, std::memory_order_relaxed) <
            kS19QaValueTraceMaxSamples;
    }

    void ResetS19QaValueTrace() noexcept
    {
        for (auto& key : g_s19QaValueTraceKeys) {
            key.store(0, std::memory_order_relaxed);
        }
        g_s19QaHealingTraceCount.store(
            0, std::memory_order_relaxed);
        g_s19QaDirectTraceCount.store(
            0, std::memory_order_relaxed);
    }


    bool g_leftAttackHeld{};
    bool g_rightAttackHeld{};

    std::uint64_t g_leftAttackGeneration{};
    std::uint64_t g_rightAttackGeneration{};
    std::uint64_t g_bashInputGeneration{};

    bool g_sprintInputHeld{};


    Snapshot::CastSnapshot g_castTransaction{};

    std::unordered_map<
        RE::FormID,
        RE::TESForm*> g_formCache{};

    struct RangedLaunchState final
    {
        bool valid{};
        std::uint64_t actionID{};
        RE::FormID sourceFormID{};
        RE::NiPoint3 position{};
    };

    struct PendingWeaponDirectHitState final
    {
        bool valid{};

        std::uint64_t actionID{};
        RE::FormID targetFormID{};
        RE::ActorHandle targetHandle{};

        bool ranged{};
        RE::NiPoint3 hitPosition{};

        bool launchKnown{};
        float distanceFt{};
    };
    RangedLaunchState g_rangedLaunchState{};
    thread_local PendingWeaponDirectHitState
        g_pendingWeaponDirectHit{};

    struct PendingBarrierWeaponReflectState final
    {
        bool valid{};
        RE::FormID attackerFormID{};
    };
    thread_local PendingBarrierWeaponReflectState
        g_pendingBarrierWeaponReflect{};

    struct SpellImpactAuthorityState final
    {
        bool valid{};

        std::uint64_t rawEventSequence{};
        std::uint64_t actionID{};

        RE::FormID sourceFormID{};
        RE::FormID casterFormID{};
        RE::FormID targetFormID{};

        RE::ActorHandle casterHandle{};
        RE::ActorHandle targetHandle{};

        bool playerAttacker{};
        bool playerTarget{};
        Hand hand{ Hand::kUnknown };
        bool actionMatched{};
        Snapshot::ActionSnapshot actionSnapshot{};


        std::chrono::steady_clock::time_point
            createdAt{};

        std::uint8_t probeCount{};
    };

    struct SpellImpactAuthorityProbe final
    {
        bool matched{};

        std::uint64_t rawEventSequence{};
        std::uint64_t actionID{};

        RE::FormID targetFormID{};
        RE::ActorHandle targetHandle{};
        Hand hand{ Hand::kUnknown };
        bool actionMatched{};
        Snapshot::ActionSnapshot actionSnapshot{};


        std::int64_t ageMs{};
        std::uint8_t probeCount{};
    };

    inline constexpr std::size_t
        kSpellImpactAuthoritySlots = 16;

    inline constexpr auto
        kSpellImpactAuthorityMaxAge =
            std::chrono::milliseconds(750);

    std::array<
        SpellImpactAuthorityState,
        kSpellImpactAuthoritySlots>
            g_spellImpactAuthority{};

    std::size_t
        g_spellImpactAuthorityWrite{};

    std::mutex
        g_spellImpactAuthorityMutex{};

    using ProjectileNativeHandle =
        RE::ProjectileHandle::
            native_handle_type;


    struct ProjectileManagerHandleSnapshot final
    {
        bool valid{};

        std::unordered_set<
            ProjectileNativeHandle>
                handles{};
    };


    struct ProjectileFireIdentityTicket final
    {
        bool valid{};
        bool birthConsumed{};

        std::uint64_t actionID{};
        RE::FormID sourceFormID{};

        RE::MagicSystem::CastingSource
            castingSource{};

        Hand hand{ Hand::kUnknown };

        Snapshot::ActionSnapshot
            actionSnapshot{};

        std::chrono::steady_clock::time_point
            createdAt{};
    };


    struct PendingProjectileBirthState final
    {
        bool valid{};

        RE::FormID sourceFormID{};

        RE::MagicSystem::CastingSource
            castingSource{};

        std::array<
            ProjectileNativeHandle,
            16>
                nativeHandles{};

        std::size_t handleCount{};

        std::chrono::steady_clock::time_point
            createdAt{};
    };


    struct ProjectileActionIdentityState final
    {
        bool valid{};

        ProjectileNativeHandle
            nativeHandle{};

        std::uint64_t actionID{};
        RE::FormID sourceFormID{};

        Hand hand{ Hand::kUnknown };

        Snapshot::ActionSnapshot
            actionSnapshot{};

        std::chrono::steady_clock::time_point
            createdAt{};
    };


    inline constexpr std::size_t
        kProjectileFireIdentityTicketSlots = 64;

    inline constexpr std::size_t
        kPendingProjectileBirthSlots = 8;

    inline constexpr std::size_t
        kProjectileActionIdentitySlots = 64;

    inline constexpr std::size_t
        kPendingProjectileHandlesPerBatch = 16;

    inline constexpr auto
        kProjectileBirthCorrelationMaxAge =
            std::chrono::milliseconds(1500);


    std::array<
        ProjectileFireIdentityTicket,
        kProjectileFireIdentityTicketSlots>
            g_projectileFireIdentityTickets{};

    std::size_t
        g_projectileFireIdentityTicketWrite{};


    std::array<
        PendingProjectileBirthState,
        kPendingProjectileBirthSlots>
            g_pendingProjectileBirths{};

    std::size_t
        g_pendingProjectileBirthWrite{};


    std::array<
        ProjectileActionIdentityState,
        kProjectileActionIdentitySlots>
            g_projectileActionIdentity{};

    std::size_t
        g_projectileActionIdentityWrite{};


    std::mutex
        g_projectileIdentityMutex{};


    ProjectileManagerHandleSnapshot
        CaptureProjectileManagerHandles()
    {
        ProjectileManagerHandleSnapshot result{};

        auto* manager =
            RE::Projectile::Manager::
                GetSingleton();

        if (!manager) {
            return result;
        }

        result.handles.reserve(128);

        {
            RE::BSSpinLockGuard
                managerLock(
                    manager->projectileLock);

            const auto append =
                [&result](
                    const auto& a_handles) {

                    for (const auto& handle :
                         a_handles) {

                        const auto native =
                            handle.native_handle();

                        if (native) {
                            result.handles.insert(
                                native);
                        }
                    }
                };

            append(
                manager->unlimited);

            append(
                manager->limited);

            append(
                manager->pending);
        }

        result.valid = true;

        return result;
    }


    std::vector<ProjectileNativeHandle>
        CollectNewbornProjectileHandles(
            const ProjectileManagerHandleSnapshot&
                a_before,
            const ProjectileManagerHandleSnapshot&
                a_after)
    {
        std::vector<ProjectileNativeHandle>
            result{};

        if (!a_before.valid ||
            !a_after.valid) {

            return result;
        }

        result.reserve(
            a_after.handles.size());

        for (const auto native :
             a_after.handles) {

            if (
                a_before.handles.find(
                    native) ==
                a_before.handles.end()
            ) {
                result.push_back(
                    native);
            }
        }

        return result;
    }


    void StoreProjectileActionIdentityUnlocked(
        ProjectileNativeHandle
            a_nativeHandle,
        const ProjectileFireIdentityTicket&
            a_ticket)
    {
        if (!a_nativeHandle ||
            !a_ticket.valid ||
            !a_ticket.actionID ||
            !a_ticket.actionSnapshot.valid) {

            return;
        }

        for (auto& existing :
             g_projectileActionIdentity) {

            if (existing.valid &&
                existing.nativeHandle ==
                    a_nativeHandle) {

                existing.actionID =
                    a_ticket.actionID;

                existing.sourceFormID =
                    a_ticket.sourceFormID;

                existing.hand =
                    a_ticket.hand;

                existing.actionSnapshot =
                    a_ticket.actionSnapshot;

                existing.createdAt =
                    std::chrono::steady_clock::now();

                return;
            }
        }

        auto& slot =
            g_projectileActionIdentity[
                g_projectileActionIdentityWrite %
                kProjectileActionIdentitySlots];

        ++g_projectileActionIdentityWrite;

        slot = {};

        slot.valid = true;

        slot.nativeHandle =
            a_nativeHandle;

        slot.actionID =
            a_ticket.actionID;

        slot.sourceFormID =
            a_ticket.sourceFormID;

        slot.hand =
            a_ticket.hand;

        slot.actionSnapshot =
            a_ticket.actionSnapshot;

        slot.createdAt =
            std::chrono::steady_clock::now();
    }


    void BindProjectileHandlesUnlocked(
        const std::vector<
            ProjectileNativeHandle>&
                a_handles,
        const ProjectileFireIdentityTicket&
            a_ticket)
    {
        for (const auto native :
             a_handles) {

            StoreProjectileActionIdentityUnlocked(
                native,
                a_ticket);
        }
    }


    void BindPendingProjectileHandlesUnlocked(
        const PendingProjectileBirthState&
            a_pending,
        const ProjectileFireIdentityTicket&
            a_ticket)
    {
        for (std::size_t i = 0;
             i < a_pending.handleCount;
             ++i) {

            StoreProjectileActionIdentityUnlocked(
                a_pending.nativeHandles[i],
                a_ticket);
        }
    }


    std::uint64_t
        TryBindProjectileBirthToExistingFireTicket(
            RE::FormID a_sourceFormID,
            RE::MagicSystem::CastingSource
                a_castingSource,
            const std::vector<
                ProjectileNativeHandle>&
                    a_handles)
    {
        if (!a_sourceFormID ||
            a_handles.empty()) {

            return 0;
        }

        const auto now =
            std::chrono::steady_clock::now();

        std::lock_guard<std::mutex>
            lock(
                g_projectileIdentityMutex);

        for (std::size_t offset = 0;
             offset <
                 kProjectileFireIdentityTicketSlots;
             ++offset) {

            const auto index =
                (g_projectileFireIdentityTicketWrite +
                 kProjectileFireIdentityTicketSlots -
                 1 -
                 offset) %
                kProjectileFireIdentityTicketSlots;

            auto& ticket =
                g_projectileFireIdentityTickets[
                    index];

            if (!ticket.valid) {
                continue;
            }

            if (
                now - ticket.createdAt >
                kProjectileBirthCorrelationMaxAge
            ) {
                if (!ticket.actionSnapshot.
                        extended.siegeStateKnown ||
                    !ticket.actionSnapshot.
                        extended.siegeDeploymentActive) {
                    ticket = {};
                }
                continue;
            }

            if (ticket.birthConsumed ||
                ticket.sourceFormID !=
                    a_sourceFormID ||
                ticket.castingSource !=
                    a_castingSource) {

                continue;
            }

            BindProjectileHandlesUnlocked(
                a_handles,
                ticket);

            ticket.birthConsumed = true;

            return ticket.actionID;
        }

        return 0;
    }


    std::size_t
        StorePendingProjectileBirth(
            RE::FormID a_sourceFormID,
            RE::MagicSystem::CastingSource
                a_castingSource,
            const std::vector<
                ProjectileNativeHandle>&
                    a_handles)
    {
        if (!a_sourceFormID ||
            a_handles.empty()) {

            return 0;
        }

        std::lock_guard<std::mutex>
            lock(
                g_projectileIdentityMutex);

        auto& slot =
            g_pendingProjectileBirths[
                g_pendingProjectileBirthWrite %
                kPendingProjectileBirthSlots];

        ++g_pendingProjectileBirthWrite;

        slot = {};

        slot.valid = true;

        slot.sourceFormID =
            a_sourceFormID;

        slot.castingSource =
            a_castingSource;

        slot.createdAt =
            std::chrono::steady_clock::now();

        const auto count =
            a_handles.size() <
                kPendingProjectileHandlesPerBatch ?
                    a_handles.size() :
                    kPendingProjectileHandlesPerBatch;

        slot.handleCount =
            count;

        for (std::size_t i = 0;
             i < count;
             ++i) {

            slot.nativeHandles[i] =
                a_handles[i];
        }

        return count;
    }


    std::size_t
        RegisterProjectileFireIdentityTicket(
            RE::SpellItem* a_spell,
            RE::MagicSystem::CastingSource
                a_castingSource,
            const AttackActionState&
                a_action)
    {
        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            !a_spell ||
            !TULLBUNG::Classification::
                IsProjectileDestructionSpell(
                    a_spell) ||
            !a_action.actionID ||
            a_action.actor != player ||
            a_action.sourceKind !=
                SourceKind::kSpell ||
            a_action.sourceFormID !=
                a_spell->GetFormID() ||
            !a_action.spellFireSeen ||
            !a_action.snapshotCaptured ||
            !a_action.actionSnapshot.valid ||
            a_action.actionSnapshot.actionID !=
                a_action.actionID) {

            return 0;
        }

        ProjectileFireIdentityTicket next{};

        next.valid = true;

        next.actionID =
            a_action.actionID;

        next.sourceFormID =
            a_action.sourceFormID;

        next.castingSource =
            a_castingSource;

        next.hand =
            a_action.hand;

        next.actionSnapshot =
            a_action.actionSnapshot;

        next.createdAt =
            std::chrono::steady_clock::now();

        const auto now =
            next.createdAt;

        std::size_t boundCount{};

        std::lock_guard<std::mutex>
            lock(
                g_projectileIdentityMutex);

        auto& ticketSlot =
            g_projectileFireIdentityTickets[
                g_projectileFireIdentityTicketWrite %
                kProjectileFireIdentityTicketSlots];

        ++g_projectileFireIdentityTicketWrite;

        ticketSlot =
            next;

        PendingProjectileBirthState*
            oldestMatch = nullptr;

        for (auto& pending :
             g_pendingProjectileBirths) {

            if (!pending.valid) {
                continue;
            }

            if (
                now - pending.createdAt >
                kProjectileBirthCorrelationMaxAge
            ) {
                pending = {};
                continue;
            }

            if (pending.sourceFormID !=
                    next.sourceFormID ||
                pending.castingSource !=
                    next.castingSource) {

                continue;
            }

            if (!oldestMatch ||
                pending.createdAt <
                    oldestMatch->createdAt) {

                oldestMatch =
                    &pending;
            }
        }

        if (oldestMatch) {
            BindPendingProjectileHandlesUnlocked(
                *oldestMatch,
                ticketSlot);

            boundCount =
                oldestMatch->handleCount;

            oldestMatch->valid =
                false;

            ticketSlot.birthConsumed =
                true;
        }

        return boundCount;
    }


    void OnPlayerSpellFireIdentity(
        RE::SpellItem* a_spell,
        RE::MagicSystem::CastingSource
            a_castingSource,
        const AttackActionState&
            a_action)
    {
        if (!a_spell ||
            !TULLBUNG::Classification::
                IsProjectileDestructionSpell(
                    a_spell)) {

            return;
        }

        const auto boundCount =
            RegisterProjectileFireIdentityTicket(
                a_spell,
                a_castingSource,
                a_action);

        logger::debug(
            "[S15SIEGE-FIRE-TICKET] ActionID={} | Spell={:08X} | PendingBound={}",
            a_action.actionID,
            a_spell->GetFormID(),
            boundCount);
    }


    void HandlePlayerSpellCastProjectileBirth(
        RE::ActorMagicCaster* a_caster,
        RE::SpellItem* a_spell,
        const ProjectileManagerHandleSnapshot&
            a_before)
    {
        if (!a_caster ||
            !a_spell ||
            !a_before.valid) {

            return;
        }

        const auto after =
            CaptureProjectileManagerHandles();

        if (!after.valid) {
            return;
        }

        const auto newborn =
            CollectNewbornProjectileHandles(
                a_before,
                after);

        if (newborn.empty()) {
            return;
        }

        const auto castingSource =
            a_caster->
                GetCastingSource();

        const auto actionID =
            TryBindProjectileBirthToExistingFireTicket(
                a_spell->GetFormID(),
                castingSource,
                newborn);

        if (actionID) {
            logger::debug(
                "[S15SIEGE-PROJECTILE-BIND] Route=FIRE_FIRST | ActionID={} | Spell={:08X} | Count={}",
                actionID,
                a_spell->GetFormID(),
                newborn.size());

            return;
        }

        const auto storedCount =
            StorePendingProjectileBirth(
                a_spell->GetFormID(),
                castingSource,
                newborn);

        logger::debug(
            "[S15SIEGE-PROJECTILE-BIRTH] Route=BIRTH_FIRST | Spell={:08X} | Newborn={} | PendingStored={}",
            a_spell->GetFormID(),
            newborn.size(),
            storedCount);
    }


    struct ProjectileImpactLocationState final
    {
        bool valid{};

        ProjectileNativeHandle
            nativeHandle{};

        std::uint64_t actionID{};
        RE::FormID sourceFormID{};

        Hand hand{ Hand::kUnknown };

        Snapshot::ActionSnapshot
            actionSnapshot{};

        RE::NiPoint3 targetLoc{};

        // Diagnostic only.
        // Actor target authority remains the existing
        // Stage14 SpellImpactAuthority / EffectTarget path.
        RE::FormID impactRefFormID{};

        std::chrono::steady_clock::time_point
            createdAt{};
    };


    inline constexpr std::size_t
        kProjectileImpactLocationSlots = 64;

    


    std::array<
        ProjectileImpactLocationState,
        kProjectileImpactLocationSlots>
            g_projectileImpactLocations{};

    std::size_t
        g_projectileImpactLocationWrite{};


    bool IsSiegeGroundImpactSurface(
        RE::Projectile* a_projectile,
        RE::TESObjectREFR* a_impactRef,
        const RE::NiPoint3& a_targetLoc,
        const RE::NiPoint3& a_velocity,
        const RE::hkpCollidable* a_impactCollidable,
        std::uint64_t a_actionID,
        float& a_normalZ,
        float& a_surfaceDeltaUnits) noexcept
    {
        // Ground authority must describe the *first direct contact surface*, not
        // merely a floor near the impact point. AddImpact provides the impact
        // location used by this hook, an incoming-velocity argument and the
        // collided root body. Cast one short ray through that location along
        // the incoming trajectory and
        // only accept the normal when the ray resolves the same root collidable.
        // This keeps the one-ray budget while rejecting wall-bottom false floors.
        constexpr float kRayHalfSpanGameUnits = 16.0f;
        constexpr float kMaximumImpactDeltaUnits = 8.0f;
        constexpr float kMinimumGroundNormalZ = 0.70710678f;
        constexpr float kMinimumDirectionLengthSq = 0.0001f;

        const float unknown =
            std::numeric_limits<float>::quiet_NaN();

        a_normalZ = 0.0f;
        a_surfaceDeltaUnits =
            std::numeric_limits<float>::infinity();

        RE::NiPoint3 incoming = a_velocity;
        const char* velocityRoute = "ADD_IMPACT_ARG";

        RE::NiPoint3 direction{ unknown, unknown, unknown };
        RE::NiPoint3 fromGame{ unknown, unknown, unknown };
        RE::NiPoint3 toGame{ unknown, unknown, unknown };
        RE::NiPoint3 hitGame{ unknown, unknown, unknown };
        RE::NiPoint3 fromHavok{ unknown, unknown, unknown };
        RE::NiPoint3 toHavok{ unknown, unknown, unknown };

        float havokScale = unknown;
        float hitFraction = unknown;
        alignas(16) float normal[4]{ unknown, unknown, unknown, unknown };

        RE::TESObjectCELL* cell = nullptr;
        RE::bhkWorld* bhkWorld = nullptr;
        RE::hkpWorld* havokWorld = nullptr;
        const RE::hkpCollidable* rayCollidable = nullptr;
        std::uint32_t rayShapeKey = 0xFFFFFFFFu;
        bool rayIssued = false;
        RE::hkpWorldRayCastInput rayInput{};

        const auto finish = [&](const char* a_reason, bool a_accepted = false) {
            const char* contactComparison =
                !rayCollidable ? "NO_RAY_HIT" :
                !a_impactCollidable ? "NO_IMPACT_COLLIDABLE" :
                rayCollidable == a_impactCollidable ?
                    "EXACT_ROOT_MATCH" : "ROOT_MISMATCH";

            logger::info(
                "[S15SIEGE-RAY] ActionID={} | Ref={:08X} | VelocityRoute={} | Velocity=({:.4f},{:.4f},{:.4f}) | Direction=({:.6f},{:.6f},{:.6f}) | ImpactGame=({:.4f},{:.4f},{:.4f}) | FromGame=({:.4f},{:.4f},{:.4f}) | ToGame=({:.4f},{:.4f},{:.4f}) | HitGame=({:.4f},{:.4f},{:.4f}) | FromHavok=({:.6f},{:.6f},{:.6f}) | ToHavok=({:.6f},{:.6f},{:.6f}) | Scale={:.8f} | RayIssued={} | Filter={:08X} | Fraction={:.6f} | Normal=({:.6f},{:.6f},{:.6f}) | ImpactDelta={:.6f} | Cell={:08X} | Bhk={:X} | World={:X} | ImpactCollidable={:X} | RayCollidable={:X} | RayShapeKey={:08X} | ContactComparison={} | FirstSurface=TRAJECTORY_RAY | Reason={} | Accepted={}",
                a_actionID,
                a_impactRef ? a_impactRef->GetFormID() : 0,
                velocityRoute,
                incoming.x, incoming.y, incoming.z,
                direction.x, direction.y, direction.z,
                a_targetLoc.x, a_targetLoc.y, a_targetLoc.z,
                fromGame.x, fromGame.y, fromGame.z,
                toGame.x, toGame.y, toGame.z,
                hitGame.x, hitGame.y, hitGame.z,
                fromHavok.x, fromHavok.y, fromHavok.z,
                toHavok.x, toHavok.y, toHavok.z,
                havokScale,
                rayIssued,
                rayInput.filterInfo.filter,
                hitFraction,
                normal[0], normal[1], normal[2],
                a_surfaceDeltaUnits,
                cell ? cell->GetFormID() : 0,
                reinterpret_cast<std::uintptr_t>(bhkWorld),
                reinterpret_cast<std::uintptr_t>(havokWorld),
                reinterpret_cast<std::uintptr_t>(a_impactCollidable),
                reinterpret_cast<std::uintptr_t>(rayCollidable),
                rayShapeKey,
                contactComparison,
                a_reason,
                a_accepted);

            return a_accepted;
        };

        if (!a_projectile) {
            return finish("NO_PROJECTILE");
        }

        if (!std::isfinite(a_targetLoc.x) ||
            !std::isfinite(a_targetLoc.y) ||
            !std::isfinite(a_targetLoc.z)) {

            return finish("INVALID_IMPACT_LOCATION");
        }

        const auto validDirection = [&](const RE::NiPoint3& a_value) noexcept {
            if (!std::isfinite(a_value.x) ||
                !std::isfinite(a_value.y) ||
                !std::isfinite(a_value.z)) {

                return false;
            }

            const float lengthSq =
                a_value.x * a_value.x +
                a_value.y * a_value.y +
                a_value.z * a_value.z;

            return
                std::isfinite(lengthSq) &&
                lengthSq >= kMinimumDirectionLengthSq;
        };

        if (!validDirection(incoming)) {
            incoming =
                a_projectile->
                    GetProjectileRuntimeData().
                        linearVelocity;
            velocityRoute = "RUNTIME_LINEAR_VELOCITY";
        }

        if (!validDirection(incoming)) {
            return finish("INVALID_IMPACT_VELOCITY");
        }

        const float directionLength =
            std::sqrt(
                incoming.x * incoming.x +
                incoming.y * incoming.y +
                incoming.z * incoming.z);

        if (!std::isfinite(directionLength) ||
            directionLength <= 0.0f) {

            return finish("INVALID_DIRECTION_LENGTH");
        }

        // AddImpact's velocity axis is opposite the actual projectile
        // travel direction on the verified Siege impact path.
        // Orient the trajectory ray from the projectile side into
        // the contacted surface so the returned normal is the
        // first-contact outward surface normal.
        direction = RE::NiPoint3{
            -incoming.x / directionLength,
            -incoming.y / directionLength,
            -incoming.z / directionLength };

        fromGame = RE::NiPoint3{
            a_targetLoc.x - direction.x * kRayHalfSpanGameUnits,
            a_targetLoc.y - direction.y * kRayHalfSpanGameUnits,
            a_targetLoc.z - direction.z * kRayHalfSpanGameUnits };

        toGame = RE::NiPoint3{
            a_targetLoc.x + direction.x * kRayHalfSpanGameUnits,
            a_targetLoc.y + direction.y * kRayHalfSpanGameUnits,
            a_targetLoc.z + direction.z * kRayHalfSpanGameUnits };

        if (!a_impactCollidable) {
            return finish("NO_IMPACT_COLLIDABLE");
        }

        cell = a_projectile->GetParentCell();
        if (!cell && a_impactRef) {
            cell = a_impactRef->GetParentCell();
        }
        if (!cell) {
            return finish("NO_CELL");
        }

        bhkWorld = cell->GetbhkWorld();
        if (!bhkWorld) {
            return finish("NO_BHK_WORLD");
        }

        havokWorld = bhkWorld->GetWorld1();
        if (!havokWorld) {
            return finish("NO_HAVOK_WORLD");
        }

        // Game -> Havok.  GetWorldScaleInverse() is Havok -> game only.
        havokScale = RE::bhkWorld::GetWorldScale();
        if (!std::isfinite(havokScale) || havokScale <= 0.0f) {
            return finish("INVALID_SCALE");
        }

        fromHavok = RE::NiPoint3{
            fromGame.x * havokScale,
            fromGame.y * havokScale,
            fromGame.z * havokScale };

        toHavok = RE::NiPoint3{
            toGame.x * havokScale,
            toGame.y * havokScale,
            toGame.z * havokScale };

        if (!std::isfinite(fromHavok.x) ||
            !std::isfinite(fromHavok.y) ||
            !std::isfinite(fromHavok.z) ||
            !std::isfinite(toHavok.x) ||
            !std::isfinite(toHavok.y) ||
            !std::isfinite(toHavok.z)) {

            return finish("INVALID_RAY_COORDINATES");
        }

        rayInput.from =
            RE::hkVector4(
                fromHavok.x,
                fromHavok.y,
                fromHavok.z,
                0.0f);

        rayInput.to =
            RE::hkVector4(
                toHavok.x,
                toHavok.y,
                toHavok.z,
                0.0f);

        // Match the query filter to the actual projectile that produced
        // this AddImpact: projectile system group + projectile base layer.
        // This avoids the previous zero-initialized CFilter authority.
        const auto* projectileBase =
            a_projectile->GetProjectileBase();

        if (!projectileBase) {
            return finish("NO_PROJECTILE_BASE");
        }

        // Siege is a spell-projectile-only path. Some vanilla projectile
        // records leave the optional collision-layer form unset, so use
        // the Spell query layer only for that missing-record fallback.
        const auto projectileLayer =
            projectileBase->data.collisionLayer ?
                projectileBase->data.collisionLayer->collisionIdx :
                static_cast<std::uint32_t>(
                    RE::COL_LAYER::kSpell);

        if (projectileLayer > 0x7Fu) {
            return finish("INVALID_PROJECTILE_COLLISION_LAYER");
        }

        rayInput.filterInfo.filter =
            (static_cast<std::uint32_t>(
                 a_projectile->GetCollisionGroup()) << 16) |
            projectileLayer;

        RE::hkpWorldRayCastOutput rayOutput{};
        rayOutput.Reset();

        // Keep the existing callback-context lock/filter behavior.  The strict
        // root-collidable equality below is the authority bridge to AddImpact.
        rayIssued = true;
        havokWorld->CastRay(
            rayInput,
            rayOutput);  // Exactly one ray per eligible first impact.

        hitFraction = rayOutput.hitFraction;
        rayCollidable = rayOutput.rootCollidable;

        if (!rayOutput.HasHit()) {
            return finish("RAY_NO_HIT");
        }

        rayShapeKey = rayOutput.shapeKeys[0];
        _mm_storeu_ps(normal, rayOutput.normal.quad);

        if (!std::isfinite(hitFraction) ||
            hitFraction < 0.0f ||
            hitFraction > 1.0f) {

            return finish("INVALID_FRACTION");
        }

        if (!std::isfinite(normal[0]) ||
            !std::isfinite(normal[1]) ||
            !std::isfinite(normal[2])) {

            return finish("INVALID_NORMAL");
        }

        hitGame = RE::NiPoint3{
            fromGame.x + (toGame.x - fromGame.x) * hitFraction,
            fromGame.y + (toGame.y - fromGame.y) * hitFraction,
            fromGame.z + (toGame.z - fromGame.z) * hitFraction };

        const float deltaX =
            hitGame.x - a_targetLoc.x;
        const float deltaY =
            hitGame.y - a_targetLoc.y;
        const float deltaZ =
            hitGame.z - a_targetLoc.z;

        const float impactDeltaSq =
            deltaX * deltaX +
            deltaY * deltaY +
            deltaZ * deltaZ;

        if (!std::isfinite(impactDeltaSq) || impactDeltaSq < 0.0f) {
            return finish("INVALID_IMPACT_DELTA");
        }

        a_surfaceDeltaUnits =
            std::sqrt(impactDeltaSq);

        if (!std::isfinite(a_surfaceDeltaUnits)) {
            return finish("INVALID_IMPACT_DELTA");
        }

        if (a_surfaceDeltaUnits > kMaximumImpactDeltaUnits) {
            return finish("IMPACT_POINT_MISMATCH");
        }

        if (rayCollidable != a_impactCollidable) {
            return finish("IMPACT_COLLIDABLE_MISMATCH");
        }

        a_normalZ = normal[2];

        if (a_normalZ < kMinimumGroundNormalZ) {
            return finish("SURFACE_TOO_STEEP");
        }

        return finish("GROUND_TRAJECTORY_MATCH", true);
    }

    ProjectileActionIdentityState*
        TryRecoverSiegeProjectileIdentityAtImpactUnlocked(
            RE::Projectile* a_projectile,
            ProjectileNativeHandle a_nativeHandle,
            const std::chrono::steady_clock::time_point&
                a_now) noexcept
    {
        if (!a_projectile ||
            !a_nativeHandle) {

            return nullptr;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player) {
            return nullptr;
        }

        const auto& runtime =
            a_projectile->
                GetProjectileRuntimeData();

        auto shooterPtr =
            runtime.shooter.get();

        auto* shooter =
            shooterPtr ?
                shooterPtr->As<RE::Actor>() :
                nullptr;

        auto* spell =
            runtime.spell ?
                runtime.spell->
                    As<RE::SpellItem>() :
                nullptr;

        if (!shooter ||
            shooter != player ||
            !spell ||
            !TULLBUNG::Classification::
                IsProjectileDestructionSpell(
                    spell)) {

            return nullptr;
        }

        const double livingSeconds =
            std::isfinite(runtime.livingTime) &&
            runtime.livingTime >= 0.0f ?
                static_cast<double>(
                    runtime.livingTime) :
                -1.0;

        if (livingSeconds < 0.0) {
            logger::info(
                "[S15SIEGE-IDENTITY] Result=MISS_AT_IMPACT | Reason=INVALID_LIVING_TIME | Spell={:08X} | Native={}",
                spell->GetFormID(),
                static_cast<std::uint64_t>(
                    a_nativeHandle));

            return nullptr;
        }

        ProjectileFireIdentityTicket*
            bestTicket = nullptr;

        double bestAgeErrorSeconds{};

        for (std::size_t offset = 0;
             offset <
                 kProjectileFireIdentityTicketSlots;
             ++offset) {

            const auto index =
                (g_projectileFireIdentityTicketWrite +
                 kProjectileFireIdentityTicketSlots -
                 1 -
                 offset) %
                kProjectileFireIdentityTicketSlots;

            auto& ticket =
                g_projectileFireIdentityTickets[
                    index];

            if (!ticket.valid ||
                !ticket.actionID ||
                !ticket.actionSnapshot.valid ||
                ticket.actionSnapshot.actionID !=
                    ticket.actionID ||
                ticket.sourceFormID !=
                    spell->GetFormID() ||
                ticket.castingSource !=
                    runtime.castingSource) {

                continue;
            }

            const double ticketAgeSeconds =
                std::chrono::duration<double>(
                    a_now -
                    ticket.createdAt).
                        count();

            if (!std::isfinite(
                    ticketAgeSeconds) ||
                ticketAgeSeconds < 0.0) {

                continue;
            }

            const double ageErrorSeconds =
                std::abs(
                    ticketAgeSeconds -
                    livingSeconds);

            if (!std::isfinite(
                    ageErrorSeconds)) {

                continue;
            }

            if (ageErrorSeconds >
                std::chrono::duration<double>(
                    kProjectileBirthCorrelationMaxAge).count()) {

                continue;
            }

            if (!bestTicket ||
                ageErrorSeconds <
                    bestAgeErrorSeconds) {

                bestTicket =
                    &ticket;

                bestAgeErrorSeconds =
                    ageErrorSeconds;
            }
        }

        if (!bestTicket) {
            logger::info(
                "[S15SIEGE-IDENTITY] Result=MISS_AT_IMPACT | Reason=NO_CORRELATED_FIRE_TICKET | Spell={:08X} | Native={} | Living={:.3f}",
                spell->GetFormID(),
                static_cast<std::uint64_t>(
                    a_nativeHandle),
                livingSeconds);

            return nullptr;
        }

        const auto recoveredActionID =
            bestTicket->actionID;

        StoreProjectileActionIdentityUnlocked(
            a_nativeHandle,
            *bestTicket);

        bestTicket->birthConsumed =
            true;

        for (auto& identity :
             g_projectileActionIdentity) {

            if (!identity.valid ||
                identity.nativeHandle !=
                    a_nativeHandle ||
                identity.actionID !=
                    recoveredActionID) {

                continue;
            }

            logger::info(
                "[S15SIEGE-IDENTITY] Result=RECOVERED_AT_IMPACT | ActionID={} | Spell={:08X} | Native={} | Living={:.3f} | AgeErrorMs={:.1f}",
                recoveredActionID,
                spell->GetFormID(),
                static_cast<std::uint64_t>(
                    a_nativeHandle),
                livingSeconds,
                bestAgeErrorSeconds * 1000.0);

            return &identity;
        }

        logger::error(
            "[S15SIEGE-IDENTITY] Result=STORE_FAILED | ActionID={} | Spell={:08X} | Native={}",
            recoveredActionID,
            spell->GetFormID(),
            static_cast<std::uint64_t>(
                a_nativeHandle));

        return nullptr;
    }

    bool RecordProjectileActualImpact(
        RE::Projectile* a_projectile,
        RE::TESObjectREFR* a_impactRef,
        const RE::NiPoint3& a_targetLoc,
        const RE::NiPoint3& a_velocity,
        const RE::hkpCollidable* a_impactCollidable,
        const char* a_kind)
    {
        if (!a_projectile) {
            return false;
        }

        RE::ProjectileHandle
            projectileHandle{
                a_projectile
            };

        const auto nativeHandle =
            projectileHandle.native_handle();

        if (!nativeHandle) {
            return false;
        }

        const auto now =
            std::chrono::steady_clock::now();

        ProjectileImpactLocationState
            next{};

        {
            std::lock_guard<std::mutex>
                lock(
                    g_projectileIdentityMutex);

            ProjectileActionIdentityState*
                matchedIdentity = nullptr;

            for (std::size_t offset = 0;
                 offset <
                     kProjectileActionIdentitySlots;
                 ++offset) {

                const auto index =
                    (g_projectileActionIdentityWrite +
                     kProjectileActionIdentitySlots -
                     1 -
                     offset) %
                    kProjectileActionIdentitySlots;

                auto& identity =
                    g_projectileActionIdentity[
                        index];

                if (!identity.valid) {
                    continue;
                }

                if (identity.nativeHandle !=
                    nativeHandle) {

                    continue;
                }

                if (!identity.actionID ||
                    !identity.actionSnapshot.valid ||
                    identity.actionSnapshot.actionID !=
                        identity.actionID) {

                    continue;
                }

                matchedIdentity =
                    &identity;

                break;
            }

            if (!matchedIdentity) {
                matchedIdentity =
                    TryRecoverSiegeProjectileIdentityAtImpactUnlocked(
                        a_projectile,
                        nativeHandle,
                        now);
            }

            if (!matchedIdentity) {
                return false;
            }

            const bool siegeFirstImpactRoute =
                matchedIdentity->actionSnapshot.
                    extended.siegeStateKnown &&
                matchedIdentity->actionSnapshot.
                    extended.siegeDeploymentActive;

            if (siegeFirstImpactRoute) {
                for (const auto& priorImpact :
                     g_projectileImpactLocations) {

                    if (!priorImpact.valid ||
                        priorImpact.nativeHandle !=
                            nativeHandle ||
                        priorImpact.actionID !=
                            matchedIdentity->actionID) {

                        continue;
                    }

                    logger::debug(
                        "[S15SIEGE-IMPACT] Kind={} | ActionID={} | Native={} | Result=IGNORED_AFTER_FIRST_DIRECT_IMPACT",
                        a_kind ?
                            a_kind :
                            "<unknown>",
                        matchedIdentity->actionID,
                        static_cast<std::uint64_t>(
                            nativeHandle));

                    return false;
                }
            }

            next.valid = true;

            next.nativeHandle =
                nativeHandle;

            next.actionID =
                matchedIdentity->actionID;

            next.sourceFormID =
                matchedIdentity->sourceFormID;

            next.hand =
                matchedIdentity->hand;

            next.actionSnapshot =
                matchedIdentity->actionSnapshot;

            next.targetLoc =
                a_targetLoc;

            next.impactRefFormID =
                a_impactRef ?
                    a_impactRef->GetFormID() :
                    0;

            next.createdAt =
                now;

            auto& locationSlot =
                g_projectileImpactLocations[
                    g_projectileImpactLocationWrite %
                    kProjectileImpactLocationSlots];

            ++g_projectileImpactLocationWrite;

            locationSlot =
                next;
        }

        logger::debug(
            "[S15SIEGE-IMPACT] Kind={} | ActionID={} | Spell={:08X} | Ref={:08X} | Native={} | Loc=({:.3f},{:.3f},{:.3f})",
            a_kind ?
                a_kind :
                "<unknown>",
            next.actionID,
            next.sourceFormID,
            next.impactRefFormID,
            static_cast<std::uint64_t>(
                next.nativeHandle),
            next.targetLoc.x,
            next.targetLoc.y,
            next.targetLoc.z);

        const bool siegeGroundRoute =
            next.actionSnapshot.
                extended.siegeStateKnown &&
            next.actionSnapshot.
                extended.siegeDeploymentActive;

        if (siegeGroundRoute) {
            // Actor/NPC/Creature are all ActorCharacter references.
            // First direct impact has already been recorded above,
            // so a rejected Actor hit permanently consumes it.
            if (a_impactRef &&
                a_impactRef->GetFormType() ==
                    RE::FormType::ActorCharacter) {

                logger::info(
                    "[S15SIEGE-GROUND] ActionID={} | Spell={:08X} | Ref={:08X} | Result=REJECT_ACTOR_DIRECT",
                    next.actionID,
                    next.sourceFormID,
                    next.impactRefFormID);

                return true;
            }

            float normalZ = 0.0f;

            float surfaceDeltaUnits =
                std::numeric_limits<float>::infinity();

            if (!IsSiegeGroundImpactSurface(
                    a_projectile,
                    a_impactRef,
                    next.targetLoc,
                    a_velocity,
                    a_impactCollidable,
                    next.actionID,
                    normalZ,
                    surfaceDeltaUnits)) {

                logger::info(
                    "[S15SIEGE-GROUND] ActionID={} | Spell={:08X} | Ref={:08X} | NormalZ={:.4f} | SurfaceDeltaUnits={:.3f} | Result=REJECT_NON_GROUND_OR_NO_SURFACE",
                    next.actionID,
                    next.sourceFormID,
                    next.impactRefFormID,
                    normalZ,
                    surfaceDeltaUnits);

                return true;
            }

            logger::info(
                "[S15SIEGE-GROUND] ActionID={} | Spell={:08X} | Ref={:08X} | NormalZ={:.4f} | SurfaceDeltaUnits={:.3f} | Result=GROUND_ACCEPT",
                next.actionID,
                next.sourceFormID,
                next.impactRefFormID,
                normalZ,
                surfaceDeltaUnits);

            logger::info(
                "[RUNE] GROUND_IMPACT_ACCEPT | ActionID={} | Spell={:08X} | Ref={:08X} | NormalZ={:.4f} | SurfaceDeltaUnits={:.3f}",
                next.actionID,
                next.sourceFormID,
                next.impactRefFormID,
                normalZ,
                surfaceDeltaUnits);
        }

        TULLBUNG::ClassRuntime::
            OnPlayerProjectileImpact(
                next.actionID,
                next.sourceFormID,
                a_impactRef,
                next.targetLoc,
                next.actionSnapshot);

        return true;
    }
    void ResetProjectileIdentityCorrelation()
    {
        std::lock_guard<std::mutex>
            lock(
                g_projectileIdentityMutex);

        g_projectileFireIdentityTickets = {};
        g_projectileFireIdentityTicketWrite = 0;

        g_pendingProjectileBirths = {};
        g_pendingProjectileBirthWrite = 0;

        g_projectileActionIdentity = {};
        g_projectileActionIdentityWrite = 0;
        g_projectileImpactLocations = {};
        g_projectileImpactLocationWrite = 0;
    }
    void ResetSpellImpactAuthority()
    {
        std::lock_guard<std::mutex>
            lock(
                g_spellImpactAuthorityMutex);

        g_spellImpactAuthority = {};
        g_spellImpactAuthorityWrite = 0;
    }

    void RegisterSpellImpactAuthority(
        const CombatContext& a_context)
    {
        if (a_context.sourceKind !=
                SourceKind::kSpell ||
            !a_context.spell ||
            !a_context.spellDirectCandidate ||
            (!a_context.playerAttacker &&
             !a_context.playerTarget) ||
            !a_context.attacker ||
            !a_context.target) {

            return;
        }

        SpellImpactAuthorityState next{};

        next.valid = true;
        next.rawEventSequence =
            a_context.rawEventSequence;
        next.actionID =
            a_context.attackActionID;
        next.sourceFormID =
            a_context.sourceFormID;
        next.casterFormID =
            a_context.attacker->GetFormID();
        next.targetFormID =
            a_context.target->GetFormID();

        next.casterHandle =
            a_context.attacker;
        next.targetHandle =
            a_context.target;

        next.playerAttacker =
            a_context.playerAttacker;
        next.playerTarget =
            a_context.playerTarget;

        next.hand =
            a_context.hand;
        next.actionMatched =
            a_context.actionMatched;
        next.actionSnapshot =
            a_context.actionSnapshot;

        next.createdAt =
            std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex>
                lock(
                    g_spellImpactAuthorityMutex);

            auto& slot =
                g_spellImpactAuthority[
                    g_spellImpactAuthorityWrite %
                    kSpellImpactAuthoritySlots];

            ++g_spellImpactAuthorityWrite;
            slot = next;
        }

        logger::debug(
            "[S14SPELL-AUTH-REG] Seq={} | ActionID={} | Spell={:08X} | Caster={:08X} | Target={:08X} | PlayerAttacker={} | PlayerTarget={}",
            next.rawEventSequence,
            next.actionID,
            next.sourceFormID,
            next.casterFormID,
            next.targetFormID,
            next.playerAttacker,
            next.playerTarget);
    }

    [[nodiscard]]
    SpellImpactAuthorityProbe
        ResolveSpellImpactAuthority(
            const RE::SpellItem* a_spell,
            RE::Actor* a_caster,
            RE::Actor* a_target)
    {
        SpellImpactAuthorityProbe result{};

        if (!a_spell ||
            !a_caster) {

            return result;
        }

        if (!a_target) {
            return result;
        }

        const auto now =
            std::chrono::steady_clock::now();

        const auto spellID =
            a_spell->GetFormID();

        const auto casterID =
            a_caster->GetFormID();

        const auto targetID =
            a_target->GetFormID();

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        const bool playerCaster =
            a_caster == player;

        std::lock_guard<std::mutex>
            lock(
                g_spellImpactAuthorityMutex);

        for (std::size_t offset = 0;
             offset <
                 kSpellImpactAuthoritySlots;
             ++offset) {

            const auto index =
                (g_spellImpactAuthorityWrite +
                 kSpellImpactAuthoritySlots -
                 1 -
                 offset) %
                kSpellImpactAuthoritySlots;

            auto& slot =
                g_spellImpactAuthority[index];

            if (!slot.valid) {
                continue;
            }

            const auto age =
                std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                        now -
                        slot.createdAt);

            if (age >
                kSpellImpactAuthorityMaxAge) {

                slot.valid = false;
                continue;
            }

            if (slot.sourceFormID !=
                    spellID ||
                slot.casterFormID !=
                    casterID) {

                continue;
            }

            if (slot.targetFormID != targetID) {
                continue;
            }

            if (playerCaster) {
                if (!slot.playerAttacker) {
                    continue;
                }
            } else {
                if (!slot.playerTarget) {
                    continue;
                }
            }

            if (slot.probeCount <
                (std::numeric_limits<
                    std::uint8_t>::max)()) {

                ++slot.probeCount;
            }

            result.matched = true;
            result.rawEventSequence =
                slot.rawEventSequence;
            result.actionID =
                slot.actionID;
            result.targetFormID =
                slot.targetFormID;
            result.targetHandle =
                slot.targetHandle;

            result.hand =
                slot.hand;
            result.actionMatched =
                slot.actionMatched;
            result.actionSnapshot =
                slot.actionSnapshot;
            result.ageMs =
                age.count();
            result.probeCount =
                slot.probeCount;

            return result;
        }

        return result;
    }

    inline constexpr float
        kWorldUnitsPerFoot =
            128.0f / 6.0f;

    float Distance3D(
        const RE::NiPoint3& a_from,
        const RE::NiPoint3& a_to)
    {
        const float dx =
            a_to.x - a_from.x;

        const float dy =
            a_to.y - a_from.y;

        const float dz =
            a_to.z - a_from.z;

        return std::sqrt(
            dx * dx +
            dy * dy +
            dz * dz);
    }

    std::string_view HandName(
        Hand a_hand);

    bool g_playerMovingKnown{};
    bool g_playerLastMoving{};

    class PlayerMovementStateSink final :
        public RE::BSTEventSink<
            RE::BSAnimationGraphEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::BSAnimationGraphEvent* a_event,
            RE::BSTEventSource<
                RE::BSAnimationGraphEvent>*) override
        {
            if (!a_event) {
                return
                    RE::BSEventNotifyControl::
                        kContinue;
            }

            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                a_event->holder != player) {

                return
                    RE::BSEventNotifyControl::
                        kContinue;
            }

            const bool moving =
                player->IsMoving();

            if (!g_playerMovingKnown ||
                moving != g_playerLastMoving) {

                g_playerMovingKnown = true;
                g_playerLastMoving = moving;

                TULLBUNG::ClassRuntime::
                    OnPlayerMovementStateChanged(
                        moving);
            }

            return
                RE::BSEventNotifyControl::
                    kContinue;
        }
    };

    PlayerMovementStateSink
        g_playerMovementStateSink{};

    void EnsurePlayerMovementStateSink()
    {
        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player) {
            return;
        }

        player->AddAnimationGraphEventSink(
            &g_playerMovementStateSink);
    }
    void ProcessEmberSpread(
        const PendingWeaponDirectHitState&
            a_probe)
    {
        if (!a_probe.valid ||
            !a_probe.ranged ||
            !a_probe.launchKnown ||
            a_probe.distanceFt < 50.0f) {

            return;
        }

        auto* tes =
            RE::TES::GetSingleton();

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!tes ||
            !player) {

            logger::warn(
                "[HUNTER-EMBER-SPREAD-SUMMARY] ActionID={} | TES/Player unavailable",
                a_probe.actionID);

            return;
        }

        RE::NiPoint3
            spreadOrigin =
                a_probe.hitPosition;

        auto spreadTargetPtr =
            a_probe.targetHandle.get();

        auto* spreadTarget =
            spreadTargetPtr.get();

        if (spreadTarget) {
            spreadOrigin =
                spreadTarget->GetPosition();
        }

        constexpr float
            kRadiusFt = 20.0f;

        constexpr float
            kRadiusUnits =
                kRadiusFt *
                kWorldUnitsPerFoot;

        std::array<
            RE::NiPoint3,
            9> samples{};

        samples[0] =
            spreadOrigin;

        samples[1] =
            spreadOrigin;
        samples[1].x +=
            kRadiusUnits;

        samples[2] =
            spreadOrigin;
        samples[2].x -=
            kRadiusUnits;

        samples[3] =
            spreadOrigin;
        samples[3].y +=
            kRadiusUnits;

        samples[4] =
            spreadOrigin;
        samples[4].y -=
            kRadiusUnits;

        samples[5] =
            spreadOrigin;
        samples[5].x +=
            kRadiusUnits;
        samples[5].y +=
            kRadiusUnits;

        samples[6] =
            spreadOrigin;
        samples[6].x +=
            kRadiusUnits;
        samples[6].y -=
            kRadiusUnits;

        samples[7] =
            spreadOrigin;
        samples[7].x -=
            kRadiusUnits;
        samples[7].y +=
            kRadiusUnits;

        samples[8] =
            spreadOrigin;
        samples[8].x -=
            kRadiusUnits;
        samples[8].y -=
            kRadiusUnits;

        std::array<
            RE::TESObjectCELL*,
            9> cells{};

        std::size_t cellCount{};

        for (const auto& sample :
             samples) {

            auto* cell =
                tes->GetCell(
                    sample);

            if (!cell) {
                continue;
            }

            bool duplicate{};

            for (std::size_t i = 0;
                 i < cellCount;
                 ++i) {

                if (cells[i] == cell) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                continue;
            }

            cells[cellCount++] =
                cell;
        }

        std::unordered_set<
            RE::FormID>
            seenActors{};

        std::uint32_t
            actorCount{};

        std::uint32_t
            hostileCount{};

        std::uint32_t
            eligibleCount{};

        std::uint32_t
            committedCount{};

        // Contract:
        // the actor directly struck by the projectile is always
        // considered by Ember Spread independently of Actor-origin
        // distance from the actual impact point.
        //
        // This matters for very large actors where the projectile
        // can hit the body while Actor::GetPosition() is farther
        // than the nominal 20 ft radius.
        {
            auto directTargetPtr =
                a_probe.targetHandle.get();

            auto* directTarget =
                directTargetPtr.get();

            if (directTarget &&
                directTarget != player) {

                const auto formID =
                    directTarget->
                        GetFormID();

                if (seenActors.
                        insert(formID).
                        second) {

                    ++actorCount;

                    const bool alive =
                        !directTarget->IsDead();

                    const bool hostile =
                        directTarget->
                            IsHostileToActor(
                                player);

                    if (hostile) {
                        ++hostileCount;
                    }

                    const float
                        distanceUnits =
                            Distance3D(
                                spreadOrigin,
                                directTarget->
                                    GetPosition());

                    const float
                        distanceFt =
                            distanceUnits /
                            kWorldUnitsPerFoot;

                    const bool
                        originWithin20ft =
                            distanceFt <=
                                kRadiusFt;

                    // Direct-target inclusion intentionally does
                    // not depend on Actor-origin distance.
                    const bool eligible =
                        alive &&
                        hostile;

                    if (eligible) {
                        ++eligibleCount;
                    }

                    bool committed{};

                    if (eligible) {
                        committed =
                            TULLBUNG::Classes::Hunter::
                                CommitEmberCandidate(
                                    a_probe.actionID,
                                    a_probe.targetFormID,
                                    directTarget);

                        if (committed) {
                            ++committedCount;
                        }
                    }

                    logger::info(
                        "[HUNTER-EMBER-SPREAD-DIRECT] ActionID={} | Actor={:08X} | Alive={} | Hostile={} | DistanceFt={:.3f} | OriginWithin20ft={} | GuaranteedCandidate={} | Committed={}",
                        a_probe.actionID,
                        formID,
                        alive,
                        hostile,
                        distanceFt,
                        originWithin20ft,
                        eligible,
                        committed);
                }
            }
        }

        for (std::size_t i = 0;
             i < cellCount;
             ++i) {

            auto* cell =
                cells[i];

            if (!cell) {
                continue;
            }

            cell->
                ForEachReferenceInRange(
                    spreadOrigin,
                    kRadiusUnits,
                    [&](RE::TESObjectREFR*
                            a_ref) {

                        auto* actor =
                            a_ref ?
                                a_ref->
                                    As<
                                        RE::Actor>() :
                                nullptr;

                        if (!actor ||
                            actor ==
                                player) {

                            return
                                RE::BSContainer::
                                    ForEachResult::
                                        kContinue;
                        }

                        const auto formID =
                            actor->
                                GetFormID();

                        if (!seenActors.
                                insert(formID).
                                second) {

                            return
                                RE::BSContainer::
                                    ForEachResult::
                                        kContinue;
                        }

                        ++actorCount;

                        const bool alive =
                            !actor->IsDead();

                        const bool hostile =
                            actor->
                                IsHostileToActor(
                                    player);

                        if (hostile) {
                            ++hostileCount;
                        }

                        const float
                            distanceUnits =
                                Distance3D(
                                    spreadOrigin,
                                    actor->
                                        GetPosition());

                        const float
                            distanceFt =
                                distanceUnits /
                                kWorldUnitsPerFoot;

                        const bool eligible =
                            alive &&
                            hostile &&
                            distanceFt <=
                                kRadiusFt;

                        if (eligible) {
                            ++eligibleCount;
                        }

                        bool committed{};

                        if (eligible) {
                            committed =
                                TULLBUNG::Classes::Hunter::
                                    CommitEmberCandidate(
                                        a_probe.actionID,
                                        a_probe.targetFormID,
                                        actor);

                            if (committed) {
                                ++committedCount;
                            }
                        }

                        logger::info(
                            "[HUNTER-EMBER-SPREAD-CANDIDATE] ActionID={} | Actor={:08X} | DirectTarget={} | Alive={} | Hostile={} | DistanceFt={:.3f} | RadiusEligible={} | Committed={}",
                            a_probe.actionID,
                            formID,
                            formID ==
                                a_probe.
                                    targetFormID,
                            alive,
                            hostile,
                            distanceFt,
                            eligible,
                            committed);

                        return
                            RE::BSContainer::
                                ForEachResult::
                                    kContinue;
                    });
        }

        logger::info(
            "[HUNTER-EMBER-SPREAD-SUMMARY] ActionID={} | Cells={} | UniqueActors={} | HostileActors={} | RadiusEligible={} | Committed={} | RadiusFt={:.1f}",
            a_probe.actionID,
            cellCount,
            actorCount,
            hostileCount,
            eligibleCount,
            committedCount,
            kRadiusFt);
        TULLBUNG::Classes::Hunter::
            FinalizeEmberRequest(
                a_probe.actionID,
                a_probe.targetFormID);
    }

    Snapshot::ExtendedActionState BuildSnapshotExtendedState(
        RE::Actor* a_actor)
    {
        Snapshot::ExtendedActionState state{};

        // Stage 10B:
        // ??쇱젫 ?遺우춭 caster ?怨밴묶??筌왖疫?亦낅슣?욃첎誘れ몵嚥?筌?쑴???
        // Void / Overcast / QuietProcessing / Precision / Siege??        // 揶???????怨밴묶 producer ?닌뗭겱 ?袁㏉돱筌왖 Known=false ?醫?.
        state.internalStateKnown =
            false;

        state.precisionStateKnown =
            false;

        state.siegeStateKnown =
            false;

        state.conditionStateKnown =
            false;

        const auto casting =
            Snapshot::CaptureCastingState(
                a_actor);

        state.castingStateKnown =
            casting.known;

        state.anySpellCasting =
            casting.anySpellCasting;

        state.leftHealingSpellCasting =
            casting.leftHealingSpellCasting;

        state.spellCharging =
            casting.charging;

        state.spellCasting =
            casting.casting;

        state.spellConcentrating =
            casting.concentrating;

        state.leftCasterState =
            casting.leftCasterState;

        state.rightCasterState =
            casting.rightCasterState;

        state.leftCastingSpellFormID =
            casting.leftSpellFormID;

        state.rightCastingSpellFormID =
            casting.rightSpellFormID;

        const auto& equipment =
            TULLBUNG::PlayerState::
                GetEquipment();

        state.priestStance =
            TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    equipment.left.spell);

        state.priestStanceKnown =
            true;

        TULLBUNG::ClassRuntime::
            PopulateActionExtendedState(
                a_actor,
                state);
        return state;
    }

    void StoreActionSnapshot(
        std::uint64_t a_actionID,
        const Snapshot::ActionSnapshot&
            a_snapshot)
    {
        if (!a_actionID ||
            !a_snapshot.valid) {

            return;
        }

        const auto store =
            [a_actionID, &a_snapshot](
                AttackActionState& a_action) {

                if (a_action.actionID !=
                    a_actionID) {

                    return;
                }

                a_action.actionSnapshot =
                    a_snapshot;

                a_action.snapshotCaptured =
                    true;
            };

        store(g_rightAction);
        store(g_leftAction);
        store(g_bothAction);
        store(g_eventAction);
        store(g_bashAction);
    }

    void EnsureActionSnapshot(
        AttackActionState& a_action)
    {
        if (!a_action.actionID ||
            !a_action.actor ||
            a_action.snapshotCaptured) {

            return;
        }

        const auto snapshot =
            Snapshot::CaptureAction(
                a_action.actor,
                a_action.actionID,
                BuildSnapshotExtendedState(
                    a_action.actor));

        StoreActionSnapshot(
            a_action.actionID,
            snapshot);

        // ACTION_START?癒?퐣 ????甕?疫꿸퀡以??뺣뼄.
        // fallback action筌?筌??醫륁뒞 Hit揶쎛 ACTION_START ??釉????뺣뼄.
        Snapshot::DebugLogAction(
            snapshot);
    }

    RE::TESForm* ResolveForm(
        RE::FormID a_formID)
    {
        if (!a_formID) {
            return nullptr;
        }

        const auto it =
            g_formCache.find(a_formID);

        if (it != g_formCache.end()) {
            return it->second;
        }

        auto* form =
            RE::TESForm::LookupByID(a_formID);

        g_formCache.emplace(
            a_formID,
            form);

        return form;
    }

    SourceKind GetSourceKind(
        RE::TESForm* a_source)
    {
        if (!a_source) {
            return SourceKind::kNone;
        }

        if (a_source->As<RE::TESObjectWEAP>()) {
            return SourceKind::kWeapon;
        }

        if (a_source->As<RE::SpellItem>()) {
            return SourceKind::kSpell;
        }

        return SourceKind::kOther;
    }

    SourceKind ResolveSourceKind(
        RE::TESForm* a_source,
        RE::TESObjectWEAP*& a_weapon,
        RE::SpellItem*& a_spell)
    {
        a_weapon = nullptr;
        a_spell = nullptr;

        if (!a_source) {
            return SourceKind::kNone;
        }

        a_weapon =
            a_source->As<RE::TESObjectWEAP>();

        if (a_weapon) {
            return SourceKind::kWeapon;
        }

        a_spell =
            a_source->As<RE::SpellItem>();

        if (a_spell) {
            return SourceKind::kSpell;
        }

        return SourceKind::kOther;
    }

    bool IsTwoHandedMelee(
        const RE::TESObjectWEAP* a_weapon)
    {
        if (!a_weapon) {
            return false;
        }

        using WC =
            TULLBUNG::Classification::WeaponClass;

        const auto weaponClass =
            TULLBUNG::Classification::
                ClassifyWeapon(a_weapon);

        return
            weaponClass == WC::kTwoHandSword ||
            weaponClass == WC::kTwoHandAxe;
    }

    Hand ResolveEquippedHand(
        RE::Actor* a_attacker,
        RE::TESForm* a_source)
    {
        if (!a_attacker ||
            !a_source) {

            return Hand::kUnknown;
        }

        auto* right =
            a_attacker->GetEquippedObject(false);

        auto* left =
            a_attacker->GetEquippedObject(true);

        const bool rightMatch =
            right == a_source;

        const bool leftMatch =
            left == a_source;

        if (rightMatch &&
            leftMatch) {

            return Hand::kBoth;
        }

        if (rightMatch) {
            return Hand::kRight;
        }

        if (leftMatch) {
            return Hand::kLeft;
        }

        return Hand::kUnknown;
    }

    Hand ResolveHitDataHand(
        RE::PlayerCharacter* a_player,
        RE::TESObjectWEAP* a_weapon,
        const RE::HitData& a_data)
    {
        if (!a_player ||
            !a_weapon) {

            return Hand::kUnknown;
        }

        if (IsTwoHandedMelee(a_weapon)) {
            return Hand::kBoth;
        }

        auto* right =
            a_player->GetEquippedObject(false);

        auto* left =
            a_player->GetEquippedObject(true);

        const bool rightMatch =
            right == a_weapon;

        const bool leftMatch =
            left == a_weapon;

        bool attackDataLeft{};

        if (a_data.attackData) {
            attackDataLeft =
                a_data.attackData->
                    IsLeftAttack();
        }

        const bool offhandFlag =
            a_data.flags.any(
                static_cast<RE::HitData::Flag>(
                    kHitDataOffhandMask));

        const bool resolvedLeft =
            attackDataLeft ||
            offhandFlag;

        if (rightMatch &&
            leftMatch) {

            return resolvedLeft ?
                Hand::kLeft :
                Hand::kRight;
        }

        if (resolvedLeft &&
            leftMatch) {

            return Hand::kLeft;
        }

        if (!resolvedLeft &&
            rightMatch) {

            return Hand::kRight;
        }

        if (rightMatch) {
            return Hand::kRight;
        }

        if (leftMatch) {
            return Hand::kLeft;
        }

        return Hand::kUnknown;
    }

    Hand ActionSlotToHand(
        SKSE::ActionEvent::Slot a_slot)
    {
        switch (a_slot) {
        case SKSE::ActionEvent::Slot::kLeft:
            return Hand::kLeft;

        case SKSE::ActionEvent::Slot::kRight:
            return Hand::kRight;

        case SKSE::ActionEvent::Slot::kVoice:
        default:
            return Hand::kUnknown;
        }
    }

    RE::MagicSystem::CastingSource
        ActionSlotToCastingSource(
            SKSE::ActionEvent::Slot a_slot)
    {
        switch (a_slot) {
        case SKSE::ActionEvent::Slot::kLeft:
            return
                RE::MagicSystem::
                    CastingSource::
                        kLeftHand;

        case SKSE::ActionEvent::Slot::kRight:
            return
                RE::MagicSystem::
                    CastingSource::
                        kRightHand;

        case SKSE::ActionEvent::Slot::kVoice:
        default:
            return
                RE::MagicSystem::
                    CastingSource::
                        kOther;
        }
    }

    bool SourceMatchesEquippedSlot(
        RE::Actor* a_actor,
        RE::TESForm* a_source,
        SKSE::ActionEvent::Slot a_slot)
    {
        if (!a_actor ||
            !a_source) {

            return false;
        }

        switch (a_slot) {
        case SKSE::ActionEvent::Slot::kLeft:
            return
                a_actor->GetEquippedObject(true) ==
                a_source;

        case SKSE::ActionEvent::Slot::kRight:
            return
                a_actor->GetEquippedObject(false) ==
                a_source;

        case SKSE::ActionEvent::Slot::kVoice:
        default:
            return false;
        }
    }

    bool CanUseDualWeaponAction(
        RE::Actor* a_actor)
    {
        if (!a_actor) {
            return false;
        }

        auto* rightForm =
            a_actor->GetEquippedObject(false);

        auto* leftForm =
            a_actor->GetEquippedObject(true);

        if (!rightForm ||
            !leftForm) {

            return false;
        }

        auto* rightWeapon =
            rightForm->As<RE::TESObjectWEAP>();

        auto* leftWeapon =
            leftForm->As<RE::TESObjectWEAP>();

        if (!rightWeapon ||
            !leftWeapon) {

            return false;
        }

        return
            TULLBUNG::Classification::
                IsMelee(rightWeapon) &&
            TULLBUNG::Classification::
                IsMelee(leftWeapon);
    }

    bool AreOppositeHands(
        Hand a_first,
        Hand a_second)
    {
        return
            (a_first == Hand::kRight &&
             a_second == Hand::kLeft) ||
            (a_first == Hand::kLeft &&
             a_second == Hand::kRight);
    }

    bool HandsCompatible(
        Hand a_expected,
        Hand a_actual)
    {
        if (a_expected == Hand::kUnknown ||
            a_actual == Hand::kUnknown) {

            return true;
        }

        if (a_expected == Hand::kBoth ||
            a_actual == Hand::kBoth) {

            return true;
        }

        return
            a_expected ==
            a_actual;
    }

    RangeKind ResolveRange(
        RE::TESObjectWEAP* a_weapon,
        RE::SpellItem* a_spell,
        RE::FormID a_projectile,
        AttackKind a_attackKind)
    {
        using namespace TULLBUNG;

        if (a_attackKind ==
            AttackKind::kBash) {

            return RangeKind::kMelee;
        }

        if (a_weapon) {
            if (Classification::IsMelee(
                    a_weapon)) {

                return RangeKind::kMelee;
            }

            if (Classification::IsRanged(
                    a_weapon)) {

                return RangeKind::kRanged;
            }
        }

        if (a_spell) {
            using Delivery =
                RE::MagicSystem::Delivery;

            switch (a_spell->GetDelivery()) {
            case Delivery::kTouch:
                return RangeKind::kMelee;

            case Delivery::kAimed:
            case Delivery::kTargetActor:
            case Delivery::kTargetLocation:
                return RangeKind::kRanged;

            default:
                break;
            }
        }

        if (a_projectile) {
            return RangeKind::kRanged;
        }

        return RangeKind::kUnknown;
    }

    const char* ActorName(
        const RE::Actor* a_actor)
    {
        if (!a_actor) {
            return "<null>";
        }

        const auto* name =
            a_actor->GetName();

        return
            name && name[0] ?
                name :
                "<unnamed>";
    }

    std::string_view HandName(
        Hand a_hand)
    {
        switch (a_hand) {
        case Hand::kRight:
            return "RIGHT";

        case Hand::kLeft:
            return "LEFT";

        case Hand::kBoth:
            return "BOTH";

        case Hand::kUnknown:
        default:
            return "UNKNOWN";
        }
    }

    std::string_view RangeName(
        RangeKind a_range)
    {
        switch (a_range) {
        case RangeKind::kMelee:
            return "MELEE";

        case RangeKind::kRanged:
            return "RANGED";

        case RangeKind::kUnknown:
        default:
            return "UNKNOWN";
        }
    }

    std::string_view AttackName(
        AttackKind a_attack)
    {
        switch (a_attack) {
        case AttackKind::kPower:
            return "POWER";

        case AttackKind::kBash:
            return "BASH";

        case AttackKind::kNormal:
        default:
            return "NORMAL";
        }
    }

    std::string_view SourceName(
        SourceKind a_source)
    {
        switch (a_source) {
        case SourceKind::kWeapon:
            return "WEAPON";

        case SourceKind::kSpell:
            return "SPELL";

        case SourceKind::kOther:
            return "OTHER";

        case SourceKind::kNone:
        default:
            return "NONE";
        }
    }

    std::string_view OriginName(
        ActionOrigin a_origin)
    {
        switch (a_origin) {
        case ActionOrigin::kWeaponSwing:
            return "WEAPON_SWING";

        case ActionOrigin::kDualWeaponSwing:
            return "DUAL_WEAPON_SWING";

        case ActionOrigin::kSpellCast:
            return "SPELL_CAST";

        case ActionOrigin::kSpellFireFallback:
            return "SPELL_FIRE_FALLBACK";

        case ActionOrigin::kBowRelease:
            return "BOW_RELEASE";

        case ActionOrigin::kWeaponHitFallback:
            return "WEAPON_HIT_FALLBACK";

        case ActionOrigin::kBashHit:
            return "BASH_HIT";

        case ActionOrigin::kNone:
        default:
            return "NONE";
        }
    }

    std::string_view ActionEventTypeName(
        SKSE::ActionEvent::Type a_type)
    {
        switch (a_type) {
        case SKSE::ActionEvent::Type::kWeaponSwing:
            return "WEAPON_SWING";

        case SKSE::ActionEvent::Type::kSpellCast:
            return "SPELL_CAST";

        case SKSE::ActionEvent::Type::kSpellFire:
            return "SPELL_FIRE";

        case SKSE::ActionEvent::Type::kVoiceCast:
            return "VOICE_CAST";

        case SKSE::ActionEvent::Type::kVoiceFire:
            return "VOICE_FIRE";

        case SKSE::ActionEvent::Type::kBowDraw:
            return "BOW_DRAW";

        case SKSE::ActionEvent::Type::kBowRelease:
            return "BOW_RELEASE";

        case SKSE::ActionEvent::Type::kBeginDraw:
            return "BEGIN_DRAW";

        case SKSE::ActionEvent::Type::kEndDraw:
            return "END_DRAW";

        case SKSE::ActionEvent::Type::kBeginSheathe:
            return "BEGIN_SHEATHE";

        case SKSE::ActionEvent::Type::kEndSheathe:
            return "END_SHEATHE";

        default:
            return "UNKNOWN";
        }
    }

    AttackActionState& WeaponLane(
        Hand a_hand)
    {
        switch (a_hand) {
        case Hand::kRight:
            return g_rightAction;

        case Hand::kLeft:
            return g_leftAction;

        case Hand::kBoth:
        case Hand::kUnknown:
        default:
            return g_bothAction;
        }
    }

    AttackActionState& OppositeWeaponLane(
        Hand a_hand)
    {
        if (a_hand == Hand::kRight) {
            return g_leftAction;
        }

        if (a_hand == Hand::kLeft) {
            return g_rightAction;
        }

        return g_bothAction;
    }

    void MarkActionProcessed(
        std::uint64_t a_actionID)
    {
        if (!a_actionID) {
            return;
        }

        auto mark =
            [a_actionID](AttackActionState& a_action) {
                if (a_action.actionID ==
                    a_actionID) {

                    a_action.firstValidDirectHitSeen =
                        true;
                }
            };

        mark(g_rightAction);
        mark(g_leftAction);
        mark(g_bothAction);
        mark(g_eventAction);
        mark(g_bashAction);
    }

    void BeginPlayerAction(
        AttackActionState& a_lane,
        RE::Actor* a_actor,
        RE::TESForm* a_source,
        Hand a_hand,
        ActionOrigin a_origin,
        std::uint64_t a_inputGeneration = 0)
    {
        if (!a_actor ||
            !a_source) {

            return;
        }

        a_lane = {};

        a_lane.actionID =
            g_nextActionID++;

        a_lane.inputGeneration =
            a_inputGeneration;

        a_lane.actor =
            a_actor;

        a_lane.sourceForm =
            a_source;

        a_lane.sourceFormID =
            a_source->GetFormID();

        a_lane.hand =
            a_hand;

        a_lane.sourceKind =
            GetSourceKind(a_source);

        a_lane.origin =
            a_origin;

        if (a_origin ==
                ActionOrigin::kWeaponSwing) {

            if (a_hand == Hand::kRight) {
                a_lane.rightSourceFormID =
                    a_lane.sourceFormID;

                a_lane.rightSwingSeen =
                    true;
            } else if (a_hand == Hand::kLeft) {
                a_lane.leftSourceFormID =
                    a_lane.sourceFormID;

                a_lane.leftSwingSeen =
                    true;
            } else if (a_hand == Hand::kBoth) {
                a_lane.rightSourceFormID =
                    a_lane.sourceFormID;

                a_lane.leftSourceFormID =
                    a_lane.sourceFormID;

                a_lane.rightSwingSeen =
                    true;

                a_lane.leftSwingSeen =
                    true;
            }
        }

        EnsureActionSnapshot(
            a_lane);

        TULLBUNG::ClassRuntime::
            OnPlayerActionStart(
                a_lane);

        logger::info(
            "[AttackAction] Begin | ID={} | Origin={} | Generation={} | Source={:08X} | SourceKind={} | Hand={}",
            a_lane.actionID,
            OriginName(
                a_lane.origin),
            a_lane.inputGeneration,
            a_lane.sourceFormID,
            SourceName(
                a_lane.sourceKind),
            HandName(
                a_lane.hand));
    }

    void PromotePendingWeaponActionsToDual(
        RE::Actor* a_actor,
        RE::TESForm* a_newSource,
        Hand a_newHand)
    {
        if (!a_actor ||
            !a_newSource ||
            !CanUseDualWeaponAction(
                a_actor)) {

            return;
        }

        auto& previousLane =
            OppositeWeaponLane(
                a_newHand);

        if (!previousLane.actionID ||
            previousLane.actor != a_actor ||
            previousLane.firstValidDirectHitSeen ||
            previousLane.dualAction ||
            previousLane.sourceKind !=
                SourceKind::kWeapon ||
            previousLane.origin !=
                ActionOrigin::kWeaponSwing ||
            !AreOppositeHands(
                previousLane.hand,
                a_newHand)) {

            return;
        }

        AttackActionState dual =
            previousLane;

        dual.dualAction =
            true;

        dual.hand =
            Hand::kBoth;

        dual.origin =
            ActionOrigin::kDualWeaponSwing;

        const auto newSourceFormID =
            a_newSource->GetFormID();

        if (previousLane.hand ==
            Hand::kRight) {

            dual.rightSourceFormID =
                previousLane.sourceFormID;

            dual.rightSwingSeen =
                true;
        } else if (previousLane.hand ==
                   Hand::kLeft) {

            dual.leftSourceFormID =
                previousLane.sourceFormID;

            dual.leftSwingSeen =
                true;
        }

        if (a_newHand == Hand::kRight) {
            dual.rightSourceFormID =
                newSourceFormID;

            dual.rightSwingSeen =
                true;
        } else if (a_newHand == Hand::kLeft) {
            dual.leftSourceFormID =
                newSourceFormID;

            dual.leftSwingSeen =
                true;
        }

        g_rightAction =
            dual;

        g_leftAction =
            dual;

        logger::info(
            "[AttackAction] PromoteDual | ID={} | RightSource={:08X} | LeftSource={:08X}",
            dual.actionID,
            dual.rightSourceFormID,
            dual.leftSourceFormID);
    }

    bool IsCompatibleDirectHit(
        const CombatContext& a_context,
        const AttackActionState& a_action)
    {
        if (!a_action.actionID ||
            !a_action.actor ||
            a_action.actor !=
                a_context.attacker) {

            return false;
        }

        if (a_action.dualAction) {
            if (!a_context.weaponDirectCandidate ||
                !a_context.sourceFormID) {

                return false;
            }

            switch (a_context.hand) {
            case Hand::kRight:
                return
                    a_action.rightSourceFormID &&
                    a_context.sourceFormID ==
                        a_action.rightSourceFormID;

            case Hand::kLeft:
                return
                    a_action.leftSourceFormID &&
                    a_context.sourceFormID ==
                        a_action.leftSourceFormID;

            case Hand::kBoth:
            case Hand::kUnknown:
            default:
                return
                    a_context.sourceFormID ==
                        a_action.rightSourceFormID ||
                    a_context.sourceFormID ==
                        a_action.leftSourceFormID;
            }
        }

        if (!a_context.sourceFormID ||
            a_context.sourceFormID !=
                a_action.sourceFormID) {

            return false;
        }

        if (!HandsCompatible(
                a_action.hand,
                a_context.hand)) {

            return false;
        }

        if (a_context.weaponDirectCandidate &&
            a_action.sourceKind ==
                SourceKind::kWeapon) {

            return true;
        }

        if (a_context.spellDirectCandidate &&
            a_action.sourceKind ==
                SourceKind::kSpell) {

            return true;
        }

        if (a_context.attackKind ==
                AttackKind::kBash &&
            a_action.origin ==
                ActionOrigin::kBashHit) {

            return true;
        }

        return false;
    }

    void ApplyMatchedAction(
        CombatContext& a_context,
        AttackActionState& a_action)
    {
        a_context.attackActionID =
            a_action.actionID;

        a_context.actionMatched =
            true;

        // SoT contract:
        // capture at the first valid processing point,
        // before any TULL effect may mutate resources/state.
        EnsureActionSnapshot(
            a_action);

        if (a_action.snapshotCaptured) {
            a_context.actionSnapshot =
                a_action.actionSnapshot;
        }

        if (!a_action.
                firstValidDirectHitSeen) {

            MarkActionProcessed(
                a_action.actionID);

            a_context.firstValidDirectHit =
                true;
        }
    }

    AttackActionState* FindMatchingMeleeAction(
        const CombatContext& a_context)
    {
        auto tryMatch =
            [&a_context](AttackActionState& a_action)
                -> AttackActionState* {

                return IsCompatibleDirectHit(
                    a_context,
                    a_action) ?
                        &a_action :
                        nullptr;
            };

        switch (a_context.hand) {
        case Hand::kRight:
            if (auto* match =
                    tryMatch(g_rightAction)) {
                return match;
            }
            break;

        case Hand::kLeft:
            if (auto* match =
                    tryMatch(g_leftAction)) {
                return match;
            }
            break;

        case Hand::kBoth:
            if (auto* match =
                    tryMatch(g_bothAction)) {
                return match;
            }
            break;

        case Hand::kUnknown:
        default:
            break;
        }

        if (auto* match =
                tryMatch(g_rightAction)) {
            return match;
        }

        if (auto* match =
                tryMatch(g_leftAction)) {
            return match;
        }

        if (auto* match =
                tryMatch(g_bothAction)) {
            return match;
        }

        return nullptr;
    }

    void HandleMeleeWeaponSwing(
        RE::Actor* a_actor,
        RE::TESForm* a_source,
        Hand a_hand)
    {
        if (!a_actor ||
            !a_source) {

            return;
        }

        auto& lane =
            WeaponLane(a_hand);

        const auto inputGeneration =
            a_hand == Hand::kLeft ?
                g_leftAttackGeneration :
            a_hand == Hand::kRight ?
                g_rightAttackGeneration :
                (std::max)(
                    g_leftAttackGeneration,
                    g_rightAttackGeneration);

        if (lane.actionID &&
            lane.actor == a_actor &&
            lane.sourceKind ==
                SourceKind::kWeapon &&
            !lane.firstValidDirectHitSeen) {

            const bool duplicateSingle =
                !lane.dualAction &&
                lane.origin ==
                    ActionOrigin::kWeaponSwing &&
                lane.sourceForm ==
                    a_source &&
                lane.hand ==
                    a_hand &&
                inputGeneration != 0 &&
                lane.inputGeneration ==
                    inputGeneration;

            const bool duplicateDual =
                lane.dualAction &&
                ((a_hand == Hand::kRight &&
                  lane.rightSwingSeen &&
                  lane.rightSourceFormID ==
                      a_source->GetFormID()) ||
                 (a_hand == Hand::kLeft &&
                  lane.leftSwingSeen &&
                  lane.leftSourceFormID ==
                      a_source->GetFormID()));

            if (duplicateSingle ||
                duplicateDual) {

                logger::info(
                    "[AttackAction] Continue | ID={} | Event=DUPLICATE_WEAPON_SWING | Hand={} | Source={:08X}",
                    lane.actionID,
                    HandName(a_hand),
                    a_source->GetFormID());

                return;
            }
        }

        if ((a_hand == Hand::kRight ||
             a_hand == Hand::kLeft) &&
            CanUseDualWeaponAction(
                a_actor)) {

            auto& opposite =
                OppositeWeaponLane(
                    a_hand);

            const bool pendingOpposite =
                opposite.actionID &&
                opposite.actor ==
                    a_actor &&
                opposite.sourceKind ==
                    SourceKind::kWeapon &&
                opposite.origin ==
                    ActionOrigin::kWeaponSwing &&
                !opposite.dualAction &&
                !opposite.
                    firstValidDirectHitSeen &&
                AreOppositeHands(
                    opposite.hand,
                    a_hand);

            if (pendingOpposite) {
                PromotePendingWeaponActionsToDual(
                    a_actor,
                    a_source,
                    a_hand);

                return;
            }
        }

        BeginPlayerAction(
            lane,
            a_actor,
            a_source,
            a_hand,
            ActionOrigin::kWeaponSwing,
            inputGeneration);
    }

    void AttachEventAction(
        CombatContext& a_context)
    {
        if (!a_context.playerAttacker) {
            return;
        }

        if (IsCompatibleDirectHit(
                a_context,
                g_eventAction)) {

            ApplyMatchedAction(
                a_context,
                g_eventAction);

            return;
        }

        if (a_context.weaponDirectCandidate) {
            BeginPlayerAction(
                g_eventAction,
                a_context.attacker,
                a_context.sourceForm,
                a_context.hand,
                ActionOrigin::
                    kWeaponHitFallback);

            if (IsCompatibleDirectHit(
                    a_context,
                    g_eventAction)) {

                ApplyMatchedAction(
                    a_context,
                    g_eventAction);
            }

            return;
        }

        if (a_context.spellDirectCandidate) {
            logger::info(
                "[AttackAction] Ignore unmatched spell hit | Source={:08X}",
                a_context.sourceFormID);
        }
    }

    void AttachBashEventAction(
        CombatContext& a_context)
    {
        if (!a_context.playerAttacker ||
            !a_context.sourceForm) {

            return;
        }

        const bool sameBashGeneration =
            g_bashInputGeneration != 0 &&
            g_bashAction.actionID &&
            g_bashAction.actor ==
                a_context.attacker &&
            g_bashAction.origin ==
                ActionOrigin::kBashHit &&
            g_bashAction.inputGeneration ==
                g_bashInputGeneration;

        if (!sameBashGeneration) {
            BeginPlayerAction(
                g_bashAction,
                a_context.attacker,
                a_context.sourceForm,
                a_context.hand,
                ActionOrigin::kBashHit,
                g_bashInputGeneration);
        }

        ApplyMatchedAction(
            a_context,
            g_bashAction);
    }

    void AttachHitDataAction(
        CombatContext& a_context)
    {
        if (!a_context.playerAttacker ||
            !a_context.weaponDirectCandidate) {

            return;
        }

        if (a_context.range ==
                RangeKind::kRanged &&
            g_eventAction.actionID &&
            g_eventAction.origin ==
                ActionOrigin::kBowRelease &&
            IsCompatibleDirectHit(
                a_context,
                g_eventAction)) {


            ApplyMatchedAction(
                a_context,
                g_eventAction);

            return;
        }

        if (auto* action =
                FindMatchingMeleeAction(
                    a_context)) {

            ApplyMatchedAction(
                a_context,
                *action);

            return;
        }

        auto& lane =
            WeaponLane(
                a_context.hand);

        BeginPlayerAction(
            lane,
            a_context.attacker,
            a_context.sourceForm,
            a_context.hand,
            ActionOrigin::
                kWeaponHitFallback);

        ApplyMatchedAction(
            a_context,
            lane);
    }

    void DebugLogHitDataContext(
        const CombatContext& a_context,
        const RE::HitData& a_data)
    {
        logger::info(
            "[HitDataContext] Seq={} | Attacker=\"{}\"({:08X}) | Target=\"{}\"({:08X}) | Weapon={:08X} | Hand={} | Attack={} | Sneak={} | Blocked={} | Physical={:.3f} | Total={:.3f} | ActionID={} | ActionMatched={} | FirstDirect={}",
            a_context.rawEventSequence,
            ActorName(
                a_context.attacker),
            a_context.attacker ?
                a_context.attacker->GetFormID() :
                0,
            ActorName(
                a_context.target),
            a_context.target ?
                a_context.target->GetFormID() :
                0,
            a_context.sourceFormID,
            HandName(
                a_context.hand),
            AttackName(
                a_context.attackKind),
            a_context.sneakAttack,
            a_context.blocked,
            a_data.physicalDamage,
            a_data.totalDamage,
            a_context.attackActionID,
            a_context.actionMatched,
            a_context.firstValidDirectHit);
    }

    void ProcessPlayerHitData(
        RE::TESObjectREFR* a_target,
        RE::Actor* a_aggressor,
        RE::TESObjectWEAP* a_weapon,
        RE::HitData& a_data)
    {
        g_pendingWeaponDirectHit = {};

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            !a_target) {

            return;
        }

        if (TULLBUNG::DamageRules::
                IsTriggerSuppressed()) {

            logger::info(
                "[TriggerFirewall] HitData suppressed | Origin={}",
                TULLBUNG::DamageRules::
                    FirewallOriginName(
                        TULLBUNG::DamageRules::
                            GetFirewallOrigin()));

            return;
        }

        auto* targetActor =
            a_target->As<RE::Actor>();

        if (!targetActor) {
            return;
        }

        if (a_aggressor != player) {
            return;
        }

        const bool bash =
            a_data.flags.any(
                RE::HitData::Flag::kBash);

        // Shield bash HitData can legitimately arrive with a null
        // HitData::weapon.  Do not reject it at the common guard.
        // Resolve a stable source form from cached equipment instead.
        if (bash) {
            RE::TESForm* bashSource =
                a_weapon;

            Hand bashHand =
                Hand::kUnknown;

            if (a_weapon) {
                bashHand =
                    ResolveHitDataHand(
                        player,
                        a_weapon,
                        a_data);
            } else {
                const auto& equipment =
                    TULLBUNG::PlayerState::
                        GetEquipment();

                if (equipment.hasShield &&
                    equipment.left.armor) {

                    bashSource =
                        equipment.left.armor;

                    bashHand =
                        Hand::kLeft;
                } else if (
                    equipment.left.form) {

                    bashSource =
                        equipment.left.form;

                    bashHand =
                        Hand::kLeft;
                } else if (
                    equipment.right.form) {

                    bashSource =
                        equipment.right.form;

                    bashHand =
                        Hand::kRight;
                }
            }

            if (!bashSource) {
                logger::warn(
                    "[S12BASH_HITDATA] Source unresolved | WeaponNull={} | ShieldCached={}",
                    a_weapon == nullptr,
                    TULLBUNG::PlayerState::
                        GetEquipment().hasShield);

                return;
            }

            CombatContext context{};

            context.attacker =
                player;

            context.target =
                targetActor;

            context.sourceForm =
                bashSource;

            // This may remain null for a shield bash.  The sourceForm
            // above is the action identity; the HitData remains the
            // authoritative physical-damage carrier.
            context.weapon =
                a_weapon;

            context.sourceFormID =
                bashSource->GetFormID();

            context.sourceKind =
                SourceKind::kWeapon;

            context.hand =
                bashHand;

            context.range =
                RangeKind::kMelee;

            context.attackKind =
                AttackKind::kBash;

            context.playerAttacker =
                true;

            context.playerTarget =
                false;

            context.weaponDirectCandidate =
                false;

            context.spellDirectCandidate =
                false;

            context.directDamage =
                a_data.totalDamage;

            context.directDamageKnown =
                true;

            context.rawEventSequence =
                ++g_rawEventSequence;

            AttachBashEventAction(
                context);

            Snapshot::HitStateInput hitState{};

            TULLBUNG::ClassRuntime::
                PopulateHitState(
                    targetActor,
                    hitState);

            hitState.directDamage =
                a_data.totalDamage;

            hitState.directDamageKnown =
                true;

            context.hitSnapshot =
                Snapshot::CaptureHit(
                    targetActor,
                    context.attackActionID,
                    hitState);

            Snapshot::DebugLogHit(
                context.hitSnapshot);


            TULLBUNG::ClassRuntime::
                OnPlayerBashHitData(
                    context,
                    a_data);

            DebugLogHitDataContext(
                context,
                a_data);

            return;
        }

        // Non-bash direct weapon damage still requires a real
        // TESObjectWEAP authority.
        if (!a_weapon) {
            return;
        }

        const bool isMeleeWeapon =
            TULLBUNG::Classification::
                IsMelee(a_weapon);

        const bool isRangedWeapon =
            TULLBUNG::Classification::
                IsRanged(a_weapon);

        if (!isMeleeWeapon &&
            !isRangedWeapon) {

            return;
        }

        CombatContext context{};

        context.attacker =
            player;

        context.target =
            targetActor;

        context.sourceForm =
            a_weapon;

        context.weapon =
            a_weapon;

        context.sourceFormID =
            a_weapon->GetFormID();

        context.sourceKind =
            SourceKind::kWeapon;

        context.hand =
            ResolveHitDataHand(
                player,
                a_weapon,
                a_data);

        context.range =
            isRangedWeapon ?
                RangeKind::kRanged :
                RangeKind::kMelee;

        if (a_data.flags.any(
                RE::HitData::Flag::
                    kPowerAttack)) {

            context.attackKind =
                AttackKind::kPower;
        } else {
            context.attackKind =
                AttackKind::kNormal;
        }

        context.sneakAttack =
            a_data.flags.any(
                RE::HitData::Flag::
                    kSneakAttack);

        context.blocked =
            a_data.flags.any(
                RE::HitData::Flag::
                    kBlocked);

        context.playerAttacker =
            true;

        context.playerTarget =
            false;

        context.weaponDirectCandidate =
            !a_weapon->IsStaff();

        context.spellDirectCandidate =
            false;

        // Production authority:
        // totalDamage is the gameplay-authoritative weapon
        // direct damage value at this hook point.
        context.directDamage =
            a_data.totalDamage;

        context.directDamageKnown =
            true;

        context.rawEventSequence =
            ++g_rawEventSequence;

        AttachHitDataAction(
            context);

        Snapshot::HitStateInput hitState{};

        TULLBUNG::ClassRuntime::
            PopulateHitState(
                targetActor,
                hitState);

        hitState.directDamage =
            a_data.totalDamage;

        hitState.directDamageKnown =
            true;

        context.hitSnapshot =
            Snapshot::CaptureHit(
                targetActor,
                context.attackActionID,
                hitState);

        Snapshot::DebugLogHit(
            context.hitSnapshot);

        TULLBUNG::ClassRuntime::
            OnPlayerWeaponDirectHit(
                context,
                a_data);

        g_pendingWeaponDirectHit.valid =
            context.weaponDirectCandidate;

        g_pendingWeaponDirectHit.actionID =
            context.attackActionID;

        g_pendingWeaponDirectHit.targetFormID =
            targetActor->GetFormID();

        g_pendingWeaponDirectHit.targetHandle =
            targetActor;

        g_pendingWeaponDirectHit.ranged =
            context.range ==
                RangeKind::kRanged;

        g_pendingWeaponDirectHit.hitPosition =
            a_data.hitPosition;

        if (g_pendingWeaponDirectHit.ranged &&
            g_rangedLaunchState.valid &&
            g_rangedLaunchState.actionID ==
                context.attackActionID &&
            g_rangedLaunchState.sourceFormID ==
                context.sourceFormID) {

            g_pendingWeaponDirectHit.launchKnown =
                true;

            g_pendingWeaponDirectHit.distanceFt =
                Distance3D(
                    g_rangedLaunchState.position,
                    a_data.hitPosition) /
                kWorldUnitsPerFoot;
        }

        ProcessEmberSpread(
            g_pendingWeaponDirectHit);
        DebugLogHitDataContext(
            context,
            a_data);
    }

    void ProcessPlayerHitDataPost(
        RE::TESObjectREFR* a_target,
        RE::Actor* a_aggressor,
        const RE::HitData& a_data)
    {
        (void)a_data;

        auto pending =
            g_pendingWeaponDirectHit;

        g_pendingWeaponDirectHit = {};

        if (!pending.valid ||
            !a_target ||
            !a_aggressor) {

            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        auto* targetActor =
            a_target->As<RE::Actor>();

        if (!player ||
            a_aggressor != player ||
            !targetActor ||
            targetActor->GetFormID() !=
                pending.targetFormID) {

            return;
        }

        TULLBUNG::ClassRuntime::
            OnPlayerWeaponDirectHitPost(
                pending.actionID,
                targetActor);
    }
    void ProcessPlayerIncomingHitData(
        RE::TESObjectREFR* a_target,
        RE::Actor* a_aggressor,
        RE::HitData& a_data,
        const Snapshot::ActorResources&
            a_playerBefore,
        const Snapshot::CastingState&
            a_castingBefore,
        float a_playerHealthAfter)
    {
        // Stage 10C:
        // The immediate post-original HP read is intentionally
        // ignored. Runtime testing showed ActorValue health may
        // not yet be committed at this point.
        (void)a_playerHealthAfter;

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            a_target != player ||
            !a_aggressor ||
            a_aggressor == player) {

            return;
        }

        Snapshot::IncomingSnapshot pending{};

        pending.valid = true;

        pending.attackerFormID =
            a_aggressor->GetFormID();

        pending.playerBefore =
            a_playerBefore;

        pending.attackerMaxHealth =
            Snapshot::CaptureResource(
                a_aggressor,
                RE::ActorValue::kHealth).
                    maximum;

        pending.attackerMonster =
            TULLBUNG::Classification::
                IsMonster(
                    a_aggressor);

        pending.castingBefore =
            a_castingBefore;

        pending.blocked =
            a_data.flags.any(
                RE::HitData::Flag::
                    kBlocked);


        pending.physicalDamage =
            a_data.physicalDamage;

        pending.totalDamage =
            a_data.totalDamage;

        pending.damageKnown =
            true;

        // Until a deferred ActorValue read succeeds, do not claim
        // that the immediate value was authoritative.
        pending.playerHealthAfter =
            a_playerBefore.health.current;

        pending.observedHealthLost =
            0.0f;

        pending.observedHealthLostKnown =
            false;

        TULLBUNG::ClassRuntime::
            OnPlayerIncomingHit(
                a_aggressor,
                a_data,
                a_playerBefore);

        auto* taskInterface =
            SKSE::GetTaskInterface();

        if (!taskInterface) {
            logger::warn(
                "[SnapshotIncomingDeferred] TaskInterface unavailable | Attacker={:08X}",
                pending.attackerFormID);

            Snapshot::DebugLogIncoming(
                pending);

            return;
        }

        // First bounded deferred read.
        taskInterface->AddTask(
            [pending]() mutable {

                auto* deferredPlayer =
                    RE::PlayerCharacter::
                        GetSingleton();

                if (!deferredPlayer) {
                    return;
                }

                const auto after =
                    Snapshot::CaptureResources(
                        deferredPlayer);

                pending.playerHealthAfter =
                    after.health.current;

                pending.observedHealthLost =
                    (std::max)(
                        0.0f,
                        pending.playerBefore.
                                health.current -
                            after.health.current);

                // In the ordinary case the ActorValue commit
                // should be visible on this task pass.
                if (pending.observedHealthLost >
                        0.001f ||
                    pending.totalDamage <=
                        0.001f) {

                    pending.observedHealthLostKnown =
                        true;

                    logger::info(
                        "[SnapshotIncomingDeferred] Pass=1 | Attacker={:08X}",
                        pending.attackerFormID);

                    Snapshot::DebugLogIncoming(
                        pending);

                    return;
                }

                auto* secondTaskInterface =
                    SKSE::GetTaskInterface();

                if (!secondTaskInterface) {
                    logger::warn(
                        "[SnapshotIncomingDeferred] Second task unavailable | Attacker={:08X}",
                        pending.attackerFormID);

                    Snapshot::DebugLogIncoming(
                        pending);

                    return;
                }

                // One additional bounded retry only.
                // This is not polling: maximum two queued reads
                // for one incoming physical hit.
                secondTaskInterface->AddTask(
                    [pending]() mutable {

                        auto* secondPlayer =
                            RE::PlayerCharacter::
                                GetSingleton();

                        if (!secondPlayer) {
                            return;
                        }

                        const auto after =
                            Snapshot::CaptureResources(
                                secondPlayer);

                        pending.playerHealthAfter =
                            after.health.current;

                        pending.observedHealthLost =
                            (std::max)(
                                0.0f,
                                pending.playerBefore.
                                        health.current -
                                    after.health.current);

                        pending.observedHealthLostKnown =
                            true;

                        logger::info(
                            "[SnapshotIncomingDeferred] Pass=2 | Attacker={:08X}",
                            pending.attackerFormID);

                        Snapshot::DebugLogIncoming(
                            pending);
                    });
            });
    }

    enum class IncomingDirectKind :
        std::uint8_t
    {
        kWeapon = 0,
        kSpell,
        kDiscard
    };


    struct IncomingDirectDamageFrame final
    {
        bool reflectEligible{};

        IncomingDirectKind kind{
            IncomingDirectKind::kDiscard
        };

        RE::Actor* source{};

        std::uint64_t actionID{};

        float actualPostDefenseDamage{};

        // Diagnostic metadata only: never consulted by damage/eligibility decisions.
        std::uint64_t diagnosticScopeID{};
        std::uint64_t diagnosticEventID{};
        RE::FormID diagnosticAttackerID{};
        RE::FormID diagnosticSpellID{};
        RE::FormID diagnosticMgefID{};
    };


    struct IncomingDirectScopeToken final
    {
        bool pushed{};
        std::size_t index{};
        std::uint64_t diagnosticScopeID{};
        std::uint64_t diagnosticEventID{};
    };


    inline constexpr std::size_t
        kIncomingDirectStackCapacity = 8;


    thread_local std::array<
        IncomingDirectDamageFrame,
        kIncomingDirectStackCapacity>
            g_incomingDirectStack{};

    thread_local std::size_t
        g_incomingDirectDepth{};

    std::atomic<std::uint64_t> g_nextBarrierDiagnosticID{ 1 };
    // Last observed event is a log breadcrumb, not damage correlation authority.
    thread_local std::uint64_t g_lastBarrierSpellDiagnosticEventID{};


    [[nodiscard]]
    bool IsMageBarrierReflectEligible(
        const Snapshot::CastingState&
            a_casting) noexcept
    {
        return
            !TULLBUNG::DamageRules::
                IsTriggerSuppressed() &&
            a_casting.known &&
            a_casting.anySpellCasting &&
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kMageBarrier);
    }


    [[nodiscard]]
    IncomingDirectScopeToken
        BeginIncomingDirectDamageScope(
            RE::Actor* a_source,
            bool a_reflectEligible,
            std::uint64_t a_actionID,
            IncomingDirectKind a_kind,
            std::uint64_t a_diagnosticEventID = 0,
            RE::FormID a_diagnosticSpellID = 0,
            RE::FormID a_diagnosticMgefID = 0) noexcept
    {
        IncomingDirectScopeToken token{};
        token.diagnosticEventID = a_diagnosticEventID;
        token.diagnosticScopeID = g_nextBarrierDiagnosticID.fetch_add(1, std::memory_order_relaxed);

        if (g_incomingDirectDepth >=
            kIncomingDirectStackCapacity) {

            logger::error(
                "[S15BARRIER-TX] Result=STACK_FULL | Depth={}",
                g_incomingDirectDepth);

            if (a_diagnosticEventID != 0) {
                logger::trace(
                    "[S15BARRIER-SCOPE] Phase=BEGIN | Scope={} | Event={} | DepthBefore={} | DepthAfter={} | Attacker={:08X} | Spell={:08X} | MGEF={:08X} | Reason=STACK_FULL",
                    token.diagnosticScopeID, a_diagnosticEventID, g_incomingDirectDepth, g_incomingDirectDepth,
                    a_source ? a_source->GetFormID() : 0, a_diagnosticSpellID, a_diagnosticMgefID);
            }
            return token;
        }

        token.pushed = true;
        token.index =
            g_incomingDirectDepth;

        auto& frame =
            g_incomingDirectStack[
                g_incomingDirectDepth++];

        frame = {};
        frame.reflectEligible =
            a_reflectEligible;
        frame.kind =
            a_kind;
        frame.source =
            a_source;
        frame.actionID =
            a_actionID;

        frame.diagnosticScopeID = token.diagnosticScopeID;
        frame.diagnosticEventID = a_diagnosticEventID;
        frame.diagnosticAttackerID = a_source ? a_source->GetFormID() : 0;
        frame.diagnosticSpellID = a_diagnosticSpellID;
        frame.diagnosticMgefID = a_diagnosticMgefID;
        if (a_diagnosticEventID != 0) {
            logger::trace(
                "[S15BARRIER-SCOPE] Phase=BEGIN | Scope={} | Event={} | ThreadTag={} | DepthBefore={} | DepthAfter={} | Attacker={:08X} | Spell={:08X} | MGEF={:08X} | Kind={} | Eligible={} | Reason=OPEN",
                frame.diagnosticScopeID, frame.diagnosticEventID,
                reinterpret_cast<std::uintptr_t>(&g_incomingDirectDepth), token.index, g_incomingDirectDepth,
                frame.diagnosticAttackerID, a_diagnosticSpellID, a_diagnosticMgefID,
                static_cast<unsigned>(a_kind), a_reflectEligible);
        }
        return token;
    }


    void EndIncomingDirectDamageScope(
        IncomingDirectScopeToken
            a_token) noexcept
    {
        if (!a_token.pushed) {
            if (a_token.diagnosticEventID != 0) {
                logger::trace(
                    "[S15BARRIER-SCOPE] Phase=END | Scope={} | Event={} | DepthBefore={} | DepthAfter={} | Reason=NOT_PUSHED",
                    a_token.diagnosticScopeID, a_token.diagnosticEventID,
                    g_incomingDirectDepth, g_incomingDirectDepth);
            }
            return;
        }

        if (g_incomingDirectDepth == 0 ||
            a_token.index !=
                g_incomingDirectDepth - 1) {

            logger::error(
                "[S15BARRIER-TX] Result=STACK_MISMATCH | Token={} | Depth={}",
                a_token.index,
                g_incomingDirectDepth);

            if (a_token.diagnosticEventID != 0) {
                logger::trace(
                    "[S15BARRIER-SCOPE] Phase=END | Scope={} | Event={} | DepthBefore={} | DepthAfter=0 | Reason=STACK_MISMATCH",
                    a_token.diagnosticScopeID, a_token.diagnosticEventID, g_incomingDirectDepth);
            }
            g_incomingDirectDepth = 0;
            g_incomingDirectStack = {};
            return;
        }

        const auto frame =
            g_incomingDirectStack[
                a_token.index];

        g_incomingDirectStack[
            a_token.index] = {};

        --g_incomingDirectDepth;

        const auto traceEnd = [&](const char* a_reason) {
            if (frame.diagnosticEventID == 0) {
                return;
            }
            logger::trace(
                "[S15BARRIER-SCOPE] Phase=END | ThreadTag={} | Scope={} | Event={} | DepthBefore={} | DepthAfter={} | Attacker={:08X} | Spell={:08X} | MGEF={:08X} | Eligible={} | Accumulated={:.6f} | Reason={}",
                reinterpret_cast<std::uintptr_t>(&g_incomingDirectDepth),
                frame.diagnosticScopeID, frame.diagnosticEventID,
                a_token.index + 1, g_incomingDirectDepth,
                frame.diagnosticAttackerID, frame.diagnosticSpellID,
                frame.diagnosticMgefID, frame.reflectEligible,
                frame.actualPostDefenseDamage, a_reason);
        };

        if (!frame.reflectEligible ||
            !frame.source ||
            !std::isfinite(
                frame.actualPostDefenseDamage) ||
            frame.actualPostDefenseDamage <=
                0.0f) {

            traceEnd(!frame.reflectEligible ? "INELIGIBLE" :
                !frame.source ? "NO_SOURCE" :
                !std::isfinite(frame.actualPostDefenseDamage) ? "NONFINITE_ACCUMULATED" :
                "NO_POST_DEFENSE_DAMAGE");
            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            frame.source == player ||
            frame.source->IsDead()) {

            traceEnd(!player ? "NO_PLAYER" : frame.source == player ? "SELF_SOURCE" : "SOURCE_DEAD");
            return;
        }

        const float reflectedDamage =
            frame.actualPostDefenseDamage *
            0.20f;

        if (!std::isfinite(
                reflectedDamage) ||
            reflectedDamage <= 0.0f) {

            traceEnd("INVALID_REFLECTED_DAMAGE");
            return;
        }

        const bool applied =
            TULLBUNG::DamageRules::
                ApplyResolvedHealthDamage(
                    player,
                    frame.source,
                    reflectedDamage,
                    TULLBUNG::DamageRules::
                        FirewallOrigin::
                            kSecondaryDamage);

        traceEnd(applied ? "APPLIED" : "SECONDARY_BLOCKED");

        logger::info(
            "[S15BARRIER-REFLECT] Result={} | Kind={} | ActionID={} | Attacker={:08X} | ActualPostDefense={:.4f} | Reflected={:.4f}",
            applied ?
                "APPLIED" :
                "BLOCKED",
            frame.kind ==
                    IncomingDirectKind::
                        kWeapon ?
                "WEAPON" :
                "SPELL",
            frame.actionID,
            frame.source->
                GetFormID(),
            frame.actualPostDefenseDamage,
            reflectedDamage);
    }

    namespace HealthDamageProbeHook
    {
        using func_t =
            void (*)(
                RE::Actor*,
                RE::Actor*,
                float);

        static inline func_t func{
            nullptr
        };

        void thunk(
            RE::Actor* a_target,
            RE::Actor* a_attacker,
            float a_damage)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            auto* playerOwner =
                player ?
                    player->AsActorValueOwner() :
                    nullptr;

            const bool playerTarget =
                player &&
                a_target == player;

            const std::uint64_t sequence =
                playerTarget ?
                    ++g_healthDamageProbeSequence :
                    0;

            const auto attackerID =
                a_attacker ?
                    a_attacker->GetFormID() :
                    0;

            float healthBefore = 0.0f;

            if (playerTarget && playerOwner) {
                healthBefore =
                    playerOwner->GetActorValue(
                        RE::ActorValue::kHealth);
            }

            IncomingDirectScopeToken
                barrierWeaponTransaction{};

            const bool pendingWeaponReflect =
                playerTarget &&
                playerOwner &&
                a_attacker &&
                g_incomingDirectDepth == 0 &&
                g_pendingBarrierWeaponReflect.valid &&
                g_pendingBarrierWeaponReflect.attackerFormID ==
                    attackerID &&
                std::isfinite(a_damage) &&
                a_damage < 0.0f;

            // If the same actor now damages the player through an
            // already-authorized spell scope, discard any stale
            // weapon token instead of misclassifying spell damage.
            if (playerTarget &&
                a_attacker &&
                g_pendingBarrierWeaponReflect.valid &&
                g_pendingBarrierWeaponReflect.attackerFormID ==
                    attackerID &&
                g_incomingDirectDepth > 0) {

                g_pendingBarrierWeaponReflect = {};
            }

            if (pendingWeaponReflect) {
                g_pendingBarrierWeaponReflect = {};

                barrierWeaponTransaction =
                    BeginIncomingDirectDamageScope(
                        a_attacker,
                        true,
                        0,
                        IncomingDirectKind::
                            kWeapon);
            }

            func(
                a_target,
                a_attacker,
                a_damage);

            if (!playerTarget || !playerOwner) {
                return;
            }

            const float healthAfter =
                playerOwner->GetActorValue(
                    RE::ActorValue::kHealth);

            const float immediateLost =
                healthBefore > healthAfter ?
                    healthBefore - healthAfter :
                    0.0f;

            const bool negativeDamage =
                std::isfinite(a_damage) &&
                a_damage < 0.0f;

            const float candidatePostDamage =
                negativeDamage ?
                    -a_damage :
                    0.0f;

            if (
                candidatePostDamage > 0.0f &&
                g_incomingDirectDepth > 0
            ) {
                auto& frame =
                    g_incomingDirectStack[
                        g_incomingDirectDepth -
                        1];

                frame.actualPostDefenseDamage +=
                    candidatePostDamage;
            }

            const auto* diagnosticFrame = g_incomingDirectDepth > 0 ?
                &g_incomingDirectStack[g_incomingDirectDepth - 1] : nullptr;
            // LastObservedSpellEvent is a breadcrumb, never damage authorization or correlation.
            logger::trace(
                "[S15POST-PROBE] Seq={} | Attacker={:08X} | RawDamage={:.4f} | NegativeDamage={} | CandidatePostDamage={:.4f} | HealthBefore={:.4f} | HealthAfter={:.4f} | ImmediateLost={:.4f} | ThreadTag={} | Scope={} | Event={} | LastObservedSpellEvent={} | Depth={} | ScopeAttacker={:08X} | Spell={:08X} | MGEF={:08X} | Accumulated={:.6f} | InScope={}",
                sequence,
                attackerID,
                a_damage,
                negativeDamage,
                candidatePostDamage,
                healthBefore,
                healthAfter,
                immediateLost, reinterpret_cast<std::uintptr_t>(&g_incomingDirectDepth),
                diagnosticFrame ? diagnosticFrame->diagnosticScopeID : 0,
                diagnosticFrame ? diagnosticFrame->diagnosticEventID : 0,
                g_lastBarrierSpellDiagnosticEventID, g_incomingDirectDepth,
                diagnosticFrame ? diagnosticFrame->diagnosticAttackerID : 0,
                diagnosticFrame ? diagnosticFrame->diagnosticSpellID : 0,
                diagnosticFrame ? diagnosticFrame->diagnosticMgefID : 0,
                diagnosticFrame ? diagnosticFrame->actualPostDefenseDamage : 0.0f,
                diagnosticFrame != nullptr);

            EndIncomingDirectDamageScope(
                barrierWeaponTransaction);
        }

        bool Install()
        {
            if (g_healthDamageProbeHookInstalled) {
                return true;
            }

            REL::Relocation<
                std::uintptr_t>
                vtbl{
                    RE::PlayerCharacter::
                        VTABLE[0]
                };

            const auto original =
                vtbl.write_vfunc(
                    0x104,
                    thunk);

            func =
                reinterpret_cast<func_t>(
                    original);

            if (!func) {
                logger::error(
                    "[Combat] S15 post-damage probe hook original = NULL");

                return false;
            }

            g_healthDamageProbeHookInstalled =
                true;

            logger::info(
                "[Combat] S15 post-damage probe hook = OK | PlayerCharacter::HandleHealthDamage vfunc=0x104 | Scope=SE/AE");

            return true;
        }
    }

    namespace CastingLifecycleHook
    {
        using start_charge_func_t =
            bool (*)(
                RE::ActorMagicCaster*);

        using start_cast_func_t =
            void (*)(
                RE::ActorMagicCaster*);

        using finish_cast_func_t =
            void (*)(
                RE::ActorMagicCaster*);

        using interrupt_cast_func_t =
            void (*)(
                RE::ActorMagicCaster*,
                bool);
        using spell_cast_func_t =
            void (*)(
                RE::ActorMagicCaster*,
                bool,
                std::uint32_t,
                RE::MagicItem*);

        static inline
            start_charge_func_t
                startChargeFunc{};

        static inline
            start_cast_func_t
                startCastFunc{};

        static inline
            finish_cast_func_t
                finishCastFunc{};

        static inline
            interrupt_cast_func_t
                interruptCastFunc{};
        static inline
            spell_cast_func_t
                spellCastFunc{};


        void NotifyPlayerCaster(
            RE::ActorMagicCaster*
                a_caster) noexcept
        {
            if (!a_caster) {
                return;
            }

            auto* actor =
                a_caster->
                    GetCasterAsActor();

            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                actor != player) {

                return;
            }

            TULLBUNG::ClassRuntime::
                OnPlayerCastingStateChanged(
                    player);
        }


        bool StartChargeThunk(
            RE::ActorMagicCaster*
                a_caster)
        {
            const bool result =
                startChargeFunc(
                    a_caster);

            NotifyPlayerCaster(
                a_caster);

            return result;
        }


        void StartCastThunk(
            RE::ActorMagicCaster*
                a_caster)
        {
            startCastFunc(
                a_caster);

            NotifyPlayerCaster(
                a_caster);
        }


        void FinishCastThunk(
            RE::ActorMagicCaster*
                a_caster)
        {
            finishCastFunc(
                a_caster);

            NotifyPlayerCaster(
                a_caster);
        }


        void InterruptCastThunk(
            RE::ActorMagicCaster*
                a_caster,
            bool a_depleteEnergy)
        {
            interruptCastFunc(
                a_caster,
                a_depleteEnergy);

            NotifyPlayerCaster(
                a_caster);
        }


        void SpellCastThunk(
            RE::ActorMagicCaster*
                a_caster,
            bool a_doCast,
            std::uint32_t a_arg2,
            RE::MagicItem* a_spell)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            auto* spell =
                a_spell ?
                    a_spell->As<
                        RE::SpellItem>() :
                    nullptr;

            const bool observe =
                a_doCast &&
                a_caster &&
                spell &&
                player &&
                a_caster->
                    GetCasterAsActor() ==
                    player &&
                TULLBUNG::Classification::
                    IsProjectileDestructionSpell(
                        spell);

            ProjectileManagerHandleSnapshot
                before{};

            if (observe) {
                before =
                    CaptureProjectileManagerHandles();
            }

            spellCastFunc(
                a_caster,
                a_doCast,
                a_arg2,
                a_spell);

            if (observe &&
                before.valid) {

                HandlePlayerSpellCastProjectileBirth(
                    a_caster,
                    spell,
                    before);
            }
        }
        bool Install()
        {
            if (g_castingLifecycleHookInstalled) {
                return true;
            }

            REL::Relocation<
                std::uintptr_t>
                vtbl{
                    RE::ActorMagicCaster::
                        VTABLE[0]
                };

            startChargeFunc =
                reinterpret_cast<
                    start_charge_func_t>(
                        vtbl.write_vfunc(
                            0x04,
                            StartChargeThunk));

            startCastFunc =
                reinterpret_cast<
                    start_cast_func_t>(
                        vtbl.write_vfunc(
                            0x06,
                            StartCastThunk));

            finishCastFunc =
                reinterpret_cast<
                    finish_cast_func_t>(
                        vtbl.write_vfunc(
                            0x07,
                            FinishCastThunk));

            interruptCastFunc =
                reinterpret_cast<
                    interrupt_cast_func_t>(
                        vtbl.write_vfunc(
                            0x08,
                            InterruptCastThunk));
            spellCastFunc =
                reinterpret_cast<
                    spell_cast_func_t>(
                        vtbl.write_vfunc(
                            0x09,
                            SpellCastThunk));

            if (
                !startChargeFunc ||
                !startCastFunc ||
                !finishCastFunc ||
                !interruptCastFunc
            ) {
                logger::error(
                    "[Combat] S15 ActorMagicCaster lifecycle hooks = FAILED");

                return false;
            }

            if (!spellCastFunc) {
                logger::error(
                    "[Combat] S15 ActorMagicCaster SpellCast hook = FAILED");

                return false;
            }
            g_castingLifecycleHookInstalled =
                true;

            logger::info(
                "[Combat] S15 ActorMagicCaster lifecycle hooks = OK | vfunc=04/06/07/08/09");

            return true;
        }
    }


    namespace ProjectileImpactHook
    {
        using missile_add_impact_func_t =
            void (*)(
                RE::MissileProjectile*,
                RE::TESObjectREFR*,
                const RE::NiPoint3&,
                const RE::NiPoint3&,
                RE::hkpCollidable*,
                std::int32_t,
                std::uint32_t);


        using grenade_add_impact_func_t =
            void (*)(
                RE::GrenadeProjectile*,
                RE::TESObjectREFR*,
                const RE::NiPoint3&,
                const RE::NiPoint3&,
                RE::hkpCollidable*,
                std::int32_t,
                std::uint32_t);


        using beam_add_impact_func_t =
            void (*)(
                RE::BeamProjectile*,
                RE::TESObjectREFR*,
                const RE::NiPoint3&,
                const RE::NiPoint3&,
                RE::hkpCollidable*,
                std::int32_t,
                std::uint32_t);


        static inline
            missile_add_impact_func_t
                missileAddImpactFunc{};

        static inline
            grenade_add_impact_func_t
                grenadeAddImpactFunc{};

        static inline
            beam_add_impact_func_t
                beamAddImpactFunc{};

        static inline bool
            installed{};


        void MissileAddImpactThunk(
            RE::MissileProjectile* a_projectile,
            RE::TESObjectREFR* a_ref,
            const RE::NiPoint3& a_targetLoc,
            const RE::NiPoint3& a_velocity,
            RE::hkpCollidable* a_collidable,
            std::int32_t a_arg6,
            std::uint32_t a_arg7)
        {
            RecordProjectileActualImpact(
                a_projectile,
                a_ref,
                a_targetLoc,
                a_velocity,
                a_collidable,
                "MISSILE");

            missileAddImpactFunc(
                a_projectile,
                a_ref,
                a_targetLoc,
                a_velocity,
                a_collidable,
                a_arg6,
                a_arg7);
        }


        void GrenadeAddImpactThunk(
            RE::GrenadeProjectile* a_projectile,
            RE::TESObjectREFR* a_ref,
            const RE::NiPoint3& a_targetLoc,
            const RE::NiPoint3& a_velocity,
            RE::hkpCollidable* a_collidable,
            std::int32_t a_arg6,
            std::uint32_t a_arg7)
        {
            RecordProjectileActualImpact(
                a_projectile,
                a_ref,
                a_targetLoc,
                a_velocity,
                a_collidable,
                "GRENADE");

            grenadeAddImpactFunc(
                a_projectile,
                a_ref,
                a_targetLoc,
                a_velocity,
                a_collidable,
                a_arg6,
                a_arg7);
        }


        void BeamAddImpactThunk(
            RE::BeamProjectile* a_projectile,
            RE::TESObjectREFR* a_ref,
            const RE::NiPoint3& a_targetLoc,
            const RE::NiPoint3& a_velocity,
            RE::hkpCollidable* a_collidable,
            std::int32_t a_arg6,
            std::uint32_t a_arg7)
        {
            RecordProjectileActualImpact(
                a_projectile,
                a_ref,
                a_targetLoc,
                a_velocity,
                a_collidable,
                "BEAM");

            beamAddImpactFunc(
                a_projectile,
                a_ref,
                a_targetLoc,
                a_velocity,
                a_collidable,
                a_arg6,
                a_arg7);
        }


        bool Install()
        {
            if (installed) {
                return true;
            }

            {
                REL::Relocation<
                    std::uintptr_t>
                        vtbl{
                            RE::MissileProjectile::
                                VTABLE[0]
                        };

                missileAddImpactFunc =
                    reinterpret_cast<
                        missile_add_impact_func_t>(
                            vtbl.write_vfunc(
                                0xBD,
                                MissileAddImpactThunk));
            }


            {
                REL::Relocation<
                    std::uintptr_t>
                        vtbl{
                            RE::GrenadeProjectile::
                                VTABLE[0]
                        };

                grenadeAddImpactFunc =
                    reinterpret_cast<
                        grenade_add_impact_func_t>(
                            vtbl.write_vfunc(
                                0xBD,
                                GrenadeAddImpactThunk));
            }


            {
                REL::Relocation<
                    std::uintptr_t>
                        vtbl{
                            RE::BeamProjectile::
                                VTABLE[0]
                        };

                beamAddImpactFunc =
                    reinterpret_cast<
                        beam_add_impact_func_t>(
                            vtbl.write_vfunc(
                                0xBD,
                                BeamAddImpactThunk));
            }


            if (!missileAddImpactFunc ||
                !grenadeAddImpactFunc ||
                !beamAddImpactFunc) {

                logger::error(
                    "[Combat] S15 projectile AddImpact hooks = FAILED");

                return false;
            }

            installed = true;

            logger::info(
                "[Combat] S15 projectile AddImpact hooks = OK | Missile/Grenade/Beam vfunc=0xBD");

            return true;
        }
    }
    namespace PlayerActorValueHook
    {
        using func_t =
            void (*)(
                RE::ActorValueOwner*,
                RE::ACTOR_VALUE_MODIFIER,
                RE::ActorValue,
                float);

        static inline func_t
            func{};


        void thunk(
            RE::ActorValueOwner*
                a_owner,
            RE::ACTOR_VALUE_MODIFIER
                a_modifier,
            RE::ActorValue
                a_actorValue,
            float a_value)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            auto* playerOwner =
                player ?
                    player->
                        AsActorValueOwner() :
                    nullptr;

            const bool trackMagicka =
                player &&
                playerOwner &&
                a_owner == playerOwner &&
                a_actorValue ==
                    RE::ActorValue::
                        kMagicka;

            float beforeCurrent{};
            float beforeMaximum{};

            if (trackMagicka) {
                beforeCurrent =
                    playerOwner->
                        GetActorValue(
                            RE::ActorValue::
                                kMagicka);

                beforeMaximum =
                    player->
                        GetActorValueMax(
                            RE::ActorValue::
                                kMagicka);
            }

            func(
                a_owner,
                a_modifier,
                a_actorValue,
                a_value);

            if (!trackMagicka) {
                return;
            }

            const float afterCurrent =
                playerOwner->
                    GetActorValue(
                        RE::ActorValue::
                            kMagicka);

            const float afterMaximum =
                player->
                    GetActorValueMax(
                        RE::ActorValue::
                            kMagicka);

            TULLBUNG::ClassRuntime::
                OnPlayerMagickaMutation(
                    beforeCurrent,
                    beforeMaximum,
                    afterCurrent,
                    afterMaximum);
        }


        bool Install()
        {
            if (g_playerActorValueHookInstalled) {
                return true;
            }

            // Local CommonLib authority:
            //
            // Actor inherits:
            // TESObjectREFR
            // -> MagicTarget
            // -> ActorValueOwner
            //
            // TESObjectREFR owns 4 vtables,
            // MagicTarget owns the next one,
            // therefore ActorValueOwner is the sixth
            // PlayerCharacter vtable entry: index 5.
            //
            // ActorValueOwner::ModActorValue = vfunc 0x06.
            static_assert(
                RE::PlayerCharacter::
                    VTABLE.size() >
                5);

            REL::Relocation<
                std::uintptr_t>
                vtbl{
                    RE::PlayerCharacter::
                        VTABLE[5]
                };

            const auto original =
                vtbl.write_vfunc(
                    0x06,
                    thunk);

            func =
                reinterpret_cast<
                    func_t>(
                        original);

            if (!func) {
                logger::error(
                    "[Combat] S15 Player ActorValueOwner hook original = NULL");

                return false;
            }

            g_playerActorValueHookInstalled =
                true;

            logger::info(
                "[Combat] S15 Player ActorValueOwner hook = OK | PlayerCharacter::VTABLE[5] | ModActorValue=0x06");

            return true;
        }
    }

    namespace WeaponHitHook
    {
        static inline constexpr
            REL::RelocationID
            kTargetID{
                37633,
                38586
            };

        static inline constexpr
            REL::VariantOffset
            kTargetOffset{
                0x16A,
                0xFA,
                0x0
            };

        namespace SE
        {
            using func_t =
                void (*)(
                    RE::ScriptEventSourceHolder*,
                    RE::NiPointer<
                        RE::TESObjectREFR>&,
                    RE::NiPointer<
                        RE::TESObjectREFR>&,
                    RE::FormID,
                    RE::FormID,
                    RE::HitData&);

            static inline func_t func{
                nullptr
            };

            void thunk(
                RE::ScriptEventSourceHolder*
                    a_holder,
                RE::NiPointer<
                    RE::TESObjectREFR>&
                    a_target,
                RE::NiPointer<
                    RE::TESObjectREFR>&
                    a_aggressor,
                RE::FormID a_source,
                RE::FormID a_projectile,
                RE::HitData& a_data)
            {
                auto* target =
                    a_target.get();

                auto* aggressorRef =
                    a_aggressor.get();

                auto* aggressor =
                    aggressorRef ?
                        aggressorRef->
                            As<RE::Actor>() :
                        nullptr;

                auto* player =
                    RE::PlayerCharacter::
                        GetSingleton();

                const bool incoming =
                    player &&
                    target == player &&
                    aggressor &&
                    aggressor != player;

                Snapshot::ActorResources
                    playerBefore{};

                Snapshot::CastingState
                    castingBefore{};

                if (incoming) {
                    playerBefore =
                        Snapshot::
                            CaptureResources(
                                player);

                    castingBefore =
                        Snapshot::
                            CaptureCastingState(
                                player);

                    TULLBUNG::ClassRuntime::
                        ApplyPlayerIncomingDamageRules(
                            aggressor,
                            a_data,
                            playerBefore);


                } else {
                    ProcessPlayerHitData(
                        target,
                        aggressor,
                        a_data.weapon,
                        a_data);
                }


                g_pendingBarrierWeaponReflect = {};

                const bool barrierWeaponDirect =
                    incoming &&
                    a_data.weapon &&
                    !a_data.weapon->
                        IsStaff() &&
                    IsMageBarrierReflectEligible(
                        castingBefore);

                if (barrierWeaponDirect) {
                    g_pendingBarrierWeaponReflect.valid =
                        true;
                    g_pendingBarrierWeaponReflect.attackerFormID =
                        aggressor ?
                            aggressor->GetFormID() :
                            0;
                }

                func(
                    a_holder,
                    a_target,
                    a_aggressor,
                    a_source,
                    a_projectile,
                    a_data);


                if (incoming) {
                    const auto after =
                        Snapshot::
                            CaptureResources(
                                player);

                    ProcessPlayerIncomingHitData(
                        target,
                        aggressor,
                        a_data,
                        playerBefore,
                        castingBefore,
                        after.health.current);
                } else {
                    ProcessPlayerHitDataPost(
                        target,
                        aggressor,
                        a_data);
                }
            }
        }

        namespace AE
        {
            using func_t =
                void (*)(
                    RE::AIProcess*,
                    RE::HitData&);

            static inline func_t func{
                nullptr
            };

            void thunk(
                RE::AIProcess*
                    a_targetProcess,
                RE::HitData&
                    a_data)
            {
                auto* target =
                    a_targetProcess ?
                        a_targetProcess->
                            GetUserData() :
                        nullptr;

                auto aggressorPtr =
                    a_data.aggressor.get();

                auto* aggressor =
                    aggressorPtr.get();

                auto* player =
                    RE::PlayerCharacter::
                        GetSingleton();

                const bool incoming =
                    player &&
                    target == player &&
                    aggressor &&
                    aggressor != player;

                Snapshot::ActorResources
                    playerBefore{};

                Snapshot::CastingState
                    castingBefore{};

                if (incoming) {
                    playerBefore =
                        Snapshot::
                            CaptureResources(
                                player);

                    castingBefore =
                        Snapshot::
                            CaptureCastingState(
                                player);

                    TULLBUNG::ClassRuntime::
                        ApplyPlayerIncomingDamageRules(
                            aggressor,
                            a_data,
                            playerBefore);
                } else {
                    ProcessPlayerHitData(
                        target,
                        aggressor,
                        a_data.weapon,
                        a_data);
                }

                g_pendingBarrierWeaponReflect = {};

                const bool barrierWeaponDirect =
                    incoming &&
                    a_data.weapon &&
                    !a_data.weapon->
                        IsStaff() &&
                    IsMageBarrierReflectEligible(
                        castingBefore);

                if (barrierWeaponDirect) {
                    g_pendingBarrierWeaponReflect.valid =
                        true;
                    g_pendingBarrierWeaponReflect.attackerFormID =
                        aggressor ?
                            aggressor->GetFormID() :
                            0;
                }

                func(
                    a_targetProcess,
                    a_data);


                if (incoming) {
                    const auto after =
                        Snapshot::
                            CaptureResources(
                                player);

                    ProcessPlayerIncomingHitData(
                        target,
                        aggressor,
                        a_data,
                        playerBefore,
                        castingBefore,
                        after.health.current);
                } else {
                    ProcessPlayerHitDataPost(
                        target,
                        aggressor,
                        a_data);
                }
            }
        }

        bool Install()
        {
            if (g_hitHookInstalled) {
                return true;
            }

            SKSE::AllocTrampoline(
                14);

            REL::Relocation<
                std::uintptr_t>
                target{
                    kTargetID,
                    kTargetOffset
                };

            auto& trampoline =
                SKSE::GetTrampoline();

            if (REL::Module::IsAE()) {
                const auto original =
                    trampoline.
                        write_call<5>(
                            target.address(),
                            AE::thunk);

                AE::func =
                    reinterpret_cast<
                        AE::func_t>(
                            original);

                logger::info(
                    "[Combat] HitData weapon hook = OK | Runtime=AE");
            } else {
                const auto original =
                    trampoline.
                        write_call<5>(
                            target.address(),
                            SE::thunk);

                SE::func =
                    reinterpret_cast<
                        SE::func_t>(
                            original);

                logger::info(
                    "[Combat] HitData weapon hook = OK | Runtime=SE");
            }

            g_hitHookInstalled =
                true;

            return true;
        }
    }

    [[nodiscard]]
    RE::Actor* ResolveActiveEffectTargetActor(
        RE::ActiveEffect* a_effect) noexcept
    {
        if (!a_effect ||
            !a_effect->target) {

            return nullptr;
        }

        auto* targetRef =
            a_effect->target->
                GetTargetStatsObject();

        return targetRef ?
            targetRef->As<RE::Actor>() :
            nullptr;
    }

    namespace SpellDamageHook
    {
        using modify_start_func_t =
            void (*)(
                RE::ValueModifierEffect*);

        using value_modify_actor_value_func_t =
            void (*)(
                RE::ValueModifierEffect*,
                RE::Actor*,
                float,
                RE::ActorValue);

        using dual_modify_actor_value_func_t =
            void (*)(
                RE::DualValueModifierEffect*,
                RE::Actor*,
                float,
                RE::ActorValue);

        using value_conditions_modify_actor_value_func_t =
            void (*)(
                RE::ValueAndConditionsEffect*,
                RE::Actor*,
                float,
                RE::ActorValue);

        using peak_modify_actor_value_func_t =
            void (*)(
                RE::PeakValueModifierEffect*,
                RE::Actor*,
                float,
                RE::ActorValue);

        static inline modify_start_func_t
            modifyOnStartFunc{};

        static inline value_modify_actor_value_func_t
            valueModifyActorValueFunc{};

        static inline dual_modify_actor_value_func_t
            dualModifyActorValueFunc{};

        static inline value_conditions_modify_actor_value_func_t
            valueConditionsModifyActorValueFunc{};

        static inline peak_modify_actor_value_func_t
            peakModifyActorValueFunc{};

        struct SpellDirectValueResolution final
        {
            bool authorized{};

            std::uint64_t actionID{};

            RE::FormID spellID{};
            RE::FormID casterID{};
            RE::FormID targetID{};

            float outgoingAdditive{};
            float incomingMultiplier{ 1.0f };
            float finalMultiplier{ 1.0f };
        };

        [[nodiscard]] float
            ResolveProductionSpellDirectBonus(
                const RE::SpellItem* a_spell,
                RE::Actor* a_target,
                Hand a_hand,
                const Snapshot::ActionSnapshot&
                    a_snapshot) noexcept
        {
            return TULLBUNG::ClassRuntime::
                ResolvePlayerSpellDirectBonusPercent(
                    a_spell,
                    a_target,
                    a_hand,
                    a_snapshot);
        }

        [[nodiscard]] float
            ResolveProductionHealingBonus(
                const RE::SpellItem* a_spell,
                Hand a_hand,
                const Snapshot::ActionSnapshot&
                    a_snapshot) noexcept
        {
            return TULLBUNG::ClassRuntime::
                ResolvePlayerHealingBonusPercent(
                    a_spell,
                    a_hand,
                    a_snapshot);
        }

        [[nodiscard]]
        SpellDirectValueResolution
            ResolveDirectValue(
                RE::ValueModifierEffect* a_effect,
                RE::Actor* a_actor) noexcept
        {
            SpellDirectValueResolution result{};

            if (!a_effect ||
                !a_actor ||
                !a_effect->spell ||
                !a_effect->effect ||
                !TULLBUNG::Classification::
                    IsDirectHealthDamageEffect(
                        a_effect->effect)) {

                return result;
            }

            auto* spell =
                a_effect->spell->
                    As<RE::SpellItem>();

            if (!spell) {
                return result;
            }

            auto casterPtr =
                a_effect->GetCasterActor();

            auto* caster =
                casterPtr.get();

            auto* effectTarget =
                ResolveActiveEffectTargetActor(
                    a_effect);

            if (!caster ||
                !effectTarget ||
                effectTarget != a_actor) {

                return result;
            }

            const auto authorityProbe =
                ResolveSpellImpactAuthority(
                    spell,
                    caster,
                    effectTarget);

            if (!authorityProbe.matched) {
                return result;
            }

            result.actionID =
                authorityProbe.actionID;
            result.spellID =
                spell->GetFormID();
            result.casterID =
                caster->GetFormID();
            result.targetID =
                effectTarget->GetFormID();

            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player) {
                return result;
            }

            if (caster == player) {
                if (!authorityProbe.actionMatched ||
                    !authorityProbe.
                        actionSnapshot.valid) {

                    return result;
                }

                result.outgoingAdditive =
                    ResolveProductionSpellDirectBonus(
                        spell,
                        effectTarget,
                        authorityProbe.hand,
                        authorityProbe.
                            actionSnapshot);
            } else if (effectTarget == player) {
                result.incomingMultiplier =
                    TULLBUNG::ClassRuntime::
                        ResolvePlayerIncomingElementalDamageMultiplier(
                            caster,
                            effectTarget,
                            a_effect->effect ?
                                a_effect->effect->
                                    baseEffect :
                                nullptr);
            } else {
                return result;
            }

            if (!std::isfinite(
                    result.outgoingAdditive) ||
                !std::isfinite(
                    result.incomingMultiplier)) {

                return result;
            }

            const float outgoingMultiplier =
                (std::max)(
                    0.0f,
                    1.0f +
                        result.outgoingAdditive /
                            100.0f);

            result.incomingMultiplier =
                (std::max)(
                    0.0f,
                    result.incomingMultiplier);

            result.finalMultiplier =
                outgoingMultiplier *
                result.incomingMultiplier;

            if (!std::isfinite(
                    result.finalMultiplier)) {

                result.finalMultiplier =
                    1.0f;

                return result;
            }

            result.authorized = true;
            return result;
        }

        [[nodiscard]] float
            ResolveProductionDirectValue(
                RE::ValueModifierEffect* a_effect,
                RE::Actor* a_actor,
                float a_value,
                const char* a_path,
                SpellDirectValueResolution*
                    a_outResolution = nullptr) noexcept
        {
            const auto resolution =
                ResolveDirectValue(
                    a_effect,
                    a_actor);

            if (a_outResolution) {
                *a_outResolution =
                    resolution;
            }

            if (!resolution.authorized ||
                !std::isfinite(a_value) ||
                a_value >= 0.0f) {

                return a_value;
            }

            const float sent =
                a_value *
                resolution.finalMultiplier;

            if (!std::isfinite(sent)) {
                return a_value;
            }

            if (std::abs(
                    sent - a_value) >
                0.000001f) {

                logger::debug(
                    "[S14SPELL-PROD] Path={} | ActionID={} | Spell={:08X} | Caster={:08X} | Target={:08X} | OutAdd={:.3f} | InMult={:.3f} | FinalMult={:.3f} | ValueBefore={:.4f} | ValueSent={:.4f}",
                    a_path ?
                        a_path :
                        "<null>",
                    resolution.actionID,
                    resolution.spellID,
                    resolution.casterID,
                    resolution.targetID,
                    resolution.outgoingAdditive,
                    resolution.incomingMultiplier,
                    resolution.finalMultiplier,
                    a_value,
                    sent);
            }

            if (LogS19QaValueFirst(
                    S19QaValueKind::kSpellDirect,
                    resolution.actionID,
                    resolution.spellID,
                    resolution.targetID)) {
                logger::info(
                    "[S19QA-DIRECT-ARG] Sample=BOUNDED_BEST_EFFORT | Stage=ENGINE_ARGUMENT | Path={} | ActionID={} | Spell={:08X} | Caster={:08X} | Target={:08X} | OutAdd={:.3f} | InMult={:.3f} | FinalMult={:.3f} | ValueBefore={:.4f} | ValueSent={:.4f}",
                    a_path ? a_path : "<null>",
                    resolution.actionID,
                    resolution.spellID,
                    resolution.casterID,
                    resolution.targetID,
                    resolution.outgoingAdditive,
                    resolution.incomingMultiplier,
                    resolution.finalMultiplier,
                    a_value,
                    sent);
            }

            return sent;
        }

        [[nodiscard]] float
            ResolveProductionHealingValue(
                RE::ValueModifierEffect* a_effect,
                RE::Actor* a_actor,
                float a_value,
                const char* a_path) noexcept
        {
            if (!a_effect ||
                !a_actor ||
                !a_effect->spell ||
                !a_effect->effect ||
                !std::isfinite(a_value) ||
                a_value <= 0.0f ||
                !TULLBUNG::Classification::
                    IsHealthRestorationEffect(
                        a_effect->effect)) {

                return a_value;
            }

            auto* spell =
                a_effect->spell->
                    As<RE::SpellItem>();

            if (!spell) {
                return a_value;
            }

            auto casterPtr =
                a_effect->GetCasterActor();

            auto* caster =
                casterPtr.get();

            auto* effectTarget =
                ResolveActiveEffectTargetActor(
                    a_effect);

            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                !caster ||
                caster != player ||
                !effectTarget ||
                effectTarget != a_actor) {

                return a_value;
            }

            const bool actionMatched =
                g_eventAction.actionID != 0 &&
                g_eventAction.actor ==
                    player &&
                g_eventAction.sourceKind ==
                    SourceKind::kSpell &&
                g_eventAction.sourceForm ==
                    spell &&
                g_eventAction.
                    actionSnapshot.valid;

            if (!actionMatched) {
                return a_value;
            }

            const float healingPercent =
                ResolveProductionHealingBonus(
                    spell,
                    g_eventAction.hand,
                    g_eventAction.
                        actionSnapshot);

            if (!std::isfinite(healingPercent)) {
                return a_value;
            }

            if (std::abs(healingPercent) <= 0.000001f) {
                return a_value;
            }

            const float multiplier =
                (std::max)(
                    0.0f,
                    1.0f +
                        healingPercent /
                            100.0f);

            const float sent =
                a_value *
                multiplier;

            if (!std::isfinite(sent)) {
                return a_value;
            }

            logger::debug(
                "[S14HEALING-PROD] Path={} | ActionID={} | Spell={:08X} | Target={:08X} | Hand={} | BonusPercent={:.2f} | ValueBefore={:.4f} | ValueSent={:.4f}",
                a_path ?
                    a_path :
                    "<null>",
                g_eventAction.actionID,
                spell->GetFormID(),
                effectTarget->GetFormID(),
                static_cast<std::uint32_t>(
                    g_eventAction.hand),
                healingPercent,
                a_value,
                sent);

            if (LogS19QaValueFirst(
                    S19QaValueKind::kHealing,
                    g_eventAction.actionID,
                    spell->GetFormID(),
                    effectTarget->GetFormID())) {
                logger::info(
                    "[S19QA-HEAL-ARG] Sample=BOUNDED_BEST_EFFORT | Stage=ENGINE_ARGUMENT | Path={} | ActionID={} | Spell={:08X} | Target={:08X} | Hand={} | BonusPercent={:.2f} | ValueBefore={:.4f} | ValueSent={:.4f}",
                    a_path ? a_path : "<null>",
                    g_eventAction.actionID,
                    spell->GetFormID(),
                    effectTarget->GetFormID(),
                    static_cast<std::uint32_t>(g_eventAction.hand),
                    healingPercent,
                    a_value,
                    sent);
            }

            return sent;
        }

        [[nodiscard]] float
            ResolveProductionActorValue(
                RE::ValueModifierEffect* a_effect,
                RE::Actor* a_actor,
                float a_value,
                const char* a_path,
                SpellDirectValueResolution*
                    a_outResolution =
                        nullptr) noexcept
        {
            if (a_outResolution) {
                *a_outResolution = {};
            }

            const float healingValue =
                ResolveProductionHealingValue(
                    a_effect,
                    a_actor,
                    a_value,
                    a_path);

            if (healingValue != a_value) {
                return healingValue;
            }

            return
                ResolveProductionDirectValue(
                    a_effect,
                    a_actor,
                    a_value,
                    a_path,
                    a_outResolution);
        }


        struct IncomingSpellDiagnostic final
        {
            std::uint64_t eventID{};
            RE::FormID spellID{};
            RE::FormID mgefID{};
        };

        IncomingSpellDiagnostic TraceIncomingSpellThunk(
            const char* a_thunk, RE::ValueModifierEffect* a_effect,
            RE::Actor* a_actor, float a_input, RE::ActorValue a_av) noexcept
        {
            IncomingSpellDiagnostic diagnostic{};
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* effectTarget = ResolveActiveEffectTargetActor(a_effect);
            auto* base = a_effect ? a_effect->GetBaseObject() : nullptr;
            // Observe potentially damaging Health events, including malformed inputs and
            // actor/AV mismatches. This filter affects diagnostics only, never authority.
            const bool incoming = !player || a_actor == player || effectTarget == player;
            const bool health = a_av == RE::ActorValue::kHealth ||
                (base && base->data.primaryAV == RE::ActorValue::kHealth);
            const bool potentialDamage = !std::isfinite(a_input) || a_input <= 0.0f ||
                (base && (base->IsHostile() || base->IsDetrimental()));
            if (!incoming || !health || !potentialDamage) {
                return diagnostic;
            }
            diagnostic.eventID = g_nextBarrierDiagnosticID.fetch_add(1, std::memory_order_relaxed);
            diagnostic.spellID = a_effect && a_effect->spell ? a_effect->spell->GetFormID() : 0;
            diagnostic.mgefID = base ? base->GetFormID() : 0;
            g_lastBarrierSpellDiagnosticEventID = diagnostic.eventID;
            const auto caster = a_effect ? a_effect->GetCasterActor() : RE::NiPointer<RE::Actor>{};
            logger::trace(
                "[S15BARRIER-THUNK] ThreadTag={} | Event={} | Thunk={} | Actor={:08X} | EffectTarget={:08X} | Caster={:08X} | Spell={:08X} | MGEF={:08X} | AV={} | PrimaryAV={} | Input={:.6f} | Depth={}",
                reinterpret_cast<std::uintptr_t>(&g_incomingDirectDepth), diagnostic.eventID,
                a_thunk, a_actor ? a_actor->GetFormID() : 0,
                effectTarget ? effectTarget->GetFormID() : 0, caster ? caster->GetFormID() : 0,
                diagnostic.spellID, diagnostic.mgefID, static_cast<std::int32_t>(a_av),
                base ? static_cast<std::int32_t>(base->data.primaryAV) : -1,
                a_input, g_incomingDirectDepth);
            return diagnostic;
        }

        [[nodiscard]]
        IncomingDirectScopeToken
            BeginIncomingSpellHealthScope(
                RE::ValueModifierEffect*
                    a_effect,
                RE::Actor* a_actor,
                float a_sentValue,
                RE::ActorValue
                    a_actorValue,
                const SpellDirectValueResolution&
                    a_resolution,
                const IncomingSpellDiagnostic& a_diagnostic) noexcept
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            IncomingDirectScopeToken rejectedToken{};
            rejectedToken.diagnosticEventID = a_diagnostic.eventID;
            const auto traceGate = [&](const char* a_reason, bool a_evaluated,
                bool a_fallback, bool a_directAuthorized, bool a_reflectEligible,
                const Snapshot::CastingState* a_observedCasting = nullptr) {
                if (a_diagnostic.eventID == 0) {
                    return;
                }
                auto* diagnosticSpell = a_effect && a_effect->spell ?
                    a_effect->spell->As<RE::SpellItem>() : nullptr;
                const auto diagnosticCasting = a_observedCasting ? *a_observedCasting :
                    Snapshot::CaptureCastingState(player);
                const bool attackSpell = diagnosticSpell &&
                    TULLBUNG::Classification::IsAttackSpell(diagnosticSpell);
                const bool directEffect = a_effect && a_effect->effect &&
                    TULLBUNG::Classification::IsDirectHealthDamageEffect(a_effect->effect);
                const bool firewall = TULLBUNG::DamageRules::IsTriggerSuppressed();
                const bool barrierNode = TULLBUNG::BuildState::IsNodeActive(
                    TULLBUNG::BuildState::NodeID::kMageBarrier);
                logger::trace(
                    "[S15BARRIER-GATE] Event={} | NormalAuthorized={} | FallbackEvaluated={} | ActiveEffectFallback={} | DirectAuthorized={} | IsAttackSpell={} | IsDirectHealthDamageEffect={} | CastingKnown={} | AnyCasting={} | Charging={} | Casting={} | Concentrating={} | BarrierNode={} | Firewall={} | BarrierCastingEligible={} | ReflectEligible={} | Sent={:.6f} | Depth={} | Reason={}",
                    a_diagnostic.eventID, a_resolution.authorized, a_evaluated, a_fallback,
                    a_directAuthorized, attackSpell, directEffect, diagnosticCasting.known,
                    diagnosticCasting.anySpellCasting, diagnosticCasting.charging,
                    diagnosticCasting.casting, diagnosticCasting.concentrating, barrierNode,
                    firewall, diagnosticCasting.known && diagnosticCasting.anySpellCasting,
                    a_reflectEligible, a_sentValue, g_incomingDirectDepth, a_reason);
            };

            const auto* baseEffect =
                a_effect ?
                    a_effect->GetBaseObject() :
                    nullptr;

            // Value/Dual/VAC/Peak can report ActorValue::kNone while the
            // MGEF primary AV is Health. Use primaryAV only for that exact
            // kNone case; a real non-Health hook AV remains authoritative.
            const auto effectiveActorValue =
                a_actorValue == RE::ActorValue::kNone && baseEffect ?
                    baseEffect->data.primaryAV :
                    a_actorValue;

            if (
                !player ||
                a_actor != player ||
                effectiveActorValue !=
                    RE::ActorValue::
                        kHealth ||
                !std::isfinite(
                    a_sentValue) ||
                a_sentValue >= 0.0f
            ) {
                traceGate(!player ? "NO_PLAYER" : a_actor != player ? "ACTOR_NOT_PLAYER" :
                    effectiveActorValue != RE::ActorValue::kHealth ? "AV_NOT_HEALTH" :
                    !std::isfinite(a_sentValue) ? "NONFINITE_SENT" : "NONNEGATIVE_SENT",
                    false, false, false, false);
                return rejectedToken;
            }

            auto casterPtr =
                a_effect ?
                    a_effect->
                        GetCasterActor() :
                    RE::NiPointer<
                        RE::Actor>{};

            auto* caster =
                casterPtr.get();

            const auto casting =
                Snapshot::
                    CaptureCastingState(
                        player);

            auto* spell =
                a_effect &&
                        a_effect->spell ?
                    a_effect->spell->
                        As<RE::SpellItem>() :
                    nullptr;

            const bool
                activeEffectDirectFallback =
                    !a_resolution.authorized &&
                    spell &&
                    a_effect &&
                    a_effect->effect &&
                    TULLBUNG::Classification::
                        IsAttackSpell(
                            spell) &&
                    TULLBUNG::Classification::
                        IsDirectHealthDamageEffect(
                            a_effect->effect);

            const bool directAuthorized =
                a_resolution.authorized ||
                activeEffectDirectFallback;

            const bool reflectEligible =
                directAuthorized &&
                caster &&
                caster != player &&
                IsMageBarrierReflectEligible(
                    casting);

            const char* diagnosticReason = reflectEligible ? "ELIGIBLE" :
                !directAuthorized ? "NOT_DIRECT_AUTHORIZED" : !caster ? "NO_CASTER" :
                caster == player ? "SELF_CASTER" :
                TULLBUNG::DamageRules::IsTriggerSuppressed() ? "FIREWALL" :
                !casting.known ? "CASTING_UNKNOWN" : !casting.anySpellCasting ? "NOT_CASTING" :
                "BARRIER_NODE_OFF";
            traceGate(diagnosticReason, true, activeEffectDirectFallback,
                directAuthorized, reflectEligible, &casting);

            if (reflectEligible &&
                activeEffectDirectFallback) {

                logger::info(
                    "[S15BARRIER-SPELL-AUTH] Route=ACTIVE_EFFECT_FALLBACK | Spell={:08X} | Caster={:08X} | Sent={:.4f}",
                    spell->GetFormID(),
                    caster->GetFormID(),
                    a_sentValue);
            }

            // If a Weapon Direct transaction is already active,
            // even an unauthorized/non-direct spell Health
            // mutation receives a discard frame. This prevents
            // poison/DoT/auxiliary magic from being folded into
            // the weapon's actual-direct total.
            if (!reflectEligible &&
                g_incomingDirectDepth == 0) {

                return rejectedToken;
            }

            return
                BeginIncomingDirectDamageScope(
                    caster,
                    reflectEligible,
                    a_resolution.authorized ?
                        a_resolution.actionID :
                        0,
                    reflectEligible ?
                        IncomingDirectKind::
                            kSpell :
                        IncomingDirectKind::
                            kDiscard,
                    a_diagnostic.eventID,
                    a_diagnostic.spellID,
                    a_diagnostic.mgefID);
        }

        void ModifyOnStartThunk(
            RE::ValueModifierEffect* a_effect)
        {
            if (!modifyOnStartFunc) {
                return;
            }

            modifyOnStartFunc(
                a_effect);
        }

        void ValueModifyActorValueThunk(
            RE::ValueModifierEffect* a_effect,
            RE::Actor* a_actor,
            float a_value,
            RE::ActorValue a_actorValue)
        {
            const auto diagnostic = TraceIncomingSpellThunk(
                "Value", a_effect, a_actor, a_value, a_actorValue);

            SpellDirectValueResolution
                resolution{};

            const float sent =
                ResolveProductionActorValue(
                    a_effect,
                    a_actor,
                    a_value,
                    "Value",
                    &resolution);

            const auto barrierTransaction =
                BeginIncomingSpellHealthScope(
                    a_effect,
                    a_actor,
                    sent,
                    a_actorValue,
                    resolution,
                    diagnostic);

            valueModifyActorValueFunc(
                a_effect,
                a_actor,
                sent,
                a_actorValue);

            EndIncomingDirectDamageScope(
                barrierTransaction);
        }


        void ValueConditionsModifyActorValueThunk(
            RE::ValueAndConditionsEffect* a_effect,
            RE::Actor* a_actor,
            float a_value,
            RE::ActorValue a_actorValue)
        {
            const auto diagnostic = TraceIncomingSpellThunk(
                "ValueConditions", a_effect, a_actor, a_value, a_actorValue);

            SpellDirectValueResolution
                resolution{};

            const float sent =
                ResolveProductionActorValue(
                    a_effect,
                    a_actor,
                    a_value,
                    "ValueConditions",
                    &resolution);

            const auto barrierTransaction =
                BeginIncomingSpellHealthScope(
                    a_effect,
                    a_actor,
                    sent,
                    a_actorValue,
                    resolution,
                    diagnostic);

            valueConditionsModifyActorValueFunc(
                a_effect,
                a_actor,
                sent,
                a_actorValue);

            EndIncomingDirectDamageScope(
                barrierTransaction);
        }


        void PeakModifyActorValueThunk(
            RE::PeakValueModifierEffect* a_effect,
            RE::Actor* a_actor,
            float a_value,
            RE::ActorValue a_actorValue)
        {
            const auto diagnostic = TraceIncomingSpellThunk(
                "Peak", a_effect, a_actor, a_value, a_actorValue);

            SpellDirectValueResolution
                resolution{};

            const float sent =
                ResolveProductionActorValue(
                    a_effect,
                    a_actor,
                    a_value,
                    "Peak",
                    &resolution);

            const auto barrierTransaction =
                BeginIncomingSpellHealthScope(
                    a_effect,
                    a_actor,
                    sent,
                    a_actorValue,
                    resolution,
                    diagnostic);

            peakModifyActorValueFunc(
                a_effect,
                a_actor,
                sent,
                a_actorValue);

            const auto* baseEffect =
                a_effect ?
                    a_effect->GetBaseObject() :
                    nullptr;

            auto* siegeSlowEffect =
                TULLBUNG::DataCache::
                    GetMageFamilyEffect(
                        TULLBUNG::DataCache::
                            MageFamilyEffectID::
                                kSiegeRuneSlow);

            const auto effectiveActorValue =
                (a_actorValue ==
                     RE::ActorValue::kNone &&
                 baseEffect) ?
                    baseEffect->data.primaryAV :
                    a_actorValue;

            if (
                a_actor &&
                baseEffect &&
                siegeSlowEffect &&
                baseEffect == siegeSlowEffect &&
                effectiveActorValue ==
                    RE::ActorValue::kSpeedMult
            ) {
                if (auto* owner =
                        a_actor->AsActorValueOwner()) {

                    constexpr float
                        kSpeedRefreshNudge = 0.01f;

                    owner->ModActorValue(
                        RE::ACTOR_VALUE_MODIFIERS::
                            kTemporary,
                        RE::ActorValue::
                            kInventoryWeight,
                        kSpeedRefreshNudge);

                    owner->ModActorValue(
                        RE::ACTOR_VALUE_MODIFIERS::
                            kTemporary,
                        RE::ActorValue::
                            kInventoryWeight,
                        -kSpeedRefreshNudge);
                }
            }

            EndIncomingDirectDamageScope(
                barrierTransaction);
        }


        void DualModifyActorValueThunk(
            RE::DualValueModifierEffect* a_effect,
            RE::Actor* a_actor,
            float a_value,
            RE::ActorValue a_actorValue)
        {
            const auto diagnostic = TraceIncomingSpellThunk(
                "Dual", a_effect, a_actor, a_value, a_actorValue);

            SpellDirectValueResolution
                resolution{};

            const float sent =
                ResolveProductionDirectValue(
                    a_effect,
                    a_actor,
                    a_value,
                    "Dual",
                    &resolution);

            const auto barrierTransaction =
                BeginIncomingSpellHealthScope(
                    a_effect,
                    a_actor,
                    sent,
                    a_actorValue,
                    resolution,
                    diagnostic);

            const bool changed =
                resolution.authorized &&
                std::isfinite(a_value) &&
                std::isfinite(sent) &&
                a_value < 0.0f &&
                std::abs(
                    sent - a_value) >
                    0.000001f;

            RE::ActorValue secondaryAV =
                RE::ActorValue::kNone;

            float secondaryWeight = 0.0f;

            if (changed) {
                secondaryAV =
                    a_effect->
                        GetAdditionalActorValue();

                secondaryWeight =
                    a_effect->
                        GetSecondaryAVWeight();
            }

            dualModifyActorValueFunc(
                a_effect,
                a_actor,
                sent,
                a_actorValue);

            EndIncomingDirectDamageScope(
                barrierTransaction);

            if (!changed ||
                !a_actor ||
                !std::isfinite(
                    secondaryWeight) ||
                (secondaryAV !=
                     RE::ActorValue::kMagicka &&
                 secondaryAV !=
                     RE::ActorValue::kStamina)) {

                return;
            }

            const float correction =
                (a_value - sent) *
                secondaryWeight;

            if (!std::isfinite(correction) ||
                std::abs(correction) <=
                    0.000001f) {

                return;
            }

            auto* owner =
                a_actor->
                    AsActorValueOwner();

            if (!owner) {
                return;
            }

            owner->ModActorValue(
                RE::ACTOR_VALUE_MODIFIERS::
                    kDamage,
                secondaryAV,
                correction);

            logger::debug(
                "[S14SPELL-DUAL-SECONDARY] ActionID={} | Spell={:08X} | Target={:08X} | SecondaryAV={} | Weight={:.4f} | Original={:.4f} | Sent={:.4f} | Correction={:.4f}",
                resolution.actionID,
                resolution.spellID,
                resolution.targetID,
                static_cast<std::uint32_t>(
                    secondaryAV),
                secondaryWeight,
                a_value,
                sent,
                correction);
        }

        bool Install()
        {
            {
                REL::Relocation<
                    std::uintptr_t>
                    vtbl{
                        RE::VTABLE_ValueModifierEffect[
                            0]
                    };

                const auto original =
                    vtbl.write_vfunc(
                        0x1B,
                        ModifyOnStartThunk);

                modifyOnStartFunc =
                    reinterpret_cast<
                        modify_start_func_t>(
                            original);
            }

            {
                REL::Relocation<
                    std::uintptr_t>
                    vtbl{
                        RE::VTABLE_ValueModifierEffect[
                            0]
                    };

                const auto original =
                    vtbl.write_vfunc(
                        0x20,
                        ValueModifyActorValueThunk);

                valueModifyActorValueFunc =
                    reinterpret_cast<
                        value_modify_actor_value_func_t>(
                            original);
            }

            {
                REL::Relocation<
                    std::uintptr_t>
                    vtbl{
                        RE::VTABLE_DualValueModifierEffect[
                            0]
                    };

                const auto original =
                    vtbl.write_vfunc(
                        0x20,
                        DualModifyActorValueThunk);

                dualModifyActorValueFunc =
                    reinterpret_cast<
                        dual_modify_actor_value_func_t>(
                            original);
            }

            {
                REL::Relocation<
                    std::uintptr_t>
                    vtbl{
                        RE::VTABLE_ValueAndConditionsEffect[
                            0]
                    };

                const auto original =
                    vtbl.write_vfunc(
                        0x20,
                        ValueConditionsModifyActorValueThunk);

                valueConditionsModifyActorValueFunc =
                    reinterpret_cast<
                        value_conditions_modify_actor_value_func_t>(
                            original);
            }

            {
                REL::Relocation<
                    std::uintptr_t>
                    vtbl{
                        RE::VTABLE_PeakValueModifierEffect[
                            0]
                    };

                const auto original =
                    vtbl.write_vfunc(
                        0x20,
                        PeakModifyActorValueThunk);

                peakModifyActorValueFunc =
                    reinterpret_cast<
                        peak_modify_actor_value_func_t>(
                            original);
            }

            if (!modifyOnStartFunc ||
                !valueModifyActorValueFunc ||
                !dualModifyActorValueFunc ||
                !valueConditionsModifyActorValueFunc ||
                !peakModifyActorValueFunc) {

                logger::error(
                    "[Combat] SpellDamage production hooks = FAILED");

                return false;
            }

            logger::info(
                "[Combat] SpellDamage production hooks = OK | Healing=Value/VAC/Peak 0x20 | LegacyStart=pass-through | Direct=Value/Dual/VAC/Peak 0x20");

            return true;
        }
    }
    class InputEventSink final :
        public RE::BSTEventSink<
            RE::InputEvent*>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* a_events,
            RE::BSTEventSource<
                RE::InputEvent*>*) override
        {
            if (!a_events ||
                !*a_events) {

                return RE::BSEventNotifyControl::
                    kContinue;
            }

            auto* userEvents =
                RE::UserEvents::
                    GetSingleton();

            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!userEvents ||
                !player) {

                return RE::BSEventNotifyControl::
                    kContinue;
            }

            EnsurePlayerMovementStateSink();

            for (auto* input = *a_events;
                 input;
                 input = input->next) {

                auto* button =
                    input->AsButtonEvent();

                if (!button) {
                    continue;
                }

                
const auto& userEvent =
                    button->QUserEvent();

                const bool isShout =
                    userEvent ==
                        userEvents->shout;

                if (isShout) {
                    if (button->IsDown()) {
                        TULLBUNG::ClassRuntime::
                            OnPlayerShoutInput();
                    }

                    continue;
                }

                const bool isSprint =
                    userEvent ==
                        userEvents->sprint;

                if (isSprint) {
                    if (button->IsDown() &&
                        !g_sprintInputHeld) {

                        TULLBUNG::ClassRuntime::
                            OnPlayerSprintInput(
                                true);

                        g_sprintInputHeld = true;

                    } else if (
                        button->IsUp() &&
                        g_sprintInputHeld) {

                        TULLBUNG::ClassRuntime::
                            OnPlayerSprintInput(
                                false);

                        g_sprintInputHeld = false;
                    }

                    continue;
                }

                const bool isLeft =
                    userEvent ==
                        userEvents->leftAttack;

                const bool isRight =
                    userEvent ==
                        userEvents->rightAttack;

                if (!isLeft &&
                    !isRight) {

                    continue;
                }

                auto& held =
                    isLeft ?
                        g_leftAttackHeld :
                        g_rightAttackHeld;

                auto& generation =
                    isLeft ?
                        g_leftAttackGeneration :
                        g_rightAttackGeneration;

                if (button->IsDown()) {
                    if (held) {
                        continue;
                    }

                    held = true;
                    ++generation;

                    logger::info(
                        "[WeaponInputBoundary] Hand={} | Generation={}",
                        isLeft ? "LEFT" : "RIGHT",
                        generation);

                    if (isRight &&
                        player->IsBlocking() &&
                        !CanUseDualWeaponAction(
                            player)) {

                        ++g_bashInputGeneration;

                        logger::info(
                            "[BashBoundary] Generation={} | Blocking=true",
                            g_bashInputGeneration);
                    }
                } else if (button->IsUp()) {
                    held = false;
                }
            }

            return RE::BSEventNotifyControl::
                kContinue;
        }
    };

    class ActionEventSink final :
        public RE::BSTEventSink<
            SKSE::ActionEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::ActionEvent* a_event,
            RE::BSTEventSource<
                SKSE::ActionEvent>*) override
        {
            if (!a_event ||
                !a_event->actor) {

                return RE::BSEventNotifyControl::
                    kContinue;
            }

            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            // Stage 12 needs NPC weapon-swing boundaries for
            // Iron Wall's "once per enemy attack action" contract.
            // Production player ActionState remains owned by Combat.
            TULLBUNG::ClassRuntime::
                OnActorActionEvent(
                    *a_event);

            if (!player ||
                a_event->actor != player) {

                return RE::BSEventNotifyControl::
                    kContinue;
            }

            const auto type =
                a_event->type.get();

            const auto slot =
                a_event->slot.get();

            const auto eventHand =
                ActionSlotToHand(slot);

            auto* source =
                a_event->sourceForm;

            const bool equippedMatch =
                SourceMatchesEquippedSlot(
                    player,
                    source,
                    slot);

            logger::info(
                "[ActionEvent] Type={} | Slot={} | Source={:08X} | EquippedMatch={}",
                ActionEventTypeName(type),
                HandName(eventHand),
                source ?
                    source->GetFormID() :
                    0,
                equippedMatch);

            switch (type) {
            case SKSE::ActionEvent::Type::
                kWeaponSwing:
            {
                if (!source ||
                    !equippedMatch) {

                    break;
                }

                auto* weapon =
                    source->As<
                        RE::TESObjectWEAP>();

                if (!weapon ||
                    !TULLBUNG::
                        Classification::
                            IsMelee(weapon)) {

                    break;
                }

                const auto actionHand =
                    IsTwoHandedMelee(weapon) ?
                        Hand::kBoth :
                        eventHand;



                HandleMeleeWeaponSwing(
                    player,
                    source,
                    actionHand);

                break;
            }

            case SKSE::ActionEvent::Type::
                kSpellCast:
            {
                if (source &&
                    equippedMatch) {

                    BeginPlayerAction(
                        g_eventAction,
                        player,
                        source,
                        eventHand,
                        ActionOrigin::
                            kSpellCast);

                    auto* spell =
                        source->As<
                            RE::SpellItem>();

                    if (spell) {
                        g_castTransaction =
                            Snapshot::
                                CaptureCastStart(
                                    player,
                                    g_eventAction.
                                        actionID,
                                    spell,
                                    ActionSlotToCastingSource(
                                        slot),
                                    BuildSnapshotExtendedState(
                                        player));

                        Snapshot::DebugLogCast(
                            g_castTransaction,
                            "CAST");
                    }
                }

                break;
            }

            case SKSE::ActionEvent::Type::
                kSpellFire:
            {
                if (!source ||
                    !equippedMatch) {

                    break;
                }

                const bool sameCast =
                    g_eventAction.actionID &&
                    g_eventAction.actor ==
                        player &&
                    g_eventAction.sourceForm ==
                        source &&
                    HandsCompatible(
                        g_eventAction.hand,
                        eventHand) &&
                    g_eventAction.origin ==
                        ActionOrigin::
                            kSpellCast;

                if (sameCast) {
                    g_eventAction.
                        spellFireSeen =
                            true;

                    logger::info(
                        "[AttackAction] Continue | ID={} | Event=SPELL_FIRE",
                        g_eventAction.actionID);

                    if (g_castTransaction.valid &&
                        g_castTransaction.actionID ==
                            g_eventAction.actionID &&
                        g_castTransaction.sourceFormID ==
                            source->GetFormID()) {

                        Snapshot::
                            FinalizeCastAtFire(
                                g_castTransaction,
                                player);

                        Snapshot::DebugLogCast(
                            g_castTransaction,
                            "FIRE");
                        TULLBUNG::ClassRuntime::
                            OnPlayerCastFinalized(
                                g_castTransaction);
                    }
                } else {
                    BeginPlayerAction(
                        g_eventAction,
                        player,
                        source,
                        eventHand,
                        ActionOrigin::
                            kSpellFireFallback);

                    g_eventAction.
                        spellFireSeen =
                            true;

                    auto* spell =
                        source->As<
                            RE::SpellItem>();

                    if (spell) {
                        g_castTransaction =
                            Snapshot::
                                CaptureCastStart(
                                    player,
                                    g_eventAction.
                                        actionID,
                                    spell,
                                    ActionSlotToCastingSource(
                                        slot),
                                    BuildSnapshotExtendedState(
                                        player));

                        Snapshot::
                            FinalizeCastAtFire(
                                g_castTransaction,
                                player);

                        Snapshot::DebugLogCast(
                            g_castTransaction,
                            "FIRE_FALLBACK");
                        TULLBUNG::ClassRuntime::
                            OnPlayerCastFinalized(
                                g_castTransaction);
                    }
                }

                OnPlayerSpellFireIdentity(
                    source->As<
                        RE::SpellItem>(),
                    ActionSlotToCastingSource(
                        slot),
                    g_eventAction);
                break;
            }

            case SKSE::ActionEvent::Type::
                kBowDraw:
            {
                if (source &&
                    equippedMatch) {

                    auto* weapon =
                        source->As<
                            RE::TESObjectWEAP>();

                    if (weapon &&
                        TULLBUNG::
                            Classification::
                                IsRanged(weapon)) {



                        // Crossbow on SE 1.5.97 does not emit the
                        // ordinary bow-release authority before HitData.
                        // Its kBowDraw event is the proven fire boundary.
                        if (weapon->IsCrossbow()) {

                            BeginPlayerAction(
                                g_eventAction,
                                player,
                                source,
                                eventHand,
                                ActionOrigin::
                                    kBowRelease);


                            g_rangedLaunchState = {};

                            g_rangedLaunchState.valid =
                                true;

                            g_rangedLaunchState.actionID =
                                g_eventAction.actionID;

                            g_rangedLaunchState.sourceFormID =
                                source->GetFormID();

                            g_rangedLaunchState.position =
                                player->GetPosition();

                            TULLBUNG::ClassRuntime::
                                OnPlayerRangedLaunch(
                                    g_eventAction.actionID,
                                    weapon,
                                    g_rangedLaunchState.position.x,
                                    g_rangedLaunchState.position.y,
                                    g_rangedLaunchState.position.z);

                            logger::info(
                                "[RANGED-CROSSBOW-LAUNCH] ActionID={} | Source={:08X} | Position=({:.3f},{:.3f},{:.3f})",
                                g_rangedLaunchState.actionID,
                                g_rangedLaunchState.sourceFormID,
                                g_rangedLaunchState.position.x,
                                g_rangedLaunchState.position.y,
                                g_rangedLaunchState.position.z);


                            const auto launchSnapshot =
                                Snapshot::CaptureLaunch(
                                    player,
                                    g_eventAction.actionID,
                                    source->GetFormID(),
                                    BuildSnapshotExtendedState(
                                        player));

                            Snapshot::DebugLogLaunch(
                                launchSnapshot);
                        }
                    }
                }

                break;
            }

            case SKSE::ActionEvent::Type::
                kBowRelease:
            {
                if (source &&
                    equippedMatch) {

                    BeginPlayerAction(
                        g_eventAction,
                        player,
                        source,
                        eventHand,
                        ActionOrigin::
                            kBowRelease);

                    auto* weapon =
                        source->As<
                            RE::TESObjectWEAP>();

                    if (weapon) {

                    }

                    g_rangedLaunchState = {};

                    g_rangedLaunchState.valid =
                        true;

                    g_rangedLaunchState.actionID =
                        g_eventAction.actionID;

                    g_rangedLaunchState.sourceFormID =
                        source->GetFormID();

                    g_rangedLaunchState.position =
                        player->GetPosition();

                    TULLBUNG::ClassRuntime::
                        OnPlayerRangedLaunch(
                            g_eventAction.actionID,
                            weapon,
                            g_rangedLaunchState.position.x,
                            g_rangedLaunchState.position.y,
                            g_rangedLaunchState.position.z);


                    const auto launchSnapshot =
                        Snapshot::CaptureLaunch(
                            player,
                            g_eventAction.actionID,
                            source->GetFormID(),
                            BuildSnapshotExtendedState(
                                player));

                    Snapshot::DebugLogLaunch(
                        launchSnapshot);
                }

                break;
            }

            default:
                break;
            }

            return RE::BSEventNotifyControl::
                kContinue;
        }
    };

    class HitEventSink final :
        public RE::BSTEventSink<
            RE::TESHitEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESHitEvent* a_event,
            RE::BSTEventSource<
                RE::TESHitEvent>*) override
        {
            if (!a_event) {
                return RE::BSEventNotifyControl::
                    kContinue;
            }

            CombatContext context{};

            if (!TULLBUNG::Combat::
                    BuildFromHitEvent(
                        *a_event,
                        context)) {

                return RE::BSEventNotifyControl::
                    kContinue;
            }

            RegisterSpellImpactAuthority(
                context);

            if (context.playerAttacker) {
                TULLBUNG::ClassRuntime::
                    OnPlayerHitEvent(
                        context);
            }

            if (context.playerAttacker ||
                context.playerTarget) {

                TULLBUNG::Combat::
                    DebugLogContext(
                        context);
            }

            return RE::BSEventNotifyControl::
                kContinue;
        }
    };

    InputEventSink g_inputEventSink{};
    ActionEventSink g_actionEventSink{};
    HitEventSink g_hitEventSink{};
}

namespace TULLBUNG::Combat
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
                "[Combat] ScriptEventSourceHolder = MISSING");

            return false;
        }

        auto* actionSource =
            SKSE::GetActionEventSource();

        if (!actionSource) {
            logger::error(
                "[Combat] SKSE ActionEvent source = MISSING");

            return false;
        }

        auto* inputSource =
            RE::BSInputDeviceManager::
                GetSingleton();

        if (!inputSource) {
            logger::error(
                "[Combat] InputEvent source = MISSING");

            return false;
        }

        if (!TULLBUNG::DamageRules::
                RunSelfTest()) {

            logger::error(
                "[Combat] DamageRules SelfTest = FAIL");

            return false;
        }

        logger::info(
            "[Combat] DamageRules SelfTest = PASS");

        if (!Snapshot::RunSelfTest()) {
            logger::error(
                "[Combat] Snapshot SelfTest = FAIL");

            return false;
        }

        logger::info(
            "[Combat] Snapshot SelfTest = PASS");

        if (!WeaponHitHook::Install()) {
            logger::error(
                "[Combat] HitData weapon hook installation failed");

            return false;
        }

        if (!HealthDamageProbeHook::Install()) {
            logger::error(
                "[Combat] S15 post-damage probe hook installation failed");

            return false;
        }
        if (!CastingLifecycleHook::Install()) {
            logger::error(
                "[Combat] S15 casting lifecycle hook installation failed");

            return false;
        }

        if (!ProjectileImpactHook::Install()) {
            logger::error(
                "[Combat] S15 projectile impact hook installation failed");

            return false;
        }
        if (!PlayerActorValueHook::Install()) {
            logger::error(
                "[Combat] S15 Player ActorValueOwner hook installation failed");

            return false;
        }
        if (!SpellDamageHook::Install()) {
            logger::error(
                "[Combat] Spell damage adapter hook installation failed");

            return false;
        }

        eventHolder->AddEventSink(
            &g_hitEventSink);

        actionSource->AddEventSink(
            &g_actionEventSink);

        inputSource->AddEventSink(
            &g_inputEventSink);

        EnsurePlayerMovementStateSink();

        g_registered = true;

        ResetTransientState();

        logger::info(
            "[Combat] TESHitEvent sink = OK");

        logger::info(
            "[Combat] SKSE ActionEvent sink = OK");

        logger::info(
            "[Combat] InputEvent sink = OK | Weapon input boundary + Bash");

        return true;
    }

    void ResetTransientState()
    {
        TULLBUNG::DamageRules::
            ResetTransientState();

        TULLBUNG::ClassRuntime::
            ResetTransientState();

        g_rightAction = {};
        g_leftAction = {};
        g_bothAction = {};
        g_eventAction = {};
        g_bashAction = {};
        ResetS19QaValueTrace();

        g_rawEventSequence = 0;
        g_healthDamageProbeSequence = 0;
        g_nextActionID = 1;

        g_leftAttackHeld = false;
        g_rightAttackHeld = false;

        g_leftAttackGeneration = 0;
        g_rightAttackGeneration = 0;
        g_bashInputGeneration = 0;

        g_sprintInputHeld = false;


        g_castTransaction = {};

        g_incomingDirectDepth = 0;
        g_incomingDirectStack = {};

        g_rangedLaunchState = {};
        g_pendingWeaponDirectHit = {};
        g_pendingBarrierWeaponReflect = {};

        ResetSpellImpactAuthority();
        ResetProjectileIdentityCorrelation();

        g_playerMovingKnown = false;
        g_playerLastMoving = false;

        if (auto* player =
                RE::PlayerCharacter::
                    GetSingleton()) {

            const bool moving =
                player->IsMoving();

            g_playerMovingKnown = true;
            g_playerLastMoving = moving;

            TULLBUNG::ClassRuntime::
                OnPlayerMovementStateChanged(
                    moving);
        }
        logger::info(
            "[Combat] Transient state reset");
    }

    bool BuildFromHitEvent(
        const RE::TESHitEvent& a_event,
        CombatContext& a_outContext)
    {
        a_outContext = {};

        const bool scopedFirewall =
            TULLBUNG::DamageRules::
                IsTriggerSuppressed();

        const bool registeredFirewall =
            TULLBUNG::DamageRules::
                IsRegisteredFirewallSource(
                    a_event.source);

        if (scopedFirewall ||
            registeredFirewall) {

            logger::info(
                "[TriggerFirewall] TESHitEvent suppressed | Source={:08X} | Scoped={} | Registered={} | Origin={}",
                a_event.source,
                scopedFirewall,
                registeredFirewall,
                TULLBUNG::DamageRules::
                    FirewallOriginName(
                        TULLBUNG::DamageRules::
                            GetFirewallOrigin()));

            return false;
        }

        auto* attackerRef =
            a_event.cause.get();

        auto* targetRef =
            a_event.target.get();

        auto* attacker =
            attackerRef ?
                attackerRef->As<RE::Actor>() :
                nullptr;

        auto* target =
            targetRef ?
                targetRef->As<RE::Actor>() :
                nullptr;

        if (!attacker ||
            !target) {

            return false;
        }

        a_outContext.attacker =
            attacker;

        a_outContext.target =
            target;

        a_outContext.sourceFormID =
            a_event.source;

        a_outContext.projectileFormID =
            a_event.projectile;

        a_outContext.sourceForm =
            ResolveForm(
                a_event.source);

        auto* projectileForm =
            ResolveForm(
                a_event.projectile);

        a_outContext.projectile =
            projectileForm ?
                projectileForm->As<
                    RE::BGSProjectile>() :
                nullptr;

        a_outContext.sourceKind =
            ResolveSourceKind(
                a_outContext.sourceForm,
                a_outContext.weapon,
                a_outContext.spell);

        a_outContext.hand =
            ResolveEquippedHand(
                attacker,
                a_outContext.sourceForm);

        const bool power =
            a_event.flags.any(
                RE::TESHitEvent::Flag::
                    kPowerAttack);

        const bool bash =
            a_event.flags.any(
                RE::TESHitEvent::Flag::
                    kBashAttack);

        a_outContext.sneakAttack =
            a_event.flags.any(
                RE::TESHitEvent::Flag::
                    kSneakAttack);

        a_outContext.blocked =
            a_event.flags.any(
                RE::TESHitEvent::Flag::
                    kHitBlocked);

        if (bash) {
            a_outContext.attackKind =
                AttackKind::kBash;
        } else if (power) {
            a_outContext.attackKind =
                AttackKind::kPower;
        } else {
            a_outContext.attackKind =
                AttackKind::kNormal;
        }

        a_outContext.range =
            ResolveRange(
                a_outContext.weapon,
                a_outContext.spell,
                a_outContext.projectileFormID,
                a_outContext.attackKind);

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        a_outContext.playerAttacker =
            player &&
            attacker == player;

        a_outContext.playerTarget =
            player &&
            target == player;

        a_outContext.weaponDirectCandidate =
            a_outContext.weapon &&
            !a_outContext.weapon->IsStaff();

        a_outContext.spellDirectCandidate =
            a_outContext.spell &&
            Classification::
                IsAttackSpell(
                    a_outContext.spell);

        a_outContext.directDamage =
            0.0f;

        a_outContext.directDamageKnown =
            false;

        a_outContext.rawEventSequence =
            ++g_rawEventSequence;

        if (a_outContext.playerAttacker &&
            a_outContext.attackKind ==
                AttackKind::kBash) {

            AttachBashEventAction(
                a_outContext);

            a_outContext.hitSnapshot =
                Snapshot::CaptureHit(
                    target,
                    a_outContext.attackActionID);

            Snapshot::DebugLogHit(
                a_outContext.hitSnapshot);

            return true;
        }

        // Stage 11C-1:
        // player weapon direct hits are authoritative in
        // HitData for both melee and ranged weapons.
        // TESHitEvent must not consume FirstDirect first.
        if (a_outContext.playerAttacker &&
            a_outContext.weapon &&
            (Classification::
                 IsMelee(
                     a_outContext.weapon) ||
             Classification::
                 IsRanged(
                     a_outContext.weapon))) {

            return false;
        }

        AttachEventAction(
            a_outContext);

        if (a_outContext.playerAttacker &&
            (a_outContext.
                 weaponDirectCandidate ||
             a_outContext.
                 spellDirectCandidate)) {

            a_outContext.hitSnapshot =
                Snapshot::CaptureHit(
                    target,
                    a_outContext.attackActionID);

            Snapshot::DebugLogHit(
                a_outContext.hitSnapshot);
        }

        return true;
    }

    void DebugLogContext(
        const CombatContext& a_context)
    {
        logger::info(
            "[CombatContext] Seq={} | Attacker=\"{}\"({:08X}) | Target=\"{}\"({:08X}) | Source={:08X} | SourceKind={} | Projectile={:08X} | Hand={} | Range={} | Attack={} | Sneak={} | Blocked={} | PlayerAttacker={} | PlayerTarget={} | WeaponDirectCandidate={} | SpellDirectCandidate={} | DirectDamage={:.3f} | DamageKnown={} | ActionID={} | ActionMatched={} | FirstDirect={}",
            a_context.rawEventSequence,
            ActorName(
                a_context.attacker),
            a_context.attacker ?
                a_context.attacker->GetFormID() :
                0,
            ActorName(
                a_context.target),
            a_context.target ?
                a_context.target->GetFormID() :
                0,
            a_context.sourceFormID,
            SourceName(
                a_context.sourceKind),
            a_context.projectileFormID,
            HandName(
                a_context.hand),
            RangeName(
                a_context.range),
            AttackName(
                a_context.attackKind),
            a_context.sneakAttack,
            a_context.blocked,
            a_context.playerAttacker,
            a_context.playerTarget,
            a_context.weaponDirectCandidate,
            a_context.spellDirectCandidate,
            a_context.directDamage,
            a_context.directDamageKnown,
            a_context.attackActionID,
            a_context.actionMatched,
            a_context.firstValidDirectHit);
    }
}
