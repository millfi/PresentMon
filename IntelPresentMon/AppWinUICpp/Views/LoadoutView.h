// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace pmon::ui::core
{
    struct Metric;
    struct QualifiedMetric;
    struct RgbaColor;
    struct Widget;
    struct WidgetMetric;
}

namespace pmon::ui::services
{
    class AppSession;
}

namespace pmon::ui::views
{
    class LoadoutView
    {
    public:
        using AsyncCallback = std::function<winrt::Windows::Foundation::IAsyncAction()>;

        LoadoutView(std::shared_ptr<services::AppSession> session, AsyncCallback load, AsyncCallback save);

        winrt::Microsoft::UI::Xaml::UIElement Element() const;
        void SetEditingEnabled(bool enabled);
        void Refresh();

    private:
        using UIElement = winrt::Microsoft::UI::Xaml::UIElement;
        using Border = winrt::Microsoft::UI::Xaml::Controls::Border;
        using Grid = winrt::Microsoft::UI::Xaml::Controls::Grid;
        using NumberBox = winrt::Microsoft::UI::Xaml::Controls::NumberBox;

        UIElement BuildWidget(const std::shared_ptr<core::Widget>& widget);
        UIElement BuildSeries(const std::shared_ptr<core::Widget>& widget, int lineKey, int lineIndex);
        UIElement BuildDetails(const std::shared_ptr<core::Widget>& widget);
        UIElement BuildRange(const std::shared_ptr<core::Widget>& owner, const std::string& id,
            const std::string& title, const std::string& description, std::vector<double> values,
            bool automatic, std::function<void(bool)> setAutomatic, std::function<void(size_t, double)> setValue);
        UIElement NumberSetting(const std::string& id, const std::string& title, const std::string& description,
            double value, double minimum, double maximum, double step, std::function<void(double)> setValue);
        UIElement ColorSetting(const std::string& id, const std::string& title, const core::RgbaColor& color,
            std::function<void(core::RgbaColor)> setValue);
        UIElement Color(const std::string& id, const std::string& title, const core::RgbaColor& color,
            std::function<void(core::RgbaColor)> setValue);

        void AddWidget(bool graph);
        void ChangeType(const std::shared_ptr<core::Widget>& widget, bool graph);
        void SelectMetric(const std::shared_ptr<core::Widget>& widget, int lineKey, const core::Metric& metric);
        void MoveWidget(const std::shared_ptr<core::Widget>& widget, int offset);
        void RemoveWidget(const std::shared_ptr<core::Widget>& widget);
        void RemoveSeries(const std::shared_ptr<core::Widget>& widget, int lineKey);
        void SyncReorderedWidgets();
        void NotifyChanged();

        core::WidgetMetric* FindLine(const std::shared_ptr<core::Widget>& widget, int lineKey) const;
        bool IsCurrentWidget(const std::shared_ptr<core::Widget>& widget) const;
        const core::Metric* FindMetric(int id) const;
        const core::Metric* DefaultMetric(bool numericOnly) const;
        std::string WidgetTitle(const core::Widget& widget) const;
        std::string MetricLabel(const core::Metric& metric, const core::QualifiedMetric& selected) const;
        std::string DeviceLabel(const core::Metric& metric, const core::QualifiedMetric& selected,
            std::optional<int> deviceId, const std::string& label) const;

        static UIElement Field(const std::string& label, const UIElement& control);
        static Grid Fields(const std::vector<UIElement>& content, int maximumColumns);
        static winrt::Windows::UI::Color ToColor(const core::RgbaColor& color);
        static core::RgbaColor FromColor(winrt::Windows::UI::Color color);

        std::shared_ptr<services::AppSession> session_;
        AsyncCallback load_;
        AsyncCallback save_;
        std::unordered_set<int> expandedWidgets_;
        Grid root_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ListView list_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock summary_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::InfoBar notice_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::AppBarButton addGraph_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::AppBarButton addReadout_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::AppBarButton loadButton_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::AppBarButton saveButton_{ nullptr };
        bool editingEnabled_ = true;
        bool dragging_ = false;
    };
}
