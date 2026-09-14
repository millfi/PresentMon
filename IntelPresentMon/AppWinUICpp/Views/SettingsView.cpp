// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT

#include "../pch.h"
#include "SettingsView.h"

#include "../Core/ConfigModels.h"
#include "FormControls.h"

#include <cmath>
#include <cctype>
#include <map>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Foundation;
using namespace Windows::UI;

namespace
{
    hstring H(const std::string& text)
    {
        if (text.empty()) return {};
        auto const length = (int)text.size();
        auto const required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
        if (required == 0) return {};
        std::wstring converted((size_t)required, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, converted.data(), required);
        return hstring{ converted };
    }

    std::string NormalizeKey(std::string text)
    {
        for (auto& character : text)
        {
            if (!std::isalnum((unsigned char)character)) character = '\0';
        }
        text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
        return text;
    }

    std::string Lower(std::string text)
    {
        for (auto& character : text) character = (char)std::tolower((unsigned char)character);
        return text;
    }

    std::string Trim(std::string text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    std::string AboutGroup(const std::string& key)
    {
        auto const lower = Lower(key);
        if (lower.find("service") != std::string::npos) return "Service";
        if (lower.find("build") != std::string::npos || lower.find("hash") != std::string::npos) return "Build";
        for (const auto* word : { "runtime", "sdk", "msvc", "crt", "framework", "architecture", "windows" })
        {
            if (lower.find(word) != std::string::npos) return "Runtime";
        }
        return "Application";
    }

    Color ToColor(const pmon::ui::core::RgbaColor& color)
    {
        return Color{
            (uint8_t)std::clamp(std::round(color.A * 255.0), 0.0, 255.0),
            (uint8_t)std::clamp(color.R, 0, 255),
            (uint8_t)std::clamp(color.G, 0, 255),
            (uint8_t)std::clamp(color.B, 0, 255),
        };
    }

    pmon::ui::core::RgbaColor FromColor(Color color)
    {
        return pmon::ui::core::RgbaColor{ color.R, color.G, color.B, color.A / 255.0 };
    }
}

namespace pmon::ui::views
{
    SettingsView::SettingsView(const std::string& section, core::Preferences& preferences,
        const core::IntrospectionData& introspection, std::function<void()> changed,
        AsyncCallback editHotkeys, AsyncCallback resetPreferences, ExploreFolderCallback exploreFolder,
        AppInfo appInfo, bool enableDevOptions) :
        preferences_(preferences),
        introspection_(introspection),
        changed_(std::move(changed)),
        editHotkeys_(std::move(editHotkeys)),
        resetPreferences_(std::move(resetPreferences)),
        exploreFolder_(std::move(exploreFolder)),
        layout_(StackPanel{})
    {
        layout_.Spacing(12.0);
        layout_.HorizontalAlignment(HorizontalAlignment::Stretch);
        FormControls::Identify(layout_, "Settings" + section, section + " settings");
        if (section == "Overlay") BuildOverlay(enableDevOptions);
        else if (section == "Data") BuildData(enableDevOptions);
        else if (section == "Capture") BuildCapture();
        else if (section == "Logging") BuildLogging();
        else if (section == "Other") BuildOther();
        else if (section == "About") BuildAbout(appInfo);
        else throw hresult_invalid_argument(H("Unknown settings section: " + section));
    }

    UIElement SettingsView::Element() const
    {
        return layout_;
    }

