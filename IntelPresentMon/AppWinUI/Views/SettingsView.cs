// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.RegularExpressions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using PresentMon.UI.Core;
using Windows.UI;

namespace PresentMon.UI.Views;

public sealed class SettingsView : UserControl
{
    private readonly Preferences preferences;
    private readonly IntrospectionData introspection;
    private readonly Action changed;
    private readonly Func<Task> editHotkeys;
    private readonly Func<Task> resetPreferences;
    private readonly Func<string, Task> exploreFolder;
    private readonly StackPanel layout = new() { Spacing = 12, HorizontalAlignment = HorizontalAlignment.Stretch };

    public SettingsView(string section, Preferences preferences, IntrospectionData introspection,
        Action changed, Func<Task> editHotkeys, Func<Task> resetPreferences,
        Func<string, Task> exploreFolder, IReadOnlyDictionary<string, string> appInfo, bool enableDevOptions)
    {
        this.preferences = preferences;
        this.introspection = introspection;
        this.changed = changed;
        this.editHotkeys = editHotkeys;
        this.resetPreferences = resetPreferences;
        this.exploreFolder = exploreFolder;
        HorizontalAlignment = HorizontalAlignment.Stretch;
        Content = layout;
        FormControls.Identify(this, "Settings" + section, section + " settings");
        switch (section)
        {
            case "Overlay": BuildOverlay(enableDevOptions); break;
            case "Data": BuildData(enableDevOptions); break;
            case "Capture": BuildCapture(); break;
            case "Logging": BuildLogging(); break;
            case "Other": BuildOther(); break;
            case "About": BuildAbout(appInfo); break;
            default: throw new ArgumentException("Unknown settings section: " + section, nameof(section));
        }
    }

