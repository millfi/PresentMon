// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "ConfigModels.h"

#include <memory>
#include <optional>
#include <vector>

namespace pmon::ui::core
{
    struct Specification
    {
        std::optional<int> Pid;
        Preferences Preferences;
        std::vector<std::shared_ptr<Widget>> Widgets;
    };

    class SpecificationBuilder
    {
    public:
        static Specification Build(std::optional<int> pid, const Preferences& preferences,
            const std::vector<std::shared_ptr<Widget>>& widgets, const IntrospectionData& intro);
    };

    class ConfigurationValidation
    {
    public:
        static void ValidatePreferences(const Preferences& preferences);
        static void ValidateWidgets(const std::vector<std::shared_ptr<Widget>>& widgets);
    };
}
