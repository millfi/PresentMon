using System.Collections.ObjectModel;
using System.Text.Json;
using System.Text.Json.Nodes;
using PresentMon.UI.Core;

var passed = 0;
var intro = new IntrospectionData
{
    SystemDeviceId = 65536,
    Adapters = [new(7, "Test", "Second GPU"), new(2, "Test", "First GPU")],
    Metrics =
    [
        new() { Id = 8, Name = "Frame time", Numeric = true, DeviceType = MetricDeviceType.Independent, PreferredUnitId = 3,
            AvailableStatIds = [1, 5], DeviceAvailability = [new(0, 1, 0)] },
        new() { Id = 20, Name = "GPU Power", Numeric = true, DeviceType = MetricDeviceType.GraphicsAdapter, PreferredUnitId = 6,
            AvailableStatIds = [1], DeviceAvailability = [new(2, 2, 0), new(7, 4, 0)] },
        new() { Id = 57, Name = "CPU Power Limit", Numeric = true, DeviceType = MetricDeviceType.System, PreferredUnitId = 6,
            AvailableStatIds = [0], DeviceAvailability = [new(65536, 1, 0)] },
    ],
};
var prefs = Preferences.CreateDefault(intro);

void Assert(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}
void Test(string name, Action action)
{
    action();
    passed++;
    Console.WriteLine("PASS " + name);
}
void Reject(Action action)
{
    try { action(); }
    catch (Exception error) when (error is JsonException or ArgumentException) { return; }
    throw new InvalidOperationException("Expected invalid configuration to be rejected.");
}

Test("Four built-in presets round-trip without losing widgets or runtime fields", () =>
{
    int[] expectedWidgets = [4, 7, 8, 5];
    int[] expectedMetrics = [4, 10, 14, 7];
    for (var slot = 0; slot < 4; slot++)
    {
        var path = Path.Combine(AppContext.BaseDirectory, "Fixtures", $"preset-{slot}.json");
        var file = LoadoutDocument.Parse(File.ReadAllText(path), intro, prefs);
        Assert(file.Widgets.Count == expectedWidgets[slot], "Preset widget count changed.");
        Assert(file.Widgets.Sum(w => w.Metrics.Count) == expectedMetrics[slot], "Preset metric count changed.");
        Assert(file.Widgets.All(w => !w.LabelIncludeDeviceId && !w.LabelIncludeDeviceName), "Absent label flags need defaults.");
        var keys = file.Widgets.SelectMany(w => w.Metrics).Select(m => m.Key).ToArray();
        Assert(keys.Distinct().Count() == keys.Length, "Imported metric keys must be unique.");
        var json = LoadoutDocument.Serialize(file);
        Assert(!json.Contains("desiredUnitId", StringComparison.Ordinal), "Runtime units leaked to disk.");
        var reloaded = LoadoutDocument.Parse(json, intro, prefs);
        Assert(reloaded.Widgets.Select(w => w.WidgetType).SequenceEqual(file.Widgets.Select(w => w.WidgetType)), "Widget types changed.");
        Assert(LoadoutDocument.Serialize(reloaded).Contains("\"widgetType\": 0", StringComparison.Ordinal), "Widget enum must remain numeric.");
    }
});

Test("Preference defaults and old adapter IDs use minimum reported GPU", () =>
{
    Assert(prefs.AdapterId == 2 && prefs.SelectedPreset == Preset.Basic, "Incorrect defaults.");
    foreach (int? adapter in new int?[] { null, 0, 99, 7 })
    {
        var root = JsonNode.Parse(PreferenceDocument.Serialize(new PreferenceFile { Preferences = Preferences.CreateDefault(intro) }))!;
        root["signature"]!["version"] = "1.0.0";
        root["preferences"]!["adapterId"] = adapter;
        var loaded = PreferenceDocument.Parse(root.ToJsonString(), intro);
        Assert(loaded.Preferences.AdapterId == (adapter == 7 ? 7 : 2), "Legacy GPU migration changed.");
    }
    var noGpuRoot = JsonNode.Parse(PreferenceDocument.Serialize(new PreferenceFile { Preferences = new() { AdapterId = 7 } }))!;
    noGpuRoot["signature"]!["version"] = "1.0.0";
    Assert(PreferenceDocument.Parse(noGpuRoot.ToJsonString(), new()).Preferences.AdapterId == 7, "No-GPU migration lost a saved adapter.");
});

