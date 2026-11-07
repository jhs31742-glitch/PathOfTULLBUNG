#include "Profiles/RuntimeBoundary.h"
#include "UISettings.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

namespace TULLBUNG::UISettings
{
    namespace
    {
        Settings g_settings{};

        constexpr std::string_view
            kSettingsPath{
                "Data/SKSE/Plugins/"
                "PathOfTULLBUNG_UI.ini"
            };

        std::string_view Trim(
            std::string_view a_value) noexcept
        {
            while (!a_value.empty() &&
                   (a_value.front() == ' ' ||
                    a_value.front() == '\t' ||
                    a_value.front() == '\r' ||
                    a_value.front() == '\n')) {

                a_value.remove_prefix(1);
            }

            while (!a_value.empty() &&
                   (a_value.back() == ' ' ||
                    a_value.back() == '\t' ||
                    a_value.back() == '\r' ||
                    a_value.back() == '\n')) {

                a_value.remove_suffix(1);
            }

            return a_value;
        }

        bool ParseUInt(
            std::string_view a_text,
            std::uint32_t& a_out) noexcept
        {
            a_text = Trim(a_text);

            if (a_text.empty()) {
                return false;
            }

            std::uint32_t value{};

            const auto result =
                std::from_chars(
                    a_text.data(),
                    a_text.data() +
                        a_text.size(),
                    value);

            if (result.ec != std::errc{} ||
                result.ptr !=
                    a_text.data() +
                        a_text.size()) {

                return false;
            }

            a_out = value;
            return true;
        }

        bool ParseBool(
            std::string_view a_text,
            bool& a_out) noexcept
        {
            a_text = Trim(a_text);

            if (a_text == "1" ||
                a_text == "true") {

                a_out = true;
                return true;
            }

            if (a_text == "0" ||
                a_text == "false") {

                a_out = false;
                return true;
            }

            return false;
        }

        bool ParseFloat(
            std::string_view a_text,
            float& a_out) noexcept
        {
            a_text = Trim(a_text);

            if (a_text.empty()) {
                return false;
            }

            std::string temp{ a_text };

            char* end = nullptr;

            const float value =
                std::strtof(
                    temp.c_str(),
                    &end);

            if (!end ||
                end !=
                    temp.c_str() +
                        temp.size()) {

                return false;
            }

            a_out = value;
            return true;
        }

        void ValidateHudElement(
            HudElementSettings& a_hud) noexcept
        {
            a_hud.x =
                std::clamp(
                    a_hud.x,
                    0.0F,
                    1.0F);

            a_hud.y =
                std::clamp(
                    a_hud.y,
                    0.0F,
                    1.0F);

            a_hud.scale =
                std::clamp(
                    a_hud.scale,
                    0.5F,
                    2.0F);

            a_hud.opacity =
                std::clamp(
                    a_hud.opacity,
                    0.0F,
                    1.0F);
        }

        void Validate(
            Settings& a_settings) noexcept
        {
            a_settings.schemaVersion =
                kSchemaVersion;

            a_settings.uiBackgroundOpacity =
                std::clamp(
                    a_settings.uiBackgroundOpacity,
                    0.0F,
                    1.0F);

            if (a_settings.themeId.empty() ||
                a_settings.themeId.size() > 64) {

                a_settings.themeId =
                    "default";
            }

            if (a_settings.menuHotkey.keyCode == 0 ||
                a_settings.menuHotkey.keyCode >
                    0xFFu) {

                a_settings.menuHotkey.keyCode =
                    kDefaultMenuKeyCode;
            }

            ValidateHudElement(
                a_settings.hud.voidHud);

            ValidateHudElement(
                a_settings.hud.composureHud);

            ValidateHudElement(
                a_settings.hud.quietProcessingHud);
        }

