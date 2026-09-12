using System.Collections.ObjectModel;

namespace PresentMon.UI.Core;

public enum Preset { Basic = 0, GameExperience = 1, GpuFocus = 2, PowerTemperature = 3, Custom = 1000 }
public enum OverlayPosition { TopLeft, TopRight, BottomLeft, BottomRight }
public enum HotkeyAction { ToggleCapture, ToggleOverlay, CyclePreset, ToggleEtlLogging }

public sealed class Preferences : ObservableObject
{
    public Preset? SelectedPreset { get; set => SetProperty(ref field, value); }
    public string CapturePath { get; set => SetProperty(ref field, value); } = "";
    public double CaptureDelay { get; set => SetProperty(ref field, value); } = 1;
    public bool EnableCaptureDelay { get; set => SetProperty(ref field, value); }
    public double CaptureDuration { get; set => SetProperty(ref field, value); } = 10;
    public bool EnableCaptureDuration { get; set => SetProperty(ref field, value); }
    public bool HideDuringCapture { get; set => SetProperty(ref field, value); } = true;
    public bool HideAlways { get; set => SetProperty(ref field, value); }
    public bool EnablePerMetricDeviceSelection { get; set => SetProperty(ref field, value); }
    public bool IndependentWindow { get; set => SetProperty(ref field, value); }
    public double MetricPollRate { get; set => SetProperty(ref field, value); } = 40;
    public double OverlayDrawRate { get; set => SetProperty(ref field, value); } = 10;
    public double TelemetrySamplingPeriodMs { get; set => SetProperty(ref field, value); } = 100;
    public double EtwFlushPeriod { get; set => SetProperty(ref field, value); } = 8;
    public bool ManualEtwFlush { get; set => SetProperty(ref field, value); } = true;
    public double MetricsOffset { get; set => SetProperty(ref field, value); } = 150;
    public double MetricsWindow { get; set => SetProperty(ref field, value); } = 1000;
    public OverlayPosition OverlayPosition { get; set => SetProperty(ref field, value); }
    public double TimeRange { get; set => SetProperty(ref field, value); } = 10;
    public double OverlayWidth { get; set => SetProperty(ref field, value); } = 400;
    public bool Upscale { get; set => SetProperty(ref field, value); }
    public double UpscaleFactor { get; set => SetProperty(ref field, value); } = 2;
    public bool GenerateStats { get; set => SetProperty(ref field, value); } = true;
    public bool EnableTargetBlocklist { get; set => SetProperty(ref field, value); } = true;
    public bool EnableAutotargetting { get; set => SetProperty(ref field, value); }
    public double OverlayMargin { get; set => SetProperty(ref field, value); }
    public double OverlayBorder { get; set => SetProperty(ref field, value); }
    public double OverlayPadding { get; set => SetProperty(ref field, value); } = 10;
    public double GraphMargin { get; set => SetProperty(ref field, value); } = 2;
    public double GraphBorder { get; set => SetProperty(ref field, value); }
    public double GraphPadding { get; set => SetProperty(ref field, value); } = 5;
    public RgbaColor OverlayBorderColor { get; set => SetProperty(ref field, value); } = new(255, 255, 255, 0);
    public RgbaColor OverlayBackgroundColor { get; set => SetProperty(ref field, value); } = new(50, 57, 91, 220 / 255.0);
    public GraphFont GraphFont { get; set => SetProperty(ref field, value); } = new();
    public int AdapterId { get; set => SetProperty(ref field, value); }

    public static Preferences CreateDefault(IntrospectionData? introspection = null) => new()
    {
        SelectedPreset = Preset.Basic,
        AdapterId = introspection?.Adapters.Select(a => a.Id).DefaultIfEmpty(0).Min() ?? 0,
    };

    public static Preset NextPreset(Preset? current) => current is null || (int)current >= 3
        ? Preset.Basic : (Preset)((int)current + 1);
}

public sealed class GraphFont : ObservableObject
{
    public string Name { get; set => SetProperty(ref field, value); } = "Verdana";
    public double AxisSize { get; set => SetProperty(ref field, value); } = 10;
}

public sealed class HotkeyCombination : ObservableObject
{
    public int Key { get; set => SetProperty(ref field, value); }
    public ObservableCollection<int> Modifiers { get; set => SetProperty(ref field, value); } = [];

    public bool Matches(HotkeyCombination? other) => other is not null && Key == other.Key
        && Modifiers.ToHashSet().SetEquals(other.Modifiers);
}

public sealed class HotkeyBinding : ObservableObject
{
    public HotkeyAction Action { get; set => SetProperty(ref field, value); }
    public HotkeyCombination? Combination { get; set => SetProperty(ref field, value); }

    public static Dictionary<string, HotkeyBinding> CreateDefaults()
    {
        var bindings = Enum.GetValues<HotkeyAction>().ToDictionary(a => a.ToString(), a => new HotkeyBinding { Action = a });
        bindings[nameof(HotkeyAction.ToggleCapture)].Combination = new() { Key = 42, Modifiers = [2, 4] };
        bindings[nameof(HotkeyAction.CyclePreset)].Combination = new() { Key = 47, Modifiers = [2, 4] };
        bindings[nameof(HotkeyAction.ToggleOverlay)].Combination = new() { Key = 46, Modifiers = [2, 4] };
        return bindings;
    }
}

public sealed record KeyOption(int Code, string Text);
public sealed record ModifierOption(int Code, string Text);
public sealed record FileSignature(string Code, string Version);

public sealed class PreferenceFile : ObservableObject
{
    public FileSignature Signature { get; set => SetProperty(ref field, value); } = new("p2c-cap-pref", "1.1.0");
    public Preferences Preferences { get; set => SetProperty(ref field, value); } = new();
    public Dictionary<string, HotkeyBinding> HotkeyBindings { get; set => SetProperty(ref field, value); } = HotkeyBinding.CreateDefaults();
}
