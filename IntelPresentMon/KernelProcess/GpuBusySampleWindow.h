// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cmath>
#include <cstddef>

namespace kproc
{
    class GpuBusySampleWindow
    {
    public:
        static constexpr size_t SampleCount = 10;

        void Add(double rawGpuBusy)
        {
            samples_[next_] = rawGpuBusy;
            next_ = (next_ + 1) % SampleCount;
            if (count_ < SampleCount) ++count_;
        }

        bool HasVariation() const
        {
            if (count_ != SampleCount) return false;
            bool changed = false;
            for (const auto value : samples_) {
                if (!std::isfinite(value)) return false;
                if (value != samples_[0]) changed = true;
            }
            return changed;
        }

        void Reset()
        {
            count_ = 0;
            next_ = 0;
        }

    private:
        std::array<double, SampleCount> samples_{};
        size_t count_ = 0;
        size_t next_ = 0;
    };
}
