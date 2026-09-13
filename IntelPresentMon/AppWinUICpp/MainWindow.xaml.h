// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "MainWindow.g.h"

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pmon::ui::core {
enum class HotkeyAction;
}

namespace pmon::ui::services {
class AppSession;
struct StartupOptions;
}

namespace pmon::ui::views {
class LoadoutView;
class SettingsView;
}

namespace winrt::PresentMon::UI::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    explicit MainWindow(pmon::ui::services::StartupOptions const& options);
    ~MainWindow();
    winrt::Windows::Foundation::IAsyncAction RunShellSmokeAsync();

    void Root_Loaded(winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void Navigation_SelectionChanged(winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
    void Navigation_PaneChanged(winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        winrt::Windows::Foundation::IInspectable const& args);
    void Navigation_DisplayModeChanged(winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        winrt::Microsoft::UI::Xaml::Controls::NavigationViewDisplayModeChangedEventArgs const& args);
    void Appearance_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
    void PageLayout_SizeChanged(winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
    void AppWindow_Closing(winrt::Microsoft::UI::Windowing::AppWindow const& sender,
        winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args);

private:
    winrt::fire_and_forget InitializeAsync();
    winrt::fire_and_forget NavigateAsync(winrt::hstring next);
    winrt::fire_and_forget RefreshProcessesAsync();
    winrt::fire_and_forget ToggleCaptureAsync();
    winrt::fire_and_forget SelectProcessAsync(std::optional<int> pid);
    winrt::fire_and_forget SelectPresetAsync(int index);
    winrt::Windows::Foundation::IAsyncAction EditHotkeyAsync(pmon::ui::core::HotkeyAction action);
    winrt::Windows::Foundation::IAsyncAction ChooseHotkeyAsync();
    winrt::Windows::Foundation::IAsyncAction ImportLoadoutAsync();
    winrt::Windows::Foundation::IAsyncAction ExportLoadoutAsync();
    winrt::fire_and_forget CloseAsync();

    void Initialize(pmon::ui::services::StartupOptions const& options);
    void RenderView();
    winrt::Microsoft::UI::Xaml::UIElement BuildOverview();
    void SynchronizeNavigation();
    void UpdateState();
    void ShowNotification(std::string const& message);
    void UpdateAppearanceVisibility();
    void FilterProcesses(winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBox const& selector);
    std::optional<int> FindProcess(winrt::hstring const& query) const;
    void SetContent(winrt::Microsoft::UI::Xaml::UIElement const& content);
    void SetWindowIdentity();
    void ClearWindowIdentity() noexcept;
    winrt::Microsoft::UI::Xaml::DependencyObject FindByAutomationId(
        winrt::Microsoft::UI::Xaml::DependencyObject const& root, winrt::hstring const& id) const;
    void InvokeControl(winrt::Microsoft::UI::Xaml::DependencyObject const& control) const;

    std::shared_ptr<pmon::ui::services::AppSession> session_;
    std::unique_ptr<pmon::ui::views::LoadoutView> loadoutView_;
    std::unique_ptr<pmon::ui::views::SettingsView> settingsView_;
    void* windowHandle_ = nullptr;
    winrt::hstring section_ = L"Overview";
    bool initialized_ = false;
    bool updating_ = false;
    bool navigating_ = false;
    bool closing_ = false;
    bool canClose_ = false;
    bool refreshingProcesses_ = false;
    uint64_t navigationVersion_ = 0;
    std::optional<int> displayedPid_;
    winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBox processSelector_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ComboBox presetSelector_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch autoTarget_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch showOverlay_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::AppBarButton captureButton_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::AppBarButton clearTargetButton_{ nullptr };
    std::vector<std::pair<int, winrt::Microsoft::UI::Xaml::Controls::Button>> hotkeyButtons_;
    winrt::Microsoft::UI::Windowing::AppWindow appWindow_{ nullptr };
    winrt::event_token appWindowClosingToken_{};
    winrt::Microsoft::UI::Xaml::Window::Closed_revoker closedRevoker_;
};

}

namespace winrt::PresentMon::UI::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}
