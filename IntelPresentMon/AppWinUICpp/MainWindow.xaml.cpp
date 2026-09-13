// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "pch.h"
#include "MainWindow.xaml.h"
#include "UiDispatcher.h"
#include "MainWindow.g.cpp"

#include "Core/ConfigModels.h"
#include "Core/ConfigurationJson.h"
#include "Core/StartupOptions.h"
#include "Services/AppSession.h"
#include "Services/WindowsServices.h"
#include "Views/FormControls.h"
#include "Views/HotkeyEditor.h"
#include "Views/LoadoutView.h"
#include "Views/SettingsView.h"

#include <microsoft.ui.xaml.window.h>
#include <shobjidl_core.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Pickers.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Automation;
using namespace Microsoft::UI::Xaml::Automation::Peers;
using namespace Microsoft::UI::Xaml::Automation::Provider;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage::Pickers;

namespace {

hstring ToHString(std::string const& value)
{
    return to_hstring(value);
}

std::string ToString(hstring const& value)
{
    return to_string(value);
}

std::string Trim(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character) != 0; }).base();
    return first < last ? std::string(first, last) : std::string{};
}

bool ContainsInsensitive(std::string source, std::string const& query)
{
    std::transform(source.begin(), source.end(), source.begin(), [](unsigned char character) { return (char)std::tolower(character); });
    auto term = query;
    std::transform(term.begin(), term.end(), term.begin(), [](unsigned char character) { return (char)std::tolower(character); });
    return source.find(term) != std::string::npos;
}

std::string HotkeyActionLabel(pmon::ui::core::HotkeyAction action)
{
    switch (action) {
    case pmon::ui::core::HotkeyAction::ToggleCapture: return "Start or stop capture";
    case pmon::ui::core::HotkeyAction::ToggleOverlay: return "Show or hide overlay";
    case pmon::ui::core::HotkeyAction::CyclePreset: return "Cycle overlay preset";
    case pmon::ui::core::HotkeyAction::ToggleEtlLogging: return "Toggle ETL logging";
    }
    return {};
}

void Identify(DependencyObject const& element, std::string const& id, std::string const& name)
{
    AutomationProperties::SetAutomationId(element, ToHString(id));
    AutomationProperties::SetName(element, ToHString(name));
}

pmon::ui::views::SettingsView::AppInfo ToAppInfo(std::unordered_map<std::string, std::string> const& values)
{
    pmon::ui::views::SettingsView::AppInfo result;
    result.reserve(values.size());
    for (auto const& value : values) result.emplace_back(value.first, value.second);
    return result;
}

}

namespace winrt::PresentMon::UI::implementation {

MainWindow::MainWindow()
{
    Initialize(pmon::ui::services::StartupOptions{});
}

MainWindow::MainWindow(pmon::ui::services::StartupOptions const& options)
{
    Initialize(options);
}

MainWindow::~MainWindow()
{
    try {
        if (appWindow_ && appWindowClosingToken_.value != 0) {
            appWindow_.Closing(appWindowClosingToken_);
        }
    }
    catch (...) {
    }
    ClearWindowIdentity();
}

IAsyncAction MainWindow::RunShellSmokeAsync()
{
    auto lifetime = get_strong();
    auto const dispatcher = DispatcherQueue();
    for (int attempt = 0; attempt < 100 && !session_->IsReady(); ++attempt) {
        co_await resume_after(std::chrono::milliseconds(25));
        co_await pmon::ui::ResumeForeground{dispatcher};
    }
    if (!session_->IsReady()) {
        throw hresult_error(E_FAIL, L"PresentMon shell did not finish initialization.");
    }
    if (!session_->IsConnected()) {
        throw hresult_error(E_FAIL, L"PresentMon shell smoke requires a connected mock kernel.");
    }

    auto waitFor = [this, dispatcher](hstring id) -> IAsyncOperation<DependencyObject> {
        auto lifetime = get_strong();
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (auto root = Root()) {
                root.UpdateLayout();
                if (auto control = FindByAutomationId(root, id)) {
                    co_return control;
                }
            }
            co_await resume_after(std::chrono::milliseconds(25));
            co_await pmon::ui::ResumeForeground{dispatcher};
        }
        throw hresult_error(E_FAIL, hstring(L"Required shell automation element did not render: ") + id);
    };
    for (auto const id : { L"NavOverview", L"NavLoadout", L"NavOverlay", L"NavData", L"NavCapture", L"NavLogging", L"NavOther", L"NavAbout",
        L"AppearanceSelector", L"ConnectionStatus", L"Notification", L"ProcessStatus", L"CaptureStatus", L"OverlayStatus", L"RateStatus" }) {
        co_await waitFor(id);
    }

