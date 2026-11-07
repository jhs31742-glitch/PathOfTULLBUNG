#include "Profiles/RuntimeBoundary.h"
#include "PCH.h"
#include "TitleRuntime.h"
#include "Prisma/FloatingTitleMinimal.h"

#include "BuildState.h"

#include <array>

namespace
{
    constexpr std::array<
        TULLBUNG::BuildState::ClassID,
        TULLBUNG::TitleSystem::kClassCount>
        kTitleClassMap{{
            TULLBUNG::BuildState::ClassID::kWarrior,
            TULLBUNG::BuildState::ClassID::kBerserker,
            TULLBUNG::BuildState::ClassID::kGuardian,
            TULLBUNG::BuildState::ClassID::kHunter,
            TULLBUNG::BuildState::ClassID::kAssassin,
            TULLBUNG::BuildState::ClassID::kMarksman,
            TULLBUNG::BuildState::ClassID::kPriest,
            TULLBUNG::BuildState::ClassID::kPaladin,
            TULLBUNG::BuildState::ClassID::kInquisitor,
            TULLBUNG::BuildState::ClassID::kMage,
            TULLBUNG::BuildState::ClassID::kVoidMage,
            TULLBUNG::BuildState::ClassID::kSiegeMage
        }};

    // Session-only runtime gate. False on every normal save load/new game.
    bool g_s19TitleQASuspended = false;

    TULLBUNG::TitleSystem::Runtime&
        RuntimeInstance() noexcept
    {
        static TULLBUNG::TitleSystem::
            Runtime runtime;

        return runtime;
    }
}

namespace TULLBUNG::TitleRuntime
{
    void SetQAEvaluationSuspended(bool a_suspend) noexcept
    {
        g_s19TitleQASuspended = a_suspend;
        logger::info("[S19QA][Title] Evaluation suspended={}", a_suspend);
    }

    bool IsQAEvaluationSuspended() noexcept
    {
        return g_s19TitleQASuspended;
    }

    TitleSystem::Runtime&
        GetRuntime() noexcept
    {
        return RuntimeInstance();
    }

    TitleSystem::ClassLevels
        CaptureClassLevels() noexcept
    {
        TitleSystem::ClassLevels
            levels{};

        for (std::size_t i = 0;
             i < kTitleClassMap.size();
             ++i) {

            levels[i] =
                BuildState::GetClassLevel(
                    kTitleClassMap[i]);
        }

        return levels;
    }

    void ResetDefaults() noexcept
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        RuntimeInstance().
            ResetDefaults();
    }

    void EstablishBaselineFromBuildState() noexcept
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        auto& runtime =
            RuntimeInstance();

        runtime.ResetDefaults();

        runtime.EstablishBaseline(
            CaptureClassLevels());
    }

    TitleSystem::RefreshResult
        RefreshFromBuildState()
    {
        std::lock_guard profileOwnerLock(UIProfiles::RuntimeMutex());
        if (g_s19TitleQASuspended) {
            return {};
        }
        auto result = RuntimeInstance().RefreshFromClassLevels(CaptureClassLevels());
        if (result.anyStateChanged) TULLBUNG::FloatingTitleMinimal::MarkTitleDirty();
        return result;
    }
}