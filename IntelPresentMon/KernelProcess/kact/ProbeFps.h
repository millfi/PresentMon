// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include "../../Interprocess/source/act/ActionHelper.h"
#include "KernelExecutionContext.h"
#include <Core/source/cli/CliOptions.h>
#include <cereal/types/vector.hpp>

#define ACT_NAME ProbeFps
#define ACT_EXEC_CTX KernelExecutionContext
#define ACT_TYPE AsyncActionBase_
#define ACT_NS kproc::kact

namespace ACT_NS
{
    using namespace pmon::ipc::act;

    class ACT_NAME : public ACT_TYPE<ACT_NAME, ACT_EXEC_CTX>
    {
    public:
        static constexpr const char* Identifier = STRINGIFY(ACT_NAME);
        struct Params
        {
            std::vector<uint32_t> pids;
            template<class A> void serialize(A& ar) { ar(pids); }
        };
        struct Response
        {
            std::vector<uint32_t> pids;
            template<class A> void serialize(A& ar) { ar(pids); }
        };
    private:
        friend class ACT_TYPE<ACT_NAME, ACT_EXEC_CTX>;
        static Response Execute_(const ACT_EXEC_CTX&, SessionContext& stx, Params&& in)
        {
            if (in.pids.empty()) {
                stx.pFpsProbe.reset();
                return {};
            }
            if (!stx.pFpsProbe) {
                const auto& opt = p2c::cli::Options::Get();
                auto session = opt.controlPipe || opt.svcAsChild
                    ? pmapi::Session{ *opt.controlPipe } : pmapi::Session{};
                stx.pFpsProbe = std::make_unique<FpsProbe>(std::move(session));
            }
            return { stx.pFpsProbe->Poll(in.pids) };
        }
    };
    ACTION_REG();
}

ACTION_TRAITS_DEF();
#undef ACT_NAME
#undef ACT_EXEC_CTX
#undef ACT_NS
#undef ACT_TYPE
