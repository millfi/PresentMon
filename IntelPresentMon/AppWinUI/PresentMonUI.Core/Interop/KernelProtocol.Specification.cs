namespace PresentMon.UI.Core.Interop;

internal static partial class KernelProtocol
{
    // Keep this order identical to KernelProcess/kact/PushSpecification.h.
    public static void Specification(CerealWriter writer, Specification specification)
    {
        writer.OptionalUInt32(specification.Pid);
        WritePreferences(writer, specification.Preferences);
        writer.List(specification.Widgets, widget => WriteWidget(writer, widget));
    }

    private static void WritePreferences(CerealWriter writer, Preferences preferences)
    {
        writer.String(preferences.CapturePath);
        writer.UInt32(Unsigned(preferences.CaptureDelay));
        writer.Bool(preferences.EnableCaptureDelay);
        writer.UInt32(Unsigned(preferences.CaptureDuration));
        writer.Bool(preferences.EnableCaptureDuration);
        writer.Bool(preferences.HideDuringCapture);
        writer.Bool(preferences.HideAlways);
        writer.Bool(preferences.EnablePerMetricDeviceSelection);
        writer.Bool(preferences.IndependentWindow);
        writer.UInt32(Unsigned(preferences.MetricPollRate));
        writer.UInt32(Unsigned(preferences.OverlayDrawRate));
        writer.UInt32(Unsigned(preferences.TelemetrySamplingPeriodMs));
        writer.UInt32(Unsigned(preferences.EtwFlushPeriod));
        writer.Bool(preferences.ManualEtwFlush);
        writer.UInt32(Unsigned(preferences.MetricsOffset));
        writer.UInt32(Unsigned(preferences.MetricsWindow));
        writer.Int32((int)preferences.OverlayPosition);
        writer.Float((float)preferences.TimeRange);
        writer.Float((float)preferences.OverlayMargin);
        writer.Float((float)preferences.OverlayBorder);
        writer.Float((float)preferences.OverlayPadding);
        writer.Float((float)preferences.GraphMargin);
        writer.Float((float)preferences.GraphBorder);
        writer.Float((float)preferences.GraphPadding);
        WriteColor(writer, preferences.OverlayBorderColor);
        WriteColor(writer, preferences.OverlayBackgroundColor);
        writer.String(preferences.GraphFont.Name);
        writer.Float((float)preferences.GraphFont.AxisSize);
        writer.UInt32(Unsigned(preferences.OverlayWidth));
        writer.Bool(preferences.Upscale);
        writer.Bool(preferences.GenerateStats);
        writer.Bool(preferences.EnableTargetBlocklist);
        writer.Bool(preferences.EnableAutotargetting);
        writer.Float((float)preferences.UpscaleFactor);
        writer.OptionalInt32(preferences.AdapterId);
    }

    private static void WriteWidget(CerealWriter writer, Widget widget)
    {
        // cereal writes a signed 32-bit variant index before the selected data.
        switch (widget)
        {
            case Graph graph:
                writer.Int32(0);
                writer.List(graph.Metrics, metric => WriteWidgetMetric(writer, metric));
                writer.UInt32(Unsigned(graph.Height));
                writer.UInt32(checked((uint)graph.VDivs));
                writer.UInt32(checked((uint)graph.HDivs));
                writer.Bool(graph.ShowBottomAxis);
                writer.String(graph.GraphType.Name);
                WriteRange(writer, graph.GraphType.Range);
                WriteRange(writer, graph.GraphType.RangeRight);
                writer.UInt32(checked((uint)graph.GraphType.BinCount));
                WriteRange(writer, graph.GraphType.CountRange);
                writer.Bool(graph.GraphType.AutoLeft);
                writer.Bool(graph.GraphType.AutoRight);
                writer.Bool(graph.GraphType.AutoCount);
                WriteColor(writer, graph.GridColor);
                WriteColor(writer, graph.DividerColor);
                WriteColor(writer, graph.BackgroundColor);
                WriteColor(writer, graph.BorderColor);
                WriteColor(writer, graph.TextColor);
                writer.Float((float)graph.TextSize);
                break;
            case Readout readout:
                writer.Int32(1);
                writer.List(readout.Metrics, metric => WriteWidgetMetric(writer, metric));
                writer.Bool(readout.ShowLabel);
                writer.Float((float)readout.FontSize);
                WriteColor(writer, readout.FontColor);
                WriteColor(writer, readout.BackgroundColor);
                break;
            default:
                throw new ArgumentException("Unsupported overlay widget type.", nameof(widget));
        }
        writer.Bool(widget.LabelIncludeDeviceId);
        writer.Bool(widget.LabelIncludeDeviceName);
    }

    private static void WriteWidgetMetric(CerealWriter writer, WidgetMetric widgetMetric)
    {
        var metric = widgetMetric.Metric;
        writer.Int32(metric.MetricId);
        writer.UInt32(checked((uint)metric.ArrayIndex));
        writer.UInt32(checked((uint)(metric.DeviceId ??
            throw new ArgumentException("A metric device must be resolved before sending the specification."))));
        writer.Int32(metric.StatId);
        writer.Int32(metric.DesiredUnitId ?? 0);
        WriteColor(writer, widgetMetric.LineColor);
        WriteColor(writer, widgetMetric.FillColor);
        writer.Int32((int)widgetMetric.AxisAffinity);
    }

    private static void WriteColor(CerealWriter writer, RgbaColor color)
    {
        if (color.R is < 0 or > 255 || color.G is < 0 or > 255 || color.B is < 0 or > 255 ||
            !double.IsFinite(color.A) || color.A is < 0 or > 1)
            throw new ArgumentOutOfRangeException(nameof(color), "Invalid overlay color.");
        writer.Float((float)color.R / 255f);
        writer.Float((float)color.G / 255f);
        writer.Float((float)color.B / 255f);
        writer.Float((float)color.A);
    }

    private static void WriteRange(CerealWriter writer, IReadOnlyList<double> values)
    {
        // std::array<int, 2> has no length prefix.
        if (values.Count != 2)
            throw new ArgumentException("A graph range must contain exactly two values.", nameof(values));
        foreach (var value in values)
        {
            if (!double.IsFinite(value))
                throw new ArgumentOutOfRangeException(nameof(values), "Native graph range limits must be finite.");
            // Native graph limits are integers; retain truncation of persisted fractional values.
            writer.Int32(checked((int)value));
        }
    }

    private static uint Unsigned(double value)
    {
        if (!double.IsFinite(value) || value < 0)
            throw new ArgumentOutOfRangeException(nameof(value), "The native setting requires a finite, nonnegative value.");
        // Preserve fractional values in JSON and truncate at the native integer boundary.
        return checked((uint)value);
    }
}
