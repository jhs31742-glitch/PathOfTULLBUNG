#pragma once

#include <cstdint>
#include <string>

namespace TULLBUNG::UISettings
{
    inline constexpr std::uint32_t kSchemaVersion = 2;
    inline constexpr std::uint32_t kDefaultMenuKeyCode = 0x23;

    struct HotkeyBinding
    {
        std::uint32_t keyCode{ kDefaultMenuKeyCode };
        bool shift{ false };
        bool ctrl{ false };
        bool alt{ false };
    };

    struct HudElementSettings
    {
        bool visible{ true };

        float x{ 0.06F };
        float y{ 0.50F };

        float scale{ 1.0F };
        float opacity{ 1.0F };
    };

    struct HudSettings
    {
        bool masterVisible{ true };

        HudElementSettings voidHud{
            true,
            0.06F,
            0.76F,
            1.0F,
            1.0F
        };

        HudElementSettings composureHud{
            true,
            0.06F,
            0.63F,
            1.0F,
            1.0F
        };

        HudElementSettings quietProcessingHud{
            true,
            0.06F,
            0.50F,
            1.0F,
            1.0F
        };
    };

    struct Settings
    {
        std::uint32_t schemaVersion{ kSchemaVersion };

        bool developerMode{ false };

        // "default" is the existing parchment /
        // pale-yellow production visual language.
        std::string themeId{ "default" };

        // Background/panel opacity only.
        // Text, node icons and controls remain opaque.
        float uiBackgroundOpacity{ 1.0F };

        HotkeyBinding menuHotkey{};

        HudSettings hud{};
    };

    [[nodiscard]]
    const Settings& Get() noexcept;

    void ResetDefaults() noexcept;

    [[nodiscard]]
    bool Load() noexcept;

    [[nodiscard]]
    bool Save() noexcept;

    [[nodiscard]]
    bool Apply(
        Settings a_settings,
        bool a_persist) noexcept;
}
// Pure owner codec for profile transactions; ordinary Save/Load contract unchanged.
#include <string_view>
namespace TULLBUNG::UISettings::TransactionCodec {std::string_view Trim(std::string_view) noexcept;bool ParseFloat(std::string_view,float&) noexcept;void Validate(Settings&) noexcept;void ApplyKeyValue(Settings&,std::string_view,std::string_view) noexcept;}
