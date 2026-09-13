// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "ConfigurationJson.h"

#include "Introspection.h"
#include "Specification.h"

#include <algorithm>
#include <charconv>
#include <compare>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace pmon::ui::core
{
    namespace
    {
        class JsonError : public std::runtime_error
        {
        public:
            using std::runtime_error::runtime_error;
        };

        struct Version
        {
            int Major = 0;
            int Minor = 0;
            int Patch = 0;

            auto operator<=>(const Version&) const = default;
        };

        Version ParseVersion(const std::string& text)
        {
            Version version;
            const auto first = text.find('.');
            const auto second = first == std::string::npos ? std::string::npos : text.find('.', first + 1);
            if (first == std::string::npos || second == std::string::npos || text.find('.', second + 1) != std::string::npos) {
                throw JsonError("Missing or invalid configuration version.");
            }
            const auto parsePart = [](std::string_view value) {
                int result = 0;
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
                if (error != std::errc{} || end != value.data() + value.size() || result < 0) {
                    throw JsonError("Missing or invalid configuration version.");
                }
                return result;
            };
            version.Major = parsePart(std::string_view{ text }.substr(0, first));
            version.Minor = parsePart(std::string_view{ text }.substr(first + 1, second - first - 1));
            version.Patch = parsePart(std::string_view{ text }.substr(second + 1));
            return version;
        }

        Json Extensions(const Json& source, std::initializer_list<const char*> known)
        {
            Json extensions = Json::object();
            if (!source.is_object()) {
                return extensions;
            }
            for (const auto& item : source.items()) {
                const auto found = std::find_if(known.begin(), known.end(), [&item](const char* key) {
                    return item.key() == key;
                });
                if (found == known.end()) {
                    extensions[item.key()] = item.value();
                }
            }
            return extensions;
        }

        Json BeginObject(const Json& extensions)
        {
            return extensions.is_object() ? extensions : Json::object();
        }

        template<typename T>
        void Read(const Json& object, const char* key, T& value)
        {
            const auto it = object.find(key);
            if (it == object.end() || it->is_null()) {
                return;
            }
            try {
                value = it->get<T>();
            }
            catch (const nlohmann::json::exception&) {
                throw JsonError(std::string{ "Invalid value for " } + key + ".");
            }
        }

        template<typename T>
        void ReadNumber(const Json& object, const char* key, T& value)
        {
            const auto it = object.find(key);
            if (it == object.end() || it->is_null()) {
                return;
            }
            try {
                if (it->is_string()) {
                    std::istringstream stream{ it->get<std::string>() };
                    T parsed{};
                    stream >> parsed;
                    if (stream.fail() || !stream.eof()) {
                        throw JsonError(std::string{ "Invalid value for " } + key + ".");
                    }
                    value = parsed;
                }
                else {
                    value = it->get<T>();
                }
            }
            catch (const nlohmann::json::exception&) {
                throw JsonError(std::string{ "Invalid value for " } + key + ".");
            }
        }

        template<typename T>
        void ReadEnum(const Json& object, const char* key, T& value)
        {
            int raw = (int)value;
            ReadNumber(object, key, raw);
            value = (T)raw;
        }

        double Number(const Json& object, const char* key, double fallback)
        {
            const auto it = object.find(key);
            if (it == object.end() || it->is_null()) {
                return fallback;
            }
            double value = fallback;
            ReadNumber(object, key, value);
            return value;
        }

        Json ToJson(const RgbaColor& value)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["r"] = value.R;
            result["g"] = value.G;
            result["b"] = value.B;
            result["a"] = value.A;
            return result;
        }

        RgbaColor RgbaColorFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid color.");
            RgbaColor value;
            ReadNumber(object, "r", value.R);
            ReadNumber(object, "g", value.G);
            ReadNumber(object, "b", value.B);
            ReadNumber(object, "a", value.A);
            value.AdditionalProperties = Extensions(object, { "r", "g", "b", "a" });
            return value;
        }

        Json ToJson(const GraphFont& value)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["name"] = value.Name;
            result["axisSize"] = value.AxisSize;
            return result;
        }

        GraphFont GraphFontFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid graph font.");
            GraphFont value;
            Read(object, "name", value.Name);
            ReadNumber(object, "axisSize", value.AxisSize);
            value.AdditionalProperties = Extensions(object, { "name", "axisSize" });
            return value;
        }

        Json ToJson(const FileSignature& value)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["code"] = value.Code;
            result["version"] = value.Version;
            return result;
        }

        FileSignature FileSignatureFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid configuration signature.");
            FileSignature value;
            Read(object, "code", value.Code);
            Read(object, "version", value.Version);
            value.AdditionalProperties = Extensions(object, { "code", "version" });
            return value;
        }

        Json ToJson(const HotkeyCombination& value)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["key"] = value.Key;
            result["modifiers"] = value.Modifiers;
            return result;
        }

        HotkeyCombination HotkeyCombinationFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid hotkey combination.");
            HotkeyCombination value;
            ReadNumber(object, "key", value.Key);
            Read(object, "modifiers", value.Modifiers);
            value.AdditionalProperties = Extensions(object, { "key", "modifiers" });
            return value;
        }

        Json ToJson(const HotkeyBinding& value)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["action"] = (int)value.Action;
            result["combination"] = value.Combination ? ToJson(*value.Combination) : Json(nullptr);
            return result;
        }

        HotkeyBinding HotkeyBindingFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid hotkey binding.");
            HotkeyBinding value;
            ReadEnum(object, "action", value.Action);
            const auto combination = object.find("combination");
            if (combination != object.end() && !combination->is_null()) {
                value.Combination = HotkeyCombinationFromJson(*combination);
            }
            value.AdditionalProperties = Extensions(object, { "action", "combination" });
            return value;
        }

        Json ToJson(const Preferences& value)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["selectedPreset"] = value.SelectedPreset ? Json((int)*value.SelectedPreset) : Json(nullptr);
            result["capturePath"] = value.CapturePath;
            result["captureDelay"] = value.CaptureDelay;
            result["enableCaptureDelay"] = value.EnableCaptureDelay;
            result["captureDuration"] = value.CaptureDuration;
            result["enableCaptureDuration"] = value.EnableCaptureDuration;
            result["hideDuringCapture"] = value.HideDuringCapture;
            result["hideAlways"] = value.HideAlways;
            result["enablePerMetricDeviceSelection"] = value.EnablePerMetricDeviceSelection;
            result["independentWindow"] = value.IndependentWindow;
            result["metricPollRate"] = value.MetricPollRate;
            result["overlayDrawRate"] = value.OverlayDrawRate;
            result["telemetrySamplingPeriodMs"] = value.TelemetrySamplingPeriodMs;
            result["etwFlushPeriod"] = value.EtwFlushPeriod;
            result["manualEtwFlush"] = value.ManualEtwFlush;
            result["metricsOffset"] = value.MetricsOffset;
            result["metricsWindow"] = value.MetricsWindow;
            result["overlayPosition"] = (int)value.OverlayPosition;
            result["timeRange"] = value.TimeRange;
            result["overlayWidth"] = value.OverlayWidth;
            result["upscale"] = value.Upscale;
            result["upscaleFactor"] = value.UpscaleFactor;
            result["generateStats"] = value.GenerateStats;
            result["enableTargetBlocklist"] = value.EnableTargetBlocklist;
            result["enableAutotargetting"] = value.EnableAutotargetting;
            result["overlayMargin"] = value.OverlayMargin;
            result["overlayBorder"] = value.OverlayBorder;
            result["overlayPadding"] = value.OverlayPadding;
            result["graphMargin"] = value.GraphMargin;
            result["graphBorder"] = value.GraphBorder;
            result["graphPadding"] = value.GraphPadding;
            result["overlayBorderColor"] = ToJson(value.OverlayBorderColor);
            result["overlayBackgroundColor"] = ToJson(value.OverlayBackgroundColor);
            result["graphFont"] = ToJson(value.GraphFont);
            result["adapterId"] = value.AdapterId;
            return result;
        }

        Preferences PreferencesFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid preferences.");
            Preferences value;
            const auto preset = object.find("selectedPreset");
            if (preset != object.end() && !preset->is_null()) {
                int raw = 0;
                ReadNumber(object, "selectedPreset", raw);
                value.SelectedPreset = (Preset)raw;
            }
            Read(object, "capturePath", value.CapturePath);
            ReadNumber(object, "captureDelay", value.CaptureDelay);
            Read(object, "enableCaptureDelay", value.EnableCaptureDelay);
            ReadNumber(object, "captureDuration", value.CaptureDuration);
            Read(object, "enableCaptureDuration", value.EnableCaptureDuration);
            Read(object, "hideDuringCapture", value.HideDuringCapture);
            Read(object, "hideAlways", value.HideAlways);
            Read(object, "enablePerMetricDeviceSelection", value.EnablePerMetricDeviceSelection);
            Read(object, "independentWindow", value.IndependentWindow);
            ReadNumber(object, "metricPollRate", value.MetricPollRate);
            ReadNumber(object, "overlayDrawRate", value.OverlayDrawRate);
            ReadNumber(object, "telemetrySamplingPeriodMs", value.TelemetrySamplingPeriodMs);
            ReadNumber(object, "etwFlushPeriod", value.EtwFlushPeriod);
            Read(object, "manualEtwFlush", value.ManualEtwFlush);
            ReadNumber(object, "metricsOffset", value.MetricsOffset);
            ReadNumber(object, "metricsWindow", value.MetricsWindow);
            ReadEnum(object, "overlayPosition", value.OverlayPosition);
            ReadNumber(object, "timeRange", value.TimeRange);
            ReadNumber(object, "overlayWidth", value.OverlayWidth);
            Read(object, "upscale", value.Upscale);
            ReadNumber(object, "upscaleFactor", value.UpscaleFactor);
            Read(object, "generateStats", value.GenerateStats);
            Read(object, "enableTargetBlocklist", value.EnableTargetBlocklist);
            Read(object, "enableAutotargetting", value.EnableAutotargetting);
            ReadNumber(object, "overlayMargin", value.OverlayMargin);
            ReadNumber(object, "overlayBorder", value.OverlayBorder);
            ReadNumber(object, "overlayPadding", value.OverlayPadding);
            ReadNumber(object, "graphMargin", value.GraphMargin);
            ReadNumber(object, "graphBorder", value.GraphBorder);
            ReadNumber(object, "graphPadding", value.GraphPadding);
            if (const auto item = object.find("overlayBorderColor"); item != object.end() && !item->is_null()) value.OverlayBorderColor = RgbaColorFromJson(*item);
            if (const auto item = object.find("overlayBackgroundColor"); item != object.end() && !item->is_null()) value.OverlayBackgroundColor = RgbaColorFromJson(*item);
            if (const auto item = object.find("graphFont"); item != object.end() && !item->is_null()) value.GraphFont = GraphFontFromJson(*item);
            ReadNumber(object, "adapterId", value.AdapterId);
            value.AdditionalProperties = Extensions(object, { "selectedPreset", "capturePath", "captureDelay", "enableCaptureDelay", "captureDuration", "enableCaptureDuration", "hideDuringCapture", "hideAlways", "enablePerMetricDeviceSelection", "independentWindow", "metricPollRate", "overlayDrawRate", "telemetrySamplingPeriodMs", "etwFlushPeriod", "manualEtwFlush", "metricsOffset", "metricsWindow", "overlayPosition", "timeRange", "overlayWidth", "upscale", "upscaleFactor", "generateStats", "enableTargetBlocklist", "enableAutotargetting", "overlayMargin", "overlayBorder", "overlayPadding", "graphMargin", "graphBorder", "graphPadding", "overlayBorderColor", "overlayBackgroundColor", "graphFont", "adapterId" });
            return value;
        }

        Json ToJson(const QualifiedMetric& value, bool persistence)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["metricId"] = value.MetricId;
            result["arrayIndex"] = value.ArrayIndex;
            result["deviceId"] = value.DeviceId ? Json(*value.DeviceId) : Json(nullptr);
            result["statId"] = value.StatId;
            if (!persistence && value.DesiredUnitId) result["desiredUnitId"] = *value.DesiredUnitId;
            else result.erase("desiredUnitId");
            return result;
        }

        QualifiedMetric QualifiedMetricFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid qualified metric.");
            QualifiedMetric value;
            ReadNumber(object, "metricId", value.MetricId);
            ReadNumber(object, "arrayIndex", value.ArrayIndex);
            const auto device = object.find("deviceId");
            if (device != object.end() && !device->is_null()) {
                int number = 0;
                ReadNumber(object, "deviceId", number);
                value.DeviceId = number;
            }
            ReadNumber(object, "statId", value.StatId);
            const auto desired = object.find("desiredUnitId");
            if (desired != object.end() && !desired->is_null()) {
                int number = 0;
                ReadNumber(object, "desiredUnitId", number);
                value.DesiredUnitId = number;
            }
            value.AdditionalProperties = Extensions(object, { "metricId", "arrayIndex", "deviceId", "statId", "desiredUnitId" });
            return value;
        }

        Json ToJson(const WidgetMetric& value, bool persistence)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["key"] = value.Key;
            result["metric"] = ToJson(value.Metric, persistence);
            result["lineColor"] = ToJson(value.LineColor);
            result["fillColor"] = ToJson(value.FillColor);
            result["axisAffinity"] = (int)value.AxisAffinity;
            return result;
        }

        WidgetMetric WidgetMetricFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid widget metric.");
            WidgetMetric value;
            ReadNumber(object, "key", value.Key);
            const auto metric = object.find("metric");
            if (metric == object.end() || metric->is_null()) throw JsonError("Missing qualified metric.");
            value.Metric = QualifiedMetricFromJson(*metric);
            if (const auto item = object.find("lineColor"); item != object.end() && !item->is_null()) value.LineColor = RgbaColorFromJson(*item);
            if (const auto item = object.find("fillColor"); item != object.end() && !item->is_null()) value.FillColor = RgbaColorFromJson(*item);
            ReadEnum(object, "axisAffinity", value.AxisAffinity);
            value.AdditionalProperties = Extensions(object, { "key", "metric", "lineColor", "fillColor", "axisAffinity" });
            return value;
        }

        Json ToJson(const GraphOptions& value)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["name"] = value.Name;
            result["range"] = value.Range;
            result["rangeRight"] = value.RangeRight;
            result["binCount"] = value.BinCount;
            result["countRange"] = value.CountRange;
            result["autoLeft"] = value.AutoLeft;
            result["autoRight"] = value.AutoRight;
            result["autoCount"] = value.AutoCount;
            return result;
        }

        GraphOptions GraphOptionsFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid graph options.");
            GraphOptions value;
            Read(object, "name", value.Name);
            Read(object, "range", value.Range);
            Read(object, "rangeRight", value.RangeRight);
            ReadNumber(object, "binCount", value.BinCount);
            Read(object, "countRange", value.CountRange);
            Read(object, "autoLeft", value.AutoLeft);
            Read(object, "autoRight", value.AutoRight);
            Read(object, "autoCount", value.AutoCount);
            value.AdditionalProperties = Extensions(object, { "name", "range", "rangeRight", "binCount", "countRange", "autoLeft", "autoRight", "autoCount" });
            return value;
        }

        Json ToJson(const Widget& value, bool persistence)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["key"] = value.Key;
            result["widgetType"] = (int)value.GetWidgetType();
            result["metrics"] = Json::array();
            for (const auto& line : value.Metrics) result["metrics"].push_back(ToJson(line, persistence));
            result["labelIncludeDeviceId"] = value.LabelIncludeDeviceId;
            result["labelIncludeDeviceName"] = value.LabelIncludeDeviceName;
            if (const auto* graph = dynamic_cast<const Graph*>(&value)) {
                result["height"] = graph->Height;
                result["vDivs"] = graph->VDivs;
                result["hDivs"] = graph->HDivs;
                result["showBottomAxis"] = graph->ShowBottomAxis;
                result["graphType"] = ToJson(graph->GraphType);
                result["gridColor"] = ToJson(graph->GridColor);
                result["dividerColor"] = ToJson(graph->DividerColor);
                result["backgroundColor"] = ToJson(graph->BackgroundColor);
                result["borderColor"] = ToJson(graph->BorderColor);
                result["textColor"] = ToJson(graph->TextColor);
                result["textSize"] = graph->TextSize;
            }
            else if (const auto* readout = dynamic_cast<const Readout*>(&value)) {
                result["showLabel"] = readout->ShowLabel;
                result["fontSize"] = readout->FontSize;
                result["fontColor"] = ToJson(readout->FontColor);
                result["backgroundColor"] = ToJson(readout->BackgroundColor);
            }
            else throw JsonError("Unsupported widget type.");
            return result;
        }

        std::shared_ptr<Widget> WidgetFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Invalid loadout widget.");
            const auto type = object.find("widgetType");
            if (type == object.end()) throw JsonError("A widget is missing its type.");
            int rawType = 0;
            ReadNumber(object, "widgetType", rawType);
            std::shared_ptr<Widget> widget;
            if (rawType == (int)WidgetType::Graph) widget = std::make_shared<Graph>();
            else if (rawType == (int)WidgetType::Readout) widget = std::make_shared<Readout>();
            else throw JsonError("Unsupported widget type.");
            ReadNumber(object, "key", widget->Key);
            const auto metrics = object.find("metrics");
            if (metrics == object.end() || !metrics->is_array()) throw JsonError("Missing widget metrics.");
            widget->Metrics.clear();
            for (const auto& line : *metrics) widget->Metrics.push_back(WidgetMetricFromJson(line));
            Read(object, "labelIncludeDeviceId", widget->LabelIncludeDeviceId);
            Read(object, "labelIncludeDeviceName", widget->LabelIncludeDeviceName);
            if (auto* graph = dynamic_cast<Graph*>(widget.get())) {
                ReadNumber(object, "height", graph->Height);
                ReadNumber(object, "vDivs", graph->VDivs);
                ReadNumber(object, "hDivs", graph->HDivs);
                Read(object, "showBottomAxis", graph->ShowBottomAxis);
                if (const auto item = object.find("graphType"); item != object.end() && !item->is_null()) graph->GraphType = GraphOptionsFromJson(*item);
                if (const auto item = object.find("gridColor"); item != object.end() && !item->is_null()) graph->GridColor = RgbaColorFromJson(*item);
                if (const auto item = object.find("dividerColor"); item != object.end() && !item->is_null()) graph->DividerColor = RgbaColorFromJson(*item);
                if (const auto item = object.find("backgroundColor"); item != object.end() && !item->is_null()) graph->BackgroundColor = RgbaColorFromJson(*item);
                if (const auto item = object.find("borderColor"); item != object.end() && !item->is_null()) graph->BorderColor = RgbaColorFromJson(*item);
                if (const auto item = object.find("textColor"); item != object.end() && !item->is_null()) graph->TextColor = RgbaColorFromJson(*item);
                ReadNumber(object, "textSize", graph->TextSize);
                widget->AdditionalProperties = Extensions(object, { "key", "widgetType", "metrics", "labelIncludeDeviceId", "labelIncludeDeviceName", "height", "vDivs", "hDivs", "showBottomAxis", "graphType", "gridColor", "dividerColor", "backgroundColor", "borderColor", "textColor", "textSize" });
            }
            else {
                auto* readout = static_cast<Readout*>(widget.get());
                Read(object, "showLabel", readout->ShowLabel);
                ReadNumber(object, "fontSize", readout->FontSize);
                if (const auto item = object.find("fontColor"); item != object.end() && !item->is_null()) readout->FontColor = RgbaColorFromJson(*item);
                if (const auto item = object.find("backgroundColor"); item != object.end() && !item->is_null()) readout->BackgroundColor = RgbaColorFromJson(*item);
                widget->AdditionalProperties = Extensions(object, { "key", "widgetType", "metrics", "labelIncludeDeviceId", "labelIncludeDeviceName", "showLabel", "fontSize", "fontColor", "backgroundColor" });
            }
            return widget;
        }

        Json ToJson(const LoadoutFile& value, bool persistence)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["signature"] = ToJson(value.Signature);
            result["widgets"] = Json::array();
            for (const auto& widget : value.Widgets) {
                if (!widget) throw JsonError("Unsupported widget type.");
                result["widgets"].push_back(ToJson(*widget, persistence));
            }
            return result;
        }

        LoadoutFile LoadoutFileFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Configuration must be a JSON object.");
            LoadoutFile value;
            const auto signature = object.find("signature");
            if (signature == object.end() || signature->is_null()) throw JsonError("Missing configuration signature.");
            value.Signature = FileSignatureFromJson(*signature);
            const auto widgets = object.find("widgets");
            if (widgets == object.end() || !widgets->is_array()) throw JsonError("Missing loadout widgets.");
            value.Widgets.clear();
            for (const auto& widget : *widgets) value.Widgets.push_back(WidgetFromJson(widget));
            value.AdditionalProperties = Extensions(object, { "signature", "widgets" });
            return value;
        }

        Json ToJson(const PreferenceFile& value)
        {
            auto result = BeginObject(value.AdditionalProperties);
            result["signature"] = ToJson(value.Signature);
            result["preferences"] = ToJson(value.Preferences);
            result["hotkeyBindings"] = Json::object();
            for (const auto& [name, binding] : value.HotkeyBindings) result["hotkeyBindings"][name] = ToJson(binding);
            return result;
        }

        PreferenceFile PreferenceFileFromJson(const Json& object)
        {
            if (!object.is_object()) throw JsonError("Configuration must be a JSON object.");
            PreferenceFile value;
            const auto signature = object.find("signature");
            if (signature == object.end() || signature->is_null()) throw JsonError("Missing configuration signature.");
            value.Signature = FileSignatureFromJson(*signature);
            const auto preferences = object.find("preferences");
            if (preferences == object.end() || preferences->is_null()) throw JsonError("Missing preferences.");
            value.Preferences = PreferencesFromJson(*preferences);
            const auto bindings = object.find("hotkeyBindings");
            if (bindings == object.end() || !bindings->is_object()) throw JsonError("Missing hotkey bindings.");
            value.HotkeyBindings.clear();
            for (const auto& item : bindings->items()) value.HotkeyBindings.emplace(item.key(), HotkeyBindingFromJson(item.value()));
            value.AdditionalProperties = Extensions(object, { "signature", "preferences", "hotkeyBindings" });
            return value;
        }

        Json ParseRoot(const std::string& text, const char* code, const char* minimumVersion, const char* currentVersion)
        {
            Json root;
            try { root = Json::parse(text); }
            catch (const nlohmann::json::exception&) { throw JsonError("Configuration must be a JSON object."); }
            if (!root.is_object()) throw JsonError("Configuration must be a JSON object.");
            const auto signature = root.find("signature");
            if (signature == root.end() || !signature->is_object() || signature->value("code", std::string{}) != code) {
                throw JsonError("Wrong configuration file type.");
            }
            const auto version = ParseVersion(signature->value("version", std::string{}));
            if (version < ParseVersion(minimumVersion)) throw JsonError("Configuration version is too old to migrate.");
            if (version > ParseVersion(currentVersion)) throw JsonError("Configuration was written by a newer version.");
            return root;
        }

        bool Before(const Version& value, const char* target)
        {
            return value < ParseVersion(target);
        }

        void NormalizeLoadout(LoadoutFile& file, const IntrospectionData& intro, const Preferences& preferences)
        {
            file.Widgets.erase(std::remove_if(file.Widgets.begin(), file.Widgets.end(), [](const auto& widget) {
                return !widget || widget->Metrics.empty();
            }), file.Widgets.end());
            Widget::NormalizeKeys(file.Widgets);
            for (const auto& widget : file.Widgets) {
                for (auto& line : widget->Metrics) {
                    const auto metric = std::find_if(intro.Metrics.begin(), intro.Metrics.end(), [&line](const Metric& candidate) {
                        return candidate.Id == line.Metric.MetricId;
                    });
                    if (metric != intro.Metrics.end()) MetricResolver::Normalize(*metric, line.Metric, intro, preferences);
                }
            }
        }
    }

    Preferences ConfigurationJson::Clone(const Preferences& value)
    {
        return PreferencesFromJson(ToJson(value));
    }

    LoadoutFile ConfigurationJson::Clone(const LoadoutFile& value)
    {
        return LoadoutFileFromJson(ToJson(value, true));
    }

    PreferenceFile PreferenceDocument::Parse(const std::string& text, const IntrospectionData& intro)
    {
        auto root = ParseRoot(text, "p2c-cap-pref", "0.16.0", CurrentVersion);
        const auto version = ParseVersion(root.at("signature").value("version", std::string{}));
        auto& preferences = root["preferences"];
        if (!preferences.is_object()) throw JsonError("Missing preferences.");
        if (Before(version, "0.17.0")) {
            const auto sampling = Number(preferences, "samplingPeriodMs", 25.0);
            const auto samples = Number(preferences, "samplesPerFrame", 4.0);
            if (sampling <= 0.0 || samples <= 0.0) throw JsonError("Invalid legacy sampling rate.");
            preferences["metricPollRate"] = std::round(1000.0 / sampling);
            preferences["overlayDrawRate"] = std::round(1000.0 / (sampling * samples));
            preferences.erase("samplingPeriodMs");
            preferences.erase("samplesPerFrame");
        }
        if (Before(version, "0.18.0")) {
            preferences["manualEtwFlush"] = true;
            preferences["etwFlushPeriod"] = 8;
            preferences["metricsOffset"] = 150;
        }
        if (Before(version, "0.20.0") && Number(preferences, "metricsOffset", 150.0) <= 32.0) preferences["metricsOffset"] = 150;
        if (Before(version, "0.21.0") && Number(preferences, "metricsOffset", 150.0) == 80.0) preferences["metricsOffset"] = 150;
        if (Before(version, "1.0.0") && !preferences.contains("enablePerMetricDeviceSelection")) preferences["enablePerMetricDeviceSelection"] = false;
        if (Before(version, "1.0.1")) {
            const auto adapter = (int)Number(preferences, "adapterId", 0.0);
            const auto exists = std::any_of(intro.Adapters.begin(), intro.Adapters.end(), [adapter](const Adapter& candidate) { return candidate.Id == adapter; });
            if (adapter == 0 || (!intro.Adapters.empty() && !exists)) {
                auto minimum = 0;
                if (!intro.Adapters.empty()) {
                    minimum = std::min_element(intro.Adapters.begin(), intro.Adapters.end(), [](const Adapter& left, const Adapter& right) {
                        return left.Id < right.Id;
                    })->Id;
                }
                preferences["adapterId"] = minimum;
            }
        }
        if (Before(version, "1.1.0")) {
            for (auto it = preferences.begin(); it != preferences.end();) {
                if (it.key() == "enableFlashInjection" || it.key().starts_with("flashInjection")) it = preferences.erase(it);
                else ++it;
            }
        }
        root["signature"]["version"] = CurrentVersion;
        auto file = PreferenceFileFromJson(root);
        for (const auto action : { HotkeyAction::ToggleCapture, HotkeyAction::ToggleOverlay, HotkeyAction::CyclePreset, HotkeyAction::ToggleEtlLogging }) {
            const auto name = action == HotkeyAction::ToggleCapture ? "ToggleCapture" : action == HotkeyAction::ToggleOverlay ? "ToggleOverlay" : action == HotkeyAction::CyclePreset ? "CyclePreset" : "ToggleEtlLogging";
            if (!file.HotkeyBindings.contains(name)) file.HotkeyBindings.emplace(name, HotkeyBinding{ action, std::nullopt });
        }
        ConfigurationValidation::ValidatePreferences(file.Preferences);
        return file;
    }

    std::string PreferenceDocument::Serialize(const PreferenceFile& file)
    {
        ConfigurationValidation::ValidatePreferences(file.Preferences);
        auto output = file;
        output.Signature = FileSignature{ "p2c-cap-pref", CurrentVersion };
        return ToJson(output).dump(2);
    }

    LoadoutFile LoadoutDocument::Parse(const std::string& text, const IntrospectionData& intro, const Preferences& preferences)
    {
        auto root = ParseRoot(text, "p2c-cap-load", "0.13.0", CurrentVersion);
        const auto version = ParseVersion(root.at("signature").value("version", std::string{}));
        auto& widgets = root["widgets"];
        if (!widgets.is_array()) throw JsonError("Missing loadout widgets.");
        for (auto& widget : widgets) {
            if (!widget.is_object()) throw JsonError("Invalid loadout widget.");
            auto& lines = widget["metrics"];
            if (!lines.is_array()) throw JsonError("Missing widget metrics.");
            if (Before(version, "1.0.0")) {
                const auto first = lines.empty() ? nullptr : &lines.front();
                widget["labelIncludeDeviceId"] = first && first->is_object() ? first->value("labelIncludeDeviceId", false) : false;
                widget["labelIncludeDeviceName"] = first && first->is_object() ? first->value("labelIncludeDeviceName", false) : false;
            }
            for (auto& line : lines) {
                if (!line.is_object()) throw JsonError("Invalid metric line.");
                auto& metric = line["metric"];
                if (!metric.is_object()) throw JsonError("Missing qualified metric.");
                if (Before(version, "0.14.0")) {
                    const auto id = metric.value("metricId", 0);
                    if (id == 4 || id == 5 || (id >= 56 && id <= 61)) metric["deviceId"] = MetricResolver::SystemDeviceId;
                    if (metric.value("statId", 0) == 10) metric["statId"] = 12;
                    if (id == 57) metric["statId"] = 0;
                }
                metric.erase("desiredUnitId");
                line.erase("labelIncludeDeviceId");
                line.erase("labelIncludeDeviceName");
            }
        }
        root["signature"]["version"] = CurrentVersion;
        auto file = LoadoutFileFromJson(root);
        NormalizeLoadout(file, intro, preferences);
        ConfigurationValidation::ValidateWidgets(file.Widgets);
        return file;
    }

    LoadoutFile LoadoutDocument::FromWidgets(const std::vector<std::shared_ptr<Widget>>& widgets)
    {
        LoadoutFile file;
        file.Widgets = widgets;
        return file;
    }

    std::string LoadoutDocument::Serialize(const LoadoutFile& file)
    {
        ConfigurationValidation::ValidateWidgets(file.Widgets);
        auto output = file;
        output.Signature = FileSignature{ "p2c-cap-load", CurrentVersion };
        return ToJson(output, true).dump(2);
    }
}
