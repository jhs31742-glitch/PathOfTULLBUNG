#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string_view>

// QA15 catalogue is enabled only in the Dev 1.2 test build.
#ifndef POT_QA15_EXTENSION
#define POT_QA15_EXTENSION 0
#endif

namespace TULLBUNG::BuildState
{
    enum class ClassID : std::uint8_t
    {
        kWarrior = 0,
        kBerserker,
        kGuardian,

        kHunter,
        kAssassin,
        kMarksman,

        kPriest,
        kPaladin,
        kInquisitor,

        kMage,
        kVoidMage,
        kSiegeMage,
#if POT_QA15_EXTENSION
        kQA15Sentinel,
#endif

        kCount
    };

    enum class NodeID : std::uint16_t
    {
        // Warrior 0x00
        kWarriorCombatBreathing = 0x000,
        kWarriorVigor           = 0x001,
        kWarriorIronWall        = 0x002,
        kWarriorAdrenaline      = 0x003,
        kWarriorBattleSprint    = 0x004,
        kWarriorMaster          = 0x005,

        // Berserker 0x10
        kBerserkerTwoHandedMastery = 0x010,
        kBerserkerBloodStrike       = 0x011,
        kBerserkerBloodSmash        = 0x012,
        kBerserkerFightingSpirit    = 0x013,
        kBerserkerSurvivalInstinct  = 0x014,
        kBerserkerMaster            = 0x015,

        // Guardian 0x20
        kGuardianShieldMastery  = 0x020,
        kGuardianBestDefense    = 0x021,
        kGuardianShieldBash     = 0x022,
        kGuardianHammerAndAnvil = 0x023,
        kGuardianMaster         = 0x024,

        // Hunter 0x30
        kHunterLightArmorMastery = 0x030,
        kHunterPoisonTracking    = 0x031,
        kHunterTacticalRetreat   = 0x032,
        kHunterVigilance         = 0x033,
        kHunterMaster            = 0x034,

        // Assassin 0x40
        kAssassinDaggerMastery  = 0x040,
        kAssassinBloodPoison    = 0x041,
        kAssassinBloodInfusion  = 0x042,
        kAssassinSilentHandling = 0x043,
        kAssassinRefinedPoison  = 0x044,
        kAssassinMaster         = 0x045,

        // Marksman 0x50
        kMarksmanOpenBattle     = 0x050,
        kMarksmanQuickHands     = 0x051,
        kMarksmanPoisonDelivery = 0x052,
        kMarksmanEmberSpread    = 0x053,
        kMarksmanComposure      = 0x054,
        kMarksmanMaster         = 0x055,

        // Priest 0x60
        kPriestHealingArts    = 0x060,
        kPriestCrusade        = 0x061,
        kPriestFervor         = 0x062,
        kPriestFocusedHealing = 0x063,
        kPriestMaster         = 0x064,
        kPriestStance = 0x065,

        // Paladin 0x70
        kPaladinMaceMastery   = 0x070,
        kPaladinOath          = 0x071,
        kPaladinHolyStrike    = 0x072,
        kPaladinWeightOfFaith = 0x073,
        kPaladinMaster        = 0x074,

        // Inquisitor 0x80
        kInquisitorMonsterStudy   = 0x080,
        kInquisitorArtifactStudy  = 0x081,
        kInquisitorMonsterDefense = 0x082,
        kInquisitorSmite          = 0x083,
        kInquisitorMaster         = 0x084,

        // Mage 0x90
        kMageBarrier         = 0x090,
        kMageArcaneStudies   = 0x091,
        kMageMagickaCollapse = 0x092,
        kMageMaster          = 0x093,

        // Void Mage 0xA0
        kVoidMageVoidStrike   = 0x0A0,
        kVoidMageVoidArmament = 0x0A1,
        kVoidMageVoidErosion  = 0x0A2,
        kVoidMageVoidDevour   = 0x0A3,
        kVoidMageMaster       = 0x0A4,
        kVoidMageVoid         = 0x0A5,

        // Siege Mage 0xB0
        kSiegeMageBallistics     = 0x0B0,
        kSiegeMageGravityShackle = 0x0B1,
        kSiegeMageFocusedBarrage = 0x0B2,
        kSiegeMageMagickaReturn  = 0x0B3,
        kSiegeMageMaster         = 0x0B4,
        kSiegeMageDeployment = 0x0B5,

#if POT_QA15_EXTENSION
        kQA15Node01 = 0x0C0,
        kQA15Node02 = 0x0C1,
        kQA15Node03 = 0x0C2,
        kQA15Node04 = 0x0C3,
        kQA15Node05 = 0x0C4,
        kQA15Master = 0x0C5,
#endif