    auto navigate = [this, dispatcher](hstring tag, hstring title) -> IAsyncAction {
        auto lifetime = get_strong();
        auto const items = Navigation().MenuItems();
        bool selected = false;
        for (uint32_t index = 0; index < items.Size(); ++index) {
            if (auto item = items.GetAt(index).try_as<NavigationViewItem>()) {
                if (auto value = item.Tag().try_as<hstring>(); value && value == tag) {
                    Navigation().SelectedItem(item);
                    selected = true;
                    break;
                }
            }
        }
        if (!selected) {
            throw hresult_error(E_FAIL, L"Shell navigation item is missing.");
        }
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (auto root = Root()) root.UpdateLayout();
            if (PageTitle().Text() == title && ContentHost().Children().Size() != 0) break;
            co_await resume_after(std::chrono::milliseconds(25));
            co_await pmon::ui::ResumeForeground{dispatcher};
        }
        if (PageTitle().Text() != title || ContentHost().Children().Size() == 0) {
            throw hresult_error(E_FAIL, L"Navigation did not render the selected page.");
        }
    };
    co_await navigate(L"Overview", L"Overview");
    for (auto const id : { L"ToggleCapture", L"RefreshProcesses", L"ClearTarget", L"ProcessSelector", L"AutoTarget",
        L"ShowOverlay", L"PresetSelector", L"CaptureDuration", L"CaptureDirectory", L"HotkeyToggleCapture" }) {
        co_await waitFor(id);
    }
    co_await navigate(L"Overlay", L"Overlay");
    auto advancedOverlay = (co_await waitFor(L"AdvancedOverlayLayout")).try_as<Expander>();
    if (!advancedOverlay) {
        throw hresult_error(E_FAIL, L"Advanced overlay settings control has the wrong type.");
    }
    advancedOverlay.IsExpanded(true);
    auto graphAxisSize = (co_await waitFor(L"GraphAxisSize")).try_as<NumberBox>();
    if (!graphAxisSize) {
        throw hresult_error(E_FAIL, L"Graph axis size control has the wrong type.");
    }
    auto const changedAxisSize = session_->Preferences().GraphFont.AxisSize == 10.0 ? 10.5 : 10.0;
    graphAxisSize.Value(changedAxisSize);
    if (session_->Preferences().GraphFont.AxisSize != changedAxisSize) {
        throw hresult_error(E_FAIL, L"Nested graph-font control did not update session preferences.");
    }
    co_await navigate(L"Data", L"Data processing");
    auto pollRate = (co_await waitFor(L"MetricPollRate")).try_as<NumberBox>();
    if (!pollRate) {
        throw hresult_error(E_FAIL, L"Metric poll-rate control has the wrong type.");
    }
    auto const changedRate = session_->Preferences().MetricPollRate == 40.0 ? 41.0 : 40.0;
    pollRate.Value(changedRate);
    if (session_->Preferences().MetricPollRate != changedRate) {
        throw hresult_error(E_FAIL, L"Metric poll-rate control did not update session preferences.");
    }
    co_await navigate(L"Capture", L"Capture");
    co_await navigate(L"Logging", L"Logging");
    co_await navigate(L"Other", L"Other");
    co_await navigate(L"About", L"About");
    co_await navigate(L"Loadout", L"Loadout");
    co_await waitFor(L"LoadoutSummary");
    co_await waitFor(L"WidgetList");
    auto addGraph = co_await waitFor(L"AddGraph");
    InvokeControl(addGraph);
    co_await resume_after(std::chrono::milliseconds(50));
    co_await pmon::ui::ResumeForeground{dispatcher};
    if (session_->Widgets().empty()) {
        throw hresult_error(E_FAIL, L"Add graph did not create an overlay widget.");
    }
    co_await resume_after(std::chrono::milliseconds(500));
    co_await pmon::ui::ResumeForeground{dispatcher};
    auto const preferencesPath = std::filesystem::u8path(session_->Options().DataDirectory) / "preferences.json";
    auto const loadoutPath = std::filesystem::u8path(session_->Options().DataDirectory) / "Loadouts" / "custom-auto.json";
    if (!std::filesystem::exists(preferencesPath) || !std::filesystem::exists(loadoutPath)) {
        throw hresult_error(E_FAIL, L"Shell edits were not autosaved to the isolated data directory.");
    }
    auto read = [](std::filesystem::path const& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string{ std::istreambuf_iterator<char>{ input }, {} };
    };
    auto persistedPreferences = pmon::ui::core::PreferenceDocument::Parse(read(preferencesPath), session_->Introspection());
    if (persistedPreferences.Preferences.GraphFont.AxisSize != changedAxisSize
        || persistedPreferences.Preferences.MetricPollRate != changedRate) {
        throw hresult_error(E_FAIL, L"Autosaved preferences do not contain shell control edits.");
    }
    auto persistedLoadout = pmon::ui::core::LoadoutDocument::Parse(read(loadoutPath), session_->Introspection(),
        persistedPreferences.Preferences);
    if (persistedLoadout.Widgets.empty()) {
        throw hresult_error(E_FAIL, L"Autosaved custom loadout does not contain the added graph.");
    }
}

