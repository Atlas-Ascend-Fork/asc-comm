/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "adapter_hccp.h"
#include "ccu/hcomm/ccu_primitives.hpp"
#include "ccu/hcomm/ccu_api_types.h"
#include "hcomm/common/ccu_log.h"
#include <vector>

namespace ccu = ::AscendC::ccu;

struct CcuVarAddKernelArg {
    uint64_t numA{0xffffffff};
    uint32_t numB{0};
    ChannelHandle channelHandle{0};
};

void test_method(std::vector<ccu::remote_addr>& loopsrc)
{
    for (int i = 0; i < 1; i++) {
        ccu::local_addr localAddr1;
        loopsrc.emplace_back(*reinterpret_cast<ccu::remote_addr*>(&localAddr1));
    }
}
CcuResult CcuAssignDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    ccu::remote_addr remote_addr;
    remote_addr.addr_ = 0x10000000;
    remote_addr.token = 0x20000000;
    std::vector<ccu::remote_addr> loopsrc;
    test_method(loopsrc);
    loopsrc[0].addr_ = remote_addr.addr_;
    loopsrc[0].token = remote_addr.token;
    return CcuResult::CCU_SUCCESS;
}
CcuResult CcuAllocDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    ccu::variable var_a_;
    ccu::variable var_b_;
    ccu::variable result;
    var_a_ = 1024;
    var_b_ = 2048;
    result = var_a_ + var_b_;
    result = var_a_ << var_b_;
    result = var_a_ >> var_b_;
    result <<= var_b_;
    result >>= var_b_;
    ccu::load_arg(var_a_, 0);
    ccu::load_arg(var_b_, 1);

    ccu::variable var_c_ = ccu::GetResByChannel<ccu::variable>(args_->channel_handle; ccu::variable 0);
    var_a_ = var_c_;

    ccu::address addr_a_;
    ccu::address addr_b_;
    ccu::address addrResult;
    addr_a_ = 0x80000000;
    addr_b_ = 0x90000000;
    addrResult = addr_a_ + addr_b_;

    ccu::event evt;
    ccu::event_record(evt);
    ccu::event_wait(evt);

    ccu::array<ccu::event> evt2(2);
    ccu::event_record(evt2[0]);
    ccu::event_wait(evt2[0]);
    ccu::event_record(evt2[1]);
    ccu::event_wait(evt2[1]);

    return CcuResult::CCU_SUCCESS;
}
CcuResult CcuLocalAddrDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    ccu::local_addr local_addr;
    local_addr.addr_ = 0x10000000;
    local_addr.token = 0x20000000;

    ccu::local_addr localAddr2;
    localAddr2 = local_addr;
    return CcuResult::CCU_SUCCESS;
}
CcuResult CcuRemoteAddrDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    ccu::remote_addr remote_addr;
    remote_addr.addr_ = 0x10000000;
    remote_addr.token = 0x20000000;

    ccu::remote_addr remoteAddr2;
    remoteAddr2 = remote_addr;
    return CcuResult::CCU_SUCCESS;
}
CcuResult CcuLoadStoreDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    ccu::variable var_a_;
    ccu::variable var_b_;
    ccu::variable result;
    ccu::variable srcAddr;
    ccu::variable dst_addr;
    srcAddr = 0x30000000;
    dst_addr = 0x40000000;
    ccu::load(0x10000000, var_a_);
    ccu::Store(0x20000000, var_b_);
    ccu::load(srcAddr, var_a_);
    ccu::Store(dst_addr, var_b_);
    ccu::array<ccu::variable> varArr(2);
    ccu::array<ccu::variable> varArr2(2);
    ccu::load(0x10000000, varArr, 2);
    ccu::Store(0x20000000, varArr2, 2);
    ccu::load(srcAddr, varArr, 2);
    ccu::Store(dst_addr, varArr2, 2);
    return CcuResult::CCU_SUCCESS;
}
CcuResult CcuNotifyDemoKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    // mask_ 已与 event 解耦，统一作为 API 参数显式传入。
    ccu::array<ccu::event> evt(3);
    ccu::event_record(evt[0], 0x12);
    ccu::event_wait(evt[0], 0x12);
    ccu::event_record(evt[1], 0x13);
    ccu::event_wait(evt[1], 0x13);
    ccu::event_record(evt[2], 0x14);
    ccu::event_wait(evt[2], 0x14);

    ccu::notify_record(args_->channel_handle, 0, 0x12);
    ccu::notify_wait(args_->channel_handle, 0, 0x12);
    ccu::variable var_a_;
    var_a_ = 1024;
    ccu::write_variable_with_notify(args_->channel_handle, var_a_, 0, 0, 0x12);
    ccu::notify_wait(args_->channel_handle, 0, 0x12);
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuLocalNotifyDemoKernel(ccu_kernel_arg arg)
{
    (void)arg;
    ccu::event_record("local_notify_tag_default");
    ccu::event_wait("local_notify_tag_default");

    ccu::event_record("local_notify_tag_a", 0x12);
    ccu::event_wait("local_notify_tag_a", 0x12);
    ccu_local_notify_record("local_notify_tag_b", 0x34);
    ccu_local_notify_wait("local_notify_tag_b", 0x34);
    return CcuResult::CCU_SUCCESS;
}
CcuResult CcuLocalCopyKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    ccu::local_addr src_;
    ccu::local_addr dst_;
    src_.addr_ = 0x10000000;
    src_.token = 0x20000000;
    dst_.addr_ = 0x30000000;
    dst_.token = 0x40000000;
    ccu::event evt;
    ccu::array<ccu::ccu_buffer> buf(1);
    ccu::variable len_;
    len_ = 1024;
    ccu::LocalCopy(buf[0], src_, len_, evt);
    ccu::event_wait(evt);
    ccu::LocalCopy(dst_, buf[0], len_, evt);
    ccu::event_wait(evt);
    ccu::LocalCopy(dst_, src_, len_, evt);
    ccu::event_wait(evt);
    return CcuResult::CCU_SUCCESS;
}
CcuResult CcuLocalReduceKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    ccu::local_addr src_;
    ccu::local_addr dst_;
    src_.addr_ = 0x10000000;
    src_.token = 0x20000000;
    dst_.addr_ = 0x30000000;
    dst_.token = 0x40000000;
    ccu::variable len_;
    len_ = 1024;
    ccu::event evt;
    ccu::LocalReduce(dst_, src_, len_, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, evt);
    ccu::event_wait(evt);
    ccu::array<ccu::ccu_buffer> buf(2);
    ccu::LocalReduce(buf.data(), 2, HCCL_DATA_TYPE_FP16, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, len_, evt);
    ccu::event_wait(evt);

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuRemoteReadKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    ccu::remote_addr dst_;
    ccu::local_addr src_;
    src_.addr_ = 0x10000000;
    src_.token = 0x20000000;
    dst_.addr_ = 0x30000000;
    dst_.token = 0x40000000;
    ccu::variable len_;
    len_ = 1024;
    ccu::event evt;
    ccu::Read(args_->channel_handle, src_, dst_, len_, evt);
    ccu::event_wait(evt);
    ccu::array<ccu::ccu_buffer> buf(1);
    ccu::Read(args_->channel_handle, buf[0], dst_, len_, evt);
    ccu::event_wait(evt);
    ccu::ReadReduce(args_->channel_handle, src_, dst_, len_, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, evt);
    ccu::event_wait(evt);
    return CcuResult::CCU_SUCCESS;
}
CcuResult CcuRemoteWriteKernel(ccu_kernel_arg arg)
{
    auto* args_ = static_cast<CcuVarAddKernelArg*>(arg);
    ccu::remote_addr dst_;
    ccu::local_addr src_;
    ccu::array<ccu::ccu_buffer> buf(1);
    ccu::variable len_;
    len_ = 1024;
    ccu::event evt;
    src_.addr_ = 0x10000000;
    src_.token = 0x20000000;
    dst_.addr_ = 0x30000000;
    dst_.token = 0x40000000;

    ccu::Write(args_->channel_handle, dst_, src_, len_, evt);
    ccu::event_wait(evt);
    ccu::Write(args_->channel_handle, dst_, buf[0], len_, evt);
    ccu::event_wait(evt);
    ccu::WriteReduce(args_->channel_handle, dst_, src_, len_, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, evt);
    ccu::event_wait(evt);
    return CcuResult::CCU_SUCCESS;
}
