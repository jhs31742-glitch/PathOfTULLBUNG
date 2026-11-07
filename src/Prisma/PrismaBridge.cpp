#include "PCH.h"
#include "Profiles/ProfileService.h"
#include "Profiles/RuntimeBoundary.h"
#include "BuildState.h"
#include "ClassRuntime.h"
#include "Inquisitor.h"
#include "Hunter.h"
#include "Prisma/HudOverlay.h"
#include "DataCache.h"
#include "Persistence.h"
#include "PlayerState.h"
#include "Prisma/PrismaBridge.h"
#include "Prisma/PrismaUI_API.h"
#include "Prisma/FloatingTitleMinimal.h"
#include "UISettings.h"
#include "TitleRuntime.h"

#include "REX/W32/DINPUT.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>
#include <string>
#include <string_view>
#include <system_error>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cwctype>
#include <exception>
#include <Windows.h>
#include <imm.h>
#pragma comment(lib, "Imm32.lib")

namespace TULLBUNG::PrismaBridge
{
    namespace
    {
        using namespace TULLBUNG::BuildState;

        PRISMA_UI_API::IVPrismaUI1* g_api = nullptr;
        PRISMA_UI_API::IVPrismaUI2* g_apiV2 = nullptr;

        PrismaView g_view = 0;
        PrismaView g_voidHudView = 0;

        bool g_initialized = false;
        bool g_inputInstalled = false;
        bool g_gameReady = false;
        bool g_createPending = false;
        bool g_mainOpenRequested = false;
        bool g_domReady = false;
        // Session-only QA authority; never serialized or saved to UISettings.
        // UI hide is NOT QA exit. Explicit footer OFF/Developer Mode OFF is.
        bool g_s19DevExtensionUnlocked = false;
        std::uint64_t g_s19GapSequence = 0;
        bool g_titleCatalogSent = false;

        bool g_voidHudCreatePending = false;
        bool g_voidHudDomReady = false;
        bool g_hudEditorActive = false;

        constexpr const char* kViewPath =
            "pathoftullbung_debug/index.html";

        constexpr const char* kVoidHudViewPath =
            "pathoftullbung_hud/index.html";

        struct ClassUiMeta final
        {
            std::string_view key;
            std::string_view name;
            std::string_view group;
            std::string_view shortCode;
        };

        constexpr std::array<
            ClassUiMeta,
            kClassCount>
            kClassUiMeta{{
                { "warrior",    "전사",       "전사 계열",   "W"  },
                { "berserker",  "광전사",     "전사 계열",   "B"  },
                { "guardian",   "수호자",     "전사 계열",   "G"  },

                { "hunter",     "사냥꾼",     "사냥꾼 계열", "H"  },
                { "assassin",   "암살자",     "사냥꾼 계열", "A"  },
                { "marksman",   "명사수",     "사냥꾼 계열", "MM" },

                { "priest",     "사제",       "사제 계열",   "P"  },
                { "paladin",    "성기사",     "사제 계열",   "PL" },
                { "inquisitor", "이단심문관", "사제 계열",   "I"  },

                { "mage",       "마법사",     "마법사 계열", "MG" },
                { "void_mage",  "공허술사",   "마법사 계열", "V"  },
                { "siege_mage", "공성마도사", "마법사 계열", "S"  }
#if POT_QA15_EXTENSION
                ,{ "qa15_sentinel", "QA 이전검증 직업", "QA 확장계열", "QA" }
#endif
            }};

        void SendToUI(
            const char* a_payload)
        {
            if (!g_api ||
                !g_view ||
                !g_domReady ||
                !a_payload) {

                return;
            }

            if (!g_api->IsValid(g_view)) {
                return;
            }

            g_api->InteropCall(
                g_view,
                "tullReceive",
                a_payload);
        }

        void SendToVoidHud(
            const char* a_payload)
        {
            if (!g_api ||
                !g_voidHudView ||
                !g_voidHudDomReady ||
                !a_payload) {

                return;
            }

            if (!g_api->IsValid(
                    g_voidHudView)) {

                return;
            }

            g_api->InteropCall(
                g_voidHudView,
                "tullHudReceive",
                a_payload);
        }


        void SendVoidHudSnapshot()
        {
            if (!g_voidHudDomReady) {
                return;
            }

            const auto& settings =
                TULLBUNG::UISettings::Get();

            TULLBUNG::HudOverlay::
                SetGameReady(
                    g_gameReady);

            TULLBUNG::HudOverlay::
                SetHudSettings(
                    settings.hud);

            TULLBUNG::HudOverlay::
                RefreshRuntimeVisibilityFromGame();

            const bool voidOwned =
                TULLBUNG::BuildState::
                    IsNodeActive(
                        TULLBUNG::BuildState::
                            NodeID::
                                kVoidMageVoid);

            const bool composureOwned =
                TULLBUNG::BuildState::
                    IsNodeActive(
                        TULLBUNG::BuildState::
                            NodeID::
                                kMarksmanComposure);

            const bool quietOwned =
                TULLBUNG::BuildState::
                    IsNodeActive(
                        TULLBUNG::BuildState::
                            NodeID::
                                kAssassinSilentHandling);

            const auto voidCount =
                (std::min)(
                    TULLBUNG::Persistence::
                        GetVoidProgress().
                            voidCount,
                    static_cast<
                        std::uint8_t>(4));

            TULLBUNG::HudOverlay::
                SetVoidState(
                    voidOwned,
                    voidCount);

            TULLBUNG::HudOverlay::
                SetComposureState(
                    composureOwned,
                    TULLBUNG::Classes::
                        Hunter::
                            IsComposureReady());

            TULLBUNG::HudOverlay::
                SetQuietState(
                    quietOwned,
                    TULLBUNG::Classes::
                        Hunter::
                            IsQuietProcessingReady());

            const auto snapshot =
                TULLBUNG::HudOverlay::
                    GetSnapshot();

            const bool mainViewVisible =
                g_api &&
                g_view &&
                g_api->IsValid(g_view) &&
                !g_api->IsHidden(g_view);

            const bool viewVisible =
                snapshot.runtime.AllowsHud() &&
                snapshot.settings.masterVisible &&
                !g_hudEditorActive;

            if (g_api &&
                g_voidHudView &&
                g_api->IsValid(
                    g_voidHudView)) {

                if (viewVisible) {
                    if (g_api->IsHidden(
                            g_voidHudView)) {

                        g_api->Show(
                            g_voidHudView);
                    }
                }
                else if (!g_api->IsHidden(
                             g_voidHudView)) {

                    g_api->Hide(
                        g_voidHudView);
                }
            }

            // Transition-only HUD observation; do not change visibility policy.
            const bool hudViewValid =
                g_api && g_voidHudView &&
                g_api->IsValid(g_voidHudView);
            const bool hudViewHidden =
                !hudViewValid || g_api->IsHidden(g_voidHudView);

            const std::array<bool, 15> hudTrace{{
                snapshot.runtime.gameReady,
                snapshot.runtime.gameplayContext,
                snapshot.runtime.skyrimUIVisible,
                snapshot.settings.masterVisible,
                g_hudEditorActive,
                mainViewVisible,
                viewVisible,
                hudViewValid,
                hudViewHidden,
                snapshot.gameplay.voidOwned,
                snapshot.gameplay.composureOwned,
                snapshot.gameplay.quietOwned,
                TULLBUNG::HudOverlay::ShouldRenderVoid(snapshot),
                TULLBUNG::HudOverlay::ShouldRenderComposure(snapshot),
                TULLBUNG::HudOverlay::ShouldRenderQuiet(snapshot)
            }};

            static bool traceInitialized = false;
            static std::array<bool, 15> previousHudTrace{};

            if (!traceInitialized || hudTrace != previousHudTrace) {
                logger::info(
                    "[S18HUD][VIEW] ready={} gameplay={} skyrimUI={} master={} editor={} mainShown={} targetShown={} valid={} hidden={} voidOwned={} composureOwned={} quietOwned={} voidRender={} composureRender={} quietRender={}",
                    snapshot.runtime.gameReady,
                    snapshot.runtime.gameplayContext,
                    snapshot.runtime.skyrimUIVisible,
                    snapshot.settings.masterVisible,
                    g_hudEditorActive,
                    mainViewVisible,
                    viewVisible,
                    hudViewValid,
                    hudViewHidden,
                    snapshot.gameplay.voidOwned,
                    snapshot.gameplay.composureOwned,
                    snapshot.gameplay.quietOwned,
                    TULLBUNG::HudOverlay::ShouldRenderVoid(snapshot),
                    TULLBUNG::HudOverlay::ShouldRenderComposure(snapshot),
                    TULLBUNG::HudOverlay::ShouldRenderQuiet(snapshot));

                previousHudTrace = hudTrace;
                traceInitialized = true;
            }

            const auto boolText =
                [](bool a_value) {
                    return
                        a_value ?
                            "true" :
                            "false";
                };

            std::string payload =
                R"({"kind":"hud-overlay")";

            payload +=
                R"(,"runtimeVisible":)" +
                std::string(
                    boolText(
                        snapshot.runtime.
                            AllowsHud()));

            payload +=
                R"(,"masterVisible":)" +
                std::string(
                    boolText(
                        snapshot.settings.
                            masterVisible));

            payload +=
                R"(,"void":{"visible":)" +
                std::string(
                    boolText(
                        snapshot.settings.
                            voidHud.visible));

            payload +=
                R"(,"active":)" +
                std::string(
                    boolText(
                        TULLBUNG::HudOverlay::
                            ShouldRenderVoid(
                                snapshot)));

            payload +=
                R"(,"count":)" +
                std::to_string(
                    static_cast<
                        std::uint32_t>(
                            snapshot.gameplay.
                                voidCount));

            payload +=
                R"(,"x":)" +
                std::to_string(
                    snapshot.settings.
                        voidHud.x);

            payload +=
                R"(,"y":)" +
                std::to_string(
                    snapshot.settings.
                        voidHud.y);

            payload +=
                R"(,"scale":)" +
                std::to_string(
                    snapshot.settings.
                        voidHud.scale);

            payload +=
                R"(,"opacity":)" +
                std::to_string(
                    snapshot.settings.
                        voidHud.opacity);

            payload +=
                R"(},"composure":{"visible":)" +
                std::string(
                    boolText(
                        snapshot.settings.
                            composureHud.visible));

            payload +=
                R"(,"active":)" +
                std::string(
                    boolText(
                        TULLBUNG::HudOverlay::
                            ShouldRenderComposure(
                                snapshot)));

            payload +=
                R"(,"x":)" +
                std::to_string(
                    snapshot.settings.
                        composureHud.x);

            payload +=
                R"(,"y":)" +
                std::to_string(
                    snapshot.settings.
                        composureHud.y);

            payload +=
                R"(,"scale":)" +
                std::to_string(
                    snapshot.settings.
                        composureHud.scale);

            payload +=
                R"(,"opacity":)" +
                std::to_string(
                    snapshot.settings.
                        composureHud.opacity);

            payload +=
                R"(},"quiet":{"visible":)" +
                std::string(
                    boolText(
                        snapshot.settings.
                            quietProcessingHud.
                                visible));

            payload +=
                R"(,"active":)" +
                std::string(
                    boolText(
                        TULLBUNG::HudOverlay::
                            ShouldRenderQuiet(
                                snapshot)));

            payload +=
                R"(,"x":)" +
                std::to_string(
                    snapshot.settings.
                        quietProcessingHud.x);

            payload +=
                R"(,"y":)" +
                std::to_string(
                    snapshot.settings.
                        quietProcessingHud.y);

            payload +=
                R"(,"scale":)" +
                std::to_string(
                    snapshot.settings.
                        quietProcessingHud.
                            scale);

            payload +=
                R"(,"opacity":)" +
                std::to_string(
                    snapshot.settings.
                        quietProcessingHud.
                            opacity);

            payload += "}}";

            SendToVoidHud(
                payload.c_str());
        }

        void SendDevStatus(
            std::string_view a_text)
        {
            std::string payload =
                R"({"kind":"dev-status","text":")";

            payload.append(
                a_text.data(),
                a_text.size());

            payload += "\"}";

            SendToUI(
                payload.c_str());
        }

        void SendTitleStateSnapshot();

        void SendBuildStateSnapshot()
        {
            if (!g_domReady) {
                return;
            }

            std::string payload;
            payload.reserve(20000);

            SendVoidHudSnapshot();

            payload +=
                R"({"kind":"build-state")";

            payload +=
                ",\"highestLevel\":" +
                std::to_string(
                    GetHighestPlayerLevelSeen());

            payload +=
                ",\"earnedJP\":" +
                std::to_string(
                    GetTotalEarnedJP());

            payload +=
                ",\"debugJP\":" +
                std::to_string(
                    GetDebugJP());

            payload +=
                ",\"spentJP\":" +
                std::to_string(
                    GetSpentJP());

            payload +=
                ",\"retainedQAJP\":" + std::to_string(kQA15ExtensionEnabled ? 0 : GetRetainedQAState().spent) +
                ",\"qa15Catalogue\":" + std::string(kQA15ExtensionEnabled ? "true" : "false") +
                ",\"availableJP\":" +
                std::to_string(
                    GetAvailableJP());

            const auto inquisitorKills =
                (std::min)(
                    TULLBUNG::Persistence::
                        GetInquisitorProgress().
                            monsterKills,
                    500u);

            const auto smiteDamage =
                TULLBUNG::Classes::
                    Inquisitor::
                        GetSmiteDamage();

            payload +=
                ",\"inquisitorKills\":" +
                std::to_string(
                    inquisitorKills);

            payload +=
                ",\"smiteDamage\":" +
                std::to_string(
                    smiteDamage);

            payload +=
                ",\"voidCount\":" +
                std::to_string(
                    static_cast<
                        std::uint32_t>(
                            TULLBUNG::Persistence::
                                GetVoidProgress().
                                    voidCount));

            payload +=
                ",\"classes\":[";

            for (std::size_t i = 0;
                 i < kClassCount;
                 ++i) {

                if (i != 0) {
                    payload += ",";
                }

                const auto classID =
                    static_cast<ClassID>(i);

                const auto& ui =
                    kClassUiMeta[i];

                payload +=
                    "{\"id\":" +
                    std::to_string(i);

                payload +=
                    ",\"key\":\"";

                payload.append(
                    ui.key.data(),
                    ui.key.size());

                payload +=
                    "\",\"name\":\"";

                payload.append(
                    ui.name.data(),
                    ui.name.size());

                payload +=
                    "\",\"group\":\"";

                payload.append(
                    ui.group.data(),
                    ui.group.size());

                payload +=
                    "\",\"shortCode\":\"";

                payload.append(
                    ui.shortCode.data(),
                    ui.shortCode.size());

                payload += "\"";

                payload +=
                    ",\"owned\":";

                payload +=
                    HasClass(classID) ?
                        "true" :
                        "false";

                payload +=
                    ",\"unlocked\":";

                payload +=
                    IsClassUnlocked(classID) ?
                        "true" :
                        "false";

                payload +=
                    ",\"masterUnlocked\":";

                payload +=
                    IsMasterUnlocked(classID) ?
                        "true" :
                        "false";

                payload +=
                    ",\"level\":" +
                    std::to_string(
                        GetClassLevel(classID));

                payload += "}";
            }

            payload +=
                "],\"nodes\":[";

            bool firstNode = true;

            std::array<
                std::uint32_t,
                kClassCount>
                nodeOrdinals{};

            for (std::size_t raw = 0;
                 raw < kNodeStorageSize;
                 ++raw) {

                const auto nodeID =
                    static_cast<NodeID>(raw);

                const auto* meta =
                    GetNodeMeta(nodeID);

                if (!meta) {
                    continue;
                }

                const auto classIndex =
                    static_cast<std::size_t>(
                        meta->classID);

                if (classIndex >=
                    kClassCount) {

                    continue;
                }

                const auto ordinal =
                    ++nodeOrdinals[
                        classIndex];

                if (!firstNode) {
                    payload += ",";
                }

                firstNode = false;

                payload +=
                    "{\"id\":" +
                    std::to_string(raw);

                payload +=
                    ",\"classId\":" +
                    std::to_string(
                        classIndex);

                payload +=
                    ",\"cost\":" +
                    std::to_string(
                        meta->cost);

                payload +=
                    ",\"master\":";

                payload +=
                    meta->master ?
                        "true" :
                        "false";

                payload +=
                    ",\"key\":\"";

                payload.append(
                    meta->key.data(),
                    meta->key.size());

                payload +=
                    "\",\"code\":\"";

                const auto& shortCode =
                    kClassUiMeta[
                        classIndex]
                        .shortCode;

                payload.append(
                    shortCode.data(),
                    shortCode.size());

                payload +=
                    std::to_string(
                        ordinal);

                payload += "\"";

                payload +=
                    ",\"owned\":";

                payload +=
                    HasNode(nodeID) ?
                        "true" :
                        "false";

                payload +=
                    ",\"enabled\":";

                payload +=
                    IsNodeEnabled(nodeID) ?
                        "true" :
                        "false";

                payload +=
                    ",\"active\":";

                payload +=
                    IsNodeActive(nodeID) ?
                        "true" :
                        "false";

                payload += "}";
            }

            payload += "]}";

            SendToUI(
                payload.c_str());

            if (g_titleCatalogSent) {
                SendTitleStateSnapshot();
            }
        }

        bool ParseCommandNumber(
            std::string_view a_command,
            std::string_view a_prefix,
            std::uint32_t& a_value)
        {
            if (!a_command.starts_with(
                    a_prefix)) {

                return false;
            }

            const auto suffix =
                a_command.substr(
                    a_prefix.size());

            if (suffix.empty()) {
                return false;
            }

            const auto result =
                std::from_chars(
                    suffix.data(),
                    suffix.data() +
                        suffix.size(),
                    a_value);

            return
                result.ec == std::errc{} &&
                result.ptr ==
                    suffix.data() +
                        suffix.size();
        }

        template <class T>
        T* LookupDevForm(
            RE::FormID a_localFormID,
            std::string_view a_plugin)
        {
            auto* data =
                RE::TESDataHandler::
                    GetSingleton();

            if (!data) {
                return nullptr;
            }

            return data->LookupForm<T>(
                a_localFormID,
                a_plugin);
        }

