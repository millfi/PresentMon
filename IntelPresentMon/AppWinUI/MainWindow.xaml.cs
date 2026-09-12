// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using PresentMon.UI.Core;
using PresentMon.UI.Services;
using PresentMon.UI.Views;
using Windows.Graphics;
using Windows.Storage.Pickers;

namespace PresentMon.UI;

public sealed partial class MainWindow : Window
{
    private readonly AppSession session;
    private readonly nint windowHandle;
    private readonly Dictionary<HotkeyAction, Button> hotkeyButtons = [];
    private string section = "Overview";
    private bool initialized;
    private bool updating;
    private bool navigating;
    private bool closing;
    private bool canClose;
    private bool refreshingProcesses;
    private int navigationVersion;
    private int? displayedPid;
    private AutoSuggestBox? processSelector;
    private ComboBox? presetSelector;
    private ToggleSwitch? autoTarget;
    private ToggleSwitch? showOverlay;
    private AppBarButton? captureButton;
    private AppBarButton? clearTargetButton;

    private static readonly PresetChoice[] Presets =
    [
        new(Preset.Basic, "Basic"),
        new(Preset.GameExperience, "Game experience"),
        new(Preset.GpuFocus, "GPU focus"),
        new(Preset.PowerTemperature, "Power and temperature"),
        new(Preset.Custom, "Custom"),
    ];

