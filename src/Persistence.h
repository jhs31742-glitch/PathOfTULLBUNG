#pragma once

#include <cstdint>

namespace TULLBUNG::Persistence
{
    struct InquisitorProgress final
    {
        std::uint32_t monsterKills{};
    };

    struct VoidProgress final
    {
        std::uint8_t voidCount{};
    };

    [[nodiscard]]
    bool Initialize() noexcept;

    // Session-only diagnostic/gate. Never means that returning cancels ESS saving.
    [[nodiscard]] bool IsSaveSafetyHeld() noexcept;

    // Applies records read by LoadCallback only after the game's
    // transient reset / PlayerState refresh boundary.
    void ApplyLoadedState() noexcept;

    [[nodiscard]]
    const InquisitorProgress&
        GetInquisitorProgress() noexcept;

    void SetInquisitorMonsterKills(
        std::uint32_t a_kills) noexcept;

    void ResetInquisitorProgress() noexcept;

    [[nodiscard]]
    const VoidProgress&
        GetVoidProgress() noexcept;

    void SetVoidCount(
        std::uint8_t a_count) noexcept;

    void ResetVoidProgress() noexcept;
}