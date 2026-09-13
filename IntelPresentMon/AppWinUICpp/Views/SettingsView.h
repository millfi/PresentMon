// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace pmon::ui::core
{
    struct Preferences;
    struct IntrospectionData;
    struct RgbaColor;
}

namespace pmon::ui::views
{
    class SettingsView
    {
    public:
        using AsyncCallback = std::function<winrt::Windows::Foundation::IAsyncAction()>;
        using ExploreFolderCallback = std::function<winrt::Windows::Foundation::IAsyncAction(const std::string&)>;
        using AppInfo = std::vector<std::pair<std::string, std::string>>;

        SettingsView(const std::string& section, core::Preferences& preferences,
            const core::IntrospectionData& introspection, std::function<void()> changed,
            AsyncCallback editHotkeys, AsyncCallback resetPreferences, ExploreFolderCallback exploreFolder,
            AppInfo appInfo, bool enableDevOptions);

        winrt::Microsoft::UI::Xaml::UIElement Element() const;

    private:
        void BuildOverlay(bool enableDevOptions);
        winrt::Microsoft::UI::Xaml::UIElement CreatePositionPicker();
        void BuildData(bool enableDevOptions);
        void BuildCapture();
        void BuildLogging();
        void BuildOther();
        void BuildAbout(const AppInfo& appInfo);
        winrt::Windows::Foundation::IAsyncAction ConfirmResetAsync();
        void AddToggle(const std::string& id, const std::string& title, const std::string& description,
            bool value, std::function<void(bool)> setter);
        void AddNumber(const std::string& id, const std::string& title, const std::string& description,
            double value, double minimum, double maximum, double step, std::function<void(double)> setter);
        void AddNumber(const winrt::Microsoft::UI::Xaml::Controls::StackPanel& panel,
            const std::string& id, const std::string& title, const std::string& description,
            double value, double minimum, double maximum, double step, std::function<void(double)> setter);
        void AddColor(const winrt::Microsoft::UI::Xaml::Controls::StackPanel& panel,
            const std::string& id, const std::string& title, const std::string& description,
            core::RgbaColor& value, std::function<void(core::RgbaColor)> setter);
        void AddExpander(const std::string& id, const std::string& title, const std::string& description,
            const winrt::Microsoft::UI::Xaml::UIElement& content);
        void Update(std::function<void()> setter);

        core::Preferences& preferences_;
        const core::IntrospectionData& introspection_;
        std::function<void()> changed_;
        AsyncCallback editHotkeys_;
        AsyncCallback resetPreferences_;
        ExploreFolderCallback exploreFolder_;
        winrt::Microsoft::UI::Xaml::Controls::StackPanel layout_;
    };
}
