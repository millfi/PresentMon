// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "ConfigurationJson.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

namespace pmon::ui::core
{
    class ConfigurationStore
    {
    public:
        ConfigurationStore(std::filesystem::path dataDirectory, std::filesystem::path presetDirectory);

        const std::filesystem::path& DataDirectory() const noexcept;
        const std::filesystem::path& PresetDirectory() const noexcept;
        std::filesystem::path PreferencesPath() const;
        std::filesystem::path CustomPath() const;
        const std::optional<std::string>& LastWarning() const noexcept;

        PreferenceFile LoadPreferences(const IntrospectionData& intro);
        void SavePreferences(const PreferenceFile& file);
        LoadoutFile LoadPreset(int slot, const IntrospectionData& intro, const Preferences& preferences);
        LoadoutFile LoadCustom(const IntrospectionData& intro, const Preferences& preferences);
        void SaveCustom(const LoadoutFile& file);
        LoadoutFile LoadLoadout(const std::filesystem::path& path, const IntrospectionData& intro,
            const Preferences& preferences);
        void SaveLoadout(const std::filesystem::path& path, const LoadoutFile& file);

        static std::string DefaultDataDirectory();

    private:
        void AtomicWrite(const std::filesystem::path& path, const std::string& content);

        std::filesystem::path dataDirectory_;
        std::filesystem::path presetDirectory_;
        std::optional<std::string> lastWarning_;
        std::unordered_set<std::filesystem::path> failedLoads_;
        std::mutex saveMutex_;
    };
}
