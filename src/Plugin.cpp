#include "PCH.h"
#include "DataCache.h"
#include "Classification.h"
#include "PlayerState.h"
#include "Persistence.h"
#include "BuildState.h"
#include "ClassRuntime.h"
#include "TitleRuntime.h"
#include "Combat.h"
#include "UISettings.h"
#include "Prisma/HudOverlay.h"
#include "Prisma/PrismaBridge.h"
#include "Prisma/FloatingTitleMinimal.h"

namespace
{
    void InitializeLog()
    {
        auto path =
            logger::log_directory();

        if (!path) {
            return;
        }

        *path /=
            "PathOfTULLBUNG.log";

        auto sink =
            std::make_shared<
                spdlog::sinks::basic_file_sink_mt>(
                    path->string(),
                    true);

        auto log =
            std::make_shared<spdlog::logger>(
                "global log",
                std::move(sink));

        log->set_level(
            spdlog::level::info);

        log->flush_on(
            spdlog::level::info);

        spdlog::set_default_logger(
            std::move(log));

        spdlog::set_pattern(
            "[%H:%M:%S.%e] [%l] %v");
    }

    void OnSKSEMessage(
        SKSE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        switch (a_message->type) {
        case SKSE::MessagingInterface::kPostLoad:
        {
            logger::info(
                "[PathOfTULLBUNG] PostLoad");

            break;
        }

        case SKSE::MessagingInterface::kDataLoaded:
        {
            logger::info(
                "[PathOfTULLBUNG] DataLoaded");

            if (!TULLBUNG::DataCache::Initialize()) {
                return;
            }

            if (!TULLBUNG::PlayerState::Initialize()) {
                return;
            }

            if (!TULLBUNG::BuildState::Initialize()) {
                return;
            }

            if (!TULLBUNG::ClassRuntime::Initialize()) {
                return;
            }

            if (!TULLBUNG::Combat::Initialize()) {
                return;
            }

            if (!TULLBUNG::UISettings::Load()) {
                logger::warn(
                    "[PathOfTULLBUNG] UI settings load failed; defaults retained");
            }

            if (!TULLBUNG::HudOverlay::
                    InstallRuntimeVisibilityGate()) {

                logger::warn(
                    "[PathOfTULLBUNG] HUD runtime visibility gate install failed");
            }
            if (!TULLBUNG::PrismaBridge::
                    InstallInput()) {

                logger::error(
                    "[PathOfTULLBUNG] Prisma input install failed");
            }

            logger::info(
                "[PathOfTULLBUNG] Core initialization complete");

            break;
        }

        case SKSE::MessagingInterface::kPreLoadGame:
        {
            TULLBUNG::FloatingTitleMinimal::MarkTitleDirty(); // Existing SetGameReady(false) performs invalidation.
            TULLBUNG::PrismaBridge::
                SetGameReady(false);

            break;
        }

        case SKSE::MessagingInterface::kPostLoadGame:
        {
            if (TULLBUNG::DataCache::IsReady()) {

                logger::info(
                    "[PathOfTULLBUNG] PostLoadGame");

                TULLBUNG::Combat::
                    ResetTransientState();

                TULLBUNG::PlayerState::
                    RefreshAll();

                                TULLBUNG::Persistence::
                    ApplyLoadedState();

TULLBUNG::BuildState::
                    ObserveCurrentPlayerLevel();

                TULLBUNG::PrismaBridge::
                    SetGameReady(true);
            }

            break;
        }

        case SKSE::MessagingInterface::kNewGame:
        {
            if (TULLBUNG::DataCache::IsReady()) {

                logger::info(
                    "[PathOfTULLBUNG] NewGame");

                // S19: do not leak QA title freeze/build authority to a new game.
                TULLBUNG::PrismaBridge::SetGameReady(false);

                TULLBUNG::Combat::
                    ResetTransientState();

                TULLBUNG::BuildState::
                    ResetForNewGame();

                TULLBUNG::TitleRuntime::
                    EstablishBaselineFromBuildState();

                TULLBUNG::Persistence::
                    ResetInquisitorProgress();

                TULLBUNG::Persistence::
                    ResetVoidProgress();

                TULLBUNG::PlayerState::
                    RefreshAll();

                TULLBUNG::BuildState::
                    ObserveCurrentPlayerLevel();

                TULLBUNG::PrismaBridge::
                    SetGameReady(true);
            }

            break;
        }

        default:
            break;
        }
    }
}

SKSEPluginLoad(
    const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    InitializeLog();

    logger::info(
        "[PathOfTULLBUNG] Loaded successfully | Version={}",
        TULLBUNG_VERSION_LABEL);

    if (!TULLBUNG::Persistence::
            Initialize()) {

        logger::critical(
            "[PathOfTULLBUNG] Persistence initialization failed");

        return false;
    }

    auto* messaging =
        SKSE::GetMessagingInterface();

    if (!messaging) {
        return false;
    }

    if (!messaging->RegisterListener(
            OnSKSEMessage)) {

        return false;
    }

    return true;
}