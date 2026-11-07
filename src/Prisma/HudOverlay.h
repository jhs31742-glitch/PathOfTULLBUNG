#pragma once

#include "UISettings.h"

#include <cstdint>

namespace TULLBUNG::HudOverlay
{
    struct RuntimeVisibility
    {
        bool gameReady{ false };
        bool gameplayContext{ false };
        bool skyrimUIVisible{ true };

        [[nodiscard]]
        bool AllowsHud() const noexcept
        {
            return
                gameReady &&
                gameplayContext &&
                skyrimUIVisible;
        }
    };

    struct GameplayState
    {
        bool voidOwned{ false };
        std::uint8_t voidCount{ 0 };

        bool composureOwned{ false };
        bool composureReady{ false };

        bool quietOwned{ false };
        bool quietReady{ false };
    };

    struct Snapshot
    {
        RuntimeVisibility runtime{};
        UISettings::HudSettings settings{};
        GameplayState gameplay{};
    };

    void Reset() noexcept;

    bool InstallRuntimeVisibilityGate() noexcept;
    void RefreshRuntimeVisibilityFromGame() noexcept;

    void SetGameReady(bool a_ready) noexcept;
    void SetGameplayContext(bool a_gameplay) noexcept;

    void SetRuntimeVisibility(
        bool a_gameReady,
        bool a_gameplayContext,
        bool a_skyrimUIVisible) noexcept;

    void SetHudSettings(
        const UISettings::HudSettings& a_settings) noexcept;

    void SetVoidState(
        bool a_owned,
        std::uint8_t a_count) noexcept;

    void SetComposureState(
        bool a_owned,
        bool a_ready) noexcept;

    void SetQuietState(
        bool a_owned,
        bool a_ready) noexcept;

    [[nodiscard]]
    Snapshot GetSnapshot() noexcept;

    [[nodiscard]]
    bool ShouldRenderVoid(
        const Snapshot& a_snapshot) noexcept;

    [[nodiscard]]
    bool ShouldRenderComposure(
        const Snapshot& a_snapshot) noexcept;

    [[nodiscard]]
    bool ShouldRenderQuiet(
        const Snapshot& a_snapshot) noexcept;
}