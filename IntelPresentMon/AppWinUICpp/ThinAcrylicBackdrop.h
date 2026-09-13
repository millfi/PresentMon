// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "ThinAcrylicBackdrop.g.h"

namespace winrt::PresentMon::UI::implementation
{
    struct ThinAcrylicBackdrop : ThinAcrylicBackdropT<ThinAcrylicBackdrop>
    {
        ThinAcrylicBackdrop() = default;

        void OnTargetConnected(
            winrt::Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop const& connectedTarget,
            winrt::Microsoft::UI::Xaml::XamlRoot const& xamlRoot);
        void OnDefaultSystemBackdropConfigurationChanged(
            winrt::Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop const& target,
            winrt::Microsoft::UI::Xaml::XamlRoot const& xamlRoot);
        void OnTargetDisconnected(
            winrt::Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop const& disconnectedTarget);

    private:
        winrt::Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController controller_{ nullptr };
    };
}

namespace winrt::PresentMon::UI::factory_implementation
{
    struct ThinAcrylicBackdrop : ThinAcrylicBackdropT<ThinAcrylicBackdrop, implementation::ThinAcrylicBackdrop>
    {
    };
}
