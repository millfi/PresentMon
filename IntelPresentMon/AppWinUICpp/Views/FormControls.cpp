// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT

#include "../pch.h"
#include "FormControls.h"
#include "../Core/UiDiagnostics.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <string_view>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Automation;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;
using namespace Windows::UI;

namespace
{
    hstring H(const std::string& text)
    {
        if (text.empty()) return {};
        auto const length = (int)text.size();
        auto const required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
        if (required == 0) return {};
        std::wstring converted((size_t)required, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, converted.data(), required);
        return hstring{ converted };
    }

    std::string Hex(Color value)
    {
        char buffer[10]{};
        sprintf_s(buffer, "#%02X%02X%02X%02X", value.A, value.R, value.G, value.B);
        return buffer;
    }

    std::string Lower(std::string text)
    {
        for (auto& character : text) character = (char)std::tolower((unsigned char)character);
        return text;
    }

    fire_and_forget RunAsyncButton(Button button, pmon::ui::views::FormControls::AsyncCallback action, std::string id)
    {
        button.IsEnabled(false);
        try
        {
            co_await action();
            pmon::ui::diagnostics::Record("button.complete", {{"control", id}});
        }
        catch (const hresult_error& error)
        {
            pmon::ui::diagnostics::Exception(id.c_str(), error.code(), to_string(error.message()));
            auto message = TextBlock{};
            message.Text(error.message());
            message.TextWrapping(TextWrapping::Wrap);
            message.MaxWidth(360);
            AutomationProperties::SetName(message, H("Action failed: ") + error.message());
            auto flyout = Flyout{};
            flyout.Content(message);
            if (button.XamlRoot()) flyout.ShowAt(button);
        }
        catch (const std::exception& error)
        {
            pmon::ui::diagnostics::Exception(id.c_str(), E_FAIL, error.what());
            auto message = TextBlock{};
            message.Text(H(error.what()));
            message.TextWrapping(TextWrapping::Wrap);
            message.MaxWidth(360);
            AutomationProperties::SetName(message, H("Action failed: ") + H(error.what()));
            auto flyout = Flyout{};
            flyout.Content(message);
            if (button.XamlRoot()) flyout.ShowAt(button);
        }
        button.IsEnabled(true);
    }
}

namespace pmon::ui::views::FormControls
{
    void CommitPendingNumbers(const DependencyObject& root)
    {
        if (!root) return;
        if (auto number = root.try_as<NumberBox>()) {
            // NumberBox.Text is the committed value; its inner TextBox can still
            // contain pending keystrokes. Use WinUI's parser and range validation.
            auto findEditor = [](auto&& self, const DependencyObject& parent) -> TextBox {
                if (auto editor = parent.try_as<TextBox>()) return editor;
                for (int i = 0; i < VisualTreeHelper::GetChildrenCount(parent); ++i) {
                    if (auto editor = self(self, VisualTreeHelper::GetChild(parent, i))) return editor;
                }
                return nullptr;
            };
            if (auto editor = findEditor(findEditor, number); editor && editor.Text() != number.Text()) {
                diagnostics::Record("number.commit", {{"control", to_string(AutomationProperties::GetAutomationId(number))},
                    {"previous", number.Value()}, {"input", to_string(editor.Text()).substr(0, 128)}});
                number.Text(editor.Text());
            }
            return;
        }
        // Take a snapshot: a callback may update the visual tree.
        std::vector<DependencyObject> children;
        for (int i = 0; i < VisualTreeHelper::GetChildrenCount(root); ++i) children.push_back(VisualTreeHelper::GetChild(root, i));
        for (auto const& child : children) CommitPendingNumbers(child);
    }

    Border Row(const std::string& title, const std::string& description, const UIElement& control)
    {
        auto border = Microsoft::UI::Xaml::Markup::XamlReader::Load(LR"(
            <Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    Background="{ThemeResource CardBackgroundFillColorDefaultBrush}"
                    BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}"
                    BorderThickness="1" CornerRadius="8" Padding="16" />
            )").as<Border>();

        auto grid = Grid{};
        grid.ColumnSpacing(24.0);
        grid.RowSpacing(12.0);
        auto firstColumn = ColumnDefinition{};
        firstColumn.Width(GridLength{ 1.0, GridUnitType::Star });
        auto secondColumn = ColumnDefinition{};
        secondColumn.Width(GridLength{ 1.2, GridUnitType::Star });
        grid.ColumnDefinitions().Append(firstColumn);
        grid.ColumnDefinitions().Append(secondColumn);
        grid.RowDefinitions().Append(RowDefinition{});
        grid.RowDefinitions().Append(RowDefinition{});

