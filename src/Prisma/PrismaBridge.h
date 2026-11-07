#pragma once

namespace TULLBUNG::PrismaBridge
{
    bool InstallInput();
    bool Initialize();

    void SetGameReady(bool a_ready);

    void NotifyHudStateChanged() noexcept;
    void NotifyVoidCountChanged() noexcept;

    bool HasView();
    bool IsVisible();

    void Open();
    void Close();
    void Toggle();
}