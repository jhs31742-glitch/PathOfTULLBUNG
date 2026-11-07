#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace TULLBUNG::DataCache
{
    enum class WarriorPerkID : std::uint8_t
    {
        kCombatBreath = 0,
        kWarriorVigor,
        kIronWall,
        kAdrenaline,
        kBattleSprint,
        kWarMachine,

        kTwoHandMastery,
        kBloodStrike,
        kBloodSmash,
        kStruggle,
        kSurvivalDesire,
        kBoilingBlood,

        kShieldMastery,
        kBestDefense,
        kShieldBash,
        kHammerAnvil,
        kIronBlood,

        kCount
    };

    enum class WarriorEffectID : std::uint8_t
    {
        kCombatBreath = 0,
        kWarriorVigor,
        kAdrenaline,
        kTwoHandMastery,
        kStruggle,
        kSurvivalDesire,
        kHammerAnvil,

        kCount
    };

    enum class WarriorSpellID : std::uint8_t
    {
        kCombatBreath = 0,
        kWarriorVigor,
        kAdrenaline,
        kTwoHandMastery,

        kStruggle1,
        kStruggle2,
        kStruggle3,
        kStruggle4,
        kStruggle5,

        kSurvivalDesire,
        kHammerAnvil,

        kCount
    };

    inline constexpr std::size_t kWarriorPerkCount =
        static_cast<std::size_t>(
            WarriorPerkID::kCount);

    inline constexpr std::size_t kWarriorEffectCount =
        static_cast<std::size_t>(
            WarriorEffectID::kCount);

    inline constexpr std::size_t kWarriorSpellCount =
        static_cast<std::size_t>(
            WarriorSpellID::kCount);

    struct WarriorForms final
    {
        std::array<
            RE::BGSPerk*,
            kWarriorPerkCount>
            perks{};

        std::array<
            RE::EffectSetting*,
            kWarriorEffectCount>
            effects{};

        std::array<
            RE::SpellItem*,
            kWarriorSpellCount>
            spells{};

        RE::BGSListForm* warriorPerks{};
        RE::BGSListForm* berserkerPerks{};
        RE::BGSListForm* guardianPerks{};

        RE::BGSListForm* runtimeSpells{};
        RE::BGSListForm* runtimeEffects{};

        bool ready{};
    };

    enum class HunterPerkID : std::uint8_t
    {
        kLightArmorMastery = 0,
        kPoisonTracking,
        kTacticalRetreat,
        kHunterVigilance,
        kHunterRivalry,

        kDaggerMastery,
        kBloodPoisoning,
        kBloodInfusion,
        kQuietProcessing,
        kBloodRefinement,
        kWoundGouge,

        kOpenBattle,
        kQuickHands,
        kBloodDelivery,
        kEmberSpread,
        kComposure,
        kExecution,

        kCount
    };

    enum class HunterEffectID : std::uint8_t
    {
        kPoisonTrackingA = 0,
        kPoisonTrackingB,
        kTacticalRetreatEthereal,
        kTacticalRetreatInvisibility,
        kDaggerDiseaseResist,

        kBloodBaseS1,
        kBloodBaseS2,
        kBloodBaseS3A,
        kBloodBaseS3B,

        kBloodRefinedS1,
        kBloodRefinedS2,
        kBloodRefinedS3A,
        kBloodRefinedS3B,

        kBloodSlow,
        kBloodRegenBlock,
        kQuickHands,

        kEmberS1,
        kEmberS2,
        kEmberS3A,
        kEmberS3B,

        kCount
    };

    enum class HunterSpellID : std::uint8_t
    {
        kPoisonTrackingA = 0,
        kPoisonTrackingB,
        kTacticalRetreat,
        kDaggerMastery,

        kBloodBaseS1,
        kBloodBaseS2,
        kBloodBaseS3A,
        kBloodBaseS3B,

        kBloodRefinedS1,
        kBloodRefinedS2,
        kBloodRefinedS3A,
        kBloodRefinedS3B,

        kQuickHands,

        kEmberS1,
        kEmberS2,
        kEmberS3A,
        kEmberS3B,

        kCount
    };

    inline constexpr std::size_t kHunterPerkCount =
        static_cast<std::size_t>(
            HunterPerkID::kCount);

    inline constexpr std::size_t kHunterEffectCount =
        static_cast<std::size_t>(
            HunterEffectID::kCount);

    inline constexpr std::size_t kHunterSpellCount =
        static_cast<std::size_t>(
            HunterSpellID::kCount);

    struct HunterForms final
    {
        std::array<
            RE::BGSPerk*,
            kHunterPerkCount>
            perks{};

        std::array<
            RE::EffectSetting*,
            kHunterEffectCount>
            effects{};

        std::array<
            RE::SpellItem*,
            kHunterSpellCount>
            spells{};

        RE::BGSListForm* hunterPerks{};
        RE::BGSListForm* assassinPerks{};
        RE::BGSListForm* marksmanPerks{};

        RE::BGSListForm* runtimeSpells{};
        RE::BGSListForm* runtimeEffects{};

        bool ready{};
    };

    enum class PriestFamilyPerkID : std::uint8_t
    {
        kPriestHealingArts = 0,
        kPriestCrusade,
        kPriestFervor,
        kPriestFocusedHealing,
        kPriestMaster,

        kPaladinMaceMastery,
        kPaladinOath,
        kPaladinHolyStrike,
        kPaladinWeightOfFaith,
        kPaladinMaster,

        kInquisitorMonsterStudy,
        kInquisitorArtifactStudy,
        kInquisitorMonsterDefense,
        kInquisitorSmite,
        kInquisitorMaster,

        kCount
    };

    enum class PriestFamilyEffectID : std::uint8_t
    {
        kEndlessPrayerMagicka = 0,
        kEndlessPrayerStamina,

        kPaladinOathBlock,
        kPaladinWeightArmor,
        kPaladinWeightMagicResist,
        kPaladinWeightDiseaseResist,

        kInquisitorArtifactHealth,
        kInquisitorArtifactMagicka,
        kInquisitorArtifactStamina,
        kInquisitorSmiteTrigger,
        kInquisitorSmiteVFX,

        kCount
    };

    enum class PriestFamilySpellID : std::uint8_t
    {
        kEndlessPrayer = 0,

        kPaladinOathBlock,
        kPaladinWeightOfFaith,

        kInquisitorArtifact1,
        kInquisitorArtifact2,
        kInquisitorArtifact3,
        kInquisitorArtifact4,
        kInquisitorArtifact5,
        kInquisitorArtifact6,
        kInquisitorArtifact7,
        kInquisitorArtifact8,

        kInquisitorSmite,
        kInquisitorSmiteVFX,

        kCount
    };

    inline constexpr std::size_t
        kPriestFamilyPerkCount =
            static_cast<std::size_t>(
                PriestFamilyPerkID::kCount);

    inline constexpr std::size_t
        kPriestFamilyEffectCount =
            static_cast<std::size_t>(
                PriestFamilyEffectID::kCount);

    inline constexpr std::size_t
        kPriestFamilySpellCount =
            static_cast<std::size_t>(
                PriestFamilySpellID::kCount);

    struct PriestFamilyForms final
    {
        std::array<
            RE::BGSPerk*,
            kPriestFamilyPerkCount>
            perks{};

        std::array<
            RE::EffectSetting*,
            kPriestFamilyEffectCount>
            effects{};

        std::array<
            RE::SpellItem*,
            kPriestFamilySpellCount>
            spells{};

        RE::BGSListForm* priestPerks{};
        RE::BGSListForm* paladinPerks{};
        RE::BGSListForm* inquisitorPerks{};

        bool ready{};
    };
    enum class MageFamilyPerkID : std::uint8_t
    {
        kMageBarrier = 0,
        kMageArcaneStudies,
        kMageMagickaCollapse,
        kMageMaster,

        kVoidStrike,
        kVoidArmament,
        kVoidErosion,
        kVoidDevour,
        kVoidMaster,

        kSiegeBallistics,
        kSiegeGravityShackle,
        kSiegeFocusedBarrage,
        kSiegeMagickaReturn,
        kSiegeMaster,

        kCount
    };

    enum class MageFamilyEffectID : std::uint8_t
    {
        kBarrierArmor = 0,
        kArcaneStudiesMagicka,
        kArchmageMagickaRegen,
        kMagickaCollapseTrigger,

        kVoidErosionMagickaRegen,
        kVoidDevourTrigger,
        kVoidVesselMagickaRegen,

        kSiegeRuneSlow,

        kCount
    };

    enum class MageFamilySpellID : std::uint8_t
    {
        kBarrierAbility = 0,

        kArcaneStudies1,
        kArcaneStudies2,
        kArcaneStudies3,
        kArcaneStudies4,
        kArcaneStudies5,
        kArcaneStudies6,
        kArcaneStudies7,
        kArcaneStudies8,
        kArcaneStudies9,
        kArcaneStudies10,

        kArchmage1,
        kArchmage2,
        kArchmage3,
        kArchmage4,
        kArchmage5,
        kArchmage6,
        kArchmage7,
        kArchmage8,
        kArchmage9,
        kArchmage10,

        kMagickaCollapsePower,

        kVoidErosionSpell,
        kVoidDevourPower,
        kVoidVesselAbility,

        kSiegeRuneSlow50,
        kSiegeRuneSlow70,
        kVoidDevourTargetVFX,

        kCount
    };

    enum class MageFamilyHazardID : std::uint8_t
    {
        kSiegeRuneBase = 0,
        kSiegeRuneGravity,
        kSiegeRuneGameplaySlowBase,
        kSiegeRuneGameplaySlowGravity,

        kCount
    };

    inline constexpr std::size_t
        kMageFamilyPerkCount =
            static_cast<std::size_t>(
                MageFamilyPerkID::kCount);

    inline constexpr std::size_t
        kMageFamilyEffectCount =
            static_cast<std::size_t>(
                MageFamilyEffectID::kCount);

    inline constexpr std::size_t
        kMageFamilySpellCount =
            static_cast<std::size_t>(
                MageFamilySpellID::kCount);

    inline constexpr std::size_t
        kMageFamilyHazardCount =
            static_cast<std::size_t>(
                MageFamilyHazardID::kCount);

    struct MageFamilyForms final
    {
        std::array<
            RE::BGSPerk*,
            kMageFamilyPerkCount>
            perks{};

        std::array<
            RE::EffectSetting*,
            kMageFamilyEffectCount>
            effects{};

        std::array<
            RE::SpellItem*,
            kMageFamilySpellCount>
            spells{};

        std::array<
            RE::TESForm*,
            kMageFamilyHazardCount>
            hazards{};

        RE::BGSListForm* magePerks{};
        RE::BGSListForm* voidMagePerks{};
        RE::TESObjectACTI* siegeRuneVFXActivator{};

        RE::BGSListForm* siegeMagePerks{};

        bool ready{};
    };

    struct Forms final
    {
        RE::BGSListForm* daedricArtifacts{};
        RE::BGSKeyword* monsterKeyword{};
        RE::BGSListForm* monsterKeywords{};
        RE::BGSListForm* monsterRaceWhitelist{};

        // Vanilla classification
        RE::BGSKeyword* weapTypeDagger{};

        // Class-local scaffolds. Missing class forms must not
        // invalidate the Stage 1-11 core readiness flag.
        WarriorForms warrior{};
        HunterForms hunter{};
        PriestFamilyForms priestFamily{};
        MageFamilyForms mageFamily{};

        bool ready{};
    };

    bool Initialize();

    [[nodiscard]] bool IsReady() noexcept;

    [[nodiscard]] bool IsWarriorReady() noexcept;
    [[nodiscard]] bool IsHunterReady() noexcept;

    [[nodiscard]] RE::BGSPerk* GetWarriorPerk(
        WarriorPerkID a_id) noexcept;

    [[nodiscard]] RE::EffectSetting* GetWarriorEffect(
        WarriorEffectID a_id) noexcept;

    [[nodiscard]] RE::SpellItem* GetWarriorSpell(
        WarriorSpellID a_id) noexcept;

    [[nodiscard]] RE::BGSPerk* GetHunterPerk(
        HunterPerkID a_id) noexcept;

    [[nodiscard]] RE::EffectSetting* GetHunterEffect(
        HunterEffectID a_id) noexcept;

    [[nodiscard]] RE::SpellItem* GetHunterSpell(
        HunterSpellID a_id) noexcept;

    [[nodiscard]] bool IsMageReady() noexcept;
    [[nodiscard]] bool IsVoidMageReady() noexcept;
    [[nodiscard]] bool IsSiegeMageReady() noexcept;

    [[nodiscard]] RE::BGSPerk*
        GetMageFamilyPerk(
            MageFamilyPerkID a_id) noexcept;

    [[nodiscard]] RE::EffectSetting*
        GetMageFamilyEffect(
            MageFamilyEffectID a_id) noexcept;

    [[nodiscard]] RE::SpellItem*
        GetMageFamilySpell(
            MageFamilySpellID a_id) noexcept;

    [[nodiscard]] RE::TESForm*
        GetMageFamilyHazard(
            MageFamilyHazardID a_id) noexcept;

    [[nodiscard]] RE::TESObjectACTI*
        GetSiegeRuneVFXActivator() noexcept;

    [[nodiscard]] const Forms& Get() noexcept;
}

namespace TULLBUNG::DataCache
{
    [[nodiscard]] bool IsPriestReady() noexcept;
    [[nodiscard]] bool IsPaladinReady() noexcept;
    [[nodiscard]] bool IsInquisitorReady() noexcept;

    [[nodiscard]] RE::BGSPerk*
        GetPriestFamilyPerk(
            PriestFamilyPerkID a_id) noexcept;

    [[nodiscard]] RE::EffectSetting*
        GetPriestFamilyEffect(
            PriestFamilyEffectID a_id) noexcept;

    [[nodiscard]] RE::SpellItem*
        GetPriestFamilySpell(
            PriestFamilySpellID a_id) noexcept;

    [[nodiscard]] RE::SpellItem*
        GetPriestEndlessPrayerSpell() noexcept;
}