        void ApplyKeyValue(
            Settings& a_settings,
            std::string_view a_key,
            std::string_view a_value) noexcept
        {
            a_key = Trim(a_key);
            a_value = Trim(a_value);

            if (a_key == "schemaVersion") {
                ParseUInt(
                    a_value,
                    a_settings.schemaVersion);
                return;
            }

            if (a_key == "developerMode") {
                ParseBool(
                    a_value,
                    a_settings.developerMode);
                return;
            }

            if (a_key == "themeId") {
                a_settings.themeId =
                    std::string{ a_value };
                return;
            }

            if (a_key == "uiBackgroundOpacity") {
                ParseFloat(
                    a_value,
                    a_settings.uiBackgroundOpacity);
                return;
            }

            if (a_key == "menuKey") {
                ParseUInt(
                    a_value,
                    a_settings.menuHotkey.keyCode);
                return;
            }

            if (a_key == "menuShift") {
                ParseBool(
                    a_value,
                    a_settings.menuHotkey.shift);
                return;
            }

            if (a_key == "menuCtrl") {
                ParseBool(
                    a_value,
                    a_settings.menuHotkey.ctrl);
                return;
            }

            if (a_key == "menuAlt") {
                ParseBool(
                    a_value,
                    a_settings.menuHotkey.alt);
                return;
            }

            if (a_key == "hudMaster") {
                ParseBool(
                    a_value,
                    a_settings.hud.masterVisible);
                return;
            }

            if (a_key == "hudVoidVisible") {
                ParseBool(
                    a_value,
                    a_settings.hud.voidHud.visible);
                return;
            }

            if (a_key == "hudVoidX") {
                ParseFloat(
                    a_value,
                    a_settings.hud.voidHud.x);
                return;
            }

            if (a_key == "hudVoidY") {
                ParseFloat(
                    a_value,
                    a_settings.hud.voidHud.y);
                return;
            }

            if (a_key == "hudVoidScale") {
                ParseFloat(
                    a_value,
                    a_settings.hud.voidHud.scale);
                return;
            }

            if (a_key == "hudVoidOpacity") {
                ParseFloat(
                    a_value,
                    a_settings.hud.voidHud.opacity);
                return;
            }

            if (a_key == "hudComposureVisible") {
                ParseBool(
                    a_value,
                    a_settings.hud.composureHud.visible);
                return;
            }

            if (a_key == "hudComposureX") {
                ParseFloat(
                    a_value,
                    a_settings.hud.composureHud.x);
                return;
            }

            if (a_key == "hudComposureY") {
                ParseFloat(
                    a_value,
                    a_settings.hud.composureHud.y);
                return;
            }

            if (a_key == "hudComposureScale") {
                ParseFloat(
                    a_value,
                    a_settings.hud.composureHud.scale);
                return;
            }

            if (a_key == "hudComposureOpacity") {
                ParseFloat(
                    a_value,
                    a_settings.hud.composureHud.opacity);
                return;
            }

            if (a_key == "hudQuietVisible") {
                ParseBool(
                    a_value,
                    a_settings.hud
                        .quietProcessingHud.visible);
                return;
            }

            if (a_key == "hudQuietX") {
                ParseFloat(
                    a_value,
                    a_settings.hud
                        .quietProcessingHud.x);
                return;
            }

            if (a_key == "hudQuietY") {
                ParseFloat(
                    a_value,
                    a_settings.hud
                        .quietProcessingHud.y);
                return;
            }

            if (a_key == "hudQuietScale") {
                ParseFloat(
                    a_value,
                    a_settings.hud
                        .quietProcessingHud.scale);
                return;
            }

            if (a_key == "hudQuietOpacity") {
                ParseFloat(
                    a_value,
                    a_settings.hud
                        .quietProcessingHud.opacity);
            }
        }

        void WriteHudElement(
            std::ofstream& a_out,
            std::string_view a_prefix,
            const HudElementSettings& a_hud)
        {
            a_out
                << a_prefix
                << "Visible="
                << (a_hud.visible ? 1 : 0)
                << '\n'

                << a_prefix
                << "X="
                << a_hud.x
                << '\n'

                << a_prefix
                << "Y="
                << a_hud.y
                << '\n'

                << a_prefix
                << "Scale="
                << a_hud.scale
                << '\n'

                << a_prefix
                << "Opacity="
                << a_hud.opacity
                << '\n';
        }
    }

    const Settings& Get() noexcept
    {
        return g_settings;
    }