    private void BuildOverlay(bool enableDevOptions)
    {
        layout.Children.Add(FormControls.Description("Changes apply to the active overlay and are saved automatically."));
        AddToggle("IndependentWindow", "Windowed mode",
            "Display widgets in a standalone window instead of an overlay following the target application.",
            preferences.IndependentWindow, value => preferences.IndependentWindow = value);
        AddToggle("HideDuringCapture", "Automatic hide",
            "Automatically hide the overlay while a capture is running.",
            preferences.HideDuringCapture, value => preferences.HideDuringCapture = value);
        layout.Children.Add(FormControls.Row("Position", "Choose the corner of the target window for the overlay.", CreatePositionPicker()));
        AddNumber("OverlayWidth", "Width", "Width of the overlay in pixels. Its content determines its height.",
            preferences.OverlayWidth, 200, 1920, 1, value => preferences.OverlayWidth = value);
        AddNumber("TimeRange", "Time scale", "Range of time shown on graph axes, in seconds. This controls scrolling speed.",
            preferences.TimeRange, 0.1, 10, 0.1, value => preferences.TimeRange = value);

        var scale = FormControls.Number("UpscaleFactor", "Graphics scaling factor", preferences.UpscaleFactor, 1, 5, 0.1,
            value => Update(() => preferences.UpscaleFactor = value));
        scale.IsEnabled = preferences.Upscale;
        var scaleControls = new StackPanel { Spacing = 8 };
        scaleControls.Children.Add(FormControls.Toggle("Upscale", "Graphics scaling", preferences.Upscale, value =>
        {
            scale.IsEnabled = value;
            Update(() => preferences.Upscale = value);
        }));
        scaleControls.Children.Add(scale);
        layout.Children.Add(FormControls.Row("Graphics scaling", "Enlarge overlay graphics for readability on high DPI displays. Factor ranges from 1 to 5.", scaleControls));
        AddNumber("OverlayDrawRate", "Draw rate", "Number of times the overlay is drawn per second (FPS).",
            preferences.OverlayDrawRate, 1, 120, 1, value => preferences.OverlayDrawRate = value);
        AddColor(layout, "OverlayBackgroundColor", "Background color", "Background color and opacity of the entire overlay.",
            preferences.OverlayBackgroundColor, value => preferences.OverlayBackgroundColor = value);

        if (!enableDevOptions) return;
        var advanced = new StackPanel { Spacing = 12 };
        AddNumber(advanced, "OverlayMargin", "Overlay margin", "Outer spacing around the overlay, in pixels.",
            preferences.OverlayMargin, 0, 100, 1, value => preferences.OverlayMargin = value);
        AddNumber(advanced, "OverlayBorder", "Overlay border", "Thickness of the overlay border, in pixels.",
            preferences.OverlayBorder, 0, 20, 1, value => preferences.OverlayBorder = value);
        AddNumber(advanced, "OverlayPadding", "Overlay padding", "Space between the overlay edge and its widgets, in pixels.",
            preferences.OverlayPadding, 0, 100, 1, value => preferences.OverlayPadding = value);
        AddColor(advanced, "OverlayBorderColor", "Overlay border color", "Color and opacity of the overlay border.",
            preferences.OverlayBorderColor, value => preferences.OverlayBorderColor = value);
        AddNumber(advanced, "GraphMargin", "Graph margin", "Outer spacing around each graph, in pixels.",
            preferences.GraphMargin, 0, 100, 1, value => preferences.GraphMargin = value);
        AddNumber(advanced, "GraphBorder", "Graph border", "Thickness of graph borders, in pixels.",
            preferences.GraphBorder, 0, 20, 1, value => preferences.GraphBorder = value);
        AddNumber(advanced, "GraphPadding", "Graph padding", "Space inside each graph, in pixels.",
            preferences.GraphPadding, 0, 100, 1, value => preferences.GraphPadding = value);
        var font = FormControls.Text("GraphFontName", "Graph axis font", preferences.GraphFont.Name, value =>
        {
            if (!string.IsNullOrWhiteSpace(value)) Update(() => preferences.GraphFont.Name = value.Trim());
        });
        font.LostFocus += (_, _) => font.Text = preferences.GraphFont.Name;
        advanced.Children.Add(FormControls.Row("Graph axis font", "Font family for graph axis labels. A font name is required.", font));
        AddNumber(advanced, "GraphAxisSize", "Graph axis text size", "Size of graph axis labels, in pixels.",
            preferences.GraphFont.AxisSize, 6, 48, 0.5, value => preferences.GraphFont.AxisSize = value);
        AddExpander("AdvancedOverlayLayout", "Advanced layout", "Developer settings for overlay spacing and typography", advanced);
    }

    private UIElement CreatePositionPicker()
    {
        var position = new Grid { ColumnSpacing = 8, RowSpacing = 8 };
        position.ColumnDefinitions.Add(new() { Width = new GridLength(1, GridUnitType.Star) });
        position.ColumnDefinitions.Add(new() { Width = new GridLength(1, GridUnitType.Star) });
        position.RowDefinitions.Add(new() { Height = GridLength.Auto });
        position.RowDefinitions.Add(new() { Height = GridLength.Auto });
        var labels = new[] { "Top left", "Top right", "Bottom left", "Bottom right" };
        var group = "OverlayPosition" + Guid.NewGuid().ToString("N");
        foreach (var index in Enumerable.Range(0, labels.Length))
        {
            var option = new RadioButton
            {
                Content = labels[index],
                GroupName = group,
                IsChecked = (int)preferences.OverlayPosition == index,
                HorizontalAlignment = HorizontalAlignment.Stretch,
            };
            FormControls.Identify(option, "OverlayPosition" + index, "Overlay position: " + labels[index]);
            option.Checked += (_, _) => Update(() => preferences.OverlayPosition = (OverlayPosition)index);
            Grid.SetRow(option, index / 2);
            Grid.SetColumn(option, index % 2);
            position.Children.Add(option);
        }
        return position;
    }

