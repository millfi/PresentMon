// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>

#include <functional>
#include <string>
#include <vector>

namespace pmon::ui::views::FormControls
{
    using AsyncCallback = std::function<winrt::Windows::Foundation::IAsyncAction()>;

    winrt::Microsoft::UI::Xaml::Controls::Border Row(const std::string& title,
        const std::string& description, const winrt::Microsoft::UI::Xaml::UIElement& control);
    winrt::Microsoft::UI::Xaml::Controls::TextBlock Heading(const std::string& title);
    winrt::Microsoft::UI::Xaml::Controls::TextBlock Description(const std::string& text);
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch Toggle(const std::string& id,
        const std::string& name, bool value, std::function<void(bool)> changed);
    winrt::Microsoft::UI::Xaml::Controls::NumberBox Number(const std::string& id,
        const std::string& name, double value, double minimum, double maximum, double step,
        std::function<void(double)> changed);
    winrt::Microsoft::UI::Xaml::Controls::TextBox Text(const std::string& id,
        const std::string& name, const std::string& value, std::function<void(const std::string&)> changed,
        bool multiline = false);
    winrt::Microsoft::UI::Xaml::Controls::ComboBox Choice(const std::string& id,
        const std::string& name, const std::vector<std::string>& options, int selectedIndex,
        std::function<void(int)> changed);
    winrt::Microsoft::UI::Xaml::Controls::Button Color(const std::string& id,
        const std::string& name, winrt::Windows::UI::Color value,
        std::function<void(winrt::Windows::UI::Color)> changed);
    winrt::Microsoft::UI::Xaml::Controls::Button ActionButton(const std::string& id,
        const std::string& text, std::function<void()> action);
    winrt::Microsoft::UI::Xaml::Controls::Button AsyncButton(const std::string& id,
        const std::string& text, AsyncCallback action);
    void Identify(const winrt::Microsoft::UI::Xaml::DependencyObject& element,
        const std::string& id, const std::string& name);
}