void MainWindow::Initialize(pmon::ui::services::StartupOptions const& options)
{
    InitializeComponent();
    session_ = std::make_shared<pmon::ui::services::AppSession>(options, DispatcherQueue());

    auto nativeWindow = this->m_inner.as<::IWindowNative>();
    HWND hwnd{};
    check_hresult(nativeWindow->get_WindowHandle(&hwnd));
    windowHandle_ = hwnd;
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());
    auto const iconPath = std::filesystem::u8path(options.InstallDirectory) / "ui" / "Assets" / "AppIcon.ico";
    AppWindow().SetIcon(hstring(iconPath.wstring()));
    SetWindowIdentity();

    auto const workArea = DisplayArea::GetFromWindowId(AppWindow().Id(), DisplayAreaFallback::Primary).WorkArea();
    auto const width = (std::min)(1380, (std::max)(640, workArea.Width - 40));
    auto const height = (std::min)(960, (std::max)(480, workArea.Height - 60));
    AppWindow().Resize({ width, height });
    AppWindow().Move({ workArea.X + (workArea.Width - width) / 2, workArea.Y + (workArea.Height - height) / 2 });

    auto weak = get_weak();
    session_->DocumentChanged([weak]() {
        if (auto self = weak.get()) {
            self->DispatcherQueue().TryEnqueue([weak]() {
                if (auto queued = weak.get()) queued->RenderView();
            });
        }
    });
    session_->StateChanged([weak]() {
        if (auto self = weak.get()) {
            self->DispatcherQueue().TryEnqueue([weak]() {
                if (auto queued = weak.get()) queued->UpdateState();
            });
        }
    });
    session_->Notification([weak](std::string const& message) {
        auto const copy = message;
        if (auto self = weak.get()) {
            self->DispatcherQueue().TryEnqueue([weak, copy]() {
                if (auto queued = weak.get()) queued->ShowNotification(copy);
            });
        }
    });

    appWindow_ = AppWindow();
    appWindowClosingToken_ = appWindow_.Closing({ get_weak(), &MainWindow::AppWindow_Closing });
    closedRevoker_ = Closed(auto_revoke, [weak](IInspectable const&, WindowEventArgs const&) {
        if (auto self = weak.get()) {
            self->ClearWindowIdentity();
            self->session_->StateChanged({});
            self->session_->DocumentChanged({});
            self->session_->Notification({});
        }
    });

    Navigation().SelectedItem(Navigation().MenuItems().GetAt(0));
    RenderView();
}

void MainWindow::Root_Loaded(IInspectable const&, RoutedEventArgs const&)
{
    if (!initialized_) {
        initialized_ = true;
        InitializeAsync();
    }
}

fire_and_forget MainWindow::InitializeAsync()
{
    auto lifetime = get_strong();
    try {
        co_await session_->InitializeAsync();
    }
    catch (std::exception const& error) {
        ShowNotification(error.what());
    }
}

void MainWindow::Navigation_SelectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const& args)
{
    if (navigating_ || !session_) return;
    if (auto item = args.SelectedItem().try_as<NavigationViewItem>()) {
        if (auto tag = item.Tag().try_as<hstring>()) NavigateAsync(*tag);
    }
}

fire_and_forget MainWindow::NavigateAsync(hstring next)
{
    auto lifetime = get_strong();
    auto const version = ++navigationVersion_;
    auto const previous = section_;
    section_ = next;
    SynchronizeNavigation();
    try {
        if (next == L"Loadout" && session_->IsReady() && session_->IsConnected()
            && session_->Preferences().SelectedPreset != pmon::ui::core::Preset::Custom) {
            co_await session_->SelectPresetAsync(pmon::ui::core::Preset::Custom);
        }
        else {
            RenderView();
        }
    }
    catch (std::exception const& error) {
        if (version == navigationVersion_) {
            section_ = previous;
            RenderView();
            SynchronizeNavigation();
        }
        ShowNotification(error.what());
    }
}

void MainWindow::SynchronizeNavigation()
{
    navigating_ = true;
    auto const items = Navigation().MenuItems();
    for (uint32_t index = 0; index < items.Size(); ++index) {
        if (auto item = items.GetAt(index).try_as<NavigationViewItem>()) {
            if (auto tag = item.Tag().try_as<hstring>(); tag && tag == section_) {
                Navigation().SelectedItem(item);
                break;
            }
        }
    }
    navigating_ = false;
    if (Navigation().DisplayMode() != NavigationViewDisplayMode::Expanded) Navigation().IsPaneOpen(false);
    UpdateAppearanceVisibility();
}