    private void BuildData(bool enableDevOptions)
    {
        var pollHint = new InfoBar
        {
            Severity = InfoBarSeverity.Informational,
            IsClosable = false,
            Title = "Polling cadence",
        };
        FormControls.Identify(pollHint, "PollingCadenceHint", "Polling cadence recommendation");
        void RefreshPollHint()
        {
            var drawRate = preferences.OverlayDrawRate;
            var ratio = drawRate > 0 ? preferences.MetricPollRate / drawRate : 0;
            pollHint.IsOpen = drawRate > 0 && Math.Abs(ratio - Math.Round(ratio)) > 0.00001;
            pollHint.Message = "For smoother sampling, use a polling rate that is a whole multiple of the overlay draw rate (currently "
                + drawRate.ToString("0.#", CultureInfo.CurrentCulture) + " FPS).";
        }
        var poll = FormControls.Number("MetricPollRate", "Polling rate in hertz", preferences.MetricPollRate, 1, 240, 1, value =>
        {
            Update(() => preferences.MetricPollRate = value);
            RefreshPollHint();
        });
        layout.Children.Add(FormControls.Row("Polling rate", "How often to request metric data from the API, in hertz. This sets the temporal resolution of graphs and readouts.", poll));
        RefreshPollHint();
        layout.Children.Add(pollHint);
        AddNumber("TelemetrySamplingPeriodMs", "Telemetry period", "Time between service power telemetry requests, in milliseconds. This affects metrics such as GPU power and temperature.",
            preferences.TelemetrySamplingPeriodMs, 1, 500, 1, value => preferences.TelemetrySamplingPeriodMs = value);
        AddNumber("MetricsWindow", "Statistics window", "Sliding sample window for statistics such as averages and percentiles, in milliseconds.",
            preferences.MetricsWindow, 10, 5000, 10, value => preferences.MetricsWindow = value);
        AddToggle("EnablePerMetricDeviceSelection", "Per-metric device selection",
            "Show GPU selectors for individual loadout metrics to track multiple GPUs. When off, every GPU metric uses the default adapter.",
            preferences.EnablePerMetricDeviceSelection, value => preferences.EnablePerMetricDeviceSelection = value);
        var adapters = introspection.Adapters.ToArray();
        var selected = Array.FindIndex(adapters, adapter => adapter.Id == preferences.AdapterId);
        var adapterChoice = FormControls.Choice("DefaultAdapter", "Default graphics adapter", adapters.Select(adapter => adapter.Name).ToArray(), selected,
            index => Update(() => preferences.AdapterId = adapters[index].Id));
        layout.Children.Add(FormControls.Row("Default adapter", adapters.Length == 0
            ? "No graphics adapters are currently available. Connect to the PresentMon service to enumerate devices."
            : "GPU used for new loadout rows, frame queries, and metrics that follow the global adapter.", adapterChoice));

        if (!enableDevOptions) return;
        var advanced = new StackPanel { Spacing = 12 };
        var period = FormControls.Number("EtwFlushPeriod", "ETW manual flush period in milliseconds", preferences.EtwFlushPeriod, 1, 1000, 1,
            value => Update(() => preferences.EtwFlushPeriod = value));
        period.IsEnabled = preferences.ManualEtwFlush;
        advanced.Children.Add(FormControls.Row("Manual ETW flush", "Flush ETW buffers manually instead of relying on the default 1,000 ms timer. A service restart may be required.",
            FormControls.Toggle("ManualEtwFlush", "Manual ETW flush", preferences.ManualEtwFlush, value =>
            {
                period.IsEnabled = value;
                Update(() => preferences.ManualEtwFlush = value);
            })));
        advanced.Children.Add(FormControls.Row("ETW flush period", "Time between manual flushes, in milliseconds. The metric window offset should roughly match this period.", period));
        AddNumber(advanced, "MetricsOffset", "Metric window offset", "Offset the sliding window, in milliseconds, to avoid including frames whose data has not arrived yet.",
            preferences.MetricsOffset, 0, 1500, 1, value => preferences.MetricsOffset = value);
        AddExpander("AdvancedDataSettings", "Advanced data settings", "Developer controls for ETW buffering and metric timing", advanced);
    }

