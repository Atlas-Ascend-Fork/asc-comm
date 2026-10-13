/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_FUNC_CALL_DEMO_H
#define CCU_FUNC_CALL_DEMO_H

#include "ccu/hcomm/ccu_primitives.hpp"
#include "ccu/hcomm/ccu_api_types.h"

namespace ccu = ::AscendC::ccu;

ccu::func CcuFuncCallBasicFunc([](ccu::variable x) {
    ccu::variable tmp{};
    tmp = x + x;
});

ccu::func CcuFuncCallNestedInnerFunc([](ccu::variable x) {
    ccu::variable tmp{};
    tmp = x + x;
});

ccu::func CcuFuncCallNestedOuterFunc([](ccu::variable x) { (void)ccu::CallFunc<CcuFuncCallNestedInnerFunc>(x); });

inline CcuResult CcuFuncCallBasicDemoKernel(ccu_kernel_arg arg)
{
    (void)arg;
    ccu::variable x{};
    x = 1;
    return ccu::CallFunc<CcuFuncCallBasicFunc>(x);
}

inline CcuResult CcuFuncCallReuseDemoKernel(ccu_kernel_arg arg)
{
    (void)arg;
    ccu::variable x{};
    x = 2;
    CCU_CHK_RET(ccu::CallFunc<CcuFuncCallBasicFunc>(x));
    CCU_CHK_RET(ccu::CallFunc<CcuFuncCallNestedInnerFunc>(x));
    return ccu::CallFunc<CcuFuncCallBasicFunc>(x);
}

inline CcuResult CcuFuncCallInLoopInvalidDemoKernel(ccu_kernel_arg arg)
{
    (void)arg;
    ccu::variable x{};
    x = 3;

    CcuResult bodyRet = CcuResult::CCU_SUCCESS;
    ccu::func body([&]() { bodyRet = ccu::CallFunc<CcuFuncCallBasicFunc>(x); });
    ccu::loop_config dummyCfg{};
    ccu::loop loop(dummyCfg, body);
    return bodyRet;
}

inline CcuResult CcuFuncCallNestedInvalidDemoKernel(ccu_kernel_arg arg)
{
    (void)arg;
    ccu::variable x{};
    x = 4;
    return ccu::CallFunc<CcuFuncCallNestedOuterFunc>(x);
}

ccu::func CcuFuncCallMultiArgFunc([](ccu::variable a, ccu::variable b, ccu::variable c) {
    ccu::variable tmp{};
    tmp = a + b;
    tmp = tmp + c;
});

inline CcuResult CcuFuncCallMultiArgDemoKernel(ccu_kernel_arg arg)
{
    (void)arg;
    ccu::variable x{};
    ccu::variable y{};
    ccu::variable z{};
    x = 1;
    y = 2;
    z = 3;
    return ccu::CallFunc<CcuFuncCallMultiArgFunc>(x, y, z);
}

#endif // CCU_FUNC_CALL_DEMO_H
