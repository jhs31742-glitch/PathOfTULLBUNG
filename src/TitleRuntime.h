#pragma once

#include "TitleSystem.h"

namespace TULLBUNG::TitleRuntime
{
    [[nodiscard]]
    TitleSystem::Runtime& GetRuntime() noexcept;

    [[nodiscard]]
    TitleSystem::ClassLevels CaptureClassLevels() noexcept;

    // S19 QA: only suspends condition evaluation, never serializes or clears titles.
    void SetQAEvaluationSuspended(bool a_suspend) noexcept;
    [[nodiscard]] bool IsQAEvaluationSuspended() noexcept;

    void ResetDefaults() noexcept;

    void EstablishBaselineFromBuildState() noexcept;

    [[nodiscard]]
    TitleSystem::RefreshResult RefreshFromBuildState();
}