    private void BuildCapture()
    {
        AddToggle("GenerateStats", "Summary statistics", "Generate a file summarizing statistics over the entire capture run.",
            preferences.GenerateStats, value => preferences.GenerateStats = value);
        AddToggle("EnableTargetBlocklist", "Target blocklist", "Filter common applications that do not render real-time graphics out of the target process list.",
            preferences.EnableTargetBlocklist, value => preferences.EnableTargetBlocklist = value);
        layout.Children.Add(FormControls.Row("Capture hotkeys", "Configure keyboard shortcuts for starting a capture, showing the overlay, and cycling presets.",
            FormControls.AsyncButton("EditCaptureHotkeys", "Edit hotkeys", editHotkeys)));
        layout.Children.Add(FormControls.Row("Capture folder", "Open the folder containing captured metric data and summary statistics.",
            FormControls.AsyncButton("ExploreCaptures", "Open in Explorer", () => exploreFolder("captures"))));
    }

    private void BuildLogging()
    {
        layout.Children.Add(new InfoBar
        {
            IsOpen = true,
            IsClosable = false,
            Severity = InfoBarSeverity.Informational,
            Title = "ETL capture is currently disabled",
            Message = "Raw ETL recording and its shortcut are unavailable in this version. Existing trace files remain accessible.",
        });
        var hotkey = new Button { Content = "Unavailable", IsEnabled = false, HorizontalAlignment = HorizontalAlignment.Left };
        FormControls.Identify(hotkey, "EtlCaptureHotkeyDisabled", "ETL capture hotkey is unavailable");
        layout.Children.Add(FormControls.Row("ETL capture hotkey", "Keyboard shortcut for starting or finishing an ETL trace.", hotkey));
        var capture = new Button { Content = "ETL disabled", IsEnabled = false, HorizontalAlignment = HorizontalAlignment.Left };
        FormControls.Identify(capture, "EtlCaptureDisabled", "ETL capture is disabled");
        layout.Children.Add(FormControls.Row("Capture ETL", "Raw ETL capture is currently disabled.", capture));
        layout.Children.Add(FormControls.Row("ETL folder", "Open the folder containing captured .etl trace files.",
            FormControls.AsyncButton("ExploreEtls", "Open in Explorer", () => exploreFolder("etls"))));
        layout.Children.Add(FormControls.Row("Application logs", "Open the folder containing PresentMon diagnostic logs.",
            FormControls.AsyncButton("ExploreLogs", "Open in Explorer", () => exploreFolder("logs"))));
    }

    private void BuildOther()
    {
        layout.Children.Add(FormControls.Row("Keyboard shortcuts", "Customize shortcuts for capture, overlay visibility, and preset selection.",
            FormControls.AsyncButton("EditHotkeys", "Edit hotkeys", editHotkeys)));
        layout.Children.Add(FormControls.Row("Target blocklist", "Open the blocklist file used to filter common applications from the target selector. Enable or disable filtering in Capture settings.",
            FormControls.AsyncButton("ExploreBlocklist", "Open blocklist", () => exploreFolder("blocklist"))));
        layout.Children.Add(FormControls.Row("Reset preferences", "Restore preferences and keyboard shortcuts to their defaults. A confirmation is required before resetting.",
            FormControls.AsyncButton("ResetPreferences", "Reset preferences", ConfirmResetAsync)));
    }

