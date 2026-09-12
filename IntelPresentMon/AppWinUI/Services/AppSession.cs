using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using Microsoft.UI.Dispatching;
using PresentMon.UI.Core;
using PresentMon.UI.Core.Interop;

namespace PresentMon.UI.Services;

public sealed class AppSession : IAsyncDisposable
{
    private readonly DispatcherQueue dispatcher;
    private readonly CancellationTokenSource lifetime = new();
    private readonly SemaphoreSlim updates = new(1, 1);
    private readonly SemaphoreSlim captureChanges = new(1, 1);
    private readonly ConfigurationStore store;
    private readonly HashSet<string> blocklist = new(StringComparer.OrdinalIgnoreCase);
    private KernelClient? kernel;
    private ObjectChangeTracker? preferencesTracker;
    private ObjectChangeTracker? widgetsTracker;
    private CancellationTokenSource? pendingSave;
    private CancellationTokenSource? captureStop;
    private bool loading;
    private bool dirty;
    private long revision;
    private bool disposed;
    private Preset? loadedPreset;
    private int? selectedPid;
    private long targetRevision;
    private string? blocklistPath;

    public AppSession(StartupOptions options, DispatcherQueue dispatcher)
    {
        Options = options;
        this.dispatcher = dispatcher;
        store = new(options.DataDirectory, Path.Combine(options.InstallDirectory, "Presets"));
    }

    public StartupOptions Options { get; }
    public PreferenceFile PreferenceFile { get; private set; } = new();
    public Preferences Preferences => PreferenceFile.Preferences;
    public IntrospectionData Introspection { get; private set; } = new();
    public ObservableCollection<Widget> Widgets { get; private set; } = [];
    public IReadOnlyList<ProcessEntry> Processes { get; private set; } = [];
    public IReadOnlyDictionary<string, string> AppInfo => appInfo;
    private readonly Dictionary<string, string> appInfo = new()
    {
        ["Application"] = "Intel PresentMon",
        ["User interface"] = "WinUI 3 / Windows App SDK",
        [".NET runtime"] = Environment.Version.ToString(),
        ["Architecture"] = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
    };
    public bool IsConnected { get; private set; }
    public bool IsReady { get; private set; }
    public bool Capturing { get; private set; }
    public string ConnectionStatus { get; private set; } = "Connecting to PresentMon...";
    public int? SelectedPid => selectedPid;
    public ProcessEntry? SelectedProcess => Processes.FirstOrDefault(p => p.Pid == selectedPid);
    // CapturePath in the legacy preference schema is currently ignored by the kernel.
    // The UI-only data-directory override must not redirect the displayed capture folder.
    public string CaptureDirectory => Path.Combine(
        Options.FilesWorking ? Environment.CurrentDirectory : ConfigurationStore.DefaultDataDirectory, "Captures");

    public event Action? StateChanged;
    public event Action? DocumentChanged;
    public event Action<string>? Notification;

