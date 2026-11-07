#include "PCH.h"
#include "Inquisitor.h"

#include "BuildState.h"
#include "Classification.h"
#include "DataCache.h"
#include "Persistence.h"
#include "PlayerState.h"
#include "Targeting.h"

#include "RE/P/ProcessLists.h"
#include "RE/P/Projectile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    bool g_available{};

    inline constexpr std::uint32_t
        kMaxMasterKills = 500;

    inline constexpr std::uint32_t
        kBaseSmiteDamage = 300;

    inline constexpr std::uint32_t
        kKillsPerStage = 100;

    inline constexpr std::uint32_t
        kDamagePerStage = 100;

    inline constexpr std::uint32_t
        kMaxSmiteDamage = 800;

    inline constexpr float
        kSmiteMagickaCost = 100.0f;

    // Skyrim world-space convention:
    // 128 units ~= 6 feet.
    // 60 feet = 1280 units.
    inline constexpr float
        kSmiteRadiusUnits = 1280.0f;

    inline constexpr float
        kSmiteVFXOriginHeightUnits = 512.0f;

    inline constexpr float
        kSmiteVFXTargetHeightUnits = 64.0f;

    [[nodiscard]] float
        ResolveHolyDamageAgainstMagicResistance(
            RE::Actor* a_target,
            float a_rawDamage) noexcept
    {
        if (!a_target ||
            !std::isfinite(a_rawDamage) ||
            a_rawDamage <= 0.0f) {

            return 0.0f;
        }

        // Keep Holy semantics identical to Paladin Holy Strike:
        // non-elemental magic damage, affected by Magic Resist.
        auto* targetAVOwner =
            a_target->AsActorValueOwner();

        if (!targetAVOwner) {
            return a_rawDamage;
        }

        const float magicResistance =
            std::clamp(
                targetAVOwner->GetActorValue(
                    RE::ActorValue::
                        kResistMagic),
                -100.0f,
                85.0f);

        return
            a_rawDamage *
            (1.0f -
             magicResistance /
                 100.0f);
    }

    [[nodiscard]] RE::Actor*
        FindSmiteTarget(
            RE::Actor* a_player) noexcept
    {
        const auto selection =
            TULLBUNG::Targeting::
                FindHighestMaxHealthMonsterWithLOS(
                    a_player,
                    kSmiteRadiusUnits);

        if (selection.actor) {
            logger::info(
                "[S14SMITE-TARGET] Target={:08X} | MaxHP={:.2f} | Distance={:.2f}",
                selection.actor->
                    GetFormID(),
                selection.score,
                selection.distance);
        }

        return selection.actor;
    }
    constexpr std::size_t
        kSmiteVfxCasterSlotCount = 8;

    std::array<
        RE::NiPointer<RE::TESObjectREFR>,
        kSmiteVfxCasterSlotCount>
            g_smiteVfxCasterSlots{};

    std::size_t
        g_smiteVfxCasterCursor = 0;

    bool
        g_smiteVfxCasterPoolDirty = false;

    void DestroySmiteVFXCaster(
        RE::NiPointer<RE::TESObjectREFR>&
            a_slot) noexcept
    {
        if (!a_slot) {
            return;
        }

        a_slot->SetCollision(false);
        a_slot->Disable();
        a_slot->SetDelete(true);

        a_slot = nullptr;
    }

    void CleanupSmiteVFXCasters(
        const char* a_reason) noexcept
    {
        std::size_t removed = 0;

        for (auto& slot :
             g_smiteVfxCasterSlots) {

            if (!slot) {
                continue;
            }

            DestroySmiteVFXCaster(
                slot);

            ++removed;
        }

        g_smiteVfxCasterCursor = 0;
        g_smiteVfxCasterPoolDirty = false;

        if (removed > 0) {
            logger::info(
                "[S14SMITE-VFX] CasterPool=CLEANUP | Reason={} | Count={}",
                a_reason ?
                    a_reason :
                    "UNKNOWN",
                removed);
        }
    }

    [[nodiscard]] RE::TESObjectREFR*
        GetSmiteVFXCaster(
            RE::Actor* a_target,
            const RE::NiPoint3&
                a_origin) noexcept
    {
        if (!a_target) {
            return nullptr;
        }

        auto* fxBase =
            RE::TESForm::
                LookupByEditorID<
                    RE::TESObjectACTI>(
                        "FXEmptyActivator");

        if (!fxBase) {
            logger::error(
                "[S14SMITE-VFX] Result=NO_FX_EMPTY_ACTIVATOR");

            return nullptr;
        }

        const auto slotIndex =
            g_smiteVfxCasterCursor;

        auto& slot =
            g_smiteVfxCasterSlots[
                slotIndex];

        // One caster reference is used for exactly one Smite.
        // On bounded ring reuse, destroy the old reference first.
        if (slot) {
            DestroySmiteVFXCaster(
                slot);
        }

        auto placed =
            a_target->PlaceObjectAtMe(
                fxBase,
                false);

        if (!placed) {
            logger::error(
                "[S14SMITE-VFX] Result=ACTIVATOR_PLACE_FAILED | Slot={}",
                slotIndex);

            return nullptr;
        }

        slot =
            placed;

        g_smiteVfxCasterPoolDirty =
            true;

        slot->SetActivationBlocked(
            true);

        slot->SetCollision(
            false);

        slot->SetPosition(
            a_origin);

        slot->Update3DPosition(
            true);

        g_smiteVfxCasterCursor =
            (g_smiteVfxCasterCursor + 1) %
            kSmiteVfxCasterSlotCount;

        logger::info(
            "[S14SMITE-VFX] RemoteCaster=CREATED | Slot={} | Ref={:08X}",
            slotIndex,
            slot->GetFormID());

        return
            slot.get();
    }
    [[nodiscard]] bool
        DispatchSmiteVFX(
            RE::Actor* a_player,
            RE::Actor* a_target,
            RE::SpellItem* a_vfxSpell = nullptr) noexcept
    {
        if (!a_player ||
            !a_target) {

            return false;
        }

        auto* vfxSpell =
            a_vfxSpell;

        if (!vfxSpell) {
            vfxSpell =
                TULLBUNG::DataCache::
                    GetPriestFamilySpell(
                        TULLBUNG::DataCache::
                            PriestFamilySpellID::
                                kInquisitorSmiteVFX);
        }

        if (!vfxSpell) {
            logger::error(
                "[S14SMITE-VFX] Result=MISSING_VFX_SPELL");

            return false;
        }

        auto targetPoint =
            a_target->GetPosition();

        targetPoint.z +=
            kSmiteVFXTargetHeightUnits;

        auto origin =
            targetPoint;

        origin.z +=
            kSmiteVFXOriginHeightUnits;

        auto* casterRef =
            GetSmiteVFXCaster(
                a_target,
                origin);

        if (!casterRef) {
            return false;
        }

        auto* caster =
            casterRef->GetMagicCaster(
                RE::MagicSystem::
                    CastingSource::
                        kOther);

        if (!caster) {
            // Some non-actor references expose their immediate
            // caster through the instant source instead.
            caster =
                casterRef->GetMagicCaster(
                    RE::MagicSystem::
                        CastingSource::
                            kInstant);
        }

        if (!caster) {
            logger::error(
                "[S14SMITE-VFX] Result=NO_NONACTOR_MAGIC_CASTER | Ref={:08X}",
                casterRef->GetFormID());

            return false;
        }

        caster->CastSpellImmediate(
            vfxSpell,
            false,
            a_target,
            1.0f,
            false,
            0.0f,
            a_player);

        logger::info(
            "[S14SMITE-VFX] Result=REMOTE_CAST | Caster={:08X} | Target={:08X} | OriginZ={:.2f} | TargetZ={:.2f}",
            casterRef->GetFormID(),
            a_target->GetFormID(),
            origin.z,
            targetPoint.z);

        return true;
    }
}

