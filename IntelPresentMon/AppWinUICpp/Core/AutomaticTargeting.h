// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <optional>
#include <vector>

namespace pmon::ui::core
{
    struct GpuProcessSample
    {
        int Pid;
        double RunningTime;
    };

    class AutomaticTargeting
    {
    public:
        using Sample = std::function<std::vector<GpuProcessSample>()>;
        using ProbeGpuBusy = std::function<std::vector<int>(const std::vector<int>&)>;
        using IsCurrent = std::function<bool()>;
        using Select = std::function<void(std::optional<int>)>;

        static void Update(const Sample& sample, const ProbeGpuBusy& probeGpuBusy,
            const IsCurrent& isCurrent, const Select& select);
    };
}
