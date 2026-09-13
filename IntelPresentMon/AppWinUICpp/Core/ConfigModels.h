// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace pmon::ui::core
{
    using Json = nlohmann::json;

    struct IntrospectionData;

    enum class Preset { Basic = 0, GameExperience = 1, GpuFocus = 2, PowerTemperature = 3, Custom = 1000 };
    enum class OverlayPosition { TopLeft, TopRight, BottomLeft, BottomRight };
    enum class HotkeyAction { ToggleCapture, ToggleOverlay, CyclePreset, ToggleEtlLogging };
    enum class WidgetType { Graph, Readout };
    enum class AxisAffinity { Left, Right };
    enum class MetricDeviceType { Independent, GraphicsAdapter, System };

    struct RgbaColor
    {
        int R = 0;
        int G = 0;
        int B = 0;
        double A = 1.0;
        Json AdditionalProperties = Json::object();
    };

    struct GraphFont
    {
        std::string Name = "Verdana";
        double AxisSize = 10.0;
        Json AdditionalProperties = Json::object();
    };

    struct HotkeyCombination
    {
        int Key = 0;
        std::vector<int> Modifiers;
        Json AdditionalProperties = Json::object();

        bool Matches(const HotkeyCombination* other) const;
    };

    struct HotkeyBinding
    {
        HotkeyAction Action = HotkeyAction::ToggleCapture;
        std::optional<HotkeyCombination> Combination;
        Json AdditionalProperties = Json::object();

        static std::unordered_map<std::string, HotkeyBinding> CreateDefaults();
    };

    struct FileSignature
    {
        std::string Code;
        std::string Version;
        Json AdditionalProperties = Json::object();
    };

    struct Preferences
    {
        std::optional<Preset> SelectedPreset;
        std::string CapturePath;
        double CaptureDelay = 1.0;
        bool EnableCaptureDelay = false;
        double CaptureDuration = 10.0;
        bool EnableCaptureDuration = false;
        bool HideDuringCapture = true;
        bool HideAlways = false;
        bool EnablePerMetricDeviceSelection = false;
        bool IndependentWindow = false;
        double MetricPollRate = 40.0;
        double OverlayDrawRate = 10.0;
        double TelemetrySamplingPeriodMs = 100.0;
        double EtwFlushPeriod = 8.0;
        bool ManualEtwFlush = true;
        double MetricsOffset = 150.0;
        double MetricsWindow = 1000.0;
        OverlayPosition OverlayPosition = OverlayPosition::TopLeft;
        double TimeRange = 10.0;
        double OverlayWidth = 400.0;
        bool Upscale = false;
        double UpscaleFactor = 2.0;
        bool GenerateStats = true;
        bool EnableTargetBlocklist = true;
        bool EnableAutotargetting = false;
        double OverlayMargin = 0.0;
        double OverlayBorder = 0.0;
        double OverlayPadding = 10.0;
        double GraphMargin = 2.0;
        double GraphBorder = 0.0;
        double GraphPadding = 5.0;
        RgbaColor OverlayBorderColor{ 255, 255, 255, 0.0 };
        RgbaColor OverlayBackgroundColor{ 50, 57, 91, 220.0 / 255.0 };
        GraphFont GraphFont;
        int AdapterId = 0;
        Json AdditionalProperties = Json::object();

        static Preferences CreateDefault(const std::vector<int>& adapterIds = {});
        static Preferences CreateDefault(const IntrospectionData& introspection);
        static Preset NextPreset(std::optional<Preset> current);
    };

    struct WidgetMetric;
    struct QualifiedMetric;
    struct Graph;
    struct Readout;

    struct Widget
    {
        int Key = 0;
        std::vector<WidgetMetric> Metrics;
        bool LabelIncludeDeviceId = false;
        bool LabelIncludeDeviceName = false;
        Json AdditionalProperties = Json::object();

        Widget();
        virtual ~Widget() = default;
        virtual WidgetType GetWidgetType() const = 0;
        virtual std::shared_ptr<Widget> Clone() const = 0;

        static void NormalizeKeys(const std::vector<std::shared_ptr<Widget>>& widgets);
        static std::shared_ptr<Graph> CreateGraph(QualifiedMetric metric);
        static std::shared_ptr<Readout> CreateReadout(QualifiedMetric metric);
    };

    struct GraphOptions
    {
        std::string Name = "Line";
        std::vector<double> Range{ 0.0, 150.0 };
        std::vector<double> RangeRight{ 0.0, 150.0 };
        int BinCount = 40;
        std::vector<double> CountRange{ 0.0, 1000.0 };
        bool AutoLeft = true;
        bool AutoRight = true;
        bool AutoCount = false;
        Json AdditionalProperties = Json::object();
    };

    struct Graph final : Widget
    {
        double Height = 80.0;
        int VDivs = 4;
        int HDivs = 40;
        bool ShowBottomAxis = false;
        GraphOptions GraphType;
        RgbaColor GridColor{ 47, 120, 190, 40.0 / 255.0 };
        RgbaColor DividerColor{ 57, 126, 150, 220.0 / 255.0 };
        RgbaColor BackgroundColor{ 0, 0, 0, 0.0 };
        RgbaColor BorderColor{ 0, 0, 0, 0.0 };
        RgbaColor TextColor{ 242, 242, 242, 1.0 };
        double TextSize = 11.0;

        WidgetType GetWidgetType() const override;
        std::shared_ptr<Widget> Clone() const override;
    };

    struct Readout final : Widget
    {
        bool ShowLabel = true;
        double FontSize = 12.0;
        RgbaColor FontColor{ 205, 211, 233, 1.0 };
        RgbaColor BackgroundColor{ 45, 50, 96, 0.4 };

        WidgetType GetWidgetType() const override;
        std::shared_ptr<Widget> Clone() const override;
    };

    struct QualifiedMetric
    {
        int MetricId = 0;
        int ArrayIndex = 0;
        std::optional<int> DeviceId;
        int StatId = 0;
        std::optional<int> DesiredUnitId;
        Json AdditionalProperties = Json::object();
    };

    struct WidgetMetric
    {
        int Key = 0;
        QualifiedMetric Metric;
        RgbaColor LineColor{ 100, 255, 255, 220.0 / 255.0 };
        RgbaColor FillColor{ 57, 210, 250, 25.0 / 255.0 };
        AxisAffinity AxisAffinity = AxisAffinity::Left;
        Json AdditionalProperties = Json::object();

        WidgetMetric();
        void RegenerateKey();
    };

    struct LoadoutFile
    {
        FileSignature Signature{ "p2c-cap-load", "1.0.0" };
        std::vector<std::shared_ptr<Widget>> Widgets;
        Json AdditionalProperties = Json::object();
    };

    struct PreferenceFile
    {
        FileSignature Signature{ "p2c-cap-pref", "1.1.0" };
        Preferences Preferences;
        std::unordered_map<std::string, HotkeyBinding> HotkeyBindings = HotkeyBinding::CreateDefaults();
        Json AdditionalProperties = Json::object();
    };

    struct MetricDeviceAvailability { int DeviceId = 0; int ArraySize = 0; int AvailabilityId = 0; };
    struct Adapter { int Id = 0; std::string Vendor; std::string Name; };
    struct MetricStat { int Id = 0; std::string Name; std::string ShortName; std::string Description; };
    struct MetricUnit { int Id = 0; std::string Name; std::string ShortName; std::string Description; };
    struct MetricAvailabilityReason { int Id = 0; std::string Description; };
    struct Metric
    {
        int Id = 0;
        std::string Name;
        std::string Description;
        std::vector<int> AvailableStatIds;
        int PreferredUnitId = 0;
        MetricDeviceType DeviceType = MetricDeviceType::Independent;
        std::vector<MetricDeviceAvailability> DeviceAvailability;
        bool Numeric = false;
    };
    struct IntrospectionData
    {
        std::vector<Metric> Metrics;
        std::vector<MetricStat> Stats;
        std::vector<MetricUnit> Units;
        std::vector<Adapter> Adapters;
        int SystemDeviceId = 0;
        int DefaultAdapterId = 0;
        std::vector<MetricAvailabilityReason> MetricAvailabilityReasons;
    };
}