    void SettingsView::BuildOverlay(bool enableDevOptions)
    {
        layout_.Children().Append(FormControls::Description("Changes apply to the active overlay and are saved automatically."));
        AddToggle("IndependentWindow", "Windowed mode",
            "Display widgets in a standalone window instead of an overlay following the target application.",
            preferences_.IndependentWindow, [this](bool value) { preferences_.IndependentWindow = value; });
        AddToggle("HideDuringCapture", "Automatic hide",
            "Automatically hide the overlay while a capture is running.",
            preferences_.HideDuringCapture, [this](bool value) { preferences_.HideDuringCapture = value; });
        layout_.Children().Append(FormControls::Row("Position", "Choose the corner of the target window for the overlay.", CreatePositionPicker()));
        AddNumber("OverlayWidth", "Width", "Width of the overlay in pixels. Its content determines its height.",
            preferences_.OverlayWidth, 200.0, 1920.0, 1.0, [this](double value) { preferences_.OverlayWidth = value; });
        AddNumber("TimeRange", "Time scale", "Range of time shown on graph axes, in seconds. This controls scrolling speed.",
            preferences_.TimeRange, 0.1, 10.0, 0.1, [this](double value) { preferences_.TimeRange = value; });

        auto scale = FormControls::Number("UpscaleFactor", "Graphics scaling factor", preferences_.UpscaleFactor, 1.0, 5.0, 0.1,
            events_.Guard([this](double value) { Update([this, value] { preferences_.UpscaleFactor = value; }); }));
        scale.IsEnabled(preferences_.Upscale);
        auto scaleControls = StackPanel{};
        scaleControls.Spacing(8.0);
        scaleControls.Children().Append(FormControls::Toggle("Upscale", "Graphics scaling", preferences_.Upscale, events_.Guard([this, scale](bool value)
        {
            scale.IsEnabled(value);
            Update([this, value] { preferences_.Upscale = value; });
        })));
        scaleControls.Children().Append(scale);
        layout_.Children().Append(FormControls::Row("Graphics scaling", "Enlarge overlay graphics for readability on high DPI displays. Factor ranges from 1 to 5.", scaleControls));
        AddNumber("OverlayDrawRate", "Draw rate", "Number of times the overlay is drawn per second (FPS).",
            preferences_.OverlayDrawRate, 1.0, 120.0, 1.0, [this](double value) { preferences_.OverlayDrawRate = value; });
        AddColor(layout_, "OverlayBackgroundColor", "Background color", "Background color and opacity of the entire overlay.",
            preferences_.OverlayBackgroundColor, [this](core::RgbaColor value) { preferences_.OverlayBackgroundColor = value; });

        if (!enableDevOptions) return;
        auto advanced = StackPanel{};
        advanced.Spacing(12.0);
        AddNumber(advanced, "OverlayMargin", "Overlay margin", "Outer spacing around the overlay, in pixels.",
            preferences_.OverlayMargin, 0.0, 100.0, 1.0, [this](double value) { preferences_.OverlayMargin = value; });
        AddNumber(advanced, "OverlayBorder", "Overlay border", "Thickness of the overlay border, in pixels.",
            preferences_.OverlayBorder, 0.0, 20.0, 1.0, [this](double value) { preferences_.OverlayBorder = value; });
        AddNumber(advanced, "OverlayPadding", "Overlay padding", "Space between the overlay edge and its widgets, in pixels.",
            preferences_.OverlayPadding, 0.0, 100.0, 1.0, [this](double value) { preferences_.OverlayPadding = value; });
        AddColor(advanced, "OverlayBorderColor", "Overlay border color", "Color and opacity of the overlay border.",
            preferences_.OverlayBorderColor, [this](core::RgbaColor value) { preferences_.OverlayBorderColor = value; });
        AddNumber(advanced, "GraphMargin", "Graph margin", "Outer spacing around each graph, in pixels.",
            preferences_.GraphMargin, 0.0, 100.0, 1.0, [this](double value) { preferences_.GraphMargin = value; });
        AddNumber(advanced, "GraphBorder", "Graph border", "Thickness of graph borders, in pixels.",
            preferences_.GraphBorder, 0.0, 20.0, 1.0, [this](double value) { preferences_.GraphBorder = value; });
        AddNumber(advanced, "GraphPadding", "Graph padding", "Space inside each graph, in pixels.",
            preferences_.GraphPadding, 0.0, 100.0, 1.0, [this](double value) { preferences_.GraphPadding = value; });
        auto font = FormControls::Text("GraphFontName", "Graph axis font", preferences_.GraphFont.Name, events_.Guard([this](const std::string& value)
        {
            const auto trimmed = Trim(value);
            if (!trimmed.empty()) Update([this, trimmed] { preferences_.GraphFont.Name = trimmed; });
        }));
        auto fontWeak = make_weak(font);
        font.LostFocus(events_.Guard([this, fontWeak](const IInspectable&, const RoutedEventArgs&)
        {
            if (auto control = fontWeak.get()) control.Text(H(preferences_.GraphFont.Name));
        }));
        advanced.Children().Append(FormControls::Row("Graph axis font", "Font family for graph axis labels. A font name is required.", font));
        AddNumber(advanced, "GraphAxisSize", "Graph axis text size", "Size of graph axis labels, in pixels.",
            preferences_.GraphFont.AxisSize, 6.0, 48.0, 0.5, [this](double value) { preferences_.GraphFont.AxisSize = value; });
        AddExpander("AdvancedOverlayLayout", "Advanced layout", "Developer settings for overlay spacing and typography", advanced);
    }

