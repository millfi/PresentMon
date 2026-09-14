// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "UiDiagnostics.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdio>

namespace pmon::ui::diagnostics
{
    namespace
    {
        struct State
        {
            std::mutex Mutex;
            std::filesystem::path Path;
            uint64_t Sequence = 0;
            uint64_t Bytes = 0;
        };

        State& GetState()
        {
            static State state;
            return state;
        }

        std::string Utf8(const std::filesystem::path& path)
        {
            auto text = path.u8string();
            return std::string(reinterpret_cast<const char*>(text.data()), text.size());
        }

        std::filesystem::path ModulePath(HMODULE module)
        {
            std::wstring path(32768, L'\0');
            auto size = GetModuleFileNameW(module, path.data(), (DWORD)path.size());
            if (!size || size == path.size()) return {};
            path.resize(size);
            return path;
        }
    }

    void Initialize(const std::string& directory) noexcept
    {
        try {
            if (directory.empty()) return;
            auto& state = GetState();
            {
                std::scoped_lock lock(state.Mutex);
                auto folder = std::filesystem::u8path(directory);
                if (!state.Path.empty() && state.Path.parent_path() == folder) return;
                std::filesystem::create_directories(folder);
                state.Path = folder / ("PresentMonUI-actions-" + std::to_string(GetCurrentProcessId())
                    + "-" + std::to_string(GetTickCount64()) + ".jsonl");
                state.Sequence = 0;
                state.Bytes = 0;
            }
            Record("session.start", {{"executable", Utf8(ModulePath(nullptr))},
                {"build", __DATE__ " " __TIME__},
                {"xamlModule", Utf8(ModulePath(GetModuleHandleW(L"Microsoft.UI.Xaml.dll")))}});
        }
        catch (...) { OutputDebugStringA("Unable to initialize UI diagnostics.\n"); }
    }

    void Record(const char* event, nlohmann::json fields, std::source_location source) noexcept
    {
        try {
            auto& state = GetState();
            std::scoped_lock lock(state.Mutex);
            if (state.Path.empty()) return;
            SYSTEMTIME now{};
            GetSystemTime(&now);
            char timestamp[32]{};
            sprintf_s(timestamp, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", now.wYear, now.wMonth,
                now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
            nlohmann::json entry{{"utc", timestamp}, {"sequence", ++state.Sequence},
                {"pid", GetCurrentProcessId()}, {"tid", GetCurrentThreadId()}, {"event", event},
                {"source", source.file_name()}, {"line", source.line()}, {"function", source.function_name()},
                {"details", std::move(fields)}};
            auto text = entry.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + '\n';
            // Each process has its own file and one previous 2 MiB segment.
            if (state.Bytes + text.size() > 2 * 1024 * 1024) {
                auto previous = state.Path;
                previous += ".previous";
                std::error_code error;
                std::filesystem::remove(previous, error);
                std::filesystem::rename(state.Path, previous, error);
                if (error) return;
                state.Bytes = 0;
            }
            std::ofstream output(state.Path, std::ios::binary | std::ios::app);
            output << text;
            output.flush();
            if (output) state.Bytes += text.size();
        }
        catch (...) { OutputDebugStringA("Unable to write UI diagnostics.\n"); }
    }

    void Exception(const char* operation, int32_t code, const std::string& message, std::source_location source) noexcept
    {
        try {
            char hr[16]{};
            sprintf_s(hr, "0x%08X", (uint32_t)code);
            void* addresses[32]{};
            auto count = CaptureStackBackTrace(1, 32, addresses, nullptr);
            auto frames = nlohmann::json::array();
            for (USHORT index = 0; index < count; ++index) {
                HMODULE module{};
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(addresses[index]), &module);
                frames.push_back({{"module", module ? Utf8(ModulePath(module)) : std::string{}},
                    {"address", (uint64_t)reinterpret_cast<uintptr_t>(addresses[index])},
                    {"rva", module ? (uint64_t)(reinterpret_cast<uintptr_t>(addresses[index])
                        - reinterpret_cast<uintptr_t>(module)) : 0}});
            }
            // This is the reporting handler's stack, not the original throw stack.
            Record("exception", {{"operation", operation}, {"hresult", hr}, {"message", message},
                {"handlerStack", std::move(frames)}}, source);
        }
        catch (...) { OutputDebugStringA("Unable to record UI exception.\n"); }
    }

    std::string LogPath()
    {
        auto& state = GetState();
        std::scoped_lock lock(state.Mutex);
        return Utf8(state.Path);
    }
}
