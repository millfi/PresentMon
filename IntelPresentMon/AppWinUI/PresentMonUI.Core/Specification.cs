namespace PresentMon.UI.Core;

public sealed class Specification
{
    public int? Pid { get; set; }
    public Preferences Preferences { get; set; } = new();
    public List<Widget> Widgets { get; set; } = [];
}

public static class SpecificationBuilder
{
    public static Specification Build(int? pid, Preferences preferences, IEnumerable<Widget> widgets, IntrospectionData intro)
    {
        ConfigurationValidation.ValidatePreferences(preferences);
        var cloned = ConfigurationJson.Clone(LoadoutDocument.FromWidgets(widgets));
        foreach (var widget in cloned.Widgets)
        {
            foreach (var line in widget.Metrics.ToArray())
            {
                var metric = intro.Metrics.FirstOrDefault(m => m.Id == line.Metric.MetricId);
                if (metric is null)
                {
                    widget.Metrics.Remove(line);
                    continue;
                }
                if (!preferences.EnablePerMetricDeviceSelection && metric.DeviceType == MetricDeviceType.GraphicsAdapter)
                    line.Metric.DeviceId = null;
                MetricResolver.Normalize(metric, line.Metric, intro, preferences);
                if (metric.DeviceType == MetricDeviceType.GraphicsAdapter)
                {
                    var device = MetricResolver.ResolveDeviceId(metric, line.Metric, intro, preferences);
                    if (device == 0) { widget.Metrics.Remove(line); continue; }
                    line.Metric.DeviceId = device;
                }
            }
            if (!preferences.EnablePerMetricDeviceSelection)
            {
                widget.LabelIncludeDeviceId = false;
                widget.LabelIncludeDeviceName = false;
            }
        }
        var survivingWidgets = cloned.Widgets.Where(w => w.Metrics.Count > 0).ToList();
        ConfigurationValidation.ValidateWidgets(survivingWidgets);
        return new() { Pid = pid, Preferences = ConfigurationJson.Clone(preferences), Widgets = survivingWidgets };
    }
}

public static class ConfigurationValidation
{
    public static void ValidatePreferences(Preferences preferences)
    {
        ArgumentNullException.ThrowIfNull(preferences);
        Positive(preferences.MetricPollRate, "Polling rate");
        Positive(preferences.OverlayDrawRate, "Overlay draw rate");
        Positive(preferences.TelemetrySamplingPeriodMs, "Telemetry period");
        Positive(preferences.EtwFlushPeriod, "ETW flush period");
        Positive(preferences.MetricsWindow, "Metric window");
        Positive(preferences.TimeRange, "Graph time range");
        Positive(preferences.OverlayWidth, "Overlay width");
        Positive(preferences.UpscaleFactor, "Graphics scale");
        Nonnegative(preferences.MetricsOffset, "Metric offset");
        Nonnegative(preferences.CaptureDelay, "Capture delay");
        if (preferences.EnableCaptureDuration) Positive(preferences.CaptureDuration, "Capture duration");
        else Nonnegative(preferences.CaptureDuration, "Capture duration");
        if (preferences.SelectedPreset is not null && !Enum.IsDefined(preferences.SelectedPreset.Value))
            throw new ArgumentException("Unknown preset selection.");
        if (!Enum.IsDefined(preferences.OverlayPosition)) throw new ArgumentException("Unknown overlay position.");
        ValidateColor(preferences.OverlayBackgroundColor);
        ValidateColor(preferences.OverlayBorderColor);
    }

    public static void ValidateWidgets(IEnumerable<Widget> widgets)
    {
        foreach (var widget in widgets)
        {
            if (widget.Metrics is null) throw new ArgumentException("Missing widget metrics.");
            foreach (var line in widget.Metrics)
            {
                if (line?.Metric is null) throw new ArgumentException("Missing qualified metric.");
                ValidateColor(line.LineColor);
                ValidateColor(line.FillColor);
                if (!Enum.IsDefined(line.AxisAffinity)) throw new ArgumentException("Unknown metric axis.");
            }
            switch (widget)
            {
                case Graph graph:
                    Positive(graph.Height, "Graph height");
                    Positive(graph.TextSize, "Graph text size");
                    Positive(graph.VDivs, "Vertical grid divisions");
                    Positive(graph.HDivs, "Horizontal grid divisions");
                    if (graph.GraphType is null) throw new ArgumentException("Missing graph options.");
                    if (graph.GraphType.Name is not ("Line" or "Histogram")) throw new ArgumentException("Unknown graph style.");
                    Positive(graph.GraphType.BinCount, "Histogram bins");
                    ValidateRange(graph.GraphType.Range, "Left value range");
                    ValidateRange(graph.GraphType.RangeRight, "Right value range");
                    ValidateRange(graph.GraphType.CountRange, "Histogram count range");
                    ValidateColor(graph.GridColor);
                    ValidateColor(graph.DividerColor);
                    ValidateColor(graph.BackgroundColor);
                    ValidateColor(graph.BorderColor);
                    ValidateColor(graph.TextColor);
                    break;
                case Readout readout:
                    Positive(readout.FontSize, "Readout font size");
                    ValidateColor(readout.FontColor);
                    ValidateColor(readout.BackgroundColor);
                    break;
                default: throw new ArgumentException("Unknown widget type.");
            }
        }
    }

    private static void ValidateRange(IList<double>? range, string name)
    {
        if (range is null || range.Count != 2 || !double.IsFinite(range[0]) || !double.IsFinite(range[1]) || range[0] > range[1])
            throw new ArgumentException($"{name} must contain an ordered minimum and maximum.");
    }

    private static void ValidateColor(RgbaColor? color)
    {
        if (color is null || color.R is < 0 or > 255 || color.G is < 0 or > 255 || color.B is < 0 or > 255
            || !double.IsFinite(color.A) || color.A < 0 || color.A > 1)
            throw new ArgumentException("Colors require RGB values from 0 to 255 and opacity from 0 to 1.");
    }

    private static void Positive(double number, string name)
    {
        if (!double.IsFinite(number) || number <= 0) throw new ArgumentException($"{name} must be greater than zero.");
    }

    private static void Nonnegative(double number, string name)
    {
        if (!double.IsFinite(number) || number < 0) throw new ArgumentException($"{name} cannot be negative.");
    }
}