    void ResetDefaults() noexcept
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        g_settings = Settings{};
    }

    bool Save() noexcept
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        try {
            const std::filesystem::path path{
                kSettingsPath
            };

            std::error_code ec;

            std::filesystem::create_directories(
                path.parent_path(),
                ec);

            if (ec) {
                spdlog::error(
                    "[S18UI] Settings directory "
                    "create failed | Error={}",
                    ec.message());

                return false;
            }

            std::ofstream out{
                path,
                std::ios::binary |
                std::ios::trunc
            };

            if (!out) {
                spdlog::error(
                    "[S18UI] Settings save open failed");

                return false;
            }

            const auto& s =
                g_settings;

            out
                << std::setprecision(6)

                << "schemaVersion="
                << kSchemaVersion
                << '\n'

                << "developerMode="
                << (s.developerMode ? 1 : 0)
                << '\n'

                << "themeId="
                << s.themeId
                << '\n'

                << "uiBackgroundOpacity="
                << s.uiBackgroundOpacity
                << '\n'

                << "menuKey="
                << s.menuHotkey.keyCode
                << '\n'

                << "menuShift="
                << (s.menuHotkey.shift ? 1 : 0)
                << '\n'

                << "menuCtrl="
                << (s.menuHotkey.ctrl ? 1 : 0)
                << '\n'

                << "menuAlt="
                << (s.menuHotkey.alt ? 1 : 0)
                << '\n'

                << "hudMaster="
                << (s.hud.masterVisible ? 1 : 0)
                << '\n';

            WriteHudElement(
                out,
                "hudVoid",
                s.hud.voidHud);

            WriteHudElement(
                out,
                "hudComposure",
                s.hud.composureHud);

            WriteHudElement(
                out,
                "hudQuiet",
                s.hud.quietProcessingHud);

            out.flush();

            if (!out) {
                spdlog::error(
                    "[S18UI] Settings save write failed");

                return false;
            }

            spdlog::info(
                "[S18UI] Settings saved");

            return true;
        }
        catch (const std::exception& e) {
            spdlog::error(
                "[S18UI] Settings save exception | {}",
                e.what());

            return false;
        }
        catch (...) {
            spdlog::error(
                "[S18UI] Settings save unknown exception");

            return false;
        }
    }

    bool Load() noexcept
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        ResetDefaults();

        try {
            const std::filesystem::path path{
                kSettingsPath
            };

            if (!std::filesystem::exists(
                    path)) {

                spdlog::info(
                    "[S18UI] Settings missing; "
                    "creating defaults");

                return Save();
            }

            std::ifstream in{
                path,
                std::ios::binary
            };

            if (!in) {
                spdlog::error(
                    "[S18UI] Settings load open failed");

                return false;
            }

            Settings loaded{};

            std::string line;

            while (std::getline(in, line)) {
                const auto view =
                    Trim(line);

                if (view.empty() ||
                    view.front() == '#' ||
                    view.front() == ';') {

                    continue;
                }

                const auto split =
                    view.find('=');

                if (split ==
                    std::string_view::npos) {

                    continue;
                }

                ApplyKeyValue(
                    loaded,
                    view.substr(0, split),
                    view.substr(split + 1));
            }

            if (loaded.schemaVersion !=
                kSchemaVersion) {

                spdlog::warn(
                    "[S18UI] Unsupported settings "
                    "schema | Found={} Expected={}",
                    loaded.schemaVersion,
                    kSchemaVersion);

                return false;
            }

            Validate(loaded);

            g_settings =
                std::move(loaded);

            spdlog::info(
                "[S18UI] Settings loaded | "
                "Developer={} Theme={} "
                "BackgroundOpacity={} "
                "MenuKey={} Mods=S{}C{}A{}",
                g_settings.developerMode,
                g_settings.themeId,
                g_settings.uiBackgroundOpacity,
                g_settings.menuHotkey.keyCode,
                g_settings.menuHotkey.shift,
                g_settings.menuHotkey.ctrl,
                g_settings.menuHotkey.alt);

            return true;
        }
        catch (const std::exception& e) {
            ResetDefaults();

            spdlog::error(
                "[S18UI] Settings load exception | {}",
                e.what());

            return false;
        }
        catch (...) {
            ResetDefaults();

            spdlog::error(
                "[S18UI] Settings load unknown exception");

            return false;
        }
    }

    bool Apply(
        Settings a_settings,
        bool a_persist) noexcept
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        Validate(a_settings);

        g_settings =
            std::move(a_settings);

        if (!a_persist) {
            return true;
        }

        return Save();
    }
}
namespace TULLBUNG::UISettings::TransactionCodec {
std::string_view Trim(std::string_view v) noexcept{return UISettings::Trim(v);}
bool ParseFloat(std::string_view v,float& out) noexcept{return UISettings::ParseFloat(v,out);}
void Validate(Settings& s) noexcept{UISettings::Validate(s);}
void ApplyKeyValue(Settings& s,std::string_view k,std::string_view v) noexcept{UISettings::ApplyKeyValue(s,k,v);}
}