void MainWindow::RenderView()
{
    if (!DispatcherQueue().HasThreadAccess()) {
        auto weak = get_weak();
        DispatcherQueue().TryEnqueue([weak]() { if (auto self = weak.get()) self->RenderView(); });
        return;
    }
    if (section_ == L"Loadout" && session_->IsReady() && session_->IsConnected()
        && session_->Preferences().SelectedPreset != pmon::ui::core::Preset::Custom) {
        ++navigationVersion_;
        section_ = L"Overview";
        SynchronizeNavigation();
    }

    processSelector_ = nullptr;
    presetSelector_ = nullptr;
    autoTarget_ = nullptr;
    showOverlay_ = nullptr;
    captureButton_ = nullptr;
    clearTargetButton_ = nullptr;
    hotkeyButtons_.clear();
    ContentHost().Children().Clear();
    loadoutView_.reset();
    settingsView_.reset();

    PageTitle().Text(section_ == L"Data" ? L"Data processing" : section_);
    if (section_ == L"Overview") PageDescription().Text(L"Choose an application, configure the overlay, and capture performance data.");
    else if (section_ == L"Loadout") PageDescription().Text(L"Build your custom overlay with graphs and readouts. Changes are saved automatically.");
    else if (section_ == L"Overlay") PageDescription().Text(L"Control where the overlay appears and how it looks.");
    else if (section_ == L"Data") PageDescription().Text(L"Configure sampling, timing, and device selection.");
    else if (section_ == L"Capture") PageDescription().Text(L"Choose how performance data is recorded.");
    else if (section_ == L"Logging") PageDescription().Text(L"Access diagnostic logs and tracing options.");
    else if (section_ == L"Other") PageDescription().Text(L"Manage shortcuts, targeting, and application preferences.");
    else PageDescription().Text(L"Application, service, and build information.");

    if (section_ == L"Loadout") {
        auto weak = get_weak();
        loadoutView_ = std::make_unique<pmon::ui::views::LoadoutView>(session_,
            [weak]() -> IAsyncAction {
                if (auto self = weak.get()) co_await self->ImportLoadoutAsync();
            },
            [weak]() -> IAsyncAction {
                if (auto self = weak.get()) co_await self->ExportLoadoutAsync();
            });
        loadoutView_->SetEditingEnabled(session_->Preferences().SelectedPreset == pmon::ui::core::Preset::Custom);
        ContentHost().Children().Append(loadoutView_->Element());
    }
    else {
        if (section_ == L"Overview") {
            SetContent(BuildOverview());
        }
        else {
            settingsView_ = std::make_unique<pmon::ui::views::SettingsView>(ToString(section_), session_->Preferences(), session_->Introspection(),
                [session = session_]() { session->Changed(); },
                [weak = get_weak()]() -> IAsyncAction { if (auto self = weak.get()) co_await self->ChooseHotkeyAsync(); },
                [session = session_]() -> IAsyncAction { co_await session->ResetPreferencesAsync(); },
                [session = session_](std::string const& kind) -> IAsyncAction { session->ExploreFolder(kind); co_return; },
                ToAppInfo(session_->AppInfo()), session_->Options().EnableDevOptions);
            SetContent(settingsView_->Element());
        }
    }
    UpdateState();
}

void MainWindow::SetContent(UIElement const& content)
{
    ScrollViewer scroll;
    scroll.Content(content);
    scroll.HorizontalScrollMode(ScrollMode::Disabled);
    scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scroll.Padding({ 0, 0, 12, 20 });
    scroll.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    ContentHost().Children().Append(scroll);
}

