#pragma once

#include "RE/Skyrim.h"
#include "RE/G/GameSettingCollection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace TULLBUNG::DamageRules
{
    inline constexpr float kConditionalDRMultiplierFloor = 0.60f;
    inline constexpr std::size_t kMaxRegisteredFirewallSources = 64;

    enum class DirectChannel : std::uint8_t
    {
        kWeapon = 0,
        kSpell
    };

    enum class FirewallOrigin : std::uint8_t
    {
        kNone = 0,
        kSecondaryDamage,
        kSpecialDamage
    };

    struct DirectDamageAccumulator final
    {
        float weaponAdditivePercent{};
        float spellAdditivePercent{};

        void AddPercent(
            DirectChannel a_channel,
            float a_percent) noexcept
        {
            if (!std::isfinite(a_percent)) {
                return;
            }

            if (a_channel ==
                DirectChannel::kWeapon) {

                weaponAdditivePercent +=
                    a_percent;
            } else {
                spellAdditivePercent +=
                    a_percent;
            }
        }

        [[nodiscard]] float Resolve(
            DirectChannel a_channel,
            float a_baseDamage) const noexcept
        {
            if (!std::isfinite(a_baseDamage) ||
                a_baseDamage <= 0.0f) {

                return 0.0f;
            }

            const float additivePercent =
                a_channel ==
                    DirectChannel::kWeapon ?
                    weaponAdditivePercent :
                    spellAdditivePercent;

            const float multiplier =
                (std::max)(
                    0.0f,
                    1.0f +
                        additivePercent /
                            100.0f);

            return
                a_baseDamage *
                multiplier;
        }
    };

    struct ConditionalDRAccumulator final
    {
        float multiplier{ 1.0f };

        void AddReductionPercent(
            float a_reductionPercent) noexcept
        {
            if (!std::isfinite(
                    a_reductionPercent)) {

                return;
            }

            const float reduction =
                std::clamp(
                    a_reductionPercent,
                    0.0f,
                    100.0f) /
                100.0f;

            multiplier *=
                1.0f - reduction;
        }

        [[nodiscard]] float
            ResolveMultiplier() const noexcept
        {
            // TULLBUNG 조건부 DR 계층에만 적용되는 floor.
            // 바닐라 방어력/마법저항 계층과는 별개다.
            return
                (std::max)(
                    multiplier,
                    kConditionalDRMultiplierFloor);
        }

        [[nodiscard]] float Apply(
            float a_damage) const noexcept
        {
            if (!std::isfinite(a_damage) ||
                a_damage <= 0.0f) {

                return 0.0f;
            }

            return
                a_damage *
                ResolveMultiplier();
        }
    };

    struct PhysicalSecondaryResolution final
    {
        float rawDamage{};
        float armorActorValue{};
        float calculatedArmorRating{};
        float armorBaseFactor{};
        float armorScalingFactor{};
        float maxArmorRating{};
        float reductionFraction{};
        float resolvedDamage{};
        bool valid{};
    };

    // TULL physical secondary damage must not borrow resistance
    // values from the triggering HitData. Bash HitData does not
    // populate resistedPhysicalDamage like a normal weapon hit.
    //
    // DamageResist is the target's current Armor Rating actor value.
    // GetArmorBaseFactorSum() supplies Skyrim's hidden worn-armor
    // reduction component. Runtime GMSTs remain authoritative.
    [[nodiscard]] inline PhysicalSecondaryResolution
        ResolvePhysicalSecondary(
            RE::Actor* a_target,
            float a_rawDamage) noexcept
    {
        PhysicalSecondaryResolution result{};

        result.rawDamage =
            std::isfinite(a_rawDamage) ?
                (std::max)(
                    0.0f,
                    a_rawDamage) :
                0.0f;

        if (!a_target ||
            result.rawDamage <= 0.0f) {

            return result;
        }

        auto* owner =
            a_target->AsActorValueOwner();

        auto* settings =
            RE::GameSettingCollection::
                GetSingleton();

        if (!owner ||
            !settings) {

            return result;
        }

        auto* scalingSetting =
            settings->GetSetting(
                "fArmorScalingFactor");

        auto* capSetting =
            settings->GetSetting(
                "fMaxArmorRating");

        if (!scalingSetting ||
            !capSetting) {

            return result;
        }

        result.armorActorValue =
            owner->GetActorValue(
                RE::ActorValue::
                    kDamageResist);

        result.calculatedArmorRating =
            a_target->CalcArmorRating();

        result.armorBaseFactor =
            a_target->
                GetArmorBaseFactorSum();

        result.armorScalingFactor =
            scalingSetting->GetFloat();

        result.maxArmorRating =
            capSetting->GetFloat();

        if (!std::isfinite(
                result.armorActorValue) ||
            !std::isfinite(
                result.calculatedArmorRating) ||
            !std::isfinite(
                result.armorBaseFactor) ||
            !std::isfinite(
                result.armorScalingFactor) ||
            !std::isfinite(
                result.maxArmorRating)) {

            return result;
        }

        const float visibleReduction =
            (std::max)(
                0.0f,
                result.armorActorValue) *
            (std::max)(
                0.0f,
                result.armorScalingFactor) /
            100.0f;

        const float hiddenReduction =
            (std::max)(
                0.0f,
                result.armorBaseFactor);

        const float capFraction =
            std::clamp(
                result.maxArmorRating /
                    100.0f,
                0.0f,
                1.0f);

        result.reductionFraction =
            std::clamp(
                visibleReduction +
                    hiddenReduction,
                0.0f,
                capFraction);

        result.resolvedDamage =
            result.rawDamage *
            (1.0f -
                result.reductionFraction);

        result.valid =
            std::isfinite(
                result.resolvedDamage);

        if (!result.valid) {
            result.resolvedDamage =
                0.0f;
        }

        return result;
    }

    namespace detail
    {
        inline thread_local std::uint32_t
            g_firewallDepth{};

        inline thread_local FirewallOrigin
            g_firewallOrigin{
                FirewallOrigin::kNone
            };

        inline std::array<
            RE::FormID,
            kMaxRegisteredFirewallSources>
            g_registeredFirewallSources{};

        inline std::size_t
            g_registeredFirewallSourceCount{};

        [[nodiscard]] inline bool NearlyEqual(
            float a_left,
            float a_right,
            float a_epsilon = 0.001f) noexcept
        {
            return
                std::fabs(
                    a_left -
                    a_right) <=
                a_epsilon;
        }
    }

    class ScopedTriggerFirewall final
    {
    public:
        explicit ScopedTriggerFirewall(
            FirewallOrigin a_origin) noexcept :
            previousOrigin_(
                detail::g_firewallOrigin)
        {
            ++detail::g_firewallDepth;

            if (a_origin !=
                FirewallOrigin::kNone) {

                detail::g_firewallOrigin =
                    a_origin;
            }
        }

        ~ScopedTriggerFirewall()
        {
            if (detail::g_firewallDepth > 0) {
                --detail::g_firewallDepth;
            }

            if (detail::g_firewallDepth == 0) {
                detail::g_firewallOrigin =
                    FirewallOrigin::kNone;
            } else {
                detail::g_firewallOrigin =
                    previousOrigin_;
            }
        }

        ScopedTriggerFirewall(
            const ScopedTriggerFirewall&) =
            delete;

        ScopedTriggerFirewall& operator=(
            const ScopedTriggerFirewall&) =
            delete;

    private:
        FirewallOrigin previousOrigin_{
            FirewallOrigin::kNone
        };
    };

    [[nodiscard]] inline bool
        IsTriggerSuppressed() noexcept
    {
        return
            detail::g_firewallDepth != 0;
    }

    [[nodiscard]] inline FirewallOrigin
        GetFirewallOrigin() noexcept
    {
        return
            detail::g_firewallOrigin;
    }

    [[nodiscard]] inline std::string_view
        FirewallOriginName(
            FirewallOrigin a_origin) noexcept
    {
        switch (a_origin) {
        case FirewallOrigin::
                kSecondaryDamage:
            return "SECONDARY";

        case FirewallOrigin::
                kSpecialDamage:
            return "SPECIAL";

        case FirewallOrigin::kNone:
        default:
            return "NONE";
        }
    }

    // 이후 ESP/MGEF 기반 TULL 2차/특수 피해 source는
    // DataLoaded에서 FormID를 1회 등록한다.
    // 등록 source의 TESHitEvent는 시간창 dedupe 없이 차단한다.
    [[nodiscard]] inline bool
        RegisterFirewallSource(
            RE::FormID a_sourceFormID) noexcept
    {
        if (!a_sourceFormID) {
            return false;
        }

        for (std::size_t i = 0;
             i < detail::
                     g_registeredFirewallSourceCount;
             ++i) {

            if (detail::
                    g_registeredFirewallSources[i] ==
                a_sourceFormID) {

                return true;
            }
        }

        if (detail::
                g_registeredFirewallSourceCount >=
            kMaxRegisteredFirewallSources) {

            return false;
        }

        detail::
            g_registeredFirewallSources[
                detail::
                    g_registeredFirewallSourceCount++] =
            a_sourceFormID;

        return true;
    }

    [[nodiscard]] inline bool
        IsRegisteredFirewallSource(
            RE::FormID a_sourceFormID) noexcept
    {
        if (!a_sourceFormID) {
            return false;
        }

        for (std::size_t i = 0;
             i < detail::
                     g_registeredFirewallSourceCount;
             ++i) {

            if (detail::
                    g_registeredFirewallSources[i] ==
                a_sourceFormID) {

                return true;
            }
        }

        return false;
    }

    inline void ResetTransientState() noexcept
    {
        detail::g_firewallDepth = 0;
        detail::g_firewallOrigin =
            FirewallOrigin::kNone;
    }

    // 호출자가 해당 효과의 저항/상한/스케일 계산까지 끝낸
    // 최종 체력 피해량을 전달한다. 이 함수는 재귀 차단과
    // 실제 체력 피해 적용만 담당한다.
    [[nodiscard]] inline bool
        ApplyResolvedHealthDamage(
            RE::Actor* a_source,
            RE::Actor* a_target,
            float a_resolvedDamage,
            FirewallOrigin a_origin =
                FirewallOrigin::
                    kSecondaryDamage)
    {
        if (!a_target ||
            !std::isfinite(
                a_resolvedDamage) ||
            a_resolvedDamage <= 0.0f ||
            IsTriggerSuppressed()) {

            return false;
        }

        ScopedTriggerFirewall firewall{
            a_origin
        };

        // 이미 TULL 쪽에서 최종량을 계산했으므로
        // Skyrim 난이도 배율을 다시 적용하지 않는다.
        (void)a_source;

        auto* avOwner =
            a_target->AsActorValueOwner();

        if (!avOwner) {
            return false;
        }

        // Exact TULL secondary/special health application.
        //
        // DoDamage() is rejected because it applies difficulty scaling.
        // RestoreActorValue() is rejected because runtime validation
        // proved that it restores Health regardless of the tested sign.
        //
        // ModActorValue with a negative delta directly lowers the
        // current Health actor value by the already-resolved amount.
        // Local vendored CommonLib uses the 3-argument
        // ModActorValue(modifier, actorValue, value) ABI.
        //
        // kDamage modifies the damage component rather than
        // permanent/maximum Health.
        avOwner->ModActorValue(
            RE::ACTOR_VALUE_MODIFIERS::kDamage,
            RE::ActorValue::kHealth,
            -a_resolvedDamage);

        return true;
    }

    [[nodiscard]] inline bool
        RunSelfTest() noexcept
    {
        bool pass = true;

        {
            DirectDamageAccumulator direct{};

            direct.AddPercent(
                DirectChannel::kWeapon,
                20.0f);

            direct.AddPercent(
                DirectChannel::kWeapon,
                15.0f);

            direct.AddPercent(
                DirectChannel::kSpell,
                30.0f);

            pass =
                pass &&
                detail::NearlyEqual(
                    direct.Resolve(
                        DirectChannel::kWeapon,
                        100.0f),
                    135.0f) &&
                detail::NearlyEqual(
                    direct.Resolve(
                        DirectChannel::kSpell,
                        100.0f),
                    130.0f);
        }

        {
            ConditionalDRAccumulator dr{};

            dr.AddReductionPercent(
                20.0f);

            dr.AddReductionPercent(
                20.0f);

            pass =
                pass &&
                detail::NearlyEqual(
                    dr.ResolveMultiplier(),
                    0.64f);

            dr.AddReductionPercent(
                20.0f);

            pass =
                pass &&
                detail::NearlyEqual(
                    dr.ResolveMultiplier(),
                    kConditionalDRMultiplierFloor) &&
                detail::NearlyEqual(
                    dr.Apply(
                        100.0f),
                    60.0f);
        }

        {
            ResetTransientState();

            pass =
                pass &&
                !IsTriggerSuppressed();

            {
                ScopedTriggerFirewall
                    first{
                        FirewallOrigin::
                            kSecondaryDamage
                    };

                pass =
                    pass &&
                    IsTriggerSuppressed() &&
                    GetFirewallOrigin() ==
                        FirewallOrigin::
                            kSecondaryDamage;

                {
                    ScopedTriggerFirewall
                        second{
                            FirewallOrigin::
                                kSpecialDamage
                        };

                    pass =
                        pass &&
                        IsTriggerSuppressed() &&
                        GetFirewallOrigin() ==
                            FirewallOrigin::
                                kSpecialDamage;
                }

                pass =
                    pass &&
                    IsTriggerSuppressed() &&
                    GetFirewallOrigin() ==
                        FirewallOrigin::
                            kSecondaryDamage;
            }

            pass =
                pass &&
                !IsTriggerSuppressed() &&
                GetFirewallOrigin() ==
                    FirewallOrigin::kNone;
        }

        return pass;
    }
}
