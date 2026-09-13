// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "ConfigModels.h"

#include <optional>
#include <string>

namespace pmon::ui::core
{
    class MetricResolver
    {
    public:
        static constexpr int SystemDeviceId = 65536;

        static int ArraySize(const Metric& metric, int deviceId);
        static int ClampArrayIndex(const Metric& metric, int deviceId, int index);
        static int ResolveDeviceId(const Metric& metric, const QualifiedMetric& qualified,
            const IntrospectionData& intro, const Preferences& preferences);
        static void Normalize(const Metric& metric, QualifiedMetric& qualified,
            const IntrospectionData& intro, const Preferences& preferences);
        static QualifiedMetric CreateQualified(const Metric& metric, const IntrospectionData& intro,
            const Preferences& preferences, std::optional<int> statId = std::nullopt);
        static QualifiedMetric CreateDefault(const IntrospectionData& intro, const Preferences& preferences);
        static bool IsAvailable(const Metric& metric, const QualifiedMetric& qualified,
            const IntrospectionData& intro, const Preferences& preferences);
        static std::optional<std::string> AvailabilityReason(const Metric& metric,
            const QualifiedMetric& qualified, const IntrospectionData& intro, const Preferences& preferences);
    };
}