        bool GiveObject(
            RE::TESBoundObject* a_object,
            std::int32_t a_count)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                !a_object ||
                a_count <= 0) {

                return false;
            }

            player->AddObjectToContainer(
                a_object,
                nullptr,
                a_count,
                nullptr);

            return true;
        }

        void GiveDaggerSet()
        {
            auto* dagger =
                LookupDevForm<
                    RE::TESObjectWEAP>(
                        0x0001397E,
                        "Skyrim.esm");

            if (GiveObject(
                    dagger,
                    1)) {

                SendDevStatus(
                    "철 단검 1개 지급");
            }
            else {

                SendDevStatus(
                    "철 단검 지급 실패");
            }
        }

        void GiveBowSet()
        {
            auto* bow =
                LookupDevForm<
                    RE::TESObjectWEAP>(
                        0x00013985,
                        "Skyrim.esm");

            auto* arrows =
                LookupDevForm<
                    RE::TESAmmo>(
                        0x0001397D,
                        "Skyrim.esm");

            const bool bowOK =
                GiveObject(
                    bow,
                    1);

            const bool ammoOK =
                GiveObject(
                    arrows,
                    100);

            if (bowOK &&
                ammoOK) {

                SendDevStatus(
                    "사냥활 1개 + 철 화살 100개 지급");
            }
            else {

                SendDevStatus(
                    "활 세트 지급 실패");
            }
        }

        void GiveCrossbowSet()
        {
            auto* crossbow =
                LookupDevForm<
                    RE::TESObjectWEAP>(
                        0x00000801,
                        "Dawnguard.esm");

            auto* bolts =
                LookupDevForm<
                    RE::TESAmmo>(
                        0x00000BB3,
                        "Dawnguard.esm");

            const bool crossbowOK =
                GiveObject(
                    crossbow,
                    1);

            const bool ammoOK =
                GiveObject(
                    bolts,
                    100);

            if (crossbowOK &&
                ammoOK) {

                SendDevStatus(
                    "석궁 1개 + 강철 볼트 100개 지급");
            }
            else {

                SendDevStatus(
                    "석궁 세트 지급 실패");
            }
        }


        void GiveMaceShieldSet()
        {
            auto* mace =
                LookupDevForm<
                    RE::TESObjectWEAP>(
                        0x00013982,
                        "Skyrim.esm");

            auto* shield =
                LookupDevForm<
                    RE::TESObjectARMO>(
                        0x00012EB6,
                        "Skyrim.esm");

            const bool maceOK =
                GiveObject(
                    mace,
                    1);

            const bool shieldOK =
                GiveObject(
                    shield,
                    1);

            SendDevStatus(
                maceOK &&
                shieldOK ?
                    "철퇴 1개 + 철 방패 1개 지급" :
                    "철퇴 / 철 방패 지급 실패");
        }



        // S19 B6 QA-only standard Skyrim.esm loadout additions.
        // No broad ActorValue reads and no implicit weapon equip.
        void GiveS19Weapon(RE::FormID a_localId,
            std::string_view a_label, std::int32_t a_count = 1)
        {
            auto* weapon = LookupDevForm<RE::TESObjectWEAP>(
                a_localId, "Skyrim.esm");
            if (!weapon) {
                logger::warn("[S19DEV][GIVE] Weapon lookup/type failed | Form={:08X}",
                    static_cast<std::uint32_t>(a_localId));
                SendDevStatus(std::string(a_label) + " Form lookup failed");
                return;
            }
            if (!GiveObject(weapon, a_count)) {
                SendDevStatus(std::string(a_label) + " Give failed");
                return;
            }
            logger::info("[S19DEV][GIVE] Weapon Form={:08X} Count={} PASS",
                static_cast<std::uint32_t>(a_localId), a_count);
            SendDevStatus(std::string(a_label) + " 지급됨 · 장착은 수동");
        }

        void GiveS19ArmorPair(RE::FormID a_bodyId,
            RE::FormID a_bootsId, std::string_view a_label)
        {
            // Resolve both before any inventory mutation.
            auto* body = LookupDevForm<RE::TESObjectARMO>(
                a_bodyId, "Skyrim.esm");
            auto* boots = LookupDevForm<RE::TESObjectARMO>(
                a_bootsId, "Skyrim.esm");
            if (!body || !boots) {
                logger::warn("[S19DEV][GIVE] Armor lookup/type failed | Body={:08X} Boots={:08X}",
                    static_cast<std::uint32_t>(a_bodyId),
                    static_cast<std::uint32_t>(a_bootsId));
                SendDevStatus(std::string(a_label) + " Form lookup failed");
                return;
            }
            const bool bodyOK = GiveObject(body, 1);
            const bool bootsOK = GiveObject(boots, 1);
            logger::info("[S19DEV][GIVE] ArmorPair Body={:08X} Boots={:08X} Body={} Boots={}",
                static_cast<std::uint32_t>(a_bodyId),
                static_cast<std::uint32_t>(a_bootsId), bodyOK, bootsOK);
            SendDevStatus(std::string(a_label) +
                (bodyOK && bootsOK ? " 지급됨 · 착용은 수동" :
                " 일부 실패 · 기준 세이브 확인"));
        }

        void GiveStage15SpellSet()
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player) {
                SendDevStatus(
                    "Stage15 주문 지급 실패");

                return;
            }

            const std::array<
                RE::SpellItem*,
                8> spells{
                    LookupDevForm<
                        RE::SpellItem>(
                            0x00012FCC,
                            "Skyrim.esm"),
                    LookupDevForm<
                        RE::SpellItem>(
                            0x00012FCD,
                            "Skyrim.esm"),
                    LookupDevForm<
                        RE::SpellItem>(
                            0x00012FD0,
                            "Skyrim.esm"),
                    LookupDevForm<
                        RE::SpellItem>(
                            0x0002DD29,
                            "Skyrim.esm"),
                    LookupDevForm<
                        RE::SpellItem>(
                            0x0001C789,
                            "Skyrim.esm"),
                    LookupDevForm<
                        RE::SpellItem>(
                            0x00035D7F,
                            "Skyrim.esm"),
                    LookupDevForm<
                        RE::SpellItem>(
                            0x0005DB90,
                            "Skyrim.esm"),
                    LookupDevForm<
                        RE::SpellItem>(
                            0x000211EB,
                            "Skyrim.esm")
                };

            for (auto* spell : spells) {
                if (!spell) {
                    logger::error(
                        "[DEV_S15_SPELLSET] Result=LOOKUP_FAIL");

                    SendDevStatus(
                        "Stage15 주문 Form 조회 실패");

                    return;
                }
            }

            for (auto* spell : spells) {
                (void)player->
                    AddSpell(
                        spell);
            }

            logger::info(
                "[DEV_S15_SPELLSET] Result=PASS | Count=8");

            SendDevStatus(
                "Stage15 바닐라 주문 8종 지급");
        }

        enum class QABuildPreset : std::uint8_t
        {
            kNone,

            kPriestHealingArts,
            kPriestCrusade,
            kPriestFervor,
            kPriestFocusedHealing,
            kPriestMaster,

            kPaladinMaceMastery,
            kPaladinOath,
            kPaladinHolyStrike,
            kPaladinWeight,
            kPaladinMaster,

            kInquisitorMonsterStudy,
            kInquisitorArtifactStudy,
            kInquisitorDefense,
            kInquisitorSmite,
            kInquisitorMaster
        };

        enum class QALoadoutPreset : std::uint8_t
        {
            kNeutral,
            kMaceHeal,
            kMaceShield,
            kHealFlames,
            kDoubleHeal,
            kFlamesShield
        };

        enum class QAStatePreset : std::uint8_t
        {
            kBaseline,
            kHP50,
            kHP79,
            kHP80,
            kResourcesHalf
        };

        struct QAProfileSpec final
        {
            std::string_view name{};
            QABuildPreset build{
                QABuildPreset::kNone
            };
            QALoadoutPreset loadout{
                QALoadoutPreset::kNeutral
            };
            QAStatePreset state{
                QAStatePreset::kBaseline
            };
            bool selectSmite{};
        };

        inline constexpr std::array<
            QAProfileSpec,
            18> kQAProfiles{{
            {
                "priest.healing_arts",
                QABuildPreset::
                    kPriestHealingArts,
                QALoadoutPreset::
                    kMaceHeal,
                QAStatePreset::
                    kHP50,
                false
            },
            {
                "priest.crusade.weapon",
                QABuildPreset::
                    kPriestCrusade,
                QALoadoutPreset::
                    kMaceHeal,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "priest.crusade.spell",
                QABuildPreset::
                    kPriestCrusade,
                QALoadoutPreset::
                    kHealFlames,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "priest.fervor.79",
                QABuildPreset::
                    kPriestFervor,
                QALoadoutPreset::
                    kMaceHeal,
                QAStatePreset::
                    kHP79,
                false
            },
            {
                "priest.fervor.80",
                QABuildPreset::
                    kPriestFervor,
                QALoadoutPreset::
                    kMaceHeal,
                QAStatePreset::
                    kHP80,
                false
            },
            {
                "priest.focused_healing",
                QABuildPreset::
                    kPriestFocusedHealing,
                QALoadoutPreset::
                    kDoubleHeal,
                QAStatePreset::
                    kHP50,
                false
            },
            {
                "priest.master",
                QABuildPreset::
                    kPriestMaster,
                QALoadoutPreset::
                    kMaceHeal,
                QAStatePreset::
                    kResourcesHalf,
                false
            },

            {
                "paladin.mace",
                QABuildPreset::
                    kPaladinMaceMastery,
                QALoadoutPreset::
                    kMaceShield,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "paladin.oath.shield",
                QABuildPreset::
                    kPaladinOath,
                QALoadoutPreset::
                    kMaceShield,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "paladin.oath.heal",
                QABuildPreset::
                    kPaladinOath,
                QALoadoutPreset::
                    kMaceHeal,
                QAStatePreset::
                    kHP50,
                false
            },
            {
                "paladin.holy.base",
                QABuildPreset::
                    kPaladinHolyStrike,
                QALoadoutPreset::
                    kMaceShield,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "paladin.weight",
                QABuildPreset::
                    kPaladinWeight,
                QALoadoutPreset::
                    kMaceShield,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "paladin.holy.master",
                QABuildPreset::
                    kPaladinMaster,
                QALoadoutPreset::
                    kMaceShield,
                QAStatePreset::
                    kBaseline,
                false
            },

            {
                "inq.monster.weapon",
                QABuildPreset::
                    kInquisitorMonsterStudy,
                QALoadoutPreset::
                    kMaceShield,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "inq.monster.spell",
                QABuildPreset::
                    kInquisitorMonsterStudy,
                QALoadoutPreset::
                    kFlamesShield,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "inq.artifact",
                QABuildPreset::
                    kInquisitorArtifactStudy,
                QALoadoutPreset::
                    kNeutral,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "inq.defense",
                QABuildPreset::
                    kInquisitorDefense,
                QALoadoutPreset::
                    kNeutral,
                QAStatePreset::
                    kBaseline,
                false
            },
            {
                "inq.smite",
                QABuildPreset::
                    kInquisitorSmite,
                QALoadoutPreset::
                    kNeutral,
                QAStatePreset::
                    kBaseline,
                true
            }
        }};

        inline constexpr QAProfileSpec
            kQAGrandInquisitorProfile{
                "inq.master",
                QABuildPreset::
                    kInquisitorMaster,
                QALoadoutPreset::
                    kNeutral,
                QAStatePreset::
                    kBaseline,
                true
            };

        [[nodiscard]] RE::BGSEquipSlot*
            LookupQAEquipSlot(
                std::string_view a_editorID)
        {
            return RE::TESForm::
                LookupByEditorID<
                    RE::BGSEquipSlot>(
                        a_editorID);
        }

        template <class T>
        [[nodiscard]] T*
            LookupQAForm(
                std::string_view a_editorID,
                RE::FormID a_fallbackID = 0,
                std::string_view a_plugin =
                    "Skyrim.esm")
        {
            if (auto* form =
                    RE::TESForm::
                        LookupByEditorID<T>(
                            a_editorID)) {

                return form;
            }

            if (a_fallbackID == 0) {
                return nullptr;
            }

            return LookupDevForm<T>(
                a_fallbackID,
                a_plugin);
        }

        [[nodiscard]] bool
            EnsureQAObject(
                RE::TESBoundObject* a_object)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                !a_object) {

                return false;
            }

            if (player->GetItemCount(
                    a_object) <= 0) {

                player->
                    AddObjectToContainer(
                        a_object,
                        nullptr,
                        1,
                        nullptr);
            }

            return
                player->GetItemCount(
                    a_object) > 0;
        }

        [[nodiscard]] bool
            EquipQAObject(
                RE::TESBoundObject* a_object,
                RE::BGSEquipSlot* a_slot)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            auto* equip =
                RE::ActorEquipManager::
                    GetSingleton();

            if (!player ||
                !equip ||
                !a_object ||
                !a_slot ||
                !EnsureQAObject(
                    a_object)) {

                return false;
            }

            equip->EquipObject(
                player,
                a_object,
                nullptr,
                1,
                a_slot,
                false,
                true,
                false,
                true);

            return true;
        }

        [[nodiscard]] bool
            EquipQASpell(
                RE::SpellItem* a_spell,
                RE::BGSEquipSlot* a_slot)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            auto* equip =
                RE::ActorEquipManager::
                    GetSingleton();

            if (!player ||
                !equip ||
                !a_spell ||
                !a_slot) {

                return false;
            }

            (void)player->AddSpell(
                a_spell);

            equip->EquipSpell(
                player,
                a_spell,
                a_slot);

            return true;
        }

        [[nodiscard]] bool
            ApplyNeutralQALoadout()
        {
            auto* rightSlot =
                LookupQAEquipSlot(
                    "RightHand");

            auto* leftSlot =
                LookupQAEquipSlot(
                    "LeftHand");

            auto* dagger =
                LookupQAForm<
                    RE::TESObjectWEAP>(
                        "IronDagger",
                        0x0001397E);

            auto* shield =
                LookupQAForm<
                    RE::TESObjectARMO>(
                        "ArmorIronShield",
                        0x00012EB6);

            const bool rightOK =
                EquipQAObject(
                    dagger,
                    rightSlot);

            const bool leftOK =
                EquipQAObject(
                    shield,
                    leftSlot);

            return
                rightOK &&
                leftOK;
        }

        void LogS19GAPSnapshot(std::uint64_t a_seq)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                logger::error("[S19QA-GAP] Event=SNAPSHOT_FAILED | Seq={} | Reason=PLAYER_MISSING", a_seq);
                return;
            }
            // GAP records a boundary, not post-clear ActorValue measurements.
            logger::info(
                "[S19QA-SNAPSHOT] Seq={} | AvailableJP={}",
                a_seq,
                GetAvailableJP());
            for (std::uint16_t classIndex = 0;
                 classIndex < kClassCount; ++classIndex) {
                std::uint8_t owned = 0;
                std::uint8_t enabled = 0;
                std::uint8_t active = 0;
                for (std::uint16_t offset = 0; offset < 6; ++offset) {
                    const auto node = static_cast<NodeID>(classIndex * 0x10 + offset);
                    if (!GetNodeMeta(node)) continue;
                    const auto flag = static_cast<std::uint8_t>(1U << offset);
                    if (HasNode(node)) owned |= flag;
                    if (IsNodeEnabled(node)) enabled |= flag;
                    if (IsNodeActive(node)) active |= flag;
                }
                logger::info(
                    "[S19QA-SNAPSHOT] Seq={} | Class={} | Acquired={} | OwnedMask={:02X} | EnabledMask={:02X} | ActiveMask={:02X}",
                    a_seq, classIndex,
                    HasClass(static_cast<ClassID>(classIndex)),
                    owned, enabled, active);
            }
        }
        [[nodiscard]] bool ClearCombatEquipment()
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            auto* equip =
                RE::ActorEquipManager::
                    GetSingleton();

            auto* rightSlot =
                LookupQAEquipSlot(
                    "RightHand");

            auto* leftSlot =
                LookupQAEquipSlot(
                    "LeftHand");

            auto* dagger =
                LookupQAForm<
                    RE::TESObjectWEAP>(
                        "IronDagger",
                        0x0001397E);

            auto* shield =
                LookupQAForm<
                    RE::TESObjectARMO>(
                        "ArmorIronShield",
                        0x00012EB6);

            if (!player ||
                !equip ||
                !rightSlot ||
                !leftSlot ||
                !dagger ||
                !shield) {

                logger::error(
                    "[DEV_CLEAR_COMBAT] Result=PRECONDITION_FAIL | Player={} | Equip={} | RightSlot={} | LeftSlot={} | Dagger={} | Shield={}",
                    player != nullptr,
                    equip != nullptr,
                    rightSlot != nullptr,
                    leftSlot != nullptr,
                    dagger != nullptr,
                    shield != nullptr);

                SendDevStatus(
                    "전투 장비 비우기 실패");

                return false;
            }

            // Reuse the already validated neutral physical loadout.
            // This overwrites prior weapon/spell/shield hand state first.
            if (!ApplyNeutralQALoadout()) {

                logger::error(
                    "[DEV_CLEAR_COMBAT] Result=NEUTRAL_OVERWRITE_FAIL");

                SendDevStatus(
                    "전투 장비 비우기 실패");

                return false;
            }

            const bool rightOK =
                equip->UnequipObject(
                    player,
                    dagger,
                    nullptr,
                    1,
                    rightSlot,
                    false,
                    true,
                    false,
                    true,
                    nullptr);

            const bool leftOK =
                equip->UnequipObject(
                    player,
                    shield,
                    nullptr,
                    1,
                    leftSlot,
                    false,
                    true,
                    false,
                    true,
                    nullptr);

            bool ammoOK = true;

            if (auto* ammo =
                    player->GetCurrentAmmo()) {

                ammoOK =
                    equip->UnequipObject(
                        player,
                        ammo,
                        nullptr,
                        1,
                        nullptr,
                        false,
                        true,
                        false,
                        true,
                        nullptr);
            }

            TULLBUNG::PlayerState::
                RefreshAll();

            TULLBUNG::ClassRuntime::
                OnBuildStateChanged();

            // Read actual equipped forms after the existing refresh.
            // GetEquippedObject covers both weapon and spell hand states.
            auto* postRight = player->GetEquippedObject(false);
            auto* postLeft = player->GetEquippedObject(true);
            auto* postAmmo = player->GetCurrentAmmo();
            const bool shieldPost = TULLBUNG::PlayerState::GetEquipment().hasShield;
            const bool emptyPost = !postRight && !postLeft && !postAmmo && !shieldPost;
            const bool ok = emptyPost; // Actual verified slot state is GAP authority; API returns remain diagnostic.

            logger::info(
                "[DEV_CLEAR_COMBAT] Result={} | Right={} | Left={} | Ammo={} | PostRight={:08X} | PostLeft={:08X} | PostAmmo={:08X} | PostShield={} | EmptyPost={}",
                ok ? "PASS" : "PARTIAL_FAIL",
                rightOK,
                leftOK,
                ammoOK,
                postRight ? postRight->GetFormID() : 0,
                postLeft ? postLeft->GetFormID() : 0,
                postAmmo ? postAmmo->GetFormID() : 0,
                shieldPost,
                emptyPost);

            SendDevStatus(
                ok ?
                    "전투 장비 비움 완료" :
                    "전투 장비 비우기 일부 실패");
            return ok;
        }

        [[nodiscard]] bool
            ApplyQALoadout(
                QALoadoutPreset a_preset)
        {
            auto* rightSlot =
                LookupQAEquipSlot(
                    "RightHand");

            auto* leftSlot =
                LookupQAEquipSlot(
                    "LeftHand");

            if (!rightSlot ||
                !leftSlot) {

                logger::error(
                    "[QA_LOADOUT] Equip slots missing | Right={} | Left={}",
                    rightSlot != nullptr,
                    leftSlot != nullptr);

                return false;
            }

            // Always overwrite both hands with a neutral
            // physical loadout first. This prevents a spell
            // from the previous QA profile leaking forward.
            if (!ApplyNeutralQALoadout()) {
                logger::error(
                    "[QA_LOADOUT] Neutral hand reset failed");

                return false;
            }

            auto* mace =
                LookupQAForm<
                    RE::TESObjectWEAP>(
                        "IronMace",
                        0x00013982);

            auto* healing =
                LookupQAForm<
                    RE::SpellItem>(
                        "Healing",
                        0x00012FCC);

            auto* flames =
                LookupQAForm<
                    RE::SpellItem>(
                        "Flames",
                        0x00012FCD);

            bool ok = true;

            switch (a_preset) {
            case QALoadoutPreset::
                    kNeutral:
                break;

            case QALoadoutPreset::
                    kMaceHeal:
                ok =
                    EquipQAObject(
                        mace,
                        rightSlot) &&
                    EquipQASpell(
                        healing,
                        leftSlot);
                break;

            case QALoadoutPreset::
                    kMaceShield:
                ok =
                    EquipQAObject(
                        mace,
                        rightSlot);
                break;

            case QALoadoutPreset::
                    kHealFlames:
                ok =
                    EquipQASpell(
                        healing,
                        leftSlot) &&
                    EquipQASpell(
                        flames,
                        rightSlot);
                break;

            case QALoadoutPreset::
                    kDoubleHeal:
                ok =
                    EquipQASpell(
                        healing,
                        leftSlot) &&
                    EquipQASpell(
                        healing,
                        rightSlot);
                break;

            case QALoadoutPreset::
                    kFlamesShield:
                ok =
                    EquipQASpell(
                        flames,
                        rightSlot);
                break;
            }

            TULLBUNG::PlayerState::
                RefreshAll();

            TULLBUNG::ClassRuntime::
                OnBuildStateChanged();

            logger::info(
                "[QA_LOADOUT] Preset={} | Result={}",
                static_cast<unsigned int>(
                    a_preset),
                ok ?
                    "PASS" :
                    "FAIL");

            return ok;
        }


        void EquipStage15Spell(
            RE::FormID a_formID,
            bool a_leftHand,
            std::string_view a_name)
        {
            auto* slot =
                LookupQAEquipSlot(
                    a_leftHand ?
                        "LeftHand" :
                        "RightHand");

            auto* spell =
                LookupDevForm<
                    RE::SpellItem>(
                        a_formID,
                        "Skyrim.esm");

            const bool ok =
                EquipQASpell(
                    spell,
                    slot);

            logger::info(
                "[DEV_S15_SPELL] Spell={:08X} | Hand={} | Result={}",
                static_cast<std::uint32_t>(
                    a_formID),
                a_leftHand ?
                    "LEFT" :
                    "RIGHT",
                ok ?
                    "PASS" :
                    "FAIL");

            std::string status(
                a_name);

            status +=
                a_leftHand ?
                    " · 왼손 장착" :
                    " · 오른손 장착";

            if (!ok) {
                status +=
                    " 실패";
            }

            SendDevStatus(
                status);
        }

        [[nodiscard]] bool
            SetQABaseActorValue(
                RE::ActorValue a_value,
                float a_target)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                !std::isfinite(
                    a_target)) {

                return false;
            }

            auto* owner =
                player->AsActorValueOwner();

            if (!owner) {
                return false;
            }

            owner->SetActorValue(
                a_value,
                a_target);

            return true;
        }

        [[nodiscard]] bool
            SetQAResourceCurrent(
                RE::ActorValue a_value,
                float a_target)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                !std::isfinite(
                    a_target)) {

                return false;
            }

            auto* owner =
                player->AsActorValueOwner();

            if (!owner) {
                return false;
            }

            // GetActorValueMax() is intentionally not used here.
            // On SE 1.5.97 the QA runtime path crashed inside
            // Actor::GetActorValueMax for Health.
            // Restore to full first, then read the safe current AV.
            owner->RestoreActorValue(
                a_value,
                1000000.0f);

            const float full =
                player->GetActorValue(
                    a_value);

            if (!std::isfinite(
                    full) ||
                full <= 0.0f) {

                return false;
            }

            const float target =
                std::clamp(
                    a_target,
                    0.0f,
                    full);

            const float delta =
                target -
                full;

            if (delta < -0.001f) {
                owner->ModActorValue(
                    RE::ACTOR_VALUE_MODIFIERS::
                        kDamage,
                    a_value,
                    delta);
            }

            return true;
        }

        [[nodiscard]] bool
            SetQAResourcePercent(
                RE::ActorValue a_value,
                float a_percent)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                !std::isfinite(
                    a_percent)) {

                return false;
            }

            auto* owner =
                player->AsActorValueOwner();

            if (!owner) {
                return false;
            }

            owner->RestoreActorValue(
                a_value,
                1000000.0f);

            const float full =
                player->GetActorValue(
                    a_value);

            if (!std::isfinite(
                    full) ||
                full <= 0.0f) {

                return false;
            }

            const float pct =
                std::clamp(
                    a_percent,
                    0.0f,
                    100.0f);

            const float target =
                full *
                pct /
                100.0f;

            const float delta =
                target -
                full;

            if (delta < -0.001f) {
                owner->ModActorValue(
                    RE::ACTOR_VALUE_MODIFIERS::
                        kDamage,
                    a_value,
                    delta);
            }

            return true;
        }

        void SetQADefenseBaseline()
        {
            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kDamageResist,
                0.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kResistMagic,
                0.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kResistDisease,
                0.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kResistFire,
                0.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kResistFrost,
                0.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kResistShock,
                0.0f);
        }

        void SetQARegenBaseline()
        {
            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kHealRate,
                0.7f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kMagickaRate,
                3.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kStaminaRate,
                5.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kHealRateMult,
                100.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kMagickaRateMult,
                100.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kStaminaRateMult,
                100.0f);
        }

        void SetQAResourceBaseBaseline()
        {
            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kHealth,
                1000.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kMagicka,
                5000.0f);

            (void)SetQABaseActorValue(
                RE::ActorValue::
                    kStamina,
                1000.0f);
        }

        void SetAllQAResourcesPercent(
            float a_percent)
        {
            (void)SetQAResourcePercent(
                RE::ActorValue::
                    kHealth,
                a_percent);

            (void)SetQAResourcePercent(
                RE::ActorValue::
                    kMagicka,
                a_percent);

            (void)SetQAResourcePercent(
                RE::ActorValue::
                    kStamina,
                a_percent);
        }

        void DumpQAState(
            std::string_view a_tag)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player) {
                logger::error(
                    "[QA_STATE] {} | Player missing",
                    a_tag);

                return;
            }

            const auto av =
                [&](RE::ActorValue a_value) {
                    return player->
                        GetActorValue(
                            a_value);
                };

