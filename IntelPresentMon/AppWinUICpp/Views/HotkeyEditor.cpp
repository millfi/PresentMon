// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT

#include "../pch.h"
#include "HotkeyEditor.h"

#include "FormControls.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Foundation;

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

    struct KeyOption
    {
        int Code;
        std::string Text;
    };

    struct ModifierOption
    {
        int Code;
        std::string Text;
    };

    std::vector<KeyOption> BuildKeys()
    {
        std::vector<KeyOption> keys{
            { 0, "Backspace" }, { 1, "Tab" }, { 2, "Enter" }, { 6, "Pause" }, { 7, "Caps Lock" },
            { 8, "Escape" }, { 9, "Space" }, { 10, "Page Up" }, { 11, "Page Down" }, { 12, "End" },
            { 13, "Home" }, { 14, "Left" }, { 15, "Up" }, { 16, "Right" }, { 17, "Down" },
            { 18, "Print" }, { 19, "Print Screen" }, { 20, "Insert" }, { 21, "Delete" },
        };
        for (int index = 0; index < 10; ++index)
        {
            keys.push_back({ 22 + index, std::to_string(index) });
        }
        for (int index = 0; index < 26; ++index)
        {
            keys.push_back({ 32 + index, std::string(1, (char)('A' + index)) });
        }
        for (int index = 0; index < 10; ++index)
        {
            keys.push_back({ 60 + index, "Num " + std::to_string(index) });
        }
        keys.insert(keys.end(), {
            { 70, "Num Multiply" }, { 71, "Num Add" }, { 72, "Num Subtract" }, { 73, "Num Decimal" },
            { 74, "Num Divide" },
        });
        for (int index = 0; index < 24; ++index)
        {
            keys.push_back({ 75 + index, "F" + std::to_string(index + 1) });
        }
        keys.insert(keys.end(), { { 99, "Num Lock" }, { 100, "Scroll Lock" } });
        return keys;
    }

    const std::vector<KeyOption>& Keys()
    {
        static const auto keys = BuildKeys();
        return keys;
    }

    const std::vector<ModifierOption>& Modifiers()
    {
        static const std::vector<ModifierOption> modifiers{
            { 2, "Ctrl" }, { 1, "Alt" }, { 4, "Shift" }, { 8, "Win" },
        };
        return modifiers;
    }

    const KeyOption* FindKey(int code)
    {
        const auto& keys = Keys();
        const auto found = std::find_if(keys.begin(), keys.end(), [code](const KeyOption& key) { return key.Code == code; });
        return found == keys.end() ? nullptr : &*found;
    }

    bool HasModifier(const pmon::ui::core::HotkeyCombination& combination, int code)
    {
        return std::find(combination.Modifiers.begin(), combination.Modifiers.end(), code) != combination.Modifiers.end();
    }
}

namespace pmon::ui::views::HotkeyEditor
{
    std::string Format(const std::optional<core::HotkeyBinding>& binding)
    {
        if (!binding || !binding->Combination) return "Not assigned";
        const auto& combination = *binding->Combination;
        std::string text;
        for (const auto& modifier : Modifiers())
        {
            if (!HasModifier(combination, modifier.Code)) continue;
            if (!text.empty()) text += " + ";
            text += modifier.Text;
        }
        if (!text.empty()) text += " + ";
        if (const auto* key = FindKey(combination.Key)) text += key->Text;
        else text += "Key " + std::to_string(combination.Key);
        return text;
    }

