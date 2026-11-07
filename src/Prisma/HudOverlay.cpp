#include "PCH.h"

#include "Prisma/HudOverlay.h"
#include "Prisma/PrismaBridge.h"

#include "RE/M/MenuModeChangeEvent.h"
#include "RE/M/MenuOpenCloseEvent.h"
#include "RE/U/UI.h"

#include <algorithm>

namespace TULLBUNG::HudOverlay
{
    namespace
    {
        Snapshot g_snapshot{};
        bool g_visibilityGateInstalled{};

        [[nodiscard]]
        bool BaseVisible(
            const Snapshot& a_snapshot,
            const UISettings::HudElementSettings& a_element,
            bool a_owned) noexcept
        {
            return
                a_snapshot.runtime.AllowsHud() &&
                a_snapshot.settings.masterVisible &&
                a_element.visible &&
                a_owned;
        }

        class VisibilitySink final :
            public RE::BSTEventSink<
                RE::MenuOpenCloseEvent>,
            public RE::BSTEventSink<
                RE::MenuModeChangeEvent>
        {
        public:
            static VisibilitySink*
                GetSingleton() noexcept
            {
                static VisibilitySink singleton;
                return std::addressof(singleton);
            }

            RE::BSEventNotifyControl
                ProcessEvent(
                    const RE::MenuOpenCloseEvent*,
                    RE::BSTEventSource<
                        RE::MenuOpenCloseEvent>*)
                    override
            {
                RefreshRuntimeVisibilityFromGame();
                TULLBUNG::PrismaBridge::
                    NotifyHudStateChanged();

                return
                    RE::BSEventNotifyControl::
                        kContinue;
            }

            RE::BSEventNotifyControl
                ProcessEvent(
                    const RE::MenuModeChangeEvent*,
                    RE::BSTEventSource<
                        RE::MenuModeChangeEvent>*)
                    override
            {
                RefreshRuntimeVisibilityFromGame();
                TULLBUNG::PrismaBridge::
                    NotifyHudStateChanged();

                return
                    RE::BSEventNotifyControl::
                        kContinue;
            }
        };
    }

    void Reset() noexcept
    {
        g_snapshot =
            Snapshot{};
    }

    bool InstallRuntimeVisibilityGate() noexcept
    {
        auto* ui =
            RE::UI::GetSingleton();

        if (!ui) {
            g_snapshot.runtime.skyrimUIVisible =
                false;

            return false;
        }

        if (!g_visibilityGateInstalled) {
            auto* sink =
                VisibilitySink::GetSingleton();

            ui->AddEventSink<
                RE::MenuOpenCloseEvent>(
                    sink);

            ui->AddEventSink<
                RE::MenuModeChangeEvent>(
                    sink);

            g_visibilityGateInstalled =
                true;
        }

        RefreshRuntimeVisibilityFromGame();
        return true;
    }

    void RefreshRuntimeVisibilityFromGame() noexcept
    {
        auto* ui =
            RE::UI::GetSingleton();

        bool gameplayContext =
            ui != nullptr;

        if (gameplayContext &&
            ui->IsApplicationMenuOpen()) {

            gameplayContext =
                false;
        }

        if (gameplayContext) {
            for (const auto& menu :
                 ui->menuStack) {

                if (menu &&
                    menu->OnStack() &&
                    !menu->AlwaysOpen()) {

                    gameplayContext =
                        false;
                    break;
                }
            }
        }

        g_snapshot.runtime.gameplayContext =
            gameplayContext;

        g_snapshot.runtime.skyrimUIVisible =
            ui &&
            ui->IsShowingMenus();
    }

    void SetGameReady(
        bool a_ready) noexcept
    {
        g_snapshot.runtime.gameReady =
            a_ready;
    }

    void SetGameplayContext(
        bool a_gameplay) noexcept
    {
        g_snapshot.runtime.gameplayContext =
            a_gameplay;
    }

    void SetRuntimeVisibility(
        bool a_gameReady,
        bool a_gameplayContext,
        bool a_skyrimUIVisible) noexcept
    {
        g_snapshot.runtime.gameReady =
            a_gameReady;

        g_snapshot.runtime.gameplayContext =
            a_gameplayContext;

        g_snapshot.runtime.skyrimUIVisible =
            a_skyrimUIVisible;
    }

    void SetHudSettings(
        const UISettings::HudSettings& a_settings) noexcept
    {
        g_snapshot.settings =
            a_settings;
    }

    void SetVoidState(
        bool a_owned,
        std::uint8_t a_count) noexcept
    {
        g_snapshot.gameplay.voidOwned =
            a_owned;

        g_snapshot.gameplay.voidCount =
            std::min<std::uint8_t>(
                a_count,
                4);
    }

    void SetComposureState(
        bool a_owned,
        bool a_ready) noexcept
    {
        g_snapshot.gameplay.composureOwned =
            a_owned;

        g_snapshot.gameplay.composureReady =
            a_ready;
    }

    void SetQuietState(
        bool a_owned,
        bool a_ready) noexcept
    {
        g_snapshot.gameplay.quietOwned =
            a_owned;

        g_snapshot.gameplay.quietReady =
            a_ready;
    }

    Snapshot GetSnapshot() noexcept
    {
        return g_snapshot;
    }

    bool ShouldRenderVoid(
        const Snapshot& a_snapshot) noexcept
    {
        return BaseVisible(
            a_snapshot,
            a_snapshot.settings.voidHud,
            a_snapshot.gameplay.voidOwned);
    }

    bool ShouldRenderComposure(
        const Snapshot& a_snapshot) noexcept
    {
        return
            BaseVisible(
                a_snapshot,
                a_snapshot.settings.composureHud,
                a_snapshot.gameplay.composureOwned) &&
            a_snapshot.gameplay.composureReady;
    }

    bool ShouldRenderQuiet(
        const Snapshot& a_snapshot) noexcept
    {
        return
            BaseVisible(
                a_snapshot,
                a_snapshot.settings.quietProcessingHud,
                a_snapshot.gameplay.quietOwned) &&
            a_snapshot.gameplay.quietReady;
    }
}