using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using PresentMon.UI.Core;

namespace PresentMon.UI.Views;

public static class HotkeyEditor
{
    // Codes are Key::Code indices from Core/source/win/Key.h, not virtual keys.
    private static readonly KeyOption[] Keys =
    [
        new(0, "Backspace"),
        new(1, "Tab"),
        new(2, "Enter"),
        new(6, "Pause"),
        new(7, "Caps Lock"),
        new(8, "Escape"),
        new(9, "Space"),
        new(10, "Page Up"),
        new(11, "Page Down"),
        new(12, "End"),
        new(13, "Home"),
        new(14, "Left"),
        new(15, "Up"),
        new(16, "Right"),
        new(17, "Down"),
        new(18, "Print"),
        new(19, "Print Screen"),
        new(20, "Insert"),
        new(21, "Delete"),
        .. Enumerable.Range(0, 10).Select(index => new KeyOption(22 + index, index.ToString())),
        .. Enumerable.Range(0, 26).Select(index => new KeyOption(32 + index, ((char)('A' + index)).ToString())),
        .. Enumerable.Range(0, 10).Select(index => new KeyOption(60 + index, $"Num {index}")),
        new(70, "Num Multiply"),
        new(71, "Num Add"),
        new(72, "Num Subtract"),
        new(73, "Num Decimal"),
        new(74, "Num Divide"),
        .. Enumerable.Range(0, 24).Select(index => new KeyOption(75 + index, $"F{index + 1}")),
        new(99, "Num Lock"),
        new(100, "Scroll Lock"),
    ];

    // Codes match ModSet: Alt = 1, Ctrl = 2, Shift = 4, Win = 8.
    private static readonly ModifierOption[] Modifiers =
    [
        new(2, "Ctrl"),
        new(1, "Alt"),
        new(4, "Shift"),
        new(8, "Win"),
    ];

    public static string Format(HotkeyBinding? binding)
    {
        if (binding?.Combination is not { } combination)
        {
            return "Not assigned";
        }

        var labels = Modifiers.Where(modifier => combination.Modifiers.Contains(modifier.Code))
            .Select(modifier => modifier.Text)
            .ToList();
        labels.Add(Keys.FirstOrDefault(key => key.Code == combination.Key)?.Text ?? $"Key {combination.Key}");
        return string.Join(" + ", labels);
    }

    public static async Task<HotkeyBinding?> EditAsync(XamlRoot root, HotkeyBinding current)
    {
        ArgumentNullException.ThrowIfNull(root);
        ArgumentNullException.ThrowIfNull(current);

        var keySelector = new ComboBox
        {
            Header = "Key",
            PlaceholderText = "Choose a key",
            ItemsSource = Keys,
            DisplayMemberPath = nameof(KeyOption.Text),
            SelectedItem = Keys.FirstOrDefault(key => key.Code == current.Combination?.Key),
            HorizontalAlignment = HorizontalAlignment.Stretch,
            IsTextSearchEnabled = true,
        };
        AutomationProperties.SetAutomationId(keySelector, "HotkeyEditorKey");
        AutomationProperties.SetName(keySelector, "Hotkey key");

        var modifierPanel = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
        var modifierBoxes = Modifiers.Select(modifier =>
        {
            var checkBox = new CheckBox
            {
                Content = modifier.Text,
                IsChecked = current.Combination?.Modifiers.Contains(modifier.Code) == true,
                MinWidth = 60,
            };
            AutomationProperties.SetAutomationId(checkBox, $"HotkeyEditor{modifier.Text}");
            AutomationProperties.SetName(checkBox, $"{modifier.Text} modifier");
            modifierPanel.Children.Add(checkBox);
            return checkBox;
        }).ToArray();

        var preview = new TextBlock
        {
            Text = Format(current),
            FontSize = 20,
            TextWrapping = TextWrapping.Wrap,
        };
        AutomationProperties.SetAutomationId(preview, "HotkeyEditorPreview");

        var content = new StackPanel { Spacing = 16 };
        content.Children.Add(new TextBlock
        {
            Text = "Choose a key and optional modifiers for this shortcut.",
            TextWrapping = TextWrapping.Wrap,
        });
        content.Children.Add(keySelector);
        content.Children.Add(new TextBlock { Text = "Modifiers" });
        content.Children.Add(modifierPanel);
        content.Children.Add(new TextBlock { Text = "Shortcut" });
        content.Children.Add(preview);

        var dialog = new ContentDialog
        {
            XamlRoot = root,
            RequestedTheme = (root.Content as FrameworkElement)?.ActualTheme ?? ElementTheme.Default,
            Title = "Edit hotkey",
            Content = content,
            PrimaryButtonText = "Save",
            SecondaryButtonText = "Clear",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
            IsPrimaryButtonEnabled = keySelector.SelectedItem is KeyOption,
        };
        AutomationProperties.SetAutomationId(dialog, "HotkeyEditorDialog");

        HotkeyBinding? ReadSelection()
        {
            if (keySelector.SelectedItem is not KeyOption selectedKey)
            {
                return null;
            }

            return new HotkeyBinding
            {
                Action = current.Action,
                Combination = new HotkeyCombination
                {
                    Key = selectedKey.Code,
                    Modifiers = new(Modifiers.Where((_, index) => modifierBoxes[index].IsChecked == true)
                        .Select(modifier => modifier.Code)),
                },
            };
        }

        void UpdatePreview()
        {
            var selection = ReadSelection();
            dialog.IsPrimaryButtonEnabled = selection is not null;
            preview.Text = Format(selection);
        }

        keySelector.SelectionChanged += (_, _) => UpdatePreview();
        foreach (var checkBox in modifierBoxes)
        {
            checkBox.Checked += (_, _) => UpdatePreview();
            checkBox.Unchecked += (_, _) => UpdatePreview();
        }

        return await dialog.ShowAsync() switch
        {
            ContentDialogResult.Primary => ReadSelection(),
            ContentDialogResult.Secondary => new HotkeyBinding { Action = current.Action },
            _ => null,
        };
    }
}
