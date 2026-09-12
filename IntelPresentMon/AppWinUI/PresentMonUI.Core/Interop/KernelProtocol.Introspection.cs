namespace PresentMon.UI.Core.Interop;

internal static partial class KernelProtocol
{
    // Matches KernelProcess/kact/Introspect.h, including defaultAdapterId.
    public static IntrospectionData Introspection(CerealReader reader)
    {
        var metrics = reader.List(() => ReadMetric(reader), 45);
        var stats = reader.List(() => new MetricStat(reader.Int32(), reader.String(), reader.String(), reader.String()), 28);
        // Unit currently has an empty native serialize method and no fields.
        var units = reader.List(() => new MetricUnit(0, "", "", ""), 0);
        var adapters = reader.List(() => new Adapter(checked((int)reader.UInt32()), reader.String(), reader.String()), 20);
        var systemDeviceId = checked((int)reader.UInt32());
        var defaultAdapterId = checked((int)reader.UInt32());
        var reasons = reader.List(() => new MetricAvailabilityReason(reader.Int32(), reader.String()), 12);
        return new IntrospectionData
        {
            Metrics = metrics,
            Stats = stats,
            Units = units,
            Adapters = adapters,
            SystemDeviceId = systemDeviceId,
            DefaultAdapterId = defaultAdapterId,
            MetricAvailabilityReasons = reasons,
        };
    }

    private static Metric ReadMetric(CerealReader reader)
    {
        var id = reader.Int32();
        var name = reader.String();
        var description = reader.String();
        var deviceType = (MetricDeviceType)reader.Int32();
        var preferredUnitId = reader.Int32();
        var availability = reader.List(() => new MetricDeviceAvailability(
            checked((int)reader.UInt32()), reader.Int32(), reader.Int32()), 12);
        var availableStats = reader.List(reader.Int32, 4);
        var numeric = reader.Bool();
        return new Metric
        {
            Id = id,
            Name = name,
            Description = description,
            DeviceType = deviceType,
            PreferredUnitId = preferredUnitId,
            DeviceAvailability = availability,
            AvailableStatIds = availableStats,
            Numeric = numeric,
        };
    }
}
