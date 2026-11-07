#include "Profiles/RuntimeBoundary.h"
#include "TitleSystem.h"

#include <algorithm>
#include <utility>

namespace TULLBUNG::TitleSystem
{
    namespace
    {
        constexpr std::array<Role, kSlotCount> kDefaultRoles{
            Role::kPrefix,
            Role::kNameTemplate,
            Role::kSuffix,
            Role::kStandalone,
            Role::kStandalone,
            Role::kStandalone
        };

        constexpr std::array<bool, kSlotCount> kDefaultActive{
            true,
            true,
            true,
            true,
            false,
            false
        };

        std::string ReplaceNameToken(
            std::string_view a_text,
            std::string_view a_name)
        {
            constexpr std::string_view token{ "<NAME>" };

            std::string result;
            result.reserve(a_text.size() + a_name.size());

            std::size_t cursor = 0;
            while (cursor < a_text.size()) {
                const auto hit = a_text.find(token, cursor);
                if (hit == std::string_view::npos) {
                    result.append(a_text.substr(cursor));
                    break;
                }

                result.append(a_text.substr(cursor, hit - cursor));
                result.append(a_name);
                cursor = hit + token.size();
            }

            return result;
        }
    }

    Runtime::Runtime()
    {
        ResetDefaults();
    }

    void Runtime::ResetDefaults()
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        acquired_.reset();
        favorites_.reset();
        customTitles_ = {};
        lastAcquisitionBatch_.clear();
        nickname_.clear();