Test("Preference migrations convert legacy timing and remove flash injection", () =>
{
    var root = JsonNode.Parse(PreferenceDocument.Serialize(new PreferenceFile()))!;
    root["signature"]!["version"] = "0.16.0";
    root["preferences"]!["samplingPeriodMs"] = 20;
    root["preferences"]!["samplesPerFrame"] = 5;
    root["preferences"]!["enableFlashInjection"] = true;
    root["preferences"]!["flashInjectionSize"] = 100;
    root["preferences"]!["captureDuration"] = "12.5";
    var file = PreferenceDocument.Parse(root.ToJsonString(), intro);
    Assert(file.Preferences.MetricPollRate == 50 && file.Preferences.OverlayDrawRate == 10, "Legacy timing failed.");
    Assert(file.Preferences.MetricsOffset == 150 && file.Preferences.ManualEtwFlush, "ETW defaults failed.");
    Assert(file.Preferences.CaptureDuration == 12.5, "Legacy numeric strings must migrate.");
    Assert(!PreferenceDocument.Serialize(file).Contains("flashInjection", StringComparison.Ordinal), "Removed injection settings survived.");
});

Test("Loadout migration fixes CPU stats and lifts label flags from first line", () =>
{
    var graph = Widget.CreateGraph(new() { MetricId = 57, DeviceId = 0, StatId = 10 });
    var root = JsonNode.Parse(LoadoutDocument.Serialize(LoadoutDocument.FromWidgets([graph])))!;
    root["signature"]!["version"] = "0.13.0";
    root["widgets"]![0]!["metrics"]![0]!["labelIncludeDeviceId"] = true;
    root["widgets"]![0]!["metrics"]![0]!["metric"]!["desiredUnitId"] = 999;
    var loaded = LoadoutDocument.Parse(root.ToJsonString(), intro, prefs);
    var line = loaded.Widgets[0].Metrics[0];
    Assert(line.Metric.DeviceId == 65536 && line.Metric.StatId == 0, "CPU migration failed.");
    Assert(line.Metric.DesiredUnitId == 6 && loaded.Widgets[0].LabelIncludeDeviceId, "Runtime unit or label migration failed.");
    Assert(line.AdditionalProperties?.ContainsKey("labelIncludeDeviceId") != true, "Per-line label flag survived.");
});

Test("Device normalization preserves selection while clamping against effective GPU", () =>
{
    var gpu = intro.Metrics[1];
    var selected = new QualifiedMetric { MetricId = gpu.Id, DeviceId = 7, ArrayIndex = 3 };
    MetricResolver.Normalize(gpu, selected, intro, prefs);
    Assert(selected.DeviceId == 7 && selected.ArrayIndex == 1, "Disabled per-device selection should use default GPU size.");
    var multi = ConfigurationJson.Clone(prefs);
    multi.EnablePerMetricDeviceSelection = true;
    selected.ArrayIndex = 9;
    MetricResolver.Normalize(gpu, selected, intro, multi);
    Assert(selected.ArrayIndex == 3, "Per-device index was not clamped.");
    selected.DeviceId = 0;
    selected.ArrayIndex = -1;
    MetricResolver.Normalize(gpu, selected, intro, multi);
    Assert(selected.DeviceId is null && selected.ArrayIndex == 0, "Default device or negative index not normalized.");
    Assert(!MetricResolver.IsAvailable(gpu, new() { ArrayIndex = -1 }, intro, prefs), "Negative index cannot be available.");
});

Test("Specification resolves runtime units and devices without modifying saved loadout", () =>
{
    var graph = Widget.CreateGraph(new() { MetricId = 20, DeviceId = 7, ArrayIndex = 3, StatId = 1 });
    graph.LabelIncludeDeviceName = true;
    graph.Metrics.Add(new() { Metric = new() { MetricId = 999 } });
    var unknown = Widget.CreateReadout(new() { MetricId = 999 });
    var spec = SpecificationBuilder.Build(123, prefs, [graph, unknown], intro);
    Assert(spec.Pid == 123 && spec.Widgets.Count == 1 && spec.Widgets[0].Metrics.Count == 1, "Unknown metrics not removed.");
    Assert(spec.Widgets[0].Metrics[0].Metric.DeviceId == 2 && spec.Widgets[0].Metrics[0].Metric.DesiredUnitId == 6, "Runtime device/units wrong.");
    Assert(!spec.Widgets[0].LabelIncludeDeviceName, "Per-device suffix should be suppressed.");
    Assert(graph.Metrics.Count == 2 && graph.Metrics[0].Metric.DeviceId == 7 && graph.LabelIncludeDeviceName, "Build mutated source.");
    var noGpu = ConfigurationJson.Clone(prefs);
    noGpu.AdapterId = 0;
    Assert(SpecificationBuilder.Build(null, noGpu, [graph], intro).Widgets.Count == 0, "GPU device 0 must be omitted.");
});

