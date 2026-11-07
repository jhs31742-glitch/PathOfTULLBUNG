#include "PCH.h"
#include "Priest.h"

#include "BuildState.h"
#include "Classification.h"
#include "DataCache.h"
#include "PlayerState.h"

namespace
{
    bool g_available{};
}

namespace TULLBUNG::Classes::Priest
{
    bool Initialize() noexcept
    {
        g_available =
            TULLBUNG::DataCache::IsReady();

        logger::info(
            "[Stage14] Priest runtime = {}",
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

        logger::info(
            "[Stage14] Priest transient state reset");
    }

    void OnEquipmentChanged() noexcept
    {
        if (!g_available) {
            return;
        }

        const auto& equipment =
            TULLBUNG::PlayerState::
                GetEquipment();

        const bool leftHealing =
            TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    equipment.left.spell);

        const bool rightHealing =
            TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    equipment.right.spell);

        logger::info(
            "[S14PRIEST-STATE] Stance={} | LeftHeal={} | RightHeal={}",
            leftHealing,
            leftHealing,
            rightHealing);

        SyncRuntimePassives();
    }

    void AccumulateWeaponDirectDamage(
        const Combat::CombatContext& a_context,
        DamageRules::DirectDamageAccumulator& a_accumulator) noexcept
    {
        if (!IsAvailable() ||
            !a_context.playerAttacker ||
            !a_context.target ||
            !a_context.weapon ||
            !a_context.weaponDirectCandidate ||
            !a_context.directDamageKnown) {

            return;
        }

        const auto& equipment =
            TULLBUNG::PlayerState::
                GetEquipment();

        const auto& snap =
            a_context.actionSnapshot;

        const bool rightWeaponDirect =
            equipment.right.weapon ==
            a_context.weapon;

        const bool stance =
            snap.valid &&
            snap.extended.priestStanceKnown &&
            snap.extended.priestStance;

        const bool leftHealingCasting =
            snap.valid &&
            snap.extended.castingStateKnown &&
            snap.extended.leftHealingSpellCasting;

        float bonus{};
        bool crusade{};
        bool fervor{};
        bool highHealth{};

        if (rightWeaponDirect &&
            stance &&
            BuildState::IsNodeActive(
                BuildState::NodeID::
                    kPriestCrusade)) {

            bonus += 20.0f;
            crusade = true;
        }

        if (rightWeaponDirect &&
            stance &&
            leftHealingCasting &&
            BuildState::IsNodeActive(
                BuildState::NodeID::
                    kPriestFervor)) {

            bonus += 15.0f;
            fervor = true;

            highHealth =
                snap.player.health.percent >=
                80.0f;

            if (highHealth) {
                bonus += 15.0f;
            }
        }

        if (bonus <= 0.0f) {
            return;
        }

        a_accumulator.AddPercent(
            DamageRules::DirectChannel::
                kWeapon,
            bonus);

        logger::info(
            "[S14DIRECT-WEAPON][Priest] ActionID={} | Crusade={} | Fervor={} | HP80={} | Bonus={:.2f}",
            a_context.attackActionID,
            crusade,
            fervor,
            highHealth,
            bonus);
    }

    void AccumulateSpellDirectDamage(
        const RE::SpellItem* a_spell,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot,
        DamageRules::DirectDamageAccumulator& a_accumulator) noexcept
    {
        if (!IsAvailable() ||
            !a_spell ||
            a_hand != Combat::Hand::kRight ||
            !a_snapshot.valid ||
            !a_snapshot.extended.priestStanceKnown ||
            !a_snapshot.extended.priestStance ||
            !TULLBUNG::Classification::
                IsAttackSpell(a_spell) ||
            !TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kPriestCrusade)) {

            return;
        }

        a_accumulator.AddPercent(
            DamageRules::DirectChannel::
                kSpell,
            20.0f);

        logger::info(
            "[S14DIRECT-SPELL][Priest] Spell={:08X} | Crusade=true | Bonus=20.00",
            a_spell->GetFormID());
    }

    float ResolveHealingBonusPercent(
        const RE::SpellItem* a_spell,
        Combat::Hand a_hand,
        const Snapshot::ActionSnapshot& a_snapshot) noexcept
    {
        if (!IsAvailable() ||
            !a_spell ||
            !TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    a_spell)) {

            return 0.0f;
        }

        const auto& eq =
            TULLBUNG::PlayerState::
                GetEquipment();

        const bool handMatch =
            (a_hand == Combat::Hand::kRight &&
             eq.right.spell == a_spell) ||
            (a_hand == Combat::Hand::kLeft &&
             eq.left.spell == a_spell);

        if (!handMatch) {
            return 0.0f;
        }

        const bool stance =
            a_snapshot.valid &&
            a_snapshot.extended.
                priestStanceKnown &&
            a_snapshot.extended.
                priestStance;

        const bool leftHealing =
            TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    eq.left.spell);

        const bool rightHealing =
            TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    eq.right.spell);

        const bool healingArts =
            stance &&
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kPriestHealingArts);

        const bool focused =
            leftHealing &&
            rightHealing &&
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kPriestFocusedHealing);

        float bonus{};

        if (healingArts) {
            bonus += 25.0f;
        }

        if (focused) {
            bonus += 25.0f;
        }

        if (bonus > 0.0f) {
            logger::debug(
                "[S14HEALING][Priest] Spell={:08X} | HealingArts={} | Focused={} | Bonus={:.2f}",
                a_spell->GetFormID(),
                healingArts,
                focused,
                bonus);
        }

        return bonus;
    }

    namespace
    {
        void SetEndlessPrayerAbility(
            bool a_enable) noexcept
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            auto* spell =
                TULLBUNG::DataCache::
                    GetPriestEndlessPrayerSpell();

            if (!player ||
                !spell) {

                if (!spell) {
                    logger::error(
                        "[S14PRIEST-MASTER] EndlessPrayer ability = MISSING");
                }

                return;
            }

            const bool hasSpell =
                player->HasSpell(
                    spell);

            if (a_enable &&
                !hasSpell) {

                player->AddSpell(
                    spell);

                logger::info(
                    "[S14PRIEST-MASTER] EndlessPrayer = ON | MagickaRateMult=+50 | StaminaRateMult=+50");
            } else if (
                !a_enable &&
                hasSpell) {

                player->RemoveSpell(
                    spell);

                logger::info(
                    "[S14PRIEST-MASTER] EndlessPrayer = OFF");
            }
        }
    }

    void SyncRuntimePassives() noexcept
    {
        if (!IsAvailable()) {
            return;
        }

        const auto& equipment =
            TULLBUNG::PlayerState::
                GetEquipment();

        const bool priestStance =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kPriestStance) &&
            TULLBUNG::Classification::
                IsHealthRestorationSpell(
                    equipment.left.spell);

        const bool masterActive =
            TULLBUNG::BuildState::
                IsNodeActive(
                    TULLBUNG::BuildState::
                        NodeID::
                            kPriestMaster);

        const bool enable =
            TULLBUNG::DataCache::
                IsPriestReady() &&
            priestStance &&
            masterActive;

        SetEndlessPrayerAbility(
            enable);
    }

    void RemoveRuntimePassives() noexcept
    {
        if (!TULLBUNG::DataCache::
                IsPriestReady()) {

            return;
        }

        SetEndlessPrayerAbility(
            false);
    }
}