    public MainWindow(StartupOptions options)
    {
        InitializeComponent();
        session = new AppSession(options, DispatcherQueue);
        windowHandle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "AppIcon.ico"));
        WindowsServices.SetWindowIdentity(windowHandle, options.MutexSuffix);
        var workArea = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary).WorkArea;
        var width = Math.Min(1380, Math.Max(640, workArea.Width - 40));
        var height = Math.Min(960, Math.Max(480, workArea.Height - 60));
        AppWindow.Resize(new SizeInt32(width, height));
        AppWindow.Move(new PointInt32(workArea.X + (workArea.Width - width) / 2,
            workArea.Y + (workArea.Height - height) / 2));
        session.DocumentChanged += RenderView;
        session.StateChanged += UpdateState;
        session.Notification += ShowNotification;
        AppWindow.Closing += Window_Closing;
        Closed += (_, _) =>
        {
            WindowsServices.ClearWindowIdentity(windowHandle);
            session.DocumentChanged -= RenderView;
            session.StateChanged -= UpdateState;
            session.Notification -= ShowNotification;
        };
        Navigation.SelectedItem = Navigation.MenuItems.OfType<NavigationViewItem>().First();
        RenderView();
    }

    private async void Root_Loaded(object sender, RoutedEventArgs args)
    {
        if (initialized) return;
        initialized = true;
        await RunAsync(session.InitializeAsync);
    }

    private async void Window_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (canClose) return;
        args.Cancel = true;
        if (closing) return;
        closing = true;
        PageContent.IsEnabled = false;
        try { await session.DisposeAsync(); }
        catch (Exception error) { session.Notify($"Unable to finish shutdown: {error.Message}"); }
        finally
        {
            WindowsServices.ClearWindowIdentity(windowHandle);
            canClose = true;
            Close();
        }
    }

    private async void Navigation_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (navigating || session is null || args.SelectedItem is not NavigationViewItem { Tag: string next }) return;
        await RunAsync(() => NavigateAsync(next));
    }

    private async Task NavigateAsync(string next)
    {
        var version = ++navigationVersion;
        var previous = section;
        section = next;
        SynchronizeNavigation();
        try
        {
            if (next == "Loadout" && session.IsReady && session.IsConnected
                && session.Preferences.SelectedPreset != Preset.Custom)
                await session.SelectPresetAsync(Preset.Custom);
            else
                RenderView();
        }
        catch
        {
            if (version == navigationVersion)
            {
                section = previous;
                RenderView();
                SynchronizeNavigation();
            }
            throw;
        }
    }

    private void SynchronizeNavigation()
    {
        navigating = true;
        Navigation.SelectedItem = Navigation.MenuItems.OfType<NavigationViewItem>()
            .FirstOrDefault(item => (string?)item.Tag == section);
        navigating = false;
        if (Navigation.DisplayMode != NavigationViewDisplayMode.Expanded) Navigation.IsPaneOpen = false;
        UpdateAppearanceVisibility();
    }

    private void RenderView()
    {
        if (!DispatcherQueue.HasThreadAccess)
        {
            DispatcherQueue.TryEnqueue(RenderView);
            return;
        }
        if (section == "Loadout" && session.IsReady && session.IsConnected
            && session.Preferences.SelectedPreset != Preset.Custom)
        {
            ++navigationVersion;
            section = "Overview";
            navigating = true;
            Navigation.SelectedItem = Navigation.MenuItems.OfType<NavigationViewItem>().First();
            navigating = false;
        }
        processSelector = null;
        presetSelector = null;
        autoTarget = null;
        showOverlay = null;
        captureButton = null;
        clearTargetButton = null;
        hotkeyButtons.Clear();
        ContentHost.Children.Clear();
        PageTitle.Text = section == "Data" ? "Data processing" : section;
        PageDescription.Text = section switch
        {
            "Overview" => "Choose an application, configure the overlay, and capture performance data.",
            "Loadout" => "Build your custom overlay with graphs and readouts. Changes are saved automatically.",
            "Overlay" => "Control where the overlay appears and how it looks.",
            "Data" => "Configure sampling, timing, and device selection.",
            "Capture" => "Choose how performance data is recorded.",
            "Logging" => "Access diagnostic logs and tracing options.",
            "Other" => "Manage shortcuts, targeting, and application preferences.",
            _ => "Application, service, and build information.",
        };
        if (section == "Loadout")
        {
            ContentHost.Children.Add(new LoadoutView(session.Widgets, session.Preferences,
                session.Introspection, session.Changed, ImportLoadoutAsync, ExportLoadoutAsync)
            {
                IsEditingEnabled = session.Preferences.SelectedPreset == Preset.Custom,
            });
        }
        else
        {
            UIElement content = section == "Overview" ? BuildOverview()
                : new SettingsView(section, session.Preferences, session.Introspection, session.Changed,
                    ChooseHotkeyAsync, session.ResetPreferencesAsync, session.ExploreFolderAsync,
                    session.AppInfo, session.Options.EnableDevOptions);
            ContentHost.Children.Add(new ScrollViewer
            {
                Content = content,
                HorizontalScrollMode = ScrollMode.Disabled,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Padding = new Thickness(0, 0, 12, 20),
                HorizontalContentAlignment = HorizontalAlignment.Stretch,
            });
        }
        UpdateState();
    }

    private UIElement BuildOverview()
    {
        var preferences = session.Preferences;
        var layout = new StackPanel { Spacing = 12 };
        var commands = new CommandBar
        {
            DefaultLabelPosition = CommandBarDefaultLabelPosition.Right,
            HorizontalContentAlignment = HorizontalAlignment.Left,
            Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent),
        };
        captureButton = Command("ToggleCapture", "Start capture", Symbol.Play, session.ToggleCaptureAsync);
        commands.PrimaryCommands.Add(captureButton);
        commands.PrimaryCommands.Add(Command("RefreshProcesses", "Refresh processes", Symbol.Refresh, RefreshProcessesAsync));
        clearTargetButton = Command("ClearTarget", "Clear target", Symbol.Clear, () => session.SelectProcessAsync(null));
        commands.PrimaryCommands.Add(clearTargetButton);
        layout.Children.Add(commands);

        processSelector = new AutoSuggestBox
        {
            PlaceholderText = "Search running applications by name, window, or PID",
            QueryIcon = new SymbolIcon(Symbol.Find),
            DisplayMemberPath = nameof(ProcessEntry.DisplayName),
            TextMemberPath = nameof(ProcessEntry.DisplayName),
            Text = session.SelectedProcess?.DisplayName ?? "",
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        var selector = processSelector;
        AutomationProperties.SetAutomationId(selector, "ProcessSelector");
        AutomationProperties.SetName(selector, "Target application");
        displayedPid = session.SelectedPid;
        selector.TextChanged += (_, args) =>
        {
            if (args.Reason == AutoSuggestionBoxTextChangeReason.UserInput) FilterProcesses(selector);
        };
        selector.GotFocus += async (_, _) => await RunAsync(RefreshProcessesAsync);
        selector.QuerySubmitted += async (_, args) => await RunAsync(async () =>
        {
            var selected = args.ChosenSuggestion as ProcessEntry;
            if (selected is null)
            {
                var matches = MatchingProcesses(args.QueryText).ToArray();
                selected = matches.FirstOrDefault(process => process.Pid.ToString() == args.QueryText.Trim())
                    ?? (matches.Length == 1 ? matches[0] : null);
            }
            if (selected is null)
            {
                session.Notify("Select an application from the search results.");
                return;
            }
            await session.SelectProcessAsync(selected.Pid);
            selector.Text = selected.DisplayName;
            selector.IsSuggestionListOpen = false;
        });
        layout.Children.Add(FormControls.Row("Target application", "The selected process supplies the overlay and capture data.", selector));
        autoTarget = FormControls.Toggle("AutoTarget", "Automatic application selection", preferences.EnableAutotargetting,
            async value =>
            {
                if (updating) return;
                await RunAsync(async () =>
                {
                    preferences.EnableAutotargetting = value;
                    if (value) await session.SelectProcessAsync(null);
                    session.Changed();
                });
            });
        layout.Children.Add(FormControls.Row("Automatic targeting", "Every second, select the highest-load application whose last 10 raw GPU Busy samples contain a change. Switching targets stops an active capture.", autoTarget));
        showOverlay = FormControls.Toggle("ShowOverlay", "Show overlay", !preferences.HideAlways, value =>
        {
            if (updating) return;
            preferences.HideAlways = !value;
            session.Changed();
        });
        layout.Children.Add(FormControls.Row("Show overlay", "Display the configured widgets over the target application.", showOverlay));

        layout.Children.Add(FormControls.Heading("Overlay loadout"));
        presetSelector = new ComboBox
        {
            ItemsSource = Presets,
            DisplayMemberPath = nameof(PresetChoice.Label),
            SelectedItem = Presets.FirstOrDefault(preset => preset.Value == (preferences.SelectedPreset ?? Preset.Basic)),
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        AutomationProperties.SetAutomationId(presetSelector, "PresetSelector");
        AutomationProperties.SetName(presetSelector, "Overlay preset");
        presetSelector.SelectionChanged += async (sender, _) =>
        {
            if (updating || ((ComboBox)sender).SelectedItem is not PresetChoice preset) return;
            await RunAsync(() => session.SelectPresetAsync(preset.Value));
        };
        var presetControls = new StackPanel { Spacing = 10 };
        presetControls.Children.Add(presetSelector);
        presetControls.Children.Add(FormControls.AsyncButton("EditLoadout", "Edit custom loadout", () => RunAsync(() => NavigateAsync("Loadout"))));
        layout.Children.Add(FormControls.Row("Preset", "Start with a built-in preset or edit the custom overlay loadout.", presetControls));

        layout.Children.Add(FormControls.Heading("Capture"));
        var duration = FormControls.Number("CaptureDuration", "Capture duration in seconds", preferences.CaptureDuration,
            0.1, 86400, 1, value =>
            {
                preferences.CaptureDuration = value;
                session.Changed();
            });
        duration.IsEnabled = preferences.EnableCaptureDuration;
        var durationControls = new StackPanel { Spacing = 8 };
        durationControls.Children.Add(FormControls.Toggle("EnableCaptureDuration", "Limit capture duration",
            preferences.EnableCaptureDuration, value =>
            {
                preferences.EnableCaptureDuration = value;
                duration.IsEnabled = value;
                session.Changed();
            }));
        durationControls.Children.Add(duration);
        layout.Children.Add(FormControls.Row("Capture duration", "Stop recording automatically after this many seconds.", durationControls));
        var capturePath = new TextBlock
        {
            Text = session.CaptureDirectory,
            IsTextSelectionEnabled = true,
            TextWrapping = TextWrapping.Wrap,
        };
        AutomationProperties.SetAutomationId(capturePath, "CaptureDirectory");
        AutomationProperties.SetName(capturePath, "Capture directory: " + session.CaptureDirectory);
        var directoryControls = new StackPanel { Spacing = 8 };
        directoryControls.Children.Add(capturePath);
        directoryControls.Children.Add(FormControls.AsyncButton("ExploreCaptures", "Open capture folder",
            () => RunAsync(() => session.ExploreFolderAsync("captures"))));
        layout.Children.Add(FormControls.Row("Capture directory", "Performance captures are saved in this folder.", directoryControls));

        layout.Children.Add(FormControls.Heading("Hotkeys"));
        foreach (var action in Enum.GetValues<HotkeyAction>().Where(action => action != HotkeyAction.ToggleEtlLogging))
        {
            var button = FormControls.AsyncButton("Hotkey" + action, HotkeyEditor.Format(session.GetBinding(action)),
                () => RunAsync(() => EditHotkeyAsync(action)));
            hotkeyButtons[action] = button;
            layout.Children.Add(FormControls.Row(ActionLabel(action), "Select to change or clear this global keyboard shortcut.", button));
        }
        return layout;
    }

    private AppBarButton Command(string id, string label, Symbol icon, Func<Task> action)
    {
        var button = new AppBarButton { Label = label, Icon = new SymbolIcon(icon) };
        AutomationProperties.SetAutomationId(button, id);
        button.Click += async (_, _) =>
        {
            button.IsEnabled = false;
            await RunAsync(action);
            button.IsEnabled = true;
            UpdateState();
        };
        return button;
    }

    private async Task RefreshProcessesAsync()
    {
        if (refreshingProcesses) return;
        refreshingProcesses = true;
        try
        {
            await session.RefreshProcessesAsync();
            if (processSelector is not null) FilterProcesses(processSelector);
        }
        finally { refreshingProcesses = false; }
    }

    private IEnumerable<ProcessEntry> MatchingProcesses(string text)
    {
        var search = text.Trim();
        return session.Processes.Where(process => search.Length == 0
            || process.DisplayName.Contains(search, StringComparison.OrdinalIgnoreCase)
            || process.WindowName.Contains(search, StringComparison.OrdinalIgnoreCase)
            || process.Pid.ToString().Contains(search, StringComparison.OrdinalIgnoreCase));
    }

    private void FilterProcesses(AutoSuggestBox selector) => selector.ItemsSource = MatchingProcesses(selector.Text).Take(200).ToArray();

    private async Task ChooseHotkeyAsync()
    {
        var actions = Enum.GetValues<HotkeyAction>().Where(action => action != HotkeyAction.ToggleEtlLogging).ToArray();
        var choice = new ComboBox { HorizontalAlignment = HorizontalAlignment.Stretch };
        foreach (var action in actions) choice.Items.Add(ActionLabel(action));
        choice.SelectedIndex = 0;
        AutomationProperties.SetAutomationId(choice, "HotkeyActionSelector");
        var dialog = new ContentDialog
        {
            XamlRoot = Root.XamlRoot,
            RequestedTheme = Root.ActualTheme,
            Title = "Choose a hotkey",
            Content = choice,
            PrimaryButtonText = "Edit",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
        };
        if (await dialog.ShowAsync() == ContentDialogResult.Primary)
            await EditHotkeyAsync(actions[choice.SelectedIndex]);
    }

    private async Task EditHotkeyAsync(HotkeyAction action)
    {
        var binding = await HotkeyEditor.EditAsync(Root.XamlRoot, session.GetBinding(action));
        if (binding is not null) await session.BindHotkeyAsync(binding);
    }

    private async Task ImportLoadoutAsync()
    {
        var picker = new FileOpenPicker { SuggestedStartLocation = PickerLocationId.DocumentsLibrary };
        picker.FileTypeFilter.Add(".json");
        WinRT.Interop.InitializeWithWindow.Initialize(picker, windowHandle);
        var file = await picker.PickSingleFileAsync();
        if (file is not null) await session.ImportLoadoutAsync(file.Path);
    }

    private async Task ExportLoadoutAsync()
    {
        var picker = new FileSavePicker
        {
            SuggestedStartLocation = PickerLocationId.DocumentsLibrary,
            SuggestedFileName = "PresentMon-loadout",
        };
        picker.FileTypeChoices.Add("PresentMon loadout", new List<string> { ".json" });
        WinRT.Interop.InitializeWithWindow.Initialize(picker, windowHandle);
        var file = await picker.PickSaveFileAsync();
        if (file is not null) session.ExportLoadout(file.Path);
    }

    private void UpdateState()
    {
        if (!DispatcherQueue.HasThreadAccess)
        {
            DispatcherQueue.TryEnqueue(UpdateState);
            return;
        }
        updating = true;
        try
        {
            Loading.IsActive = !session.IsReady;
            Loading.Visibility = session.IsReady ? Visibility.Collapsed : Visibility.Visible;
            foreach (var item in Navigation.MenuItems.OfType<NavigationViewItem>()) item.IsEnabled = session.IsReady;
            PageContent.IsEnabled = !closing && session.IsReady && (session.IsConnected || section == "About");
            ConnectionNotice.IsOpen = !session.IsConnected;
            ConnectionNotice.Title = session.IsReady ? "PresentMon connection" : "Connecting";
            ConnectionNotice.Message = session.ConnectionStatus;
            ConnectionNotice.Severity = session.IsReady ? InfoBarSeverity.Warning : InfoBarSeverity.Informational;
            ProcessStatus.Text = session.SelectedProcess?.DisplayName
                ?? (session.SelectedPid is int pid ? $"Process {pid}" : "No application selected");
            CaptureStatus.Text = session.Capturing ? "Capture: recording" : "Capture: idle";
            OverlayStatus.Text = $"Overlay: {(session.Preferences.HideAlways ? "hidden" : "shown")}  |  Auto-hide: {(session.Preferences.HideDuringCapture ? "on" : "off")}";
            RateStatus.Text = $"Poll: {session.Preferences.MetricPollRate:g} Hz  |  Draw: {session.Preferences.OverlayDrawRate:g} FPS";
            if (captureButton is not null)
            {
                captureButton.Label = session.Capturing ? "Stop capture" : "Start capture";
                captureButton.Icon = new SymbolIcon(session.Capturing ? Symbol.Stop : Symbol.Play);
                captureButton.IsEnabled = session.IsConnected && (session.Capturing || session.SelectedPid is not null);
            }
            if (clearTargetButton is not null) clearTargetButton.IsEnabled = session.SelectedPid is not null;
            if (autoTarget is not null) autoTarget.IsOn = session.Preferences.EnableAutotargetting;
            if (showOverlay is not null) showOverlay.IsOn = !session.Preferences.HideAlways;
            if (presetSelector is not null)
                presetSelector.SelectedItem = Presets.FirstOrDefault(preset => preset.Value == (session.Preferences.SelectedPreset ?? Preset.Basic));
            if (processSelector is not null && displayedPid != session.SelectedPid)
            {
                displayedPid = session.SelectedPid;
                processSelector.Text = session.SelectedProcess?.DisplayName ?? "";
            }
            foreach (var entry in hotkeyButtons)
            {
                var shortcut = HotkeyEditor.Format(session.GetBinding(entry.Key));
                entry.Value.Content = shortcut;
                AutomationProperties.SetName(entry.Value, $"{ActionLabel(entry.Key)}: {shortcut}");
            }
        }
        finally { updating = false; }
    }

    private async Task RunAsync(Func<Task> action)
    {
        try { await action(); }
        catch (OperationCanceledException) when (closing) { }
        catch (Exception error) { session.Notify(error.Message); }
        finally { if (!closing) UpdateState(); }
    }

    private void ShowNotification(string message)
    {
        if (!DispatcherQueue.HasThreadAccess)
        {
            DispatcherQueue.TryEnqueue(() => ShowNotification(message));
            return;
        }
        Notice.Title = "PresentMon";
        Notice.Message = message;
        Notice.IsOpen = true;
    }

    private void Appearance_SelectionChanged(object sender, SelectionChangedEventArgs args)
    {
        if (Root is null) return;
        Root.RequestedTheme = ((ComboBox)sender).SelectedIndex switch
        {
            1 => ElementTheme.Light,
            2 => ElementTheme.Dark,
            _ => ElementTheme.Default,
        };
    }

    private void Navigation_PaneChanged(NavigationView sender, object args) => UpdateAppearanceVisibility();

    private void Navigation_DisplayModeChanged(NavigationView sender, NavigationViewDisplayModeChangedEventArgs args)
        => UpdateAppearanceVisibility();

    private void UpdateAppearanceVisibility()
    {
        if (AppearancePanel is not null)
            AppearancePanel.Visibility = Navigation.IsPaneOpen ? Visibility.Visible : Visibility.Collapsed;
    }

    private void PageLayout_SizeChanged(object sender, SizeChangedEventArgs args)
    {
        PageLayout.Padding = args.NewSize.Width < 720 ? new Thickness(16, 16, 16, 8) : new Thickness(28, 20, 28, 12);
    }

    private static string ActionLabel(HotkeyAction action) => action switch
    {
        HotkeyAction.ToggleCapture => "Start or stop capture",
        HotkeyAction.ToggleOverlay => "Show or hide overlay",
        HotkeyAction.CyclePreset => "Cycle overlay preset",
        _ => "Toggle ETL logging",
    };

    private sealed record PresetChoice(Preset Value, string Label);
}
