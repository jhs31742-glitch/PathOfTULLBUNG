#pragma once
// FT preference is separate from ESS, co-save and exported UI profiles.
#include "Prisma/PrismaUI_API.h"
#include <cstdint>
#include <string_view>
namespace TULLBUNG::FloatingTitleMinimal {
using StatusSink = void (*)(const char*);
void Configure(PRISMA_UI_API::IVPrismaUI1* api, StatusSink sink) noexcept;
void SetGameReady(bool ready) noexcept;
void MarkTitleDirty() noexcept;
void RefreshTitles() noexcept;
void SetFontCatalog(std::string_view ownedJson) noexcept;
bool HandleCommand(std::string_view ownedCommand) noexcept;
void InvalidateForImport() noexcept;
void SuspendForMenu() noexcept;
// Activation and frame sampling use the verified engine main window thread.
// Engine main-thread ID is checked; camera-final order / Prisma viewport parity remain RUNTIME_UNVERIFIED.
struct FrameContext {
    std::uint64_t engineFrame{};
    float width{}, height{};
};
bool BindVerifiedFrameThread(std::uint32_t threadID) noexcept;
void OnVerifiedGameFrame(const FrameContext& context) noexcept;
} // namespace TULLBUNG::FloatingTitleMinimal