    public async Task InitializeAsync()
    {
        LoadBlocklist();
        loading = true;
        try
        {
            if (!string.IsNullOrWhiteSpace(Options.PipeName))
            {
                kernel = await KernelClient.ConnectAsync(Options.PipeName, lifetime.Token);
                Introspection = await kernel.IntrospectAsync(lifetime.Token);
                IsConnected = true;
                ConnectionStatus = "Connected";
                appInfo["Kernel connection"] = Options.PipeName;
                appInfo["Service version"] = kernel.Session.ServiceVersion;
                appInfo["Service build"] = kernel.Session.ServiceBuildId;
                appInfo["Service build time"] = kernel.Session.ServiceBuildTime;
                appInfo["Middleware API"] = kernel.Session.MiddlewareApiVersion;
                _ = ReceiveEventsAsync();
            }
            else
            {
                ConnectionStatus = "Not connected. Start PresentMon.exe to enable tracking and capture.";
            }
            PreferenceFile = store.LoadPreferences(Introspection);
            if (!string.IsNullOrEmpty(store.LastWarning)) Notify(store.LastWarning);
            LoadSelectedPreset();
            await RefreshProcessesAsync();
            if (kernel is not null)
            {
                foreach (var binding in PreferenceFile.HotkeyBindings.Values)
                {
                    try { await ApplyBindingAsync(binding); }
                    catch (Exception ex) { Notify($"Unable to bind {binding.Action}: {ex.Message}"); }
                }
                await PushAsync();
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !lifetime.IsCancellationRequested)
        {
            IsConnected = false;
            ConnectionStatus = "Unable to connect to PresentMon";
            Notify($"{ex.Message} Start the matching PresentMon.exe build with PresentMon Service running.");
        }
        finally
        {
            loading = false;
            IsReady = true;
            AttachTrackers();
            DocumentChanged?.Invoke();
            StateChanged?.Invoke();
        }
        if (IsConnected) _ = PollProcessesAsync();
    }

    public async Task RefreshProcessesAsync()
    {
        Processes = await WindowsServices.EnumerateProcessesAsync(
            Preferences.EnableTargetBlocklist ? blocklist : null, lifetime.Token);
        StateChanged?.Invoke();
    }

    public async Task SelectProcessAsync(int? pid, Func<bool>? isCurrent = null)
    {
        if (isCurrent is not null && !isCurrent()) return;
        if (pid == selectedPid) return;
        if (Capturing) await SetCaptureAsync(false);
        if (isCurrent is not null && !isCurrent()) return;
        var previous = selectedPid;
        selectedPid = pid;
        targetRevision++;
        try { await PushAsync(); }
        catch { selectedPid = previous; targetRevision++; throw; }
        StateChanged?.Invoke();
    }

    public async Task SelectPresetAsync(Preset preset)
    {
        if (Preferences.SelectedPreset == preset && loadedPreset == preset) return;
        if (Preferences.SelectedPreset == Preset.Custom && dirty) store.SaveCustom(new() { Widgets = Widgets });
        loading = true;
        try
        {
            Preferences.SelectedPreset = preset;
            LoadSelectedPreset();
        }
        finally { loading = false; }
        AttachTrackers();
        DocumentChanged?.Invoke();
        Changed();
        await FlushAsync();
    }

    private void LoadSelectedPreset()
    {
        var preset = Preferences.SelectedPreset ?? Preset.Basic;
        var document = preset == Preset.Custom
            ? store.LoadCustom(Introspection, Preferences)
            : store.LoadPreset((int)preset, Introspection, Preferences);
        Widgets = document.Widgets;
        loadedPreset = preset;
        if (!string.IsNullOrWhiteSpace(store.LastWarning)) Notify(store.LastWarning);
    }

    private void AttachTrackers()
    {
        preferencesTracker?.Dispose();
        widgetsTracker?.Dispose();
        preferencesTracker = new(Preferences, Changed);
        widgetsTracker = new(Widgets, Changed);
    }

    public void Changed()
    {
        if (loading || disposed || !IsConnected) return;
        dirty = true;
        revision++;
        pendingSave?.Cancel();
        pendingSave?.Dispose();
        pendingSave = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        _ = SaveAfterDelayAsync(pendingSave.Token);
        StateChanged?.Invoke();
    }

    private async Task SaveAfterDelayAsync(CancellationToken token)
    {
        try
        {
            await Task.Delay(400, token);
            await FlushAsync();
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { Notify($"Unable to apply settings: {ex.Message}"); }
    }

    public async Task FlushAsync()
    {
        await updates.WaitAsync(lifetime.Token);
        try
        {
            if (!dirty) return;
            var savedRevision = revision;
            var preferencesSnapshot = ConfigurationJson.Clone(PreferenceFile);
            var loadoutSnapshot = LoadoutDocument.Parse(LoadoutDocument.Serialize(new() { Widgets = Widgets }), Introspection, preferencesSnapshot.Preferences);
            if (kernel is not null && IsConnected)
                await kernel.PushSpecificationAsync(SpecificationBuilder.Build(selectedPid,
                    preferencesSnapshot.Preferences, loadoutSnapshot.Widgets, Introspection), lifetime.Token);
            store.SavePreferences(preferencesSnapshot);
            if (preferencesSnapshot.Preferences.SelectedPreset == Preset.Custom) store.SaveCustom(loadoutSnapshot);
            dirty = revision != savedRevision;
        }
        finally { updates.Release(); }
    }

    private async Task PushAsync()
    {
        if (kernel is null || !IsConnected) return;
        var specification = SpecificationBuilder.Build(selectedPid, Preferences, Widgets, Introspection);
        await kernel.PushSpecificationAsync(specification, lifetime.Token);
    }

    public async Task ToggleCaptureAsync()
    {
        await captureChanges.WaitAsync(lifetime.Token);
        try { await WriteCaptureAsync(!Capturing); }
        finally { captureChanges.Release(); }
    }

    public async Task SetCaptureAsync(bool active)
    {
        await captureChanges.WaitAsync(lifetime.Token);
        try { await WriteCaptureAsync(active); }
        finally { captureChanges.Release(); }
    }

    private async Task WriteCaptureAsync(bool active)
    {
        if (active == Capturing) return;
        if (kernel is null || !IsConnected) throw new InvalidOperationException("PresentMon is not connected.");
        if (active && selectedPid is null) throw new InvalidOperationException("Select an application before starting capture.");
        var captureTargetRevision = targetRevision;
        await FlushAsync();
        if (active && (selectedPid is null || targetRevision != captureTargetRevision || !IsConnected))
            throw new InvalidOperationException("The target changed before capture could start.");
        await kernel.SetCaptureAsync(active, lifetime.Token);
        if (active && (selectedPid is null || targetRevision != captureTargetRevision || !IsConnected))
        {
            if (kernel.IsConnected) await kernel.SetCaptureAsync(false, lifetime.Token);
            throw new InvalidOperationException("Capture stopped because the target changed during startup.");
        }
        Capturing = active;
        captureStop?.Cancel();
        captureStop?.Dispose();
        captureStop = null;
        if (active && Preferences.EnableCaptureDuration)
        {
            captureStop = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
            _ = StopCaptureAfterDelayAsync(TimeSpan.FromSeconds(Preferences.CaptureDuration), captureStop.Token);
        }
        StateChanged?.Invoke();
    }

    private async Task StopCaptureAfterDelayAsync(TimeSpan duration, CancellationToken token)
    {
        try
        {
            await Task.Delay(duration, token);
            await SetCaptureAsync(false);
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { Notify($"Unable to stop capture: {ex.Message}"); }
    }

    public HotkeyBinding GetBinding(HotkeyAction action) => PreferenceFile.HotkeyBindings.GetValueOrDefault(action.ToString())
        ?? new() { Action = action };

    public async Task BindHotkeyAsync(HotkeyBinding binding)
    {
        if (binding.Combination is not null && PreferenceFile.HotkeyBindings.Values.Any(b =>
            b.Action != binding.Action && b.Combination?.Matches(binding.Combination) == true))
            throw new InvalidOperationException("This shortcut is already assigned to another PresentMon action.");
        await ApplyBindingAsync(binding);
        PreferenceFile.HotkeyBindings[binding.Action.ToString()] = binding;
        Changed();
        await FlushAsync();
    }

    private Task ApplyBindingAsync(HotkeyBinding binding)
    {
        if (kernel is null || !IsConnected) return Task.CompletedTask;
        return binding.Combination is null
            ? kernel.ClearHotkeyAsync((int)binding.Action, lifetime.Token)
            : kernel.BindHotkeyAsync(binding, lifetime.Token);
    }

    public async Task ResetPreferencesAsync()
    {
        if (Capturing) await SetCaptureAsync(false);
        loading = true;
        try
        {
            PreferenceFile = new() { Preferences = Preferences.CreateDefault(Introspection) };
            foreach (var binding in PreferenceFile.HotkeyBindings.Values) await ApplyBindingAsync(binding);
            LoadSelectedPreset();
        }
        finally { loading = false; }
        AttachTrackers();
        DocumentChanged?.Invoke();
        Changed();
        await FlushAsync();
    }

    public async Task ImportLoadoutAsync(string path)
    {
        var document = store.LoadLoadout(path, Introspection, Preferences);
        loading = true;
        try
        {
            Preferences.SelectedPreset = Preset.Custom;
            loadedPreset = Preset.Custom;
            Widgets = document.Widgets;
        }
        finally { loading = false; }
        AttachTrackers();
        DocumentChanged?.Invoke();
        Changed();
        await FlushAsync();
    }

    public void ExportLoadout(string path) => store.SaveLoadout(path, new() { Widgets = Widgets });

    public Task ExploreFolderAsync(string kind)
    {
        if (kind == "blocklist")
        {
            if (blocklistPath is null) throw new FileNotFoundException("No target blocklist was found in the application or user data directory.");
            var explorer = new ProcessStartInfo("explorer.exe") { UseShellExecute = true };
            explorer.ArgumentList.Add("/select,");
            explorer.ArgumentList.Add(blocklistPath);
            Process.Start(explorer);
            return Task.CompletedTask;
        }
        var path = kind switch
        {
            "captures" => CaptureDirectory,
            "etls" => Path.Combine(Options.DataDirectory, "Etl"),
            "logs" => Options.LogDirectory,
            _ => Options.DataDirectory,
        };
        Directory.CreateDirectory(path);
        Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
        return Task.CompletedTask;
    }

    private void LoadBlocklist()
    {
        var candidates = Options.FilesWorking
            ? new[] { Path.Combine(Options.InstallDirectory, "BlockLists", "TargetBlockList.txt"), Path.Combine(Options.InstallDirectory, "TargetBlockList.txt") }
            : new[] { Path.Combine(Options.AppDataDirectory, "TargetBlockList.txt"), Path.Combine(Options.InstallDirectory, "TargetBlockList.txt") };
        foreach (var path in candidates)
        {
            if (!File.Exists(path)) continue;
            try
            {
                foreach (var line in File.ReadLines(path))
                    if (!string.IsNullOrWhiteSpace(line)) blocklist.Add(line.Trim());
                blocklistPath = path;
                return;
            }
            catch (IOException) { }
        }
    }

    private async Task PollProcessesAsync()
    {
        var lastScan = Stopwatch.GetTimestamp();
        var probeActive = false;
        while (!lifetime.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(250, lifetime.Token);
                if (!IsConnected) return;
                if (Preferences.EnableAutotargetting && Stopwatch.GetElapsedTime(lastScan) >= AutomaticTargeting.Interval)
                {
                    lastScan = Stopwatch.GetTimestamp();
                    var scannedTargetRevision = targetRevision;
                    var scannedSettingsRevision = revision;
                    bool IsCurrent() => IsConnected && Preferences.EnableAutotargetting
                        && targetRevision == scannedTargetRevision && revision == scannedSettingsRevision;
                    await AutomaticTargeting.UpdateAsync(
                        () => WindowsServices.GetGpuProcessSamplesAsync(
                            Preferences.EnableTargetBlocklist ? blocklist : null, lifetime.Token),
                        pids =>
                        {
                            probeActive = true;
                            return kernel!.ProbeGpuBusyAsync(pids, lifetime.Token);
                        },
                        IsCurrent,
                        async pid =>
                        {
                            if (pid == selectedPid) return;
                            await RefreshProcessesAsync();
                            // The process may have exited or lost its window during the scan.
                            if (pid is not null && !Processes.Any(p => p.Pid == pid)) return;
                            await SelectProcessAsync(pid, IsCurrent);
                        });
                }
                if (!Preferences.EnableAutotargetting && probeActive)
                {
                    await kernel!.ProbeGpuBusyAsync([], lifetime.Token);
                    probeActive = false;
                }
                if (selectedPid is int pid)
                {
                    var checkedTargetRevision = targetRevision;
                    var exists = await Task.Run(() =>
                    {
                        try { using var process = Process.GetProcessById(pid); return !process.HasExited; }
                        catch (ArgumentException) { return false; }
                    }, lifetime.Token);
                    if (!exists) await SelectProcessAsync(null, () => targetRevision == checkedTargetRevision);
                }
            }
            catch (OperationCanceledException) { return; }
            catch (Exception ex)
            {
                Notify($"Automatic targeting: {ex.Message}");
                try { await Task.Delay(5000, lifetime.Token); }
                catch (OperationCanceledException) { return; }
            }
        }
    }

    private async Task ReceiveEventsAsync()
    {
        try
        {
            if (kernel is null) return;
            await foreach (var message in kernel.ReadEventsAsync(lifetime.Token))
            {
                await OnUiAsync(async () =>
                {
                    try { await HandleEventAsync(message); }
                    catch (Exception ex) { Notify($"PresentMon event: {ex.Message}"); }
                });
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            await OnUiAsync(() =>
            {
                IsConnected = false;
                Capturing = false;
                ConnectionStatus = "PresentMon disconnected";
                Notify(ex.Message);
                StateChanged?.Invoke();
                return Task.CompletedTask;
            });
        }
    }

    private async Task HandleEventAsync(KernelEvent message)
    {
        switch (message.Kind)
        {
            case KernelEventKind.HotkeyFired:
                switch ((HotkeyAction)(message.ActionId ?? -1))
                {
                    case HotkeyAction.ToggleCapture: await ToggleCaptureAsync(); break;
                    case HotkeyAction.ToggleOverlay: Preferences.HideAlways = !Preferences.HideAlways; break;
                    case HotkeyAction.CyclePreset: await SelectPresetAsync(Preferences.NextPreset(Preferences.SelectedPreset)); break;
                    case HotkeyAction.ToggleEtlLogging: Notify("ETL capture is currently disabled."); break;
                }
                break;
            case KernelEventKind.TargetLost:
            case KernelEventKind.StalePid:
            case KernelEventKind.OverlayDied:
                if (message.Kind == KernelEventKind.TargetLost && message.ProcessId is uint lostPid
                    && selectedPid != (int)lostPid) break;
                selectedPid = null;
                targetRevision++;
                Capturing = false;
                captureStop?.Cancel();
                if (message.Kind != KernelEventKind.TargetLost) Notify(message.Kind == KernelEventKind.StalePid
                    ? "Selected process has already exited." : "The overlay stopped unexpectedly.");
                StateChanged?.Invoke();
                break;
            case KernelEventKind.PresentmonInitFailed:
                Notify("Failed to initialize PresentMon API. Ensure PresentMon Service is running.");
                break;
        }
    }

    private Task OnUiAsync(Func<Task> action)
    {
        if (dispatcher.HasThreadAccess) return action();
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!dispatcher.TryEnqueue(async () =>
        {
            try { await action(); completion.SetResult(); }
            catch (Exception ex) { completion.SetException(ex); }
        })) completion.SetCanceled();
        return completion.Task;
    }

    public void Notify(string message)
    {
        Notification?.Invoke(message);
        try
        {
            Directory.CreateDirectory(Options.LogDirectory);
            File.AppendAllText(Path.Combine(Options.LogDirectory, "PresentMonUI.log"), $"{DateTimeOffset.Now:O} {message}{Environment.NewLine}");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { Debug.WriteLine(ex.Message); }
    }

    public async ValueTask DisposeAsync()
    {
        if (disposed) return;
        try
        {
            pendingSave?.Cancel();
            if (Capturing && IsConnected) await SetCaptureAsync(false);
            await FlushAsync();
        }
        catch (Exception ex) { Notify($"Shutdown: {ex.Message}"); }
        disposed = true;
        preferencesTracker?.Dispose();
        widgetsTracker?.Dispose();
        captureStop?.Cancel();
        lifetime.Cancel();
        if (kernel is not null) await kernel.DisposeAsync();
        pendingSave?.Dispose();
        captureStop?.Dispose();
        lifetime.Dispose();
    }
}