        for (std::size_t i = 0; i < kSlotCount; ++i) {
            slots_[i] = FreshSlot(
                i + 1,
                kDefaultActive[i],
                kDefaultRoles[i]);
        }
    }

    void Runtime::EstablishBaseline(const ClassLevels& a_levels)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        acquired_.reset();
        favorites_.reset();
        lastAcquisitionBatch_.clear();

        for (const auto& title : kTitles) {
            if (EvaluateTitle(title, a_levels)) {
                acquired_.set(title.numericID);
            }
        }

        // User state is applied separately after this baseline during load.
        for (auto& slot : slots_) {
            if (slot.selectedTitleID != 0 &&
                !IsAcquired(slot.selectedTitleID)) {
                ClearSelection(slot);
            }
        }
    }

    RefreshResult Runtime::RefreshFromClassLevels(const ClassLevels& a_levels)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        std::bitset<kMaxTitleNumericID + 1> next;
        RefreshResult result;

        for (const auto& title : kTitles) {
            if (EvaluateTitle(title, a_levels)) {
                next.set(title.numericID);
            }
        }

        for (const auto& title : kTitles) {
            const auto id = title.numericID;
            const bool was = acquired_.test(id);
            const bool now = next.test(id);

            if (!was && now) {
                result.acquired.push_back(id);
            }
            else if (was && !now) {
                result.lost.push_back(id);
            }
        }

        acquired_ = next;

        if (!result.lost.empty()) {
            ClearLostSelectionsAndFavorites(result.lost);
            result.anyStateChanged = true;
        }

        if (!result.acquired.empty()) {
            lastAcquisitionBatch_ = result.acquired;
            ApplyAutoToAllSlots();
            result.anyStateChanged = true;
        }

        return result;
    }

    bool Runtime::IsAcquired(std::uint16_t a_titleID) const noexcept
    {
        if (IsCustomTitleID(a_titleID)) {
            return GetCustomTitle(a_titleID) != nullptr;
        }
        return a_titleID <= kMaxTitleNumericID &&
               FindTitle(a_titleID) != nullptr &&
               acquired_.test(a_titleID);
    }

    bool Runtime::IsFavorite(std::uint16_t a_titleID) const noexcept
    {
        if (const auto* custom = GetCustomTitle(a_titleID)) {
            return custom->favorite;
        }
        return a_titleID <= kMaxTitleNumericID &&
               favorites_.test(a_titleID);
    }

    std::vector<std::uint16_t> Runtime::AcquiredTitleIDs() const
    {
        std::vector<std::uint16_t> result;
        result.reserve(kTitleCount + kCustomTitleCount);
        for (const auto& title : kTitles) {
            if (acquired_.test(title.numericID)) {
                result.push_back(title.numericID);
            }
        }
        for (std::size_t i = 0; i < kCustomTitleCount; ++i) {
            if (customTitles_[i].present) {
                result.push_back(static_cast<std::uint16_t>(kCustomTitleFirstID + i));
            }
        }
        return result;
    }

    std::vector<std::uint16_t> Runtime::FavoriteTitleIDs() const
    {
        std::vector<std::uint16_t> result;
        for (const auto& title : kTitles) {
            if (favorites_.test(title.numericID)) {
                result.push_back(title.numericID);
            }
        }
        for (std::size_t i = 0; i < kCustomTitleCount; ++i) {
            if (customTitles_[i].present && customTitles_[i].favorite) {
                result.push_back(static_cast<std::uint16_t>(kCustomTitleFirstID + i));
            }
        }
        return result;
    }

    std::span<const std::uint16_t> Runtime::LastAcquisitionBatch() const noexcept
    {
        return lastAcquisitionBatch_;
    }

    const std::array<SlotState, kSlotCount>& Runtime::Slots() const noexcept
    {
        return slots_;
    }

    const SlotState* Runtime::GetSlot(std::size_t a_slot1Based) const noexcept
    {
        if (a_slot1Based == 0 || a_slot1Based > kSlotCount) {
            return nullptr;
        }
        return &slots_[a_slot1Based - 1];
    }

    const CustomTitleRecord* Runtime::GetCustomTitle(std::uint16_t a_id) const noexcept
    {
        if (!IsCustomTitleID(a_id)) {
            return nullptr;
        }
        const auto& title = customTitles_[a_id - kCustomTitleFirstID];
        return title.present ? &title : nullptr;
    }

    bool Runtime::UpsertCustomTitle(
        std::uint16_t a_id, std::string a_before, std::string a_after)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if (!IsCustomTitleID(a_id) ||
            a_before.size() > kCustomTextMaxBytes ||
            a_after.size() > kCustomTextMaxBytes) {
            return false;
        }
        const auto visible = [](std::string_view a_text) {
            return a_text.find_first_not_of(" \t\r\n") != std::string_view::npos;
        };
        const bool standalone = a_id < kCustomTitleFirstID + 10;
        if ((standalone && (!visible(a_before) || !a_after.empty())) ||
            (!standalone && !visible(a_before) && !visible(a_after))) {
            return false;
        }
        auto& title = customTitles_[a_id - kCustomTitleFirstID];
        const bool keepFavorite = title.present && title.favorite;
        title.present = true;
        title.favorite = keepFavorite;
        title.before = std::move(a_before);
        title.after = std::move(a_after);
        return true;
    }

    bool Runtime::DeleteCustomTitle(std::uint16_t a_id)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if (!GetCustomTitle(a_id)) {
            return false;
        }
        customTitles_[a_id - kCustomTitleFirstID] = {};
        for (auto& slot : slots_) {
            if (slot.selectedTitleID == a_id) {
                ClearSelection(slot);
            }
        }
        return true;
    }

    CustomTitleRecords Runtime::MakeCustomTitleRecords() const
    {
        return customTitles_;
    }

    void Runtime::ApplyCustomTitleRecords(const CustomTitleRecords& a_records)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        customTitles_ = {};
        for (std::size_t i = 0; i < kCustomTitleCount; ++i) {
            const auto& source = a_records[i];
            if (!source.present) {
                continue;
            }
            const auto id = static_cast<std::uint16_t>(kCustomTitleFirstID + i);
            if (UpsertCustomTitle(id, source.before, source.after)) {
                customTitles_[i].favorite = source.favorite;
            }
        }
        for (auto& slot : slots_) {
            if (IsCustomTitleID(slot.selectedTitleID) &&
                !GetCustomTitle(slot.selectedTitleID)) {
                ClearSelection(slot);
            }
        }
    }

    bool Runtime::ToggleFavorite(std::uint16_t a_titleID)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if (!IsAcquired(a_titleID)) {
            return false;
        }

        if (IsCustomTitleID(a_titleID)) {
            auto& title = customTitles_[a_titleID - kCustomTitleFirstID];
            title.favorite = !title.favorite;
            return true;
        }
        favorites_.flip(a_titleID);
        return true;
    }

    bool Runtime::SelectTitle(
        std::size_t a_slot1Based,
        std::uint16_t a_titleID,
        std::uint8_t a_segmentIndex)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if (a_slot1Based == 0 || a_slot1Based > kSlotCount) {
            return false;
        }

        auto& slot = slots_[a_slot1Based - 1];
        if (!slot.active) {
            return false;
        }

        if (a_titleID == 0) {
            ClearSelection(slot);
            slot.autoEnabled = false;
            return true;
        }

        if (!ValidateSelection(slot, a_titleID, a_segmentIndex)) {
            return false;
        }

        slot.selectedTitleID = a_titleID;
        slot.selectedSegmentIndex = a_segmentIndex;
        slot.autoEnabled = false;
        return true;
    }

    bool Runtime::SetAuto(std::size_t a_slot1Based, bool a_enabled)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if (a_slot1Based == 0 || a_slot1Based > kSlotCount) {
            return false;
        }

        auto& slot = slots_[a_slot1Based - 1];
        if (!slot.active) {
            return false;
        }

        slot.autoEnabled = a_enabled;
        if (a_enabled) {
            (void)ApplyAutoCandidate(slot);
        }
        return true;
    }

    bool Runtime::RemoveSlot(std::size_t a_slot1Based)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if (a_slot1Based == 0 || a_slot1Based > kSlotCount) {
            return false;
        }

        auto& slot = slots_[a_slot1Based - 1];
        if (!slot.active) {
            return false;
        }

        slot = FreshSlot(
            a_slot1Based,
            false,
            DefaultRole(a_slot1Based));
        return true;
    }

    std::optional<std::size_t> Runtime::ManualAdd(Role a_role)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if (!IsValidRole(a_role)) {
            return std::nullopt;
        }

        for (std::size_t i = 0; i < kSlotCount; ++i) {
            if (!slots_[i].active) {
                slots_[i] = FreshSlot(i + 1, true, a_role);
                return i + 1;
            }
        }

        return std::nullopt;
    }

    bool Runtime::EnsurePresetSlot(std::size_t a_slot1Based)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if (a_slot1Based == 0 || a_slot1Based > kSlotCount) {
            return false;
        }

        auto& slot = slots_[a_slot1Based - 1];
        if (slot.active) {
            return true;
        }

        slot = FreshSlot(
            a_slot1Based,
            true,
            DefaultRole(a_slot1Based));
        return true;
    }

    bool Runtime::ApplyPresetPresenceMask(std::uint8_t a_activeMask)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if ((a_activeMask & 0xC0u) != 0u) {
            return false;
        }

        for (std::size_t i = 0; i < kSlotCount; ++i) {
            const bool required = (a_activeMask & (1u << i)) != 0u;
            const bool current = slots_[i].active;

            if (current && !required) {
                slots_[i] = FreshSlot(i + 1, false, DefaultRole(i + 1));
            }
            else if (!current && required) {
                slots_[i] = FreshSlot(i + 1, true, DefaultRole(i + 1));
            }
        }

        return true;
    }

    void Runtime::SetNickname(std::string a_nickname)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        nickname_ = std::move(a_nickname);
    }

    const std::string& Runtime::Nickname() const noexcept
    {
        return nickname_;
    }

    std::string Runtime::EffectiveName(std::string_view a_characterName) const
    {
        if (!nickname_.empty()) {
            return nickname_;
        }
        return std::string{ a_characterName };
    }

    std::string Runtime::ResolveSlotText(
        std::size_t a_slot1Based,
        std::string_view a_characterName) const
    {
        const auto* slot = GetSlot(a_slot1Based);
        if (!slot || !slot->active) {
            return {};
        }

        const auto effectiveName = EffectiveName(a_characterName);

        if (slot->selectedTitleID == 0) {
            if (slot->role == Role::kNameTemplate) {
                return effectiveName;
            }
            return {};
        }

        if (IsCustomTitleID(slot->selectedTitleID)) {
            const auto* custom = GetCustomTitle(slot->selectedTitleID);
            if (!custom) {
                return {};
            }
            if (slot->selectedTitleID < kCustomTitleFirstID + 10) {
                return custom->before;
            }
            return custom->before + effectiveName + custom->after;
        }

        const auto* title = FindTitle(slot->selectedTitleID);
        if (!title) {
            return {};
        }

        const auto* segment = FindSegment(*title, slot->selectedSegmentIndex);
        if (!segment || !SegmentSupportsRole(*segment, slot->role)) {
            return {};
        }

        if (segment->containsNameToken) {
            return ReplaceNameToken(segment->text, effectiveName);
        }
        return std::string{ segment->text };
    }

    PersistedState Runtime::MakePersistedState() const
    {
        PersistedState state;
        state.nickname = nickname_;
        state.slots = slots_;
        // TIT1 v1 retains only official favorites. Custom favorites use
        // separate custom records, added during the Persistence phase.
        for (const auto& title : kTitles) {
            if (favorites_.test(title.numericID)) {
                state.favoriteTitleIDs.push_back(title.numericID);
            }
        }
        return state;
    }

    void Runtime::ApplyPersistedState(const PersistedState& a_state)
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        nickname_ = a_state.nickname;
        favorites_.reset();
        lastAcquisitionBatch_.clear();

        for (std::size_t i = 0; i < kSlotCount; ++i) {
            const auto slotNumber = i + 1;
            const auto& incoming = a_state.slots[i];

            if (!incoming.active) {
                slots_[i] = FreshSlot(
                    slotNumber,
                    false,
                    DefaultRole(slotNumber));
                continue;
            }

            const auto role = IsValidRole(incoming.role) ?
                incoming.role :
                DefaultRole(slotNumber);

            slots_[i] = FreshSlot(slotNumber, true, role);
            slots_[i].autoEnabled = incoming.autoEnabled;

            if (incoming.selectedTitleID == 0) {
                // NAME_TEMPLATE => NAME_ONLY, others => EMPTY.
                ClearSelection(slots_[i]);
                continue;
            }

            if (ValidateSelection(
                    slots_[i],
                    incoming.selectedTitleID,
                    incoming.selectedSegmentIndex)) {

                slots_[i].selectedTitleID = incoming.selectedTitleID;
                slots_[i].selectedSegmentIndex = incoming.selectedSegmentIndex;
            }
            else {
                ClearSelection(slots_[i]);
            }
        }

        for (const auto id : a_state.favoriteTitleIDs) {
            if (id <= kMaxTitleNumericID && IsAcquired(id)) {
                favorites_.set(id);
            }
        }
    }

    Role Runtime::DefaultRole(std::size_t a_slot1Based) noexcept
    {
        if (a_slot1Based == 0 || a_slot1Based > kSlotCount) {
            return Role::kStandalone;
        }
        return kDefaultRoles[a_slot1Based - 1];
    }

    bool Runtime::DefaultActive(std::size_t a_slot1Based) noexcept
    {
        if (a_slot1Based == 0 || a_slot1Based > kSlotCount) {
            return false;
        }
        return kDefaultActive[a_slot1Based - 1];
    }

    bool Runtime::IsValidRole(Role a_role) noexcept
    {
        switch (a_role) {
        case Role::kPrefix:
        case Role::kSuffix:
        case Role::kStandalone:
        case Role::kNameTemplate:
            return true;
        }
        return false;
    }

    SlotState Runtime::FreshSlot(
        std::size_t,
        bool a_active,
        Role a_role) noexcept
    {
        SlotState result;
        result.active = a_active;
        result.role = a_role;
        result.selectedTitleID = 0;
        result.selectedSegmentIndex = 0;
        result.autoEnabled = true;
        return result;
    }

    bool Runtime::ValidateSelection(
        const SlotState& a_slot,
        std::uint16_t a_titleID,
        std::uint8_t a_segmentIndex) const noexcept
    {
        if (!a_slot.active || !IsAcquired(a_titleID)) {
            return false;
        }

        if (IsCustomTitleID(a_titleID)) {
            return a_segmentIndex == 0 &&
                a_slot.role == (a_titleID < kCustomTitleFirstID + 10 ?
                    Role::kStandalone : Role::kNameTemplate);
        }

        const auto* title = FindTitle(a_titleID);
        if (!title) {
            return false;
        }

        const auto* segment = FindSegment(*title, a_segmentIndex);
        return segment && SegmentSupportsRole(*segment, a_slot.role);
    }

    void Runtime::ClearSelection(SlotState& a_slot) noexcept
    {
        a_slot.selectedTitleID = 0;
        a_slot.selectedSegmentIndex = 0;
    }

    bool Runtime::ApplyAutoCandidate(SlotState& a_slot)
    {
        if (!a_slot.active || !a_slot.autoEnabled || lastAcquisitionBatch_.empty()) {
            return false;
        }

        for (const auto titleID : lastAcquisitionBatch_) {
            if (!IsAcquired(titleID)) {
                continue;
            }

            const auto* title = FindTitle(titleID);
            if (!title) {
                continue;
            }

            const auto* segment = LowestCompatibleSegment(*title, a_slot.role);
            if (!segment) {
                continue;
            }

            a_slot.selectedTitleID = titleID;
            a_slot.selectedSegmentIndex = segment->index;
            return true;
        }

        return false;
    }

    void Runtime::ApplyAutoToAllSlots()
    {
        for (auto& slot : slots_) {
            (void)ApplyAutoCandidate(slot);
        }
    }

    void Runtime::ClearLostSelectionsAndFavorites(
        std::span<const std::uint16_t> a_lost)
    {
        for (const auto id : a_lost) {
            if (id <= kMaxTitleNumericID) {
                favorites_.reset(id);
            }

            for (auto& slot : slots_) {
                if (slot.selectedTitleID == id) {
                    ClearSelection(slot);
                }
            }
        }
    }
}
