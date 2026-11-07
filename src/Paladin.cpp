#include "PCH.h"
#include "Paladin.h"

#include "BuildState.h"
#include "Classification.h"
#include "DataCache.h"
#include "PlayerState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace
{
    bool g_available{};

    std::uint8_t
        g_holyStrikeOvercastIndex{};

    constexpr std::array<float, 6>
        kHolyStrikeCostMultipliers{
            1.0f,
            2.0f,
            4.0f,
            8.0f,
            16.0f,
            32.0f
        };

    [[nodiscard]] bool
        HolyStrikeActive() noexcept
    {
        return TULLBUNG::BuildState::
            IsNodeActive(
                TULLBUNG::BuildState::
                    NodeID::
                        kPaladinHolyStrike);
    }

    [[nodiscard]] bool
        MasterActive() noexcept
    {
        return TULLBUNG::BuildState::
            IsNodeActive(
                TULLBUNG::BuildState::
                    NodeID::
                        kPaladinMaster);
    }

    [[nodiscard]] float
        HolyStrikeBaseCost() noexcept
    {
        return
            MasterActive() ?
                20.0f :
                40.0f;
    }

    [[nodiscard]] float
        HolyStrikeDamagePercent() noexcept
    {
        return
            MasterActive() ?
                50.0f :
                25.0f;
    }

    [[nodiscard]] float
        HolyStrikeCurrentCost() noexcept
    {
        const auto index =
            (std::min)(
                static_cast<std::size_t>(
                    g_holyStrikeOvercastIndex),
                kHolyStrikeCostMultipliers.
                    size() - 1);

        return
            HolyStrikeBaseCost() *
            kHolyStrikeCostMultipliers[
                index];
    }

    [[nodiscard]] bool
        SpendMagicka(
            float a_cost) noexcept
    {
        if (!std::isfinite(a_cost) ||
            a_cost <= 0.0f) {

            return false;
        }

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        if (!player) {
            return false;
        }

        // Eligibility is decided from the ActionSnapshot
        // by the Holy Strike caller.
        //
        // This function only commits the already-authorized
        // Magicka cost to the live ActorValue.
        auto* owner =
            player->AsActorValueOwner();

        if (!owner) {
            return false;
        }

        owner->ModActorValue(
            RE::ACTOR_VALUE_MODIFIERS::
                kDamage,
            RE::ActorValue::
                kMagicka,
            -a_cost);

        return true;
    }

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

        // Stage14 Holy damage contract:
        // non-elemental magic secondary,
        // affected by target Magic Resistance.
        //
        // Positive Skyrim magic resistance is capped at 85%.
        // Negative resistance remains meaningful as weakness.
        const float magicResistance =
            std::clamp(
                a_target->AsActorValueOwner()->GetActorValue(
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
}

namespace TULLBUNG::Classes::Paladin
{
    bool Initialize() noexcept
    {
        g_available =
            TULLBUNG::DataCache::
                IsPaladinReady();

        g_holyStrikeOvercastIndex = 0;

        logger::info(
            "[Stage14] Paladin runtime = {}",
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
        RemoveRuntimePassives();

        g_holyStrikeOvercastIndex = 0;

        logger::info(
            "[S14PALADIN] HolyStrike overcast reset = x1");
    }

    void OnEquipmentChanged() noexcept
    {
        SyncRuntimePassives();
    }

    void AccumulateWeaponDirectDamage(
        const Combat::CombatContext& a_context,
        DamageRules::DirectDamageAccumulator& a_accumulator) noexcept
    {
        if (!IsAvailable() ||
            !a_context.playerAttacker ||
            !a_context.weapon ||
            !a_context.weaponDirectCandidate ||
            !a_context.directDamageKnown ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kPaladinMaceMastery) ||
            !TULLBUNG::Classification::
                IsOneHandedBlunt(
                    a_context.weapon)) {

            return;
        }

        a_accumulator.AddPercent(
            DamageRules::DirectChannel::
                kWeapon,
            20.0f);

        logger::info(
            "[S14DIRECT-WEAPON][Paladin] ActionID={} | MaceMastery=true | Bonus=20.00",
            a_context.attackActionID);
    }

    float ResolveHealingBonusPercent(
        const RE::SpellItem* a_spell,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot) noexcept
    {
        (void)a_snapshot;

        if (!IsAvailable() ||
            !a_spell ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kPaladinOath) ||
            !TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    a_spell)) {

            return 0.0f;
        }

        const auto& eq =
            TULLBUNG::PlayerState::
                GetEquipment();

        const bool leftHealing =
            TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    eq.left.spell);

        const bool rightHealing =
            TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    eq.right.spell);

        const bool pair =
            (a_hand == Combat::Hand::kLeft &&
             eq.left.spell == a_spell &&
             leftHealing &&
             TULLBUNG::Classification::
                IsOneHandedBlunt(
                    eq.right.weapon)) ||
            (a_hand == Combat::Hand::kRight &&
             eq.right.spell == a_spell &&
             rightHealing &&
             TULLBUNG::Classification::
                IsOneHandedBlunt(
                    eq.left.weapon));

        if (!pair) {
            return 0.0f;
        }

        logger::info(
            "[S14HEALING][Paladin] Spell={:08X} | Oath=true | Bonus=20.00",
            a_spell->GetFormID());

        return 20.0f;
    }

    void OnPlayerWeaponDirectHit(
        const Combat::CombatContext& a_context,
        RE::HitData& a_data) noexcept
    {
        if (!IsAvailable() ||
            !a_context.playerAttacker ||
            !a_context.target ||
            !a_context.weapon ||
            !a_context.weaponDirectCandidate ||
            !a_context.directDamageKnown ||
            !a_context.firstValidDirectHit ||
            !TULLBUNG::Classification::
                IsOneHandedBlunt(
                    a_context.weapon)) {

            return;
        }

        if (a_context.attackKind ==
                Combat::AttackKind::
                    kNormal) {

            if (g_holyStrikeOvercastIndex != 0) {
                logger::info(
                    "[S14HOLYSTRIKE] Normal mace hit -> Overcast reset x{} -> x1",
                    kHolyStrikeCostMultipliers[
                        g_holyStrikeOvercastIndex]);
            }

            g_holyStrikeOvercastIndex = 0;
            return;
        }

        if (a_context.attackKind !=
                Combat::AttackKind::
                    kPower ||
            !HolyStrikeActive()) {

            return;
        }

        if (!std::isfinite(
                a_data.totalDamage) ||
            a_data.totalDamage <= 0.0f) {

            return;
        }

        const float cost =
            HolyStrikeCurrentCost();

        const auto& snapshot =
            a_context.actionSnapshot;

        if (!snapshot.valid) {
            logger::warn(
                "[S14HOLYSTRIKE] ActionID={} | Result=NO_ACTION_SNAPSHOT | Stage={} | Cost=0 | StageMaintained=true",
                a_context.attackActionID,
                g_holyStrikeOvercastIndex);

            return;
        }

        const float snapshotMagicka =
            snapshot.player.
                magicka.current;

        if (!std::isfinite(
                snapshotMagicka) ||
            snapshotMagicka +
                0.001f <
                cost) {

            logger::info(
                "[S14HOLYSTRIKE] ActionID={} | Result=NO_MAGICKA | SnapshotMP={:.2f} | Stage={} | Required={:.2f} | Cost=0 | StageMaintained=true",
                a_context.attackActionID,
                snapshotMagicka,
                g_holyStrikeOvercastIndex,
                cost);

            return;
        }

        if (!SpendMagicka(cost)) {
            logger::error(
                "[S14HOLYSTRIKE] ActionID={} | Result=COST_COMMIT_FAIL | SnapshotMP={:.2f} | Stage={} | Required={:.2f} | StageMaintained=true",
                a_context.attackActionID,
                snapshotMagicka,
                g_holyStrikeOvercastIndex,
                cost);

            return;
        }

        const float rawHolyDamage =
            a_data.totalDamage *
            HolyStrikeDamagePercent() /
            100.0f;

        const float resolvedHolyDamage =
            ResolveHolyDamageAgainstMagicResistance(
                a_context.target,
                rawHolyDamage);

        auto* player =
            RE::PlayerCharacter::
                GetSingleton();

        const bool applied =
            TULLBUNG::DamageRules::
                ApplyResolvedHealthDamage(
                    player,
                    a_context.target,
                    resolvedHolyDamage,
                    TULLBUNG::DamageRules::
                        FirewallOrigin::
                            kSecondaryDamage);

        const auto stageBefore =
            g_holyStrikeOvercastIndex;

        if (applied &&
            g_holyStrikeOvercastIndex <
                kHolyStrikeCostMultipliers.
                    size() - 1) {

            ++g_holyStrikeOvercastIndex;
        }

        logger::info(
            "[S14HOLYSTRIKE] ActionID={} | Applied={} | StageBefore={} | StageAfter={} | Cost={:.2f} | Direct={:.3f} | HolyRaw={:.3f} | HolyResolved={:.3f} | Master={}",
            a_context.attackActionID,
            applied,
            stageBefore,
            g_holyStrikeOvercastIndex,
            cost,
            a_data.totalDamage,
            rawHolyDamage,
            resolvedHolyDamage,
            MasterActive());
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
                    "[S14PALADIN-PASSIVE] {} = ON",
                    a_name);
            } else if (
                !a_enable &&
                hasSpell) {

                player->RemoveSpell(
                    a_spell);

                logger::info(
                    "[S14PALADIN-PASSIVE] {} = OFF",
                    a_name);
            }
        }
    }

    void SyncRuntimePassives() noexcept
    {
        if (!IsAvailable() ||
            !TULLBUNG::DataCache::
                IsPaladinReady()) {

            return;
        }

        const auto& equipment =
            TULLBUNG::PlayerState::
                GetEquipment();

        const bool rightBlunt =
            TULLBUNG::Classification::
                IsOneHandedBlunt(
                    equipment.right.weapon);

        const bool leftBlunt =
            TULLBUNG::Classification::
                IsOneHandedBlunt(
                    equipment.left.weapon);

        const bool oathBlockEnable =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kPaladinOath) &&
            rightBlunt &&
            equipment.hasShield;

        const bool weightEnable =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kPaladinWeightOfFaith) &&
            (rightBlunt ||
             leftBlunt);

        SetRuntimeAbility(
            TULLBUNG::DataCache::
                GetPriestFamilySpell(
                    TULLBUNG::DataCache::
                        PriestFamilySpellID::
                            kPaladinOathBlock),
            oathBlockEnable,
            "OathBlock");

        SetRuntimeAbility(
            TULLBUNG::DataCache::
                GetPriestFamilySpell(
                    TULLBUNG::DataCache::
                        PriestFamilySpellID::
                            kPaladinWeightOfFaith),
            weightEnable,
            "WeightOfFaith");
    }

    void RemoveRuntimePassives() noexcept
    {
        if (!TULLBUNG::DataCache::
                IsPaladinReady()) {

            return;
        }

        SetRuntimeAbility(
            TULLBUNG::DataCache::
                GetPriestFamilySpell(
                    TULLBUNG::DataCache::
                        PriestFamilySpellID::
                            kPaladinOathBlock),
            false,
            "OathBlock");

        SetRuntimeAbility(
            TULLBUNG::DataCache::
                GetPriestFamilySpell(
                    TULLBUNG::DataCache::
                        PriestFamilySpellID::
                            kPaladinWeightOfFaith),
            false,
            "WeightOfFaith");
    }
    std::uint8_t
        GetHolyStrikeOvercastIndex() noexcept
    {
        return g_holyStrikeOvercastIndex;
    }


    void RestoreHolyStrikeOvercastIndex(
        std::uint8_t a_index) noexcept
    {
        const auto maxIndex =
            static_cast<std::uint8_t>(
                kHolyStrikeCostMultipliers.
                    size() - 1);

        g_holyStrikeOvercastIndex =
            (std::min)(
                a_index,
                maxIndex);

        logger::info(
            "[S17PERSIST] HolyStrikeOvercast restored | Stage={}",
            g_holyStrikeOvercastIndex);
    }
}