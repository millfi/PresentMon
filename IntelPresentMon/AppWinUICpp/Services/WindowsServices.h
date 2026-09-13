// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "../Core/StartupOptions.h"

#include <cstdint>
#include <stop_token>
#include <string>
#include <unordered_set>
#include <vector>

namespace pmon::ui::services {

struct ProcessEntry {
    int Pid = 0;
    std::string Name;
    std::string WindowName;

    std::string DisplayName() const;
};

struct GpuProcessSample {
    int Pid = 0;
    double RunningTime = 0.0;
};

class WindowsServices {
public:
    static std::vector<ProcessEntry> EnumerateProcesses(
        std::unordered_set<std::string> const* blocklist = nullptr,
        std::stop_token cancellation = {});
    static std::vector<GpuProcessSample> GetGpuProcessSamples(
        std::unordered_set<std::string> const* blocklist = nullptr,
        std::stop_token cancellation = {});

    static void SetWindowIdentity(void* hwnd, std::string const& mutexSuffix);
    static void ClearWindowIdentity(void* hwnd) noexcept;
};

}
