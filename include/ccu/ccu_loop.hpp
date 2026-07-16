/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_LOOP_HPP
#define CCU_LOOP_HPP

#include <vector>

#include "ccu_types.h"
#include "ccu_variable.hpp"
#include "ccu_func.hpp"
#include "ccu_primitives_impl.h"
#include "ccu_utils.hpp"

namespace AscendC {
namespace ccu {

class Loop {
public:
    Loop(Variable &loopCfg, const Func &func)
    {
        ComposeLoopBody(func);
        isVarBased_ = true;
        loopParamVar_ = &loopCfg;
    }

    Loop(const CcuLoopConfig &loopCfg, const Func &func)
    {
        ComposeLoopBody(func);
        isVarBased_ = false;
        config_ = loopCfg;
    }

    CcuLoop Handle() const
    {
        return handle_;
    }

    bool IsVarBased() const
    {
        return isVarBased_;
    }

    Variable *LoopParamVar() const
    {
        return loopParamVar_;
    }

    const CcuLoopConfig *Config() const
    {
        return &config_;
    }

private:
    void ComposeLoopBody(const Func &func)
    {
        if (func.NumIn() != 0) {
            throw ::AscendC::ccu::detail::CcuException(CcuResult::CCU_E_PARA,
                "ccu::Loop requires a no-argument ccu::Func");
        }
        CCU_THROW_IF_FAILED(::CcuLoopCreate(&handle_), "CcuLoopCreate failed");
        CCU_THROW_IF_FAILED(::_CcuLoopBodyEnter(handle_), "_CcuLoopBodyEnter failed");
        try {
            func.RunBody(nullptr);
        } catch (...) {
            (void)::_CcuLoopBodyExit(handle_);
            throw;
        }
        CCU_THROW_IF_FAILED(::_CcuLoopBodyExit(handle_), "_CcuLoopBodyExit failed");
    }

    CcuLoop handle_{0};
    bool isVarBased_{false};
    Variable *loopParamVar_{nullptr};
    CcuLoopConfig config_{};
};

class LoopGroup {
public:

    LoopGroup(Variable &parallelCfg, Variable &offsetCfg, uint32_t maxLoopNum,
              const std::vector<Loop> &loops)
    {
        CCU_THROW_IF_FAILED(
            ::CcuLoopGroupCreateFromVar(&handle_, maxLoopNum,
                                        parallelCfg.handle, offsetCfg.handle),
            "CcuLoopGroupCreateFromVar failed");
        AddLoops(loops);
    }

    LoopGroup(const CcuLoopGroupConfig &loopGroupCfg, uint32_t maxLoopNum,
              const std::vector<Loop> &loops)
    {
        CcuLoopGroupConfig localCfg = loopGroupCfg;
        CCU_THROW_IF_FAILED(
            ::CcuLoopGroupCreate(&handle_, maxLoopNum, &localCfg),
            "CcuLoopGroupCreate failed");
        AddLoops(loops);
    }

    CcuLoopGroup Handle() const
    {
        return handle_;
    }

private:
    void AddLoops(const std::vector<Loop> &loops)
    {
        for (const auto &loop : loops) {
            if (loop.IsVarBased()) {
                auto *loopParamVar = loop.LoopParamVar();
                if (loopParamVar == nullptr) {
                    throw ::AscendC::ccu::detail::CcuException(CcuResult::CCU_E_PARA,
                        "ccu::Loop var-based loop has null loop parameter");
                }
                CCU_THROW_IF_FAILED(
                    ::CcuLoopGroupAddLoopFromVar(handle_, loop.Handle(), loopParamVar->handle),
                    "CcuLoopGroupAddLoopFromVar failed");
            } else {
                CCU_THROW_IF_FAILED(
                    ::CcuLoopGroupAddLoop(handle_, loop.Handle(), loop.Config()),
                    "CcuLoopGroupAddLoop failed");
            }
        }
    }

    CcuLoopGroup handle_{0};
};

} // namespace ccu
} // namespace AscendC

#endif // CCU_LOOP_HPP
