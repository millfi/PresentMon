// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "pch.h"
#include "ThinAcrylicBackdrop.h"
#include "ThinAcrylicBackdrop.g.cpp"

namespace winrt::PresentMon::UI::implementation
{
    void ThinAcrylicBackdrop::OnTargetConnected(
        Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop const& connectedTarget,
        Microsoft::UI::Xaml::XamlRoot const& xamlRoot)
    {
        m_inner.as<Microsoft::UI::Xaml::Media::ISystemBackdropOverrides>().OnTargetConnected(connectedTarget, xamlRoot);
        if (!Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController::IsSupported()) {
            return;
        }
        if (controller_) {
            throw hresult_error(E_FAIL, L"Create a separate acrylic backdrop for each window.");
        }

        controller_ = Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController();
        controller_.Kind(Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicKind::Thin);
        controller_.SetSystemBackdropConfiguration(GetDefaultSystemBackdropConfiguration(connectedTarget, xamlRoot));
        controller_.AddSystemBackdropTarget(connectedTarget);
    }

    void ThinAcrylicBackdrop::OnDefaultSystemBackdropConfigurationChanged(
        Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop const&,
        Microsoft::UI::Xaml::XamlRoot const&)
    {
        // XAML has already updated the configuration observed by the controller.
        // Calling the base callback can receive a stale null target after wrapper release.
    }

    void ThinAcrylicBackdrop::OnTargetDisconnected(
        Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop const& disconnectedTarget)
    {
        if (controller_) {
            controller_.RemoveSystemBackdropTarget(disconnectedTarget);
            controller_.Close();
            controller_ = nullptr;
        }
        m_inner.as<Microsoft::UI::Xaml::Media::ISystemBackdropOverrides>().OnTargetDisconnected(disconnectedTarget);
    }
}
