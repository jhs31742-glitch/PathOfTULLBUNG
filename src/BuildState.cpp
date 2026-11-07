#include "PCH.h"
#include "BuildState.h"
#include "Persistence.h"
#include "ClassRuntime.h"

#include <limits>

namespace
{
    using namespace TULLBUNG::BuildState;

    struct State final
    {
        std::bitset<kClassCount> acquiredClasses{};
        std::bitset<kNodeStorageSize> acquiredNodes{};
        std::bitset<kNodeStorageSize> enabledNodes{};

        std::array<std::uint8_t, kNodeStorageSize> paidCost{};

        std::uint16_t highestPlayerLevelSeen{};
        std::uint16_t totalEarnedJP{};
        std::uint16_t spentJP{};
        RetainedQAState retainedQA{};
    };

    State g_state{};
    std::uint16_t g_debugJP{};

    constexpr std::array<NodeMeta, kQA15ExtensionEnabled ? 72 : 66> kNodes{{
        // Warrior
        { NodeID::kWarriorCombatBreathing, ClassID::kWarrior, 1, false, "warrior.combat_breathing" },
        { NodeID::kWarriorVigor,           ClassID::kWarrior, 2, false, "warrior.vigor" },
        { NodeID::kWarriorIronWall,        ClassID::kWarrior, 2, false, "warrior.iron_wall" },
        { NodeID::kWarriorAdrenaline,      ClassID::kWarrior, 3, false, "warrior.adrenaline" },
        { NodeID::kWarriorBattleSprint,    ClassID::kWarrior, 2, false, "warrior.battle_sprint" },
        { NodeID::kWarriorMaster,          ClassID::kWarrior, 3, true,  "warrior.master" },

        // Berserker
        { NodeID::kBerserkerTwoHandedMastery, ClassID::kBerserker, 2, false, "berserker.two_handed_mastery" },
        { NodeID::kBerserkerBloodStrike,       ClassID::kBerserker, 2, false, "berserker.blood_strike" },
        { NodeID::kBerserkerBloodSmash,        ClassID::kBerserker, 3, false, "berserker.blood_smash" },
        { NodeID::kBerserkerFightingSpirit,    ClassID::kBerserker, 2, false, "berserker.fighting_spirit" },
        { NodeID::kBerserkerSurvivalInstinct,  ClassID::kBerserker, 1, false, "berserker.survival_instinct" },
        { NodeID::kBerserkerMaster,            ClassID::kBerserker, 3, true,  "berserker.master" },

        // Guardian
        { NodeID::kGuardianShieldMastery,  ClassID::kGuardian, 2, false, "guardian.shield_mastery" },
        { NodeID::kGuardianBestDefense,    ClassID::kGuardian, 3, false, "guardian.best_defense" },
        { NodeID::kGuardianShieldBash,     ClassID::kGuardian, 3, false, "guardian.shield_bash" },
        { NodeID::kGuardianHammerAndAnvil, ClassID::kGuardian, 2, false, "guardian.hammer_and_anvil" },
        { NodeID::kGuardianMaster,         ClassID::kGuardian, 2, true,  "guardian.master" },

        // Hunter
        { NodeID::kHunterLightArmorMastery, ClassID::kHunter, 2, false, "hunter.light_armor_mastery" },
        { NodeID::kHunterPoisonTracking,    ClassID::kHunter, 2, false, "hunter.poison_tracking" },
        { NodeID::kHunterTacticalRetreat,   ClassID::kHunter, 3, false, "hunter.tactical_retreat" },
        { NodeID::kHunterVigilance,         ClassID::kHunter, 3, false, "hunter.vigilance" },
        { NodeID::kHunterMaster,            ClassID::kHunter, 3, true,  "hunter.master" },

        // Assassin
        { NodeID::kAssassinDaggerMastery,  ClassID::kAssassin, 1, false, "assassin.dagger_mastery" },
        { NodeID::kAssassinBloodPoison,    ClassID::kAssassin, 1, false, "assassin.blood_poison" },
        { NodeID::kAssassinBloodInfusion,  ClassID::kAssassin, 2, false, "assassin.blood_infusion" },
        { NodeID::kAssassinSilentHandling, ClassID::kAssassin, 3, false, "assassin.silent_handling" },
        { NodeID::kAssassinRefinedPoison,  ClassID::kAssassin, 3, false, "assassin.refined_poison" },
        { NodeID::kAssassinMaster,         ClassID::kAssassin, 3, true,  "assassin.master" },

        // Marksman
        { NodeID::kMarksmanOpenBattle,     ClassID::kMarksman, 2, false, "marksman.open_battle" },
        { NodeID::kMarksmanQuickHands,     ClassID::kMarksman, 2, false, "marksman.quick_hands" },
        { NodeID::kMarksmanPoisonDelivery, ClassID::kMarksman, 1, false, "marksman.poison_delivery" },
        { NodeID::kMarksmanEmberSpread,    ClassID::kMarksman, 3, false, "marksman.ember_spread" },
        { NodeID::kMarksmanComposure,      ClassID::kMarksman, 2, false, "marksman.composure" },
        { NodeID::kMarksmanMaster,         ClassID::kMarksman, 3, true,  "marksman.master" },

        // Priest
        { NodeID::kPriestHealingArts,    ClassID::kPriest, 2, false, "priest.healing_arts" },
        { NodeID::kPriestCrusade,        ClassID::kPriest, 3, false, "priest.crusade" },
        { NodeID::kPriestFervor,         ClassID::kPriest, 3, false, "priest.fervor" },
        { NodeID::kPriestFocusedHealing, ClassID::kPriest, 2, false, "priest.focused_healing" },
        { NodeID::kPriestMaster,         ClassID::kPriest, 3, true,  "priest.master" },
        { NodeID::kPriestStance, ClassID::kPriest, 0, false, "priest.stance" },

        // Paladin
        { NodeID::kPaladinMaceMastery,   ClassID::kPaladin, 2, false, "paladin.mace_mastery" },
        { NodeID::kPaladinOath,          ClassID::kPaladin, 2, false, "paladin.oath" },
        { NodeID::kPaladinHolyStrike,    ClassID::kPaladin, 3, false, "paladin.holy_strike" },
        { NodeID::kPaladinWeightOfFaith, ClassID::kPaladin, 3, false, "paladin.weight_of_faith" },
        { NodeID::kPaladinMaster,        ClassID::kPaladin, 3, true,  "paladin.master" },

        // Inquisitor
        { NodeID::kInquisitorMonsterStudy,   ClassID::kInquisitor, 2, false, "inquisitor.monster_study" },
        { NodeID::kInquisitorArtifactStudy,  ClassID::kInquisitor, 2, false, "inquisitor.artifact_study" },
        { NodeID::kInquisitorMonsterDefense, ClassID::kInquisitor, 3, false, "inquisitor.monster_defense" },
        { NodeID::kInquisitorSmite,          ClassID::kInquisitor, 3, false, "inquisitor.smite" },
        { NodeID::kInquisitorMaster,         ClassID::kInquisitor, 3, true,  "inquisitor.master" },

        // Mage
        { NodeID::kMageBarrier,         ClassID::kMage, 3, false, "mage.barrier" },
        { NodeID::kMageArcaneStudies,   ClassID::kMage, 3, false, "mage.arcane_studies" },
        { NodeID::kMageMagickaCollapse, ClassID::kMage, 4, false, "mage.magicka_collapse" },
        { NodeID::kMageMaster,          ClassID::kMage, 3, true,  "mage.master" },

        // Void Mage
        { NodeID::kVoidMageVoidStrike,   ClassID::kVoidMage, 1, false, "void_mage.void_strike" },
        { NodeID::kVoidMageVoidArmament, ClassID::kVoidMage, 2, false, "void_mage.void_armament" },
        { NodeID::kVoidMageVoidErosion,  ClassID::kVoidMage, 3, false, "void_mage.void_erosion" },
        { NodeID::kVoidMageVoidDevour,   ClassID::kVoidMage, 4, false, "void_mage.void_devour" },
        { NodeID::kVoidMageMaster,       ClassID::kVoidMage, 3, true,  "void_mage.master" },
        { NodeID::kVoidMageVoid,         ClassID::kVoidMage, 0, false, "void_mage.void" },

        // Siege Mage
        { NodeID::kSiegeMageBallistics,     ClassID::kSiegeMage, 2, false, "siege_mage.ballistics" },
        { NodeID::kSiegeMageGravityShackle, ClassID::kSiegeMage, 2, false, "siege_mage.gravity_shackle" },
        { NodeID::kSiegeMageFocusedBarrage, ClassID::kSiegeMage, 3, false, "siege_mage.focused_barrage" },
        { NodeID::kSiegeMageMagickaReturn,  ClassID::kSiegeMage, 3, false, "siege_mage.magicka_return" },
        { NodeID::kSiegeMageMaster,         ClassID::kSiegeMage, 3, true,  "siege_mage.master" },
        { NodeID::kSiegeMageDeployment, ClassID::kSiegeMage, 0, false, "siege_mage.deployment" }
#if POT_QA15_EXTENSION
        ,{ NodeID::kQA15Node01, ClassID::kQA15Sentinel, 2, false, "qa15.node_01" }
        ,{ NodeID::kQA15Node02, ClassID::kQA15Sentinel, 2, false, "qa15.node_02" }
        ,{ NodeID::kQA15Node03, ClassID::kQA15Sentinel, 2, false, "qa15.node_03" }
        ,{ NodeID::kQA15Node04, ClassID::kQA15Sentinel, 2, false, "qa15.node_04" }
        ,{ NodeID::kQA15Node05, ClassID::kQA15Sentinel, 2, false, "qa15.node_05" }
        ,{ NodeID::kQA15Master, ClassID::kQA15Sentinel, 3, true, "qa15.master" }
#endif
    }};

