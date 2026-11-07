#pragma once

#include "TitleCatalog.generated.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TULLBUNG::TitleSystem
{
    inline constexpr std::size_t kSlotCount = 6;
    inline constexpr std::uint16_t kCustomTitleFirstID = 1000;
    inline constexpr std::size_t kCustomTitleCount = 20;
    inline constexpr std::size_t kCustomTextMaxBytes = 1024;

    struct CustomTitleRecord
    {
        bool present{ false };
        bool favorite{ false };
        std::string before;
        std::string after;
    };

    using CustomTitleRecords = std::array<CustomTitleRecord, kCustomTitleCount>;

    [[nodiscard]] constexpr bool IsCustomTitleID(std::uint16_t a_id) noexcept
    {
        return a_id >= kCustomTitleFirstID &&
               a_id < kCustomTitleFirstID + kCustomTitleCount;
    }

    struct SlotState
    {
        bool active{ false };
        Role role{ Role::kStandalone };
        std::uint16_t selectedTitleID{ 0 };
        std::uint8_t selectedSegmentIndex{ 0 };
        bool autoEnabled{ true };
    };

    struct PersistedState
    {
        std::string nickname;
        std::array<SlotState, kSlotCount> slots{};
        std::vector<std::uint16_t> favoriteTitleIDs;
    };

    struct RefreshResult
    {
        std::vector<std::uint16_t> acquired;
        std::vector<std::uint16_t> lost;
        bool anyStateChanged{ false };
    };

    class Runtime
    {
    public:
        Runtime();

        void ResetDefaults();

        // Load/New Game baseline: derive acquired titles from finalized BuildState
        // without generating a recent-acquisition batch or AUTO reassignment.
        void EstablishBaseline(const ClassLevels& a_levels);

        // Runtime mutation path. False->true acquisitions form one new batch.
        RefreshResult RefreshFromClassLevels(const ClassLevels& a_levels);

        [[nodiscard]] bool IsAcquired(std::uint16_t a_titleID) const noexcept;
        [[nodiscard]] bool IsFavorite(std::uint16_t a_titleID) const noexcept;
        [[nodiscard]] std::vector<std::uint16_t> AcquiredTitleIDs() const;
        [[nodiscard]] std::vector<std::uint16_t> FavoriteTitleIDs() const;
        [[nodiscard]] std::span<const std::uint16_t> LastAcquisitionBatch() const noexcept;

        [[nodiscard]] const std::array<SlotState, kSlotCount>& Slots() const noexcept;
        [[nodiscard]] const SlotState* GetSlot(std::size_t a_slot1Based) const noexcept;

        [[nodiscard]] const CustomTitleRecord* GetCustomTitle(std::uint16_t a_id) const noexcept;
        bool UpsertCustomTitle(std::uint16_t a_id, std::string a_before, std::string a_after);
        bool DeleteCustomTitle(std::uint16_t a_id);
        [[nodiscard]] CustomTitleRecords MakeCustomTitleRecords() const;
        void ApplyCustomTitleRecords(const CustomTitleRecords& a_records);

        bool ToggleFavorite(std::uint16_t a_titleID);
        bool SelectTitle(
            std::size_t a_slot1Based,
            std::uint16_t a_titleID,
            std::uint8_t a_segmentIndex);
        bool SetAuto(std::size_t a_slot1Based, bool a_enabled);
        bool RemoveSlot(std::size_t a_slot1Based);

        // Returns created 1-based fixed slot number, or nullopt if all six are active.
        std::optional<std::size_t> ManualAdd(Role a_role);
        bool EnsurePresetSlot(std::size_t a_slot1Based);
        bool ApplyPresetPresenceMask(std::uint8_t a_activeMask);

        void SetNickname(std::string a_nickname);
        [[nodiscard]] const std::string& Nickname() const noexcept;
        [[nodiscard]] std::string EffectiveName(std::string_view a_characterName) const;
        [[nodiscard]] std::string ResolveSlotText(
            std::size_t a_slot1Based,
            std::string_view a_characterName) const;

        [[nodiscard]] PersistedState MakePersistedState() const;

        // Apply after EstablishBaseline(). Semantically invalid/retired/unowned
        // selections are cleared, invalid favorites are dropped, and recent batch
        // remains empty. Grid geometry is deliberately outside this state.
        void ApplyPersistedState(const PersistedState& a_state);

        [[nodiscard]] static Role DefaultRole(std::size_t a_slot1Based) noexcept;
        [[nodiscard]] static bool DefaultActive(std::size_t a_slot1Based) noexcept;
        [[nodiscard]] static bool IsValidRole(Role a_role) noexcept;

    private:
        [[nodiscard]] static SlotState FreshSlot(
            std::size_t a_slot1Based,
            bool a_active,
            Role a_role) noexcept;

        [[nodiscard]] bool ValidateSelection(
            const SlotState& a_slot,
            std::uint16_t a_titleID,
            std::uint8_t a_segmentIndex) const noexcept;

        void ClearSelection(SlotState& a_slot) noexcept;
        bool ApplyAutoCandidate(SlotState& a_slot);
        void ApplyAutoToAllSlots();
        void ClearLostSelectionsAndFavorites(std::span<const std::uint16_t> a_lost);

        CustomTitleRecords customTitles_{};
        std::bitset<kMaxTitleNumericID + 1> acquired_{};
        std::bitset<kMaxTitleNumericID + 1> favorites_{};
        std::array<SlotState, kSlotCount> slots_{};
        std::vector<std::uint16_t> lastAcquisitionBatch_;
        std::string nickname_;
    };
}
