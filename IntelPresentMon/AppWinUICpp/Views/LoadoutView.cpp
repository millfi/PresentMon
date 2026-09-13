// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "../pch.h"
#include "LoadoutView.h"

#include "../Core/ConfigModels.h"
#include "../Core/Introspection.h"
#include "../Services/AppSession.h"
#include "FormControls.h"

#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>

#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Automation;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

namespace pmon::ui::views
{
    namespace
    {
        hstring Text(const std::string& value)
        {
            return to_hstring(value);
        }

        bool Contains(const std::vector<int>& values, int value)
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        std::vector<std::string> MetricNames(const std::vector<const core::Metric*>& metrics,
            const std::function<std::string(const core::Metric&)>& label)
        {
            std::vector<std::string> names;
            names.reserve(metrics.size());
            for (const auto* metric : metrics) {
                names.push_back(label(*metric));
            }
            return names;
        }

        IInspectable StringItems(const std::vector<std::string>& values)
        {
            auto items = single_threaded_vector<IInspectable>();
            for (const auto& value : values) {
                items.Append(box_value(Text(value)));
            }
            return items;
        }

        std::optional<std::string> ItemText(const IInspectable& value)
        {
            if (!value) {
                return std::nullopt;
            }
            try {
                return to_string(unbox_value<hstring>(value));
            }
            catch (...) {
                return std::nullopt;
            }
        }

        Button IconButton(const std::string& id, const std::string& name, Symbol icon,
            const std::function<void()>& action, bool enabled = true)
        {
            Button button;
            button.Content(SymbolIcon{ icon });
            button.IsEnabled(enabled);
            button.Padding(Thickness{ 8.0, 8.0, 8.0, 8.0 });
            AutomationProperties::SetAutomationId(button, Text(id));
            AutomationProperties::SetName(button, Text(name));
            ToolTipService::SetToolTip(button, box_value(Text(name)));
            button.Click([action](auto const&, auto const&) { action(); });
            return button;
        }

        Button GlyphButton(const std::string& id, const std::string& name, const hstring& glyph,
            const std::function<void()>& action, bool enabled = true)
        {
            Button button;
            FontIcon icon;
            icon.Glyph(glyph);
            icon.FontFamily(FontFamily{ L"Segoe Fluent Icons" });
            button.Content(icon);
            button.IsEnabled(enabled);
            button.Padding(Thickness{ 8.0, 8.0, 8.0, 8.0 });
            AutomationProperties::SetAutomationId(button, Text(id));
            AutomationProperties::SetName(button, Text(name));
            ToolTipService::SetToolTip(button, box_value(Text(name)));
            button.Click([action](auto const&, auto const&) { action(); });
            return button;
        }

        fire_and_forget RunCommandAsync(AppBarButton button, InfoBar notice, std::string label,
            pmon::ui::views::LoadoutView::AsyncCallback action)
        {
            button.IsEnabled(false);
            try {
                co_await action();
            }
            catch (const hresult_error& error) {
                notice.Title(Text(label));
                notice.Message(error.message());
                notice.IsOpen(true);
            }
            catch (const std::exception& error) {
                notice.Title(Text(label));
                notice.Message(Text(error.what()));
                notice.IsOpen(true);
            }
            button.IsEnabled(true);
        }