    constexpr bool IsValidClass(ClassID a_class)
    {
        return static_cast<std::size_t>(a_class) < kClassCount;
    }

    constexpr bool IsBaseClass(ClassID a_class)
    {
        return a_class == ClassID::kWarrior ||
               a_class == ClassID::kHunter ||
               a_class == ClassID::kPriest ||
               a_class == ClassID::kMage
#if POT_QA15_EXTENSION
               || a_class == ClassID::kQA15Sentinel
#endif
               ;
    }

    ClassID ParentClass(ClassID a_class)
    {
        switch (a_class) {
        case ClassID::kBerserker:
        case ClassID::kGuardian:
            return ClassID::kWarrior;

        case ClassID::kAssassin:
        case ClassID::kMarksman:
            return ClassID::kHunter;

        case ClassID::kPaladin:
        case ClassID::kInquisitor:
            return ClassID::kPriest;

        case ClassID::kVoidMage:
        case ClassID::kSiegeMage:
            return ClassID::kMage;

        default:
            return ClassID::kCount;
        }
    }

    std::size_t ClassIndex(ClassID a_class)
    {
        return static_cast<std::size_t>(a_class);
    }

    std::size_t NodeIndex(NodeID a_node)
    {
        return static_cast<std::size_t>(a_node);
    }

