// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "AutomaticTargeting.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace pmon::ui::core
{
    void AutomaticTargeting::Update(const Sample& sample, const ProbeGpuBusy& probeGpuBusy,
        const IsCurrent& isCurrent, const Select& select)
    {
        const auto samples = sample();
        if (!isCurrent()) {
            return;
        }

        std::vector<GpuProcessSample> active;
        std::vector<int> candidatePids;
        std::unordered_set<int> seen;
        for (const auto& entry : samples) {
            if (std::isfinite(entry.RunningTime) && entry.RunningTime > 0.) {
                active.push_back(entry);
                if (seen.insert(entry.Pid).second) {
                    candidatePids.push_back(entry.Pid);
                }
            }
        }

        const auto measurableList = probeGpuBusy(candidatePids);
        if (!isCurrent()) {
            return;
        }
        const std::unordered_set<int> measurable{ measurableList.begin(), measurableList.end() };
        const auto top = std::min_element(active.begin(), active.end(), [&measurable](const auto& left, const auto& right) {
            const auto leftEligible = measurable.contains(left.Pid);
            const auto rightEligible = measurable.contains(right.Pid);
            if (leftEligible != rightEligible) {
                return leftEligible;
            }
            if (!leftEligible) {
                return false;
            }
            if (left.RunningTime != right.RunningTime) {
                return left.RunningTime > right.RunningTime;
            }
            return left.Pid < right.Pid;
        });
        if (top != active.end() && measurable.contains(top->Pid)) {
            select(top->Pid);
        }
    }
}