    private async Task ConfirmResetAsync()
    {
        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot,
            Title = "Reset preferences?",
            Content = "All current preferences and keyboard shortcuts will be replaced with their default values.",
            PrimaryButtonText = "Reset",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Close,
        };
        FormControls.Identify(dialog, "ConfirmResetPreferences", "Confirm preference reset");
        if (await dialog.ShowAsync() == ContentDialogResult.Primary) await resetPreferences();
    }

    private void BuildAbout(IReadOnlyDictionary<string, string> appInfo)
    {
        layout.Children.Add(FormControls.Description("PresentMon performance monitoring, powered by a native WinUI 3 interface."));
        var runtime = new TextBlock { Text = "WinUI 3 / Windows App SDK", IsTextSelectionEnabled = true, TextWrapping = TextWrapping.Wrap };
        FormControls.Identify(runtime, "AboutInterface", "Interface: WinUI 3 / Windows App SDK");
        layout.Children.Add(FormControls.Row("Interface", "Native Windows controls with keyboard and screen reader support.", runtime));
        var entries = appInfo.GroupBy(entry => AboutGroup(entry.Key))
            .ToDictionary(group => group.Key, group => group.ToArray());
        foreach (var group in new[] { "Application", "Build", "Service", "Runtime" })
        {
            if (!entries.TryGetValue(group, out var rows)) continue;
            layout.Children.Add(FormControls.Heading(group));
            foreach (var entry in rows)
            {
                var value = new TextBlock
                {
                    Text = string.IsNullOrWhiteSpace(entry.Value) ? "Not available" : entry.Value,
                    IsTextSelectionEnabled = true,
                    TextWrapping = TextWrapping.Wrap,
                };
                var label = entry.Key;
                FormControls.Identify(value, "About" + Regex.Replace(entry.Key, "[^a-zA-Z0-9]", ""), label + ": " + value.Text);
                layout.Children.Add(FormControls.Row(label, "", value));
            }
        }
        if (appInfo.Count == 0)
        {
            layout.Children.Add(new InfoBar
            {
                IsOpen = true,
                IsClosable = false,
                Severity = InfoBarSeverity.Informational,
                Title = "Application information is unavailable",
                Message = "Build and service information will appear once the application has connected.",
            });
        }
    }

    private static string AboutGroup(string key)
    {
        if (key.Contains("service", StringComparison.OrdinalIgnoreCase)) return "Service";
        if (key.Contains("build", StringComparison.OrdinalIgnoreCase) || key.Contains("hash", StringComparison.OrdinalIgnoreCase)) return "Build";
        if (new[] { "runtime", "sdk", "msvc", "crt", "framework", "architecture", "windows" }
            .Any(word => key.Contains(word, StringComparison.OrdinalIgnoreCase))) return "Runtime";
        return "Application";
    }

    private void AddToggle(string id, string title, string description, bool value, Action<bool> setter) =>
        layout.Children.Add(FormControls.Row(title, description, FormControls.Toggle(id, title, value, next => Update(() => setter(next)))));

    private void AddNumber(string id, string title, string description, double value, double minimum,
        double maximum, double step, Action<double> setter) =>
        AddNumber(layout, id, title, description, value, minimum, maximum, step, setter);

    private void AddNumber(StackPanel panel, string id, string title, string description, double value,
        double minimum, double maximum, double step, Action<double> setter) =>
        panel.Children.Add(FormControls.Row(title, description, FormControls.Number(id, title, value, minimum, maximum, step,
            next => Update(() => setter(next)))));

    private void AddColor(StackPanel panel, string id, string title, string description, RgbaColor value, Action<RgbaColor> setter)
    {
        var color = Color.FromArgb((byte)Math.Clamp(Math.Round(value.A * 255), 0, 255),
            (byte)Math.Clamp(value.R, 0, 255), (byte)Math.Clamp(value.G, 0, 255), (byte)Math.Clamp(value.B, 0, 255));
        panel.Children.Add(FormControls.Row(title, description, FormControls.Color(id, title, color,
            next => Update(() => setter(new RgbaColor(next.R, next.G, next.B, next.A / 255.0))))));
    }

    private void AddExpander(string id, string title, string description, UIElement content)
    {
        var header = new StackPanel { Spacing = 4 };
        header.Children.Add(new TextBlock { Text = title, TextWrapping = TextWrapping.Wrap });
        header.Children.Add(FormControls.Description(description));
        var expander = new Expander
        {
            Header = header,
            Content = content,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
        };
        FormControls.Identify(expander, id, title);
        layout.Children.Add(expander);
    }

    private void Update(Action setter)
    {
        setter();
        changed();
    }
}