    UIElement SettingsView::CreatePositionPicker()
    {
        auto position = Grid{};
        position.ColumnSpacing(8.0);
        position.RowSpacing(8.0);
        auto firstColumn = ColumnDefinition{};
        firstColumn.Width(GridLength{ 1.0, GridUnitType::Star });
        auto secondColumn = ColumnDefinition{};
        secondColumn.Width(GridLength{ 1.0, GridUnitType::Star });
        position.ColumnDefinitions().Append(firstColumn);
        position.ColumnDefinitions().Append(secondColumn);
        position.RowDefinitions().Append(RowDefinition{});
        position.RowDefinitions().Append(RowDefinition{});
        const std::vector<std::string> labels{ "Top left", "Top right", "Bottom left", "Bottom right" };
        auto group = H("OverlayPosition" + std::to_string(GetTickCount64()));
        for (int index = 0; index < (int)labels.size(); ++index)
        {
            auto option = RadioButton{};
            option.Content(box_value(H(labels[(size_t)index])));
            option.GroupName(group);
            option.IsChecked((int)preferences_.OverlayPosition == index);
            option.HorizontalAlignment(HorizontalAlignment::Stretch);
            FormControls::Identify(option, "OverlayPosition" + std::to_string(index), "Overlay position: " + labels[(size_t)index]);
            option.Checked(events_.Guard([this, index](const IInspectable&, const RoutedEventArgs&)
            {
                Update([this, index] { preferences_.OverlayPosition = (core::OverlayPosition)index; });
            }));
            Grid::SetRow(option, index / 2);
            Grid::SetColumn(option, index % 2);
            position.Children().Append(option);
        }
        return position;
    }