    bool IsVoidMageChildNode(NodeID a_node) noexcept
    {
        switch (a_node) {
        case NodeID::kVoidMageVoidStrike:
        case NodeID::kVoidMageVoidArmament:
        case NodeID::kVoidMageVoidErosion:
        case NodeID::kVoidMageVoidDevour:
        case NodeID::kVoidMageMaster:
            return true;

        default:
            return false;
        }
    }

    const NodeMeta* FindNode(NodeID a_node)
    {
        for (const auto& node : kNodes) {
            if (node.id == a_node) {
                return std::addressof(node);
            }
        }

        return nullptr;
    }

    std::uint16_t CalculateEarnedJP(
        std::uint16_t a_highestLevel)
    {
        return static_cast<std::uint16_t>(
            a_highestLevel / 3);
    }

    void ClearPurchasedNodes(
        bool a_log)
    {
        const auto refunded =
            g_state.spentJP;

        g_state.acquiredNodes.reset();
        g_state.enabledNodes.reset();
        g_state.paidCost.fill(0);
        g_state.spentJP = 0;

        if (a_log) {
            logger::info(
                "[BuildState] ResetAllNodes | RefundedJP={} | EarnedJP={} | DebugJP={} | AvailableJP={}",
                refunded,
                g_state.totalEarnedJP,
                g_debugJP,
                TULLBUNG::BuildState::GetAvailableJP());
        }
    }