    IAsyncAction EditAsync(const XamlRoot& root, const core::HotkeyBinding& current, EditedCallback edited)
    {
        if (!root) throw hresult_invalid_argument(H("root"));
        auto keySelector = ComboBox{};
        keySelector.Header(box_value(H("Key")));
        keySelector.PlaceholderText(H("Choose a key"));
        keySelector.HorizontalAlignment(HorizontalAlignment::Stretch);
        keySelector.IsTextSearchEnabled(true);
        int selectedIndex = -1;
        const auto& keys = Keys();
        for (int index = 0; index < (int)keys.size(); ++index)
        {
            keySelector.Items().Append(box_value(H(keys[(size_t)index].Text)));
            if (current.Combination && current.Combination->Key == keys[(size_t)index].Code) selectedIndex = index;
        }
        keySelector.SelectedIndex(selectedIndex);
        FormControls::Identify(keySelector, "HotkeyEditorKey", "Hotkey key");

        auto modifierPanel = StackPanel{};
        modifierPanel.Orientation(Orientation::Horizontal);
        modifierPanel.Spacing(12.0);
        std::vector<CheckBox> modifierBoxes;
        modifierBoxes.reserve(Modifiers().size());
        for (const auto& modifier : Modifiers())
        {
            auto checkBox = CheckBox{};
            checkBox.Content(box_value(H(modifier.Text)));
            checkBox.IsChecked(current.Combination && HasModifier(*current.Combination, modifier.Code));
            checkBox.MinWidth(60.0);
            FormControls::Identify(checkBox, std::string("HotkeyEditor") + modifier.Text, std::string(modifier.Text) + " modifier");
            modifierPanel.Children().Append(checkBox);
            modifierBoxes.push_back(checkBox);
        }

        auto preview = TextBlock{};
        preview.Text(H(Format(current)));
        preview.FontSize(20.0);
        preview.TextWrapping(TextWrapping::Wrap);
        FormControls::Identify(preview, "HotkeyEditorPreview", "Shortcut preview");

        auto content = StackPanel{};
        content.Spacing(16.0);
        auto instruction = TextBlock{};
        instruction.Text(H("Choose a key and optional modifiers for this shortcut."));
        instruction.TextWrapping(TextWrapping::Wrap);
        content.Children().Append(instruction);
        content.Children().Append(keySelector);
        auto modifierLabel = TextBlock{};
        modifierLabel.Text(H("Modifiers"));
        content.Children().Append(modifierLabel);
        content.Children().Append(modifierPanel);
        auto shortcutLabel = TextBlock{};
        shortcutLabel.Text(H("Shortcut"));
        content.Children().Append(shortcutLabel);
        content.Children().Append(preview);

        auto dialog = ContentDialog{};
        dialog.XamlRoot(root);
        if (auto element = root.Content().try_as<FrameworkElement>()) dialog.RequestedTheme(element.ActualTheme());
        dialog.Title(box_value(H("Edit hotkey")));
        dialog.Content(content);
        dialog.PrimaryButtonText(H("Save"));
        dialog.SecondaryButtonText(H("Clear"));
        dialog.CloseButtonText(H("Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(selectedIndex >= 0);
        FormControls::Identify(dialog, "HotkeyEditorDialog", "Edit hotkey");

        auto keySelectorWeak = make_weak(keySelector);
        std::vector<weak_ref<CheckBox>> modifierBoxesWeak;
        modifierBoxesWeak.reserve(modifierBoxes.size());
        for (const auto& checkBox : modifierBoxes) modifierBoxesWeak.push_back(make_weak(checkBox));
        auto readSelection = [keySelectorWeak, modifierBoxesWeak, current]() -> std::optional<core::HotkeyBinding>
        {
            auto keySelector = keySelectorWeak.get();
            if (!keySelector) return std::nullopt;
            auto const index = keySelector.SelectedIndex();
            if (index < 0 || index >= (int)Keys().size()) return std::nullopt;
            core::HotkeyBinding result;
            result.Action = current.Action;
            core::HotkeyCombination combination;
            combination.Key = Keys()[(size_t)index].Code;
            for (int modifierIndex = 0; modifierIndex < (int)modifierBoxesWeak.size(); ++modifierIndex)
            {
                auto checkBox = modifierBoxesWeak[(size_t)modifierIndex].get();
                if (!checkBox) continue;
                auto const isChecked = checkBox.IsChecked();
                if (isChecked && isChecked.Value())
                {
                    combination.Modifiers.push_back(Modifiers()[(size_t)modifierIndex].Code);
                }
            }
            result.Combination = std::move(combination);
            return result;
        };
        auto dialogWeak = make_weak(dialog);
        auto previewWeak = make_weak(preview);
        auto updatePreview = [readSelection, dialogWeak, previewWeak]
        {
            auto const selection = readSelection();
            if (auto dialog = dialogWeak.get()) dialog.IsPrimaryButtonEnabled(selection.has_value());
            if (auto preview = previewWeak.get()) preview.Text(H(Format(selection)));
        };
        keySelector.SelectionChanged([updatePreview](const IInspectable&, const SelectionChangedEventArgs&)
        {
            updatePreview();
        });
        for (const auto& checkBox : modifierBoxes)
        {
            checkBox.Checked([updatePreview](const IInspectable&, const RoutedEventArgs&) { updatePreview(); });
            checkBox.Unchecked([updatePreview](const IInspectable&, const RoutedEventArgs&) { updatePreview(); });
        }

        auto const result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::Primary) co_await edited(readSelection());
        else if (result == ContentDialogResult::Secondary)
        {
            core::HotkeyBinding cleared;
            cleared.Action = current.Action;
            co_await edited(std::move(cleared));
        }
        else co_await edited(std::nullopt);
    }
}
