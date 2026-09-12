using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;

namespace PresentMon.UI.Core;

public static class ConfigurationJson
{
    public static JsonSerializerOptions Options { get; } = CreateOptions(false);
    public static JsonSerializerOptions PersistenceOptions { get; } = CreateOptions(true);

    private static JsonSerializerOptions CreateOptions(bool persistence)
    {
        var resolver = new DefaultJsonTypeInfoResolver();
        if (persistence)
            resolver.Modifiers.Add(info =>
            {
                if (info.Type == typeof(QualifiedMetric))
                    foreach (var property in info.Properties)
                        if (property.Name == "desiredUnitId") property.ShouldSerialize = (_, _) => false;
            });
        var options = new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            PropertyNameCaseInsensitive = true,
            WriteIndented = true,
            NumberHandling = JsonNumberHandling.AllowReadingFromString,
            TypeInfoResolver = resolver,
        };
        options.Converters.Add(new WidgetConverter());
        return options;
    }

    public static T Clone<T>(T value) => JsonSerializer.Deserialize<T>(JsonSerializer.Serialize(value, Options), Options)
        ?? throw new JsonException("Unable to clone configuration.");

    private sealed class WidgetConverter : JsonConverter<Widget>
    {
        public override Widget Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            using var document = JsonDocument.ParseValue(ref reader);
            if (!document.RootElement.TryGetProperty("widgetType", out var type))
                throw new JsonException("A widget is missing its type.");
            return type.GetInt32() switch
            {
                0 => document.RootElement.Deserialize<Graph>(options) ?? throw new JsonException("Invalid graph."),
                1 => document.RootElement.Deserialize<Readout>(options) ?? throw new JsonException("Invalid readout."),
                _ => throw new JsonException("Unsupported widget type."),
            };
        }

        public override void Write(Utf8JsonWriter writer, Widget value, JsonSerializerOptions options)
        {
            switch (value)
            {
                case Graph graph: JsonSerializer.Serialize(writer, graph, options); break;
                case Readout readout: JsonSerializer.Serialize(writer, readout, options); break;
                default: throw new JsonException("Unsupported widget type.");
            }
        }
    }
}

public static class PreferenceDocument
{
    public const string CurrentVersion = "1.1.0";

    public static PreferenceFile Parse(string json, IntrospectionData intro)
    {
        var root = DocumentMigration.ParseRoot(json, "p2c-cap-pref", "0.16.0", CurrentVersion);
        var version = DocumentMigration.VersionOf(root);
        var preferences = root["preferences"] as JsonObject ?? throw new JsonException("Missing preferences.");
        bool Before(string target) => version < Version.Parse(target);
        double Number(string key, double fallback) => DocumentMigration.Number(preferences[key], fallback);
        if (Before("0.17.0"))
        {
            var sampling = Number("samplingPeriodMs", 25);
            var samples = Number("samplesPerFrame", 4);
            if (sampling <= 0 || samples <= 0) throw new JsonException("Invalid legacy sampling rate.");
            preferences["metricPollRate"] = Math.Round(1000 / sampling, MidpointRounding.AwayFromZero);
            preferences["overlayDrawRate"] = Math.Round(1000 / (sampling * samples), MidpointRounding.AwayFromZero);
            preferences.Remove("samplingPeriodMs");
            preferences.Remove("samplesPerFrame");
        }
        if (Before("0.18.0"))
        {
            preferences["manualEtwFlush"] = true;
            preferences["etwFlushPeriod"] = 8;
            preferences["metricsOffset"] = 150;
        }
        if (Before("0.20.0") && Number("metricsOffset", 150) <= 32) preferences["metricsOffset"] = 150;
        if (Before("0.21.0") && Number("metricsOffset", 150) == 80) preferences["metricsOffset"] = 150;
        if (Before("1.0.0") && preferences["enablePerMetricDeviceSelection"] is null)
            preferences["enablePerMetricDeviceSelection"] = false;
        if (Before("1.0.1"))
        {
            var adapter = (int)Number("adapterId", 0);
            if (adapter == 0 || intro.Adapters.Count > 0 && !intro.Adapters.Any(a => a.Id == adapter))
                preferences["adapterId"] = intro.Adapters.Select(a => a.Id).DefaultIfEmpty(0).Min();
        }
        if (Before("1.1.0"))
            foreach (var key in preferences.Select(p => p.Key).Where(k => k == "enableFlashInjection" || k.StartsWith("flashInjection", StringComparison.Ordinal)).ToArray())
                preferences.Remove(key);
        root["signature"]!["version"] = CurrentVersion;
        var file = root.Deserialize<PreferenceFile>(ConfigurationJson.Options) ?? throw new JsonException("Empty preference file.");
        if (file.HotkeyBindings is null) throw new JsonException("Missing hotkey bindings.");
        foreach (var action in Enum.GetValues<HotkeyAction>())
            if (!file.HotkeyBindings.ContainsKey(action.ToString())) file.HotkeyBindings[action.ToString()] = new() { Action = action };
        ConfigurationValidation.ValidatePreferences(file.Preferences);
        return file;
    }

