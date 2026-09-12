using System.Collections.ObjectModel;
using System.Text.Json.Serialization;

namespace PresentMon.UI.Core;

public enum WidgetType { Graph, Readout }
public enum AxisAffinity { Left, Right }

public sealed class RgbaColor : ObservableObject
{
    public RgbaColor() { }
    public RgbaColor(int r, int g, int b, double a) { R = r; G = g; B = b; A = a; }
    public int R { get; set => SetProperty(ref field, value); }
    public int G { get; set => SetProperty(ref field, value); }
    public int B { get; set => SetProperty(ref field, value); }
    public double A { get; set => SetProperty(ref field, value); } = 1;
}

public abstract class Widget : ObservableObject
{
    private static int nextKey;
    public int Key { get; set => SetProperty(ref field, value); } = Interlocked.Increment(ref nextKey);
    public ObservableCollection<WidgetMetric> Metrics { get; set => SetProperty(ref field, value); } = [];
    public abstract WidgetType WidgetType { get; }
    public bool LabelIncludeDeviceId { get; set => SetProperty(ref field, value); }
    public bool LabelIncludeDeviceName { get; set => SetProperty(ref field, value); }

    public static Graph CreateGraph(QualifiedMetric metric) => new() { Metrics = [new WidgetMetric { Metric = metric }] };
    public static Readout CreateReadout(QualifiedMetric metric) => new() { Metrics = [new WidgetMetric { Metric = metric }] };

    public static void NormalizeKeys(IEnumerable<Widget> widgets)
    {
        foreach (var widget in widgets)
        {
            widget.Key = Interlocked.Increment(ref nextKey);
            foreach (var line in widget.Metrics) line.RegenerateKey();
        }
    }
}

public sealed class Graph : Widget
{
    public override WidgetType WidgetType => WidgetType.Graph;
    public double Height { get; set => SetProperty(ref field, value); } = 80;
    public int VDivs { get; set => SetProperty(ref field, value); } = 4;
    public int HDivs { get; set => SetProperty(ref field, value); } = 40;
    public bool ShowBottomAxis { get; set => SetProperty(ref field, value); }
    public GraphOptions GraphType { get; set => SetProperty(ref field, value); } = new();
    public RgbaColor GridColor { get; set => SetProperty(ref field, value); } = new(47, 120, 190, 40 / 255.0);
    public RgbaColor DividerColor { get; set => SetProperty(ref field, value); } = new(57, 126, 150, 220 / 255.0);
    public RgbaColor BackgroundColor { get; set => SetProperty(ref field, value); } = new(0, 0, 0, 0);
    public RgbaColor BorderColor { get; set => SetProperty(ref field, value); } = new(0, 0, 0, 0);
    public RgbaColor TextColor { get; set => SetProperty(ref field, value); } = new(242, 242, 242, 1);
    public double TextSize { get; set => SetProperty(ref field, value); } = 11;
}

public sealed class GraphOptions : ObservableObject
{
    public string Name { get; set => SetProperty(ref field, value); } = "Line";
    public ObservableCollection<double> Range { get; set => SetProperty(ref field, value); } = [0, 150];
    public ObservableCollection<double> RangeRight { get; set => SetProperty(ref field, value); } = [0, 150];
    public int BinCount { get; set => SetProperty(ref field, value); } = 40;
    public ObservableCollection<double> CountRange { get; set => SetProperty(ref field, value); } = [0, 1000];
    public bool AutoLeft { get; set => SetProperty(ref field, value); } = true;
    public bool AutoRight { get; set => SetProperty(ref field, value); } = true;
    public bool AutoCount { get; set => SetProperty(ref field, value); }
}

public sealed class Readout : Widget
{
    public override WidgetType WidgetType => WidgetType.Readout;
    public bool ShowLabel { get; set => SetProperty(ref field, value); } = true;
    public double FontSize { get; set => SetProperty(ref field, value); } = 12;
    public RgbaColor FontColor { get; set => SetProperty(ref field, value); } = new(205, 211, 233, 1);
    public RgbaColor BackgroundColor { get; set => SetProperty(ref field, value); } = new(45, 50, 96, 0.4);
}

public sealed class WidgetMetric : ObservableObject
{
    private static int nextKey;
    public int Key { get; set => SetProperty(ref field, value); } = Interlocked.Increment(ref nextKey);
    public QualifiedMetric Metric { get; set => SetProperty(ref field, value); } = new();
    public RgbaColor LineColor { get; set => SetProperty(ref field, value); } = new(100, 255, 255, 220 / 255.0);
    public RgbaColor FillColor { get; set => SetProperty(ref field, value); } = new(57, 210, 250, 25 / 255.0);
    public AxisAffinity AxisAffinity { get; set => SetProperty(ref field, value); }
    internal void RegenerateKey() => Key = Interlocked.Increment(ref nextKey);
}

public sealed class QualifiedMetric : ObservableObject
{
    public int MetricId { get; set => SetProperty(ref field, value); }
    public int ArrayIndex { get; set => SetProperty(ref field, value); }
    public int? DeviceId { get; set => SetProperty(ref field, value); }
    public int StatId { get; set => SetProperty(ref field, value); }
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? DesiredUnitId { get; set => SetProperty(ref field, value); }
}

public sealed class LoadoutFile : ObservableObject
{
    public FileSignature Signature { get; set => SetProperty(ref field, value); } = new("p2c-cap-load", "1.0.0");
    public ObservableCollection<Widget> Widgets { get; set => SetProperty(ref field, value); } = [];
}
