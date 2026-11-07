#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace TULLBUNG::TitleSystem
{
    inline constexpr std::size_t kClassCount = 12;
    inline constexpr std::size_t kTitleCount = 296;
    inline constexpr std::size_t kSegmentCount = 305;
    inline constexpr std::size_t kConditionCount = 675;
    inline constexpr std::uint16_t kMaxTitleNumericID = 303;

    enum class Role : std::uint8_t
    {
        kPrefix = 1,
        kSuffix = 2,
        kStandalone = 4,
        kNameTemplate = 8
    };

    enum class TitleStyle : std::uint8_t
    {
        kDefault = 0,
        kBold = 1,
        kAccent = 2,
        kAccentBold = 3
    };

    enum class ConditionKind : std::uint8_t
    {
        kClassMinLevel = 0,
        kClassCountThreshold = 1
    };

    struct Condition
    {
        ConditionKind kind{ ConditionKind::kClassMinLevel };
        std::uint8_t classID{ 0 };
        std::uint8_t minLevel{ 0 };
        std::uint16_t eligibleClassMask{ 0 };
        std::uint8_t minCount{ 0 };
        std::uint8_t minLevelEach{ 0 };
    };

    struct SegmentDef
    {
        std::uint8_t index{ 0 };
        std::uint8_t roleMask{ 0 };
        bool containsNameToken{ false };
        std::string_view text{};
    };

    struct TitleDef
    {
        std::uint16_t numericID{ 0 };
        TitleStyle style{ TitleStyle::kDefault };
        std::uint8_t roleUnionMask{ 0 };
        std::uint16_t conditionOffset{ 0 };
        std::uint8_t conditionCount{ 0 };
        std::uint16_t segmentOffset{ 0 };
        std::uint8_t segmentCount{ 0 };
        std::string_view text{};
        std::string_view hoverCondition{};
    };

    using ClassLevels = std::array<std::uint8_t, kClassCount>;

    extern const std::array<Condition, kConditionCount> kConditions;
    extern const std::array<SegmentDef, kSegmentCount> kSegments;
    extern const std::array<TitleDef, kTitleCount> kTitles;

    [[nodiscard]] const TitleDef* FindTitle(std::uint16_t a_numericID) noexcept;
    [[nodiscard]] std::span<const Condition> ConditionsOf(const TitleDef& a_title) noexcept;
    [[nodiscard]] std::span<const SegmentDef> SegmentsOf(const TitleDef& a_title) noexcept;
    [[nodiscard]] const SegmentDef* FindSegment(const TitleDef& a_title, std::uint8_t a_segmentIndex) noexcept;
    [[nodiscard]] bool SegmentSupportsRole(const SegmentDef& a_segment, Role a_role) noexcept;
    [[nodiscard]] const SegmentDef* LowestCompatibleSegment(const TitleDef& a_title, Role a_role) noexcept;
    [[nodiscard]] bool EvaluateTitle(const TitleDef& a_title, const ClassLevels& a_levels) noexcept;
}
