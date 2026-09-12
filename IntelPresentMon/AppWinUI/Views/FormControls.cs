// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace PresentMon.UI.Views;

public static class FormControls
{
    public static Border Row(string title, string description, UIElement control)
    {
        var border = (Border)XamlReader.Load("""
            <Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    Background="{ThemeResource CardBackgroundFillColorDefaultBrush}"
                    BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}"
                    BorderThickness="1" CornerRadius="8" Padding="16" />
            """);
        var grid = new Grid { ColumnSpacing = 24, RowSpacing = 12 };
        grid.ColumnDefinitions.Add(new() { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new() { Width = new GridLength(1.2, GridUnitType.Star) });
        grid.RowDefinitions.Add(new() { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new() { Height = GridLength.Auto });
        var labels = new StackPanel { Spacing = 4, VerticalAlignment = VerticalAlignment.Center };
        var label = new TextBlock { Text = title, TextWrapping = TextWrapping.Wrap };
        if (Application.Current.Resources.TryGetValue("BodyStrongTextBlockStyle", out var style))
            label.Style = (Style)style;
        labels.Children.Add(label);
        if (!string.IsNullOrWhiteSpace(description))
        {
            var detail = Description(description);
            labels.Children.Add(detail);
            AutomationProperties.SetHelpText(control, description);
        }
        var controlHost = new Grid();
        controlHost.Children.Add(control);
        grid.Children.Add(labels);
        grid.Children.Add(controlHost);
        if (control is FrameworkElement element)
            element.VerticalAlignment = VerticalAlignment.Center;
        bool? wide = null;
        void Arrange(double width)
        {
            var next = width >= 620;
            if (next == wide) return;
            wide = next;
            Grid.SetColumnSpan(labels, next ? 1 : 2);
            Grid.SetColumn(controlHost, next ? 1 : 0);
            Grid.SetRow(controlHost, next ? 0 : 1);
            Grid.SetColumnSpan(controlHost, next ? 1 : 2);
        }
        Arrange(0);
        grid.SizeChanged += (_, args) => Arrange(args.NewSize.Width);
        border.Child = grid;
        return border;
    }

    public static TextBlock Heading(string title)
    {
        var heading = new TextBlock
        {
            Text = title,
            Margin = new Thickness(0, 12, 0, 2),
            TextWrapping = TextWrapping.Wrap,
        };
        if (Application.Current.Resources.TryGetValue("SubtitleTextBlockStyle", out var style))
            heading.Style = (Style)style;
        AutomationProperties.SetHeadingLevel(heading, AutomationHeadingLevel.Level2);
        return heading;
    }

    public static TextBlock Description(string text)
    {
        var detail = (TextBlock)XamlReader.Load("""
            <TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                       Foreground="{ThemeResource TextFillColorSecondaryBrush}"
                       TextWrapping="Wrap" FontSize="12" />
            """);
        detail.Text = text;
        return detail;
    }

    public static ToggleSwitch Toggle(string id, string name, bool value, Action<bool> changed)
    {
        var toggle = new ToggleSwitch
        {
            IsOn = value,
            OnContent = "On",
            OffContent = "Off",
            HorizontalAlignment = HorizontalAlignment.Left,
            MinWidth = 100,
        };
        Identify(toggle, id, name);
        toggle.Toggled += (_, _) => changed(toggle.IsOn);
        return toggle;
    }

    public static NumberBox Number(string id, string name, double value, double minimum,
        double maximum, double step, Action<double> changed)
    {
        var number = new NumberBox
        {
            Minimum = minimum,
            Maximum = maximum,
            SmallChange = step,
            LargeChange = step * 10,
            Value = double.IsFinite(value) ? Math.Clamp(value, minimum, maximum) : minimum,
            SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact,
            ValidationMode = NumberBoxValidationMode.InvalidInputOverwritten,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            MinWidth = 120,
        };
        Identify(number, id, name);
        var previous = number.Value;
        number.ValueChanged += (_, args) =>
        {
            if (!double.IsFinite(args.NewValue))
            {
                number.Value = previous;
                return;
            }
            previous = args.NewValue;
            changed(args.NewValue);
        };
        return number;
    }

    public static TextBox Text(string id, string name, string value, Action<string> changed,
        bool multiline = false)
    {
        var text = new TextBox
        {
            Text = value,
            AcceptsReturn = multiline,
            TextWrapping = multiline ? TextWrapping.Wrap : TextWrapping.NoWrap,
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        Identify(text, id, name);
        text.TextChanged += (_, _) => changed(text.Text);
        return text;
    }

    public static ComboBox Choice(string id, string name, IReadOnlyList<string> options,
        int selectedIndex, Action<int> changed)
    {
        var combo = new ComboBox
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            PlaceholderText = options.Count == 0 ? "None available" : "Select an option",
            IsEnabled = options.Count > 0,
        };
        foreach (var option in options) combo.Items.Add(option);
        combo.SelectedIndex = selectedIndex >= 0 && selectedIndex < options.Count ? selectedIndex : -1;
        Identify(combo, id, name);
        combo.SelectionChanged += (_, _) =>
        {
            if (combo.SelectedIndex >= 0) changed(combo.SelectedIndex);
        };
        return combo;
    }

    public static UIElement Color(string id, string name, Color value, Action<Color> changed)
    {
        var swatch = new Border
        {
            Width = 24,
            Height = 24,
            CornerRadius = new CornerRadius(4),
            Background = new SolidColorBrush(value),
        };
        var label = new TextBlock { Text = Hex(value), VerticalAlignment = VerticalAlignment.Center };
        var content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
        content.Children.Add(swatch);
        content.Children.Add(label);
        var button = new Button { Content = content, HorizontalAlignment = HorizontalAlignment.Left };
        Identify(button, id, name);
        ToolTipService.SetToolTip(button, "Choose " + name.ToLowerInvariant());
        var flyout = new Flyout();
        flyout.Opening += (_, _) =>
        {
            if (flyout.Content is not null) return;
            var picker = new ColorPicker { Color = value, IsAlphaEnabled = true, MinWidth = 240, MaxWidth = 320 };
            Identify(picker, id + "Picker", name);
            picker.ColorChanged += (_, args) =>
            {
                swatch.Background = new SolidColorBrush(args.NewColor);
                label.Text = Hex(args.NewColor);
                changed(args.NewColor);
            };
            flyout.Content = picker;
        };
        button.Flyout = flyout;
        return button;
    }

    public static Button ActionButton(string id, string text, Action action)
    {
        var button = new Button { Content = text, HorizontalAlignment = HorizontalAlignment.Left };
        Identify(button, id, text);
        button.Click += (_, _) => action();
        return button;
    }

    public static Button AsyncButton(string id, string text, Func<Task> action)
    {
        var button = new Button { Content = text, HorizontalAlignment = HorizontalAlignment.Left };
        Identify(button, id, text);
        button.Click += async (_, _) =>
        {
            button.IsEnabled = false;
            try
            {
                await action();
            }
            catch (Exception error)
            {
                var message = new TextBlock
                {
                    Text = error.Message,
                    TextWrapping = TextWrapping.Wrap,
                    MaxWidth = 360,
                };
                var flyout = new Flyout { Content = message };
                AutomationProperties.SetName(message, "Action failed: " + error.Message);
                if (button.XamlRoot is not null) flyout.ShowAt(button);
            }
            finally
            {
                button.IsEnabled = true;
            }
        };
        return button;
    }

    public static void Identify(DependencyObject element, string id, string name)
    {
        AutomationProperties.SetAutomationId(element, id);
        AutomationProperties.SetName(element, name);
    }

    private static string Hex(Color value) => $"#{value.A:X2}{value.R:X2}{value.G:X2}{value.B:X2}";
}