    void SettingsView::BuildData(bool enableDevOptions)
    {
        auto pollHint = InfoBar{};
        pollHint.Severity(InfoBarSeverity::Informational);
        pollHint.IsClosable(false);
        pollHint.Title(H("Polling cadence"));
        FormControls::Identify(pollHint, "PollingCadenceHint", "Polling cadence recommendation");
        auto refreshPollHint = [this, pollHint]
        {
            auto const drawRate = preferences_.OverlayDrawRate;
            auto const ratio = drawRate > 0.0 ? preferences_.MetricPollRate / drawRate : 0.0;
            pollHint.IsOpen(drawRate > 0.0 && std::abs(ratio - std::round(ratio)) > 0.00001);
            pollHint.Message(H("For smoother sampling, use a polling rate that is a whole multiple of the overlay draw rate (currently "
                + std::to_string(drawRate) + " FPS)."));
        };
        auto poll = FormControls::Number("MetricPollRate", "Polling rate in hertz", preferences_.MetricPollRate, 1.0, 240.0, 1.0,
            events_.Guard([this, refreshPollHint](double value)
        {
            Update([this, value] { preferences_.MetricPollRate = value; });
            refreshPollHint();
        }));
        layout_.Children().Append(FormControls::Row("Polling rate", "How often to request metric data from the API, in hertz. This sets the temporal resolution of graphs and readouts.", poll));
        refreshPollHint();
        layout_.Children().Append(pollHint);
        AddNumber("TelemetrySamplingPeriodMs", "Telemetry period", "Time between service power telemetry requests, in milliseconds. This affects metrics such as GPU power and temperature.",
            preferences_.TelemetrySamplingPeriodMs, 1.0, 500.0, 1.0, [this](double value) { preferences_.TelemetrySamplingPeriodMs = value; });
        AddNumber("MetricsWindow", "Statistics window", "Sliding sample window for statistics such as averages and percentiles, in milliseconds.",
            preferences_.MetricsWindow, 10.0, 5000.0, 10.0, [this](double value) { preferences_.MetricsWindow = value; });
        AddToggle("EnablePerMetricDeviceSelection", "Per-metric device selection",
            "Show GPU selectors for individual loadout metrics to track multiple GPUs. When off, every GPU metric uses the default adapter.",
            preferences_.EnablePerMetricDeviceSelection, [this](bool value) { preferences_.EnablePerMetricDeviceSelection = value; });
        std::vector<std::string> adapterNames;
        int selected = -1;
        for (int index = 0; index < (int)introspection_.Adapters.size(); ++index)
        {
            auto const& adapter = introspection_.Adapters[(size_t)index];
            adapterNames.push_back(adapter.Name);
            if (adapter.Id == preferences_.AdapterId) selected = index;
        }
        auto adapterChoice = FormControls::Choice("DefaultAdapter", "Default graphics adapter", adapterNames, selected,
            events_.Guard([this](int index) { Update([this, index] { preferences_.AdapterId = introspection_.Adapters[(size_t)index].Id; }); }));
        layout_.Children().Append(FormControls::Row("Default adapter", adapterNames.empty()
            ? "No graphics adapters are currently available. Connect to the PresentMon service to enumerate devices."
            : "GPU used for new loadout rows, frame queries, and metrics that follow the global adapter.", adapterChoice));

        if (!enableDevOptions) return;
        auto advanced = StackPanel{};
        advanced.Spacing(12.0);
        auto period = FormControls::Number("EtwFlushPeriod", "ETW manual flush period in milliseconds", preferences_.EtwFlushPeriod, 1.0, 1000.0, 1.0,
            events_.Guard([this](double value) { Update([this, value] { preferences_.EtwFlushPeriod = value; }); }));
        period.IsEnabled(preferences_.ManualEtwFlush);
        advanced.Children().Append(FormControls::Row("Manual ETW flush", "Flush ETW buffers manually instead of relying on the default 1,000 ms timer. A service restart may be required.",
            FormControls::Toggle("ManualEtwFlush", "Manual ETW flush", preferences_.ManualEtwFlush, events_.Guard([this, period](bool value)
        {
            period.IsEnabled(value);
            Update([this, value] { preferences_.ManualEtwFlush = value; });
        }))));
        advanced.Children().Append(FormControls::Row("ETW flush period", "Time between manual flushes, in milliseconds. The metric window offset should roughly match this period.", period));
        AddNumber(advanced, "MetricsOffset", "Metric window offset", "Offset the sliding window, in milliseconds, to avoid including frames whose data has not arrived yet.",
            preferences_.MetricsOffset, 0.0, 1500.0, 1.0, [this](double value) { preferences_.MetricsOffset = value; });
        AddExpander("AdvancedDataSettings", "Advanced data settings", "Developer controls for ETW buffering and metric timing", advanced);
    }

    void SettingsView::BuildCapture()
    {
        AddToggle("GenerateStats", "Summary statistics", "Generate a file summarizing statistics over the entire capture run.",
            preferences_.GenerateStats, [this](bool value) { preferences_.GenerateStats = value; });
        AddToggle("EnableTargetBlocklist", "Target blocklist", "Filter common applications that do not render real-time graphics out of the target process list.",
            preferences_.EnableTargetBlocklist, [this](bool value) { preferences_.EnableTargetBlocklist = value; });
        layout_.Children().Append(FormControls::Row("Capture hotkeys", "Configure keyboard shortcuts for starting a capture, showing the overlay, and cycling presets.",
            FormControls::AsyncButton("EditCaptureHotkeys", "Edit hotkeys", editHotkeys_)));
        layout_.Children().Append(FormControls::Row("Capture folder", "Open the folder containing captured metric data and summary statistics.",
            FormControls::AsyncButton("ExploreCaptures", "Open in Explorer", [explore = exploreFolder_] { return explore("captures"); })));
    }