namespace TULLBUNG::Classes::Inquisitor
{
    bool Initialize() noexcept
    {
        g_available =
            TULLBUNG::DataCache::
                IsInquisitorReady();

        logger::info(
            "[Stage14] Inquisitor runtime = {}",
            g_available ?
                "READY" :
                "DISABLED");

        return g_available;
    }

    bool IsAvailable() noexcept
    {
        return g_available;
    }

    bool DispatchSharedVFX(
        RE::Actor* a_player,
        RE::Actor* a_target,
        RE::SpellItem* a_vfxSpell) noexcept
    {
        if (!a_vfxSpell) {
            return false;
        }

        return DispatchSmiteVFX(
            a_player,
            a_target,
            a_vfxSpell);
    }

    void OnCombatStateChanged(
        RE::Actor* a_actor,
        RE::Actor* a_target,
        bool a_enteredCombat,
        bool a_leftCombat) noexcept
    {
        (void)a_target;
        (void)a_enteredCombat;

        if (!a_leftCombat) {
            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            a_actor != player) {

            return;
        }

        CleanupSmiteVFXCasters(
            "PLAYER_COMBAT_END");
    }
    void OnPlayerMovementStateChanged(
        bool a_moving) noexcept
    {
        (void)a_moving;

        if (!g_smiteVfxCasterPoolDirty) {
            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            player->IsInCombat()) {

            return;
        }

        CleanupSmiteVFXCasters(
            "POST_COMBAT_MOVEMENT");
    }
    void ResetTransientState() noexcept
    {
        CleanupSmiteVFXCasters("TRANSIENT_RESET");

        RemoveRuntimePassives();

        // Monster-kill progression is persistent INQ1 state.
        // Do not clear it at transient reset/load boundaries.
    }