        auto labels = StackPanel{};
        labels.Spacing(4.0);
        labels.VerticalAlignment(VerticalAlignment::Center);
        auto label = TextBlock{};
        label.Text(H(title));
        label.TextWrapping(TextWrapping::Wrap);
        if (auto style = Application::Current().Resources().TryLookup(box_value(H("BodyStrongTextBlockStyle"))).try_as<Style>())
        {
            label.Style(style);
        }
        labels.Children().Append(label);
        if (!description.empty())
        {
            labels.Children().Append(Description(description));
            AutomationProperties::SetHelpText(control, H(description));
        }

        auto controlHost = Grid{};
        controlHost.Children().Append(control);
        grid.Children().Append(labels);
        grid.Children().Append(controlHost);
        if (auto element = control.try_as<FrameworkElement>()) element.VerticalAlignment(VerticalAlignment::Center);

        auto wide = std::make_shared<std::optional<bool>>();
        auto labelsWeak = make_weak(labels);
        auto controlHostWeak = make_weak(controlHost);
        auto arrange = [labelsWeak, controlHostWeak, wide](double width)
        {
            auto labels = labelsWeak.get();
            auto controlHost = controlHostWeak.get();
            if (!labels || !controlHost) return;
            auto const next = width >= 620.0;
            if (wide->has_value() && wide->value() == next) return;
            *wide = next;
            Grid::SetColumnSpan(labels, next ? 1 : 2);
            Grid::SetColumn(controlHost, next ? 1 : 0);
            Grid::SetRow(controlHost, next ? 0 : 1);
            Grid::SetColumnSpan(controlHost, next ? 1 : 2);
        };
        arrange(0.0);
        grid.SizeChanged([arrange](const IInspectable&, const SizeChangedEventArgs& args)
        {
            arrange(args.NewSize().Width);
        });
        border.Child(grid);
        return border;
    }

    TextBlock Heading(const std::string& title)
    {
        auto heading = TextBlock{};
        heading.Text(H(title));
        heading.Margin(Thickness{ 0.0, 12.0, 0.0, 2.0 });
        heading.TextWrapping(TextWrapping::Wrap);
        if (auto style = Application::Current().Resources().TryLookup(box_value(H("SubtitleTextBlockStyle"))).try_as<Style>())
        {
            heading.Style(style);
        }
        AutomationProperties::SetHeadingLevel(heading, Peers::AutomationHeadingLevel::Level2);
        return heading;
    }

    TextBlock Description(const std::string& text)
    {
        auto detail = Microsoft::UI::Xaml::Markup::XamlReader::Load(LR"(
            <TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                       Foreground="{ThemeResource TextFillColorSecondaryBrush}"
                       TextWrapping="Wrap" FontSize="12" />
            )").as<TextBlock>();
        detail.Text(H(text));
        return detail;
    }

    ToggleSwitch Toggle(const std::string& id, const std::string& name, bool value, std::function<void(bool)> changed)
    {
        auto toggle = ToggleSwitch{};
        toggle.IsOn(value);
        toggle.OnContent(box_value(H("On")));
        toggle.OffContent(box_value(H("Off")));
        toggle.HorizontalAlignment(HorizontalAlignment::Left);
        toggle.MinWidth(100.0);
        Identify(toggle, id, name);
        auto weak = make_weak(toggle);
        toggle.Toggled([changed, weak, id](const IInspectable&, const IInspectable&)
        {
            if (auto control = weak.get()) {
                diagnostics::Record("toggle.change", {{"control", id}, {"value", control.IsOn()}});
                changed(control.IsOn());
            }
        });
        return toggle;
    }

    NumberBox Number(const std::string& id, const std::string& name, double value, double minimum,
        double maximum, double step, std::function<void(double)> changed)
    {
        auto number = NumberBox{};
        number.Minimum(minimum);
        number.Maximum(maximum);
        number.SmallChange(step);
        number.LargeChange(step * 10.0);
        number.Value(std::isfinite(value) ? std::clamp(value, minimum, maximum) : minimum);
        number.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
        number.ValidationMode(NumberBoxValidationMode::InvalidInputOverwritten);
        number.HorizontalAlignment(HorizontalAlignment::Stretch);
        number.MinWidth(120.0);
        Identify(number, id, name);
        auto previous = std::make_shared<double>(number.Value());
        number.ValueChanged([changed, previous, id](const NumberBox& control, const NumberBoxValueChangedEventArgs& args)
        {
            if (!std::isfinite(args.NewValue()))
            {
                control.Value(*previous);
                return;
            }
            *previous = args.NewValue();
            diagnostics::Record("number.change", {{"control", id}, {"old", args.OldValue()}, {"new", args.NewValue()}});
            try { changed(args.NewValue()); }
            catch (const hresult_error& error) {
                diagnostics::Exception(id.c_str(), error.code(), to_string(error.message()));
                throw;
            }
            catch (const std::exception& error) {
                diagnostics::Exception(id.c_str(), E_FAIL, error.what());
                throw;
            }
        });
        return number;
    }

    TextBox Text(const std::string& id, const std::string& name, const std::string& value,
        std::function<void(const std::string&)> changed, bool multiline)
    {
        auto text = TextBox{};
        text.Text(H(value));
        text.AcceptsReturn(multiline);
        text.TextWrapping(multiline ? TextWrapping::Wrap : TextWrapping::NoWrap);
        text.HorizontalAlignment(HorizontalAlignment::Stretch);
        Identify(text, id, name);
        auto weak = make_weak(text);
        text.TextChanged([changed, weak, id](const IInspectable&, const TextChangedEventArgs&)
        {
            if (auto control = weak.get()) {
                diagnostics::Record("text.change", {{"control", id}, {"length", control.Text().size()}});
                changed(to_string(control.Text()));
            }
        });
        return text;
    }

    ComboBox Choice(const std::string& id, const std::string& name, const std::vector<std::string>& options,
        int selectedIndex, std::function<void(int)> changed)
    {
        auto combo = ComboBox{};
        combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        combo.PlaceholderText(H(options.empty() ? "None available" : "Select an option"));
        combo.IsEnabled(!options.empty());
        for (const auto& option : options) combo.Items().Append(box_value(H(option)));
        combo.SelectedIndex(selectedIndex >= 0 && selectedIndex < (int)options.size() ? selectedIndex : -1);
        Identify(combo, id, name);
        auto weak = make_weak(combo);
        combo.SelectionChanged([changed, weak, id](const IInspectable&, const SelectionChangedEventArgs&)
        {
            if (auto control = weak.get(); control && control.SelectedIndex() >= 0) {
                diagnostics::Record("choice.change", {{"control", id}, {"index", control.SelectedIndex()}});
                changed(control.SelectedIndex());
            }
        });
        return combo;
    }

    Button Color(const std::string& id, const std::string& name, Windows::UI::Color value,
        std::function<void(Windows::UI::Color)> changed)
    {
        auto swatch = Border{};
        swatch.Width(24.0);
        swatch.Height(24.0);
        swatch.CornerRadius(CornerRadius{ 4.0 });
        swatch.Background(SolidColorBrush{ value });
        auto label = TextBlock{};
        label.Text(H(Hex(value)));
        label.VerticalAlignment(VerticalAlignment::Center);
        auto content = StackPanel{};
        content.Orientation(Orientation::Horizontal);
        content.Spacing(10.0);
        content.Children().Append(swatch);
        content.Children().Append(label);
        auto button = Button{};
        button.Content(content);
        button.HorizontalAlignment(HorizontalAlignment::Left);
        Identify(button, id, name);
        ToolTipService::SetToolTip(button, box_value(H("Choose ") + H(Lower(name))));

        auto flyout = Flyout{};
        auto swatchWeak = make_weak(swatch);
        auto labelWeak = make_weak(label);
        flyout.Opening([swatchWeak, labelWeak, value, changed, id, name](const IInspectable& sender, const IInspectable&)
        {
            auto flyout = sender.as<Flyout>();
            if (flyout.Content()) return;
            auto picker = ColorPicker{};
            picker.Color(value);
            picker.IsAlphaEnabled(true);
            picker.MinWidth(240.0);
            picker.MaxWidth(320.0);
            Identify(picker, id + "Picker", name);
            picker.ColorChanged([swatchWeak, labelWeak, changed, id](const ColorPicker&, const ColorChangedEventArgs& args)
            {
                diagnostics::Record("color.change", {{"control", id}, {"value", Hex(args.NewColor())}});
                if (auto swatch = swatchWeak.get()) swatch.Background(SolidColorBrush{ args.NewColor() });
                if (auto label = labelWeak.get()) label.Text(H(Hex(args.NewColor())));
                changed(args.NewColor());
            });
            flyout.Content(picker);
        });
        button.Flyout(flyout);
        return button;
    }

    Button ActionButton(const std::string& id, const std::string& text, std::function<void()> action)
    {
        auto button = Button{};
        button.Content(box_value(H(text)));
        button.HorizontalAlignment(HorizontalAlignment::Left);
        Identify(button, id, text);
        button.Click([action, id](const IInspectable&, const RoutedEventArgs&)
        {
            diagnostics::Record("button.invoke", {{"control", id}});
            action();
        });
        return button;
    }

    Button AsyncButton(const std::string& id, const std::string& text, AsyncCallback action)
    {
        auto button = Button{};
        button.Content(box_value(H(text)));
        button.HorizontalAlignment(HorizontalAlignment::Left);
        Identify(button, id, text);
        auto weak = make_weak(button);
        button.Click([weak, action, id](const IInspectable&, const RoutedEventArgs&)
        {
            diagnostics::Record("button.invoke", {{"control", id}});
            if (auto control = weak.get()) RunAsyncButton(control, action, id);
        });
        return button;
    }

    void Identify(const DependencyObject& element, const std::string& id, const std::string& name)
    {
        AutomationProperties::SetAutomationId(element, H(id));
        AutomationProperties::SetName(element, H(name));
    }
}