    void SettingsView::BuildLogging()
    {
        auto notice = InfoBar{};
        notice.IsOpen(true);
        notice.IsClosable(false);
        notice.Severity(InfoBarSeverity::Informational);
        notice.Title(H("ETL capture is currently disabled"));
        notice.Message(H("Raw ETL recording and its shortcut are unavailable in this version. Existing trace files remain accessible."));
        layout_.Children().Append(notice);
        auto hotkey = Button{};
        hotkey.Content(box_value(H("Unavailable")));
        hotkey.IsEnabled(false);
        hotkey.HorizontalAlignment(HorizontalAlignment::Left);
        FormControls::Identify(hotkey, "EtlCaptureHotkeyDisabled", "ETL capture hotkey is unavailable");
        layout_.Children().Append(FormControls::Row("ETL capture hotkey", "Keyboard shortcut for starting or finishing an ETL trace.", hotkey));
        auto capture = Button{};
        capture.Content(box_value(H("ETL disabled")));
        capture.IsEnabled(false);
        capture.HorizontalAlignment(HorizontalAlignment::Left);
        FormControls::Identify(capture, "EtlCaptureDisabled", "ETL capture is disabled");
        layout_.Children().Append(FormControls::Row("Capture ETL", "Raw ETL capture is currently disabled.", capture));
        layout_.Children().Append(FormControls::Row("ETL folder", "Open the folder containing captured .etl trace files.",
            FormControls::AsyncButton("ExploreEtls", "Open in Explorer", [explore = exploreFolder_] { return explore("etls"); })));
        layout_.Children().Append(FormControls::Row("Application logs", "Open the folder containing PresentMon diagnostic logs.",
            FormControls::AsyncButton("ExploreLogs", "Open in Explorer", [explore = exploreFolder_] { return explore("logs"); })));
    }

    void SettingsView::BuildOther()
    {
        layout_.Children().Append(FormControls::Row("Keyboard shortcuts", "Customize shortcuts for capture, overlay visibility, and preset selection.",
            FormControls::AsyncButton("EditHotkeys", "Edit hotkeys", editHotkeys_)));
        layout_.Children().Append(FormControls::Row("Target blocklist", "Open the blocklist file used to filter common applications from the target selector. Enable or disable filtering in Capture settings.",
            FormControls::AsyncButton("ExploreBlocklist", "Open blocklist", [explore = exploreFolder_] { return explore("blocklist"); })));
        layout_.Children().Append(FormControls::Row("Reset preferences", "Restore preferences and keyboard shortcuts to their defaults. A confirmation is required before resetting.",
            FormControls::AsyncButton("ResetPreferences", "Reset preferences", [layout = make_weak(layout_), reset = resetPreferences_] {
                return ConfirmResetAsync(layout, reset);
            })));
    }

    IAsyncAction SettingsView::ConfirmResetAsync(weak_ref<StackPanel> layout, AsyncCallback resetPreferences)
    {
        auto panel = layout.get();
        if (!panel || !panel.XamlRoot()) co_return;
        auto dialog = ContentDialog{};
        dialog.XamlRoot(panel.XamlRoot());
        dialog.Title(box_value(H("Reset preferences?")));
        dialog.Content(box_value(H("All current preferences and keyboard shortcuts will be replaced with their default values.")));
        dialog.PrimaryButtonText(H("Reset"));
        dialog.CloseButtonText(H("Cancel"));
        dialog.DefaultButton(ContentDialogButton::Close);
        FormControls::Identify(dialog, "ConfirmResetPreferences", "Confirm preference reset");
        if (co_await dialog.ShowAsync() == ContentDialogResult::Primary) co_await resetPreferences();
    }

