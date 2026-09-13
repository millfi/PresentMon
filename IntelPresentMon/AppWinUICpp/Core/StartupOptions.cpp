// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "StartupOptions.h"

#include <Windows.h>
#include <ShlObj.h>

#include <filesystem>
#include <cctype>
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace pmon::ui::services
{
    namespace
    {
        std::string ToUtf8(const std::wstring& value)
        {
            if (value.empty()) {
                return {};
            }
            const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
            std::string output((size_t)size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), output.data(), size, nullptr, nullptr);
            return output;
        }

        std::filesystem::path ExecutableDirectory()
        {
            std::wstring path(MAX_PATH, L'\0');
            const auto length = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
            if (length == 0 || length == path.size()) {
                throw std::runtime_error("Unable to determine executable directory.");
            }
            path.resize(length);
            return std::filesystem::path{ path }.parent_path();
        }

        std::filesystem::path KnownFolder(REFKNOWNFOLDERID id)
        {
            PWSTR path = nullptr;
            const auto result = SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &path);
            if (FAILED(result) || path == nullptr) {
                throw std::runtime_error("Windows did not provide the requested known folder.");
            }
            const std::filesystem::path output{ path };
            CoTaskMemFree(path);
            return output;
        }

        std::string Absolute(const std::string& value)
        {
            return ToUtf8(std::filesystem::absolute(std::filesystem::u8path(value)).lexically_normal().wstring());
        }

        bool StartsWithOption(const std::string& argument)
        {
            return argument.rfind("--", 0) == 0;
        }

        std::string Lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return (char)std::tolower(character);
            });
            return value;
        }
    }

    StartupOptions StartupOptions::Parse(const std::vector<std::string>& args)
    {
        StartupOptions options;
        std::string dataDirectory;
        std::string logDirectory;

        for (size_t index = 0; index < args.size(); ++index) {
            const auto& argument = args[index];
            const auto separator = argument.find('=');
            const auto name = argument.substr(0, separator);
            const auto readValue = [&]() {
                std::string value;
                if (separator != std::string::npos) {
                    value = argument.substr(separator + 1);
                }
                else if (index + 1 < args.size() && !StartsWithOption(args[index + 1])) {
                    value = args[++index];
                }
                if (value.empty()) {
                    throw std::invalid_argument("The " + name + " option requires a value.");
                }
                return value;
            };
            const auto readFlag = [&]() {
                if (separator == std::string::npos) {
                    return true;
                }
                const auto value = Lowercase(argument.substr(separator + 1));
                if (value == "true") {
                    return true;
                }
                if (value == "false") {
                    return false;
                }
                throw std::invalid_argument("The " + name + " option requires true or false.");
            };

            if (name == "--p2c-act-name") {
                options.PipeName = readValue();
            }
            else if (name == "--p2c-files-working") {
                options.FilesWorking = readFlag();
            }
            else if (name == "--p2c-ui-mutex-name") {
                options.MutexSuffix = readValue();
            }
            else if (name == "--p2c-log-folder") {
                logDirectory = readValue();
            }
            else if (name == "--p2c-enable-ui-dev-options") {
                options.EnableDevOptions = readFlag();
            }
            else if (name == "--data-directory") {
                dataDirectory = readValue();
            }
            else {
                throw std::invalid_argument("The " + name + " option is not supported by the WinUI interface.");
            }
        }

        if (!dataDirectory.empty()) {
            options.DataDirectory = Absolute(dataDirectory);
            options.AppDataDirectory = options.DataDirectory;
        }
        else if (options.FilesWorking) {
            options.DataDirectory = ToUtf8(std::filesystem::current_path().wstring());
            options.AppDataDirectory = options.DataDirectory;
        }
        else {
            options.DataDirectory = ToUtf8((KnownFolder(FOLDERID_Documents) / L"PresentMon").wstring());
            options.AppDataDirectory = ToUtf8((KnownFolder(FOLDERID_LocalAppData) / L"Intel" / L"PresentMon").wstring());
        }
        const auto executableDirectory = ExecutableDirectory();
        options.InstallDirectory = ToUtf8((Lowercase(ToUtf8(executableDirectory.filename().wstring())) == "ui"
            ? executableDirectory.parent_path() : executableDirectory).wstring());
        options.LogDirectory = logDirectory.empty()
            ? ToUtf8((std::filesystem::u8path(options.AppDataDirectory) / "logs").wstring()) : Absolute(logDirectory);
        return options;
    }
}