UIElement MainWindow::BuildOverview()
{
    auto const& preferences = session_->Preferences();
    StackPanel layout;
    layout.Spacing(12);
    CommandBar commands;
    commands.DefaultLabelPosition(CommandBarDefaultLabelPosition::Right);
    commands.HorizontalContentAlignment(HorizontalAlignment::Left);
    commands.Background(SolidColorBrush(Microsoft::UI::Colors::Transparent()));

    captureButton_ = AppBarButton();
    captureButton_.Label(L"Start capture");
    captureButton_.Icon(SymbolIcon(Symbol::Play));
    Identify(captureButton_, "ToggleCapture", "Start capture");
    captureButton_.Click([weak = get_weak()](IInspectable const&, RoutedEventArgs const&) {
        if (auto self = weak.get()) self->ToggleCaptureAsync();
    });
    commands.PrimaryCommands().Append(captureButton_);

    AppBarButton refresh;
    refresh.Label(L"Refresh processes");
    refresh.Icon(SymbolIcon(Symbol::Refresh));
    Identify(refresh, "RefreshProcesses", "Refresh processes");
    refresh.Click([weak = get_weak()](IInspectable const&, RoutedEventArgs const&) {
        if (auto self = weak.get()) self->RefreshProcessesAsync();
    });
    commands.PrimaryCommands().Append(refresh);

    clearTargetButton_ = AppBarButton();
    clearTargetButton_.Label(L"Clear target");
    clearTargetButton_.Icon(SymbolIcon(Symbol::Clear));
    Identify(clearTargetButton_, "ClearTarget", "Clear target");
    clearTargetButton_.Click([weak = get_weak()](IInspectable const&, RoutedEventArgs const&) {
        if (auto self = weak.get()) self->SelectProcessAsync(std::nullopt);
    });
    commands.PrimaryCommands().Append(clearTargetButton_);
    layout.Children().Append(commands);

    processSelector_ = AutoSuggestBox();
    processSelector_.PlaceholderText(L"Search running applications by name, window, or PID");
    processSelector_.QueryIcon(SymbolIcon(Symbol::Find));
    processSelector_.Text(session_->SelectedProcess() ? ToHString(session_->SelectedProcess()->DisplayName()) : L"");
    processSelector_.HorizontalAlignment(HorizontalAlignment::Stretch);
    Identify(processSelector_, "ProcessSelector", "Target application");
    displayedPid_ = session_->SelectedPid();
    processSelector_.TextChanged([weak = get_weak()](AutoSuggestBox const& box, AutoSuggestBoxTextChangedEventArgs const& args) {
        if (args.Reason() == AutoSuggestionBoxTextChangeReason::UserInput) {
            if (auto self = weak.get()) self->FilterProcesses(box);
        }
    });
    processSelector_.GotFocus([weak = get_weak()](IInspectable const&, RoutedEventArgs const&) {
        if (auto self = weak.get()) self->RefreshProcessesAsync();
    });
    processSelector_.QuerySubmitted([weak = get_weak()](AutoSuggestBox const& box, AutoSuggestBoxQuerySubmittedEventArgs const& args) {
        if (auto self = weak.get()) {
            auto query = args.QueryText();
            if (auto chosen = args.ChosenSuggestion().try_as<hstring>()) query = *chosen;
            auto const selection = self->FindProcess(query);
            if (selection) {
                self->SelectProcessAsync(selection);
                for (auto const& process : self->session_->Processes()) {
                    if (process.Pid == *selection) {
                        box.Text(ToHString(process.DisplayName()));
                        break;
                    }
                }
                box.IsSuggestionListOpen(false);
            }
            else self->ShowNotification("Select an application from the search results.");
        }
    });
    layout.Children().Append(pmon::ui::views::FormControls::Row("Target application",
        "The selected process supplies the overlay and capture data.", processSelector_));

    autoTarget_ = pmon::ui::views::FormControls::Toggle("AutoTarget", "Automatic application selection",
        preferences.EnableAutotargetting, [weak = get_weak()](bool enabled) {
            if (auto self = weak.get(); self && !self->updating_) {
                self->session_->Preferences().EnableAutotargetting = enabled;
                if (enabled) self->SelectProcessAsync(std::nullopt);
                self->session_->Changed();
            }
        });
    layout.Children().Append(pmon::ui::views::FormControls::Row("Automatic targeting",
        "Every second, select the highest-load application whose last 10 raw GPU Busy samples contain a change. Switching targets stops an active capture.", autoTarget_));

    showOverlay_ = pmon::ui::views::FormControls::Toggle("ShowOverlay", "Show overlay", !preferences.HideAlways,
        [weak = get_weak()](bool visible) {
            if (auto self = weak.get(); self && !self->updating_) {
                self->session_->Preferences().HideAlways = !visible;
                self->session_->Changed();
            }
        });
    layout.Children().Append(pmon::ui::views::FormControls::Row("Show overlay",
        "Display the configured widgets over the target application.", showOverlay_));

    layout.Children().Append(pmon::ui::views::FormControls::Heading("Overlay loadout"));
    presetSelector_ = ComboBox();
    for (auto const label : { L"Basic", L"Game experience", L"GPU focus", L"Power and temperature", L"Custom" }) {
        presetSelector_.Items().Append(box_value(hstring(label)));
    }
    auto const selectedPreset = preferences.SelectedPreset.value_or(pmon::ui::core::Preset::Basic);
    presetSelector_.SelectedIndex(selectedPreset == pmon::ui::core::Preset::Custom ? 4 : (int)selectedPreset);
    presetSelector_.HorizontalAlignment(HorizontalAlignment::Stretch);
    Identify(presetSelector_, "PresetSelector", "Overlay preset");
    presetSelector_.SelectionChanged([weak = get_weak()](IInspectable const& sender, SelectionChangedEventArgs const&) {
        if (auto self = weak.get(); self && !self->updating_) self->SelectPresetAsync(sender.as<ComboBox>().SelectedIndex());
    });
    StackPanel presetControls;
    presetControls.Spacing(10);
    presetControls.Children().Append(presetSelector_);
    presetControls.Children().Append(pmon::ui::views::FormControls::AsyncButton("EditLoadout", "Edit custom loadout",
        [weak = get_weak()]() -> IAsyncAction { if (auto self = weak.get()) self->NavigateAsync(L"Loadout"); co_return; }));
    layout.Children().Append(pmon::ui::views::FormControls::Row("Preset",
        "Start with a built-in preset or edit the custom overlay loadout.", presetControls));

    layout.Children().Append(pmon::ui::views::FormControls::Heading("Capture"));
    auto duration = pmon::ui::views::FormControls::Number("CaptureDuration", "Capture duration in seconds",
        preferences.CaptureDuration, 0.1, 86400.0, 1.0, [session = session_](double value) {
            session->Preferences().CaptureDuration = value;
            session->Changed();
        });
    duration.IsEnabled(preferences.EnableCaptureDuration);
    StackPanel durationControls;
    durationControls.Spacing(8);
    durationControls.Children().Append(pmon::ui::views::FormControls::Toggle("EnableCaptureDuration", "Limit capture duration",
        preferences.EnableCaptureDuration, [session = session_, duration](bool enabled) {
            session->Preferences().EnableCaptureDuration = enabled;
            duration.IsEnabled(enabled);
            session->Changed();
        }));
    durationControls.Children().Append(duration);
    layout.Children().Append(pmon::ui::views::FormControls::Row("Capture duration",
        "Stop recording automatically after this many seconds.", durationControls));

    auto capturePath = TextBlock();
    capturePath.Text(ToHString(session_->CaptureDirectory()));
    capturePath.IsTextSelectionEnabled(true);
    capturePath.TextWrapping(TextWrapping::Wrap);
    Identify(capturePath, "CaptureDirectory", "Capture directory: " + session_->CaptureDirectory());
    StackPanel directoryControls;
    directoryControls.Spacing(8);
    directoryControls.Children().Append(capturePath);
    directoryControls.Children().Append(pmon::ui::views::FormControls::AsyncButton("ExploreCaptures", "Open capture folder",
        [session = session_]() -> IAsyncAction { session->ExploreFolder("captures"); co_return; }));
    layout.Children().Append(pmon::ui::views::FormControls::Row("Capture directory",
        "Performance captures are saved in this folder.", directoryControls));

    layout.Children().Append(pmon::ui::views::FormControls::Heading("Hotkeys"));
    for (auto const action : { pmon::ui::core::HotkeyAction::ToggleCapture,
        pmon::ui::core::HotkeyAction::ToggleOverlay, pmon::ui::core::HotkeyAction::CyclePreset }) {
        auto const label = action == pmon::ui::core::HotkeyAction::ToggleCapture ? "Start or stop capture"
            : action == pmon::ui::core::HotkeyAction::ToggleOverlay ? "Show or hide overlay"
            : "Cycle overlay preset";
        auto const id = action == pmon::ui::core::HotkeyAction::ToggleCapture ? "HotkeyToggleCapture"
            : action == pmon::ui::core::HotkeyAction::ToggleOverlay ? "HotkeyToggleOverlay"
            : "HotkeyCyclePreset";
        auto button = pmon::ui::views::FormControls::AsyncButton(id,
            pmon::ui::views::HotkeyEditor::Format(std::optional{ session_->GetBinding(action) }),
            [weak = get_weak(), action]() -> IAsyncAction {
                if (auto self = weak.get()) co_await self->EditHotkeyAsync(action);
            });
        hotkeyButtons_.emplace_back((int)action, button);
        layout.Children().Append(pmon::ui::views::FormControls::Row(label,
            "Select to change or clear this global keyboard shortcut.", button));
    }
    return layout;
}