        ColumnDefinition AutoColumn()
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::Auto());
            return column;
        }

        ColumnDefinition StarColumn()
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            return column;
        }

        RowDefinition AutoRow()
        {
            RowDefinition row;
            row.Height(GridLengthHelper::Auto());
            return row;
        }

        RowDefinition StarRow()
        {
            RowDefinition row;
            row.Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            return row;
        }
    }

    LoadoutView::LoadoutView(std::shared_ptr<services::AppSession> session, AsyncCallback load, AsyncCallback save)
        : session_{ std::move(session) }
        , load_{ std::move(load) }
        , save_{ std::move(save) }
    {
        root_ = Grid{};
        root_.RowSpacing(12.0);
        root_.RowDefinitions().Append(AutoRow());
        root_.RowDefinitions().Append(AutoRow());
        root_.RowDefinitions().Append(StarRow());
        notice_ = InfoBar{};
        notice_.IsOpen(false);
        notice_.IsClosable(true);
        notice_.Severity(InfoBarSeverity::Warning);

        CommandBar commands;
        commands.DefaultLabelPosition(CommandBarDefaultLabelPosition::Right);
        commands.HorizontalContentAlignment(HorizontalAlignment::Left);
        commands.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
        addGraph_ = AppBarButton{};
        addGraph_.Label(L"Add graph");
        addGraph_.Icon(SymbolIcon{ Symbol::Add });
        AutomationProperties::SetAutomationId(addGraph_, L"AddGraph");
        addGraph_.Click([this](auto const&, auto const&) { AddWidget(true); });
        addReadout_ = AppBarButton{};
        addReadout_.Label(L"Add readout");
        addReadout_.Icon(SymbolIcon{ Symbol::Font });
        AutomationProperties::SetAutomationId(addReadout_, L"AddReadout");
        addReadout_.Click([this](auto const&, auto const&) { AddWidget(false); });
        loadButton_ = AppBarButton{};
        loadButton_.Label(L"Open loadout");
        loadButton_.Icon(SymbolIcon{ Symbol::OpenFile });
        AutomationProperties::SetAutomationId(loadButton_, L"LoadLoadout");
        loadButton_.Click([button = loadButton_, notice = notice_, load = load_](auto const&, auto const&) {
            RunCommandAsync(button, notice, "Open loadout", load);
        });
        saveButton_ = AppBarButton{};
        saveButton_.Label(L"Save loadout");
        saveButton_.Icon(SymbolIcon{ Symbol::Save });
        AutomationProperties::SetAutomationId(saveButton_, L"SaveLoadout");
        saveButton_.Click([button = saveButton_, notice = notice_, save = save_](auto const&, auto const&) {
            RunCommandAsync(button, notice, "Save loadout", save);
        });
        commands.PrimaryCommands().Append(addGraph_);
        commands.PrimaryCommands().Append(addReadout_);
        commands.PrimaryCommands().Append(AppBarSeparator{});
        commands.PrimaryCommands().Append(loadButton_);
        commands.PrimaryCommands().Append(saveButton_);
        root_.Children().Append(commands);

        StackPanel messagePanel;
        messagePanel.Spacing(8.0);
        summary_ = FormControls::Description("");
        summary_.FontSize(14.0);
        AutomationProperties::SetAutomationId(summary_, L"LoadoutSummary");
        messagePanel.Children().Append(summary_);
        messagePanel.Children().Append(notice_);
        Grid::SetRow(messagePanel, 1);
        root_.Children().Append(messagePanel);

        list_ = ListView{};
        list_.CanDragItems(true);
        list_.CanReorderItems(true);
        list_.AllowDrop(true);
        list_.SelectionMode(ListViewSelectionMode::None);
        list_.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        list_.Padding(Thickness{ 0.0, 0.0, 12.0, 16.0 });
        ScrollViewer::SetHorizontalScrollMode(list_, ScrollMode::Disabled);
        ScrollViewer::SetHorizontalScrollBarVisibility(list_, ScrollBarVisibility::Disabled);
        AutomationProperties::SetAutomationId(list_, L"WidgetList");
        AutomationProperties::SetName(list_, L"Overlay widgets. Drag to reorder, or use each widget's move buttons.");
        list_.DragItemsStarting([this](auto const&, auto const&) { dragging_ = true; });
        list_.DragItemsCompleted([this](auto const&, auto const&) {
            dragging_ = false;
            SyncReorderedWidgets();
            NotifyChanged();
            Refresh();
        });
        Grid::SetRow(list_, 2);
        root_.Children().Append(list_);
        Refresh();
    }

    UIElement LoadoutView::Element() const
    {
        return root_;
    }

    void LoadoutView::SetEditingEnabled(bool enabled)
    {
        editingEnabled_ = enabled;
        root_.IsHitTestVisible(enabled);
        list_.IsEnabled(enabled);
        loadButton_.IsEnabled(enabled);
        saveButton_.IsEnabled(enabled);
        Refresh();
    }

    void LoadoutView::Refresh()
    {
        if (!session_ || dragging_) {
            return;
        }
        const auto& intro = session_->Introspection();
        const auto& widgets = session_->Widgets();
        if (intro.Metrics.empty()) {
            summary_.Text(L"Metric information is not available yet. Connect to PresentMon to edit the loadout.");
        }
        else if (widgets.empty()) {
            summary_.Text(L"Add a graph or readout to start your overlay.");
        }
        else {
            const auto count = widgets.size();
            summary_.Text(Text(std::to_string(count) + " widget" + (count == 1 ? "" : "s")
                + " in display order. Drag cards to reorder, or use Move up and Move down."));
        }
        addGraph_.IsEnabled(editingEnabled_ && std::any_of(intro.Metrics.begin(), intro.Metrics.end(),
            [](const auto& metric) { return metric.Numeric; }));
        addReadout_.IsEnabled(editingEnabled_ && !intro.Metrics.empty());
        list_.Items().Clear();
        for (const auto& widget : widgets) {
            if (widget) {
                list_.Items().Append(BuildWidget(widget));
            }
        }
    }

    UIElement LoadoutView::BuildWidget(const std::shared_ptr<core::Widget>& widget)
    {
        auto card = Microsoft::UI::Xaml::Markup::XamlReader::Load(LR"(
            <Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                Background="{ThemeResource CardBackgroundFillColorDefaultBrush}"
                BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" />
            )").as<Border>();
        card.Padding(Thickness{ 16.0, 16.0, 16.0, 16.0 });
        card.CornerRadius(CornerRadius{ 8.0 });
        card.BorderThickness(Thickness{ 1.0, 1.0, 1.0, 1.0 });
        card.HorizontalAlignment(HorizontalAlignment::Stretch);
        card.Margin(Thickness{ 0.0, 0.0, 0.0, 10.0 });
        card.Tag(box_value((int32_t)widget->Key));

        StackPanel content;
        content.Spacing(14.0);
        Grid header;
        header.ColumnSpacing(12.0);
        header.ColumnDefinitions().Append(StarColumn());
        header.ColumnDefinitions().Append(AutoColumn());
        TextBlock title;
        auto& widgets = session_->Widgets();
        const auto position = std::find(widgets.begin(), widgets.end(), widget);
        const auto index = position == widgets.end() ? 0 : (int)(position - widgets.begin()) + 1;
        title.Text(Text(std::to_string(index) + ". " + WidgetTitle(*widget)));
        title.FontSize(18.0);
        title.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        title.TextTrimming(TextTrimming::CharacterEllipsis);
        title.VerticalAlignment(VerticalAlignment::Center);
        header.Children().Append(title);
        StackPanel actions;
        actions.Orientation(Orientation::Horizontal);
        actions.Spacing(4.0);
        actions.Children().Append(GlyphButton("MoveUp_" + std::to_string(widget->Key), "Move widget up", L"\xE70E",
            [this, widget] { MoveWidget(widget, -1); }, index > 1));
        actions.Children().Append(GlyphButton("MoveDown_" + std::to_string(widget->Key), "Move widget down", L"\xE70D",
            [this, widget] { MoveWidget(widget, 1); }, index < (int)widgets.size()));
        actions.Children().Append(IconButton("RemoveWidget_" + std::to_string(widget->Key), "Remove widget", Symbol::Delete,
            [this, widget] { RemoveWidget(widget); }));
        Grid::SetColumn(actions, 1);
        header.Children().Append(actions);
        content.Children().Append(header);

        std::vector<std::string> typeChoices{ "Readout" };
        const auto* firstMetric = widget->Metrics.empty() ? nullptr : FindMetric(widget->Metrics.front().Metric.MetricId);
        if (!firstMetric || firstMetric->Numeric) {
            typeChoices.push_back("Graph");
        }
        const auto isGraph = widget->GetWidgetType() == core::WidgetType::Graph;
        std::vector<UIElement> typeFields;
        typeFields.push_back(Field("Widget type", FormControls::Choice("WidgetType_" + std::to_string(widget->Key), "Widget type",
            typeChoices, isGraph ? (int)typeChoices.size() - 1 : 0,
            [this, widget, typeChoices](int selection) { ChangeType(widget, typeChoices[(size_t)selection] == "Graph"); })));
        if (const auto graph = std::dynamic_pointer_cast<core::Graph>(widget)) {
            typeFields.push_back(Field("Graph type", FormControls::Choice("GraphType_" + std::to_string(widget->Key), "Graph type",
                { "Line", "Histogram" }, graph->GraphType.Name == "Histogram" ? 1 : 0,
                [this, widget](int selection) {
                    const auto graph = std::dynamic_pointer_cast<core::Graph>(widget);
                    if (!graph) return;
                    graph->GraphType.Name = selection == 0 ? "Line" : "Histogram";
                    if (selection == 1 && graph->Metrics.size() > 1) graph->Metrics.erase(graph->Metrics.begin() + 1, graph->Metrics.end());
                    NotifyChanged();
                    Refresh();
                })));
        }
        content.Children().Append(Fields(typeFields, 2));

        for (size_t lineIndex = 0; lineIndex < widget->Metrics.size(); ++lineIndex) {
            content.Children().Append(BuildSeries(widget, widget->Metrics[lineIndex].Key, (int)lineIndex));
        }
        if (const auto graph = std::dynamic_pointer_cast<core::Graph>(widget); graph && graph->GraphType.Name == "Line") {
            auto add = FormControls::ActionButton("AddSeries_" + std::to_string(widget->Key), "Add metric series", [this, widget] {
                const auto* metric = DefaultMetric(true);
                if (!metric) return;
                core::WidgetMetric line;
                line.Metric = core::MetricResolver::CreateQualified(*metric, session_->Introspection(), session_->Preferences());
                widget->Metrics.push_back(std::move(line));
                NotifyChanged();
                Refresh();
            });
            add.HorizontalAlignment(HorizontalAlignment::Left);
            add.IsEnabled(std::any_of(session_->Introspection().Metrics.begin(), session_->Introspection().Metrics.end(),
                [](const auto& metric) { return metric.Numeric; }));
            content.Children().Append(add);
        }

        Expander details;
        details.Header(box_value(isGraph ? L"Graph settings" : L"Readout settings"));
        details.HorizontalAlignment(HorizontalAlignment::Stretch);
        details.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        details.IsExpanded(expandedWidgets_.contains(widget->Key));
        AutomationProperties::SetAutomationId(details, Text("Details_" + std::to_string(widget->Key)));
        details.Expanding([this, details, widget](auto const&, auto const&) {
            expandedWidgets_.insert(widget->Key);
            if (!details.Content()) details.Content(BuildDetails(widget));
        });
        details.Collapsed([this, widget](auto const&, auto const&) { expandedWidgets_.erase(widget->Key); });
        if (details.IsExpanded()) details.Content(BuildDetails(widget));
        content.Children().Append(details);
        card.Child(content);
        return card;
    }

    UIElement LoadoutView::BuildSeries(const std::shared_ptr<core::Widget>& widget, int lineKey, int lineIndex)
    {
        auto* line = FindLine(widget, lineKey);
        if (!line) return TextBlock{};
        StackPanel content;
        content.Spacing(10.0);
        const auto id = std::to_string(widget->Key) + "_" + std::to_string(lineKey);
        const auto* current = FindMetric(line->Metric.MetricId);
        if (widget->Metrics.size() > 1) {
            Grid heading;
            heading.ColumnDefinitions().Append(StarColumn());
            heading.ColumnDefinitions().Append(AutoColumn());
            TextBlock headingText;
            headingText.Text(Text("Series " + std::to_string(lineIndex + 1)));
            headingText.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
            headingText.VerticalAlignment(VerticalAlignment::Center);
            heading.Children().Append(headingText);
            auto remove = IconButton("RemoveSeries_" + id, "Remove series " + std::to_string(lineIndex + 1), Symbol::Remove,
                [this, widget, lineKey] { RemoveSeries(widget, lineKey); });
            Grid::SetColumn(remove, 1);
            heading.Children().Append(remove);
            content.Children().Append(heading);
        }

        std::vector<const core::Metric*> choices;
        for (const auto& metric : session_->Introspection().Metrics) {
            if (lineIndex == 0 || metric.Numeric) choices.push_back(&metric);
        }
        const auto selectedMetric = line->Metric;
        const auto labels = MetricNames(choices, [this, selectedMetric](const core::Metric& metric) { return MetricLabel(metric, selectedMetric); });
        AutoSuggestBox metricBox;
        metricBox.Text(Text(current ? current->Name : "Unknown metric (" + std::to_string(line->Metric.MetricId) + ")"));
        metricBox.PlaceholderText(L"Find a metric");
        metricBox.ItemsSource(StringItems(labels));
        metricBox.QueryIcon(SymbolIcon{ Symbol::Find });
        metricBox.HorizontalAlignment(HorizontalAlignment::Stretch);
        AutomationProperties::SetAutomationId(metricBox, Text("Metric_" + id));
        AutomationProperties::SetName(metricBox, Text("Metric for series " + std::to_string(lineIndex + 1)));
        ToolTipService::SetToolTip(metricBox, box_value(Text(current ? current->Description : "Choose a metric reported by PresentMon.")));
        metricBox.TextChanged([choices, labels](auto const& box, AutoSuggestBoxTextChangedEventArgs const& args) {
            if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput) return;
            const auto query = to_string(box.Text());
            std::vector<std::string> filtered;
            for (size_t i = 0; i < choices.size(); ++i) {
                const auto& metric = *choices[i];
                const auto contains = [&query](const std::string& source) {
                    auto folded = source;
                    auto term = query;
                    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char character) { return (char)std::tolower(character); });
                    std::transform(term.begin(), term.end(), term.begin(), [](unsigned char character) { return (char)std::tolower(character); });
                    return folded.find(term) != std::string::npos;
                };
                if (contains(metric.Name) || contains(metric.Description)) filtered.push_back(labels[i]);
            }
            box.ItemsSource(StringItems(filtered));
        });
        metricBox.GotFocus([labels](const IInspectable& sender, const RoutedEventArgs&) {
            const auto box = sender.as<AutoSuggestBox>();
            box.ItemsSource(StringItems(labels));
            box.IsSuggestionListOpen(true);
        });
        metricBox.SuggestionChosen([choices, labels](auto const& box, AutoSuggestBoxSuggestionChosenEventArgs const& args) {
            const auto selected = ItemText(args.SelectedItem());
            if (!selected) return;
            const auto it = std::find(labels.begin(), labels.end(), *selected);
            if (it != labels.end()) box.Text(Text((*choices[(size_t)(it - labels.begin())]).Name));
        });
        metricBox.QuerySubmitted([this, widget, lineKey, choices, labels, currentName = current ? current->Name : "Unknown metric (" + std::to_string(line->Metric.MetricId) + ")"](
            auto const& box, AutoSuggestBoxQuerySubmittedEventArgs const& args) {
            std::optional<size_t> selected;
            if (const auto chosen = ItemText(args.ChosenSuggestion())) {
                const auto it = std::find(labels.begin(), labels.end(), *chosen);
                if (it != labels.end()) selected = (size_t)(it - labels.begin());
            }
            if (!selected) {
                const auto query = to_string(args.QueryText());
                const auto it = std::find_if(choices.begin(), choices.end(), [&query](const auto* metric) {
                    return _stricmp(metric->Name.c_str(), query.c_str()) == 0;
                });
                if (it != choices.end()) selected = (size_t)(it - choices.begin());
            }
            if (selected) SelectMetric(widget, lineKey, *choices[*selected]);
            else box.Text(Text(currentName));
            box.IsSuggestionListOpen(false);
        });

        std::vector<UIElement> fields;
        fields.push_back(Field("Metric", metricBox));
        std::vector<const core::MetricStat*> stats;
        if (current) {
            for (const auto& stat : session_->Introspection().Stats) {
                if (Contains(current->AvailableStatIds, stat.Id)) stats.push_back(&stat);
            }
        }
        if (!stats.empty()) {
            std::vector<std::string> names;
            int selection = -1;
            for (size_t i = 0; i < stats.size(); ++i) {
                names.push_back(stats[i]->Name);
                if (stats[i]->Id == line->Metric.StatId) selection = (int)i;
            }
            fields.push_back(Field("Statistic", FormControls::Choice("Stat_" + id, "Statistic", names, selection,
                [this, widget, lineKey, stats](int selected) {
                    if (auto* refreshed = FindLine(widget, lineKey); refreshed && selected >= 0 && selected < (int)stats.size()) {
                        refreshed->Metric.StatId = stats[(size_t)selected]->Id;
                        NotifyChanged();
                    }
                })));
        }
        else {
            TextBlock unavailable;
            unavailable.Text(Text("Unavailable (" + std::to_string(line->Metric.StatId) + ")"));
            unavailable.TextWrapping(TextWrapping::Wrap);
            fields.push_back(Field("Statistic", unavailable));
        }

        if (current) {
            const auto effectiveId = core::MetricResolver::ResolveDeviceId(*current, line->Metric,
                session_->Introspection(), session_->Preferences());
            if (session_->Preferences().EnablePerMetricDeviceSelection
                && current->DeviceType == core::MetricDeviceType::GraphicsAdapter
                && !session_->Introspection().Adapters.empty()) {
                std::vector<std::optional<int>> devices{ std::nullopt };
                std::vector<std::string> deviceLabels{ "Default adapter" };
                for (const auto& adapter : session_->Introspection().Adapters) {
                    devices.push_back(adapter.Id);
                    deviceLabels.push_back("[" + std::to_string(adapter.Id) + "] " + adapter.Name);
                }
                if (line->Metric.DeviceId && *line->Metric.DeviceId != 0
                    && std::find(devices.begin(), devices.end(), line->Metric.DeviceId) == devices.end()) {
                    devices.push_back(line->Metric.DeviceId);
                    deviceLabels.push_back("[" + std::to_string(*line->Metric.DeviceId) + "] Unknown GPU");
                }
                std::vector<std::string> deviceChoices;
                int selection = 0;
                for (size_t i = 0; i < devices.size(); ++i) {
                    deviceChoices.push_back(DeviceLabel(*current, line->Metric, devices[i], deviceLabels[i]));
                    if (devices[i] == line->Metric.DeviceId || (!devices[i] && !line->Metric.DeviceId)) selection = (int)i;
                }
                const auto metricId = current->Id;
                fields.push_back(Field("GPU device", FormControls::Choice("Device_" + id, "GPU device", deviceChoices, selection,
                    [this, widget, lineKey, metricId, devices](int selected) {
                        auto* refreshed = FindLine(widget, lineKey);
                        const auto* currentMetric = FindMetric(metricId);
                        if (!refreshed || !currentMetric || selected < 0 || selected >= (int)devices.size()) return;
                        refreshed->Metric.DeviceId = devices[(size_t)selected];
                        core::MetricResolver::Normalize(*currentMetric, refreshed->Metric, session_->Introspection(), session_->Preferences());
                        NotifyChanged();
                        Refresh();
                    })));
            }
            const auto arraySize = core::MetricResolver::ArraySize(*current, effectiveId);
            if (arraySize > 1) {
                fields.push_back(Field("Array index", FormControls::Number("ArrayIndex_" + id, "Array index", line->Metric.ArrayIndex,
                    0.0, (double)arraySize - 1.0, 1.0, [this, widget, lineKey](double value) {
                        if (auto* refreshed = FindLine(widget, lineKey)) {
                            refreshed->Metric.ArrayIndex = (int)value;
                            NotifyChanged();
                        }
                    })));
            }
        }
        content.Children().Append(Fields(fields, 3));
        if (current && !core::MetricResolver::IsAvailable(*current, line->Metric, session_->Introspection(), session_->Preferences())) {
            content.Children().Append(FormControls::Description(core::MetricResolver::AvailabilityReason(*current, line->Metric,
                session_->Introspection(), session_->Preferences()).value_or("This metric is unavailable.")));
        }
        if (const auto graph = std::dynamic_pointer_cast<core::Graph>(widget)) {
            std::vector<UIElement> appearance;
            if (graph->GraphType.Name == "Line") {
                appearance.push_back(Field("Right axis", FormControls::Toggle("RightAxis_" + id, "Use right axis",
                    line->AxisAffinity == core::AxisAffinity::Right, [this, widget, lineKey](bool value) {
                        if (auto* refreshed = FindLine(widget, lineKey)) {
                            refreshed->AxisAffinity = value ? core::AxisAffinity::Right : core::AxisAffinity::Left;
                            NotifyChanged();
                        }
                    })));
            }
            appearance.push_back(Field("Line color", Color("LineColor_" + id, "Line color", line->LineColor,
                [this, widget, lineKey](core::RgbaColor value) {
                    if (auto* refreshed = FindLine(widget, lineKey)) refreshed->LineColor = value;
                    NotifyChanged();
                })));
            appearance.push_back(Field("Fill color", Color("FillColor_" + id, "Fill color", line->FillColor,
                [this, widget, lineKey](core::RgbaColor value) {
                    if (auto* refreshed = FindLine(widget, lineKey)) refreshed->FillColor = value;
                    NotifyChanged();
                })));
            content.Children().Append(Fields(appearance, 3));
        }
        auto series = Microsoft::UI::Xaml::Markup::XamlReader::Load(LR"(
            <Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                Background="{ThemeResource LayerFillColorDefaultBrush}" />
            )").as<Border>();
        series.Padding(Thickness{ 12.0, 12.0, 12.0, 12.0 });
        series.CornerRadius(CornerRadius{ 6.0 });
        series.Child(content);
        return series;
    }

    UIElement LoadoutView::BuildDetails(const std::shared_ptr<core::Widget>& widget)
    {
        StackPanel panel;
        panel.Spacing(10.0);
        if (const auto graph = std::dynamic_pointer_cast<core::Graph>(widget)) {
            auto& options = graph->GraphType;
            const auto histogram = options.Name == "Histogram";
            panel.Children().Append(FormControls::Heading(histogram ? "Histogram data" : "Graph axes"));
            panel.Children().Append(BuildRange(graph, "LeftRange_" + std::to_string(widget->Key),
                histogram ? "Value range" : "Left value range",
                "Automatically scale the range, or enter the minimum and maximum values.", options.Range, options.AutoLeft,
                [graph](bool value) { graph->GraphType.AutoLeft = value; },
                [graph](size_t index, double value) {
                    if (graph->GraphType.Range.size() <= index) graph->GraphType.Range.resize(index + 1, index == 0 ? 0.0 : 150.0);
                    graph->GraphType.Range[index] = value;
                }));
            if (!histogram) {
                panel.Children().Append(BuildRange(graph, "RightRange_" + std::to_string(widget->Key), "Right value range",
                    "Range for metric series assigned to the right axis.", options.RangeRight, options.AutoRight,
                    [graph](bool value) { graph->GraphType.AutoRight = value; },
                    [graph](size_t index, double value) {
                        if (graph->GraphType.RangeRight.size() <= index) graph->GraphType.RangeRight.resize(index + 1, index == 0 ? 0.0 : 150.0);
                        graph->GraphType.RangeRight[index] = value;
                    }));
                panel.Children().Append(FormControls::Row("Bottom axis", "Show the time axis along the bottom of this graph.",
                    FormControls::Toggle("BottomAxis_" + std::to_string(widget->Key), "Show bottom axis", graph->ShowBottomAxis,
                        [this, graph](bool value) { graph->ShowBottomAxis = value; NotifyChanged(); })));
            }
            else {
                panel.Children().Append(NumberSetting("BinCount_" + std::to_string(widget->Key), "Number of bins",
                    "Number of bars in the histogram.", graph->GraphType.BinCount, 5.0, 200.0, 1.0,
                    [graph](double value) { graph->GraphType.BinCount = (int)value; }));
                panel.Children().Append(BuildRange(graph, "CountRange_" + std::to_string(widget->Key), "Count range",
                    "Range of counts displayed in each bin.", options.CountRange, options.AutoCount,
                    [graph](bool value) { graph->GraphType.AutoCount = value; },
                    [graph](size_t index, double value) {
                        if (graph->GraphType.CountRange.size() <= index) graph->GraphType.CountRange.resize(index + 1, index == 0 ? 0.0 : 150.0);
                        graph->GraphType.CountRange[index] = value;
                    }));
                const auto& preferences = session_->Preferences();
                std::ostringstream expected;
                expected << "Expected data points: " << preferences.TimeRange << " seconds x " << preferences.MetricPollRate
                    << " Hz = " << preferences.TimeRange * preferences.MetricPollRate
                    << ". Change time scale in Overlay settings and poll rate in Data processing settings.";
                panel.Children().Append(FormControls::Description(expected.str()));
            }
            panel.Children().Append(FormControls::Heading("Style"));
            panel.Children().Append(NumberSetting("GraphHeight_" + std::to_string(widget->Key), "Graph height",
                "Vertical size of this graph in pixels.", graph->Height, 20.0, 450.0, 1.0,
                [graph](double value) { graph->Height = value; }));
            panel.Children().Append(NumberSetting("VerticalDivisions_" + std::to_string(widget->Key), "Vertical divisions",
                "Number of vertical divisions in the grid.", graph->VDivs, 1.0, 40.0, 1.0,
                [graph](double value) { graph->VDivs = (int)value; }));
            panel.Children().Append(NumberSetting("HorizontalDivisions_" + std::to_string(widget->Key), "Horizontal divisions",
                "Number of horizontal divisions in the grid.", graph->HDivs, 1.0, 100.0, 1.0,
                [graph](double value) { graph->HDivs = (int)value; }));
            panel.Children().Append(NumberSetting("GraphTextSize_" + std::to_string(widget->Key), "Font size",
                "Text size for this graph.", graph->TextSize, 5.0, 80.0, 0.5,
                [graph](double value) { graph->TextSize = value; }));
            panel.Children().Append(ColorSetting("GridColor_" + std::to_string(widget->Key), "Grid color", graph->GridColor,
                [graph](core::RgbaColor value) { graph->GridColor = value; }));
            panel.Children().Append(ColorSetting("BackgroundColor_" + std::to_string(widget->Key), "Background color", graph->BackgroundColor,
                [graph](core::RgbaColor value) { graph->BackgroundColor = value; }));
            panel.Children().Append(ColorSetting("TextColor_" + std::to_string(widget->Key), "Text color", graph->TextColor,
                [graph](core::RgbaColor value) { graph->TextColor = value; }));
            panel.Children().Append(ColorSetting("DividerColor_" + std::to_string(widget->Key), "Divider color", graph->DividerColor,
                [graph](core::RgbaColor value) { graph->DividerColor = value; }));
            panel.Children().Append(ColorSetting("BorderColor_" + std::to_string(widget->Key), "Border color", graph->BorderColor,
                [graph](core::RgbaColor value) { graph->BorderColor = value; }));
        }
        else if (const auto readout = std::dynamic_pointer_cast<core::Readout>(widget)) {
            panel.Children().Append(NumberSetting("ReadoutFontSize_" + std::to_string(widget->Key), "Font size",
                "Text size for this readout.", readout->FontSize, 5.0, 80.0, 0.5,
                [readout](double value) { readout->FontSize = value; }));
            panel.Children().Append(ColorSetting("ReadoutFontColor_" + std::to_string(widget->Key), "Text color", readout->FontColor,
                [readout](core::RgbaColor value) { readout->FontColor = value; }));
            panel.Children().Append(ColorSetting("ReadoutBackgroundColor_" + std::to_string(widget->Key), "Background color", readout->BackgroundColor,
                [readout](core::RgbaColor value) { readout->BackgroundColor = value; }));
            panel.Children().Append(FormControls::Row("Show label", "Display the metric name next to its value.",
                FormControls::Toggle("ShowLabel_" + std::to_string(widget->Key), "Show label", readout->ShowLabel,
                    [this, readout](bool value) { readout->ShowLabel = value; NotifyChanged(); })));
        }
        if (session_->Preferences().EnablePerMetricDeviceSelection) {
            panel.Children().Append(FormControls::Heading("Metric labels"));
            panel.Children().Append(FormControls::Row("Device ID", "Include the GPU device ID in metric labels.",
                FormControls::Toggle("LabelDeviceId_" + std::to_string(widget->Key), "Include device ID", widget->LabelIncludeDeviceId,
                    [this, widget](bool value) { widget->LabelIncludeDeviceId = value; NotifyChanged(); })));
            panel.Children().Append(FormControls::Row("Device name", "Include the GPU name in metric labels.",
                FormControls::Toggle("LabelDeviceName_" + std::to_string(widget->Key), "Include device name", widget->LabelIncludeDeviceName,
                    [this, widget](bool value) { widget->LabelIncludeDeviceName = value; NotifyChanged(); })));
        }
        return panel;
    }

    UIElement LoadoutView::BuildRange(const std::shared_ptr<core::Widget>& owner, const std::string& id,
        const std::string& title, const std::string& description, std::vector<double> values,
        bool automatic, std::function<void(bool)> setAutomatic, std::function<void(size_t, double)> setValue)
    {
        while (values.size() < 2) values.push_back(values.empty() ? 0.0 : 150.0);
        StackPanel fields;
        fields.Spacing(8.0);
        auto minimum = FormControls::Number(id + "Minimum", "Minimum", values[0], std::numeric_limits<double>::lowest(), values[1], 1.0,
            [this, owner, setValue](double value) {
                if (!IsCurrentWidget(owner)) return;
                setValue(0, value);
                NotifyChanged();
            });
        auto maximum = FormControls::Number(id + "Maximum", "Maximum", values[1], values[0], std::numeric_limits<double>::max(), 1.0,
            [this, owner, setValue](double value) {
                if (!IsCurrentWidget(owner)) return;
                setValue(1, value);
                NotifyChanged();
            });
        minimum.ValueChanged([maximum, minimum](auto const&, auto const&) { maximum.Minimum(minimum.Value()); });
        maximum.ValueChanged([minimum, maximum](auto const&, auto const&) { minimum.Maximum(maximum.Value()); });
        minimum.IsEnabled(!automatic);
        maximum.IsEnabled(!automatic);
        fields.Children().Append(Field("Autoscale", FormControls::Toggle(id + "Autoscale", "Autoscale", automatic,
            [this, owner, setAutomatic, minimum, maximum](bool value) {
                if (!IsCurrentWidget(owner)) return;
                setAutomatic(value);
                minimum.IsEnabled(!value);
                maximum.IsEnabled(!value);
                NotifyChanged();
            })));
        fields.Children().Append(Fields({ Field("Minimum", minimum), Field("Maximum", maximum) }, 2));
        return FormControls::Row(title, description, fields);
    }

    UIElement LoadoutView::NumberSetting(const std::string& id, const std::string& title, const std::string& description,
        double value, double minimum, double maximum, double step, std::function<void(double)> setValue)
    {
        return FormControls::Row(title, description, FormControls::Number(id, title, value, minimum, maximum, step,
            [this, setValue](double number) { setValue(number); NotifyChanged(); }));
    }

    UIElement LoadoutView::ColorSetting(const std::string& id, const std::string& title, const core::RgbaColor& color,
        std::function<void(core::RgbaColor)> setValue)
    {
        return FormControls::Row(title, "", Color(id, title, color, std::move(setValue)));
    }

    UIElement LoadoutView::Color(const std::string& id, const std::string& title, const core::RgbaColor& color,
        std::function<void(core::RgbaColor)> setValue)
    {
        return FormControls::Color(id, title, ToColor(color), [this, setValue = std::move(setValue)](Windows::UI::Color value) {
            setValue(FromColor(value));
            NotifyChanged();
        });
    }

    void LoadoutView::AddWidget(bool graph)
    {
        const auto* metric = DefaultMetric(graph);
        if (!metric) return;
        auto qualified = core::MetricResolver::CreateQualified(*metric, session_->Introspection(), session_->Preferences());
        std::shared_ptr<core::Widget> widget;
        if (graph) widget = std::make_shared<core::Graph>();
        else widget = std::make_shared<core::Readout>();
        widget->Metrics.emplace_back();
        widget->Metrics.back().Metric = std::move(qualified);
        session_->Widgets().push_back(std::move(widget));
        NotifyChanged();
        Refresh();
        if (list_.Items().Size() > 0) list_.ScrollIntoView(list_.Items().GetAt(list_.Items().Size() - 1), ScrollIntoViewAlignment::Leading);
    }

    void LoadoutView::ChangeType(const std::shared_ptr<core::Widget>& widget, bool graph)
    {
        if (!widget || graph == (widget->GetWidgetType() == core::WidgetType::Graph)) return;
        core::QualifiedMetric metric;
        if (!widget->Metrics.empty()) metric = widget->Metrics.front().Metric;
        else {
            const auto* defaultMetric = DefaultMetric(graph);
            if (!defaultMetric) return;
            metric = core::MetricResolver::CreateQualified(*defaultMetric, session_->Introspection(), session_->Preferences());
        }
        std::shared_ptr<core::Widget> replacement;
        if (graph) replacement = std::make_shared<core::Graph>();
        else replacement = std::make_shared<core::Readout>();
        replacement->Metrics.emplace_back();
        replacement->Metrics.front().Metric = std::move(metric);
        auto& widgets = session_->Widgets();
        const auto position = std::find(widgets.begin(), widgets.end(), widget);
        if (position == widgets.end()) return;
        *position = std::move(replacement);
        NotifyChanged();
        Refresh();
    }

    void LoadoutView::SelectMetric(const std::shared_ptr<core::Widget>& widget, int lineKey, const core::Metric& metric)
    {
        auto* line = FindLine(widget, lineKey);
        if (!line || line->Metric.MetricId == metric.Id) return;
        const auto* previous = FindMetric(line->Metric.MetricId);
        if (metric.DeviceType == core::MetricDeviceType::GraphicsAdapter
            && (!previous || previous->DeviceType != core::MetricDeviceType::GraphicsAdapter)) line->Metric.DeviceId.reset();
        line->Metric.MetricId = metric.Id;
        if (!Contains(metric.AvailableStatIds, line->Metric.StatId)) {
            line->Metric.StatId = metric.AvailableStatIds.empty() ? 0 : metric.AvailableStatIds.front();
        }
        core::MetricResolver::Normalize(metric, line->Metric, session_->Introspection(), session_->Preferences());
        if (!metric.Numeric && widget->GetWidgetType() == core::WidgetType::Graph) {
            auto readout = std::make_shared<core::Readout>();
            readout->Metrics.emplace_back();
            readout->Metrics.front().Metric = line->Metric;
            readout->Metrics.front().LineColor = line->LineColor;
            readout->Metrics.front().FillColor = line->FillColor;
            auto& widgets = session_->Widgets();
            const auto position = std::find(widgets.begin(), widgets.end(), widget);
            if (position != widgets.end()) *position = std::move(readout);
            NotifyChanged();
            Refresh();
            return;
        }
        NotifyChanged();
        Refresh();
    }

    void LoadoutView::MoveWidget(const std::shared_ptr<core::Widget>& widget, int offset)
    {
        auto& widgets = session_->Widgets();
        const auto position = std::find(widgets.begin(), widgets.end(), widget);
        if (position == widgets.end()) return;
        const auto from = (int)(position - widgets.begin());
        const auto to = from + offset;
        if (to < 0 || to >= (int)widgets.size()) return;
        auto moving = *position;
        widgets.erase(position);
        widgets.insert(widgets.begin() + to, std::move(moving));
        NotifyChanged();
        Refresh();
    }

    void LoadoutView::RemoveWidget(const std::shared_ptr<core::Widget>& widget)
    {
        auto& widgets = session_->Widgets();
        const auto position = std::find(widgets.begin(), widgets.end(), widget);
        if (position == widgets.end()) return;
        expandedWidgets_.erase(widget->Key);
        widgets.erase(position);
        NotifyChanged();
        Refresh();
    }

    void LoadoutView::RemoveSeries(const std::shared_ptr<core::Widget>& widget, int lineKey)
    {
        if (!widget) return;
        const auto position = std::find_if(widget->Metrics.begin(), widget->Metrics.end(),
            [lineKey](const auto& line) { return line.Key == lineKey; });
        if (position == widget->Metrics.end()) return;
        widget->Metrics.erase(position);
        NotifyChanged();
        Refresh();
    }

    void LoadoutView::SyncReorderedWidgets()
    {
        std::vector<std::shared_ptr<core::Widget>> reordered;
        auto& widgets = session_->Widgets();
        reordered.reserve(widgets.size());
        for (uint32_t index = 0; index < list_.Items().Size(); ++index) {
            const auto card = list_.Items().GetAt(index).try_as<Border>();
            const auto key = card ? card.Tag().try_as<IReference<int32_t>>() : nullptr;
            if (!key) continue;
            const auto widget = std::find_if(widgets.begin(), widgets.end(), [value = key.Value()](const auto& candidate) {
                return candidate && candidate->Key == value;
            });
            if (widget != widgets.end()) reordered.push_back(*widget);
        }
        if (reordered.size() == widgets.size()) widgets = std::move(reordered);
    }

    void LoadoutView::NotifyChanged()
    {
        if (session_) session_->Changed();
    }

    core::WidgetMetric* LoadoutView::FindLine(const std::shared_ptr<core::Widget>& widget, int lineKey) const
    {
        if (!widget) return nullptr;
        const auto position = std::find_if(widget->Metrics.begin(), widget->Metrics.end(),
            [lineKey](const auto& line) { return line.Key == lineKey; });
        return position == widget->Metrics.end() ? nullptr : &*position;
    }

    bool LoadoutView::IsCurrentWidget(const std::shared_ptr<core::Widget>& widget) const
    {
        if (!session_ || !widget) return false;
        const auto& widgets = session_->Widgets();
        return std::find(widgets.begin(), widgets.end(), widget) != widgets.end();
    }

    const core::Metric* LoadoutView::FindMetric(int id) const
    {
        const auto& metrics = session_->Introspection().Metrics;
        const auto position = std::find_if(metrics.begin(), metrics.end(), [id](const auto& metric) { return metric.Id == id; });
        return position == metrics.end() ? nullptr : &*position;
    }

    const core::Metric* LoadoutView::DefaultMetric(bool numericOnly) const
    {
        const auto& metrics = session_->Introspection().Metrics;
        if (!numericOnly) return metrics.empty() ? nullptr : &metrics.front();
        const auto preferred = std::find_if(metrics.begin(), metrics.end(), [](const auto& metric) { return metric.Id == 8 && metric.Numeric; });
        if (preferred != metrics.end()) return &*preferred;
        const auto numeric = std::find_if(metrics.begin(), metrics.end(), [](const auto& metric) { return metric.Numeric; });
        return numeric == metrics.end() ? nullptr : &*numeric;
    }

    std::string LoadoutView::WidgetTitle(const core::Widget& widget) const
    {
        if (!widget.Metrics.empty()) {
            if (const auto* metric = FindMetric(widget.Metrics.front().Metric.MetricId)) return metric->Name;
        }
        return widget.GetWidgetType() == core::WidgetType::Graph ? "Graph" : "Readout";
    }

    std::string LoadoutView::MetricLabel(const core::Metric& metric, const core::QualifiedMetric& selected) const
    {
        auto probe = core::MetricResolver::CreateQualified(metric, session_->Introspection(), session_->Preferences());
        probe.DeviceId = selected.DeviceId;
        core::MetricResolver::Normalize(metric, probe, session_->Introspection(), session_->Preferences());
        auto available = core::MetricResolver::IsAvailable(metric, probe, session_->Introspection(), session_->Preferences());
        if (!available && metric.DeviceType == core::MetricDeviceType::GraphicsAdapter
            && session_->Preferences().EnablePerMetricDeviceSelection) {
            for (const auto& adapter : session_->Introspection().Adapters) {
                probe.DeviceId = adapter.Id;
                if (core::MetricResolver::IsAvailable(metric, probe, session_->Introspection(), session_->Preferences())) {
                    available = true;
                    break;
                }
            }
        }
        return available ? metric.Name : metric.Name + " (unavailable)";
    }

    std::string LoadoutView::DeviceLabel(const core::Metric& metric, const core::QualifiedMetric& selected,
        std::optional<int> deviceId, const std::string& label) const
    {
        auto probe = core::MetricResolver::CreateQualified(metric, session_->Introspection(), session_->Preferences(), selected.StatId);
        probe.DeviceId = deviceId;
        core::MetricResolver::Normalize(metric, probe, session_->Introspection(), session_->Preferences());
        return core::MetricResolver::IsAvailable(metric, probe, session_->Introspection(), session_->Preferences())
            ? label : label + " (unavailable)";
    }

    UIElement LoadoutView::Field(const std::string& label, const UIElement& control)
    {
        StackPanel field;
        field.Spacing(5.0);
        field.Children().Append(FormControls::Description(label));
        if (const auto element = control.try_as<FrameworkElement>()) element.HorizontalAlignment(HorizontalAlignment::Stretch);
        field.Children().Append(control);
        return field;
    }

    Grid LoadoutView::Fields(const std::vector<UIElement>& content, int maximumColumns)
    {
        Grid grid;
        grid.ColumnSpacing(12.0);
        grid.RowSpacing(12.0);
        auto hosts = std::make_shared<std::vector<Grid>>();
        hosts->reserve(content.size());
        for (const auto& child : content) {
            Grid host;
            host.Children().Append(child);
            grid.Children().Append(host);
            hosts->push_back(host);
        }
        auto columns = std::make_shared<int>(0);
        const auto arrange = [grid, hosts, columns, maximumColumns](double width) {
            const auto desired = std::clamp((int)(width / 210.0), 1, std::min(maximumColumns, std::max(1, (int)hosts->size())));
            if (*columns == desired) return;
            *columns = desired;
            grid.ColumnDefinitions().Clear();
            grid.RowDefinitions().Clear();
            for (int column = 0; column < *columns; ++column) grid.ColumnDefinitions().Append(StarColumn());
            for (size_t row = 0; row < (hosts->size() + (size_t)*columns - 1) / (size_t)*columns; ++row) grid.RowDefinitions().Append(AutoRow());
            for (size_t index = 0; index < hosts->size(); ++index) {
                Grid::SetColumn((*hosts)[index], (int)(index % (size_t)*columns));
                Grid::SetRow((*hosts)[index], (int)(index / (size_t)*columns));
            }
        };
        arrange(0.0);
        grid.SizeChanged([arrange](auto const&, SizeChangedEventArgs const& args) { arrange(args.NewSize().Width); });
        return grid;
    }

    Windows::UI::Color LoadoutView::ToColor(const core::RgbaColor& color)
    {
        Windows::UI::Color result{};
        result.A = (uint8_t)std::clamp((int)std::round(color.A * 255.0), 0, 255);
        result.R = (uint8_t)std::clamp(color.R, 0, 255);
        result.G = (uint8_t)std::clamp(color.G, 0, 255);
        result.B = (uint8_t)std::clamp(color.B, 0, 255);
        return result;
    }

    core::RgbaColor LoadoutView::FromColor(Windows::UI::Color color)
    {
        core::RgbaColor result;
        result.R = color.R;
        result.G = color.G;
        result.B = color.B;
        result.A = color.A / 255.0;
        return result;
    }

}
