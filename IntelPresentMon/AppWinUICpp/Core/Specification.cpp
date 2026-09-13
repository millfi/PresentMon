// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "Specification.h"

#include "Introspection.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pmon::ui::core
{
    namespace
    {
        void Positive(double value, const char* name)
        {
            if (!std::isfinite(value) || value <= 0.) {
                throw std::invalid_argument(std::string{ name } + " must be greater than zero.");
            }
        }

        void Nonnegative(double value, const char* name)
        {
            if (!std::isfinite(value) || value < 0.) {
                throw std::invalid_argument(std::string{ name } + " cannot be negative.");
            }
        }

        void ValidateColor(const RgbaColor& color)
        {
            if (color.R < 0 || color.R > 255 || color.G < 0 || color.G > 255 || color.B < 0 || color.B > 255
                || !std::isfinite(color.A) || color.A < 0. || color.A > 1.) {
                throw std::invalid_argument("Colors require RGB values from 0 to 255 and opacity from 0 to 1.");
            }
        }

        void ValidateRange(const std::vector<double>& range, const char* name)
        {
            if (range.size() != 2 || !std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0] > range[1]) {
                throw std::invalid_argument(std::string{ name } + " must contain an ordered minimum and maximum.");
            }
        }

        bool ValidPreset(Preset preset)
        {
            return preset == Preset::Basic || preset == Preset::GameExperience || preset == Preset::GpuFocus
                || preset == Preset::PowerTemperature || preset == Preset::Custom;
        }

        const Metric* FindMetric(const IntrospectionData& intro, int id)
        {
            const auto it = std::find_if(intro.Metrics.begin(), intro.Metrics.end(),
                [id](const auto& metric) { return metric.Id == id; });
            return it == intro.Metrics.end() ? nullptr : &*it;
        }
    }

    Specification SpecificationBuilder::Build(std::optional<int> pid, const Preferences& preferences,
        const std::vector<std::shared_ptr<Widget>>& widgets, const IntrospectionData& intro)
    {
        ConfigurationValidation::ValidatePreferences(preferences);

        std::vector<std::shared_ptr<Widget>> surviving;
        surviving.reserve(widgets.size());
        for (const auto& original : widgets) {
            if (!original) {
                continue;
            }
            auto widget = original->Clone();
            auto& metrics = widget->Metrics;
            metrics.erase(std::remove_if(metrics.begin(), metrics.end(), [&](auto& line) {
                const auto* metric = FindMetric(intro, line.Metric.MetricId);
                if (!metric) {
                    return true;
                }
                if (!preferences.EnablePerMetricDeviceSelection && metric->DeviceType == MetricDeviceType::GraphicsAdapter) {
                    line.Metric.DeviceId.reset();
                }
                MetricResolver::Normalize(*metric, line.Metric, intro, preferences);
                if (metric->DeviceType == MetricDeviceType::GraphicsAdapter) {
                    const auto deviceId = MetricResolver::ResolveDeviceId(*metric, line.Metric, intro, preferences);
                    if (deviceId == 0) {
                        return true;
                    }
                    line.Metric.DeviceId = deviceId;
                }
                return false;
            }), metrics.end());
            if (!preferences.EnablePerMetricDeviceSelection) {
                widget->LabelIncludeDeviceId = false;
                widget->LabelIncludeDeviceName = false;
            }
            if (!metrics.empty()) {
                surviving.push_back(std::move(widget));
            }
        }
        ConfigurationValidation::ValidateWidgets(surviving);
        return Specification{ pid, preferences, std::move(surviving) };
    }

    void ConfigurationValidation::ValidatePreferences(const Preferences& preferences)
    {
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
        if (preferences.EnableCaptureDuration) {
            Positive(preferences.CaptureDuration, "Capture duration");
        }
        else {
            Nonnegative(preferences.CaptureDuration, "Capture duration");
        }
        if (preferences.SelectedPreset && !ValidPreset(*preferences.SelectedPreset)) {
            throw std::invalid_argument("Unknown preset selection.");
        }
        if (preferences.OverlayPosition != OverlayPosition::TopLeft && preferences.OverlayPosition != OverlayPosition::TopRight
            && preferences.OverlayPosition != OverlayPosition::BottomLeft && preferences.OverlayPosition != OverlayPosition::BottomRight) {
            throw std::invalid_argument("Unknown overlay position.");
        }
        ValidateColor(preferences.OverlayBackgroundColor);
        ValidateColor(preferences.OverlayBorderColor);
    }

    void ConfigurationValidation::ValidateWidgets(const std::vector<std::shared_ptr<Widget>>& widgets)
    {
        for (const auto& widget : widgets) {
            if (!widget) {
                throw std::invalid_argument("Unknown widget type.");
            }
            for (const auto& line : widget->Metrics) {
                ValidateColor(line.LineColor);
                ValidateColor(line.FillColor);
                if (line.AxisAffinity != AxisAffinity::Left && line.AxisAffinity != AxisAffinity::Right) {
                    throw std::invalid_argument("Unknown metric axis.");
                }
            }
            if (const auto* graph = dynamic_cast<const Graph*>(widget.get())) {
                Positive(graph->Height, "Graph height");
                Positive(graph->TextSize, "Graph text size");
                Positive(graph->VDivs, "Vertical grid divisions");
                Positive(graph->HDivs, "Horizontal grid divisions");
                if (graph->GraphType.Name != "Line" && graph->GraphType.Name != "Histogram") {
                    throw std::invalid_argument("Unknown graph style.");
                }
                Positive(graph->GraphType.BinCount, "Histogram bins");
                ValidateRange(graph->GraphType.Range, "Left value range");
                ValidateRange(graph->GraphType.RangeRight, "Right value range");
                ValidateRange(graph->GraphType.CountRange, "Histogram count range");
                ValidateColor(graph->GridColor);
                ValidateColor(graph->DividerColor);
                ValidateColor(graph->BackgroundColor);
                ValidateColor(graph->BorderColor);
                ValidateColor(graph->TextColor);
            }
            else if (const auto* readout = dynamic_cast<const Readout*>(widget.get())) {
                Positive(readout->FontSize, "Readout font size");
                ValidateColor(readout->FontColor);
                ValidateColor(readout->BackgroundColor);
            }
            else {
                throw std::invalid_argument("Unknown widget type.");
            }
        }
    }
}