Test("Unknown extension properties survive persistence but all desired units are runtime-only", () =>
{
    var root = JsonNode.Parse(LoadoutDocument.Serialize(LoadoutDocument.FromWidgets([Widget.CreateReadout(new() { MetricId = 999 })])))!;
    root["widgets"]![0]!["futureStyle"] = "retained";
    root["widgets"]![0]!["metrics"]![0]!["metric"]!["desiredUnitId"] = 999;
    var output = LoadoutDocument.Serialize(LoadoutDocument.Parse(root.ToJsonString(), intro, prefs));
    Assert(output.Contains("futureStyle", StringComparison.Ordinal) && !output.Contains("desiredUnitId", StringComparison.Ordinal), "Unknown-property persistence failed.");
});

Test("Invalid versions and unsafe settings are rejected", () =>
{
    var prefRoot = JsonNode.Parse(PreferenceDocument.Serialize(new PreferenceFile()))!;
    foreach (var version in new[] { "0.15.0", "9.0.0", "invalid" })
    {
        prefRoot["signature"]!["version"] = version;
        Reject(() => PreferenceDocument.Parse(prefRoot.ToJsonString(), intro));
    }
    var loadRoot = JsonNode.Parse(LoadoutDocument.Serialize(new()))!;
    loadRoot["signature"]!["version"] = "0.12.0";
    Reject(() => LoadoutDocument.Parse(loadRoot.ToJsonString(), intro, prefs));
    loadRoot["signature"]!["code"] = "wrong";
    Reject(() => LoadoutDocument.Parse(loadRoot.ToJsonString(), intro, prefs));
    Reject(() => PreferenceDocument.Serialize(new() { Preferences = new() { MetricPollRate = 0 } }));
    Reject(() => LoadoutDocument.Serialize(LoadoutDocument.FromWidgets([new Graph { GraphType = new() { Range = [5, 1] } }])));
});

Test("Nested model and collection edits trigger change tracking and detach correctly", () =>
{
    var file = new PreferenceFile();
    var events = 0;
    using (var tracker = new ObjectChangeTracker(file, () => events++))
    {
        file.Preferences.OverlayBackgroundColor.R = 45;
        file.HotkeyBindings[nameof(HotkeyAction.ToggleCapture)].Combination!.Modifiers.Add(1);
        file.Preferences.GraphFont = new();
        file.Preferences.GraphFont.AxisSize = 22;
    }
    var before = events;
    file.Preferences.GraphFont.AxisSize = 23;
    Assert(events >= 4 && events == before, "Nested changes or dispose failed.");
    var widgets = new ObservableCollection<Widget>();
    events = 0;
    using var widgetTracker = new ObjectChangeTracker(widgets, () => events++);
    widgets.Add(new Graph());
    ((Graph)widgets[0]).GraphType.Range[0] = -1;
    Assert(events >= 2, "New collection children must become observable.");
});

Test("Atomic persistence retains corrupt originals and last valid backup", () =>
{
    var directory = Path.Combine(Path.GetTempPath(), "PresentMonUI.Core.Tests", Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(directory);
    try
    {
        var store = new ConfigurationStore(directory, Path.Combine(AppContext.BaseDirectory, "Fixtures"));
        File.WriteAllText(store.PreferencesPath, "invalid original");
        var defaults = store.LoadPreferences(intro);
        Assert(defaults.Preferences.AdapterId == 2 && store.LastWarning is not null, "Corruption recovery must be reported.");
        Assert(File.ReadAllText(store.PreferencesPath) == "invalid original", "Loading must not overwrite original.");
        store.SavePreferences(defaults);
        Assert(Directory.GetFiles(directory, "preferences.json.recovery-*.json").Any(p => File.ReadAllText(p) == "invalid original"), "Recovery original missing.");
        defaults.Preferences.OverlayWidth = 500;
        store.SavePreferences(defaults);
        Assert(PreferenceDocument.Parse(File.ReadAllText(store.PreferencesPath + ".bak"), intro).Preferences.OverlayWidth == 400, "Previous valid backup lost.");
        Assert(store.LoadPreferences(intro).Preferences.OverlayWidth == 500, "Saved settings were not read back.");
        Assert(Directory.GetFiles(directory, "*.tmp").Length == 0, "Atomic write left temporary files.");
        store.SaveCustom(LoadoutDocument.FromWidgets([Widget.CreateGraph(MetricResolver.CreateDefault(intro, prefs))]));
        Assert(store.LoadCustom(intro, prefs).Widgets.Count == 1, "Custom autosave failed.");
    }
    finally { Directory.Delete(directory, true); }
});

passed += await KernelProtocolTests.RunAsync();
passed += StartupOptionsTests.Run();
Console.WriteLine($"All {passed} regression checks passed.");
