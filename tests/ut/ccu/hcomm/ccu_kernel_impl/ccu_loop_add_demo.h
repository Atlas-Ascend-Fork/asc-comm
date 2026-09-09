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

struct CcuLoopAddKernelArg {
    uint32_t numA{0};
    uint32_t numB{0};
};

CcuResult CcuLoopAddDemoKernel(ccu_kernel_arg arg)
{
    using namespace ccu;
    auto* args_ = static_cast<CcuLoopAddKernelArg*>(arg);

    variable r1{};
    variable r2{};
    variable r3{};
    variable r4{};
    variable r5{};
    variable r6{};
    variable r7{};
    variable numA{};
    variable numB{};

    numA = args_->numA;
    numB = args_->numB;

    r1 = numA + numB;

    // ========== loop_group 1 (config-based): two config loops, no unroll ==========
    func body1([&]() { r2 = numA + numB; });
    func body2([&]() { r3 = numA + numB; });

    loop_config cfg1 = {.addr_offset = 0, .iter_num = 2};
    loop_config cfg2 = {.addr_offset = 0, .iter_num = 2};
    loop loop1(cfg1, body1);
    loop loop2(cfg2, body2);

    loop_group_config grpCfg1 = {
        .clone_num = 0, .clone_loop_offset = 0, .addr_offset = 0, .ccu_buffer_offset = 0, .event_offset = 0};
    loop_group group1(grpCfg1, /* max_loop_num= */ 2, {loop1, loop2});

    r4 = numA + numB;

    // ========== loop_group 2: reuse loop2 + offset loop3 ==========
    func body3([&]() { r5 = numA + numB; });
    loop_config cfg3 = {.addr_offset = 4096, .iter_num = 4};
    loop loop3(cfg3, body3);

    loop_group_config grpCfg2 = {
        .clone_num = 3, .clone_loop_offset = 1, .addr_offset = 4096, .ccu_buffer_offset = 1, .event_offset = 1};
    loop_group group2(grpCfg2, /* max_loop_num= */ 2, {loop2, loop3});

    // ========== loop_group 3 (var_-based): variable group with two distinct var_-loops ==========
    variable varLoopParam4{};
    variable varLoopParam5{};
    variable varParallel{};
    variable varOffset{};

    varLoopParam4 = 0x0001000200030000ULL;
    varLoopParam5 = 0x0002000300040000ULL;
    varParallel = 0x0002000100020000ULL;
    varOffset = 0x1000000100010000ULL;

    func body4([&]() { r6 = numA + numB; });
    func body5([&]() { r7 = numA + numB; });
    loop loop4(varLoopParam4, body4);
    loop loop5(varLoopParam5, body5);

    loop_group group3(varParallel, varOffset, /* max_loop_num= */ 2, {loop4, loop5});

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuV2CompatLoopGroupDemoKernel(ccu_kernel_arg arg)
{
    using namespace ccu;
    (void)arg;

    variable numA{};
    variable numB{};
    variable r1{};
    numA = 3;
    numB = 4;

    variable varLoopParam1{};
    variable varLoopParam2{};
    variable varParallel{};
    variable varOffset{};
    varLoopParam1 = 0x0001000200030000ULL;
    varLoopParam2 = 0x0002000300040000ULL;
    varParallel = 0x0002000100020000ULL;
    varOffset = 0x1000000100010000ULL;

    func body1([&]() { r1 = numA + numB; });
    func body2([&]() { r1 = numA + numB; });

    loop loop1(varLoopParam1, body1);
    loop loop2(varLoopParam2, body2);

    loop_group group(varParallel, varOffset, /* max_loop_num= */ 2, {loop1, loop2});

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuV2ConfigLoopGroupDemoKernel(ccu_kernel_arg arg)
{
    using namespace ccu;
    (void)arg;

    variable numA{};
    variable numB{};
    variable r1{};
    variable r2{};
    numA = 3;
    numB = 4;

    func body1([&]() { r1 = numA + numB; });
    func body2([&]() { r2 = numA + numB; });

    loop_config cfg1 = {.addr_offset = 0, .iter_num = 2};
    loop_config cfg2 = {.addr_offset = 4096, .iter_num = 4};
    loop loop1(cfg1, body1);
    loop loop2(cfg2, body2);

    loop_group_config grpCfg = {
        .clone_num = 3, .clone_loop_offset = 1, .addr_offset = 4096, .ccu_buffer_offset = 1, .event_offset = 1};
    loop_group group(grpCfg, /* max_loop_num= */ 2, {loop1, loop2});

    return CcuResult::CCU_SUCCESS;
}

inline CcuResult CcuIfInLoopInvalidDemoKernel(ccu_kernel_arg arg)
{
    using namespace ccu;
    (void)arg;
    variable v{};
    v = 0;
    func body([&]() {
        CCU_IF(v == 0)
        {
            variable t{};
            t = v + v;
        }
    });
    loop_config dummyCfg{};
    loop loop(dummyCfg, body);
    return CcuResult::CCU_SUCCESS;
}

inline CcuResult CcuNotifyRecordInLoopInvalidDemoKernel(ccu_kernel_arg arg)
{
    using namespace ccu;
    (void)arg;
    ChannelHandle ch = 0;
    func body([&]() { (void)notify_record(ch, 0); });
    loop_config dummyCfg{};
    loop loop(dummyCfg, body);
    return CcuResult::CCU_SUCCESS;
}

inline CcuResult CcuWriteVarWithNotifyInLoopInvalidDemoKernel(ccu_kernel_arg arg)
{
    using namespace ccu;
    (void)arg;
    ChannelHandle ch = 0;
    variable v{};
    v = 1;
    func body([&]() { (void)write_variable_with_notify(ch, v, 0, 0); });
    loop_config dummyCfg{};
    loop loop(dummyCfg, body);
    return CcuResult::CCU_SUCCESS;
}

inline CcuResult CcuEventRecordTagInLoopInvalidDemoKernel(ccu_kernel_arg arg)
{
    using namespace ccu;
    (void)arg;
    func body([&]() { (void)event_record("evt_tag", 1); });
    loop_config dummyCfg{};
    loop loop(dummyCfg, body);
    return CcuResult::CCU_SUCCESS;
}

inline CcuResult CcuEventRecordInLoopInvalidDemoKernel(ccu_kernel_arg arg)
{
    using namespace ccu;
    (void)arg;
    event evt{};
    func body([&]() { (void)event_record(evt, 1); });
    loop_config dummyCfg{};
    loop loop(dummyCfg, body);
    return CcuResult::CCU_SUCCESS;
}

// 全组合覆盖：group 类型（config / var_） × loop 类型（config / var_ / 混用）。
// 校验点（按 group 创建顺序在日志中出现 LoopGroupBundle[loops=N, total_loop_num=M]）：
//   - config 型 group：无论内部 loop 是 config / var_ / 混用，total_loop_num 必须等于 loop 总数；
//   - var_ 型 group：parallel_param 走运行期寄存器，bundle 的 total_loop_num 不参与，保持 0。
CcuResult CcuLoopCfgDemoKernel(ccu_kernel_arg arg)
{
    using namespace ccu;
    (void)arg;

    variable a{};
    variable b{};
    a = 3;
    b = 4;

    // 每个 loop 各自 compose 一份独立 body
    variable s1{};
    variable s2{};
    variable s3{};
    variable s4{};
    variable s5{};
    variable s6{};
    func body1([&]() { s1 = a + b; });
    func body2([&]() { s2 = a + b; });
    func body3([&]() { s3 = a + b; });
    func body4([&]() { s4 = a + b; });
    func body5([&]() { s5 = a + b; });
    func body6([&]() { s6 = a + b; });

    // config 型 loop
    loop_config cfg = {.addr_offset = 0, .iter_num = 2};
    loop lc1(cfg, body1);
    loop lc2(cfg, body2);
    loop lc3(cfg, body3);

    // var_ 型 loop
    variable lp1{};
    variable lp2{};
    variable lp3{};
    lp1 = 2;
    lp2 = 2;
    lp3 = 2;
    loop lv1(lp1, body4);
    loop lv2(lp2, body5);
    loop lv3(lp3, body6);

    loop_group_config cfgGrp = {
        .clone_num = 0, .clone_loop_offset = 0, .addr_offset = 0, .ccu_buffer_offset = 0, .event_offset = 0};

    // ===== config 型 group（total_loop_num 以立即数编码，必须等于 loop 总数）=====
    // 1) config group + config loops  -> loops=2, total_loop_num=2
    loop_group g1(cfgGrp, /* max_loop_num= */ 2, {lc1, lc2});
    // 2) config group + var_ loops      -> loops=3, total_loop_num=3（
    loop_group g2(cfgGrp, /* max_loop_num= */ 3, {lv1, lv2, lv3});
    // 3) config group + mixed loops    -> loops=2, total_loop_num=2
    loop_group g3(cfgGrp, /* max_loop_num= */ 2, {lc1, lv1});

    // ===== var_ 型 group（parallel/offset 走运行期寄存器，bundle total_loop_num 保持 0）=====
    variable par{};
    variable off{};
    par = 0x0002000100020000ULL;
    off = 0x1000000100010000ULL;
    // 4) var_ group + var_ loops         -> loops=2, total_loop_num=0
    loop_group g4(par, off, /* max_loop_num= */ 2, {lv1, lv2});
    // 5) var_ group + config loops      -> loops=3, total_loop_num=0
    loop_group g5(par, off, /* max_loop_num= */ 3, {lc1, lc2, lc3});
    // 6) var_ group + mixed loops       -> loops=2, total_loop_num=0
    loop_group g6(par, off, /* max_loop_num= */ 2, {lc1, lv1});

    return CcuResult::CCU_SUCCESS;
}