fire_and_forget MainWindow::RefreshProcessesAsync()
{
    auto lifetime = get_strong();
    if (refreshingProcesses_) co_return;
    refreshingProcesses_ = true;
    try {
        co_await session_->RefreshProcessesAsync();
        if (processSelector_) FilterProcesses(processSelector_);
    }
    catch (std::exception const& error) {
        ShowNotification(error.what());
    }
    refreshingProcesses_ = false;
}

std::optional<int> MainWindow::FindProcess(hstring const& query) const
{
    auto const text = Trim(ToString(query));
    std::vector<pmon::ui::services::ProcessEntry const*> matches;
    for (auto const& process : session_->Processes()) {
        auto const display = process.DisplayName();
        if (text.empty() || ContainsInsensitive(display, text) || ContainsInsensitive(process.WindowName, text)
            || std::to_string(process.Pid).find(text) != std::string::npos) matches.push_back(&process);
    }
    for (auto const* process : matches) if (std::to_string(process->Pid) == text) return process->Pid;
    if (matches.size() == 1) return matches.front()->Pid;
    return std::nullopt;
}

void MainWindow::FilterProcesses(AutoSuggestBox const& selector)
{
    auto const text = Trim(ToString(selector.Text()));
    auto values = single_threaded_vector<IInspectable>();
    for (auto const& process : session_->Processes()) {
        auto const display = process.DisplayName();
        if (text.empty() || ContainsInsensitive(display, text) || ContainsInsensitive(process.WindowName, text)
            || std::to_string(process.Pid).find(text) != std::string::npos) {
            values.Append(box_value(ToHString(display)));
            if (values.Size() == 200) break;
        }
    }
    selector.ItemsSource(values);
}

fire_and_forget MainWindow::SelectProcessAsync(std::optional<int> pid)
{
    auto lifetime = get_strong();
    try { co_await session_->SelectProcessAsync(pid); }
    catch (std::exception const& error) { ShowNotification(error.what()); }
}

fire_and_forget MainWindow::SelectPresetAsync(int index)
{
    auto lifetime = get_strong();
    if (index < 0 || index > 4) co_return;
    try {
        co_await session_->SelectPresetAsync(index == 4 ? pmon::ui::core::Preset::Custom : (pmon::ui::core::Preset)index);
    }
    catch (std::exception const& error) { ShowNotification(error.what()); }
}

fire_and_forget MainWindow::ToggleCaptureAsync()
{
    auto lifetime = get_strong();
    try { co_await session_->ToggleCaptureAsync(); }
    catch (std::exception const& error) { ShowNotification(error.what()); }
}

IAsyncAction MainWindow::ChooseHotkeyAsync()
{
    auto lifetime = get_strong();
    auto const actions = { pmon::ui::core::HotkeyAction::ToggleCapture, pmon::ui::core::HotkeyAction::ToggleOverlay,
        pmon::ui::core::HotkeyAction::CyclePreset };
    ComboBox choice;
    choice.Items().Append(box_value(L"Start or stop capture"));
    choice.Items().Append(box_value(L"Show or hide overlay"));
    choice.Items().Append(box_value(L"Cycle overlay preset"));
    choice.SelectedIndex(0);
    Identify(choice, "HotkeyActionSelector", "Hotkey action");
    ContentDialog dialog;
    dialog.XamlRoot(Root().XamlRoot());
    dialog.RequestedTheme(Root().ActualTheme());
    dialog.Title(box_value(L"Choose a hotkey"));
    dialog.Content(choice);
    dialog.PrimaryButtonText(L"Edit");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Primary);
    if (co_await dialog.ShowAsync() == ContentDialogResult::Primary) {
        auto iterator = actions.begin();
        std::advance(iterator, choice.SelectedIndex());
        co_await EditHotkeyAsync(*iterator);
    }
}

