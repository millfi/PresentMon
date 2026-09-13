// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <winrt/Windows.Foundation.h>

namespace pmon::ui::tests
{
    struct BackdropSmokeReport
    {
        std::string RunId;
        std::vector<std::string> Steps;
        std::string Failure;
    };

    winrt::Windows::Foundation::IAsyncAction RunBackdropTestsAsync(std::shared_ptr<BackdropSmokeReport> report);
}
