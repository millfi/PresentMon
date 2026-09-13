// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

#include "../Core/ConfigModels.h"

#include <functional>
#include <optional>
#include <string>

namespace pmon::ui::views::HotkeyEditor
{
    using EditedCallback = std::function<winrt::Windows::Foundation::IAsyncAction(std::optional<core::HotkeyBinding>)>;

    std::string Format(const std::optional<core::HotkeyBinding>& binding);
    winrt::Windows::Foundation::IAsyncAction EditAsync(const winrt::Microsoft::UI::Xaml::XamlRoot& root,
        const core::HotkeyBinding& current, EditedCallback edited);
}