const auto selected =
                player->
                    GetActorRuntimeData().
                        selectedPower;

            logger::info(
                "[QA_STATE] {} | GetAV HP={:.2f} | MP={:.2f} | SP={:.2f} | Armor={:.2f} | MR={:.2f} | Disease={:.2f} | Fire={:.2f} | Frost={:.2f} | Shock={:.2f} | HealRate={:.2f} | MagickaRate={:.2f} | StaminaRate={:.2f} | HealRateMult={:.2f} | MagickaRateMult={:.2f} | StaminaRateMult={:.2f} | Kills={} | ArtifactUnique={} | ArtifactCounted={} | SelectedPower={:08X}",
                a_tag,
                av(RE::ActorValue::
                    kHealth),
                av(RE::ActorValue::
                    kMagicka),
                av(RE::ActorValue::
                    kStamina),
                av(RE::ActorValue::
                    kDamageResist),
                av(RE::ActorValue::
                    kResistMagic),
                av(RE::ActorValue::
                    kResistDisease),
                av(RE::ActorValue::
                    kResistFire),
                av(RE::ActorValue::
                    kResistFrost),
                av(RE::ActorValue::
                    kResistShock),
                av(RE::ActorValue::
                    kHealRate),
                av(RE::ActorValue::
                    kMagickaRate),
                av(RE::ActorValue::
                    kStaminaRate),
                av(RE::ActorValue::
                    kHealRateMult),
                av(RE::ActorValue::
                    kMagickaRateMult),
                av(RE::ActorValue::
                    kStaminaRateMult),
                TULLBUNG::Persistence::
                    GetInquisitorProgress().
                        monsterKills,
                TULLBUNG::PlayerState::
                    GetArtifacts().
                        uniqueOwned,
                TULLBUNG::PlayerState::
                    GetArtifacts().
                        counted,
                selected ?
                    selected->GetFormID() :
                    0);

            SendDevStatus(
                "QA state logged: " +
                std::string(
                    a_tag));
        }

        [[nodiscard]] bool
            EnsureQAClass(
                TULLBUNG::BuildState::
                    ClassID a_class)
        {
            using Result =
                TULLBUNG::BuildState::
                    AcquireClassResult;

            const auto result =
                TULLBUNG::BuildState::
                    TryAcquireClass(
                        a_class);

            return
                result ==
                    Result::kSuccess ||
                result ==
                    Result::kAlreadyAcquired;
        }

        [[nodiscard]] bool
            PurchaseQANode(
                TULLBUNG::BuildState::
                    NodeID a_node)
        {
            using Result =
                TULLBUNG::BuildState::
                    PurchaseNodeResult;

            const auto result =
                TULLBUNG::BuildState::
                    TryPurchaseNode(
                        a_node);

            return
                result ==
                    Result::kSuccess ||
                result ==
                    Result::kAlreadyOwned;
        }

        void DisableQANode(
            TULLBUNG::BuildState::
                NodeID a_node)
        {
            if (TULLBUNG::BuildState::
                    HasNode(
                        a_node) &&
                TULLBUNG::BuildState::
                    IsNodeEnabled(
                        a_node)) {

                (void)TULLBUNG::
                    BuildState::
                        SetNodeEnabled(
                            a_node,
                            false);
            }
        }

        [[nodiscard]] bool
            UnlockQAAdvancedClass(
                TULLBUNG::BuildState::
                    ClassID a_advanced)
        {
            using namespace
                TULLBUNG::BuildState;

            if (!EnsureQAClass(
                    ClassID::kPriest)) {

                return false;
            }

            // Exact 5 JP parent threshold:
            // Healing Arts 2 + Crusade 3.
            if (!PurchaseQANode(
                    NodeID::
                        kPriestHealingArts) ||
                !PurchaseQANode(
                    NodeID::
                        kPriestCrusade)) {

                return false;
            }

            const bool acquired =
                EnsureQAClass(
                    a_advanced);

            // Parent prerequisites stay purchased so the
            // advanced-class unlock remains historically valid,
            // but are disabled to prevent test effect bleed.
            DisableQANode(
                NodeID::
                    kPriestHealingArts);

            DisableQANode(
                NodeID::
                    kPriestCrusade);

            return acquired;
        }

        [[nodiscard]] bool
            ApplyQABuild(
                QABuildPreset a_preset)
        {
            using namespace
                TULLBUNG::BuildState;

            if (a_preset ==
                QABuildPreset::kNone) {

                return true;
            }

            AddDebugJP(
                200);

            if (!EnsureQAClass(
                    ClassID::kPriest)) {

                return false;
            }

            const bool priestPreset =
                a_preset == QABuildPreset::kPriestHealingArts ||
                a_preset == QABuildPreset::kPriestCrusade ||
                a_preset == QABuildPreset::kPriestFervor ||
                a_preset == QABuildPreset::kPriestFocusedHealing ||
                a_preset == QABuildPreset::kPriestMaster;

            if (priestPreset &&
                !PurchaseQANode(
                    NodeID::kPriestStance)) {

                return false;
            }

            bool ok = true;

            switch (a_preset) {
            case QABuildPreset::
                    kPriestHealingArts:
                ok =
                    PurchaseQANode(
                        NodeID::
                            kPriestHealingArts);
                break;

            case QABuildPreset::
                    kPriestCrusade:
                ok =
                    PurchaseQANode(
                        NodeID::
                            kPriestCrusade);
                break;

            case QABuildPreset::
                    kPriestFervor:
                ok =
                    PurchaseQANode(
                        NodeID::
                            kPriestFervor);
                break;

            case QABuildPreset::
                    kPriestFocusedHealing:
                ok =
                    PurchaseQANode(
                        NodeID::
                            kPriestFocusedHealing);
                break;

            case QABuildPreset::
                    kPriestMaster:
                ok =
                    PurchaseQANode(
                        NodeID::
                            kPriestHealingArts) &&
                    PurchaseQANode(
                        NodeID::
                            kPriestCrusade) &&
                    PurchaseQANode(
                        NodeID::
                            kPriestFervor) &&
                    PurchaseQANode(
                        NodeID::
                            kPriestFocusedHealing) &&
                    PurchaseQANode(
                        NodeID::
                            kPriestMaster);

                DisableQANode(
                    NodeID::
                        kPriestHealingArts);
                DisableQANode(
                    NodeID::
                        kPriestCrusade);
                DisableQANode(
                    NodeID::
                        kPriestFervor);
                DisableQANode(
                    NodeID::
                        kPriestFocusedHealing);
                break;

            case QABuildPreset::
                    kPaladinMaceMastery:
            case QABuildPreset::
                    kPaladinOath:
            case QABuildPreset::
                    kPaladinHolyStrike:
            case QABuildPreset::
                    kPaladinWeight:
            case QABuildPreset::
                    kPaladinMaster:
            {
                ok =
                    UnlockQAAdvancedClass(
                        ClassID::kPaladin);

                if (!ok) {
                    break;
                }

                if (a_preset ==
                    QABuildPreset::
                        kPaladinMaceMastery) {

                    ok =
                        PurchaseQANode(
                            NodeID::
                                kPaladinMaceMastery);
                }
                else if (
                    a_preset ==
                    QABuildPreset::
                        kPaladinOath) {

                    ok =
                        PurchaseQANode(
                            NodeID::
                                kPaladinOath);
                }
                else if (
                    a_preset ==
                    QABuildPreset::
                        kPaladinHolyStrike) {

                    ok =
                        PurchaseQANode(
                            NodeID::
                                kPaladinHolyStrike);
                }
                else if (
                    a_preset ==
                    QABuildPreset::
                        kPaladinWeight) {

                    ok =
                        PurchaseQANode(
                            NodeID::
                                kPaladinWeightOfFaith);
                }
                else {
                    ok =
                        PurchaseQANode(
                            NodeID::
                                kPaladinMaceMastery) &&
                        PurchaseQANode(
                            NodeID::
                                kPaladinOath) &&
                        PurchaseQANode(
                            NodeID::
                                kPaladinHolyStrike) &&
                        PurchaseQANode(
                            NodeID::
                                kPaladinWeightOfFaith) &&
                        PurchaseQANode(
                            NodeID::
                                kPaladinMaster);

                    DisableQANode(
                        NodeID::
                            kPaladinMaceMastery);
                    DisableQANode(
                        NodeID::
                            kPaladinOath);
                    DisableQANode(
                        NodeID::
                            kPaladinWeightOfFaith);

                    // Holy Strike remains active so the
                    // master 50% / MP20 behavior can be tested.
                }

                break;
            }

            case QABuildPreset::
                    kInquisitorMonsterStudy:
            case QABuildPreset::
                    kInquisitorArtifactStudy:
            case QABuildPreset::
                    kInquisitorDefense:
            case QABuildPreset::
                    kInquisitorSmite:
            case QABuildPreset::
                    kInquisitorMaster:
            {
                ok =
                    UnlockQAAdvancedClass(
                        ClassID::
                            kInquisitor);

                if (!ok) {
                    break;
                }

                if (a_preset ==
                    QABuildPreset::
                        kInquisitorMonsterStudy) {

                    ok =
                        PurchaseQANode(
                            NodeID::
                                kInquisitorMonsterStudy);
                }
                else if (
                    a_preset ==
                    QABuildPreset::
                        kInquisitorArtifactStudy) {

                    ok =
                        PurchaseQANode(
                            NodeID::
                                kInquisitorArtifactStudy);
                }
                else if (
                    a_preset ==
                    QABuildPreset::
                        kInquisitorDefense) {

                    ok =
                        PurchaseQANode(
                            NodeID::
                                kInquisitorMonsterDefense);
                }
                else if (
                    a_preset ==
                    QABuildPreset::
                        kInquisitorSmite) {

                    ok =
                        PurchaseQANode(
                            NodeID::
                                kInquisitorSmite);
                }
                else {
                    ok =
                        PurchaseQANode(
                            NodeID::
                                kInquisitorMonsterStudy) &&
                        PurchaseQANode(
                            NodeID::
                                kInquisitorArtifactStudy) &&
                        PurchaseQANode(
                            NodeID::
                                kInquisitorMonsterDefense) &&
                        PurchaseQANode(
                            NodeID::
                                kInquisitorSmite) &&
                        PurchaseQANode(
                            NodeID::
                                kInquisitorMaster);

                    DisableQANode(
                        NodeID::
                            kInquisitorMonsterStudy);
                    DisableQANode(
                        NodeID::
                            kInquisitorArtifactStudy);
                    DisableQANode(
                        NodeID::
                            kInquisitorMonsterDefense);

                    // Smite remains active for the
                    // Grand Inquisitor profile.
                }

                break;
            }

            case QABuildPreset::kNone:
                break;
            }

            TULLBUNG::ClassRuntime::
                OnBuildStateChanged();

            logger::info(
                "[QA_BUILD] Preset={} | Result={} | DebugJP={} | SpentJP={}",
                static_cast<unsigned int>(
                    a_preset),
                ok ?
                    "PASS" :
                    "FAIL",
                GetDebugJP(),
                GetSpentJP());

            return ok;
        }

        void ApplyQAStatePreset(
            QAStatePreset a_preset)
        {
            switch (a_preset) {
            case QAStatePreset::
                    kBaseline:
                break;

            case QAStatePreset::
                    kHP50:
                (void)SetQAResourcePercent(
                    RE::ActorValue::
                        kHealth,
                    50.0f);
                break;

            case QAStatePreset::
                    kHP79:
                (void)SetQAResourcePercent(
                    RE::ActorValue::
                        kHealth,
                    79.0f);
                break;

            case QAStatePreset::
                    kHP80:
                (void)SetQAResourcePercent(
                    RE::ActorValue::
                        kHealth,
                    80.0f);
                break;

            case QAStatePreset::
                    kResourcesHalf:
                SetAllQAResourcesPercent(
                    50.0f);
                break;
            }
        }

        void SelectQASmitePower()
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            auto* smite =
                TULLBUNG::DataCache::
                    GetPriestFamilySpell(
                        TULLBUNG::
                            DataCache::
                                PriestFamilySpellID::
                                    kInquisitorSmite);

            if (!player ||
                !smite) {

                logger::error(
                    "[QA_PROFILE] Smite Power select failed | Player={} | Smite={}",
                    player != nullptr,
                    smite != nullptr);

                return;
            }

            (void)player->AddSpell(
                smite);

            player->
                GetActorRuntimeData().
                    selectedPower =
                        smite;

            logger::info(
                "[QA_PROFILE] Smite Power selected | Form={:08X}",
                smite->GetFormID());
        }

        [[nodiscard]] const QAProfileSpec*
            FindQAProfile(
                std::string_view a_name)
        {
            for (const auto& profile :
                 kQAProfiles) {

                if (profile.name ==
                    a_name) {

                    return
                        std::addressof(
                            profile);
                }
            }

            if (kQAGrandInquisitorProfile.
                    name ==
                a_name) {

                return
                    std::addressof(
                        kQAGrandInquisitorProfile);
            }

            return nullptr;
        }

        void ApplyQAProfile(
            std::string_view a_name)
        {
            const auto* spec =
                FindQAProfile(
                    a_name);

            if (!spec) {
                logger::error(
                    "[DEV_PRESET] UNKNOWN | Name={}",
                    a_name);

                SendDevStatus(
                    "Unknown DEV preset");

                return;
            }

            logger::info(
                "[DEV_PRESET] BEGIN | {}",
                spec->name);

            // S13-style safe preparation:
            // TULL build state + validated equipment only.
            // Player/enemy ActorValues are NEVER changed here.
            TULLBUNG::BuildState::
                ResetAllNodes();

            TULLBUNG::BuildState::
                ClearDebugJP();

            TULLBUNG::ClassRuntime::
                ResetTransientState();

            const bool buildOK =
                ApplyQABuild(
                    spec->build);

            const bool loadoutOK =
                ApplyQALoadout(
                    spec->loadout);

            TULLBUNG::PlayerState::
                RefreshAll();

            TULLBUNG::ClassRuntime::
                OnBuildStateChanged();

            logger::info(
                "[DEV_PRESET] READY | {} | Build={} | Loadout={} | ActorValueMutation=NONE",
                spec->name,
                buildOK ?
                    "PASS" :
                    "FAIL",
                loadoutOK ?
                    "PASS" :
                    "FAIL");

            SendDevStatus(
                "Stage14 DEV preset ready");
        }
        // S19 B5: QA-only clean slate for class/node ownership.
        // Preserve highest player level and earned JP. This is NOT a
        // full save or auxiliary progress reset. Baseline QA save required.
        [[nodiscard]] bool ResetS19QAClassesAndNodes()
        {
            using namespace TULLBUNG::BuildState;
            PersistentState saved{};
            ExportPersistentState(saved);
            const auto oldDebugJP = GetDebugJP();
            PersistentState clean{};
            clean.highestPlayerLevelSeen = saved.highestPlayerLevelSeen;
            const bool restored = RestorePersistentState(clean);
            if (!restored) {
                logger::error("[S19QA] Reset classes/nodes rejected by validator");
                return false;
            }
            TULLBUNG::ClassRuntime::ResetTransientState();
            TULLBUNG::ClassRuntime::OnBuildStateChanged();
            bool cleanOK = GetSpentJP() == (kQA15ExtensionEnabled ? 0 : GetRetainedQAState().spent) && GetDebugJP() == 0;
            for (std::size_t c=0;c<kClassCount;++c) {
                if (HasClass(static_cast<ClassID>(c))) cleanOK=false;
            }
            for (std::uint32_t n=0;n<kNodeStorageSize;++n) {
                const auto node = static_cast<NodeID>(n);
                if (GetNodeMeta(node) &&
                    (HasNode(node) || IsNodeEnabled(node))) cleanOK=false;
            }
            if (!cleanOK) {
                logger::error("[S19QA] Reset postcheck failed, restoring prior BuildState");
                if (RestorePersistentState(saved)) {
                    AddDebugJP(oldDebugJP);
                    TULLBUNG::ClassRuntime::ResetTransientState();
                    TULLBUNG::ClassRuntime::OnBuildStateChanged();
                } else {
                    logger::critical("[S19QA] BuildState rollback rejected - reload baseline save");
                }
                return false;
            }
            logger::info("[S19QA] Reset known classes/nodes PASS | RetainedQAJP={} HighestLevel={} | AuxiliaryProgress=MAY_CHANGE",
                kQA15ExtensionEnabled ? 0 : GetRetainedQAState().spent,GetHighestPlayerLevelSeen());
            return true;
        }

        // S19-B7: explicit exit is destructive to QA build only. It is NOT
        // a save restore, and does not reset title ownership/style/auxiliary stats.
        [[nodiscard]] bool EndS19QASession()
        {
            if (!g_s19DevExtensionUnlocked) return true;
            // Keep title evaluation frozen until a clean build is verified.
            if (!ResetS19QAClassesAndNodes()) {
                logger::error("[S19QA] Exit reset FAILED; QA gate remains active");
                SendDevStatus("QA exit blocked: reset failed; reload baseline save");
                return false;
            }
            g_s19DevExtensionUnlocked = false;
            TULLBUNG::TitleRuntime::SetQAEvaluationSuspended(false);
            // One final evaluation against verified empty build.
            (void)TULLBUNG::TitleRuntime::RefreshFromBuildState();
            SendBuildStateSnapshot();
            SendTitleStateSnapshot();
            logger::info("[S19QA] Session END | Classes=0 Nodes=0 DebugJP=0");
            return true;
        }

        // S19 B4: transient QA-only class-mask setup.
        // This is NOT a save-state undo; class acquisition persists until
        // a baseline save is reloaded. Runtime acceptance is separate.
        [[nodiscard]] bool ApplyS19QAClassMask(
            std::uint16_t a_mask,
            const std::array<bool,TULLBUNG::BuildState::kNodeStorageSize>* a_selectedNodes = nullptr,
            std::string_view a_caseId = {})
        {
            using namespace TULLBUNG::BuildState;
            constexpr std::uint16_t kAllowed =
                static_cast<std::uint16_t>((1U << kClassCount) - 1U);
            if (a_mask == 0 || (a_mask & ~kAllowed) != 0) {
                logger::warn("[S19QA] Invalid class mask | Mask={}", a_mask);
                return false;
            }

            // Original four 3-class families remain IDs 0..11; QA15 ID 12 is a standalone root.
            static_assert(kClassCount == (kQA15ExtensionEnabled ? 13 : 12));
            static_assert(static_cast<unsigned>(ClassID::kSiegeMage)==11);
#if POT_QA15_EXTENSION
            static_assert(static_cast<unsigned>(ClassID::kQA15Sentinel)==12);
#endif
            std::uint32_t catalogCount = 0;
            for (std::uint32_t n = 0; n < kNodeStorageSize; ++n) {
                const auto* meta = GetNodeMeta(static_cast<NodeID>(n));
                if (meta) {
                    if (static_cast<std::size_t>(meta->classID) >= kClassCount) {
                        logger::error("[S19QA] Unexpected class ID");
                        return false;
                    }
                    ++catalogCount;
                }
            }
            if (catalogCount != (kQA15ExtensionEnabled ? 72u : 66u)) {
                logger::error("[S19QA] Catalog changed | Nodes={}", catalogCount);
                return false;
            }

            // Parent classes must be acquired and reach Lv5 before
            // children. Their acquired nodes remain owned, but will
            // be disabled if the parent is not in the requested mask.
            const auto requested = [&](std::size_t a_index) {
                return a_index < kClassCount && (a_mask & (1U << a_index)) != 0;
            };
            const auto classRequired = [&](std::size_t a_index) {
                if (requested(a_index)) return true;
                return a_index % 3 == 0 &&
                    (requested(a_index + 1) ||
                     requested(a_index + 2));
            };

            // Request full state through ordinary BuildState APIs,
            // without direct edits to actor values or persisted flags.
            if (!ResetS19QAClassesAndNodes()) {
                SendDevStatus("S19 QA reset failed - reload baseline save");
                return false;
            }
            AddDebugJP(400);

            bool success = true;
            std::uint32_t bought = 0;
            std::uint32_t enabled = 0;
            std::uint32_t acquired = 0;

            const auto acquire = [&](std::size_t a_index) {
                const auto id = static_cast<ClassID>(a_index);
                const bool ok = EnsureQAClass(id);
                if (ok) ++acquired;
                else logger::error(
                    "[S19QA] Class acquire failed | ID={}", a_index);
                return ok;
            };
            const auto purchasePass = [&](std::size_t a_index,
                std::uint8_t a_pass) {
                for (std::uint32_t n = 0; n < kNodeStorageSize; ++n) {
                    const auto id = static_cast<NodeID>(n);
                    const auto* meta = GetNodeMeta(id);
                    if (!meta ||
                        static_cast<std::size_t>(meta->classID) != a_index)
                        continue;
                    // cost 0 first (void prerequisite), then regular
                    // purchased nodes, then MASTER at Lv10.
                    const bool use = a_pass == 0 ? meta->cost == 0 :
                        a_pass == 1 ? !meta->master && meta->cost != 0 :
                        meta->master;
                    if (!use) continue;
                    if (!PurchaseQANode(id)) {
                        logger::error(
                            "[S19QA] Node purchase failed | ID={} Class={} Pass={}",
                            n,a_index,a_pass);
                        return false;
                    }
                    ++bought;
                }
                return true;
            };

            for (std::size_t base = 0; base < kClassCount; base += 3) {
                if (!classRequired(base)) continue;
                if (!acquire(base)) { success = false; break; }
                // Set Lv10 of parent before unlocking advanced class.
                if (!purchasePass(base,0) || !purchasePass(base,1)) {
                    success = false; break;
                }
                if (requested(base) && !purchasePass(base,2)) {
                    success = false; break;
                }
                for (std::size_t child = base+1; child < (std::min)(base+3,kClassCount); ++child) {
                    if (!requested(child)) continue;
                    if (!acquire(child) || !purchasePass(child,0) ||
                        !purchasePass(child,1) || !purchasePass(child,2)) {
                        success = false; break;
                    }
                }
                if (!success) break;
            }

            // Disable purchased prerequisites from non-selected parents,
            // but keep purchased ownership to preserve advanced unlocks.
            if (success) {
                for (std::uint32_t n = 0; n < kNodeStorageSize; ++n) {
                    const auto id = static_cast<NodeID>(n);
                    const auto* meta = GetNodeMeta(id);
                    if (!meta || !HasNode(id)) continue;
                    const auto idx = static_cast<std::size_t>(meta->classID);
                    const bool desired = a_selectedNodes ? (*a_selectedNodes)[n] : requested(idx);
                    if (!desired) {
                        const auto result = SetNodeEnabled(id,false);
                        if (result != SetNodeEnabledResult::kSuccess &&
                            result != SetNodeEnabledResult::kAlreadyInState) {
                            success = false;
                        }
                    }
                    if (IsNodeEnabled(id) == desired) ++enabled;
                    else success = false;
                }
            }

            // Verify every requested class and every canonical node.
            std::uint32_t expectedRequested = 0;
            std::uint32_t verifiedRequested = 0;
            for (std::size_t c = 0; c < kClassCount; ++c) {
                if (requested(c) && !HasClass(static_cast<ClassID>(c)))
                    success = false;
            }
            for (std::uint32_t n = 0; n < kNodeStorageSize; ++n) {
                const auto id = static_cast<NodeID>(n);
                const auto* meta = GetNodeMeta(id);
                if (!meta) continue;
                const bool desired = a_selectedNodes ? (*a_selectedNodes)[n] : requested(static_cast<std::size_t>(meta->classID));
                if (IsNodeEnabled(id) != desired) success = false;
                if (desired) {
                    ++expectedRequested;
                    if (HasNode(id) && IsNodeEnabled(id) && (!a_selectedNodes || IsNodeActive(id))) ++verifiedRequested;
                    else success = false;
                }
            }
            if (expectedRequested != verifiedRequested) success = false;
            // Avoid broad PlayerState::RefreshAll in DEV preset dispatch.
            // BuildState and owned passives are already synchronized.
            TULLBUNG::ClassRuntime::OnBuildStateChanged();
            logger::info(
                "[S19QA] Class mask result | Mask={} Result={} Classes={} Bought={} EnabledCheck={} Nodes={}/{} JP={}/{}",
                a_mask, success ? "PASS" : "FAIL", acquired,
                bought, enabled, verifiedRequested, expectedRequested,
                GetSpentJP(), GetDebugJP());
            // Partial failure is NOT rolled back here: effects may have
            // already fired. User must reload baseline before retrying.
            SendDevStatus(success ?
                "S19 QA preset validated" :
                "S19 QA preset failed - reload baseline save");
            SendBuildStateSnapshot();
            const std::string response =
                std::string(R"({"kind":"s19-profile-result","ok":)") +
                (success ? "true" : "false") +
                R"(,"label":")" + (a_caseId.empty() ? ("Class mask " + std::to_string(a_mask)) : ("Case " + std::string(a_caseId))) +
                R"(","reason":"See PathOfTULLBUNG.log"})";
            SendToUI(response.c_str());
            return success;
        }

        // CP031-A R4: QA ordinals mapped to exact Native IDs.
        static constexpr std::array<std::uint16_t,66> kS19CaseNodeIDs{{0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x010, 0x011, 0x012, 0x013, 0x014, 0x015, 0x020, 0x021, 0x022, 0x023, 0x024, 0x030, 0x031, 0x032, 0x033, 0x034, 0x040, 0x041, 0x042, 0x043, 0x044, 0x045, 0x050, 0x051, 0x052, 0x053, 0x054, 0x055, 0x065, 0x060, 0x061, 0x062, 0x063, 0x064, 0x070, 0x071, 0x072, 0x073, 0x074, 0x080, 0x081, 0x082, 0x083, 0x084, 0x090, 0x091, 0x092, 0x093, 0x0A5, 0x0A0, 0x0A1, 0x0A2, 0x0A3, 0x0A4, 0x0B5, 0x0B0, 0x0B1, 0x0B2, 0x0B3, 0x0B4}};
        struct S19CaseSpec { std::string_view id; std::string_view qa; };
        static constexpr std::array<S19CaseSpec,18> kS19Cases{{
                {"G01","1,2,4,7,11,13,23,40,44,47,52,54,60"},
                {"G02","3,5,6,14,15,16,17,41,42,43,45"},
                {"G03","8,9,10,12"},
                {"G04","18,19,20,21,22,24,25,26,27,28"},
                {"G05","29,30,31,32,33,34"},
                {"G06","35,36,37,38,39"},
                {"G07","46,48,49,50"},
                {"G08","51,53"},
                {"G09","55,56,57,58,59"},
                {"G10","61,62,63,64,65,66"},
                {"EX01","6,16,43,45,55,56"},
                {"EX02","4,7,8,9,10,11,12"},
                {"EX03","8,18,23,24,25,26,27,28,35,37,46"},
                {"EX04","18,22,29,30,31,32,33,34,46"},
                {"EX05","35,37,38,46,51,61,62,64,65"},
                {"EX06","46,49,55,59"},
                {"EX07","61,62,63,64,65,66"},
                {"EX08","47,52,54,55,60"},
        }};
        [[nodiscard]] bool ApplyS19QACasePreset(std::string_view a_case)
        {
            using namespace TULLBUNG::BuildState;
            const auto found = std::find_if(kS19Cases.begin(),kS19Cases.end(),
                [&](const S19CaseSpec& s){ return s.id == a_case; });
            if(found == kS19Cases.end()){
                logger::warn("[S19QA-CASE] Unknown ID={}",a_case);
                SendDevStatus("Unknown S19 QA CASE");
                return false;
            }
            std::array<bool,kNodeStorageSize> selected{};
            std::uint16_t mask=0;
            std::uint32_t count=0;
            const auto add=[&](std::uint16_t n)->bool {
                if(n >= kNodeStorageSize || selected[n]) return false;
                const auto* meta=GetNodeMeta(static_cast<NodeID>(n));
                if(!meta) return false;
                const auto cls=static_cast<std::size_t>(meta->classID);
                if(cls >= kClassCount) return false;
                selected[n]=true;
                mask |= static_cast<std::uint16_t>(1U << cls);
                ++count;
                return true;
            };
            std::string_view left=found->qa;
            while(!left.empty()){
                const auto sep=left.find(',');
                const auto part=left.substr(0,sep);
                std::uint32_t q=0;
                const auto parse=std::from_chars(part.data(),part.data()+part.size(),q);
                if(part.empty() || parse.ec != std::errc{} ||
                   parse.ptr != part.data()+part.size() ||
                   q==0 || q>kS19CaseNodeIDs.size() ||
                   !add(kS19CaseNodeIDs[q-1])){
                    logger::error("[S19QA-CASE] Bad QA mapping | ID={}",a_case);
                    return false;
                }
                if(sep==std::string_view::npos) break;
                left.remove_prefix(sep+1);
            }
            const auto root=[&](std::uint16_t id,std::uint16_t first,
                                std::uint16_t last)->bool {
                bool needed=false;
                for(std::uint16_t n=first;n<=last;++n){
                    if(selected[n]){needed=true;break;}
                }
                if(!needed || selected[id]) return true;
                return add(id);
            };
            if(!root(0x065,0x060,0x064) ||
               !root(0x0A5,0x0A0,0x0A4) ||
               !root(0x0B5,0x0B0,0x0B4) || !mask || !count){
                logger::error("[S19QA-CASE] Root selection FAIL | ID={}",a_case);
                return false;
            }
            const bool ok=ApplyS19QAClassMask(mask,&selected,a_case);
            logger::info("[S19QA-CASE] ID={} Mask={} Nodes={} Result={}",
                a_case,mask,count,ok?"PASS":"FAIL");
            return ok;
        }


        void SendSettingsSnapshot();
        void ExecuteJSCommand(const char*);
        void ExecuteTitleJSCommand(const char*);
        void TraceFTBoundary(std::atomic<unsigned>& count, const char* stage, std::string_view kind)
        {
            if(count.fetch_add(1,std::memory_order_relaxed)<8)
                logger::info("[FTTRACE] {} | Kind={} | Thread={}",stage,kind,GetCurrentThreadId());
        }

        void QueueOwnedCommand(const char* argument,bool title,bool profile)
        {
            if(!argument)return;const auto length=strnlen_s(argument,profile?4000001:65537);
            if(length>(profile?4000000:65536))return;
            if(profile)logger::info("[S20A5] Profile callback | Thread={}",GetCurrentThreadId());
            std::string owned(argument,length);auto* tasks=SKSE::GetTaskInterface();
            if(!title&&!profile&&owned.starts_with("ft.")){
                static std::atomic<unsigned> callbackCount{}, onCallbackCount{};
                const auto kind=std::string_view(owned).substr(0,owned.find(':'));
                TraceFTBoundary(kind=="ft.on"?onCallbackCount:callbackCount,tasks?"callback":"callback-no-task",kind);
            }
            if(!tasks)return;
            tasks->AddTask([owned=std::move(owned),title,profile](){
                if(!title&&!profile&&owned.starts_with("ft.")){
                    static std::atomic<unsigned> taskCount{}, onTaskCount{};
                    const auto kind=std::string_view(owned).substr(0,owned.find(':'));
                    TraceFTBoundary(kind=="ft.on"?onTaskCount:taskCount,"task-enter",kind);
                }
                std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
                if(profile){logger::info("[S20A5] Profile dispatch | Thread={}",GetCurrentThreadId());const auto response=TULLBUNG::UIProfiles::HandleRequest(owned);if(g_api&&g_view&&g_domReady&&g_api->IsValid(g_view))g_api->InteropCall(g_view,"tullProfileReceive",response.c_str());if(response.find("\"operation\":\"finalize\"")!=std::string::npos||response.find("\"operation\":\"abort\"")!=std::string::npos){SendSettingsSnapshot();SendTitleStateSnapshot();SendVoidHudSnapshot();}return;}
                if(!title&&TULLBUNG::FloatingTitleMinimal::HandleCommand(owned))return;
                if(title)TULLBUNG::FloatingTitleMinimal::MarkTitleDirty();
                if(TULLBUNG::UIProfiles::Busy()&&owned!="close"&&owned!="ime.toggle"){logger::info("[S20A5] Existing mutation deferred/rejected while Import pending");return;}
                if(title)ExecuteTitleJSCommand(owned.c_str());else ExecuteJSCommand(owned.c_str());
            });
        }
        void OnJSCommand(const char* argument){QueueOwnedCommand(argument,false,false);}
        void OnTitleJSCommand(const char* argument){QueueOwnedCommand(argument,true,false);}
        void OnProfileJSCommand(const char* argument){QueueOwnedCommand(argument,false,true);}
        void OnConsoleMessage(
            PrismaView a_view,
            PRISMA_UI_API::
                ConsoleMessageLevel a_level,
            const char* a_message)
        {
            logger::info(
                "[PrismaDebug][JS] View={} | Level={} | Message={}",
                a_view,
                static_cast<unsigned int>(
                    a_level),
                a_message ?
                    a_message :
                    "");
        }

        std::uint32_t CountOwnedDaedricArtifacts(
            RE::PlayerCharacter* a_player,
            const RE::BGSListForm* a_list)
        {
            if (!a_player ||
                !a_list) {

                return 0;
            }

            std::uint32_t count = 0;

            a_list->ForEachForm(
                [&](RE::TESForm* a_form) {
                    if (!a_form) {
                        return RE::BSContainer::
                            ForEachResult::
                                kContinue;
                    }

                    auto* item =
                        a_form->
                            As<RE::TESBoundObject>();

                    if (!item) {
                        logger::warn(
                            "[S14QA][Artifact] Non-item form skipped | Form={:08X}",
                            a_form->GetFormID());

                        return RE::BSContainer::
                            ForEachResult::
                                kContinue;
                    }

                    if (a_player->
                            GetItemCount(
                                item) > 0) {

                        ++count;
                    }

                    return RE::BSContainer::
                        ForEachResult::
                            kContinue;
                });

            return count;
        }

        void DebugSetInquisitorKills(
            std::uint32_t a_value)
        {
            TULLBUNG::Persistence::
                SetInquisitorMonsterKills(
                    a_value);

            const auto applied =
                TULLBUNG::Persistence::
                    GetInquisitorProgress().
                        monsterKills;

            logger::info(
                "[S14QA][InquisitorKills] SET | Requested={} | Applied={}",
                a_value,
                applied);

            SendDevStatus(
                "Inquisitor kills = " +
                std::to_string(
                    applied));
        }

        void DebugAddInquisitorKills(
            std::uint32_t a_value)
        {
            const auto before =
                TULLBUNG::Persistence::
                    GetInquisitorProgress().
                        monsterKills;

            constexpr std::uint32_t
                kQaKillCap = 500;

            const auto safeBefore =
                (std::min)(
                    before,
                    kQaKillCap);

            const auto remaining =
                kQaKillCap -
                safeBefore;

            const auto delta =
                (std::min)(
                    a_value,
                    remaining);

            TULLBUNG::Persistence::
                SetInquisitorMonsterKills(
                    safeBefore +
                    delta);

            const auto after =
                TULLBUNG::Persistence::
                    GetInquisitorProgress().
                        monsterKills;

            logger::info(
                "[S14QA][InquisitorKills] ADD | Requested={} | Before={} | AppliedDelta={} | After={}",
                a_value,
                before,
                delta,
                after);

            SendDevStatus(
                "Inquisitor kills = " +
                std::to_string(
                    after));
        }

        void DebugGiveDaedricArtifacts(
            std::uint32_t a_targetOwned,
            bool a_all)
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player ||
                !TULLBUNG::DataCache::
                    IsReady()) {

                logger::error(
                    "[S14QA][Artifact] GIVE failed | Player={} | DataCacheReady={}",
                    player != nullptr,
                    TULLBUNG::DataCache::
                        IsReady());

                SendDevStatus(
                    "Artifact QA failed");

                return;
            }

            auto* list =
                TULLBUNG::DataCache::
                    Get().
                        daedricArtifacts;

            if (!list) {
                logger::error(
                    "[S14QA][Artifact] GIVE failed | DaedricArtifacts FLST missing");

                SendDevStatus(
                    "Artifact FLST missing");

                return;
            }

            const auto before =
                CountOwnedDaedricArtifacts(
                    player,
                    list);

            if (!a_all &&
                before >=
                    a_targetOwned) {

                TULLBUNG::PlayerState::
                    RefreshArtifacts();

                logger::info(
                    "[S14QA][Artifact] TARGET already satisfied | Target={} | Before={} | Added=0 | After={}",
                    a_targetOwned,
                    before,
                    TULLBUNG::PlayerState::
                        GetArtifacts().
                            uniqueOwned);

                SendDevStatus(
                    "Artifacts owned = " +
                    std::to_string(
                        TULLBUNG::
                            PlayerState::
                                GetArtifacts().
                                    uniqueOwned));

                return;
            }

            std::uint32_t current =
                before;

            std::uint32_t added = 0;

            list->ForEachForm(
                [&](RE::TESForm* a_form) {
                    if (!a_form) {
                        return RE::BSContainer::
                            ForEachResult::
                                kContinue;
                    }

                    if (!a_all &&
                        current >=
                            a_targetOwned) {

                        return RE::BSContainer::
                            ForEachResult::
                                kStop;
                    }

                    auto* item =
                        a_form->
                            As<RE::TESBoundObject>();

                    if (!item) {
                        logger::warn(
                            "[S14QA][Artifact] Non-item form skipped | Form={:08X}",
                            a_form->GetFormID());

                        return RE::BSContainer::
                            ForEachResult::
                                kContinue;
                    }

                    if (player->
                            GetItemCount(
                                item) > 0) {

                        return RE::BSContainer::
                            ForEachResult::
                                kContinue;
                    }

                    player->
                        AddObjectToContainer(
                            item,
                            nullptr,
                            1,
                            nullptr);

                    ++current;
                    ++added;

                    logger::info(
                        "[S14QA][Artifact] GIVE | Form={:08X} | CurrentUniqueTargetProgress={}",
                        item->GetFormID(),
                        current);

                    return RE::BSContainer::
                        ForEachResult::
                            kContinue;
                });

            // Do not fake the ArtifactState count.
            // Re-run the real production inventory calculation.
            TULLBUNG::PlayerState::
                RefreshArtifacts();

            const auto& state =
                TULLBUNG::PlayerState::
                    GetArtifacts();

            logger::info(
                "[S14QA][Artifact] COMPLETE | Mode={} | Requested={} | Before={} | Added={} | UniqueOwned={} | Counted={} | BonusEach={}",
                a_all ?
                    "ALL" :
                    "TARGET",
                a_targetOwned,
                before,
                added,
                state.uniqueOwned,
                state.counted,
                state.potentialBonusEach);

            SendDevStatus(
                "Artifacts owned = " +
                std::to_string(
                    state.uniqueOwned) +
                " / counted = " +
                std::to_string(
                    state.counted));
        }
        bool g_hotkeyCaptureActive = false;

        void AppendJsonString(
            std::string& a_out,
            std::string_view a_value)
        {
            static constexpr char kHex[] =
                "0123456789ABCDEF";

            a_out += '"';

            for (const char raw : a_value) {
                const auto ch =
                    static_cast<unsigned char>(raw);

                if (ch == '"' ||
                    ch == '\\') {

                    a_out += '\\';
                    a_out +=
                        static_cast<char>(ch);
                }
                else if (ch < 0x20u) {

                    a_out += "\\u00";
                    a_out +=
                        kHex[
                            (ch >> 4u) &
                            0x0Fu];
                    a_out +=
                        kHex[
                            ch &
                            0x0Fu];
                }
                else {
                    a_out +=
                        static_cast<char>(ch);
                }
            }

            a_out += '"';
        }

        struct UserFontEntry final
        {
            std::string id;
            std::string label;
            std::string url;
            std::string format;
        };

        std::uint32_t FontBE32(const unsigned char* a_data)
        {
            return (static_cast<std::uint32_t>(a_data[0]) << 24u) |
                   (static_cast<std::uint32_t>(a_data[1]) << 16u) |
                   (static_cast<std::uint32_t>(a_data[2]) << 8u) |
                   static_cast<std::uint32_t>(a_data[3]);
        }

        std::uint16_t FontBE16(const unsigned char* a_data)
        {
            return static_cast<std::uint16_t>(
                (static_cast<unsigned>(a_data[0]) << 8u) | a_data[1]);
        }

        bool FontHeaderEligible(
            const std::filesystem::path& a_path,
            std::uintmax_t a_bytes,
            std::string_view a_format)
        {
            if (a_bytes < 48 || a_bytes > 64u * 1024u * 1024u) {
                return false;
            }

            std::ifstream input(a_path, std::ios::binary);
            if (!input) {
                return false;
            }
            std::array<unsigned char, 4096> data{};
            input.read(reinterpret_cast<char*>(data.data()), data.size());
            const auto read = static_cast<std::size_t>(input.gcount());
            if (read < 48) {
                return false;
            }

            const auto tag = FontBE32(data.data());
            const bool sfnt = tag == 0x00010000u || tag == 0x4F54544Fu;
            const bool woff = tag == 0x774F4646u;
            const bool woff2 = tag == 0x774F4632u;

            if ((a_format == "truetype" || a_format == "opentype") && !sfnt) {
                return false;
            }
            if (a_format == "woff" && !woff) {
                return false;
            }
            if (a_format == "woff2" && !woff2) {
                return false;
            }

            const auto count = FontBE16(data.data() + (sfnt ? 4 : 12));
            if (count == 0 || count > 128) {
                return false;
            }
            if (sfnt || woff) {
                const std::size_t base = sfnt ? 12u : 44u;
                const std::size_t stride = sfnt ? 16u : 20u;
                const std::size_t directoryEnd = base + stride * count;
                if (read < directoryEnd) {
                    return false;
                }
                if (woff && (FontBE32(data.data() + 8) != a_bytes ||
                             FontBE16(data.data() + 14) != 0)) {
                    return false;
                }
                for (std::size_t i = 0; i < count; ++i) {
                    const auto* item = data.data() + base + stride * i;
                    const auto offset = FontBE32(item + (sfnt ? 8 : 4));
                    const auto bytes = FontBE32(item + (sfnt ? 12 : 8));
                    if (bytes == 0 || offset < directoryEnd ||
                        static_cast<std::uintmax_t>(offset) + bytes > a_bytes) {
                        return false;
                    }
                    if (woff && bytes > FontBE32(item + 12)) {
                        return false;
                    }
                }
                return true;
            }
            // WOFF2 requires a decoder for complete table validation.
            // Browser FontFace.load() must independently validate it in B4-B2.
            return woff2 && FontBE32(data.data() + 8) == a_bytes &&
                   FontBE16(data.data() + 14) == 0 &&
                   FontBE32(data.data() + 20) > 0 &&
                   FontBE32(data.data() + 20) <= a_bytes - 48;
        }

        std::string UserFontUrlEncode(std::string_view a_text)
        {
            constexpr char digits[] = "0123456789ABCDEF";
            std::string out;
            for (const unsigned char ch : a_text) {
                if ((ch >= 'A' && ch <= 'Z') ||
                    (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') ||
                    ch == '-' || ch == '_' || ch == '.' || ch == '/' ) {
                    out += static_cast<char>(ch);
                } else {
                    out += '%';
                    out += digits[ch >> 4u];
                    out += digits[ch & 15u];
                }
            }
            return out;
        }

        void SendUserFontList()
        {
            namespace fs = std::filesystem;
            std::vector<UserFontEntry> fonts;
            // R1-11: counts only; no user font filenames.
            std::array<std::size_t,4> discovered{};
            std::array<std::size_t,4> validated{};
            bool rootFound = false;
            bool truncated = false;
            try {
                std::array<wchar_t, 32768> module{};
                const auto len = ::GetModuleFileNameW(
                    nullptr, module.data(), static_cast<DWORD>(module.size()));
                if (len != 0 && len < module.size()) {
                    const fs::path root = fs::path(
                        std::wstring(module.data(), len)).parent_path() /
                        L"Data" / L"PrismaUI" / L"views" /
                        L"pathoftullbung_debug" / L"assets" / L"fonts" / L"user";
                    std::error_code error;
                    rootFound = fs::is_directory(root, error);
                    if (rootFound) {
                        std::size_t visited = 0;
                        for (fs::recursive_directory_iterator it(
                                 root, fs::directory_options::skip_permission_denied, error), end;
                             !error && it != end && visited < 4096 && fonts.size() < 256;
                             it.increment(error)) {
                            ++visited;
                            if (it.depth() >= 8) {
                                it.disable_recursion_pending();
                            }
                            std::error_code fileError;
                            if (it->is_symlink(fileError) ||
                                !it->is_regular_file(fileError) || fileError) {
                                continue;
                            }
                            auto extension = it->path().extension().wstring();
                            for (auto& ch : extension) {
                                ch = static_cast<wchar_t>(std::towlower(ch));
                            }
                            const char* format = nullptr;
                            if (extension == L".ttf") format = "truetype";
                            else if (extension == L".otf") format = "opentype";
                            else if (extension == L".woff") format = "woff";
                            else if (extension == L".woff2") format = "woff2";
                            if (!format) continue;
                            const std::size_t fi = extension == L".ttf" ? 0u :
                                extension == L".otf" ? 1u :
                                extension == L".woff" ? 2u : 3u;
                            ++discovered[fi];
                            const auto bytes = fs::file_size(it->path(), fileError);
                            if (fileError || !FontHeaderEligible(it->path(), bytes, format)) {
                                continue;
                            }
                            ++validated[fi];
                            const auto relative = it->path().lexically_relative(root);
                            if (relative.empty()) continue;
                            const auto u8 = relative.generic_u8string();
                            const std::string name(
                                reinterpret_cast<const char*>(u8.data()), u8.size());
                            if (name.empty()) {
                                continue;
                            }
                            fonts.push_back({
                                "user/" + name, name,
                                "assets/fonts/user/" + UserFontUrlEncode(name),
                                format});
                        }
                        truncated = visited >= 4096 || fonts.size() >= 256;
                    }
                }
            } catch (const std::exception& ex) {
                logger::warn("[S18FONT] Scan exception: {}", ex.what());
            }
            std::sort(fonts.begin(), fonts.end(),
                [](const UserFontEntry& a, const UserFontEntry& b) {
                    return a.id < b.id;
                });
            std::string payload =
                R"({"kind":"font-list","rootFound":)";
            payload += rootFound ? "true" : "false";
            payload += R"(,"truncated":)";
            payload += truncated ? "true" : "false";
            payload += R"(,"fonts":[{"id":"system","label":"맑은 고딕","format":"system","url":""})";
            for (const auto& entry : fonts) {
                payload += R"(,{"id":)";
                AppendJsonString(payload, entry.id);
                payload += R"(,"label":)";
                AppendJsonString(payload, entry.label);
                payload += R"(,"format":)";
                AppendJsonString(payload, entry.format);
                payload += R"(,"url":)";
                AppendJsonString(payload, entry.url);
                payload += '}';
            }
            payload += "]}";
            logger::info("[S18FONT] CandidateCount={} RootFound={} Truncated={}",
                         fonts.size(), rootFound, truncated);
            logger::info("[S18FONT][R1-11] seen TTF={} OTF={} WOFF={} WOFF2={} | headerPass TTF={} OTF={} WOFF={} WOFF2={}",
                         discovered[0], discovered[1], discovered[2], discovered[3],
                         validated[0], validated[1], validated[2], validated[3]);
            SendToUI(payload.c_str());
            TULLBUNG::FloatingTitleMinimal::SetFontCatalog(payload);
        }

        const char* TitleRoleName(
            TULLBUNG::TitleSystem::Role a_role) noexcept
        {
            using TULLBUNG::TitleSystem::Role;

            switch (a_role) {
            case Role::kPrefix:
                return "PREFIX";

            case Role::kSuffix:
                return "SUFFIX";

            case Role::kStandalone:
                return "STANDALONE";

            case Role::kNameTemplate:
                return "NAME_TEMPLATE";
            }

            return "STANDALONE";
        }

        const char* TitleStyleName(
            TULLBUNG::TitleSystem::TitleStyle a_style) noexcept
        {
            using TULLBUNG::TitleSystem::TitleStyle;

            switch (a_style) {
            case TitleStyle::kDefault:
                return "default";

            case TitleStyle::kBold:
                return "bold";

            case TitleStyle::kAccent:
                return "accent";

            case TitleStyle::kAccentBold:
                return "accent_bold";
            }

            return "default";
        }

        void AppendTitleRoleArray(
            std::string& a_out,
            std::uint8_t a_mask)
        {
            using TULLBUNG::TitleSystem::Role;

            a_out += '[';

            bool comma = false;

            const auto append =
                [&](Role a_role) {

                    const auto bit =
                        static_cast<
                            std::uint8_t>(
                                a_role);

                    if ((a_mask & bit) == 0) {
                        return;
                    }

                    if (comma) {
                        a_out += ',';
                    }

                    AppendJsonString(
                        a_out,
                        TitleRoleName(
                            a_role));

                    comma = true;
                };

            append(
                Role::kPrefix);

            append(
                Role::kSuffix);

            append(
                Role::kStandalone);

            append(
                Role::kNameTemplate);

            a_out += ']';
        }

        std::string_view
            CurrentCharacterName() noexcept
        {
            auto* player =
                RE::PlayerCharacter::
                    GetSingleton();

            if (!player) {
                return {};
            }

            const auto* name =
                player->GetName();

            return
                name && name[0] ?
                    std::string_view{
                        name
                    } :
                    std::string_view{};
        }

        void SendTitleCatalog()
        {
            if (!g_domReady) {
                return;
            }

            std::string payload;
            payload.reserve(
                131072);

            payload =
                R"({"kind":"title-catalog","titles":[)";

            const auto& titles =
                TULLBUNG::TitleSystem::
                    kTitles;

            for (std::size_t i = 0;
                 i < titles.size();
                 ++i) {

                const auto& title =
                    titles[i];

                if (i != 0) {
                    payload += ',';
                }

                payload +=
                    R"({"id":)" +
                    std::to_string(
                        title.numericID);

                payload +=
                    R"(,"name":)";

                AppendJsonString(
                    payload,
                    title.text);

                payload +=
                    R"(,"style":)";

                AppendJsonString(
                    payload,
                    TitleStyleName(
                        title.style));

                payload +=
                    R"(,"segments":[)";

                const auto segments =
                    TULLBUNG::TitleSystem::
                        SegmentsOf(
                            title);

                for (std::size_t s = 0;
                     s < segments.size();
                     ++s) {

                    const auto& segment =
                        segments[s];

                    if (s != 0) {
                        payload += ',';
                    }

                    payload +=
                        R"({"index":)" +
                        std::to_string(
                            segment.index);

                    payload +=
                        R"(,"text":)";

                    AppendJsonString(
                        payload,
                        segment.text);

                    payload +=
                        R"(,"roleMask":)" +
                        std::to_string(
                            segment.roleMask);

                    payload +=
                        R"(,"roles":)";

                    AppendTitleRoleArray(
                        payload,
                        segment.roleMask);

                    payload += '}';
                }

                payload +=
                    R"(],"conditionText":)";

                AppendJsonString(
                    payload,
                    title.hoverCondition);

                payload += '}';
            }

            payload += "]}";

            SendToUI(
                payload.c_str());

            g_titleCatalogSent = true;
        }

        void SendTitleStateSnapshot()
        {
            TULLBUNG::FloatingTitleMinimal::RefreshTitles();
            if (!g_domReady) {
                return;
            }

            auto& runtime =
                TULLBUNG::TitleRuntime::
                    GetRuntime();

            const auto characterName =
                CurrentCharacterName();

            std::string payload;
            payload.reserve(
                4096);

            payload =
                R"({"kind":"title-state","characterName":)";

            AppendJsonString(
                payload,
                characterName);

            payload +=
                R"(,"nickname":)";

            AppendJsonString(
                payload,
                runtime.Nickname());

            payload +=
                R"(,"effectiveName":)";

            AppendJsonString(
                payload,
                runtime.EffectiveName(
                    characterName));

            payload +=
                R"(,"ownedTitleIDs":[)";

            const auto owned =
                runtime.
                    AcquiredTitleIDs();

            for (std::size_t i = 0;
                 i < owned.size();
                 ++i) {

                if (i != 0) {
                    payload += ',';
                }

                payload +=
                    std::to_string(
                        owned[i]);
            }

            payload +=
                R"(],"favoriteTitleIDs":[)";

            const auto favorites =
                runtime.
                    FavoriteTitleIDs();

            for (std::size_t i = 0;
                 i < favorites.size();
                 ++i) {

                if (i != 0) {
                    payload += ',';
                }

                payload +=
                    std::to_string(
                        favorites[i]);
            }

            payload +=
                R"(],"slots":[)";

            const auto& slots =
                runtime.Slots();

            for (std::size_t i = 0;
                 i < slots.size();
                 ++i) {

                const auto& slot =
                    slots[i];

                if (i != 0) {
                    payload += ',';
                }

                payload +=
                    R"({"slot":)" +
                    std::to_string(
                        i + 1);

                payload +=
                    R"(,"active":)";

                payload +=
                    slot.active ?
                        "true" :
                        "false";

                payload +=
                    R"(,"role":)";

                AppendJsonString(
                    payload,
                    TitleRoleName(
                        slot.role));

                payload +=
                    R"(,"auto":)";

                payload +=
                    slot.autoEnabled ?
                        "true" :
                        "false";

                payload +=
                    R"(,"titleID":)" +
                    std::to_string(
                        slot.selectedTitleID);

                payload +=
                    R"(,"segmentIndex":)" +
                    std::to_string(
                        slot.selectedSegmentIndex);

                payload +=
                    R"(,"resolvedText":)";

                AppendJsonString(
                    payload,
                    runtime.ResolveSlotText(
                        i + 1,
                        characterName));

                payload += '}';
            }

            payload += R"(],"customTitles":[)";

            const auto customTitles =
                runtime.MakeCustomTitleRecords();

            for (std::size_t i = 0;
                 i < TULLBUNG::TitleSystem::kCustomTitleCount;
                 ++i) {

                if (i != 0) {
                    payload += ',';
                }

                const auto& custom = customTitles[i];

                const auto id = static_cast<std::uint16_t>(
                    TULLBUNG::TitleSystem::kCustomTitleFirstID + i);

                payload += R"({"id":)";
                payload += std::to_string(id);

                payload += R"(,"code":)";

                const std::string code =
                    "C" + std::to_string(i / 10) +
                    std::to_string(i % 10);

                AppendJsonString(payload, code);

                payload += R"(,"present":)";
                payload += custom.present ? "true" : "false";

                payload += R"(,"favorite":)";
                payload +=
                    (custom.present && custom.favorite) ?
                    "true" : "false";

                payload += R"(,"before":)";

                AppendJsonString(
                    payload,
                    custom.present ?
                        std::string_view(custom.before) :
                        std::string_view{});

                payload += R"(,"after":)";

                AppendJsonString(
                    payload,
                    custom.present ?
                        std::string_view(custom.after) :
                        std::string_view{});

                payload += '}';
            }

            payload += "]}";

            SendToUI(
                payload.c_str());
        }
        void SendSettingsSnapshot()
        {
            if (!g_domReady) {
                return;
            }

            const auto& settings =
                TULLBUNG::UISettings::
                    Get();

            std::string payload;
            payload.reserve(1024);

            const auto addBool =
                [&](std::string_view a_key,
                    bool a_value) {

                    payload += ",\"";

                    payload.append(
                        a_key.data(),
                        a_key.size());

                    payload += "\":";

                    payload +=
                        a_value ?
                            "true" :
                            "false";
                };

            const auto addUInt =
                [&](std::string_view a_key,
                    std::uint32_t a_value) {

                    payload += ",\"";

                    payload.append(
                        a_key.data(),
                        a_key.size());

                    payload += "\":";

                    payload +=
                        std::to_string(
                            a_value);
                };

            const auto addFloat =
                [&](std::string_view a_key,
                    float a_value) {

                    payload += ",\"";

                    payload.append(
                        a_key.data(),
                        a_key.size());

                    payload += "\":";

                    payload +=
                        std::to_string(
                            a_value);
                };

            payload =
                R"({"kind":"settings")";

            addUInt(
                "schemaVersion",
                settings.schemaVersion);

            addBool(
                "developerMode",
                settings.developerMode);

            payload +=
                R"(,"themeId":)";

            AppendJsonString(
                payload,
                settings.themeId);

            addFloat(
                "uiBackgroundOpacity",
                settings.
                    uiBackgroundOpacity);

            addUInt(
                "menuKey",
                settings.
                    menuHotkey.
                        keyCode);

            addBool(
                "menuShift",
                settings.
                    menuHotkey.
                        shift);

            addBool(
                "menuCtrl",
                settings.
                    menuHotkey.
                        ctrl);

            addBool(
                "menuAlt",
                settings.
                    menuHotkey.
                        alt);

            addBool(
                "hotkeyCaptureActive",
                g_hotkeyCaptureActive);

            addBool(
                "hudMaster",
                settings.
                    hud.
                        masterVisible);

            addBool(
                "hudVoidVisible",
                settings.
                    hud.
                        voidHud.
                            visible);

            addFloat(
                "hudVoidX",
                settings.
                    hud.
                        voidHud.x);

            addFloat(
                "hudVoidY",
                settings.
                    hud.
                        voidHud.y);

            addFloat(
                "hudVoidScale",
                settings.
                    hud.
                        voidHud.scale);

            addFloat(
                "hudVoidOpacity",
                settings.
                    hud.
                        voidHud.opacity);

            addBool(
                "hudComposureVisible",
                settings.
                    hud.
                        composureHud.
                            visible);

            addFloat(
                "hudComposureX",
                settings.
                    hud.
                        composureHud.x);

            addFloat(
                "hudComposureY",
                settings.
                    hud.
                        composureHud.y);

            addFloat(
                "hudComposureScale",
                settings.
                    hud.
                        composureHud.scale);

            addFloat(
                "hudComposureOpacity",
                settings.
                    hud.
                        composureHud.
                            opacity);

            addBool(
                "hudQuietVisible",
                settings.
                    hud.
                        quietProcessingHud.
                            visible);

            addFloat(
                "hudQuietX",
                settings.
                    hud.
                        quietProcessingHud.x);

            addFloat(
                "hudQuietY",
                settings.
                    hud.
                        quietProcessingHud.y);

            addFloat(
                "hudQuietScale",
                settings.
                    hud.
                        quietProcessingHud.
                            scale);

            addFloat(
                "hudQuietOpacity",
                settings.
                    hud.
                        quietProcessingHud.
                            opacity);

            payload += "}";

            SendToUI(
                payload.c_str());
        }

        bool ParseSettingsBool(
            std::string_view a_value,
            bool& a_out)
        {
            if (a_value == "1" ||
                a_value == "true") {

                a_out = true;
                return true;
            }

            if (a_value == "0" ||
                a_value == "false") {

                a_out = false;
                return true;
            }

            return false;
        }

        bool ParseSettingsFloat(
            std::string_view a_value,
            float& a_out)
        {
            if (a_value.empty()) {
                return false;
            }

            float value{};

            const auto result =
                std::from_chars(
                    a_value.data(),
                    a_value.data() +
                        a_value.size(),
                    value);

            if (result.ec !=
                    std::errc{} ||
                result.ptr !=
                    a_value.data() +
                        a_value.size() ||
                !std::isfinite(
                    value)) {

                return false;
            }

            a_out = value;
            return true;
        }

        bool CommitSettings(
            TULLBUNG::UISettings::
                Settings a_updated,
            std::string_view a_key)
        {
            const auto previous =
                TULLBUNG::
                    UISettings::
                        Get();

            if (!TULLBUNG::
                    UISettings::
                        Apply(
                            a_updated,
                            true)) {

                (void)TULLBUNG::
                    UISettings::
                        Apply(
                            previous,
                            false);

                logger::error(
                    "[S18UI] Settings save failed | Key={}",
                    a_key);

                SendSettingsSnapshot();
                return false;
            }

            logger::info(
                "[S18UI] Settings applied | Key={}",
                a_key);

            SendSettingsSnapshot();
            SendVoidHudSnapshot();
            return true;
        }

        bool ApplySettingsValue(
            std::string_view a_key,
            std::string_view a_value)
        {
            auto updated =
                TULLBUNG::
                    UISettings::
                        Get();

            bool parsed =
                false;

            const auto setBool =
                [&](bool& a_field) {

                    return
                        ParseSettingsBool(
                            a_value,
                            a_field);
                };

            const auto setFloat =
                [&](float& a_field) {

                    return
                        ParseSettingsFloat(
                            a_value,
                            a_field);
                };

            if (a_key ==
                "developerMode") {

                parsed =
                    setBool(
                        updated.
                            developerMode);
            }
            else if (
                a_key ==
                "themeId") {

                updated.themeId =
                    std::string{
                        a_value
                    };

                parsed = true;
            }
            else if (
                a_key ==
                "uiBackgroundOpacity") {

                parsed =
                    setFloat(
                        updated.
                            uiBackgroundOpacity);
            }
            else if (
                a_key ==
                "hudMaster") {

                parsed =
                    setBool(
                        updated.
                            hud.
                                masterVisible);
            }
            else if (
                a_key ==
                "hudVoidVisible") {

                parsed =
                    setBool(
                        updated.
                            hud.
                                voidHud.
                                    visible);
            }
            else if (
                a_key ==
                "hudVoidX") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                voidHud.x);
            }
            else if (
                a_key ==
                "hudVoidY") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                voidHud.y);
            }
            else if (
                a_key ==
                "hudVoidScale") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                voidHud.scale);
            }
            else if (
                a_key ==
                "hudVoidOpacity") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                voidHud.
                                    opacity);
            }
            else if (
                a_key ==
                "hudComposureVisible") {

                parsed =
                    setBool(
                        updated.
                            hud.
                                composureHud.
                                    visible);
            }
            else if (
                a_key ==
                "hudComposureX") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                composureHud.x);
            }
            else if (
                a_key ==
                "hudComposureY") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                composureHud.y);
            }
            else if (
                a_key ==
                "hudComposureScale") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                composureHud.
                                    scale);
            }
            else if (
                a_key ==
                "hudComposureOpacity") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                composureHud.
                                    opacity);
            }
            else if (
                a_key ==
                "hudQuietVisible") {

                parsed =
                    setBool(
                        updated.
                            hud.
                                quietProcessingHud.
                                    visible);
            }
            else if (
                a_key ==
                "hudQuietX") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                quietProcessingHud.x);
            }
            else if (
                a_key ==
                "hudQuietY") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                quietProcessingHud.y);
            }
            else if (
                a_key ==
                "hudQuietScale") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                quietProcessingHud.
                                    scale);
            }
            else if (
                a_key ==
                "hudQuietOpacity") {

                parsed =
                    setFloat(
                        updated.
                            hud.
                                quietProcessingHud.
                                    opacity);
            }
            else {

                logger::warn(
                    "[S18UI] Unknown settings key | {}",
                    a_key);

                SendSettingsSnapshot();
                return true;
            }

            if (!parsed) {

                logger::warn(
                    "[S18QA][SETTINGS] Invalid value");

                SendSettingsSnapshot();
                return true;
            }

            // On Developer Mode OFF, clear the QA build BEFORE committing
            // settings. Failure keeps DEV enabled and titles isolated.
            if (a_key == "developerMode" && !updated.developerMode &&
                g_s19DevExtensionUnlocked && !EndS19QASession()) {
                SendSettingsSnapshot();
                return true;
            }
            (void)CommitSettings(
                updated,
                a_key);

            // Revoking developer mode must revoke the hidden extension too.
            if (a_key == "developerMode" &&
                !TULLBUNG::UISettings::Get().developerMode) {
                g_s19DevExtensionUnlocked = false;
                SendToUI(R"({"kind":"dev-extension","enabled":false})");
            }

            return true;
        }

        bool HandleSettingsCommand(
            std::string_view a_command)
        {
            if (a_command ==
                "settings.font.refresh") {

                SendUserFontList();
                return true;
            }

            if (a_command ==
                "settings.refresh") {

                SendSettingsSnapshot();
                return true;
            }

            if (a_command ==
                "settings.hotkey.reset") {

                auto updated =
                    TULLBUNG::UISettings::Get();
                updated.menuHotkey =
                    TULLBUNG::UISettings::HotkeyBinding{};

                g_hotkeyCaptureActive = false;
                const bool saved =
                    CommitSettings(updated, "menuHotkey.reset");
                logger::info(
                    "[S18UI][HOTKEY] ResetDefault | Success={} Key={}",
                    saved,
                    TULLBUNG::UISettings::kDefaultMenuKeyCode);
                return true;
            }

            if (a_command ==
                "settings.hotkey.capture.begin") {

                g_hotkeyCaptureActive =
                    true;

                logger::info(
                    "[S18UI] Hotkey capture begin");

                SendSettingsSnapshot();
                return true;
            }

            if (a_command ==
                "settings.hotkey.capture.cancel") {

                g_hotkeyCaptureActive =
                    false;

                logger::info(
                    "[S18UI] Hotkey capture cancelled");

                SendSettingsSnapshot();
                return true;
            }

            constexpr
                std::string_view
                    prefix =
                        "settings.set.";

            if (!a_command.starts_with(
                    prefix)) {

                return false;
            }

            const auto assignment =
                a_command.substr(
                    prefix.size());

            const auto split =
                assignment.find('=');

            if (split ==
                std::string_view::npos) {

                logger::warn(
                    "[S18QA][SETTINGS] Malformed command");

                SendSettingsSnapshot();
                return true;
            }

            return
                ApplySettingsValue(
                    assignment.substr(
                        0,
                        split),
                    assignment.substr(
                        split + 1));
        }

        bool ParseTitleUnsigned(
            std::string_view a_text,
            std::uint32_t& a_value)
        {
            if (a_text.empty()) {
                return false;
            }

            const auto result =
                std::from_chars(
                    a_text.data(),
                    a_text.data() +
                        a_text.size(),
                    a_value);

            return
                result.ec == std::errc{} &&
                result.ptr ==
                    a_text.data() +
                        a_text.size();
        }

        bool TakeTitleToken(
            std::string_view& a_input,
            std::string_view& a_token)
        {
            if (a_input.empty()) {
                return false;
            }

            const auto split =
                a_input.find('.');

            if (split ==
                std::string_view::npos) {

                a_token = a_input;
                a_input = {};
                return !a_token.empty();
            }

            a_token =
                a_input.substr(
                    0,
                    split);

            a_input.remove_prefix(
                split + 1);

            return !a_token.empty();
        }

        int TitleHexValue(
            char a_ch) noexcept
        {
            if (a_ch >= '0' &&
                a_ch <= '9') {

                return a_ch - '0';
            }

            if (a_ch >= 'A' &&
                a_ch <= 'F') {

                return
                    10 +
                    (a_ch - 'A');
            }

            if (a_ch >= 'a' &&
                a_ch <= 'f') {

                return
                    10 +
                    (a_ch - 'a');
            }

            return -1;
        }

        bool DecodeTitleText(
            std::string_view a_encoded,
            std::string& a_out)
        {
            a_out.clear();

            a_out.reserve(
                a_encoded.size());

            for (std::size_t i = 0;
                 i < a_encoded.size();
                 ++i) {

                const char ch =
                    a_encoded[i];

                if (ch != '%') {
                    a_out += ch;
                    continue;
                }

                if (i + 2 >=
                    a_encoded.size()) {

                    return false;
                }

                const int hi =
                    TitleHexValue(
                        a_encoded[i + 1]);

                const int lo =
                    TitleHexValue(
                        a_encoded[i + 2]);

                if (hi < 0 ||
                    lo < 0) {

                    return false;
                }

                a_out +=
                    static_cast<char>(
                        (hi << 4) |
                        lo);

                i += 2;
            }

            return true;
        }

        bool ParseTitleRole(
            std::string_view a_text,
            TULLBUNG::TitleSystem::Role& a_role)
        {
            using
                TULLBUNG::TitleSystem::Role;

            if (a_text == "PREFIX") {

                a_role =
                    Role::kPrefix;

                return true;
            }

            if (a_text == "SUFFIX") {

                a_role =
                    Role::kSuffix;

                return true;
            }

            if (a_text == "STANDALONE") {

                a_role =
                    Role::kStandalone;

                return true;
            }

            if (a_text == "NAME_TEMPLATE") {

                a_role =
                    Role::kNameTemplate;

                return true;
            }

            return false;
        }

        void ExecuteTitleJSCommand(
            const char* a_argument)
        {
            const std::string_view command =
                a_argument ?
                    std::string_view(
                        a_argument) :
                    std::string_view{};

            auto& runtime =
                TULLBUNG::TitleRuntime::
                    GetRuntime();

            if (command.starts_with("title.custom.")) {
                constexpr std::string_view kSet = "title.custom.set.";
                constexpr std::string_view kDelete = "title.custom.delete.";
                bool customAccepted = false;
                std::uint32_t customID = 0;

                if (command.starts_with(kSet)) {
                    const auto rest = command.substr(kSet.size());
                    const auto idEnd = rest.find('.');

                    if (idEnd != std::string_view::npos) {
                        const auto encoded = rest.substr(idEnd + 1);
                        const auto textEnd = encoded.find('.');

                        if (textEnd != std::string_view::npos) {
                            const auto beforeToken = encoded.substr(0, textEnd);
                            const auto afterToken = encoded.substr(textEnd + 1);

                            std::string before;
                            std::string after;

                            const auto validUtf8 = [](const std::string& text) {
                                return text.empty() ||
                                    ::MultiByteToWideChar(
                                        CP_UTF8, MB_ERR_INVALID_CHARS,
                                        text.data(),
                                        static_cast<int>(text.size()),
                                        nullptr, 0) > 0;
                            };

                            customAccepted =
                                ParseTitleUnsigned(
                                    rest.substr(0, idEnd), customID) &&
                                customID >=
                                    TULLBUNG::TitleSystem::kCustomTitleFirstID &&
                                customID <
                                    TULLBUNG::TitleSystem::kCustomTitleFirstID +
                                    TULLBUNG::TitleSystem::kCustomTitleCount &&
                                beforeToken.size() <=
                                    3 * TULLBUNG::TitleSystem::kCustomTextMaxBytes &&
                                afterToken.size() <=
                                    3 * TULLBUNG::TitleSystem::kCustomTextMaxBytes &&
                                afterToken.find('.') ==
                                    std::string_view::npos &&
                                DecodeTitleText(beforeToken, before) &&
                                DecodeTitleText(afterToken, after) &&
                                validUtf8(before) &&
                                validUtf8(after) &&
                                runtime.UpsertCustomTitle(
                                    static_cast<std::uint16_t>(customID),
                                    std::move(before),
                                    std::move(after));
                        }
                    }
                }
                else if (command.starts_with(kDelete)) {
                    customAccepted =
                        ParseTitleUnsigned(
                            command.substr(kDelete.size()), customID) &&
                        customID >=
                            TULLBUNG::TitleSystem::kCustomTitleFirstID &&
                        customID <
                            TULLBUNG::TitleSystem::kCustomTitleFirstID +
                            TULLBUNG::TitleSystem::kCustomTitleCount &&
                        runtime.DeleteCustomTitle(
                            static_cast<std::uint16_t>(customID));
                }

                // Do not log user-entered custom title text.
                const bool qaIDValid =
                    customID >= TULLBUNG::TitleSystem::kCustomTitleFirstID &&
                    customID < TULLBUNG::TitleSystem::kCustomTitleFirstID +
                        TULLBUNG::TitleSystem::kCustomTitleCount;

                const auto* qaCustom = qaIDValid ?
                    runtime.GetCustomTitle(
                        static_cast<std::uint16_t>(customID)) :
                    nullptr;

                logger::info(
                    "[S18QA][CUSTOM] Op={} ID={} Accepted={} Present={} Favorite={}",
                    command.starts_with(kSet) ? "set" :
                        (command.starts_with(kDelete) ? "delete" : "unknown"),
                    customID,
                    customAccepted,
                    qaCustom != nullptr,
                    qaCustom && qaCustom->favorite);

                const auto& qaCustomSlots = runtime.Slots();
                logger::info(
                    "[S18QA][CUSTOM_SLOTS] IDs={},{},{},{},{},{}",
                    qaCustomSlots[0].selectedTitleID,
                    qaCustomSlots[1].selectedTitleID,
                    qaCustomSlots[2].selectedTitleID,
                    qaCustomSlots[3].selectedTitleID,
                    qaCustomSlots[4].selectedTitleID,
                    qaCustomSlots[5].selectedTitleID);

                SendTitleStateSnapshot();
                return;
            }

            bool accepted = false;

            if (command ==
                "title.refresh") {

                SendTitleStateSnapshot();
                return;
            }

            constexpr std::string_view
                kSelectPrefix =
                    "title.select.";

            if (command.starts_with(
                    kSelectPrefix)) {

                auto rest =
                    command.substr(
                        kSelectPrefix.size());

                std::string_view slotText;
                std::string_view titleText;
                std::string_view segmentText;

                std::uint32_t slot = 0;
                std::uint32_t title = 0;
                std::uint32_t segment = 0;

                accepted =
                    TakeTitleToken(
                        rest,
                        slotText) &&
                    TakeTitleToken(
                        rest,
                        titleText) &&
                    TakeTitleToken(
                        rest,
                        segmentText) &&
                    rest.empty() &&
                    ParseTitleUnsigned(
                        slotText,
                        slot) &&
                    ParseTitleUnsigned(
                        titleText,
                        title) &&
                    ParseTitleUnsigned(
                        segmentText,
                        segment) &&
                    slot <=
                        TULLBUNG::TitleSystem::
                            kSlotCount &&
                    title <=
                        static_cast<
                            std::uint32_t>(
                                (std::numeric_limits<
                                    std::uint16_t>::max)()) &&
                    segment <=
                        static_cast<
                            std::uint32_t>(
                                (std::numeric_limits<
                                    std::uint8_t>::max)()) &&
                    (title != 0u ||
                        segment == 0u) &&
                    runtime.SelectTitle(
                        slot,
                        static_cast<
                            std::uint16_t>(
                                title),
                        static_cast<
                            std::uint8_t>(
                                segment));
            }
            else {

                constexpr std::string_view
                    kAutoPrefix =
                        "title.auto.";

                if (command.starts_with(
                        kAutoPrefix)) {

                    auto rest =
                        command.substr(
                            kAutoPrefix.size());

                    std::string_view slotText;
                    std::string_view enabledText;

                    std::uint32_t slot = 0;
                    std::uint32_t enabled = 0;

                    accepted =
                        TakeTitleToken(
                            rest,
                            slotText) &&
                        TakeTitleToken(
                            rest,
                            enabledText) &&
                        rest.empty() &&
                        ParseTitleUnsigned(
                            slotText,
                            slot) &&
                        ParseTitleUnsigned(
                            enabledText,
                            enabled) &&
                        enabled <= 1u &&
                        runtime.SetAuto(
                            slot,
                            enabled != 0u);
                }
                else {

                    std::uint32_t value = 0;

                    if (ParseCommandNumber(
                            command,
                            "title.favorite.",
                            value)) {

                        if (value <=
                            static_cast<
                                std::uint32_t>(
                                    (std::numeric_limits<
                                        std::uint16_t>::max)())) {

                            accepted =
                                runtime.ToggleFavorite(
                                    static_cast<
                                        std::uint16_t>(
                                            value));
                        }
                    }
                    else if (
                        ParseCommandNumber(
                            command,
                            "title.remove.",
                            value)) {

                        accepted =
                            runtime.RemoveSlot(
                                value);
                    }
                    else if (
                        ParseCommandNumber(
                            command,
                            "title.presence.",
                            value)) {

                        if (value <= 0x3Fu) {

                            accepted =
                                runtime.
                                    ApplyPresetPresenceMask(
                                        static_cast<
                                            std::uint8_t>(
                                                value));
                        }
                    }
                    else {

                        constexpr
                            std::string_view
                                kAddPrefix =
                                    "title.add.";

                        constexpr
                            std::string_view
                                kNicknamePrefix =
                                    "title.nickname.";

                        if (command.starts_with(
                                kAddPrefix)) {

                            TULLBUNG::
                                TitleSystem::Role
                                    role{};

                            if (ParseTitleRole(
                                    command.substr(
                                        kAddPrefix.size()),
                                    role)) {

                                accepted =
                                    runtime.ManualAdd(
                                        role).
                                            has_value();
                            }
                        }
                        else if (
                            command.starts_with(
                                kNicknamePrefix)) {

                            std::string nickname;

                            if (DecodeTitleText(
                                    command.substr(
                                        kNicknamePrefix.size()),
                                    nickname)) {

                                runtime.SetNickname(
                                    std::move(
                                        nickname));

                                accepted = true;
                            }
                        }
                    }
                }
            }

            const std::string_view qaOperation =
                command.starts_with("title.select.") ? "select" :
                command.starts_with("title.auto.") ? "auto" :
                command.starts_with("title.favorite.") ? "favorite" :
                command.starts_with("title.remove.") ? "remove" :
                command.starts_with("title.presence.") ? "presence" :
                command.starts_with("title.add.") ? "add" :
                command.starts_with("title.nickname.") ? "nickname" :
                "unknown";

            if (accepted) {
                logger::info(
                    "[S18QA][TITLE] Op={} Accepted=true",
                    qaOperation);
            }
            else {
                logger::warn(
                    "[S18QA][TITLE] Op={} Accepted=false",
                    qaOperation);
            }

            const auto& qaSlots = runtime.Slots();

            logger::info(
                "[S18QA][TITLE_SLOTS] IDs={},{},{},{},{},{} Favorites={}",
                qaSlots[0].selectedTitleID,
                qaSlots[1].selectedTitleID,
                qaSlots[2].selectedTitleID,
                qaSlots[3].selectedTitleID,
                qaSlots[4].selectedTitleID,
                qaSlots[5].selectedTitleID,
                runtime.FavoriteTitleIDs().size());

            SendTitleStateSnapshot();
        }
        void HandleImeToggle()
        {
            const auto reply = [](std::string_view a_state) {
                const std::string payload =
                    std::string(R"({"kind":"ime-mode","state":")") +
                    std::string(a_state) + R"("})";
                SendToUI(payload.c_str());
            };

            if (!g_api || !g_view || !g_domReady ||
                !IsVisible() || !g_api->HasFocus(g_view)) {
                logger::info("[S18UI][IME] REJECT | Reason=NoPrismaFocus");
                reply("no-focus");
                return;
            }

            const HWND hwnd = ::GetForegroundWindow();
            DWORD processId = 0;
            const DWORD threadId = hwnd ?
                ::GetWindowThreadProcessId(hwnd, &processId) : 0;
            if (!threadId || processId != ::GetCurrentProcessId()) {
                logger::info("[S18UI][IME] REJECT | Reason=NotGameForeground");
                reply("not-game");
                return;
            }

            const HKL layout = ::GetKeyboardLayout(threadId);
            if ((reinterpret_cast<UINT_PTR>(layout) & 0xFFFFu) != 0x0412u) {
                logger::info("[S18UI][IME] REJECT | Reason=KoreanLayoutInactive");
                reply("no-korean-layout");
                return;
            }

            const HIMC context = ::ImmGetContext(hwnd);
            if (!context) {
                // The Skyrim foreground HWND can have no IMM32 context
                // even when PrismaUI owns a focused Ultralight text field.
                // Do not associate or synthesize a global HIMC here.
                // Offer a single explicit VK_HANGUL key request as a
                // fallback; successful delivery is NOT mode confirmation.
                INPUT keys[2]{};
                keys[0].type = INPUT_KEYBOARD;
                keys[0].ki.wVk = VK_HANGUL;
                keys[1].type = INPUT_KEYBOARD;
                keys[1].ki.wVk = VK_HANGUL;
                keys[1].ki.dwFlags = KEYEVENTF_KEYUP;

                const UINT delivered = ::SendInput(2, keys, sizeof(INPUT));
                if (delivered != 2) {
                    // If only key-down was sent, best-effort key release.
                    if (delivered == 1) {
                        INPUT release{};
                        release.type = INPUT_KEYBOARD;
                        release.ki.wVk = VK_HANGUL;
                        release.ki.dwFlags = KEYEVENTF_KEYUP;
                        ::SendInput(1, &release, sizeof(INPUT));
                    }
                    logger::warn("[S18UI][IME] REJECT | Reason=HangulKeyDispatchFailed Events={}", delivered);
                    reply("key-failed");
                    return;
                }

                logger::info("[S18UI][IME] HangulKeyDispatched | Events=2 ModeVerified=false");
                reply("key-sent");
                return;
            }

            const bool before = ::ImmGetOpenStatus(context) != FALSE;
            const BOOL requested = ::ImmSetOpenStatus(context, before ? FALSE : TRUE);
            const bool after = ::ImmGetOpenStatus(context) != FALSE;
            ::ImmReleaseContext(hwnd, context);

            if (requested == FALSE || before == after) {
                logger::warn("[S18UI][IME] REJECT | Reason=ImmTransitionFailed Before={} After={}", before, after);
                reply("unchanged");
                return;
            }

            logger::info("[S18UI][IME] ImmOpenTransition | Before={} After={} (text-input not yet verified)", before, after);
            reply(after ? "ko" : "en");
        }

        void ExecuteJSCommand(
            const char* a_argument)
        {
            const std::string_view command =
                a_argument ?
                    std::string_view(
                        a_argument) :
                    std::string_view{};

            if (command == "close" ||
                command == "hud.editor.open" ||
                command == "hud.editor.close") {
                logger::info(
                    "[S18QA][UI] Event={}",
                    command);
            }

            if (command == "ime.toggle") {
                HandleImeToggle();
                return;
            }

            if (command == "close") {

                Close();
                return;
            }

            if (HandleSettingsCommand(
                    command)) {

                return;
            }

            if (command == "dev.extension.toggle") {
                const bool allowed =
                    g_domReady && g_gameReady &&
                    TULLBUNG::UISettings::Get().developerMode;
                if (!allowed) {
                    SendDevStatus("DEV extension unavailable");
                } else if (!g_s19DevExtensionUnlocked) {
                    // Block all class-title reacquisition while clearing QA.
                    TULLBUNG::TitleRuntime::SetQAEvaluationSuspended(true);
                    if (ResetS19QAClassesAndNodes()) {
                        g_s19DevExtensionUnlocked = true;
                        g_s19GapSequence = 0;
                        logger::info("[S19QA] Session START | clean build verified");
                        SendBuildStateSnapshot();
                    } else {
                        TULLBUNG::TitleRuntime::SetQAEvaluationSuspended(false);
                        (void)TULLBUNG::TitleRuntime::RefreshFromBuildState();
                        SendDevStatus("QA entry reset FAILED; reload baseline save");
                    }
                } else {
                    (void)EndS19QASession();
                }
                SendToUI(g_s19DevExtensionUnlocked ?
                    R"({"kind":"dev-extension","enabled":true})" :
                    R"({"kind":"dev-extension","enabled":false})");
                return;
            }

            const bool s19ExtendedCommand =
                command.starts_with("qa.s19.") ||
                command.starts_with("qa.profile.") ||
                command.starts_with("qa.dump") ||
                command.starts_with("dev.equip.") ||
                command.starts_with("dev.spell.") ||
                command.starts_with("dev.spells.") ||
                command.starts_with("debug.inq.") ||
                command.starts_with("debug.artifacts.") ||
                command.starts_with("debug.void.");

            if (s19ExtendedCommand &&
                (!g_s19DevExtensionUnlocked ||
                 !TULLBUNG::UISettings::Get().developerMode)) {
                logger::warn("[S19DEV] Command rejected | Extension locked");
                SendDevStatus("S19 DEV extension locked");
                return;
            }

            if (command == "qa.s19.reset.all") {
                logger::info("[S19QA-RESET] Event=REQUEST");
                const bool ok = ResetS19QAClassesAndNodes();
                SendDevStatus(ok ?
                    "S19 QA class/node reset confirmed" :
                    "S19 QA class/node reset FAILED - reload baseline save");
                SendBuildStateSnapshot();
                SendToUI(ok ?
                    R"({"kind":"s19-reset-result","ok":true})" :
                    R"({"kind":"s19-reset-result","ok":false})");
                return;
            }
            constexpr std::string_view kS19CasePrefix = "qa.s19.case.";
            if(command.starts_with(kS19CasePrefix)){
                logger::info("[S19QA-CASE] Event=REQUEST | ID={}", command.substr(kS19CasePrefix.size()));
                (void)ApplyS19QACasePreset(
                    command.substr(kS19CasePrefix.size()));
                return;
            }
            constexpr std::string_view kS19MaskPrefix =
                "qa.s19.mask.";
            if (command.starts_with(kS19MaskPrefix)) {
                const auto literal = command.substr(kS19MaskPrefix.size());
                std::uint32_t mask = 0;
                const auto parse = std::from_chars(
                    literal.data(), literal.data()+literal.size(), mask);
                if (literal.empty() || parse.ec != std::errc{} ||
                    parse.ptr != literal.data()+literal.size() ||
                    mask == 0 || mask >= (1U << kClassCount)) {
                    SendDevStatus("Invalid S19 class mask");
                    logger::warn("[S19QA] Invalid mask argument");
                    return;
                }
                (void)ApplyS19QAClassMask(
                    static_cast<std::uint16_t>(mask));
                return;
            }

            if (command ==
                "hud.editor.open") {

                g_hudEditorActive =
                    true;

                SendVoidHudSnapshot();
                return;
            }

            if (command ==
                "hud.editor.close") {

                g_hudEditorActive =
                    false;

                SendVoidHudSnapshot();
                return;
            }

            if (command ==
                "build.refresh") {

                SendBuildStateSnapshot();
                return;
            }

            if (command ==
                "build.reset") {

                ResetAllNodes();
                SendBuildStateSnapshot();

                return;
            }

            if (command.starts_with("debug.jp.") &&
                !TULLBUNG::UISettings::Get().developerMode) {
                logger::warn("[S19DEV] DebugJP command rejected | Developer mode off");
                return;
            }

            if (command ==
                "debug.jp.add.10") {

                AddDebugJP(10);
                SendBuildStateSnapshot();
                return;
            }

            if (command ==
                "debug.jp.add.50") {

                AddDebugJP(50);
                SendBuildStateSnapshot();
                return;
            }

            if (command == "debug.jp.add.200") {
                if (!g_s19DevExtensionUnlocked ||
                    !TULLBUNG::UISettings::Get().developerMode) {
                    logger::warn("[S19QA] +200 JP rejected: QA extension locked");
                    return;
                }
                AddDebugJP(200);
                SendBuildStateSnapshot();
                logger::info("[S19QA] DebugJP +200 | Total={}", GetDebugJP());
                return;
            }

            if (command ==
                "debug.jp.clear") {

                ClearDebugJP();
                SendBuildStateSnapshot();
                return;
            }

            if (command ==
                "debug.inq.kills.clear") {

                DebugSetInquisitorKills(
                    0);

                return;
            }

            if (command ==
                "debug.artifacts.add.all") {

                DebugGiveDaedricArtifacts(
                    0,
                    true);

                return;
            }

            if (command == "debug.void.set.0") {
                TULLBUNG::Persistence::
                    SetVoidCount(0);
                SendBuildStateSnapshot();
                return;
            }

            if (command == "debug.void.set.1") {
                TULLBUNG::Persistence::
                    SetVoidCount(1);
                SendBuildStateSnapshot();
                return;
            }

            if (command == "debug.void.set.2") {
                TULLBUNG::Persistence::
                    SetVoidCount(2);
                SendBuildStateSnapshot();
                return;
            }

            if (command == "debug.void.set.3") {
                TULLBUNG::Persistence::
                    SetVoidCount(3);
                SendBuildStateSnapshot();
                return;
            }

            if (command == "debug.void.set.4") {
                TULLBUNG::Persistence::
                    SetVoidCount(4);
                SendBuildStateSnapshot();
                return;
            }

            // Stage14 DEV safety rule:
            // Direct ActorValue QA automation is disabled.
            // Use Skyrim console for HP/MP/SP/defense/resist/regen.
            if (command == "qa.zero.full" ||
                command == "qa.zero.state" ||
                command == "qa.resource.full" ||
                command == "qa.resource.half" ||
                command == "qa.defense.zero" ||
                command == "qa.regen.baseline" ||
                command.starts_with("qa.hp.pct.") ||
                command.starts_with("qa.mp.") ||
                command.starts_with("qa.sp.")) {

                logger::warn(
                    "[DEV] Unsafe direct-stat QA command blocked");

                SendDevStatus(
                    "Direct stat QA disabled - use Skyrim console");

                return;
            }

            // S19 DEV safety gate: prevent legacy full ActorValue snapshots.
            // The old handlers remain below for historical source continuity,
            // but are intentionally unreachable via UI commands.
            if (command.starts_with("qa.dump")) {
                logger::warn("[S19DEV] Legacy ActorValue QA dump blocked");
                SendDevStatus("S19: ActorValue QA dump disabled");
                return;
            }

            if (command ==
                "qa.dump.baseline") {

                TULLBUNG::PlayerState::
                    RefreshAll();

                DumpQAState(
                    "BASELINE");

                return;
            }

            if (command ==
                "qa.dump.pre") {

                TULLBUNG::PlayerState::
                    RefreshAll();

                DumpQAState(
                    "PRE");

                return;
            }

            if (command ==
                "qa.dump.post") {

                TULLBUNG::PlayerState::
                    RefreshAll();

                DumpQAState(
                    "POST");

                return;
            }

            if (command ==
                "qa.dump") {

                TULLBUNG::PlayerState::
                    RefreshAll();

                DumpQAState(
                    "MANUAL_DUMP");

                return;
            }
            constexpr std::string_view
                kQAProfilePrefix =
                    "qa.profile.";

            if (command.starts_with(
                    kQAProfilePrefix)) {

                ApplyQAProfile(
                    command.substr(
                        kQAProfilePrefix.
                            size()));

                return;
            }

            if (command ==
                "dev.equip.clear") {

                logger::info("[S19QA-GAP] Event=REQUEST | Source=DEV_BUTTON | NextSeq={}",
                    g_s19GapSequence + 1);
                if (ClearCombatEquipment()) {
                    ++g_s19GapSequence;
                    logger::info("[S19QA-GAP] Event=VALID | Source=DEV_BUTTON | Seq={}",
                        g_s19GapSequence);
                    LogS19GAPSnapshot(g_s19GapSequence);
                } else {
                    logger::warn("[S19QA-GAP] Event=INVALID | Source=DEV_BUTTON | NextSeq={} | BoundaryCommitted=false",
                        g_s19GapSequence + 1);
                }
                return;
            }

            if (command ==
                "dev.equip.dagger") {

                GiveDaggerSet();
                return;
            }

            if (command ==
                "dev.equip.bow") {

                GiveBowSet();
                return;
            }

            if (command ==
                "dev.equip.crossbow") {

                GiveCrossbowSet();
                return;
            }

            if (command ==
                "dev.equip.mace_shield") {

                GiveMaceShieldSet();
                return;
            }

            // S19 B6: new commands remain behind the existing
            // Developer Mode AND hidden DEV extension Native gate.
            if (command == "dev.equip.dual_dagger") {
                GiveS19Weapon(0x0001397E, "철 단검 2개", 2);
                return;
            }
            if (command == "dev.equip.iron_sword") {
                GiveS19Weapon(0x00012EB7, "철검");
                return;
            }
            if (command == "dev.equip.iron_greatsword") {
                GiveS19Weapon(0x0001359D, "철 양손검");
                return;
            }
            if (command == "dev.equip.iron_battleaxe") {
                GiveS19Weapon(0x00013980, "철 전투도끼");
                return;
            }
            if (command == "dev.equip.light_pair") {
                GiveS19ArmorPair(0x0003619E, 0x00013920, "가죽 경갑 2부위");
                return;
            }
            if (command == "dev.equip.heavy_pair") {
                GiveS19ArmorPair(0x00012E49, 0x00012E4B, "철 중갑 2부위");
                return;
            }
            if (command == "dev.spell.boundbow.right") {
                EquipStage15Spell(0x000211ED, false, "Bound Bow");
                return;
            }
            if (command == "dev.spell.boundbattleaxe.right") {
                EquipStage15Spell(0x000211EC, false, "Bound Battleaxe");
                return;
            }

            if (command ==
                "dev.spells.stage15.give") {

                GiveStage15SpellSet();
                return;
            }

            if (command ==
                "dev.spell.healing.left") {

                EquipStage15Spell(
                    0x00012FCC,
                    true,
                    "Healing");

                return;
            }

            if (command ==
                "dev.spell.flames.right") {

                EquipStage15Spell(
                    0x00012FCD,
                    false,
                    "Flames");

                return;
            }

            if (command ==
                "dev.spell.firebolt.right") {

                EquipStage15Spell(
                    0x00012FD0,
                    false,
                    "Firebolt");

                return;
            }

            if (command ==
                "dev.spell.lightningbolt.right") {

                EquipStage15Spell(
                    0x0002DD29,
                    false,
                    "Lightning Bolt");

                return;
            }

            if (command ==
                "dev.spell.fireball.right") {

                EquipStage15Spell(
                    0x0001C789,
                    false,
                    "Fireball");

                return;
            }

            if (command ==
                "dev.spell.wallflames.right") {

                EquipStage15Spell(
                    0x00035D7F,
                    false,
                    "Wall of Flames");

                return;
            }

            if (command ==
                "dev.spell.firerune.right") {

                EquipStage15Spell(
                    0x0005DB90,
                    false,
                    "Fire Rune");

                return;
            }

            if (command ==
                "dev.spell.boundsword.left") {

                EquipStage15Spell(
                    0x000211EB,
                    true,
                    "Bound Sword");

                return;
            }

            std::uint32_t value = 0;

            if (ParseCommandNumber(
                    command,
                    "debug.inq.kills.set.",
                    value)) {

                DebugSetInquisitorKills(
                    value);

                return;
            }

            if (ParseCommandNumber(
                    command,
                    "debug.inq.kills.add.",
                    value)) {

                DebugAddInquisitorKills(
                    value);

                return;
            }

            if (ParseCommandNumber(
                    command,
                    "debug.artifacts.add.",
                    value)) {

                DebugGiveDaedricArtifacts(
                    value,
                    false);

                return;
            }

            if (ParseCommandNumber(
                    command,
                    "qa.hp.pct.",
                    value)) {

                const auto pct =
                    (std::min)(
                        value,
                        100u);

                (void)SetQAResourcePercent(
                    RE::ActorValue::
                        kHealth,
                    static_cast<float>(
                        pct));

                DumpQAState(
                    "HP_PERCENT");

                return;
            }

            if (ParseCommandNumber(
                    command,
                    "qa.mp.",
                    value)) {

                (void)SetQAResourceCurrent(
                    RE::ActorValue::
                        kMagicka,
                    static_cast<float>(
                        value));

                DumpQAState(
                    "MAGICKA_CURRENT");

                return;
            }

            if (ParseCommandNumber(
                    command,
                    "qa.sp.",
                    value)) {

                (void)SetQAResourceCurrent(
                    RE::ActorValue::
                        kStamina,
                    static_cast<float>(
                        value));

                DumpQAState(
                    "STAMINA_CURRENT");

                return;
            }

            if (ParseCommandNumber(
                    command,
                    "build.acquire.",
                    value)) {

                if (value <
                    static_cast<std::uint32_t>(
                        ClassID::kCount)) {

                    (void)TryAcquireClass(
                        static_cast<ClassID>(
                            value));
                }

                SendBuildStateSnapshot();
                return;
            }

            if (ParseCommandNumber(
                    command,
                    "build.purchase.",
                    value)) {

                if (value <
                    kNodeStorageSize) {

                    (void)TryPurchaseNode(
                        static_cast<NodeID>(
                            value));
                }

                SendBuildStateSnapshot();
                return;
            }

            if (ParseCommandNumber(
                    command,
                    "build.toggle.",
                    value)) {

                if (value <
                    kNodeStorageSize) {

                    (void)ToggleNode(
                        static_cast<NodeID>(
                            value));
                }

                SendBuildStateSnapshot();
                return;
            }
        }

        bool HasOtherPrismaFocus()
        {
            return g_api->HasAnyActiveFocus() &&
                (!HasView() || !g_api->HasFocus(g_view));
        }

        bool ShowMainViewWithPause()
        {
            if (HasOtherPrismaFocus()) {
                logger::info("[PrismaDebug] Main view open refused | Other Prisma view focused");
                Close();
                return false;
            }

            g_api->Show(g_view);
            if (!g_api->Focus(g_view, true, false)) {
                logger::warn("[PrismaDebug] Main view focus request rejected");
                Close();
                return false;
            }

            logger::info("[PrismaDebug] Main view focus requested | PauseGame=true");
            return true;
        }

        void SendMainViewStartup()
        {
            SendToUI(R"({"kind":"cpp-ready","dllVersion":")" TULLBUNG_VERSION_LABEL R"("})");
            SendBuildStateSnapshot();
            SendTitleCatalog();
            SendTitleStateSnapshot();
        }

        void FinishMainDomReady(PrismaView a_view)
        {
            std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
            if (!g_createPending || !a_view || a_view != g_view || !HasView()) {
                return;
            }

            g_createPending = false;
            g_domReady = true;
            g_titleCatalogSent = false;

            if (!g_gameReady || !g_mainOpenRequested || !ShowMainViewWithPause()) {
                return;
            }

            SendSettingsSnapshot();
            SendToUI(g_s19DevExtensionUnlocked ?
                R"({"kind":"dev-extension","enabled":true})" :
                R"({"kind":"dev-extension","enabled":false})");
            SendUserFontList();
            SendMainViewStartup();
        }

        void OnDomReady(PrismaView a_view)
        {
            // PrismaUI 1.4.1 calls back while holding its view registry lock.
            // Leave that lock before calling back into the view API.
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([a_view]() { FinishMainDomReady(a_view); });
            }
        }

        bool CancelPendingMainOpen()
        {
            std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
            if (!g_createPending || !g_mainOpenRequested) {
                return false;
            }
            Close();
            return true;
        }

        void OnVoidHudDomReady(
            PrismaView a_view)
        {
            if (!g_api ||
                !a_view ||
                a_view != g_voidHudView ||
                !g_api->IsValid(a_view)) {

                return;
            }

            g_voidHudCreatePending = false;
            g_voidHudDomReady = true;

            SendVoidHudSnapshot();

            logger::info(
                "[PrismaVoidHUD] DOM ready | View={}",
                g_voidHudView);
        }


        void EnsureVoidHudView()
        {
            if (!g_api ||
                g_voidHudCreatePending) {

                return;
            }

            if (g_voidHudView &&
                g_api->IsValid(
                    g_voidHudView)) {

                SendVoidHudSnapshot();
                return;
            }

            g_voidHudDomReady = false;
            g_voidHudCreatePending = true;

            g_voidHudView =
                g_api->CreateView(
                    kVoidHudViewPath,
                    OnVoidHudDomReady);

            if (!g_voidHudView) {
                g_voidHudCreatePending = false;

                logger::error(
                    "[PrismaVoidHUD] CreateView failed");

                return;
            }

            logger::info(
                "[PrismaVoidHUD] CreateView requested | View={}",
                g_voidHudView);
        }

        class InputSink final :
            public RE::BSTEventSink<
                RE::InputEvent*>
        {
        public:
            static InputSink*
                GetSingleton()
            {
                static InputSink singleton;

                return std::addressof(
                    singleton);
            }

            static void NeutralizeKeyboardButton(
                RE::ButtonEvent* a_button) noexcept
            {
                if (!a_button) {
                    return;
                }

                a_button->userEvent =
                    RE::BSFixedString{};

                a_button->idCode = 0;
                a_button->value = 0.0F;
                a_button->heldDownSecs = 0.0F;
            }

            RE::BSEventNotifyControl
                ProcessEvent(
                    RE::InputEvent* const*
                        a_events,
                    RE::BSTEventSource<
                        RE::InputEvent*>*)
                    override
            {
                if (!a_events) {

                    return
                        RE::BSEventNotifyControl::
                            kContinue;
                }

                static std::array<
                    bool,
                    256>
                    suppressedKeyboardKeys{};

                const bool firewallAtDispatch =
                    g_hotkeyCaptureActive ||
                    g_createPending ||
                    IsVisible();

                static bool leftShiftDown = false;
                static bool rightShiftDown = false;
                static bool leftCtrlDown = false;
                static bool rightCtrlDown = false;
                static bool leftAltDown = false;
                static bool rightAltDown = false;

                for (auto* input = *a_events;
                     input;
                     input = input->next) {

                    auto* button =
                        input->AsButtonEvent();

                    if (!button ||
                        button->GetDevice() !=
                            RE::INPUT_DEVICE::
                                kKeyboard) {

                        continue;
                    }

                    const auto keyCode =
                        button->GetIDCode();

                    const bool isDown =
                        button->IsDown();

                    const bool isUp =
                        button->IsUp();

                    if (!isDown &&
                        !isUp) {

                        continue;
                    }

                    const bool downState =
                        isDown;

                    switch (keyCode) {
                    case static_cast<
                        std::uint32_t>(
                            REX::W32::
                                DIK_LSHIFT):

                        leftShiftDown =
                            downState;
                        break;

                    case static_cast<
                        std::uint32_t>(
                            REX::W32::
                                DIK_RSHIFT):

                        rightShiftDown =
                            downState;
                        break;

                    case static_cast<
                        std::uint32_t>(
                            REX::W32::
                                DIK_LCONTROL):

                        leftCtrlDown =
                            downState;
                        break;

                    case static_cast<
                        std::uint32_t>(
                            REX::W32::
                                DIK_RCONTROL):

                        rightCtrlDown =
                            downState;
                        break;

                    case static_cast<
                        std::uint32_t>(
                            REX::W32::
                                DIK_LMENU):

                        leftAltDown =
                            downState;
                        break;

                    case static_cast<
                        std::uint32_t>(
                            REX::W32::
                                DIK_RMENU):

                        rightAltDown =
                            downState;
                        break;

                    default:
                        break;
                    }

                    if (!isDown) {
                        continue;
                    }

                    bool shiftDown =
                        leftShiftDown ||
                        rightShiftDown;

                    bool ctrlDown =
                        leftCtrlDown ||
                        rightCtrlDown;

                    bool altDown =
                        leftAltDown ||
                        rightAltDown;

                    const bool modifierKey =
                        keyCode ==
                            static_cast<
                                std::uint32_t>(
                                    REX::W32::
                                        DIK_LSHIFT) ||
                        keyCode ==
                            static_cast<
                                std::uint32_t>(
                                    REX::W32::
                                        DIK_RSHIFT) ||
                        keyCode ==
                            static_cast<
                                std::uint32_t>(
                                    REX::W32::
                                        DIK_LCONTROL) ||
                        keyCode ==
                            static_cast<
                                std::uint32_t>(
                                    REX::W32::
                                        DIK_RCONTROL) ||
                        keyCode ==
                            static_cast<
                                std::uint32_t>(
                                    REX::W32::
                                        DIK_LMENU) ||
                        keyCode ==
                            static_cast<
                                std::uint32_t>(
                                    REX::W32::
                                        DIK_RMENU);

                    // Event-driven correction only for a menu hotkey or
                    // a pending hotkey capture; never change game inputs.
                    if (!modifierKey &&
                        (g_hotkeyCaptureActive ||
                         keyCode == TULLBUNG::UISettings::
                             Get().menuHotkey.keyCode)) {

                        DWORD foregroundPid = 0;
                        const auto foreground = ::GetForegroundWindow();
                        if (foreground &&
                            ::GetWindowThreadProcessId(
                                foreground, &foregroundPid) != 0 &&
                            foregroundPid == ::GetCurrentProcessId()) {

                            const auto pressed = [](int virtualKey) {
                                return (::GetAsyncKeyState(virtualKey) &
                                        static_cast<SHORT>(0x8000)) != 0;
                            };

                            const bool physicalLeftShift = pressed(VK_LSHIFT);
                            const bool physicalRightShift = pressed(VK_RSHIFT);
                            const bool physicalLeftCtrl = pressed(VK_LCONTROL);
                            const bool physicalRightCtrl = pressed(VK_RCONTROL);
                            const bool physicalLeftAlt = pressed(VK_LMENU);
                            const bool physicalRightAlt = pressed(VK_RMENU);

                            const bool realShift =
                                physicalLeftShift || physicalRightShift;
                            const bool realCtrl =
                                physicalLeftCtrl || physicalRightCtrl;
                            const bool realAlt =
                                physicalLeftAlt || physicalRightAlt;

                            if (shiftDown != realShift ||
                                ctrlDown != realCtrl ||
                                altDown != realAlt) {

                                logger::warn(
                                    "[S18UI][HOTKEY] ModifierResync | "
                                    "Event={}/{}/{} Physical={}/{}/{} Key={}",
                                    shiftDown, ctrlDown, altDown,
                                    realShift, realCtrl, realAlt, keyCode);
                            }

                            leftShiftDown = physicalLeftShift;
                            rightShiftDown = physicalRightShift;
                            leftCtrlDown = physicalLeftCtrl;
                            rightCtrlDown = physicalRightCtrl;
                            leftAltDown = physicalLeftAlt;
                            rightAltDown = physicalRightAlt;

                            shiftDown = realShift;
                            ctrlDown = realCtrl;
                            altDown = realAlt;
                        }
                    }

                    if (g_hotkeyCaptureActive) {

                        if (keyCode ==
                            static_cast<
                                std::uint32_t>(
                                    REX::W32::
                                        DIK_ESCAPE)) {

                            g_hotkeyCaptureActive =
                                false;

                            logger::info(
                                "[S18UI] Hotkey capture cancelled by ESC");

                            SendSettingsSnapshot();
                            break;
                        }

                        if (modifierKey) {
                            continue;
                        }

                        const auto previous =
                            TULLBUNG::UISettings::
                                Get();

                        auto updated =
                            previous;

                        updated.menuHotkey.keyCode =
                            keyCode;

                        updated.menuHotkey.shift =
                            shiftDown;

                        updated.menuHotkey.ctrl =
                            ctrlDown;

                        updated.menuHotkey.alt =
                            altDown;

                        if (!TULLBUNG::UISettings::
                                Apply(
                                    updated,
                                    true)) {

                            (void)TULLBUNG::UISettings::
                                Apply(
                                    previous,
                                    false);

                            logger::error(
                                "[S18UI] Hotkey capture save failed");

                            break;
                        }

                        g_hotkeyCaptureActive =
                            false;

                        logger::info(
                            "[S18UI] Hotkey captured | Key={} Shift={} Ctrl={} Alt={}",
                            keyCode,
                            shiftDown,
                            ctrlDown,
                            altDown);

                        SendSettingsSnapshot();
                        break;
                    }

                    if (keyCode == static_cast<std::uint32_t>(REX::W32::DIK_ESCAPE)) {
                        if (CancelPendingMainOpen()) {
                            break;
                        }
                        if (IsVisible()) {
                            SendToUI(R"({"kind":"native-escape"})");
                            logger::info("[S18UI] ESC routed to UI");
                            break;
                        }
                    }

                    const auto& hotkey =
                        TULLBUNG::UISettings::
                            Get().menuHotkey;

                    if (keyCode == hotkey.keyCode) {
                        logger::info(
                            "[S18UI][HOTKEY] code={} mods={}/{}/{} wanted={}/{}/{} ready={} pending={} view={} visible={} editor={}",
                            keyCode, shiftDown, ctrlDown, altDown,
                            hotkey.shift, hotkey.ctrl, hotkey.alt,
                            g_gameReady, g_createPending,
                            HasView(), IsVisible(), g_hudEditorActive);
                    }

                    if (keyCode ==
                            hotkey.keyCode &&
                        shiftDown ==
                            hotkey.shift &&
                        ctrlDown ==
                            hotkey.ctrl &&
                        altDown ==
                            hotkey.alt) {

                        Toggle();
                        break;
                    }
                }

                const bool firewallAfterDispatch =
                    g_hotkeyCaptureActive ||
                    g_createPending ||
                    IsVisible();

                const bool firewallActive =
                    firewallAtDispatch ||
                    firewallAfterDispatch;

                for (auto* input = *a_events;
                     input;
                     input = input->next) {

                    auto* button =
                        input->AsButtonEvent();

                    if (!button ||
                        button->GetDevice() !=
                            RE::INPUT_DEVICE::
                                kKeyboard) {

                        continue;
                    }

                    const auto keyCode =
                        button->GetIDCode();

                    const bool isDown =
                        button->IsDown();

                    const bool isHeld =
                        button->IsHeld();

                    const bool isUp =
                        button->IsUp();

                    bool suppress =
                        false;

                    if (keyCode <
                        suppressedKeyboardKeys.
                            size()) {

                        auto& suppressed =
                            suppressedKeyboardKeys[
                                keyCode];

                        if (isDown) {

                            if (firewallActive) {

                                suppressed =
                                    true;

                                suppress =
                                    true;
                            }
                            else {

                                /*
                                    Clears any stale ledger state
                                    before a new unsuppressed press.
                                */
                                suppressed =
                                    false;
                            }
                        }
                        else if (isHeld) {

                            /*
                                Held input is blocked while the UI
                                is active, but a key that began before
                                the UI opened does not gain a fake
                                suppressed-down ledger entry.
                            */
                            suppress =
                                firewallActive ||
                                suppressed;
                        }
                        else if (isUp) {

                            /*
                                Release is swallowed only when its
                                own down was swallowed.
                            */
                            suppress =
                                suppressed;

                            suppressed =
                                false;
                        }
                        else if (firewallActive) {

                            suppress =
                                true;
                        }
                    }
                    else if (firewallActive) {

                        /*
                            DIK keyboard ids are inside the ledger
                            range. Fail-closed for an unexpected id.
                        */
                        suppress =
                            true;
                    }

                    if (suppress) {

                        NeutralizeKeyboardButton(
                            button);
                    }
                }

                return
                    RE::BSEventNotifyControl::
                        kContinue;
            }
        };
    }

    bool InstallInput()
    {
        if (g_inputInstalled) {
            return true;
        }

        auto* input =
            RE::BSInputDeviceManager::
                GetSingleton();

        if (!input) {

            logger::error(
                "[PrismaDebug] Input manager unavailable");

            return false;
        }

        auto* sink =
            InputSink::GetSingleton();

        input->AddEventSink(
            sink);

        {
            RE::BSSpinLockGuard locker(
                input->lock);

            auto sinkIt =
                std::find(
                    input->sinks.begin(),
                    input->sinks.end(),
                    sink);

            if (sinkIt ==
                input->sinks.end()) {

                logger::error(
                    "[S18UI] Input firewall sink registration missing");

                return false;
            }

            if (sinkIt !=
                input->sinks.begin()) {

                std::rotate(
                    input->sinks.begin(),
                    sinkIt,
                    sinkIt + 1);
            }

            if (input->sinks.empty() ||
                *input->sinks.begin() !=
                    sink) {

                logger::error(
                    "[S18UI] Input firewall sink priority failed");

                return false;
            }
        }
        g_inputInstalled = true;

        logger::info(
            "[PrismaDebug] Input sink installed");

        return true;
    }

    bool Initialize()
    {
        if (g_initialized) {

            return
                g_api != nullptr;
        }

        g_apiV2 =
            PRISMA_UI_API::
                RequestPluginAPI<
                    PRISMA_UI_API::
                        IVPrismaUI2>();

        if (g_apiV2) {

            g_api =
                g_apiV2;
        }
        else {

            g_api =
                PRISMA_UI_API::
                    RequestPluginAPI<
                        PRISMA_UI_API::
                            IVPrismaUI1>();
        }

        if (!g_api) {

            logger::error(
                "[PrismaDebug] Prisma API unavailable");

            return false;
        }

        TULLBUNG::FloatingTitleMinimal::Configure(g_api, [](const char* payload) {
            static std::atomic<unsigned> statusCount{};
            const bool deliver=g_api&&g_view&&g_domReady&&g_api->IsValid(g_view);
            if(statusCount.fetch_add(1,std::memory_order_relaxed)<8)
                logger::info("[FTTRACE] status-interop | Deliver={} | Thread={}",deliver,GetCurrentThreadId());
            if(deliver)g_api->InteropCall(g_view,"tullFTStatus",payload);
        });
        g_initialized = true;

        return true;
    }

    void SetGameReady(
        bool a_ready)
    {
        std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
        TULLBUNG::UIProfiles::SetDiagnosticSink([](const std::string& line){logger::info("[S20A5] {}",line);});
        TULLBUNG::UIProfiles::SetAvailable(a_ready);
        if (!a_ready) {
            TULLBUNG::FloatingTitleMinimal::SetGameReady(false);
            // Save/load transition overwrites the old QA build. Do NOT
            // execute an exit-reset against the newly loaded save.
            g_s19DevExtensionUnlocked = false;
            TULLBUNG::TitleRuntime::SetQAEvaluationSuspended(false);
            g_hudEditorActive = false;
            g_hotkeyCaptureActive = false;
        }
        g_gameReady =
            a_ready;


        TULLBUNG::HudOverlay::
            SetGameReady(
                g_gameReady);

        TULLBUNG::HudOverlay::
            RefreshRuntimeVisibilityFromGame();

        if (g_gameReady) {

            if (Initialize()) {
                EnsureVoidHudView();
                SendUserFontList();
                TULLBUNG::FloatingTitleMinimal::SetGameReady(true);
            }
        }
        else if (
            g_api &&
            g_voidHudView &&
            g_api->IsValid(
                g_voidHudView)
        ) {
            g_api->Hide(
                g_voidHudView);
        }
        if (!g_gameReady && (g_mainOpenRequested || IsVisible())) {
            Close();
        }
    }

    void NotifyHudStateChanged() noexcept
    {
        SendVoidHudSnapshot();
        const auto visibility = TULLBUNG::HudOverlay::GetSnapshot().runtime;
        if(!visibility.gameplayContext || !visibility.skyrimUIVisible)
            TULLBUNG::FloatingTitleMinimal::SuspendForMenu();
    }
    void NotifyVoidCountChanged() noexcept
    {
        NotifyHudStateChanged();
    }

    bool HasView()
    {
        return
            g_api &&
            g_view &&
            g_api->IsValid(
                g_view);
    }

    bool IsVisible()
    {
        return
            HasView() &&
            !g_api->IsHidden(
                g_view);
    }

    void Open()
    {
        std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
        if (!g_gameReady || g_mainOpenRequested || !Initialize()) {
            return;
        }

        if (HasOtherPrismaFocus()) {
            logger::info("[PrismaDebug] Main view open refused | Other Prisma view focused");
            return;
        }

        g_mainOpenRequested = true;
        if (g_createPending) {
            return;
        }

        TULLBUNG::FloatingTitleMinimal::SuspendForMenu();
        if (HasView()) {
            if (!ShowMainViewWithPause()) {
                return;
            }

            SendToUI(R"({"kind":"native-main-reopen"})");
            SendToUI(g_s19DevExtensionUnlocked ?
                R"({"kind":"dev-extension","enabled":true})" :
                R"({"kind":"dev-extension","enabled":false})");

            SendSettingsSnapshot();
            SendUserFontList();
            SendVoidHudSnapshot();
            if (!g_titleCatalogSent) {
                SendMainViewStartup();
            } else {
                SendBuildStateSnapshot();
            }

            return;
        }

        g_hudEditorActive = false;
        // Recreated Prisma UI does not terminate an active QA session.
        g_domReady = false;
        g_createPending = true;

        g_view =
            g_api->CreateView(
                kViewPath,
                OnDomReady);

        if (!g_view) {
            g_createPending = false;
            g_mainOpenRequested = false;

            logger::error(
                "[PrismaDebug] CreateView failed");

            return;
        }

        g_api->RegisterJSListener(
            g_view,
            "tullDebugCommand",
            OnJSCommand);
        g_api->RegisterJSListener(
            g_view,
            "tullTitleCommand",
            OnTitleJSCommand);

        g_api->RegisterJSListener(g_view,"tullProfileCommand",OnProfileJSCommand);

        if (g_apiV2) {

            g_apiV2->
                RegisterConsoleCallback(
                    g_view,
                    OnConsoleMessage);
        }
    }

    void Close()
    {
        std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
        g_mainOpenRequested = false;
        TULLBUNG::FloatingTitleMinimal::RefreshTitles();
        // S19-B7: UI hide during combat QA must retain the QA class build.
        // Actual QA exit is explicit toggle OFF / Developer Mode OFF.
        g_hudEditorActive = false;
        g_hotkeyCaptureActive = false;
        if (!HasView()) {
            return;
        }

        g_api->Hide(
            g_view);

        SendVoidHudSnapshot();

        logger::info(
            "[PrismaDebug] View hide requested | View={}",
            g_view);
    }

    void Toggle()
    {
        std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
        if (!g_gameReady) {
            return;
        }

        if (g_mainOpenRequested) {
            Close();
        }
        else {
            Open();
        }
    }
}
