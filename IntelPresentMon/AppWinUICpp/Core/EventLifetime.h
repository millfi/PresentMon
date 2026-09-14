// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <utility>

namespace pmon::ui::core
{
    // UI-thread callbacks may outlive their native owner through a XAML control.
    class EventLifetime
    {
    public:
        EventLifetime() = default;
        EventLifetime(const EventLifetime&) = delete;
        EventLifetime& operator=(const EventLifetime&) = delete;
        ~EventLifetime() { Invalidate(); }
        void Invalidate() noexcept { *alive_ = false; }

        template<class F> auto Guard(F callback) const
        {
            return [alive = alive_, callback = std::move(callback)](auto&&... args) {
                if (*alive) callback(std::forward<decltype(args)>(args)...);
            };
        }
    private:
        std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    };
}
