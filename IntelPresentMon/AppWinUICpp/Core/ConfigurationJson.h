// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "ConfigModels.h"

#include <string>
#include <vector>

namespace pmon::ui::core
{
    class ConfigurationJson
    {
    public:
        static Preferences Clone(const Preferences& value);
        static LoadoutFile Clone(const LoadoutFile& value);
    };

    class PreferenceDocument
    {
    public:
        static constexpr const char* CurrentVersion = "1.1.0";

        static PreferenceFile Parse(const std::string& text, const IntrospectionData& intro);
        static std::string Serialize(const PreferenceFile& file);
    };

    class LoadoutDocument
    {
    public:
        static constexpr const char* CurrentVersion = "1.0.0";

        static LoadoutFile Parse(const std::string& text, const IntrospectionData& intro, const Preferences& preferences);
        static LoadoutFile FromWidgets(const std::vector<std::shared_ptr<Widget>>& widgets);
        static std::string Serialize(const LoadoutFile& file);
    };
}