    public static string Serialize(PreferenceFile file)
    {
        ConfigurationValidation.ValidatePreferences(file.Preferences);
        file.Signature = new("p2c-cap-pref", CurrentVersion);
        return JsonSerializer.Serialize(file, ConfigurationJson.PersistenceOptions);
    }
}

public static class LoadoutDocument
{
    public const string CurrentVersion = "1.0.0";

    public static LoadoutFile Parse(string json, IntrospectionData intro, Preferences preferences)
    {
        var root = DocumentMigration.ParseRoot(json, "p2c-cap-load", "0.13.0", CurrentVersion);
        var version = DocumentMigration.VersionOf(root);
        var widgets = root["widgets"] as JsonArray ?? throw new JsonException("Missing loadout widgets.");
        foreach (var node in widgets)
        {
            var widget = node as JsonObject ?? throw new JsonException("Invalid loadout widget.");
            var lines = widget["metrics"] as JsonArray ?? throw new JsonException("Missing widget metrics.");
            if (version < new Version(1, 0, 0))
            {
                widget["labelIncludeDeviceId"] = lines.FirstOrDefault()?["labelIncludeDeviceId"]?.GetValue<bool>() ?? false;
                widget["labelIncludeDeviceName"] = lines.FirstOrDefault()?["labelIncludeDeviceName"]?.GetValue<bool>() ?? false;
            }
            foreach (var lineNode in lines)
            {
                var line = lineNode as JsonObject ?? throw new JsonException("Invalid metric line.");
                var metric = line["metric"] as JsonObject ?? throw new JsonException("Missing qualified metric.");
                if (version < new Version(0, 14, 0))
                {
                    var id = metric["metricId"]!.GetValue<int>();
                    if (id is 4 or 5 or >= 56 and <= 61) metric["deviceId"] = MetricResolver.SystemDeviceId;
                    if (metric["statId"]!.GetValue<int>() == 10) metric["statId"] = 12;
                    if (id == 57) metric["statId"] = 0;
                }
                metric.Remove("desiredUnitId");
                line.Remove("labelIncludeDeviceId");
                line.Remove("labelIncludeDeviceName");
            }
        }
        root["signature"]!["version"] = CurrentVersion;
        var file = root.Deserialize<LoadoutFile>(ConfigurationJson.Options) ?? throw new JsonException("Empty loadout.");
        foreach (var widget in file.Widgets.Where(w => w.Metrics.Count == 0).ToArray()) file.Widgets.Remove(widget);
        Widget.NormalizeKeys(file.Widgets);
        foreach (var line in file.Widgets.SelectMany(w => w.Metrics))
        {
            var metric = intro.Metrics.FirstOrDefault(m => m.Id == line.Metric.MetricId);
            if (metric is not null) MetricResolver.Normalize(metric, line.Metric, intro, preferences);
        }
        ConfigurationValidation.ValidateWidgets(file.Widgets);
        return file;
    }

    public static LoadoutFile FromWidgets(IEnumerable<Widget> widgets) => new() { Widgets = new(widgets) };

    public static string Serialize(LoadoutFile file)
    {
        ConfigurationValidation.ValidateWidgets(file.Widgets);
        file.Signature = new("p2c-cap-load", CurrentVersion);
        return JsonSerializer.Serialize(file, ConfigurationJson.PersistenceOptions);
    }
}

internal static class DocumentMigration
{
    public static JsonObject ParseRoot(string json, string code, string minimumVersion, string currentVersion)
    {
        var root = JsonNode.Parse(json) as JsonObject ?? throw new JsonException("Configuration must be a JSON object.");
        if (root["signature"]?["code"]?.GetValue<string>() != code) throw new JsonException("Wrong configuration file type.");
        var version = VersionOf(root);
        if (version < Version.Parse(minimumVersion)) throw new JsonException("Configuration version is too old to migrate.");
        if (version > Version.Parse(currentVersion)) throw new JsonException("Configuration was written by a newer version.");
        return root;
    }

    public static Version VersionOf(JsonObject root) => Version.TryParse(root["signature"]?["version"]?.GetValue<string>(), out var version)
        ? version : throw new JsonException("Missing or invalid configuration version.");

    public static double Number(JsonNode? node, double fallback)
    {
        if (node is null) return fallback;
        if (node is JsonValue value && value.TryGetValue<double>(out var number)) return number;
        if (double.TryParse(node.ToString(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out number)) return number;
        throw new JsonException("Expected a numeric preference.");
    }
}
