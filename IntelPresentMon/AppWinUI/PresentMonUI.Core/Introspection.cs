namespace PresentMon.UI.Core;

public enum MetricDeviceType { Independent, GraphicsAdapter, System }
public sealed record MetricDeviceAvailability(int DeviceId, int ArraySize, int AvailabilityId);
public sealed record Adapter(int Id, string Vendor, string Name);
public sealed record MetricStat(int Id, string Name, string ShortName, string Description);
public sealed record MetricUnit(int Id, string Name, string ShortName, string Description);
public sealed record MetricAvailabilityReason(int Id, string Description);

public sealed record Metric
{
    public int Id { get; init; }
    public string Name { get; init; } = "";
    public string Description { get; init; } = "";
    public IReadOnlyList<int> AvailableStatIds { get; init; } = [];
    public int PreferredUnitId { get; init; }
    public MetricDeviceType DeviceType { get; init; }
    public IReadOnlyList<MetricDeviceAvailability> DeviceAvailability { get; init; } = [];
    public bool Numeric { get; init; }
}

public sealed record IntrospectionData
{
    public IReadOnlyList<Metric> Metrics { get; init; } = [];
    public IReadOnlyList<MetricStat> Stats { get; init; } = [];
    public IReadOnlyList<MetricUnit> Units { get; init; } = [];
    public IReadOnlyList<Adapter> Adapters { get; init; } = [];
    public int SystemDeviceId { get; init; }
    public int DefaultAdapterId { get; init; }
    public IReadOnlyList<MetricAvailabilityReason> MetricAvailabilityReasons { get; init; } = [];
}

public static class MetricResolver
{
    public const int SystemDeviceId = 65536;
    public static int ArraySize(Metric metric, int deviceId) =>
        metric.DeviceAvailability.FirstOrDefault(d => d.DeviceId == deviceId)?.ArraySize ?? 0;

    public static int ClampArrayIndex(Metric metric, int deviceId, int index) =>
        Math.Clamp(index, 0, Math.Max(0, ArraySize(metric, deviceId) - 1));

    public static int ResolveDeviceId(Metric metric, QualifiedMetric qualified, IntrospectionData intro, Preferences preferences) =>
        metric.DeviceType switch
        {
            MetricDeviceType.Independent => 0,
            MetricDeviceType.System => intro.SystemDeviceId != 0 ? intro.SystemDeviceId : SystemDeviceId,
            MetricDeviceType.GraphicsAdapter => preferences.EnablePerMetricDeviceSelection
                && qualified.DeviceId is not null and not 0 ? qualified.DeviceId.Value : preferences.AdapterId,
            _ => qualified.DeviceId ?? 0,
        };

    public static void Normalize(Metric metric, QualifiedMetric qualified, IntrospectionData intro, Preferences preferences)
    {
        if (metric.DeviceType == MetricDeviceType.Independent) qualified.DeviceId = 0;
        else if (metric.DeviceType == MetricDeviceType.System)
            qualified.DeviceId = intro.SystemDeviceId != 0 ? intro.SystemDeviceId : SystemDeviceId;
        else if (metric.DeviceType == MetricDeviceType.GraphicsAdapter && qualified.DeviceId == 0)
            qualified.DeviceId = null;
        qualified.ArrayIndex = ClampArrayIndex(metric, ResolveDeviceId(metric, qualified, intro, preferences), qualified.ArrayIndex);
        qualified.DesiredUnitId = metric.PreferredUnitId;
    }

    public static QualifiedMetric CreateQualified(Metric metric, IntrospectionData intro, Preferences preferences, int? statId = null)
    {
        var qualified = new QualifiedMetric
        {
            MetricId = metric.Id,
            StatId = statId is not null && metric.AvailableStatIds.Contains(statId.Value)
                ? statId.Value : metric.AvailableStatIds.FirstOrDefault(),
            DeviceId = metric.DeviceType == MetricDeviceType.GraphicsAdapter ? null : 0,
        };
        Normalize(metric, qualified, intro, preferences);
        return qualified;
    }

    public static QualifiedMetric CreateDefault(IntrospectionData intro, Preferences preferences)
    {
        var metric = intro.Metrics.FirstOrDefault(m => m.Id == 8 && m.Numeric)
            ?? intro.Metrics.FirstOrDefault(m => m.Numeric)
            ?? throw new InvalidOperationException("No numeric metrics are available.");
        return CreateQualified(metric, intro, preferences, 1);
    }

    public static bool IsAvailable(Metric metric, QualifiedMetric qualified, IntrospectionData intro, Preferences preferences)
    {
        var deviceId = ResolveDeviceId(metric, qualified, intro, preferences);
        var entry = metric.DeviceAvailability.FirstOrDefault(d => d.DeviceId == deviceId);
        return entry is { AvailabilityId: 0 } && qualified.ArrayIndex >= 0
            && (entry.ArraySize <= 0 ? qualified.ArrayIndex == 0 : qualified.ArrayIndex < entry.ArraySize);
    }

    public static string? AvailabilityReason(Metric metric, QualifiedMetric qualified, IntrospectionData intro, Preferences preferences)
    {
        if (IsAvailable(metric, qualified, intro, preferences)) return null;
        var deviceId = ResolveDeviceId(metric, qualified, intro, preferences);
        var availabilityId = metric.DeviceAvailability.FirstOrDefault(d => d.DeviceId == deviceId)?.AvailabilityId ?? 1;
        return intro.MetricAvailabilityReasons.FirstOrDefault(r => r.Id == availabilityId)?.Description
            ?? "This metric is not available on the selected device.";
    }
}
