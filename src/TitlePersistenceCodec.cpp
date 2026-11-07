#include "TitlePersistenceCodec.h"

#include <limits>
#include <string>

namespace TULLBUNG::TitleSystem::PersistenceCodec
{
    namespace
    {
        void PutU8(std::vector<std::byte>& a_out, std::uint8_t a_value)
        {
            a_out.push_back(static_cast<std::byte>(a_value));
        }

        void PutU32(std::vector<std::byte>& a_out, std::uint32_t a_value)
        {
            for (unsigned shift = 0; shift < 32; shift += 8) {
                PutU8(a_out, static_cast<std::uint8_t>((a_value >> shift) & 0xFFu));
            }
        }

        bool GetU8(
            std::span<const std::byte> a_bytes,
            std::size_t& a_cursor,
            std::uint8_t& a_out)
        {
            if (a_cursor >= a_bytes.size()) {
                return false;
            }
            a_out = static_cast<std::uint8_t>(a_bytes[a_cursor]);
            ++a_cursor;
            return true;
        }

        bool GetU32(
            std::span<const std::byte> a_bytes,
            std::size_t& a_cursor,
            std::uint32_t& a_out)
        {
            if (a_bytes.size() - a_cursor < 4) {
                return false;
            }

            a_out = 0;
            for (unsigned shift = 0; shift < 32; shift += 8) {
                a_out |= static_cast<std::uint32_t>(
                    static_cast<std::uint8_t>(a_bytes[a_cursor++])) << shift;
            }
            return true;
        }

        bool DecodeRole(std::uint8_t a_raw, Role& a_out)
        {
            const auto role = static_cast<Role>(a_raw);
            if (!Runtime::IsValidRole(role)) {
                return false;
            }
            a_out = role;
            return true;
        }
    }

    std::vector<std::byte> Encode(const PersistedState& a_state)
    {
        std::vector<std::byte> out;
        out.reserve(128 + a_state.nickname.size() + a_state.favoriteTitleIDs.size() * 4);

        PutU32(out, kPayloadSchemaVersion);
        PutU32(out, static_cast<std::uint32_t>(a_state.nickname.size()));
        for (const char raw : a_state.nickname) {
            PutU8(out, static_cast<std::uint8_t>(static_cast<unsigned char>(raw)));
        }

        for (const auto& slot : a_state.slots) {
            PutU8(out, slot.active ? 1u : 0u);
            PutU8(out, static_cast<std::uint8_t>(slot.role));
            PutU32(out, slot.selectedTitleID);
            PutU8(out, slot.selectedSegmentIndex);
            PutU8(out, slot.autoEnabled ? 1u : 0u);
        }

        PutU32(out, static_cast<std::uint32_t>(a_state.favoriteTitleIDs.size()));
        for (const auto id : a_state.favoriteTitleIDs) {
            PutU32(out, id);
        }

        return out;
    }

    std::vector<std::byte> EncodeCustom(const CustomTitleRecords& a_records)
    {
        const auto visible = [](std::string_view text) {
            return text.find_first_not_of(" \t\r\n") !=
                   std::string_view::npos;
        };

        std::vector<std::byte> out;
        out.reserve(8 + kCustomTitleCount * 14);

        PutU32(out, kCustomPayloadSchemaVersion);
        PutU32(out, static_cast<std::uint32_t>(kCustomTitleCount));

        for (std::size_t i = 0; i < kCustomTitleCount; ++i) {
            const auto& record = a_records[i];

            if (!record.present) {
                PutU8(out, 0);
                PutU8(out, 0);
                PutU32(out, 0);
                PutU32(out, 0);
                continue;
            }

            if (record.before.size() > kCustomTextMaxBytes ||
                record.after.size() > kCustomTextMaxBytes ||
                (i < 10 && (!visible(record.before) ||
                             !record.after.empty())) ||
                (i >= 10 && !visible(record.before) &&
                              !visible(record.after))) {
                return {};
            }

            PutU8(out, 1);
            PutU8(out, record.favorite ? 1 : 0);
            PutU32(out, static_cast<std::uint32_t>(record.before.size()));
            PutU32(out, static_cast<std::uint32_t>(record.after.size()));

            for (const unsigned char ch : record.before) {
                PutU8(out, ch);
            }
            for (const unsigned char ch : record.after) {
                PutU8(out, ch);
            }
        }

        return out;
    }

