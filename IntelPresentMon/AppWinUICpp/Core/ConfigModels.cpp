// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "ConfigModels.h"

#include <algorithm>
#include <unordered_set>

namespace pmon::ui::core
{
    namespace
    {
        std::atomic_int widgetKey{ 0 };
        std::atomic_int metricKey{ 0 };

        int NextWidgetKey()
        {
            return ++widgetKey;
        }

        int NextMetricKey()
        {
            return ++metricKey;
        }
    }

    bool HotkeyCombination::Matches(const HotkeyCombination* other) const
    {
        if (!other || Key != other->Key || Modifiers.size() != other->Modifiers.size()) {
            return false;
        }
        std::unordered_multiset<int> left{ Modifiers.begin(), Modifiers.end() };
        std::unordered_multiset<int> right{ other->Modifiers.begin(), other->Modifiers.end() };
        return left == right;
    }

    std::unordered_map<std::string, HotkeyBinding> HotkeyBinding::CreateDefaults()
    {
        std::unordered_map<std::string, HotkeyBinding> bindings;
        bindings.emplace("ToggleCapture", HotkeyBinding{ HotkeyAction::ToggleCapture, HotkeyCombination{ 42, { 2, 4 } } });
        bindings.emplace("ToggleOverlay", HotkeyBinding{ HotkeyAction::ToggleOverlay, HotkeyCombination{ 46, { 2, 4 } } });
        bindings.emplace("CyclePreset", HotkeyBinding{ HotkeyAction::CyclePreset, HotkeyCombination{ 47, { 2, 4 } } });
        bindings.emplace("ToggleEtlLogging", HotkeyBinding{ HotkeyAction::ToggleEtlLogging, std::nullopt });
        return bindings;
    }

    Preferences Preferences::CreateDefault(const std::vector<int>& adapterIds)
    {
        Preferences preferences;
        preferences.SelectedPreset = Preset::Basic;
        if (!adapterIds.empty()) {
            preferences.AdapterId = *std::min_element(adapterIds.begin(), adapterIds.end());
        }
        return preferences;
    }

    Preferences Preferences::CreateDefault(const IntrospectionData& introspection)
    {
        std::vector<int> adapterIds;
        adapterIds.reserve(introspection.Adapters.size());
        for (const auto& adapter : introspection.Adapters) {
            adapterIds.push_back(adapter.Id);
        }
        return CreateDefault(adapterIds);
    }

    Preset Preferences::NextPreset(std::optional<Preset> current)
    {
        if (!current || (int)*current >= (int)Preset::PowerTemperature) {
            return Preset::Basic;
        }
        return (Preset)((int)*current + 1);
    }

    Widget::Widget()
        : Key{ NextWidgetKey() }
    {}

    void Widget::NormalizeKeys(const std::vector<std::shared_ptr<Widget>>& widgets)
    {
        for (const auto& widget : widgets) {
            if (!widget) {
                continue;
            }
            widget->Key = NextWidgetKey();
            for (auto& line : widget->Metrics) {
                line.RegenerateKey();
            }
        }
    }

    std::shared_ptr<Graph> Widget::CreateGraph(QualifiedMetric metric)
    {
        auto graph = std::make_shared<Graph>();
        graph->Metrics.emplace_back();
        graph->Metrics.back().Metric = std::move(metric);
        return graph;
    }

    std::shared_ptr<Readout> Widget::CreateReadout(QualifiedMetric metric)
    {
        auto readout = std::make_shared<Readout>();
        readout->Metrics.emplace_back();
        readout->Metrics.back().Metric = std::move(metric);
        return readout;
    }

    WidgetType Graph::GetWidgetType() const
    {
        return WidgetType::Graph;
    }

    std::shared_ptr<Widget> Graph::Clone() const
    {
        return std::make_shared<Graph>(*this);
    }

    WidgetType Readout::GetWidgetType() const
    {
        return WidgetType::Readout;
    }

    std::shared_ptr<Widget> Readout::Clone() const
    {
        return std::make_shared<Readout>(*this);
    }

    WidgetMetric::WidgetMetric()
        : Key{ NextMetricKey() }
    {}

    void WidgetMetric::RegenerateKey()
    {
        Key = NextMetricKey();
    }
}