IAsyncAction MainWindow::EditHotkeyAsync(pmon::ui::core::HotkeyAction action)
{
    auto lifetime = get_strong();
    auto session = session_;
    co_await pmon::ui::views::HotkeyEditor::EditAsync(Root().XamlRoot(), session_->GetBinding(action),
        [session](std::optional<pmon::ui::core::HotkeyBinding> binding) -> IAsyncAction {
            if (binding) co_await session->BindHotkeyAsync(*binding);
        });
}

IAsyncAction MainWindow::ImportLoadoutAsync()
{
    auto lifetime = get_strong();
    FileOpenPicker picker;
    picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
    picker.FileTypeFilter().Append(L".json");
    auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
    check_hresult(initializeWithWindow->Initialize(reinterpret_cast<HWND>(windowHandle_)));
    if (auto file = co_await picker.PickSingleFileAsync()) {
        try { co_await session_->ImportLoadoutAsync(ToString(file.Path())); }
        catch (std::exception const& error) { ShowNotification(error.what()); }
    }
}

IAsyncAction MainWindow::ExportLoadoutAsync()
{
    auto lifetime = get_strong();
    FileSavePicker picker;
    picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
    picker.SuggestedFileName(L"PresentMon-loadout");
    auto choices = single_threaded_vector<hstring>();
    choices.Append(L".json");
    picker.FileTypeChoices().Insert(L"PresentMon loadout", choices);
    auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
    check_hresult(initializeWithWindow->Initialize(reinterpret_cast<HWND>(windowHandle_)));
    if (auto file = co_await picker.PickSaveFileAsync()) {
        try { session_->ExportLoadout(ToString(file.Path())); }
        catch (std::exception const& error) { ShowNotification(error.what()); }
    }
}

void MainWindow::UpdateState()
{
    if (!session_ || !Loading() || !Navigation() || !PageContent() || !ConnectionNotice() || !ProcessStatus()
        || !CaptureStatus() || !OverlayStatus() || !RateStatus()) return;
    if (!DispatcherQueue().HasThreadAccess()) {
        auto weak = get_weak();
        DispatcherQueue().TryEnqueue([weak]() { if (auto self = weak.get()) self->UpdateState(); });
        return;
    }
    updating_ = true;
    Loading().IsActive(!session_->IsReady());
    Loading().Visibility(session_->IsReady() ? Visibility::Collapsed : Visibility::Visible);
    auto const menuItems = Navigation().MenuItems();
    for (uint32_t index = 0; index < menuItems.Size(); ++index) {
        if (auto item = menuItems.GetAt(index).try_as<NavigationViewItem>()) item.IsEnabled(session_->IsReady());
    }
    PageContent().IsEnabled(!closing_ && session_->IsReady() && (session_->IsConnected() || section_ == L"About"));
    ConnectionNotice().IsOpen(!session_->IsConnected());
    ConnectionNotice().Title(session_->IsReady() ? L"PresentMon connection" : L"Connecting");
    ConnectionNotice().Message(ToHString(session_->ConnectionStatus()));
    ConnectionNotice().Severity(session_->IsReady() ? InfoBarSeverity::Warning : InfoBarSeverity::Informational);
    if (auto selected = session_->SelectedProcess()) ProcessStatus().Text(ToHString(selected->DisplayName()));
    else if (auto pid = session_->SelectedPid()) ProcessStatus().Text(L"Process " + to_hstring(*pid));
    else ProcessStatus().Text(L"No application selected");
    CaptureStatus().Text(session_->Capturing() ? L"Capture: recording" : L"Capture: idle");
    auto const& preferences = session_->Preferences();
    OverlayStatus().Text(L"Overlay: " + hstring(preferences.HideAlways ? L"hidden" : L"shown")
        + L"  |  Auto-hide: " + hstring(preferences.HideDuringCapture ? L"on" : L"off"));
    RateStatus().Text(L"Poll: " + to_hstring(preferences.MetricPollRate) + L" Hz  |  Draw: "
        + to_hstring(preferences.OverlayDrawRate) + L" FPS");
    if (captureButton_) {
        captureButton_.Label(session_->Capturing() ? L"Stop capture" : L"Start capture");
        captureButton_.Icon(SymbolIcon(session_->Capturing() ? Symbol::Stop : Symbol::Play));
        captureButton_.IsEnabled(session_->IsConnected() && (session_->Capturing() || session_->SelectedPid().has_value()));
    }
    if (clearTargetButton_) clearTargetButton_.IsEnabled(session_->SelectedPid().has_value());
    if (processSelector_ && displayedPid_ != session_->SelectedPid()) {
        processSelector_.Text(session_->SelectedProcess() ? ToHString(session_->SelectedProcess()->DisplayName()) : L"");
        displayedPid_ = session_->SelectedPid();
    }
    if (autoTarget_) autoTarget_.IsOn(preferences.EnableAutotargetting);
    if (showOverlay_) showOverlay_.IsOn(!preferences.HideAlways);
    if (presetSelector_) {
        auto const preset = preferences.SelectedPreset.value_or(pmon::ui::core::Preset::Basic);
        presetSelector_.SelectedIndex(preset == pmon::ui::core::Preset::Custom ? 4 : (int)preset);
    }
    for (auto const& hotkey : hotkeyButtons_) {
        auto const action = (pmon::ui::core::HotkeyAction)hotkey.first;
        auto const shortcut = pmon::ui::views::HotkeyEditor::Format(std::optional{ session_->GetBinding(action) });
        hotkey.second.Content(box_value(ToHString(shortcut)));
        AutomationProperties::SetName(hotkey.second, ToHString(HotkeyActionLabel(action) + ": " + shortcut));
    }
    updating_ = false;
}