    bool ValidateCatalog()
    {
        bool valid = true;

        std::bitset<kNodeStorageSize> seenIDs{};

        for (const auto& node : kNodes) {
            const auto index =
                NodeIndex(node.id);

            if (index >= kNodeStorageSize) {
                valid = false;

                logger::error(
                    "[BuildState] Node ID out of range: {}",
                    node.key);

                continue;
            }

            if (seenIDs.test(index)) {
                valid = false;

                logger::error(
                    "[BuildState] Duplicate Node ID: {}",
                    node.key);

                continue;
            }

            seenIDs.set(index);
        }

        for (std::size_t classIndex = 0;
             classIndex < kClassCount;
             ++classIndex) {

            const auto classID =
                static_cast<ClassID>(classIndex);

            std::uint16_t normalTotal = 0;

            for (const auto& node : kNodes) {
                if (node.classID == classID &&
                    !node.master) {
                    normalTotal += node.cost;
                }
            }

            if (normalTotal != 10) {
                valid = false;

                logger::error(
                    "[BuildState] Class {} normal JP total = {} (expected 10)",
                    classIndex,
                    normalTotal);
            }
        }

        return valid;
    }

    bool RunSelfTest()
    {
        const State backup =
            g_state;

        g_state = {};

        g_state.highestPlayerLevelSeen = 300;
        g_state.totalEarnedJP = 100;

        bool pass = true;

        const auto check =
            [&](bool a_condition,
                std::string_view a_name) {
                if (!a_condition) {
                    pass = false;

                    logger::error(
                        "[BuildState] SelfTest FAIL: {}",
                        a_name);
                }
            };

        using ACR =
            AcquireClassResult;

        using PNR =
            PurchaseNodeResult;

        using ENR =
            SetNodeEnabledResult;

        check(
            TryAcquireClass(ClassID::kWarrior) ==
                ACR::kSuccess,
            "Acquire base class");

        check(
            TryAcquireClass(ClassID::kWarrior) ==
                ACR::kAlreadyAcquired,
            "Reject duplicate class acquisition");

        check(
            TryAcquireClass(ClassID::kBerserker) ==
                ACR::kLocked,
            "Advanced class locked before parent Lv5");

        check(
            TryPurchaseNode(
                NodeID::kWarriorCombatBreathing) ==
                PNR::kSuccess,
            "Purchase warrior node 1");

        check(
            TryPurchaseNode(
                NodeID::kWarriorVigor) ==
                PNR::kSuccess,
            "Purchase warrior node 2");

        check(
            TryPurchaseNode(
                NodeID::kWarriorIronWall) ==
                PNR::kSuccess,
            "Purchase warrior node 3");

        check(
            GetClassLevel(ClassID::kWarrior) == 5,
            "Warrior reaches Lv5");

        check(
            TryPurchaseNode(
                NodeID::kWarriorIronWall) ==
                PNR::kAlreadyOwned,
            "Reject duplicate node purchase");

        check(
            HasNode(NodeID::kWarriorVigor),
            "Purchased node remains owned");

        check(
            IsNodeActive(NodeID::kWarriorVigor),
            "Purchased node enabled by default");

        check(
            SetNodeEnabled(
                NodeID::kWarriorVigor,
                false) ==
                ENR::kSuccess,
            "Disable purchased node");

        check(
            HasNode(NodeID::kWarriorVigor) &&
            !IsNodeEnabled(NodeID::kWarriorVigor) &&
            !IsNodeActive(NodeID::kWarriorVigor),
            "Disabled node stays purchased but inactive");

        check(
            GetClassLevel(ClassID::kWarrior) == 5,
            "Disabled node still counts for class level");

        check(
            SetNodeEnabled(
                NodeID::kWarriorVigor,
                false) ==
                ENR::kAlreadyInState,
            "Reject redundant disable");

        check(
            ToggleNode(
                NodeID::kWarriorVigor) ==
                ENR::kSuccess,
            "Toggle node back on");

        check(
            IsNodeActive(NodeID::kWarriorVigor),
            "Toggled node active again");

        const auto jpBeforeAdvanced =
            GetAvailableJP();

        check(
            TryAcquireClass(ClassID::kBerserker) ==
                ACR::kSuccess,
            "Advanced class unlocks at parent Lv5");

        check(
            GetAvailableJP() ==
                jpBeforeAdvanced,
            "Advanced unlock grants no free JP and costs no JP");

        check(
            TryPurchaseNode(
                NodeID::kWarriorMaster) ==
                PNR::kMasterLocked,
            "Master locked before class Lv10");

        check(
            TryPurchaseNode(
                NodeID::kWarriorAdrenaline) ==
                PNR::kSuccess,
            "Purchase warrior node 4");

        check(
            TryPurchaseNode(
                NodeID::kWarriorBattleSprint) ==
                PNR::kSuccess,
            "Purchase warrior node 5");

        check(
            GetClassLevel(ClassID::kWarrior) == 10,
            "Warrior reaches Lv10");

        check(
            IsMasterUnlocked(ClassID::kWarrior),
            "Master unlocks at Lv10");

        check(
            TryPurchaseNode(
                NodeID::kWarriorMaster) ==
                PNR::kSuccess,
            "Purchase master");

        check(
            GetClassLevel(ClassID::kWarrior) == 10,
            "Master JP excluded from class level");

        check(
            TryAcquireClass(ClassID::kHunter) ==
                ACR::kSuccess,
            "Acquire second base class");

        g_state.totalEarnedJP =
            g_state.spentJP;

        check(
            TryPurchaseNode(
                NodeID::kHunterLightArmorMastery) ==
                PNR::kInsufficientJP,
            "Reject purchase with insufficient JP");

        g_state.totalEarnedJP = 100;

        const auto classesBeforeReset =
            g_state.acquiredClasses;

        const auto highestBeforeReset =
            g_state.highestPlayerLevelSeen;

        const auto earnedBeforeReset =
            g_state.totalEarnedJP;

        ClearPurchasedNodes(false);

        check(
            g_state.acquiredClasses ==
                classesBeforeReset,
            "Reset preserves acquired classes");

        check(
            g_state.highestPlayerLevelSeen ==
                highestBeforeReset,
            "Reset preserves highest player level");

        check(
            g_state.totalEarnedJP ==
                earnedBeforeReset,
            "Reset preserves earned JP");

        check(
            g_state.spentJP == 0 &&
            GetAvailableJP() ==
                earnedBeforeReset,
            "Reset refunds all spent JP");

        check(
            !HasNode(
                NodeID::kWarriorCombatBreathing) &&
            !IsNodeEnabled(
                NodeID::kWarriorCombatBreathing),
            "Reset clears node ownership and enabled state");

        check(
            GetClassLevel(ClassID::kWarrior) == 0,
            "Reset clears class node level");

        g_state =
            backup;

        if (pass) {
            logger::info(
                "[BuildState] SelfTest = PASS");
        } else {
            logger::error(
                "[BuildState] SelfTest = FAILED");
        }

        return pass;
    }
}

