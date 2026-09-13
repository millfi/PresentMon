// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "Introspection.h"

#include <algorithm>
#include <stdexcept>

namespace pmon::ui::core
{
    namespace
    {
        const MetricDeviceAvailability* FindDevice(const Metric& metric, int deviceId)
        {
            const auto it = std::find_if(metric.DeviceAvailability.begin(), metric.DeviceAvailability.end(),
                [deviceId](const auto& candidate) { return candidate.DeviceId == deviceId; });
            return it == metric.DeviceAvailability.end() ? nullptr : &*it;
        }

        bool Contains(const std::vector<int>& values, int value)
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }
    }

    int MetricResolver::ArraySize(const Metric& metric, int deviceId)
    {
        const auto* entry = FindDevice(metric, deviceId);
        return entry ? entry->ArraySize : 0;
    }

    int MetricResolver::ClampArrayIndex(const Metric& metric, int deviceId, int index)
    {
        return std::clamp(index, 0, std::max(0, ArraySize(metric, deviceId) - 1));
    }

    int MetricResolver::ResolveDeviceId(const Metric& metric, const QualifiedMetric& qualified,
        const IntrospectionData& intro, const Preferences& preferences)
    {
        switch (metric.DeviceType) {
        case MetricDeviceType::Independent:
            return 0;
        case MetricDeviceType::System:
            return intro.SystemDeviceId != 0 ? intro.SystemDeviceId : SystemDeviceId;
        case MetricDeviceType::GraphicsAdapter:
            return preferences.EnablePerMetricDeviceSelection && qualified.DeviceId && *qualified.DeviceId != 0
                ? *qualified.DeviceId : preferences.AdapterId;
        }
        return qualified.DeviceId.value_or(0);
    }

    void MetricResolver::Normalize(const Metric& metric, QualifiedMetric& qualified,
        const IntrospectionData& intro, const Preferences& preferences)
    {
        if (metric.DeviceType == MetricDeviceType::Independent) {
            qualified.DeviceId = 0;
        }
        else if (metric.DeviceType == MetricDeviceType::System) {
            qualified.DeviceId = intro.SystemDeviceId != 0 ? intro.SystemDeviceId : SystemDeviceId;
        }
        else if (metric.DeviceType == MetricDeviceType::GraphicsAdapter && qualified.DeviceId.value_or(0) == 0) {
            qualified.DeviceId.reset();
        }
        qualified.ArrayIndex = ClampArrayIndex(metric, ResolveDeviceId(metric, qualified, intro, preferences), qualified.ArrayIndex);
        qualified.DesiredUnitId = metric.PreferredUnitId;
    }

    QualifiedMetric MetricResolver::CreateQualified(const Metric& metric, const IntrospectionData& intro,
        const Preferences& preferences, std::optional<int> statId)
    {
        QualifiedMetric qualified;
        qualified.MetricId = metric.Id;
        qualified.StatId = statId && Contains(metric.AvailableStatIds, *statId)
            ? *statId : (metric.AvailableStatIds.empty() ? 0 : metric.AvailableStatIds.front());
        qualified.DeviceId = metric.DeviceType == MetricDeviceType::GraphicsAdapter
            ? std::nullopt : std::optional{ 0 };
        Normalize(metric, qualified, intro, preferences);
        return qualified;
    }

    QualifiedMetric MetricResolver::CreateDefault(const IntrospectionData& intro, const Preferences& preferences)
    {
        const auto preferred = std::find_if(intro.Metrics.begin(), intro.Metrics.end(),
            [](const auto& metric) { return metric.Id == 8 && metric.Numeric; });
        const auto numeric = preferred != intro.Metrics.end() ? preferred : std::find_if(intro.Metrics.begin(), intro.Metrics.end(),
            [](const auto& metric) { return metric.Numeric; });
        if (numeric == intro.Metrics.end()) {
            throw std::invalid_argument("No numeric metrics are available.");
        }
        return CreateQualified(*numeric, intro, preferences, 1);
    }

    bool MetricResolver::IsAvailable(const Metric& metric, const QualifiedMetric& qualified,
        const IntrospectionData& intro, const Preferences& preferences)
    {
        const auto* entry = FindDevice(metric, ResolveDeviceId(metric, qualified, intro, preferences));
        return entry && entry->AvailabilityId == 0 && qualified.ArrayIndex >= 0
            && (entry->ArraySize <= 0 ? qualified.ArrayIndex == 0 : qualified.ArrayIndex < entry->ArraySize);
    }

    std::optional<std::string> MetricResolver::AvailabilityReason(const Metric& metric,
        const QualifiedMetric& qualified, const IntrospectionData& intro, const Preferences& preferences)
    {
        if (IsAvailable(metric, qualified, intro, preferences)) {
            return std::nullopt;
        }
        const auto* entry = FindDevice(metric, ResolveDeviceId(metric, qualified, intro, preferences));
        const auto availabilityId = entry ? entry->AvailabilityId : 1;
        const auto reason = std::find_if(intro.MetricAvailabilityReasons.begin(), intro.MetricAvailabilityReasons.end(),
            [availabilityId](const auto& candidate) { return candidate.Id == availabilityId; });
        return reason == intro.MetricAvailabilityReasons.end()
            ? "This metric is not available on the selected device." : reason->Description;
    }
}
