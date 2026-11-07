#include "PCH.h"
#include "DataCache.h"

#include <array>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::string_view kPluginName =
        "[TULL] Path of TULLBUNG.esp";

    TULLBUNG::DataCache::Forms g_forms{};

    inline constexpr std::size_t kHunterCatalogCount = 5;
    inline constexpr std::size_t kPriestFamilyCatalogCount = 3;
    inline constexpr std::size_t kMageFamilyCatalogCount = 3;

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kWarriorPerkCount>
        kWarriorPerkEDIDs{{
            "TULL_Warrior_CombatBreath_PERK",
            "TULL_Warrior_WarriorVigor_PERK",
            "TULL_Warrior_IronWall_PERK",
            "TULL_Warrior_Adrenaline_PERK",
            "TULL_Warrior_BattleSprint_PERK",
            "TULL_Warrior_WarMachine_PERK",

            "TULL_Berserker_TwoHandMastery_PERK",
            "TULL_Berserker_BloodStrike_PERK",
            "TULL_Berserker_BloodSmash_PERK",
            "TULL_Berserker_Struggle_PERK",
            "TULL_Berserker_SurvivalDesire_PERK",
            "TULL_Berserker_BoilingBlood_PERK",

            "TULL_Guardian_ShieldMastery_PERK",
            "TULL_Guardian_BestDefense_PERK",
            "TULL_Guardian_ShieldBash_PERK",
            "TULL_Guardian_HammerAnvil_PERK",
            "TULL_Guardian_IronBlood_PERK"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kWarriorEffectCount>
        kWarriorEffectEDIDs{{
            "TULL_Warrior_CombatBreath_MGEF",
            "TULL_Warrior_WarriorVigor_MGEF",
            "TULL_Warrior_Adrenaline_MGEF",
            "TULL_Berserker_TwoHandMastery_MGEF",
            "TULL_Berserker_Struggle_MGEF",
            "TULL_Berserker_SurvivalDesire_MGEF",
            "TULL_Guardian_HammerAnvil_MGEF"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kWarriorSpellCount>
        kWarriorSpellEDIDs{{
            "TULL_Warrior_CombatBreath_Ability",
            "TULL_Warrior_WarriorVigor_Ability",
            "TULL_Warrior_Adrenaline_Ability",
            "TULL_Berserker_TwoHandMastery_Ability",

            "TULL_Berserker_Struggle_Ability_1",
            "TULL_Berserker_Struggle_Ability_2",
            "TULL_Berserker_Struggle_Ability_3",
            "TULL_Berserker_Struggle_Ability_4",
            "TULL_Berserker_Struggle_Ability_5",

            "TULL_Berserker_SurvivalDesire_Ability",
            "TULL_Guardian_HammerAnvil_Spell"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kHunterPerkCount>
        kHunterPerkEDIDs{{
            "TULL_Hunter_LightArmorMastery_PERK",
            "TULL_Hunter_PoisonTracking_PERK",
            "TULL_Hunter_TacticalRetreat_PERK",
            "TULL_Hunter_Vigilance_PERK",
            "TULL_Hunter_Rivalry_PERK",

            "TULL_Assassin_DaggerMastery_PERK",
            "TULL_Assassin_BloodPoisoning_PERK",
            "TULL_Assassin_BloodInfusion_PERK",
            "TULL_Assassin_QuietProcessing_PERK",
            "TULL_Assassin_BloodRefinement_PERK",
            "TULL_Assassin_WoundGouge_PERK",

            "TULL_Marksman_OpenBattle_PERK",
            "TULL_Marksman_QuickHands_PERK",
            "TULL_Marksman_BloodDelivery_PERK",
            "TULL_Marksman_EmberSpread_PERK",
            "TULL_Marksman_Composure_PERK",
            "TULL_Marksman_Execution_PERK"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kHunterEffectCount>
        kHunterEffectEDIDs{{
            "TULL_Hunter_PoisonTracking_A_MGEF",
            "TULL_Hunter_PoisonTracking_B_MGEF",
            "TULL_Hunter_TacticalRetreat_Ethereal_MGEF",
            "TULL_Hunter_TacticalRetreat_Invisibility_MGEF",
            "TULL_Assassin_DaggerMastery_DiseaseResist_MGEF",

            "TULL_Assassin_BloodPoison_Base_S1_MGEF",
            "TULL_Assassin_BloodPoison_Base_S2_MGEF",
            "TULL_Assassin_BloodPoison_Base_S3A_MGEF",
            "TULL_Assassin_BloodPoison_Base_S3B_MGEF",

            "TULL_Assassin_BloodPoison_Refined_S1_MGEF",
            "TULL_Assassin_BloodPoison_Refined_S2_MGEF",
            "TULL_Assassin_BloodPoison_Refined_S3A_MGEF",
            "TULL_Assassin_BloodPoison_Refined_S3B_MGEF",

            "TULL_Assassin_BloodPoison_Slow_MGEF",
            "TULL_Assassin_BloodPoison_RegenBlock_MGEF",
            "TULL_Marksman_QuickHands_MGEF",

            "TULL_Marksman_EmberBurn_S1_MGEF",
            "TULL_Marksman_EmberBurn_S2_MGEF",
            "TULL_Marksman_EmberBurn_S3A_MGEF",
            "TULL_Marksman_EmberBurn_S3B_MGEF"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kHunterSpellCount>
        kHunterSpellEDIDs{{
            "TULL_Hunter_PoisonTracking_A_Spell",
            "TULL_Hunter_PoisonTracking_B_Spell",
            "TULL_Hunter_TacticalRetreat_Spell",
            "TULL_Assassin_DaggerMastery_Ability",

            "TULL_Assassin_BloodPoison_Base_S1_Spell",
            "TULL_Assassin_BloodPoison_Base_S2_Spell",
            "TULL_Assassin_BloodPoison_Base_S3A_Spell",
            "TULL_Assassin_BloodPoison_Base_S3B_Spell",

            "TULL_Assassin_BloodPoison_Refined_S1_Spell",
            "TULL_Assassin_BloodPoison_Refined_S2_Spell",
            "TULL_Assassin_BloodPoison_Refined_S3A_Spell",
            "TULL_Assassin_BloodPoison_Refined_S3B_Spell",

            "TULL_Marksman_QuickHands_Ability",

            "TULL_Marksman_EmberBurn_S1_Spell",
            "TULL_Marksman_EmberBurn_S2_Spell",
            "TULL_Marksman_EmberBurn_S3A_Spell",
            "TULL_Marksman_EmberBurn_S3B_Spell"
        }};


    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kPriestFamilyPerkCount>
        kPriestFamilyPerkEDIDs{{
            "TULL_Priest_HealingArts_PERK",
            "TULL_Priest_Crusade_PERK",
            "TULL_Priest_Fervor_PERK",
            "TULL_Priest_FocusedHealing_PERK",
            "TULL_Priest_EndlessPrayer_PERK",

            "TULL_Paladin_MaceMastery_PERK",
            "TULL_Paladin_Oath_PERK",
            "TULL_Paladin_HolyStrike_PERK",
            "TULL_Paladin_WeightOfFaith_PERK",
            "TULL_Paladin_JudgmentMace_PERK",

            "TULL_Inquisitor_MonsterStudy_PERK",
            "TULL_Inquisitor_ArtifactStudy_PERK",
            "TULL_Inquisitor_MonsterDefense_PERK",
            "TULL_Inquisitor_Smite_PERK",
            "TULL_Inquisitor_GrandInquisitor_PERK"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kPriestFamilyEffectCount>
        kPriestFamilyEffectEDIDs{{
            "TULL_Priest_EndlessPrayer_MagickaRegen_MGEF",
            "TULL_Priest_EndlessPrayer_StaminaRegen_MGEF",

            "TULL_Paladin_OathBlock_MGEF",
            "TULL_Paladin_WeightOfFaith_Armor_MGEF",
            "TULL_Paladin_WeightOfFaith_MagicResist_MGEF",
            "TULL_Paladin_WeightOfFaith_DiseaseResist_MGEF",

            "TULL_Inquisitor_ArtifactStudy_Health_MGEF",
            "TULL_Inquisitor_ArtifactStudy_Magicka_MGEF",
            "TULL_Inquisitor_ArtifactStudy_Stamina_MGEF",
            "TULL_Inquisitor_Smite_Trigger_MGEF",
            "TULL_Inquisitor_Smite_VFX_MGEF"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kPriestFamilySpellCount>
        kPriestFamilySpellEDIDs{{
            "TULL_Priest_EndlessPrayer_Ability",

            "TULL_Paladin_OathBlock_Ability",
            "TULL_Paladin_WeightOfFaith_Ability",

            "TULL_Inquisitor_ArtifactStudy_Ability_1",
            "TULL_Inquisitor_ArtifactStudy_Ability_2",
            "TULL_Inquisitor_ArtifactStudy_Ability_3",
            "TULL_Inquisitor_ArtifactStudy_Ability_4",
            "TULL_Inquisitor_ArtifactStudy_Ability_5",
            "TULL_Inquisitor_ArtifactStudy_Ability_6",
            "TULL_Inquisitor_ArtifactStudy_Ability_7",
            "TULL_Inquisitor_ArtifactStudy_Ability_8",

            "TULL_Inquisitor_Smite_Power",
            "TULL_Inquisitor_Smite_VFX_Spell"
        }};
    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kMageFamilyPerkCount>
        kMageFamilyPerkEDIDs{{
            "TULL_Mage_Barrier_PERK",
            "TULL_Mage_ArcaneStudies_PERK",
            "TULL_Mage_MagickaCollapse_PERK",
            "TULL_Mage_Archmage_PERK",

            "TULL_VoidMage_VoidStrike_PERK",
            "TULL_VoidMage_VoidArmament_PERK",
            "TULL_VoidMage_VoidErosion_PERK",
            "TULL_VoidMage_VoidDevour_PERK",
            "TULL_VoidMage_VoidVessel_PERK",

            "TULL_SiegeMage_Ballistics_PERK",
            "TULL_SiegeMage_GravityShackle_PERK",
            "TULL_SiegeMage_FocusedBarrage_PERK",
            "TULL_SiegeMage_MagickaReturn_PERK",
            "TULL_SiegeMage_ArcaneFortification_PERK"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kMageFamilyEffectCount>
        kMageFamilyEffectEDIDs{{
            "TULL_Mage_Barrier_Armor_MGEF",
            "TULL_Mage_ArcaneStudies_Magicka_MGEF",
            "TULL_Mage_Archmage_MagickaRegen_MGEF",
            "TULL_Mage_MagickaCollapse_Trigger_MGEF",

            "TULL_VoidMage_VoidErosion_MagickaRegen_MGEF",
            "TULL_VoidMage_VoidDevour_Trigger_MGEF",
            "TULL_VoidMage_VoidVessel_MagickaRegen_MGEF",

            "TULL_SiegeMage_SiegeRune_Slow_MGEF"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kMageFamilySpellCount>
        kMageFamilySpellEDIDs{{
            "TULL_Mage_Barrier_Ability",

            "TULL_Mage_ArcaneStudies_Ability_1",
            "TULL_Mage_ArcaneStudies_Ability_2",
            "TULL_Mage_ArcaneStudies_Ability_3",
            "TULL_Mage_ArcaneStudies_Ability_4",
            "TULL_Mage_ArcaneStudies_Ability_5",
            "TULL_Mage_ArcaneStudies_Ability_6",
            "TULL_Mage_ArcaneStudies_Ability_7",
            "TULL_Mage_ArcaneStudies_Ability_8",
            "TULL_Mage_ArcaneStudies_Ability_9",
            "TULL_Mage_ArcaneStudies_Ability_10",

            "TULL_Mage_Archmage_Ability_1",
            "TULL_Mage_Archmage_Ability_2",
            "TULL_Mage_Archmage_Ability_3",
            "TULL_Mage_Archmage_Ability_4",
            "TULL_Mage_Archmage_Ability_5",
            "TULL_Mage_Archmage_Ability_6",
            "TULL_Mage_Archmage_Ability_7",
            "TULL_Mage_Archmage_Ability_8",
            "TULL_Mage_Archmage_Ability_9",
            "TULL_Mage_Archmage_Ability_10",

            "TULL_Mage_MagickaCollapse_Power",

            "TULL_VoidMage_VoidErosion_Spell",
            "TULL_VoidMage_VoidDevour_Power",
            "TULL_VoidMage_VoidVessel_Ability",

            "TULL_SiegeMage_SiegeRune_Slow50_Spell",
            "TULL_SiegeMage_SiegeRune_Slow70_Spell",
            "TULL_VoidMage_VoidDevour_Target_VFX_Spell"
        }};

    constexpr std::array<
        std::string_view,
        TULLBUNG::DataCache::kMageFamilyHazardCount>
        kMageFamilyHazardEDIDs{{
            "TULL_SiegeMage_SiegeRune_Base_HAZD",
            "TULL_SiegeMage_SiegeRune_Gravity_HAZD",
            "TULL_SiegeMage_SiegeRune_GameplaySlow_Base_HAZD",
            "TULL_SiegeMage_SiegeRune_GameplaySlow_Gravity_HAZD"
        }};

    template <class T>
    T* Lookup(
        std::string_view a_editorID)
    {
        return
            RE::TESForm::
                LookupByEditorID<T>(
                    a_editorID);
    }

    void LogResult(
        std::string_view a_name,
        const RE::TESForm* a_form)
    {
        if (a_form) {
            logger::info(
                "[DataCache] {} = OK",
                a_name);
        } else {
            logger::error(
                "[DataCache] {} = MISSING",
                a_name);
        }
    }

    template <
        class T,
        std::size_t N>
    std::size_t LoadByEditorID(
        std::array<T*, N>& a_out,
        const std::array<
            std::string_view,
            N>& a_editorIDs,
        std::string_view a_featureName,
        std::string_view a_groupName)
    {
        std::size_t found = 0;

        for (std::size_t i = 0;
             i < N;
             ++i) {

            a_out[i] =
                Lookup<T>(
                    a_editorIDs[i]);

            if (a_out[i]) {
                ++found;
            } else {
                logger::error(
                    "[DataCache][{}] {} = MISSING | EditorID={}",
                    a_featureName,
                    a_groupName,
                    a_editorIDs[i]);
            }
        }

        return found;
    }

    void LoadWarriorForms()
    {
        auto& warrior =
            g_forms.warrior;

        std::size_t found = 0;

        found +=
            LoadByEditorID(
                warrior.perks,
                kWarriorPerkEDIDs,
                "Warrior",
                "PERK");

        found +=
            LoadByEditorID(
                warrior.effects,
                kWarriorEffectEDIDs,
                "Warrior",
                "MGEF");

        found +=
            LoadByEditorID(
                warrior.spells,
                kWarriorSpellEDIDs,
                "Warrior",
                "SPEL");

        warrior.warriorPerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_WarriorPerks");

        warrior.berserkerPerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_BerserkerPerks");

        warrior.guardianPerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_GuardianPerks");

        warrior.runtimeSpells =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_WarriorRuntimeSpells");

        warrior.runtimeEffects =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_WarriorRuntimeEffects");

        const std::array<
            std::pair<
                std::string_view,
                const RE::TESForm*>,
            5>
            catalogs{{
                {
                    "TULLBUNG_WarriorPerks",
                    warrior.warriorPerks
                },
                {
                    "TULLBUNG_BerserkerPerks",
                    warrior.berserkerPerks
                },
                {
                    "TULLBUNG_GuardianPerks",
                    warrior.guardianPerks
                },
                {
                    "TULLBUNG_WarriorRuntimeSpells",
                    warrior.runtimeSpells
                },
                {
                    "TULLBUNG_WarriorRuntimeEffects",
                    warrior.runtimeEffects
                }
            }};

        for (const auto& [name, form] :
             catalogs) {

            if (form) {
                ++found;
            } else {
                logger::error(
                    "[DataCache][Warrior] FLST = MISSING | EditorID={}",
                    name);
            }
        }

        constexpr std::size_t
            kExpectedWarriorForms =
                kWarriorPerkEDIDs.size() +
                kWarriorEffectEDIDs.size() +
                kWarriorSpellEDIDs.size() +
                catalogs.size();

        warrior.ready =
            found ==
            kExpectedWarriorForms;

        if (warrior.ready) {
            logger::info(
                "[DataCache][Warrior] Ready | Forms={}/{}",
                found,
                kExpectedWarriorForms);
        } else {
            logger::error(
                "[DataCache][Warrior] Incomplete | Forms={}/{}",
                found,
                kExpectedWarriorForms);
        }
    }

    void LoadHunterForms()
    {
        auto& hunter =
            g_forms.hunter;

        std::size_t found = 0;

        found +=
            LoadByEditorID(
                hunter.perks,
                kHunterPerkEDIDs,
                "Hunter",
                "PERK");

        found +=
            LoadByEditorID(
                hunter.effects,
                kHunterEffectEDIDs,
                "Hunter",
                "MGEF");

        found +=
            LoadByEditorID(
                hunter.spells,
                kHunterSpellEDIDs,
                "Hunter",
                "SPEL");

        hunter.hunterPerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_HunterPerks");

        hunter.assassinPerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_AssassinPerks");

        hunter.marksmanPerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_MarksmanPerks");

        hunter.runtimeSpells =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_HunterRuntimeSpells");

        hunter.runtimeEffects =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_HunterRuntimeEffects");

        const std::array<
            std::pair<
                std::string_view,
                const RE::TESForm*>,
            kHunterCatalogCount>
            catalogs{{
                {
                    "TULLBUNG_HunterPerks",
                    hunter.hunterPerks
                },
                {
                    "TULLBUNG_AssassinPerks",
                    hunter.assassinPerks
                },
                {
                    "TULLBUNG_MarksmanPerks",
                    hunter.marksmanPerks
                },
                {
                    "TULLBUNG_HunterRuntimeSpells",
                    hunter.runtimeSpells
                },
                {
                    "TULLBUNG_HunterRuntimeEffects",
                    hunter.runtimeEffects
                }
            }};

        for (const auto& [name, form] :
             catalogs) {

            if (form) {
                ++found;
            } else {
                logger::error(
                    "[DataCache][Hunter] FLST = MISSING | EditorID={}",
                    name);
            }
        }

        constexpr std::size_t
            kExpectedHunterForms =
                kHunterPerkEDIDs.size() +
                kHunterEffectEDIDs.size() +
                kHunterSpellEDIDs.size() +
                kHunterCatalogCount;

        static_assert(
            kExpectedHunterForms == 59,
            "Stage13 Hunter DataCache contract changed");

        hunter.ready =
            found ==
            kExpectedHunterForms;

        if (hunter.ready) {
            logger::info(
                "[DataCache][Hunter] Ready | Forms={}/{}",
                found,
                kExpectedHunterForms);
        } else {
            logger::error(
                "[DataCache][Hunter] Incomplete | Forms={}/{}",
                found,
                kExpectedHunterForms);
        }
    }


    void LoadMageFamilyForms()
    {
        auto& family =
            g_forms.mageFamily;

        std::size_t found = 0;

        found +=
            LoadByEditorID(
                family.perks,
                kMageFamilyPerkEDIDs,
                "MageFamily",
                "PERK");

        found +=
            LoadByEditorID(
                family.effects,
                kMageFamilyEffectEDIDs,
                "MageFamily",
                "MGEF");

        found +=
            LoadByEditorID(
                family.spells,
                kMageFamilySpellEDIDs,
                "MageFamily",
                "SPEL");

        found +=
            LoadByEditorID(
                family.hazards,
                kMageFamilyHazardEDIDs,
                "MageFamily",
                "HAZD");

        family.magePerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_MagePerks");

        family.voidMagePerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_VoidMagePerks");

        family.siegeRuneVFXActivator =
            Lookup<RE::TESObjectACTI>(
                "TULL_SiegeMage_SiegeRune_VFX_ACTI");

        if (family.siegeRuneVFXActivator) {
            ++found;
        } else {
            logger::warn(
                "[DataCache][MageFamily] ACTI = MISSING | EditorID={}",
                "TULL_SiegeMage_SiegeRune_VFX_ACTI");
        }


        family.siegeMagePerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_SiegeMagePerks");

        const std::array<
            std::pair<
                std::string_view,
                const RE::TESForm*>,
            kMageFamilyCatalogCount>
            catalogs{{
                {
                    "TULLBUNG_MagePerks",
                    family.magePerks
                },
                {
                    "TULLBUNG_VoidMagePerks",
                    family.voidMagePerks
                },
                {
                    "TULLBUNG_SiegeMagePerks",
                    family.siegeMagePerks
                }
            }};

        for (const auto& [name, form] :
             catalogs) {

            if (form) {
                ++found;
            } else {
                logger::error(
                    "[DataCache][MageFamily] FLST = MISSING | EditorID={}",
                    name);
            }
        }

        constexpr std::size_t
            kExpectedMageFamilyForms =
                kMageFamilyPerkEDIDs.size() +
                kMageFamilyEffectEDIDs.size() +
                kMageFamilySpellEDIDs.size() +
                kMageFamilyHazardEDIDs.size() +
                kMageFamilyCatalogCount + 1;

        static_assert(
            kExpectedMageFamilyForms == 58,
            "Stage15 MageFamily DataCache contract changed");

        family.ready =
            found ==
            kExpectedMageFamilyForms;

        if (family.ready) {
            logger::info(
                "[DataCache][MageFamily] Ready | Forms={}/{}",
                found,
                kExpectedMageFamilyForms);
        } else {
            logger::error(
                "[DataCache][MageFamily] Incomplete | Forms={}/{}",
                found,
                kExpectedMageFamilyForms);
        }
    }

    void LoadPriestFamilyForms()
    {
        auto& family =
            g_forms.priestFamily;

        std::size_t found = 0;

        found +=
            LoadByEditorID(
                family.perks,
                kPriestFamilyPerkEDIDs,
                "PriestFamily",
                "PERK");

        found +=
            LoadByEditorID(
                family.effects,
                kPriestFamilyEffectEDIDs,
                "PriestFamily",
                "MGEF");

        found +=
            LoadByEditorID(
                family.spells,
                kPriestFamilySpellEDIDs,
                "PriestFamily",
                "SPEL");

        family.priestPerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_PriestPerks");

        family.paladinPerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_PaladinPerks");

        family.inquisitorPerks =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_InquisitorPerks");

        const std::array<
            std::pair<
                std::string_view,
                const RE::TESForm*>,
            kPriestFamilyCatalogCount>
            catalogs{{
                {
                    "TULLBUNG_PriestPerks",
                    family.priestPerks
                },
                {
                    "TULLBUNG_PaladinPerks",
                    family.paladinPerks
                },
                {
                    "TULLBUNG_InquisitorPerks",
                    family.inquisitorPerks
                }
            }};

        for (const auto& [name, form] :
             catalogs) {

            if (form) {
                ++found;
            } else {
                logger::error(
                    "[DataCache][PriestFamily] FLST = MISSING | EditorID={}",
                    name);
            }
        }

        constexpr std::size_t
            kExpectedPriestFamilyForms =
                kPriestFamilyPerkEDIDs.size() +
                kPriestFamilyEffectEDIDs.size() +
                kPriestFamilySpellEDIDs.size() +
                kPriestFamilyCatalogCount;

        static_assert(
            kExpectedPriestFamilyForms == 42,
            "Stage14 PriestFamily DataCache contract changed");

        family.ready =
            found ==
            kExpectedPriestFamilyForms;

        if (family.ready) {
            logger::info(
                "[DataCache][PriestFamily] Ready | Forms={}/{}",
                found,
                kExpectedPriestFamilyForms);
        } else {
            logger::error(
                "[DataCache][PriestFamily] Incomplete | Forms={}/{}",
                found,
                kExpectedPriestFamilyForms);
        }
    }
    template <
        class Enum,
        class T,
        std::size_t N>
    T* GetIndexed(
        const std::array<T*, N>& a_array,
        Enum a_id) noexcept
    {
        const auto index =
            static_cast<std::size_t>(
                a_id);

        if (index >= N) {
            return nullptr;
        }

        return a_array[index];
    }
}

namespace TULLBUNG::DataCache
{
    bool Initialize()
    {
        g_forms = {};

        auto* dataHandler =
            RE::TESDataHandler::
                GetSingleton();

        if (!dataHandler) {
            logger::error(
                "[DataCache] TESDataHandler = MISSING");

            return false;
        }

        const auto* plugin =
            dataHandler->LookupModByName(
                kPluginName);

        if (!plugin) {
            logger::error(
                "[DataCache] Plugin = MISSING ({})",
                kPluginName);

            return false;
        }

        logger::info(
            "[DataCache] Plugin = OK");

        g_forms.daedricArtifacts =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_DaedricArtifacts");

        g_forms.monsterKeyword =
            Lookup<RE::BGSKeyword>(
                "TULLBUNG_Monster");

        g_forms.monsterKeywords =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_MonsterKeywords");

        g_forms.monsterRaceWhitelist =
            Lookup<RE::BGSListForm>(
                "TULLBUNG_MonsterRaceWhitelist");

        g_forms.weapTypeDagger =
            Lookup<RE::BGSKeyword>(
                "WeapTypeDagger");

        LogResult(
            "DaedricArtifacts",
            g_forms.daedricArtifacts);

        LogResult(
            "MonsterKeyword",
            g_forms.monsterKeyword);

        LogResult(
            "MonsterKeywords",
            g_forms.monsterKeywords);

        LogResult(
            "MonsterRaceWhitelist",
            g_forms.monsterRaceWhitelist);

        LogResult(
            "WeapTypeDagger",
            g_forms.weapTypeDagger);

        g_forms.ready =
            g_forms.daedricArtifacts &&
            g_forms.monsterKeyword &&
            g_forms.monsterKeywords &&
            g_forms.monsterRaceWhitelist &&
            g_forms.weapTypeDagger;

        if (g_forms.ready) {
            logger::info(
                "[DataCache] Core Ready");
        } else {
            logger::error(
                "[DataCache] Core initialization incomplete");
        }

        // Class data is feature-local. Missing class scaffold forms
        // do not invalidate Stage 1-11 core readiness.
        LoadWarriorForms();
        LoadHunterForms();
        LoadPriestFamilyForms();
        LoadMageFamilyForms();

        return g_forms.ready;
    }

    bool IsReady() noexcept
    {
        return g_forms.ready;
    }

    bool IsWarriorReady() noexcept
    {
        return
            g_forms.ready &&
            g_forms.warrior.ready;
    }

    bool IsHunterReady() noexcept
    {
        return
            g_forms.ready &&
            g_forms.hunter.ready;
    }

    bool IsMageReady() noexcept
    {
        return
            g_forms.ready &&
            g_forms.mageFamily.ready;
    }

    bool IsVoidMageReady() noexcept
    {
        return
            g_forms.ready &&
            g_forms.mageFamily.ready;
    }

    bool IsSiegeMageReady() noexcept
    {
        return
            g_forms.ready &&
            g_forms.mageFamily.ready;
    }

    RE::BGSPerk*
        GetMageFamilyPerk(
            MageFamilyPerkID a_id) noexcept
    {
        return GetIndexed(
            g_forms.mageFamily.perks,
            a_id);
    }

    RE::EffectSetting*
        GetMageFamilyEffect(
            MageFamilyEffectID a_id) noexcept
    {
        return GetIndexed(
            g_forms.mageFamily.effects,
            a_id);
    }

    RE::SpellItem*
        GetMageFamilySpell(
            MageFamilySpellID a_id) noexcept
    {
        return GetIndexed(
            g_forms.mageFamily.spells,
            a_id);
    }

    RE::TESForm*
        GetMageFamilyHazard(
            MageFamilyHazardID a_id) noexcept
    {
        return GetIndexed(
            g_forms.mageFamily.hazards,
            a_id);
    }

    RE::TESObjectACTI*
        GetSiegeRuneVFXActivator() noexcept
    {
        return
            g_forms.mageFamily.
                siegeRuneVFXActivator;
    }


    RE::BGSPerk* GetWarriorPerk(
        WarriorPerkID a_id) noexcept
    {
        return GetIndexed(
            g_forms.warrior.perks,
            a_id);
    }

    RE::EffectSetting* GetWarriorEffect(
        WarriorEffectID a_id) noexcept
    {
        return GetIndexed(
            g_forms.warrior.effects,
            a_id);
    }

    RE::SpellItem* GetWarriorSpell(
        WarriorSpellID a_id) noexcept
    {
        return GetIndexed(
            g_forms.warrior.spells,
            a_id);
    }

    RE::BGSPerk* GetHunterPerk(
        HunterPerkID a_id) noexcept
    {
        return GetIndexed(
            g_forms.hunter.perks,
            a_id);
    }

    RE::EffectSetting* GetHunterEffect(
        HunterEffectID a_id) noexcept
    {
        return GetIndexed(
            g_forms.hunter.effects,
            a_id);
    }

    RE::SpellItem* GetHunterSpell(
        HunterSpellID a_id) noexcept
    {
        return GetIndexed(
            g_forms.hunter.spells,
            a_id);
    }

    const Forms& Get() noexcept
    {
        return g_forms;
    }
}

namespace TULLBUNG::DataCache
{
    bool IsPriestReady() noexcept
    {
        return
            g_forms.ready &&
            g_forms.priestFamily.ready;
    }

    bool IsPaladinReady() noexcept
    {
        return
            g_forms.ready &&
            g_forms.priestFamily.ready;
    }

    bool IsInquisitorReady() noexcept
    {
        return
            g_forms.ready &&
            g_forms.priestFamily.ready;
    }

    RE::BGSPerk*
        GetPriestFamilyPerk(
            PriestFamilyPerkID a_id) noexcept
    {
        return GetIndexed(
            g_forms.priestFamily.perks,
            a_id);
    }

    RE::EffectSetting*
        GetPriestFamilyEffect(
            PriestFamilyEffectID a_id) noexcept
    {
        return GetIndexed(
            g_forms.priestFamily.effects,
            a_id);
    }

    RE::SpellItem*
        GetPriestFamilySpell(
            PriestFamilySpellID a_id) noexcept
    {
        return GetIndexed(
            g_forms.priestFamily.spells,
            a_id);
    }

    RE::SpellItem*
        GetPriestEndlessPrayerSpell() noexcept
    {
        return GetPriestFamilySpell(
            PriestFamilySpellID::
                kEndlessPrayer);
    }
}
