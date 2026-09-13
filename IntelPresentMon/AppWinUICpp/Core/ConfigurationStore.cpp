// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "ConfigurationStore.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace pmon::ui::core
{
    namespace
    {
        std::atomic_uint64_t temporaryNumber{ 0 };

        std::string ToUtf8(std::wstring_view value)
        {
            if (value.empty()) {
                return {};
            }
            const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
            std::string output((size_t)size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), output.data(), size, nullptr, nullptr);
            return output;
        }

        std::filesystem::path WithFilenameSuffix(const std::filesystem::path& path, const std::wstring& suffix)
        {
            return path.parent_path() / (path.filename().wstring() + suffix);
        }

        std::filesystem::path FullPath(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto full = std::filesystem::absolute(path, error);
            if (error) throw std::filesystem::filesystem_error("Unable to resolve configuration path", path, error);
            return full;
        }

        std::filesystem::path TemporaryPath(const std::filesystem::path& path)
        {
            const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            return WithFilenameSuffix(path, L"." + std::to_wstring(stamp) + L"."
                + std::to_wstring(++temporaryNumber) + L".tmp");
        }

        std::filesystem::path RecoveryPath(const std::filesystem::path& path)
        {
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            return WithFilenameSuffix(path, L".recovery-" + std::to_wstring(stamp) + L"-"
                + std::to_wstring(++temporaryNumber) + L".json");
        }
    }

    ConfigurationStore::ConfigurationStore(std::filesystem::path dataDirectory, std::filesystem::path presetDirectory)
        : dataDirectory_{ FullPath(dataDirectory) }
        , presetDirectory_{ FullPath(presetDirectory) }
    {}

    const std::filesystem::path& ConfigurationStore::DataDirectory() const noexcept
    {
        return dataDirectory_;
    }

    const std::filesystem::path& ConfigurationStore::PresetDirectory() const noexcept
    {
        return presetDirectory_;
    }

    std::filesystem::path ConfigurationStore::PreferencesPath() const
    {
        return dataDirectory_ / "preferences.json";
    }

    std::filesystem::path ConfigurationStore::CustomPath() const
    {
        return dataDirectory_ / "Loadouts" / "custom-auto.json";
    }

    const std::optional<std::string>& ConfigurationStore::LastWarning() const noexcept
    {
        return lastWarning_;
    }

    PreferenceFile ConfigurationStore::LoadPreferences(const IntrospectionData& intro)
    {
        lastWarning_.reset();
        const auto path = PreferencesPath();
        if (!std::filesystem::exists(path)) return PreferenceFile{ FileSignature{ "p2c-cap-pref", PreferenceDocument::CurrentVersion }, Preferences::CreateDefault(intro) };
        try {
            std::ifstream input{ path, std::ios::binary };
            if (!input) throw std::ios_base::failure("Unable to read preferences.");
            return PreferenceDocument::Parse({ std::istreambuf_iterator<char>{ input }, {} }, intro);
        }
        catch (const std::exception& error) {
            failedLoads_.insert(path);
            lastWarning_ = std::string{ "Preferences could not be loaded: " } + error.what()
                + " The original file will be retained as a recovery copy when settings are saved.";
            return PreferenceFile{ FileSignature{ "p2c-cap-pref", PreferenceDocument::CurrentVersion }, Preferences::CreateDefault(intro) };
        }
    }

    void ConfigurationStore::SavePreferences(const PreferenceFile& file)
    {
        AtomicWrite(PreferencesPath(), PreferenceDocument::Serialize(file));
    }

    LoadoutFile ConfigurationStore::LoadPreset(int slot, const IntrospectionData& intro, const Preferences& preferences)
    {
        if (slot < 0 || slot > 3) throw std::out_of_range("Preset slot must be from 0 to 3.");
        return LoadLoadout(presetDirectory_ / ("preset-" + std::to_string(slot) + ".json"), intro, preferences);
    }

    LoadoutFile ConfigurationStore::LoadCustom(const IntrospectionData& intro, const Preferences& preferences)
    {
        lastWarning_.reset();
        const auto path = CustomPath();
        if (!std::filesystem::exists(path)) return {};
        try {
            return LoadLoadout(path, intro, preferences);
        }
        catch (const std::exception& error) {
            failedLoads_.insert(path);
            lastWarning_ = std::string{ "The custom loadout could not be loaded: " } + error.what()
                + " The original file will be retained as a recovery copy when the loadout is saved.";
            return {};
        }
    }

    void ConfigurationStore::SaveCustom(const LoadoutFile& file)
    {
        SaveLoadout(CustomPath(), file);
    }

    LoadoutFile ConfigurationStore::LoadLoadout(const std::filesystem::path& path, const IntrospectionData& intro,
        const Preferences& preferences)
    {
        const auto fullPath = FullPath(path);
        try {
            std::ifstream input{ fullPath, std::ios::binary };
            if (!input) throw std::ios_base::failure("Unable to read loadout.");
            return LoadoutDocument::Parse({ std::istreambuf_iterator<char>{ input }, {} }, intro, preferences);
        }
        catch (const std::exception&) {
            if (std::filesystem::exists(fullPath)) failedLoads_.insert(fullPath);
            throw;
        }
    }

    void ConfigurationStore::SaveLoadout(const std::filesystem::path& path, const LoadoutFile& file)
    {
        AtomicWrite(FullPath(path), LoadoutDocument::Serialize(file));
    }

    std::string ConfigurationStore::DefaultDataDirectory()
    {
        if (const auto* profile = _wgetenv(L"USERPROFILE")) {
            return ToUtf8((std::filesystem::path{ profile } / L"Documents" / L"PresentMon").wstring());
        }
        return ToUtf8((std::filesystem::temp_directory_path() / L"PresentMon").wstring());
    }

    void ConfigurationStore::AtomicWrite(const std::filesystem::path& path, const std::string& content)
    {
        std::lock_guard lock{ saveMutex_ };
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = TemporaryPath(path);
        try {
            {
                std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
                if (!output) throw std::ios_base::failure("Unable to create temporary configuration file.");
                output.write(content.data(), (std::streamsize)content.size());
                output.flush();
                if (!output) throw std::ios_base::failure("Unable to write temporary configuration file.");
            }
            if (std::filesystem::exists(path)) {
                if (failedLoads_.contains(path)) {
                    std::filesystem::copy_file(path, RecoveryPath(path), std::filesystem::copy_options::none);
                    failedLoads_.erase(path);
                }
                const auto backup = WithFilenameSuffix(path, L".bak");
                if (!ReplaceFileW(path.c_str(), temporary.c_str(), backup.c_str(), REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
                    throw std::system_error((int)GetLastError(), std::system_category(), "Unable to replace configuration file");
                }
            }
            else {
                std::filesystem::rename(temporary, path);
            }
        }
        catch (...) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
            throw;
        }
    }
}