namespace TULLBUNG::BuildState
{
    bool Initialize()
    {
        if (!ValidateCatalog()) {
            logger::error(
                "[BuildState] Catalog validation FAILED");

            return false;
        }

        logger::info(
            "[BuildState] Catalog = OK | Classes={} | Nodes={}",
            kClassCount,
            kNodes.size());

        if (!RunSelfTest()) {
            logger::error(
                "[BuildState] SelfTest failed");

            return false;
        }

        return true;
    }

    void ResetForNewGame()
    {
        g_state = {};
        g_debugJP = 0;

        logger::info(
            "[BuildState] ResetForNewGame");
    }

    void ResetAllNodes()
    {
        ClearPurchasedNodes(true);

        TULLBUNG::ClassRuntime::
            OnBuildStateChanged();
    }

    void ObservePlayerLevel(
        std::uint16_t a_level)
    {
        if (a_level >
            g_state.highestPlayerLevelSeen) {

            g_state.highestPlayerLevelSeen =
                a_level;

            g_state.totalEarnedJP =
                CalculateEarnedJP(
                    g_state.highestPlayerLevelSeen);
        }
    }

    void ObserveCurrentPlayerLevel()
    {
        auto* player =
            RE::PlayerCharacter::GetSingleton();

        if (!player) {
            return;
        }

        const auto level =
            static_cast<std::uint16_t>(
                player->GetLevel());

        ObservePlayerLevel(level);

        LogSummary();
    }

