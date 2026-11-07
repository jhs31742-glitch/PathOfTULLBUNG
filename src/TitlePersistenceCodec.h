#pragma once

#include "TitleSystem.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace TULLBUNG::TitleSystem::PersistenceCodec
{
    inline constexpr std::uint32_t kPayloadSchemaVersion = 1;

    enum class DecodeStatus
    {
        kOK,
        kTruncated,
        kUnsupportedVersion,
        kInvalidRole,
        kTrailingData,
        kInvalidCustomRecord
    };

    inline constexpr std::uint32_t kCustomPayloadSchemaVersion = 1;

    // Independent custom-title payload. TIT1 v1 remains unchanged.
    [[nodiscard]] std::vector<std::byte> EncodeCustom(
        const CustomTitleRecords& a_records);

    [[nodiscard]] DecodeStatus DecodeCustom(
        std::span<const std::byte> a_bytes,
        CustomTitleRecords& a_out);

    // Pure codec only. It does not call SKSE serialization APIs and therefore
    // can be developed/tested in parallel. The production Persistence.cpp
    // adapter should wrap this payload in TIT1 record version 1 later.
    [[nodiscard]] std::vector<std::byte> Encode(const PersistedState& a_state);
    [[nodiscard]] DecodeStatus Decode(
        std::span<const std::byte> a_bytes,
        PersistedState& a_out);
}
