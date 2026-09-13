// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "CerealBinary.h"
#include "../Core/ConfigModels.h"
#include "../Core/Specification.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pmon::ui::interop
{
    struct KernelPacketHeader
    {
        std::string Identifier;
        uint32_t CommandToken = 0;
        int TransportStatus = 0;
        int ExecutionStatus = 0;
        int PacketType = 0;
        uint16_t HeaderVersion = 0;
        uint16_t ActionVersion = 0;
    };

    enum class KernelEventKind
    {
        HotkeyFired,
        TargetLost,
        PresentmonInitFailed,
        OverlayDied,
        StalePid,
    };

    struct KernelEvent
    {
        KernelEventKind Kind;
        std::optional<int> ActionId;
        std::optional<uint32_t> ProcessId;
    };

    struct KernelSessionInfo
    {
        uint32_t KernelPid = 0;
        std::string ServiceBuildId;
        std::string ServiceBuildTime;
        std::string ServiceVersion;
        std::string MiddlewareApiVersion;
    };

    class KernelRequestException : public std::runtime_error
    {
    public:
        KernelRequestException(std::string identifier, int transportStatus, int executionStatus);
        const std::string& Identifier() const noexcept;
        int TransportStatus() const noexcept;
        int ExecutionStatus() const noexcept;

    private:
        std::string identifier_;
        int transportStatus_ = 0;
        int executionStatus_ = 0;
    };

    using MetricDeviceAvailability = core::MetricDeviceAvailability;
    using Adapter = core::Adapter;
    using MetricStat = core::MetricStat;
    using MetricUnit = core::MetricUnit;
    using MetricAvailabilityReason = core::MetricAvailabilityReason;
    using MetricDeviceType = core::MetricDeviceType;
    using Metric = core::Metric;
    using IntrospectionData = core::IntrospectionData;

    class KernelProtocol
    {
    public:
        static constexpr uint32_t MaximumPacketBytes = 16 * 1024 * 1024;
        static constexpr uint32_t MinimumPacketBytes = 28;

        static std::vector<uint8_t> Request(const std::string& identifier, uint32_t commandToken,
            const std::function<void(CerealWriter&)>& payload);
        static KernelPacketHeader Header(CerealReader& reader);
        static void ValidateResponse(const KernelPacketHeader& header, const std::string& identifier, uint32_t token);
        static KernelSessionInfo Session(CerealReader& reader);
        static KernelEvent Event(const KernelPacketHeader& header, CerealReader& reader);
        static IntrospectionData Introspection(CerealReader& reader);
        static void Specification(CerealWriter& writer, const core::Specification& specification);
    };
}
