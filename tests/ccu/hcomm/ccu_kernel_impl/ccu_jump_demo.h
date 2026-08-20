/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/hcomm/ccu_primitives.hpp"
#include "ccu/hcomm/ccu_api_types.h"

namespace ccu = ::AscendC::ccu;

// ======================== CCU IF Demo ========================

struct CcuIfDemoKernelArg {
    uint32_t value;
    uint64_t expected_;
};

CcuResult CcuIfDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuIfDemoKernelArg*>(arg);

    ccu::variable var_;
    var_ = args_->value;

    CCU_IF(var_ == args_->expected_)
    {
        ccu::variable thenResult, thenAddend;
        thenAddend = 100;
        thenResult = var_ + thenAddend;
    }
    CCU_ELSE
    {
        ccu::variable elseResult, elseAddend;
        elseAddend = 200;
        elseResult = var_ + elseAddend;
    }

    return CcuResult::CCU_SUCCESS;
}
// ======================== CCU IF (without else) Demo ========================

struct CcuIfNoElseDemoKernelArg {
    uint32_t value;
    uint64_t threshold;
};

CcuResult CcuIfNoElseDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuIfNoElseDemoKernelArg*>(arg);

    ccu::variable var_;
    var_ = args_->value;

    ccu::variable result;
    result = 0;

    ccu::variable addend;
    addend = 100;

    CCU_IF(var_ == args_->threshold) { result = var_ + addend; }

    return CcuResult::CCU_SUCCESS;
}

// ======================== Nested IF: if{if{}}else{} ========================

struct CcuNestedIfOuterElseDemoKernelArg {
    uint32_t outerVal;
    uint64_t outerExpected;
    uint32_t innerVal;
    uint64_t innerExpected;
};

CcuResult CcuNestedIfOuterElseDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuNestedIfOuterElseDemoKernelArg*>(arg);

    ccu::variable outerVar;
    outerVar = args_->outerVal;

    ccu::variable innerVar;
    innerVar = args_->innerVal;

    ccu::variable result;
    result = 0;

    ccu::variable addend;

    CCU_IF(outerVar == args_->outerExpected)
    {
        CCU_IF(innerVar == args_->innerExpected)
        {
            addend = 10;
            result = result + addend;
        }
        addend = 20;
        result = result + addend;
    }
    CCU_ELSE
    {
        addend = 30;
        result = result + addend;
    }

    return CcuResult::CCU_SUCCESS;
}

// ======================== Nested IF: if{if{}else{}} ========================

struct CcuNestedIfInnerElseDemoKernelArg {
    uint32_t outerVal;
    uint64_t outerExpected;
    uint32_t innerVal;
    uint64_t innerExpected;
};

CcuResult CcuNestedIfInnerElseDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuNestedIfInnerElseDemoKernelArg*>(arg);

    ccu::variable outerVar;
    outerVar = args_->outerVal;

    ccu::variable innerVar;
    innerVar = args_->innerVal;

    ccu::variable result;
    result = 0;

    ccu::variable addend;

    CCU_IF(outerVar == args_->outerExpected)
    {
        CCU_IF(innerVar == args_->innerExpected)
        {
            addend = 10;
            result = result + addend;
        }
        CCU_ELSE
        {
            addend = 20;
            result = result + addend;
        }
        addend = 30;
        result = result + addend;
    }

    return CcuResult::CCU_SUCCESS;
}

// ======================== Nested IF: if{}if{} ========================

struct CcuNestedIfIfDemoKernelArg {
    uint32_t outerVal;
    uint64_t outerExpected;
    uint32_t innerVal;
    uint64_t innerExpected;
};

CcuResult CcuNestedIfIfDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuNestedIfIfDemoKernelArg*>(arg);

    ccu::variable outerVar;
    outerVar = args_->outerVal;

    ccu::variable innerVar;
    innerVar = args_->innerVal;

    ccu::variable result;
    result = 0;

    ccu::variable addend;

    CCU_IF(outerVar == args_->outerExpected)
    {
        addend = 10;
        result = result + addend;
    }
    CCU_IF(innerVar == args_->innerExpected)
    {
        addend = 20;
        result = result + addend;
    }

    return CcuResult::CCU_SUCCESS;
}

// ======================== CCU WHILE Demo ========================

struct CcuWhileDemoKernelArg {
    uint32_t loop_count;
};

CcuResult CcuWhileDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuWhileDemoKernelArg*>(arg);

    ccu::variable counter;
    counter = 0;

    ccu::variable limit;
    limit = args_->loop_count;

    ccu::variable one;
    one = 1;

    ccu::variable accumulator;
    accumulator = 0;

    ccu::variable step;
    step = 10;

    CCU_WHILE(counter != args_->loop_count)
    {
        accumulator = accumulator + step;
        counter = counter + one;
    }

    return CcuResult::CCU_SUCCESS;
}

// ======================== CCU DO_WHILE_WHILE Demo (do{while{}}while) ========================

struct CcuDoWhileWhileDemoKernelArg {
    uint32_t loop_count;
};

CcuResult CcuDoWhileWhileDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuDoWhileWhileDemoKernelArg*>(arg);

    ccu::variable counter_1;
    counter_1 = 0;
    ccu::variable counter_2;
    counter_2 = 0;

    ccu::variable one;
    one = 1;

    ccu::variable accumulator;
    accumulator = 0;

    ccu::variable step;
    step = 10;

    CCU_DO
    {
        CCU_WHILE(counter_2 != args_->loop_count)
        {
            accumulator = accumulator + step;
            counter_2 = counter_2 + one;
        }
        accumulator = accumulator + step;
        counter_1 = counter_1 + one;
    }
    CCU_WHILE(counter_1 != args_->loop_count);
    return CcuResult::CCU_SUCCESS;
}

// ======================== CCU DO {} CCU_WHILE Demo (unified syntax) ========================

struct CcuDoWhileUnifiedDemoKernelArg {
    uint32_t loop_count;
};

CcuResult CcuDoWhileUnifiedDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuDoWhileUnifiedDemoKernelArg*>(arg);

    ccu::variable counter;
    counter = 0;

    ccu::variable one;
    one = 1;

    ccu::variable accumulator;
    accumulator = 0;

    ccu::variable step;
    step = 10;

    CCU_DO
    {
        accumulator = accumulator + step;
        counter = counter + one;
    }
    CCU_WHILE(counter != args_->loop_count);

    return CcuResult::CCU_SUCCESS;
}

// ======================== Nested_in_IF IF: if{if{}if{}} ========================

struct CcuNestedInIfIfDemoKernelArg {
    uint32_t outerVal;
    uint64_t outerExpected;
    uint32_t innerVal_1;
    uint64_t innerExpected_1;
    uint32_t innerVal_2;
    uint64_t innerExpected_2;
};

CcuResult CcuNestedInIfIfDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuNestedInIfIfDemoKernelArg*>(arg);

    ccu::variable outerVar;
    outerVar = args_->outerVal;

    ccu::variable innerVar_1;
    innerVar_1 = args_->innerVal_1;

    ccu::variable innerVar_2;
    innerVar_2 = args_->innerVal_2;

    ccu::variable result;
    result = 0;

    ccu::variable addend;

    CCU_IF(outerVar == args_->outerExpected)
    {
        CCU_IF(innerVar_1 == args_->innerExpected_1)
        {
            addend = 10;
            result = result + addend;
        }
        CCU_IF(innerVar_2 == args_->innerExpected_2)
        {
            addend = 20;
            result = result + addend;
        }
    }

    return CcuResult::CCU_SUCCESS;
}
