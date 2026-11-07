#pragma once

namespace RE
{
    class Actor;
}

namespace TULLBUNG::Targeting
{
    struct Selection final
    {
        RE::Actor* actor{};
        float score{};
        float distance{};
        bool processListsAvailable{};
    };

    [[nodiscard]]
    Selection FindHighestMaxHealthMonsterWithLOS(
        RE::Actor* a_player,
        float a_radiusUnits) noexcept;

    [[nodiscard]]
    Selection FindHighestCurrentMagickaHostile(
        RE::Actor* a_player,
        float a_radiusUnits) noexcept;
}