    DecodeStatus DecodeCustom(
        std::span<const std::byte> a_bytes,
        CustomTitleRecords& a_out)
    {
        std::size_t cursor = 0;
        std::uint32_t version{};
        std::uint32_t count{};

        if (!GetU32(a_bytes, cursor, version)) {
            return DecodeStatus::kTruncated;
        }
        if (version != kCustomPayloadSchemaVersion) {
            return DecodeStatus::kUnsupportedVersion;
        }
        if (!GetU32(a_bytes, cursor, count)) {
            return DecodeStatus::kTruncated;
        }
        if (count != kCustomTitleCount) {
            return DecodeStatus::kInvalidCustomRecord;
        }

        const auto visible = [](std::string_view text) {
            return text.find_first_not_of(" \t\r\n") !=
                   std::string_view::npos;
        };

        CustomTitleRecords decoded{};

        for (std::size_t i = 0; i < kCustomTitleCount; ++i) {
            std::uint8_t present{};
            std::uint8_t favorite{};
            std::uint32_t beforeLength{};
            std::uint32_t afterLength{};

            if (!GetU8(a_bytes, cursor, present) ||
                !GetU8(a_bytes, cursor, favorite) ||
                !GetU32(a_bytes, cursor, beforeLength) ||
                !GetU32(a_bytes, cursor, afterLength)) {
                return DecodeStatus::kTruncated;
            }

            if (present > 1 || favorite > 1 ||
                beforeLength > kCustomTextMaxBytes ||
                afterLength > kCustomTextMaxBytes ||
                (present == 0 &&
                 (favorite != 0 || beforeLength != 0 ||
                  afterLength != 0))) {
                return DecodeStatus::kInvalidCustomRecord;
            }

            if (beforeLength > a_bytes.size() - cursor) {
                return DecodeStatus::kTruncated;
            }

            const auto* begin = reinterpret_cast<const char*>(
                a_bytes.data() + cursor);
            decoded[i].before.assign(begin, beforeLength);
            cursor += beforeLength;

            if (afterLength > a_bytes.size() - cursor) {
                return DecodeStatus::kTruncated;
            }

            begin = reinterpret_cast<const char*>(
                a_bytes.data() + cursor);
            decoded[i].after.assign(begin, afterLength);
            cursor += afterLength;

            if (present != 0) {
                if ((i < 10 && (!visible(decoded[i].before) ||
                                !decoded[i].after.empty())) ||
                    (i >= 10 && !visible(decoded[i].before) &&
                                !visible(decoded[i].after))) {
                    return DecodeStatus::kInvalidCustomRecord;
                }

                decoded[i].present = true;
                decoded[i].favorite = favorite != 0;
            }
        }

        if (cursor != a_bytes.size()) {
            return DecodeStatus::kTrailingData;
        }

        a_out = std::move(decoded);
        return DecodeStatus::kOK;
    }
    DecodeStatus Decode(
        std::span<const std::byte> a_bytes,
        PersistedState& a_out)
    {
        std::size_t cursor = 0;
        std::uint32_t version = 0;
        if (!GetU32(a_bytes, cursor, version)) {
            return DecodeStatus::kTruncated;
        }
        if (version != kPayloadSchemaVersion) {
            return DecodeStatus::kUnsupportedVersion;
        }

        std::uint32_t nicknameLength = 0;
        if (!GetU32(a_bytes, cursor, nicknameLength) ||
            nicknameLength > a_bytes.size() - cursor) {
            return DecodeStatus::kTruncated;
        }

        PersistedState decoded;
        decoded.nickname.assign(
            reinterpret_cast<const char*>(a_bytes.data() + cursor),
            nicknameLength);
        cursor += nicknameLength;

        for (auto& slot : decoded.slots) {
            std::uint8_t active = 0;
            std::uint8_t roleRaw = 0;
            std::uint32_t titleID = 0;
            std::uint8_t segment = 0;
            std::uint8_t autoEnabled = 0;

            if (!GetU8(a_bytes, cursor, active) ||
                !GetU8(a_bytes, cursor, roleRaw) ||
                !GetU32(a_bytes, cursor, titleID) ||
                !GetU8(a_bytes, cursor, segment) ||
                !GetU8(a_bytes, cursor, autoEnabled)) {
                return DecodeStatus::kTruncated;
            }

            Role role{};
            if (!DecodeRole(roleRaw, role)) {
                return DecodeStatus::kInvalidRole;
            }

            slot.active = active != 0;
            slot.role = role;
            slot.selectedTitleID = titleID <= (std::numeric_limits<std::uint16_t>::max)() ?
                static_cast<std::uint16_t>(titleID) : 0;
            slot.selectedSegmentIndex = segment;
            slot.autoEnabled = autoEnabled != 0;
        }

        std::uint32_t favoriteCount = 0;
        if (!GetU32(a_bytes, cursor, favoriteCount)) {
            return DecodeStatus::kTruncated;
        }

        if (favoriteCount > (a_bytes.size() - cursor) / 4) {
            return DecodeStatus::kTruncated;
        }

        decoded.favoriteTitleIDs.reserve(favoriteCount);
        for (std::uint32_t i = 0; i < favoriteCount; ++i) {
            std::uint32_t id = 0;
            if (!GetU32(a_bytes, cursor, id)) {
                return DecodeStatus::kTruncated;
            }
            if (id <= (std::numeric_limits<std::uint16_t>::max)()) {
                decoded.favoriteTitleIDs.push_back(static_cast<std::uint16_t>(id));
            }
        }

        if (cursor != a_bytes.size()) {
            return DecodeStatus::kTrailingData;
        }

        a_out = std::move(decoded);
        return DecodeStatus::kOK;
    }
}