    void ExportPersistentState(
        PersistentState& a_out) noexcept
    {
        a_out = {};

        a_out.highestPlayerLevelSeen =
            g_state.highestPlayerLevelSeen;

        for (std::size_t i = 0;
             i < kClassCount;
             ++i) {

            a_out.acquiredClasses[i] =
                g_state.acquiredClasses.test(i) ?
                    1 :
                    0;
        }

        for (std::size_t i = 0;
             i < kNodeStorageSize;
             ++i) {

            a_out.acquiredNodes[i] =
                g_state.acquiredNodes.test(i) ?
                    1 :
                    0;

            a_out.enabledNodes[i] =
                g_state.enabledNodes.test(i) ?
                    1 :
                    0;

            a_out.paidCost[i] =
                g_state.paidCost[i];
        }
    }


    bool RestorePersistentState(
        const PersistentState&
            a_state) noexcept
    {
        State restored{};

        restored.highestPlayerLevelSeen =
            a_state.highestPlayerLevelSeen;

        restored.totalEarnedJP =
            CalculateEarnedJP(
                restored.
                    highestPlayerLevelSeen);

        for (std::size_t i = 0;
             i < kClassCount;
             ++i) {

            const auto value =
                a_state.acquiredClasses[i];

            if (value > 1) {
                logger::error(
                    "[BuildState][Persistence] Invalid class flag | Index={} | Value={}",
                    i,
                    value);

                return false;
            }

            if (value != 0) {
                restored.acquiredClasses.set(i);
            }
        }

        std::uint32_t spent{};

        for (std::size_t i = 0;
             i < kNodeStorageSize;
             ++i) {

            const auto acquired =
                a_state.acquiredNodes[i];

            const auto enabled =
                a_state.enabledNodes[i];

            const auto paid =
                a_state.paidCost[i];

            if (acquired > 1 ||
                enabled > 1 ||
                (enabled != 0 &&
                 acquired == 0)) {

                logger::error(
                    "[BuildState][Persistence] Invalid node flags | Index={} | Acquired={} | Enabled={}",
                    i,
                    acquired,
                    enabled);

                return false;
            }

            if (acquired == 0) {
                if (paid != 0) {
                    logger::error(
                        "[BuildState][Persistence] Paid cost on unowned node | Index={} | Cost={}",
                        i,
                        paid);

                    return false;
                }

                continue;
            }

            const auto* meta =
                FindNode(
                    static_cast<NodeID>(i));

            if (!meta) {
                logger::error(
                    "[BuildState][Persistence] Unknown acquired node | Index={}",
                    i);

                return false;
            }

            const auto classIndex =
                ClassIndex(
                    meta->classID);

            if (classIndex >= kClassCount ||
                !restored.acquiredClasses.test(
                    classIndex)) {

                logger::error(
                    "[BuildState][Persistence] Node without acquired class | Index={} | Class={}",
                    i,
                    classIndex);

                return false;
            }

            if (paid != meta->cost) {
                logger::error(
                    "[BuildState][Persistence] Node cost mismatch | Index={} | Saved={} | Expected={}",
                    i,
                    paid,
                    meta->cost);

                return false;
            }

            restored.acquiredNodes.set(i);

            restored.enabledNodes.set(
                i,
                enabled != 0);

            restored.paidCost[i] =
                paid;

            spent +=
                static_cast<std::uint32_t>(
                    paid);
        }

        if (spent >
            (std::numeric_limits<
                std::uint16_t>::max)()) {

            logger::error(
                "[BuildState][Persistence] Spent JP overflow | Spent={}",
                spent);

            return false;
        }

        restored.spentJP =
            static_cast<std::uint16_t>(
                spent);

        // Core-only restore (including existing DEV reset helpers) must not drop
        // a known newer save extension in the 0.9 compatibility build.
        restored.retainedQA = g_state.retainedQA;
        g_state =
            restored;

        // DEV-only state is intentionally not serialized.
        g_debugJP = 0;

        logger::info(
            "[BuildState][Persistence] Restored | HighestLevel={} | EarnedJP={} | DebugJP=0 | SpentJP={} | AvailableJP={}",
            g_state.highestPlayerLevelSeen,
            g_state.totalEarnedJP,
            g_state.spentJP,
            GetAvailableJP());

        return true;
    }

    bool HasClass(
        ClassID a_class) noexcept
    {
        if (!IsValidClass(a_class)) {
            return false;
        }

        return g_state.acquiredClasses.test(
            ClassIndex(a_class));
    }

