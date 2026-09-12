using System.Text.Json;
using PresentMon.UI.Core;
using PresentMon.UI.Core.Interop;

public static class NativeProtocolFixtureTests
{
    public static async Task<int> RunAsync()
    {
        // These binaries are produced by native cereal using the repository's
        // serialization declarations, extracted without changing their field order.
        EqualBytes("full-spec", KernelProtocol.Request("PushSpecification", 17,
            writer => KernelProtocol.Specification(writer, FullSpecification())));
        EqualBytes("open-session", KernelProtocol.Request("OpenSession", 29, writer => writer.UInt32(54321)));
        EqualBytes("bind-hotkey", KernelProtocol.Request("BindHotkey", 31, writer =>
        {
            writer.UInt32(42);
            writer.List(new uint[] { 2, 4 }, writer.UInt32);
            writer.Int32(3);
        }));

        var sessionReader = await ReadAsync("open-session-response");
        KernelProtocol.ValidateResponse(KernelProtocol.Header(sessionReader), "OpenSession", 29);
        var session = KernelProtocol.Session(sessionReader);
        sessionReader.RequireEnd();
        var expectedSession = new KernelSessionInfo(65432, "0123456789abcdef", "2026-09-12T09:10:11Z", "2.4.0", "3.4.0 fixture Release");
        if (session != expectedSession) throw new Exception("Native OpenSession response differs from the managed reader.");

        var introReader = await ReadAsync("introspect");
        KernelProtocol.ValidateResponse(KernelProtocol.Header(introReader), "Introspect", 23);
        var introspection = KernelProtocol.Introspection(introReader);
        introReader.RequireEnd();
        if (JsonSerializer.Serialize(introspection) != JsonSerializer.Serialize(ExpectedIntrospection()))
            throw new Exception("Native introspection response differs from the managed reader.");
        Console.WriteLine("PASS Native cereal fixtures match full specification, hotkey, session, and introspection bytes");
        return 5;
    }

    private static async Task<CerealReader> ReadAsync(string name)
    {
        await using var stream = File.OpenRead(FixturePath(name));
        return new(await KernelClient.ReadPacketAsync(stream, default));
    }

    private static string FixturePath(string name) => Path.Combine(AppContext.BaseDirectory, "NativeFixtures", name + ".bin");

    private static void EqualBytes(string name, byte[] actual)
    {
        var expected = File.ReadAllBytes(FixturePath(name));
        if (expected.AsSpan().SequenceEqual(actual)) return;
        var differing = Enumerable.Range(0, Math.Min(expected.Length, actual.Length))
            .FirstOrDefault(index => expected[index] != actual[index], -1);
        throw new Exception($"Native fixture {name} differs at byte {differing}; expected length {expected.Length}, actual {actual.Length}.");
    }

    private static Specification FullSpecification() => new()
    {
        Pid = 4242,
        Preferences = new()
        {
            CapturePath = @"C:\Captures\native-fixture", CaptureDelay = 3, EnableCaptureDelay = true,
            CaptureDuration = 61, EnableCaptureDuration = false, HideDuringCapture = true, HideAlways = false,
            EnablePerMetricDeviceSelection = true, IndependentWindow = true, MetricPollRate = 47, OverlayDrawRate = 29,
            TelemetrySamplingPeriodMs = 113, EtwFlushPeriod = 17, ManualEtwFlush = false, MetricsOffset = 157,
            MetricsWindow = 1021, OverlayPosition = OverlayPosition.BottomRight, TimeRange = 13.5,
            OverlayMargin = 1.25, OverlayBorder = 2.5, OverlayPadding = 3.75, GraphMargin = 4.25,
            GraphBorder = 5.5, GraphPadding = 6.75, OverlayBorderColor = new(11, 23, 37, .25),
            OverlayBackgroundColor = new(41, 53, 67, .5), GraphFont = new() { Name = "Segoe UI", AxisSize = 12.5 },
            OverlayWidth = 613, Upscale = true, GenerateStats = false, EnableTargetBlocklist = true,
            EnableAutotargetting = false, UpscaleFactor = 1.75, AdapterId = 7,
        },
        Widgets =
        [
            new Graph
            {
                Metrics =
                [
                    new() { Metric = new() { MetricId = 11, ArrayIndex = 2, DeviceId = 7, StatId = 2, DesiredUnitId = 4 },
                        LineColor = new(71, 83, 97, .75), FillColor = new(101, 113, 127, .125), AxisAffinity = AxisAffinity.Left },
                    new() { Metric = new() { MetricId = 14, ArrayIndex = 3, DeviceId = 9, StatId = 8, DesiredUnitId = 6 },
                        LineColor = new(131, 149, 157, .625), FillColor = new(163, 179, 191, .375), AxisAffinity = AxisAffinity.Right },
                ],
                Height = 227, VDivs = 7, HDivs = 11, ShowBottomAxis = false,
                GraphType = new() { Name = "Histogram", Range = [-17, 239], RangeRight = [-31, 401], BinCount = 37,
                    CountRange = [5, 97], AutoLeft = true, AutoRight = false, AutoCount = true },
                GridColor = new(13, 29, 43, .125), DividerColor = new(59, 73, 89, .25),
                BackgroundColor = new(103, 109, 137, .375), BorderColor = new(151, 167, 181, .5),
                TextColor = new(193, 211, 227, .875), TextSize = 14.25,
                LabelIncludeDeviceId = true, LabelIncludeDeviceName = false,
            },
            new Readout
            {
                Metrics =
                [
                    new() { Metric = new() { MetricId = 9, ArrayIndex = 4, DeviceId = 12, StatId = 1, DesiredUnitId = 7 },
                        LineColor = new(17, 31, 47, .5), FillColor = new(61, 79, 101, .75), AxisAffinity = AxisAffinity.Right },
                ],
                ShowLabel = true, FontSize = 19.5, FontColor = new(107, 139, 173, .625),
                BackgroundColor = new(199, 223, 251, .125), LabelIncludeDeviceId = false, LabelIncludeDeviceName = true,
            },
        ],
    };

    private static IntrospectionData ExpectedIntrospection() => new()
    {
        Metrics =
        [
            new() { Id = 11, Name = "Displayed FPS", Description = "Displayed frames per second",
                DeviceType = MetricDeviceType.Independent, PreferredUnitId = 4,
                DeviceAvailability = [new(0, 1, 0), new(7, 2, 3)], AvailableStatIds = [1, 2], Numeric = true },
            new() { Id = 14, Name = "GPU Busy", Description = "GPU busy time",
                DeviceType = MetricDeviceType.GraphicsAdapter, PreferredUnitId = 6,
                DeviceAvailability = [new(7, 3, 0), new(9, 4, 2)], AvailableStatIds = [8], Numeric = true },
            new() { Id = 5, Name = "CPU Name", Description = "Processor model",
                DeviceType = MetricDeviceType.System, PreferredUnitId = 0,
                DeviceAvailability = [new(12, 1, 0)], AvailableStatIds = [0], Numeric = false },
        ],
        Stats = [new(0, "None", "", "Unaggregated value"), new(1, "Average", "Avg", "Arithmetic mean"),
            new(2, "99th percentile", "P99", "Upper percentile"), new(8, "Maximum", "Max", "Largest sample")],
        Adapters = [new(7, "Intel", "Fixture Arc"), new(9, "Other", "Fixture GPU")],
        SystemDeviceId = 12,
        DefaultAdapterId = 9,
        MetricAvailabilityReasons = [new(0, "Available"), new(2, "Not exported by source"), new(3, "Not supported by device")],
    };
}
