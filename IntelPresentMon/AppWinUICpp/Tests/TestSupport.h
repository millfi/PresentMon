// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace pmon::ui::tests
{
    inline void Expect(bool condition, const std::string& message)
    {
        if (!condition) {
            throw std::runtime_error{ message };
        }
    }

    template<class Exception, class Function>
    void ExpectThrows(Function&& function, const std::string& message)
    {
        try {
            std::forward<Function>(function)();
        }
        catch (const Exception&) {
            return;
        }
        throw std::runtime_error{ message };
    }

    inline int RunTest(const std::function<void()>& test)
    {
        test();
        return 1;
    }
}