        kInvalid = 0xFFFF
    };

    constexpr std::size_t kClassCount =
        static_cast<std::size_t>(ClassID::kCount);

    constexpr bool kQA15ExtensionEnabled = POT_QA15_EXTENSION != 0;
    constexpr std::size_t kNodeStorageSize = kQA15ExtensionEnabled ? 0x0D0 : 0x0C0;

    // Session memory only. These are exactly the existing QA15 v2 extension bytes.
    struct RetainedQAState {bool version2{};std::array<std::uint8_t,49> extension{};std::uint16_t spent{};};
    [[nodiscard]] RetainedQAState GetRetainedQAState() noexcept;
    void SetRetainedQAState(const RetainedQAState&) noexcept;

    struct NodeMeta final
    {
        NodeID id{};
        ClassID classID{};
        std::uint8_t cost{};
        bool master{};
        std::string_view key{};
    };

    struct PersistentState final
    {
        std::uint16_t highestPlayerLevelSeen{};

        std::array<
            std::uint8_t,
            kClassCount>
                acquiredClasses{};

        std::array<
            std::uint8_t,
            kNodeStorageSize>
                acquiredNodes{};

        std::array<
            std::uint8_t,
            kNodeStorageSize>
                enabledNodes{};

        std::array<
            std::uint8_t,
            kNodeStorageSize>
                paidCost{};
    };

    enum class AcquireClassResult : std::uint8_t
    {
        kSuccess,
        kAlreadyAcquired,
        kLocked,
        kInvalid
    };

    enum class PurchaseNodeResult : std::uint8_t
    {
        kSuccess,
        kAlreadyOwned,
        kClassNotAcquired,
        kMasterLocked,
        kInsufficientJP,
        kInvalid,
        kPrerequisiteLocked
    };

    enum class SetNodeEnabledResult : std::uint8_t
    {
        kSuccess,
        kAlreadyInState,
        kNotOwned,
        kInvalid
    };

    bool Initialize();

    // 새 게임용 완전 초기화.
    void ResetForNewGame();

    // 플레이어가 사용하는 전체 노드 초기화.
    // 획득 직업 / 최고 레벨 / 획득 총 JP는 유지한다.
    void ResetAllNodes();

    void ObservePlayerLevel(std::uint16_t a_level);
    void ObserveCurrentPlayerLevel();

    void ExportPersistentState(
        PersistentState& a_out) noexcept;

    [[nodiscard]]
    bool RestorePersistentState(
        const PersistentState& a_state) noexcept;

    [[nodiscard]] bool HasClass(ClassID a_class) noexcept;
    [[nodiscard]] bool HasNode(NodeID a_node) noexcept;

    [[nodiscard]] bool IsNodeEnabled(NodeID a_node) noexcept;
    [[nodiscard]] bool IsNodeActive(NodeID a_node) noexcept;

    [[nodiscard]] bool IsClassUnlocked(ClassID a_class);
    [[nodiscard]] bool IsMasterUnlocked(ClassID a_class);

    [[nodiscard]] std::uint8_t GetClassLevel(ClassID a_class);

    [[nodiscard]] std::uint16_t GetHighestPlayerLevelSeen() noexcept;
    [[nodiscard]] std::uint16_t GetTotalEarnedJP() noexcept;
    [[nodiscard]] std::uint16_t GetSpentJP() noexcept;
    [[nodiscard]] std::uint16_t GetDebugJP() noexcept;
    [[nodiscard]] std::uint16_t GetAvailableJP() noexcept;

    void AddDebugJP(std::uint16_t a_amount) noexcept;
    void ClearDebugJP() noexcept;

    [[nodiscard]] AcquireClassResult TryAcquireClass(ClassID a_class);
    [[nodiscard]] PurchaseNodeResult TryPurchaseNode(NodeID a_node);

    [[nodiscard]] SetNodeEnabledResult SetNodeEnabled(
        NodeID a_node,
        bool a_enabled);

    [[nodiscard]] SetNodeEnabledResult ToggleNode(NodeID a_node);

    [[nodiscard]] const NodeMeta* GetNodeMeta(NodeID a_node) noexcept;

    void LogSummary();
}