    void SettingsView::BuildAbout(const AppInfo& appInfo)
    {
        layout_.Children().Append(FormControls::Description("PresentMon performance monitoring, powered by a native WinUI 3 interface."));
        auto runtime = TextBlock{};
        runtime.Text(H("WinUI 3 / Windows App SDK"));
        runtime.IsTextSelectionEnabled(true);
        runtime.TextWrapping(TextWrapping::Wrap);
        FormControls::Identify(runtime, "AboutInterface", "Interface: WinUI 3 / Windows App SDK");
        layout_.Children().Append(FormControls::Row("Interface", "Native Windows controls with keyboard and screen reader support.", runtime));
        std::map<std::string, AppInfo> entries;
        for (const auto& entry : appInfo) entries[AboutGroup(entry.first)].push_back(entry);
        for (const auto& group : { "Application", "Build", "Service", "Runtime" })
        {
            const auto found = entries.find(group);
            if (found == entries.end()) continue;
            layout_.Children().Append(FormControls::Heading(group));
            for (const auto& entry : found->second)
            {
                auto value = TextBlock{};
                value.Text(H(entry.second.empty() ? "Not available" : entry.second));
                value.IsTextSelectionEnabled(true);
                value.TextWrapping(TextWrapping::Wrap);
                FormControls::Identify(value, "About" + NormalizeKey(entry.first), entry.first + ": " + to_string(value.Text()));
                layout_.Children().Append(FormControls::Row(entry.first, "", value));
            }
        }
        if (appInfo.empty())
        {
            auto unavailable = InfoBar{};
            unavailable.IsOpen(true);
            unavailable.IsClosable(false);
            unavailable.Severity(InfoBarSeverity::Informational);
            unavailable.Title(H("Application information is unavailable"));
            unavailable.Message(H("Build and service information will appear once the application has connected."));
            layout_.Children().Append(unavailable);
        }
    }

    void SettingsView::AddToggle(const std::string& id, const std::string& title, const std::string& description,
        bool value, std::function<void(bool)> setter)
    {
        layout_.Children().Append(FormControls::Row(title, description, FormControls::Toggle(id, title, value,
            events_.Guard([this, setter = std::move(setter)](bool next) { Update([setter, next] { setter(next); }); }))));
    }

    void SettingsView::AddNumber(const std::string& id, const std::string& title, const std::string& description,
        double value, double minimum, double maximum, double step, std::function<void(double)> setter)
    {
        AddNumber(layout_, id, title, description, value, minimum, maximum, step, std::move(setter));
    }

    void SettingsView::AddNumber(const StackPanel& panel, const std::string& id, const std::string& title,
        const std::string& description, double value, double minimum, double maximum, double step,
        std::function<void(double)> setter)
    {
        panel.Children().Append(FormControls::Row(title, description, FormControls::Number(id, title, value, minimum, maximum, step,
            events_.Guard([this, setter = std::move(setter)](double next) { Update([setter, next] { setter(next); }); }))));
    }

    void SettingsView::AddColor(const StackPanel& panel, const std::string& id, const std::string& title,
        const std::string& description, core::RgbaColor& value, std::function<void(core::RgbaColor)> setter)
    {
        panel.Children().Append(FormControls::Row(title, description, FormControls::Color(id, title, ToColor(value),
            events_.Guard([this, setter = std::move(setter)](Color next) { Update([setter, next] { setter(FromColor(next)); }); }))));
    }

    void SettingsView::AddExpander(const std::string& id, const std::string& title, const std::string& description,
        const UIElement& content)
    {
        auto header = StackPanel{};
        header.Spacing(4.0);
        auto label = TextBlock{};
        label.Text(H(title));
        label.TextWrapping(TextWrapping::Wrap);
        header.Children().Append(label);
        header.Children().Append(FormControls::Description(description));
        auto expander = Expander{};
        expander.Header(header);
        expander.Content(content);
        expander.HorizontalAlignment(HorizontalAlignment::Stretch);
        expander.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        FormControls::Identify(expander, id, title);
        layout_.Children().Append(expander);
    }

    void SettingsView::Update(std::function<void()> setter)
    {
        setter();
        changed_();
    }
}