void MainWindow::ShowNotification(std::string const& message)
{
    if (auto notice = Notice()) {
        notice.Title(L"PresentMon");
        notice.Message(ToHString(message));
        notice.IsOpen(true);
    }
}

void MainWindow::Navigation_PaneChanged(NavigationView const& sender, IInspectable const&)
{
    if (auto appearance = AppearancePanel()) {
        appearance.Visibility(sender.IsPaneOpen() ? Visibility::Visible : Visibility::Collapsed);
    }
}

void MainWindow::Navigation_DisplayModeChanged(NavigationView const& sender, NavigationViewDisplayModeChangedEventArgs const&)
{
    if (auto appearance = AppearancePanel()) {
        appearance.Visibility(sender.IsPaneOpen() ? Visibility::Visible : Visibility::Collapsed);
    }
}

void MainWindow::UpdateAppearanceVisibility()
{
    auto const appearance = AppearancePanel();
    auto const navigation = Navigation();
    if (!appearance || !navigation) return;
    appearance.Visibility(navigation.IsPaneOpen() ? Visibility::Visible : Visibility::Collapsed);
}

void MainWindow::Appearance_SelectionChanged(IInspectable const& sender, SelectionChangedEventArgs const&)
{
    auto const root = Root();
    auto const selector = sender.try_as<ComboBox>();
    if (!root || !selector) return;
    auto const selected = selector.SelectedIndex();
    root.RequestedTheme(selected == 1 ? ElementTheme::Light : selected == 2 ? ElementTheme::Dark : ElementTheme::Default);
}

void MainWindow::PageLayout_SizeChanged(IInspectable const& sender, SizeChangedEventArgs const& args)
{
    if (auto layout = sender.try_as<Grid>()) {
        layout.Padding(args.NewSize().Width < 720 ? Thickness{ 16, 16, 16, 8 } : Thickness{ 28, 20, 28, 12 });
    }
}

void MainWindow::AppWindow_Closing(Microsoft::UI::Windowing::AppWindow const&, AppWindowClosingEventArgs const& args)
{
    if (canClose_) return;
    args.Cancel(true);
    if (!closing_) {
        closing_ = true;
        if (auto pageContent = PageContent()) pageContent.IsEnabled(false);
        CloseAsync();
    }
}

fire_and_forget MainWindow::CloseAsync()
{
    auto lifetime = get_strong();
    try { co_await session_->CloseAsync(); }
    catch (std::exception const& error) { ShowNotification(std::string("Unable to finish shutdown: ") + error.what()); }
    ClearWindowIdentity();
    canClose_ = true;
    Close();
}

void MainWindow::SetWindowIdentity()
{
    if (windowHandle_) pmon::ui::services::WindowsServices::SetWindowIdentity(windowHandle_, session_->Options().MutexSuffix);
}

DependencyObject MainWindow::FindByAutomationId(DependencyObject const& root, hstring const& id) const
{
    if (!root) return nullptr;
    if (AutomationProperties::GetAutomationId(root) == id) return root;
    auto const count = VisualTreeHelper::GetChildrenCount(root);
    for (int index = 0; index < count; ++index) {
        if (auto found = FindByAutomationId(VisualTreeHelper::GetChild(root, index), id)) return found;
    }
    return nullptr;
}

void MainWindow::InvokeControl(DependencyObject const& control) const
{
    auto element = control.try_as<FrameworkElement>();
    if (!element) {
        throw hresult_error(E_FAIL, L"Shell control is not a framework element.");
    }
    auto peer = FrameworkElementAutomationPeer::CreatePeerForElement(element);
    if (!peer) {
        throw hresult_error(E_FAIL, L"Shell control has no automation peer.");
    }
    auto provider = peer.GetPattern(PatternInterface::Invoke).try_as<IInvokeProvider>();
    if (!provider) {
        throw hresult_error(E_FAIL, L"Shell control does not support Invoke automation.");
    }
    provider.Invoke();
}

void MainWindow::ClearWindowIdentity() noexcept
{
    if (windowHandle_) pmon::ui::services::WindowsServices::ClearWindowIdentity(windowHandle_);
}

}