    bool HasNode(
        NodeID a_node) noexcept
    {
        const auto index =
            NodeIndex(a_node);

        if (index >= kNodeStorageSize) {
            return false;
        }

        return g_state.acquiredNodes.test(index);
    }

    bool IsNodeEnabled(
        NodeID a_node) noexcept
    {
        const auto index =
            NodeIndex(a_node);

        if (index >= kNodeStorageSize) {
            return false;
        }

        return g_state.enabledNodes.test(index);
    }

    bool IsNodeActive(
        NodeID a_node) noexcept
    {
        if (!HasNode(a_node) ||
            !IsNodeEnabled(a_node)) {

            return false;
        }

        if (IsVoidMageChildNode(a_node)) {
            return
                HasNode(
                    NodeID::kVoidMageVoid) &&
                IsNodeEnabled(
                    NodeID::kVoidMageVoid);
        }

        return true;
    }

    bool IsClassUnlocked(
        ClassID a_class)
    {
        if (!IsValidClass(a_class)) {
            return false;
        }

        if (IsBaseClass(a_class)) {
            return true;
        }

        const auto parent =
            ParentClass(a_class);

        if (!IsValidClass(parent) ||
            !HasClass(parent)) {
            return false;
        }

        return GetClassLevel(parent) >= 5;
    }

    bool IsMasterUnlocked(
        ClassID a_class)
    {
        if (!IsValidClass(a_class) ||
            !HasClass(a_class)) {
            return false;
        }

        return GetClassLevel(a_class) >= 10;
    }

    std::uint8_t GetClassLevel(
        ClassID a_class)
    {
        if (!IsValidClass(a_class)) {
            return 0;
        }

        std::uint16_t total = 0;

        for (const auto& node : kNodes) {
            if (node.classID != a_class ||
                node.master) {
                continue;
            }

            const auto index =
                NodeIndex(node.id);

            if (g_state.acquiredNodes.test(index)) {
                total +=
                    g_state.paidCost[index];
            }
        }

        return static_cast<std::uint8_t>(
            total > 10 ? 10 : total);
    }

    std::uint16_t GetHighestPlayerLevelSeen() noexcept
    {
        return g_state.highestPlayerLevelSeen;
    }

    std::uint16_t GetTotalEarnedJP() noexcept
    {
        return g_state.totalEarnedJP;
    }

    RetainedQAState GetRetainedQAState() noexcept {return g_state.retainedQA;}
    void SetRetainedQAState(const RetainedQAState& state) noexcept {g_state.retainedQA=state;}

    std::uint16_t GetSpentJP() noexcept
    {
        return static_cast<std::uint16_t>(g_state.spentJP + (kQA15ExtensionEnabled ? 0 : g_state.retainedQA.spent));
    }

    std::uint16_t GetDebugJP() noexcept
    {
        return g_debugJP;
    }

    std::uint16_t GetAvailableJP() noexcept
    {
        const auto totalBudget =
            static_cast<std::uint32_t>(
                g_state.totalEarnedJP) +
            static_cast<std::uint32_t>(
                g_debugJP);

        if (static_cast<std::uint32_t>(
                GetSpentJP()) >=
            totalBudget) {
            return 0;
        }

        const auto available =
            totalBudget -
            static_cast<std::uint32_t>(
                GetSpentJP());

        return static_cast<std::uint16_t>(
            available > 65535u ?
                65535u :
                available);
    }

    void AddDebugJP(
        std::uint16_t a_amount) noexcept
    {
        const auto next =
            static_cast<std::uint32_t>(
                g_debugJP) +
            static_cast<std::uint32_t>(
                a_amount);

        g_debugJP =
            static_cast<std::uint16_t>(
                next > 65535u ?
                    65535u :
                    next);

        logger::info(
            "[BuildState][DebugJP] Add={} | DebugJP={} | AvailableJP={}",
            a_amount,
            g_debugJP,
            GetAvailableJP());
    }

