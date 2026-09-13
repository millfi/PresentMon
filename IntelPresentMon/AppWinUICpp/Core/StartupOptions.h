// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace pmon::ui::services
{
    struct StartupOptions
    {
        static constexpr const char* DefaultMutexSuffix = "UiBrowserProcess";

        std::string PipeName;
        bool FilesWorking = false;
        std::string MutexSuffix = DefaultMutexSuffix;
        std::string DataDirectory;
        std::string AppDataDirectory;
        std::string InstallDirectory;
        std::string LogDirectory;
        bool EnableDevOptions = false;

        static StartupOptions Parse(const std::vector<std::string>& args);
    };
}