    namespace
    {
        void SetRuntimeAbility(
            RE::SpellItem* a_spell,
            bool a_enable,
            const char* a_name) noexcept
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                !a_spell) {

                return;
            }

            const bool hasSpell =
                player->HasSpell(
                    a_spell);

            if (a_enable &&
                !hasSpell) {

                player->AddSpell(
                    a_spell);

                logger::info(
                    "[S14INQ-PASSIVE] {} = ON",
                    a_name);
            } else if (
                !a_enable &&
                hasSpell) {

                player->RemoveSpell(
                    a_spell);

                logger::info(
                    "[S14INQ-PASSIVE] {} = OFF",
                    a_name);
            }
        }

        constexpr std::array<
            TULLBUNG::DataCache::
                PriestFamilySpellID,
            8>
            kArtifactAbilities{{
                TULLBUNG::DataCache::
                    PriestFamilySpellID::
                        kInquisitorArtifact1,
                TULLBUNG::DataCache::
                    PriestFamilySpellID::
                        kInquisitorArtifact2,
                TULLBUNG::DataCache::
                    PriestFamilySpellID::
                        kInquisitorArtifact3,
                TULLBUNG::DataCache::
                    PriestFamilySpellID::
                        kInquisitorArtifact4,
                TULLBUNG::DataCache::
                    PriestFamilySpellID::
                        kInquisitorArtifact5,
                TULLBUNG::DataCache::
                    PriestFamilySpellID::
                        kInquisitorArtifact6,
                TULLBUNG::DataCache::
                    PriestFamilySpellID::
                        kInquisitorArtifact7,
                TULLBUNG::DataCache::
                    PriestFamilySpellID::
                        kInquisitorArtifact8
            }};
    }

    void SyncRuntimePassives() noexcept
    {
        if (!IsAvailable() ||
            !TULLBUNG::DataCache::
                IsInquisitorReady()) {

            return;
        }

        const bool artifactStudyActive =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorArtifactStudy);

        const auto counted =
            TULLBUNG::PlayerState::
                GetArtifacts().
                    counted;

        const int selectedIndex =
            artifactStudyActive &&
            counted >= 1 &&
            counted <= 8 ?
                static_cast<int>(
                    counted - 1) :
                -1;

        for (std::size_t i = 0;
             i < kArtifactAbilities.size();
             ++i) {

            SetRuntimeAbility(
                TULLBUNG::DataCache::
                    GetPriestFamilySpell(
                        kArtifactAbilities[i]),
                static_cast<int>(i) ==
                    selectedIndex,
                "ArtifactStudy");
        }

        const bool smiteActive =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorSmite);

        SetRuntimeAbility(
            TULLBUNG::DataCache::
                GetPriestFamilySpell(
                    TULLBUNG::DataCache::
                        PriestFamilySpellID::
                            kInquisitorSmite),
            smiteActive,
            "SmitePower");

        logger::info(
            "[S14INQ-PASSIVE] ArtifactActive={} | Counted={} | SelectedTier={} | SmitePower={}",
            artifactStudyActive,
            counted,
            selectedIndex >= 0 ?
                selectedIndex + 1 :
                0,
            smiteActive);
    }

    void RemoveRuntimePassives() noexcept
    {
        if (!TULLBUNG::DataCache::
                IsInquisitorReady()) {

            return;
        }

        for (const auto id :
             kArtifactAbilities) {

            SetRuntimeAbility(
                TULLBUNG::DataCache::
                    GetPriestFamilySpell(id),
                false,
                "ArtifactStudy");
        }

        SetRuntimeAbility(
            TULLBUNG::DataCache::
                GetPriestFamilySpell(
                    TULLBUNG::DataCache::
                        PriestFamilySpellID::
                            kInquisitorSmite),
            false,
            "SmitePower");
    }
    void AccumulateSpellDirectDamage(
        const RE::SpellItem* a_spell,
        RE::Actor* a_target,
        DamageRules::DirectDamageAccumulator&
            a_accumulator) noexcept
    {
        if (!IsAvailable() ||
            !a_spell ||
            !a_target ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorMonsterStudy) ||
            !TULLBUNG::Classification::
                IsAttackSpell(
                    a_spell) ||
            !TULLBUNG::Classification::
                IsMonster(
                    a_target)) {

            return;
        }

        a_accumulator.AddPercent(
            DamageRules::DirectChannel::
                kSpell,
            20.0f);

        logger::info(
            "[S14DIRECT-SPELL][Inquisitor] Spell={:08X} | Target={:08X} | MonsterStudy=true | Bonus=20.00",
            a_spell->GetFormID(),
            a_target->GetFormID());
    }

    void AccumulateIncomingElementalDR(
        RE::Actor* a_caster,
        RE::Actor* a_target,
        const RE::EffectSetting* a_effect,
        DamageRules::ConditionalDRAccumulator&
            a_accumulator) noexcept
    {
        if (!IsAvailable() ||
            !a_caster ||
            !a_target ||
            !a_effect ||
            !a_effect->IsHostile() ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorMonsterDefense) ||
            !TULLBUNG::Classification::
                IsMonster(
                    a_caster)) {

            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            a_target != player ||
            a_caster == player) {

            return;
        }

        const auto resistance =
            a_effect->data.
                resistVariable;

        const bool elemental =
            resistance ==
                RE::ActorValue::
                    kResistFire ||
            resistance ==
                RE::ActorValue::
                    kResistFrost ||
            resistance ==
                RE::ActorValue::
                    kResistShock;

        if (!elemental) {
            return;
        }

        a_accumulator.AddReductionPercent(
            20.0f);

        logger::info(
            "[S14INQ-DEFENSE] MonsterElementalDR=20 | Caster={:08X} | Effect={:08X} | ResistAV={}",
            a_caster->GetFormID(),
            a_effect->GetFormID(),
            static_cast<std::int32_t>(
                resistance));
    }

    void OnPlayerVoiceFire(
        RE::Actor* a_player) noexcept
    {
        if (!IsAvailable() ||
            !a_player ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorSmite) ||
            TULLBUNG::DamageRules::
                IsTriggerSuppressed()) {

            return;
        }

        auto* smitePower =
            TULLBUNG::DataCache::
                GetPriestFamilySpell(
                    TULLBUNG::DataCache::
                        PriestFamilySpellID::
                            kInquisitorSmite);

        if (!smitePower) {
            return;
        }

        auto* selectedPower =
            a_player->
                GetActorRuntimeData().
                    selectedPower;

        if (selectedPower !=
            smitePower) {

            return;
        }

        auto* target =
            FindSmiteTarget(
                a_player);

        if (!target) {
            logger::info(
                "[S14SMITE] Result=NO_TARGET | Cost=0");
            return;
        }

        auto* avOwner =
            a_player->
                AsActorValueOwner();

        if (!avOwner) {
            logger::error(
                "[S14SMITE] Result=NO_AV_OWNER | Cost=0");
            return;
        }

        const float currentMagicka =
            avOwner->GetActorValue(
                RE::ActorValue::
                    kMagicka);

        if (!std::isfinite(
                currentMagicka) ||
            currentMagicka +
                0.001f <
                kSmiteMagickaCost) {

            logger::info(
                "[S14SMITE] Result=NO_MAGICKA | Target={:08X} | CurrentMP={:.2f} | RequiredMP={:.2f} | Cost=0",
                target->GetFormID(),
                currentMagicka,
                kSmiteMagickaCost);

            return;
        }

        const float rawHolyDamage =
            static_cast<float>(
                GetSmiteDamage());

        const float resolvedHolyDamage =
            ResolveHolyDamageAgainstMagicResistance(
                target,
                rawHolyDamage);

        const bool applied =
            TULLBUNG::DamageRules::
                ApplyResolvedHealthDamage(
                    a_player,
                    target,
                    resolvedHolyDamage,
                    TULLBUNG::DamageRules::
                        FirewallOrigin::
                            kSpecialDamage);

        if (applied) {
            avOwner->ModActorValue(
                RE::ACTOR_VALUE_MODIFIERS::
                    kDamage,
                RE::ActorValue::
                    kMagicka,
                -kSmiteMagickaCost);

            (void)DispatchSmiteVFX(
                a_player,
                target);
        }

        logger::info(
            "[S14SMITE] Result={} | Target={:08X} | RawHoly={:.2f} | ResolvedHoly={:.2f} | MPSpent={:.2f} | MonsterKills={} | Master={}",
            applied ?
                "APPLIED" :
                "APPLY_FAIL",
            target->GetFormID(),
            rawHolyDamage,
            resolvedHolyDamage,
            applied ?
                kSmiteMagickaCost :
                0.0f,
            TULLBUNG::Persistence::
                GetInquisitorProgress().
                    monsterKills,
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorMaster));
    }
    void AccumulateWeaponDirectDamage(
        const Combat::CombatContext& a_context,
        DamageRules::DirectDamageAccumulator& a_accumulator) noexcept
    {
        if (!IsAvailable() ||
            !a_context.playerAttacker ||
            !a_context.target ||
            !a_context.weaponDirectCandidate ||
            !a_context.directDamageKnown ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorMonsterStudy) ||
            !TULLBUNG::Classification::
                IsMonster(
                    a_context.target)) {

            return;
        }

        a_accumulator.AddPercent(
            DamageRules::DirectChannel::
                kWeapon,
            20.0f);

        logger::info(
            "[S14DIRECT-WEAPON][Inquisitor] ActionID={} | MonsterStudy=true | Bonus=20.00",
            a_context.attackActionID);
    }

    void AccumulateIncomingDirectDR(
        RE::Actor* a_aggressor,
        const RE::HitData& a_data,
        const Snapshot::ActorResources& a_playerBefore,
        DamageRules::ConditionalDRAccumulator& a_accumulator) noexcept
    {
        (void)a_data;
        (void)a_playerBefore;

        if (!IsAvailable() ||
            !a_aggressor ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorMonsterDefense) ||
            !TULLBUNG::Classification::
                IsMonster(
                    a_aggressor)) {

            return;
        }

        // This ClassRuntime adapter is entered only for
        // incoming direct physical HitData.
        a_accumulator.AddReductionPercent(
            20.0f);

        logger::info(
            "[S14INQ-DEFENSE] MonsterPhysicalDirectDR=20 | Aggressor={:08X}",
            a_aggressor->GetFormID());
    }

    void OnDeathEvent(
        RE::Actor* a_dying,
        RE::Actor* a_killer) noexcept
    {
        if (!IsAvailable() ||
            !a_dying ||
            !a_killer ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorMaster) ||
            !TULLBUNG::Classification::
                IsMonster(
                    a_dying)) {

            return;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player ||
            a_killer != player) {

            return;
        }

        const auto before =
            TULLBUNG::Persistence::
                GetInquisitorProgress().
                    monsterKills;

        if (before >=
            kMaxMasterKills) {

            return;
        }

        const auto after =
            (std::min)(
                before + 1u,
                kMaxMasterKills);

        TULLBUNG::Persistence::
            SetInquisitorMonsterKills(
                after);

        logger::info(
            "[S14INQ-MASTER] MonsterKill={} | Progress={}/100 | SmiteDamage={}",
            after,
            after >= kMaxMasterKills ?
                100u :
                after % kKillsPerStage,
            GetSmiteDamage());
    }

    std::uint32_t
        GetSmiteDamage() noexcept
    {
        if (!TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kInquisitorMaster)) {

            return kBaseSmiteDamage;
        }

        const auto kills =
            (std::min)(
                TULLBUNG::Persistence::
                    GetInquisitorProgress().
                        monsterKills,
                kMaxMasterKills);

        const auto stages =
            kills /
            kKillsPerStage;

        return
            (std::min)(
                kBaseSmiteDamage +
                    stages *
                        kDamagePerStage,
                kMaxSmiteDamage);
    }
}