    void ClearDebugJP() noexcept
    {
        g_debugJP = 0;

        logger::info(
            "[BuildState][DebugJP] Clear | DebugJP=0 | AvailableJP={}",
            GetAvailableJP());
    }
    AcquireClassResult TryAcquireClass(
        ClassID a_class)
    {
        if (!IsValidClass(a_class)) {
            return AcquireClassResult::kInvalid;
        }

        if (HasClass(a_class)) {
            return AcquireClassResult::kAlreadyAcquired;
        }

        if (!IsClassUnlocked(a_class)) {
            return AcquireClassResult::kLocked;
        }

        g_state.acquiredClasses.set(
            ClassIndex(a_class));

        return AcquireClassResult::kSuccess;
    }

    PurchaseNodeResult TryPurchaseNode(
        NodeID a_node)
    {
        const auto* meta =
            FindNode(a_node);

        if (!meta) {
            return PurchaseNodeResult::kInvalid;
        }

        const auto index =
            NodeIndex(a_node);

        if (index >= kNodeStorageSize) {
            return PurchaseNodeResult::kInvalid;
        }

        if (g_state.acquiredNodes.test(index)) {
            return PurchaseNodeResult::kAlreadyOwned;
        }

        if (!HasClass(meta->classID)) {
            return PurchaseNodeResult::kClassNotAcquired;
        }

        if (IsVoidMageChildNode(a_node) &&
            !HasNode(
                NodeID::kVoidMageVoid)) {

            return
                PurchaseNodeResult::
                    kPrerequisiteLocked;
        }

        if (meta->master &&
            !IsMasterUnlocked(meta->classID)) {
            return PurchaseNodeResult::kMasterLocked;
        }

        if (GetAvailableJP() <
            meta->cost) {
            return PurchaseNodeResult::kInsufficientJP;
        }

        g_state.acquiredNodes.set(index);
        g_state.enabledNodes.set(index);

        g_state.paidCost[index] =
            meta->cost;

        g_state.spentJP =
            static_cast<std::uint16_t>(
                g_state.spentJP +
                meta->cost);

        if (a_node ==
                NodeID::
                    kInquisitorMaster) {

            // SoT authority:
            // Grand Inquisitor starts its own monster-kill
            // progression from 0 when the node is purchased.
            TULLBUNG::Persistence::
                SetInquisitorMonsterKills(0);

            logger::info(
                "[S14INQ-MASTER] Purchased | MonsterKills reset = 0");
        }
                TULLBUNG::ClassRuntime::
            OnBuildStateChanged();
return PurchaseNodeResult::kSuccess;
    }

    SetNodeEnabledResult SetNodeEnabled(
        NodeID a_node,
        bool a_enabled)
    {
        const auto* meta =
            FindNode(a_node);

        if (!meta) {
            return SetNodeEnabledResult::kInvalid;
        }

        const auto index =
            NodeIndex(a_node);

        if (index >= kNodeStorageSize) {
            return SetNodeEnabledResult::kInvalid;
        }

        if (!g_state.acquiredNodes.test(index)) {
            return SetNodeEnabledResult::kNotOwned;
        }

        const bool current =
            g_state.enabledNodes.test(index);

        if (current == a_enabled) {
            return SetNodeEnabledResult::kAlreadyInState;
        }

        g_state.enabledNodes.set(
            index,
            a_enabled);

                TULLBUNG::ClassRuntime::
            OnBuildStateChanged();
return SetNodeEnabledResult::kSuccess;
    }

    SetNodeEnabledResult ToggleNode(
        NodeID a_node)
    {
        const auto* meta =
            FindNode(a_node);

        if (!meta) {
            return SetNodeEnabledResult::kInvalid;
        }

        const auto index =
            NodeIndex(a_node);

        if (index >= kNodeStorageSize) {
            return SetNodeEnabledResult::kInvalid;
        }

        if (!g_state.acquiredNodes.test(index)) {
            return SetNodeEnabledResult::kNotOwned;
        }

        return SetNodeEnabled(
            a_node,
            !g_state.enabledNodes.test(index));
    }

    const NodeMeta* GetNodeMeta(
        NodeID a_node) noexcept
    {
        return FindNode(a_node);
    }

    void LogSummary()
    {
        logger::info(
            "[BuildState] HighestLevel={} | EarnedJP={} | DebugJP={} | SpentJP={} | AvailableJP={}",
            g_state.highestPlayerLevelSeen,
            g_state.totalEarnedJP,
            g_debugJP,
            g_state.spentJP,
            GetAvailableJP());
    }
}
