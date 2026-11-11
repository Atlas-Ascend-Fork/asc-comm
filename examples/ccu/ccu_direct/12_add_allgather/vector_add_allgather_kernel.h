/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef VECTOR_ADD_ALLGATHER_KERNEL_H
#define VECTOR_ADD_ALLGATHER_KERNEL_H

#include "kernel_operator.h"

#include "ccu_add_allgather_kernel.h"

namespace ops_comm_ag {

template <typename T, uint64_t count>
__global__ __vector__ void vector_add(
    __gm__ void* srcGm, __gm__ void* computeGm, __gm__ void* dstGm, CcuMappedAddrs mappedAddrs)
{
    AscendC::InitSocState();
    AscendC::GlobalTensor<T> srcGlobal;
    AscendC::GlobalTensor<T> dstGlobal;
    srcGlobal.SetGlobalBuffer((__gm__ T*)srcGm);
    dstGlobal.SetGlobalBuffer((__gm__ T*)computeGm);

    AscendC::LocalMemAllocator<AscendC::Hardware::UB> ubAllocator;
    AscendC::LocalTensor<T> srcLocal = ubAllocator.Alloc<T, count>();
    AscendC::LocalTensor<T> dstLocal = ubAllocator.Alloc<T, count>();

    AscendC::DataCopy(srcLocal, srcGlobal, count);
    AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(EVENT_ID0);
    AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(EVENT_ID0);

    T scalar = 1.0;
    AscendC::Adds(dstLocal, srcLocal, scalar, count);

    AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(EVENT_ID0);
    AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(EVENT_ID0);
    AscendC::DataCopy(dstGlobal, dstLocal, count);
    AscendC::PipeBarrier<PIPE_ALL>();

    __gm__ uint64_t* taskArgs0 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[0]);
    __gm__ uint64_t* taskArgs1 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[1]);

    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs0, reinterpret_cast<uint64_t>(computeGm)); // 计算输出为通信输入
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs1, reinterpret_cast<uint64_t>(dstGm));
    AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

    // taskargs参数设置后，通知ccu通信任务启动 使用event[0]
    __gm__ uint64_t* eventAddr0 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[0]);
    __gm__ uint64_t* eventAddr1 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[1]);
    AscendC::WriteGmBypassDCache<uint64_t>(eventAddr0, 1);
    AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

    while (1) {
        // 轮询通信任务是否结束，使用event[1]
        uint64_t waitEvent = AscendC::ReadGmBypassDCache(eventAddr1);
        AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();
        AscendC::Nop<800>();
        if (waitEvent == 1) {
            break;
        }
    }
}
} // namespace ops_comm_ag

#endif // VECTOR_ADD_ALLGATHER_KERNEL_H
