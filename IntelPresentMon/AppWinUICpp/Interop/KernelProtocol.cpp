// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "KernelProtocol.h"

#include <cmath>
#include <format>
#include <limits>
#include <memory>

namespace pmon::ui::interop
{
    namespace
    {
        uint32_t Unsigned(double value)
        {
            if (!std::isfinite(value) || value < 0 || value > (double)std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("The native setting requires a finite, nonnegative value.");
            }
            return (uint32_t)value;
        }

        void WriteColor(CerealWriter& writer, const core::RgbaColor& color)
        {
            if (color.R < 0 || color.R > 255 || color.G < 0 || color.G > 255 || color.B < 0 || color.B > 255 ||
                !std::isfinite(color.A) || color.A < 0 || color.A > 1) {
                throw std::out_of_range("Invalid overlay color.");
            }
            writer.Float((float)color.R / 255.0f);
            writer.Float((float)color.G / 255.0f);
            writer.Float((float)color.B / 255.0f);
            writer.Float((float)color.A);
        }

        void WriteRange(CerealWriter& writer, const std::vector<double>& values)
        {
            if (values.size() != 2) {
                throw std::invalid_argument("A graph range must contain exactly two values.");
            }
            for (const auto value : values) {
                if (!std::isfinite(value) || value < (double)std::numeric_limits<int>::min() ||
                    value > (double)std::numeric_limits<int>::max()) {
                    throw std::out_of_range("Native graph range limits must be finite integers.");
                }
                writer.Int32((int)value);
            }
        }

        void WriteWidgetMetric(CerealWriter& writer, const core::WidgetMetric& value)
        {
            if (!value.Metric.DeviceId) {
                throw std::invalid_argument("A metric device must be resolved before sending the specification.");
            }
            if (value.Metric.ArrayIndex < 0) {
                throw std::out_of_range("Metric array index cannot be negative.");
            }
            writer.Int32(value.Metric.MetricId);
            writer.UInt32((uint32_t)value.Metric.ArrayIndex);
            writer.UInt32((uint32_t)*value.Metric.DeviceId);
            writer.Int32(value.Metric.StatId);
            writer.Int32(value.Metric.DesiredUnitId.value_or(0));
            WriteColor(writer, value.LineColor);
            WriteColor(writer, value.FillColor);
            writer.Int32((int)value.AxisAffinity);
        }

        void WriteWidget(CerealWriter& writer, const core::Widget& widget)
        {
            if (widget.GetWidgetType() == core::WidgetType::Graph) {
                const auto& graph = dynamic_cast<const core::Graph&>(widget);
                writer.Int32(0);
                writer.List(graph.Metrics, [&writer](const core::WidgetMetric& value) { WriteWidgetMetric(writer, value); });
                writer.UInt32(Unsigned(graph.Height));
                if (graph.VDivs < 0 || graph.HDivs < 0 || graph.GraphType.BinCount < 0) {
                    throw std::out_of_range("Graph counts cannot be negative.");
                }
                writer.UInt32((uint32_t)graph.VDivs);
                writer.UInt32((uint32_t)graph.HDivs);
                writer.Bool(graph.ShowBottomAxis);
                writer.String(graph.GraphType.Name);
                WriteRange(writer, graph.GraphType.Range);
                WriteRange(writer, graph.GraphType.RangeRight);
                writer.UInt32((uint32_t)graph.GraphType.BinCount);
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
            }
            else if (widget.GetWidgetType() == core::WidgetType::Readout) {
                const auto& readout = dynamic_cast<const core::Readout&>(widget);
                writer.Int32(1);
                writer.List(readout.Metrics, [&writer](const core::WidgetMetric& value) { WriteWidgetMetric(writer, value); });
                writer.Bool(readout.ShowLabel);
                writer.Float((float)readout.FontSize);
                WriteColor(writer, readout.FontColor);
                WriteColor(writer, readout.BackgroundColor);
            }
            else {
                throw std::invalid_argument("Unsupported overlay widget type.");
            }
            writer.Bool(widget.LabelIncludeDeviceId);
            writer.Bool(widget.LabelIncludeDeviceName);
        }
    }

    KernelRequestException::KernelRequestException(std::string identifier, int transportStatus, int executionStatus)
        : std::runtime_error(std::format("Kernel request {} failed (transport {}, execution {}).", identifier, transportStatus, executionStatus)),
          identifier_(std::move(identifier)), transportStatus_(transportStatus), executionStatus_(executionStatus) {}

    const std::string& KernelRequestException::Identifier() const noexcept { return identifier_; }
    int KernelRequestException::TransportStatus() const noexcept { return transportStatus_; }
    int KernelRequestException::ExecutionStatus() const noexcept { return executionStatus_; }

    std::vector<uint8_t> KernelProtocol::Request(const std::string& identifier, uint32_t commandToken,
        const std::function<void(CerealWriter&)>& payload)
    {
        CerealWriter writer;
        writer.String(identifier);
        writer.UInt32(commandToken);
        writer.Int32(0);
        writer.Int32(0);
        writer.Int32(0);
        writer.UInt16(1);
        writer.UInt16(1);
        payload(writer);
        return writer.Packet();
    }

