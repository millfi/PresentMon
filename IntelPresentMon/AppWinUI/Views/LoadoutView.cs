using System.Collections.ObjectModel;
using System.Collections.Specialized;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using PresentMon.UI.Core;

namespace PresentMon.UI.Views;

public sealed class LoadoutView : UserControl
{
    private readonly ObservableCollection<Widget> widgets;
    private readonly Preferences preferences;
    private readonly IntrospectionData introspection;
    private readonly Action changed;
    private readonly HashSet<int> expandedWidgets = [];
    private readonly ListView list;
    private readonly TextBlock summary;
    private readonly InfoBar notice;
    private readonly AppBarButton addGraph;
    private readonly AppBarButton addReadout;
    private bool suppressCollectionRefresh;
    private bool subscribed;
    private bool refreshQueued;
    private bool dragging;

    public LoadoutView(ObservableCollection<Widget> widgets, Preferences preferences,
        IntrospectionData intro, Action changed, Func<Task> load, Func<Task> save)
    {
        this.widgets = widgets;
        this.preferences = preferences;
        introspection = intro;
        this.changed = changed;

        var layout = new Grid { RowSpacing = 12 };
        layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        layout.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });

        var commands = new CommandBar
        {
            DefaultLabelPosition = CommandBarDefaultLabelPosition.Right,
            HorizontalContentAlignment = HorizontalAlignment.Left,
            Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent),
        };
        addGraph = Command("AddGraph", "Add graph", Symbol.Add, () => AddWidget(true));
        addReadout = Command("AddReadout", "Add readout", Symbol.Font, () => AddWidget(false));
        commands.PrimaryCommands.Add(addGraph);
        commands.PrimaryCommands.Add(addReadout);
        commands.PrimaryCommands.Add(new AppBarSeparator());
        commands.PrimaryCommands.Add(AsyncCommand("LoadLoadout", "Open loadout", Symbol.OpenFile, load));
        commands.PrimaryCommands.Add(AsyncCommand("SaveLoadout", "Save loadout", Symbol.Save, save));
        layout.Children.Add(commands);

        var messagePanel = new StackPanel { Spacing = 8 };
        summary = FormControls.Description("");
        summary.FontSize = 14;
        AutomationProperties.SetAutomationId(summary, "LoadoutSummary");
        messagePanel.Children.Add(summary);
        notice = new InfoBar { IsOpen = false, IsClosable = true, Severity = InfoBarSeverity.Warning };
        messagePanel.Children.Add(notice);
        Grid.SetRow(messagePanel, 1);
        layout.Children.Add(messagePanel);

        list = new ListView
        {
            ItemsSource = widgets,
            ItemTemplate = (DataTemplate)XamlReader.Load("""
                <DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
                    <Border Background="{ThemeResource CardBackgroundFillColorDefaultBrush}"
                        BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}"
                        Padding="16" CornerRadius="8" BorderThickness="1"
                        HorizontalAlignment="Stretch" />
                </DataTemplate>
                """),
            CanDragItems = true,
            CanReorderItems = true,
            AllowDrop = true,
            SelectionMode = ListViewSelectionMode.None,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            Padding = new Thickness(0, 0, 12, 16),
        };
        list.ItemContainerStyle = new Style(typeof(ListViewItem))
        {
            Setters =
            {
                new Setter(Control.HorizontalContentAlignmentProperty, HorizontalAlignment.Stretch),
                new Setter(Control.PaddingProperty, new Thickness(0)),
                new Setter(FrameworkElement.MarginProperty, new Thickness(0, 0, 0, 10)),
            },
        };
        ScrollViewer.SetHorizontalScrollMode(list, ScrollMode.Disabled);
        ScrollViewer.SetHorizontalScrollBarVisibility(list, ScrollBarVisibility.Disabled);
        AutomationProperties.SetAutomationId(list, "WidgetList");
        AutomationProperties.SetName(list, "Overlay widgets. Drag to reorder, or use each widget's move buttons.");
        list.ContainerContentChanging += (_, args) =>
        {
            if (args.ItemContainer.ContentTemplateRoot is not Border card) return;
            if (args.InRecycleQueue)
            {
                card.Child = null;
                card.Tag = null;
                return;
            }
            if (args.Item is not Widget widget) return;
            card.Tag = widget;
            card.Child = BuildWidget(widget);
            args.Handled = true;
        };
        list.DragItemsStarting += (_, _) => dragging = true;
        list.DragItemsCompleted += (_, _) =>
        {
            dragging = false;
            changed();
            Refresh();
        };
        Grid.SetRow(list, 2);
        layout.Children.Add(list);
        Content = layout;
        Loaded += (_, _) =>
        {
            if (subscribed) return;
            widgets.CollectionChanged += WidgetsChanged;
            subscribed = true;
            Refresh();
        };
        Unloaded += (_, _) =>
        {
            widgets.CollectionChanged -= WidgetsChanged;
            subscribed = false;
        };
        Refresh();
    }

    public bool IsEditingEnabled
    {
        get => IsEnabled;
        set => IsEnabled = value;
    }

    public void Refresh()
    {
        UpdateSummary();
        if (refreshQueued || dragging) return;
        refreshQueued = true;
        DispatcherQueue.TryEnqueue(() =>
        {
            refreshQueued = false;
            if (dragging) return;
            foreach (var widget in widgets) RefreshWidgetNow(widget);
        });
    }

    private void WidgetsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (!suppressCollectionRefresh) Refresh();
    }

    private void UpdateSummary()
    {
        summary.Text = widgets.Count == 0
            ? "Add a graph or readout to start your overlay."
            : $"{widgets.Count} widget{(widgets.Count == 1 ? "" : "s")} in display order. Drag cards to reorder, or use Move up and Move down.";
        addGraph.IsEnabled = introspection.Metrics.Any(m => m.Numeric);
        addReadout.IsEnabled = introspection.Metrics.Count > 0;
        if (introspection.Metrics.Count == 0)
            summary.Text = "Metric information is not available yet. Connect to PresentMon to edit the loadout.";
    }

    private void RefreshWidget(Widget widget)
    {
        DispatcherQueue.TryEnqueue(() => RefreshWidgetNow(widget));
    }

    private void RefreshWidgetNow(Widget widget)
    {
        if (list.ContainerFromItem(widget) is ListViewItem { ContentTemplateRoot: Border card })
        {
            card.Tag = widget;
            card.Child = BuildWidget(widget);
        }
    }

    private UIElement BuildWidget(Widget widget)
    {
        var content = new StackPanel { Spacing = 14 };
        var header = new Grid { ColumnSpacing = 12 };
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var title = new TextBlock
        {
            Text = $"{widgets.IndexOf(widget) + 1}. {WidgetTitle(widget)}",
            FontSize = 18,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            TextTrimming = TextTrimming.CharacterEllipsis,
            VerticalAlignment = VerticalAlignment.Center,
        };
        header.Children.Add(title);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        actions.Children.Add(IconButton($"MoveUp_{widget.Key}", "Move widget up", "\uE70E",
            () => MoveWidget(widget, -1), widgets.IndexOf(widget) > 0));
        actions.Children.Add(IconButton($"MoveDown_{widget.Key}", "Move widget down", "\uE70D",
            () => MoveWidget(widget, 1), widgets.IndexOf(widget) < widgets.Count - 1));
        actions.Children.Add(IconButton($"RemoveWidget_{widget.Key}", "Remove widget", Symbol.Delete,
            () => MutateCollection(() => widgets.Remove(widget))));
        Grid.SetColumn(actions, 1);
        header.Children.Add(actions);
        content.Children.Add(header);

        var typeChoices = new List<string> { "Readout" };
        var firstMetric = widget.Metrics.FirstOrDefault();
        var metricInfo = introspection.Metrics.FirstOrDefault(m => m.Id == firstMetric?.Metric.MetricId);
        if (metricInfo?.Numeric == true || metricInfo is null) typeChoices.Add("Graph");
        var typeFields = new List<UIElement>
        {
            Field("Widget type", FormControls.Choice($"WidgetType_{widget.Key}", "Widget type", typeChoices,
                typeChoices.IndexOf(widget is Graph ? "Graph" : "Readout"),
                index => ChangeType(widget, typeChoices[index] == "Graph"))),
        };
        if (widget is Graph graph)
        {
            typeFields.Add(Field("Graph type", FormControls.Choice($"GraphType_{widget.Key}", "Graph type",
                new[] { "Line", "Histogram" }, graph.GraphType.Name == "Histogram" ? 1 : 0, index =>
                {
                    graph.GraphType.Name = index == 0 ? "Line" : "Histogram";
                    if (index == 1)
                        while (graph.Metrics.Count > 1) graph.Metrics.RemoveAt(graph.Metrics.Count - 1);
                    changed();
                    RefreshWidget(graph);
                })));
        }
        content.Children.Add(Fields(typeFields, 2));

        for (var index = 0; index < widget.Metrics.Count; index++)
        {
            var line = widget.Metrics[index];
            content.Children.Add(BuildSeries(widget, line, index));
        }
        if (widget is Graph { GraphType.Name: "Line" })
        {
            var add = FormControls.ActionButton($"AddSeries_{widget.Key}", "Add metric series", () =>
            {
                var metric = DefaultMetric(true);
                if (metric is null) return;
                widget.Metrics.Add(new WidgetMetric { Metric = MetricResolver.CreateQualified(metric, introspection, preferences) });
                changed();
                RefreshWidget(widget);
            });
            add.HorizontalAlignment = HorizontalAlignment.Left;
            add.IsEnabled = introspection.Metrics.Any(m => m.Numeric);
            content.Children.Add(add);
        }

        var details = new Expander
        {
            Header = widget is Graph ? "Graph settings" : "Readout settings",
            HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            IsExpanded = expandedWidgets.Contains(widget.Key),
        };
        AutomationProperties.SetAutomationId(details, $"Details_{widget.Key}");
        details.Expanding += (_, _) =>
        {
            expandedWidgets.Add(widget.Key);
            details.Content ??= BuildDetails(widget);
        };
        details.Collapsed += (_, _) => expandedWidgets.Remove(widget.Key);
        if (details.IsExpanded) details.Content = BuildDetails(widget);
        content.Children.Add(details);
        return content;
    }

    private UIElement BuildSeries(Widget widget, WidgetMetric line, int lineIndex)
    {
        var content = new StackPanel { Spacing = 10 };
        var id = $"{widget.Key}_{line.Key}";
        var current = introspection.Metrics.FirstOrDefault(m => m.Id == line.Metric.MetricId);
        if (widget.Metrics.Count > 1)
        {
            var heading = new Grid();
            heading.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            heading.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            heading.Children.Add(new TextBlock
            {
                Text = $"Series {lineIndex + 1}",
                FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
                VerticalAlignment = VerticalAlignment.Center,
            });
            var remove = IconButton($"RemoveSeries_{id}", $"Remove series {lineIndex + 1}", Symbol.Remove, () =>
            {
                widget.Metrics.Remove(line);
                changed();
                RefreshWidget(widget);
            });
            Grid.SetColumn(remove, 1);
            heading.Children.Add(remove);
            content.Children.Add(heading);
        }

        var metricChoices = introspection.Metrics.Where(m => lineIndex == 0 || m.Numeric)
            .Select(m => new MetricChoice(m, MetricLabel(m, line.Metric))).ToList();
        var metricBox = new AutoSuggestBox
        {
            Text = current?.Name ?? $"Unknown metric ({line.Metric.MetricId})",
            PlaceholderText = "Find a metric",
            ItemsSource = metricChoices.Select(choice => choice.Label).ToList(),
            QueryIcon = new SymbolIcon(Symbol.Find),
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        AutomationProperties.SetAutomationId(metricBox, $"Metric_{id}");
        AutomationProperties.SetName(metricBox, $"Metric for series {lineIndex + 1}");
        ToolTipService.SetToolTip(metricBox, current?.Description ?? "Choose a metric reported by PresentMon.");
        metricBox.TextChanged += (box, args) =>
        {
            if (args.Reason != AutoSuggestionBoxTextChangeReason.UserInput) return;
            box.ItemsSource = metricChoices.Where(choice =>
                choice.Metric.Name.Contains(box.Text, StringComparison.OrdinalIgnoreCase) ||
                choice.Metric.Description.Contains(box.Text, StringComparison.OrdinalIgnoreCase))
                .Select(choice => choice.Label).ToList();
        };
        metricBox.GotFocus += (_, _) =>
        {
            metricBox.ItemsSource = metricChoices.Select(choice => choice.Label).ToList();
            metricBox.IsSuggestionListOpen = true;
        };
        metricBox.SuggestionChosen += (box, args) =>
        {
            var choice = metricChoices.FirstOrDefault(c => c.Label == (args.SelectedItem as string));
            if (choice is not null) box.Text = choice.Metric.Name;
        };
        metricBox.QuerySubmitted += (box, args) =>
        {
            var choice = metricChoices.FirstOrDefault(c => c.Label == (args.ChosenSuggestion as string))
                ?? metricChoices.FirstOrDefault(c => string.Equals(c.Metric.Name, args.QueryText, StringComparison.OrdinalIgnoreCase));
            if (choice is not null) SelectMetric(widget, line, choice.Metric);
            else box.Text = current?.Name ?? $"Unknown metric ({line.Metric.MetricId})";
            box.IsSuggestionListOpen = false;
        };

        var fields = new List<UIElement> { Field("Metric", metricBox) };
        var stats = introspection.Stats.Where(s => current?.AvailableStatIds.Contains(s.Id) == true).ToList();
        if (stats.Count > 0)
        {
            fields.Add(Field("Statistic", FormControls.Choice($"Stat_{id}", "Statistic", stats.Select(s => s.Name).ToList(),
                stats.FindIndex(s => s.Id == line.Metric.StatId), index =>
                {
                    line.Metric.StatId = stats[index].Id;
                    changed();
                })));
        }
        else
        {
            fields.Add(Field("Statistic", new TextBlock
            {
                Text = $"Unavailable ({line.Metric.StatId})",
                TextWrapping = TextWrapping.Wrap,
            }));
        }

        if (current is not null)
        {
            var effectiveId = MetricResolver.ResolveDeviceId(current, line.Metric, introspection, preferences);
            if (preferences.EnablePerMetricDeviceSelection && current.DeviceType == MetricDeviceType.GraphicsAdapter && introspection.Adapters.Count > 0)
            {
                var devices = new List<DeviceChoice> { new(null, "Default adapter") };
                devices.AddRange(introspection.Adapters.Select(a => new DeviceChoice(a.Id, $"[{a.Id}] {a.Name}")));
                if (line.Metric.DeviceId is int stored && stored != 0 && devices.All(d => d.Id != stored))
                    devices.Add(new DeviceChoice(stored, $"[{stored}] Unknown GPU"));
                var labels = devices.Select(device => DeviceLabel(current, line.Metric, device)).ToList();
                var selection = devices.FindIndex(d => d.Id == (line.Metric.DeviceId == 0 ? null : line.Metric.DeviceId));
                var selector = FormControls.Choice($"Device_{id}", "GPU device", labels, Math.Max(0, selection), index =>
                {
                    line.Metric.DeviceId = devices[index].Id;
                    MetricResolver.Normalize(current, line.Metric, introspection, preferences);
                    changed();
                    RefreshWidget(widget);
                });
                fields.Add(Field("GPU device", selector));
            }
            var arraySize = MetricResolver.ArraySize(current, effectiveId);
            if (arraySize > 1)
            {
                fields.Add(Field("Array index", FormControls.Number($"ArrayIndex_{id}", "Array index",
                    line.Metric.ArrayIndex, 0, arraySize - 1, 1, value =>
                    {
                        line.Metric.ArrayIndex = (int)value;
                        changed();
                    })));
            }
        }
        content.Children.Add(Fields(fields, 3));

        if (current is not null && !MetricResolver.IsAvailable(current, line.Metric, introspection, preferences))
        {
            content.Children.Add(FormControls.Description(
                MetricResolver.AvailabilityReason(current, line.Metric, introspection, preferences) ?? "This metric is unavailable."));
        }
        if (widget is Graph graph)
        {
            var appearance = new List<UIElement>();
            if (graph.GraphType.Name == "Line")
                appearance.Add(Field("Right axis", FormControls.Toggle($"RightAxis_{id}", "Use right axis",
                    line.AxisAffinity == AxisAffinity.Right, value =>
                    {
                        line.AxisAffinity = value ? AxisAffinity.Right : AxisAffinity.Left;
                        changed();
                    })));
            appearance.Add(Field("Line color", Color($"LineColor_{id}", "Line color", line.LineColor, value => line.LineColor = value)));
            appearance.Add(Field("Fill color", Color($"FillColor_{id}", "Fill color", line.FillColor, value => line.FillColor = value)));
            content.Children.Add(Fields(appearance, 3));
        }
        var series = (Border)XamlReader.Load("""
            <Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                Background="{ThemeResource LayerFillColorDefaultBrush}"
                Padding="12" CornerRadius="6" />
            """);
        series.Child = content;
        return series;
    }

    private UIElement BuildDetails(Widget widget)
    {
        var panel = new StackPanel { Spacing = 10 };
        if (widget is Graph graph)
        {
            var options = graph.GraphType;
            var histogram = options.Name == "Histogram";
            panel.Children.Add(FormControls.Heading(histogram ? "Histogram data" : "Graph axes"));
            panel.Children.Add(Range($"LeftRange_{widget.Key}", histogram ? "Value range" : "Left value range",
                "Automatically scale the range, or enter the minimum and maximum values.",
                options.Range, options.AutoLeft, value => options.AutoLeft = value));
            if (!histogram)
            {
                panel.Children.Add(Range($"RightRange_{widget.Key}", "Right value range",
                    "Range for metric series assigned to the right axis.", options.RangeRight,
                    options.AutoRight, value => options.AutoRight = value));
                panel.Children.Add(FormControls.Row("Bottom axis", "Show the time axis along the bottom of this graph.",
                    FormControls.Toggle($"BottomAxis_{widget.Key}", "Show bottom axis", graph.ShowBottomAxis, value =>
                    {
                        graph.ShowBottomAxis = value;
                        changed();
                    })));
            }
            else
            {
                panel.Children.Add(NumberSetting($"BinCount_{widget.Key}", "Number of bins", "Number of bars in the histogram.",
                    options.BinCount, 5, 200, 1, value => options.BinCount = (int)value));
                panel.Children.Add(Range($"CountRange_{widget.Key}", "Count range", "Range of counts displayed in each bin.",
                    options.CountRange, options.AutoCount, value => options.AutoCount = value));
                panel.Children.Add(FormControls.Description(
                    $"Expected data points: {preferences.TimeRange:g} seconds x {preferences.MetricPollRate:g} Hz = {preferences.TimeRange * preferences.MetricPollRate:g}. Change time scale in Overlay settings and poll rate in Data processing settings."));
            }
            panel.Children.Add(FormControls.Heading("Style"));
            panel.Children.Add(NumberSetting($"GraphHeight_{widget.Key}", "Graph height", "Vertical size of this graph in pixels.",
                graph.Height, 20, 450, 1, value => graph.Height = value));
            panel.Children.Add(NumberSetting($"VerticalDivisions_{widget.Key}", "Vertical divisions", "Number of vertical divisions in the grid.",
                graph.VDivs, 1, 40, 1, value => graph.VDivs = (int)value));
            panel.Children.Add(NumberSetting($"HorizontalDivisions_{widget.Key}", "Horizontal divisions", "Number of horizontal divisions in the grid.",
                graph.HDivs, 1, 100, 1, value => graph.HDivs = (int)value));
            panel.Children.Add(NumberSetting($"GraphTextSize_{widget.Key}", "Font size", "Text size for this graph.",
                graph.TextSize, 5, 80, 0.5, value => graph.TextSize = value));
            panel.Children.Add(ColorSetting($"GridColor_{widget.Key}", "Grid color", graph.GridColor, value => graph.GridColor = value));
            panel.Children.Add(ColorSetting($"BackgroundColor_{widget.Key}", "Background color", graph.BackgroundColor, value => graph.BackgroundColor = value));
            panel.Children.Add(ColorSetting($"TextColor_{widget.Key}", "Text color", graph.TextColor, value => graph.TextColor = value));
            panel.Children.Add(ColorSetting($"DividerColor_{widget.Key}", "Divider color", graph.DividerColor, value => graph.DividerColor = value));
            panel.Children.Add(ColorSetting($"BorderColor_{widget.Key}", "Border color", graph.BorderColor, value => graph.BorderColor = value));
        }
        else if (widget is Readout readout)
        {
            panel.Children.Add(NumberSetting($"ReadoutFontSize_{widget.Key}", "Font size", "Text size for this readout.",
                readout.FontSize, 5, 80, 0.5, value => readout.FontSize = value));
            panel.Children.Add(ColorSetting($"ReadoutFontColor_{widget.Key}", "Text color", readout.FontColor, value => readout.FontColor = value));
            panel.Children.Add(ColorSetting($"ReadoutBackgroundColor_{widget.Key}", "Background color", readout.BackgroundColor, value => readout.BackgroundColor = value));
            panel.Children.Add(FormControls.Row("Show label", "Display the metric name next to its value.",
                FormControls.Toggle($"ShowLabel_{widget.Key}", "Show label", readout.ShowLabel, value =>
                {
                    readout.ShowLabel = value;
                    changed();
                })));
        }
        if (preferences.EnablePerMetricDeviceSelection)
        {
            panel.Children.Add(FormControls.Heading("Metric labels"));
            panel.Children.Add(FormControls.Row("Device ID", "Include the GPU device ID in metric labels.",
                FormControls.Toggle($"LabelDeviceId_{widget.Key}", "Include device ID", widget.LabelIncludeDeviceId, value =>
                {
                    widget.LabelIncludeDeviceId = value;
                    changed();
                })));
            panel.Children.Add(FormControls.Row("Device name", "Include the GPU name in metric labels.",
                FormControls.Toggle($"LabelDeviceName_{widget.Key}", "Include device name", widget.LabelIncludeDeviceName, value =>
                {
                    widget.LabelIncludeDeviceName = value;
                    changed();
                })));
        }
        return panel;
    }

    private UIElement Range(string id, string title, string description, ObservableCollection<double> values,
        bool automatic, Action<bool> setAutomatic)
    {
        while (values.Count < 2) values.Add(values.Count == 0 ? 0 : 150);
        var fields = new StackPanel { Spacing = 8 };
        var minimum = FormControls.Number($"{id}Minimum", "Minimum", values[0], double.MinValue, values[1], 1, value =>
        {
            values[0] = value;
            changed();
        });
        var maximum = FormControls.Number($"{id}Maximum", "Maximum", values[1], values[0], double.MaxValue, 1, value =>
        {
            values[1] = value;
            changed();
        });
        minimum.ValueChanged += (_, _) => maximum.Minimum = minimum.Value;
        maximum.ValueChanged += (_, _) => minimum.Maximum = maximum.Value;
        minimum.IsEnabled = maximum.IsEnabled = !automatic;
        fields.Children.Add(Field("Autoscale", FormControls.Toggle($"{id}Autoscale", "Autoscale", automatic, value =>
        {
            setAutomatic(value);
            minimum.IsEnabled = maximum.IsEnabled = !value;
            changed();
        })));
        fields.Children.Add(Fields(new[] { Field("Minimum", minimum), Field("Maximum", maximum) }, 2));
        return FormControls.Row(title, description, fields);
    }

    private UIElement NumberSetting(string id, string title, string description, double value,
        double minimum, double maximum, double step, Action<double> setValue) =>
        FormControls.Row(title, description, FormControls.Number(id, title, value, minimum, maximum, step, number =>
        {
            setValue(number);
            changed();
        }));

    private UIElement ColorSetting(string id, string title, RgbaColor color, Action<RgbaColor> setValue) =>
        FormControls.Row(title, "", Color(id, title, color, setValue));

    private UIElement Color(string id, string title, RgbaColor color, Action<RgbaColor> setValue) =>
        FormControls.Color(id, title,
            Windows.UI.Color.FromArgb((byte)Math.Clamp(Math.Round(color.A * 255), 0, 255),
                (byte)Math.Clamp(color.R, 0, 255), (byte)Math.Clamp(color.G, 0, 255), (byte)Math.Clamp(color.B, 0, 255)), value =>
            {
                setValue(new RgbaColor(value.R, value.G, value.B, value.A / 255.0));
                changed();
            });

    private void SelectMetric(Widget widget, WidgetMetric line, Metric metric)
    {
        if (line.Metric.MetricId == metric.Id) return;
        var previous = introspection.Metrics.FirstOrDefault(m => m.Id == line.Metric.MetricId);
        if (metric.DeviceType == MetricDeviceType.GraphicsAdapter && previous?.DeviceType != MetricDeviceType.GraphicsAdapter)
            line.Metric.DeviceId = null;
        line.Metric.MetricId = metric.Id;
        if (!metric.AvailableStatIds.Contains(line.Metric.StatId))
            line.Metric.StatId = metric.AvailableStatIds.FirstOrDefault();
        MetricResolver.Normalize(metric, line.Metric, introspection, preferences);
        if (!metric.Numeric && widget is Graph)
        {
            var readout = Widget.CreateReadout(line.Metric);
            readout.Metrics[0].LineColor = line.LineColor;
            readout.Metrics[0].FillColor = line.FillColor;
            MutateCollection(() => widgets[widgets.IndexOf(widget)] = readout);
            return;
        }
        changed();
        RefreshWidget(widget);
    }

    private void ChangeType(Widget widget, bool graph)
    {
        if (graph == (widget is Graph)) return;
        var metric = widget.Metrics.FirstOrDefault()?.Metric;
        if (metric is null)
        {
            var defaultMetric = DefaultMetric(graph);
            if (defaultMetric is null) return;
            metric = MetricResolver.CreateQualified(defaultMetric, introspection, preferences);
        }
        Widget replacement = graph ? Widget.CreateGraph(metric) : Widget.CreateReadout(metric);
        MutateCollection(() => widgets[widgets.IndexOf(widget)] = replacement);
    }

    private Metric? DefaultMetric(bool numericOnly) => numericOnly
        ? introspection.Metrics.FirstOrDefault(m => m.Id == 8 && m.Numeric)
            ?? introspection.Metrics.FirstOrDefault(m => m.Numeric)
        : introspection.Metrics.FirstOrDefault();

    private void AddWidget(bool graph)
    {
        var metric = DefaultMetric(graph);
        if (metric is null) return;
        var qualified = MetricResolver.CreateQualified(metric, introspection, preferences);
        Widget widget = graph ? Widget.CreateGraph(qualified) : Widget.CreateReadout(qualified);
        MutateCollection(() => widgets.Add(widget));
        list.ScrollIntoView(widget, ScrollIntoViewAlignment.Leading);
    }

    private void MutateCollection(Action mutation)
    {
        suppressCollectionRefresh = true;
        try { mutation(); }
        finally { suppressCollectionRefresh = false; }
        changed();
        Refresh();
    }

    private void MoveWidget(Widget widget, int offset)
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            var from = widgets.IndexOf(widget);
            var to = from + offset;
            if (from < 0 || to < 0 || to >= widgets.Count) return;
            suppressCollectionRefresh = true;
            try { widgets.Move(from, to); }
            finally { suppressCollectionRefresh = false; }
            changed();
            Refresh();
            list.ScrollIntoView(widget);
        });
    }

    private string WidgetTitle(Widget widget)
    {
        var metric = introspection.Metrics.FirstOrDefault(m => m.Id == widget.Metrics.FirstOrDefault()?.Metric.MetricId);
        return metric?.Name ?? (widget is Graph ? "Graph" : "Readout");
    }

    private string MetricLabel(Metric metric, QualifiedMetric selected)
    {
        var probe = MetricResolver.CreateQualified(metric, introspection, preferences);
        probe.DeviceId = selected.DeviceId;
        MetricResolver.Normalize(metric, probe, introspection, preferences);
        var available = MetricResolver.IsAvailable(metric, probe, introspection, preferences);
        if (!available && metric.DeviceType == MetricDeviceType.GraphicsAdapter && preferences.EnablePerMetricDeviceSelection)
            available = introspection.Adapters.Any(adapter =>
            {
                probe.DeviceId = adapter.Id;
                return MetricResolver.IsAvailable(metric, probe, introspection, preferences);
            });
        return available ? metric.Name : $"{metric.Name} (unavailable)";
    }

    private string DeviceLabel(Metric metric, QualifiedMetric selected, DeviceChoice device)
    {
        var probe = MetricResolver.CreateQualified(metric, introspection, preferences, selected.StatId);
        probe.DeviceId = device.Id;
        MetricResolver.Normalize(metric, probe, introspection, preferences);
        return MetricResolver.IsAvailable(metric, probe, introspection, preferences)
            ? device.Label : $"{device.Label} (unavailable)";
    }

    private AppBarButton AsyncCommand(string id, string label, Symbol icon, Func<Task> action)
    {
        var button = new AppBarButton { Label = label, Icon = new SymbolIcon(icon) };
        AutomationProperties.SetAutomationId(button, id);
        button.Click += async (_, _) =>
        {
            button.IsEnabled = false;
            try
            {
                await action();
                Refresh();
            }
            catch (Exception error)
            {
                notice.Title = label;
                notice.Message = error.Message;
                notice.IsOpen = true;
            }
            finally { button.IsEnabled = true; }
        };
        return button;
    }

    private static AppBarButton Command(string id, string label, Symbol icon, Action action)
    {
        var button = new AppBarButton { Label = label, Icon = new SymbolIcon(icon) };
        AutomationProperties.SetAutomationId(button, id);
        button.Click += (_, _) => action();
        return button;
    }

    private static Button IconButton(string id, string name, object icon, Action action, bool enabled = true)
    {
        var button = new Button
        {
            Content = icon is Symbol symbol ? new SymbolIcon(symbol)
                : new FontIcon { Glyph = (string)icon, FontFamily = new FontFamily("Segoe Fluent Icons") },
            IsEnabled = enabled,
            Padding = new Thickness(8),
        };
        AutomationProperties.SetAutomationId(button, id);
        AutomationProperties.SetName(button, name);
        ToolTipService.SetToolTip(button, name);
        button.Click += (_, _) => action();
        return button;
    }

    private static UIElement Field(string label, UIElement control)
    {
        var field = new StackPanel { Spacing = 5 };
        field.Children.Add(FormControls.Description(label));
        if (control is FrameworkElement element) element.HorizontalAlignment = HorizontalAlignment.Stretch;
        field.Children.Add(control);
        return field;
    }

    private static Grid Fields(IEnumerable<UIElement> content, int maximumColumns)
    {
        var grid = new Grid { ColumnSpacing = 12, RowSpacing = 12 };
        var fields = content.Select(child =>
        {
            var host = new Grid();
            host.Children.Add(child);
            return host;
        }).ToList();
        foreach (var field in fields) grid.Children.Add(field);
        var columns = 0;
        void Arrange(double width)
        {
            var next = Math.Clamp((int)(width / 210), 1, Math.Min(maximumColumns, Math.Max(1, fields.Count)));
            if (columns == next) return;
            columns = next;
            grid.ColumnDefinitions.Clear();
            grid.RowDefinitions.Clear();
            for (var column = 0; column < columns; column++)
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            for (var row = 0; row < (fields.Count + columns - 1) / columns; row++)
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            for (var index = 0; index < fields.Count; index++)
            {
                Grid.SetColumn(fields[index], index % columns);
                Grid.SetRow(fields[index], index / columns);
            }
        }
        Arrange(0);
        grid.SizeChanged += (_, args) => Arrange(args.NewSize.Width);
        return grid;
    }

    private sealed record MetricChoice(Metric Metric, string Label);
    private sealed record DeviceChoice(int? Id, string Label);
}
