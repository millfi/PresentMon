// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <source_location>
#include <string>

namespace pmon::ui::diagnostics
{
    void Initialize(const std::string& directory) noexcept;
    void Record(const char* event, nlohmann::json fields = nlohmann::json::object(),
        std::source_location source = std::source_location::current()) noexcept;
    void Exception(const char* operation, int32_t code, const std::string& message,
        std::source_location source = std::source_location::current()) noexcept;
    std::string LogPath();
}