    KernelPacketHeader KernelProtocol::Header(CerealReader& reader)
    {
        return { reader.String(), reader.UInt32(), reader.Int32(), reader.Int32(), reader.Int32(), reader.UInt16(), reader.UInt16() };
    }

    void KernelProtocol::ValidateResponse(const KernelPacketHeader& header, const std::string& identifier, uint32_t token)
    {
        if (header.Identifier != identifier || header.CommandToken != token || header.PacketType != 0 ||
            header.HeaderVersion != 1 || header.ActionVersion != 1 || header.TransportStatus < 0 ||
            header.TransportStatus > 2 || (header.TransportStatus == 0 && header.ExecutionStatus != 0)) {
            throw ProtocolError("Kernel response does not match the request or protocol version.");
        }
    }

    KernelSessionInfo KernelProtocol::Session(CerealReader& reader)
    {
        return { reader.UInt32(), reader.String(), reader.String(), reader.String(), reader.String() };
    }

    KernelEvent KernelProtocol::Event(const KernelPacketHeader& header, CerealReader& reader)
    {
        if (header.PacketType != 2 || header.HeaderVersion != 1 || header.ActionVersion != 1 ||
            header.TransportStatus != 0 || header.ExecutionStatus != 0) {
            throw ProtocolError("Unsupported kernel event header.");
        }
        KernelEvent result{};
        if (header.Identifier == "HotkeyFiredAction") {
            result = { KernelEventKind::HotkeyFired, reader.Int32(), std::nullopt };
        }
        else if (header.Identifier == "TargetLostAction") {
            result = { KernelEventKind::TargetLost, std::nullopt, reader.UInt32() };
        }
        else if (header.Identifier == "PresentmonInitFailedAction") {
            result = { KernelEventKind::PresentmonInitFailed, std::nullopt, std::nullopt };
        }
        else if (header.Identifier == "OverlayDiedAction") {
            result = { KernelEventKind::OverlayDied, std::nullopt, std::nullopt };
        }
        else if (header.Identifier == "StalePidAction") {
            result = { KernelEventKind::StalePid, std::nullopt, std::nullopt };
        }
        else {
            throw ProtocolError("Unsupported kernel event: " + header.Identifier);
        }
        reader.RequireEnd();
        return result;
    }

    IntrospectionData KernelProtocol::Introspection(CerealReader& reader)
    {
        IntrospectionData result;
        result.Metrics = reader.List<Metric>([&reader] {
            Metric metric;
            metric.Id = reader.Int32();
            metric.Name = reader.String();
            metric.Description = reader.String();
            metric.DeviceType = (MetricDeviceType)reader.Int32();
            metric.PreferredUnitId = reader.Int32();
            metric.DeviceAvailability = reader.List<MetricDeviceAvailability>([&reader] {
                const auto deviceId = reader.UInt32();
                if (deviceId > (uint32_t)std::numeric_limits<int>::max()) {
                    throw ProtocolError("Device ID does not fit in an int.");
                }
                return MetricDeviceAvailability{ (int)deviceId, reader.Int32(), reader.Int32() };
            }, 12);
            metric.AvailableStatIds = reader.List<int>([&reader] { return reader.Int32(); }, 4);
            metric.Numeric = reader.Bool();
            return metric;
        }, 45);
        result.Stats = reader.List<MetricStat>([&reader] {
            return MetricStat{ reader.Int32(), reader.String(), reader.String(), reader.String() };
        }, 28);
        result.Units = reader.List<MetricUnit>([] { return MetricUnit{}; }, 0);
        result.Adapters = reader.List<Adapter>([&reader] {
            const auto id = reader.UInt32();
            if (id > (uint32_t)std::numeric_limits<int>::max()) {
                throw ProtocolError("Adapter ID does not fit in an int.");
            }
            return Adapter{ (int)id, reader.String(), reader.String() };
        }, 20);
        const auto systemDeviceId = reader.UInt32();
        const auto defaultAdapterId = reader.UInt32();
        if (systemDeviceId > (uint32_t)std::numeric_limits<int>::max() ||
            defaultAdapterId > (uint32_t)std::numeric_limits<int>::max()) {
            throw ProtocolError("Introspection device ID does not fit in an int.");
        }
        result.SystemDeviceId = (int)systemDeviceId;
        result.DefaultAdapterId = (int)defaultAdapterId;
        result.MetricAvailabilityReasons = reader.List<MetricAvailabilityReason>([&reader] {
            return MetricAvailabilityReason{ reader.Int32(), reader.String() };
        }, 12);
        return result;
    }

    void KernelProtocol::Specification(CerealWriter& writer, const core::Specification& specification)
    {
        writer.OptionalUInt32(specification.Pid);
        const auto& preferences = specification.Preferences;
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
        writer.List(specification.Widgets, [&writer](const std::shared_ptr<core::Widget>& widget) {
            if (!widget) {
                throw std::invalid_argument("Specification cannot contain a null widget.");
            }
            WriteWidget(writer, *widget);
        });
    }
}
