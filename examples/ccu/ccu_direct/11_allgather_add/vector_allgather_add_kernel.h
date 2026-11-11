/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef VECTOR_ALLGATHER_ADD_KERNEL_H
#define VECTOR_ALLGATHER_ADD_KERNEL_H

#include "kernel_operator.h"

#include "ccu_allgather_add_kernel.h"

namespace ops_comm_ag {

template <typename T, uint64_t maxCount>
__global__ __vector__ void vector_add(
    __gm__ void* src, __gm__ void* recvBuf, __gm__ void* dstGm, CcuMappedAddrs mappedAddrs, uint64_t token,
    uint64_t currentRankSliceInputOffset, uint64_t currentRankSliceOutputOffset, uint64_t sliceSize, uint64_t goSize0,
    uint64_t goSize1, uint64_t goSize2, uint64_t goSize3, uint64_t count)
{
    AscendC::InitSocState();
    AscendC::GlobalTensor<T> srcGlobal;
    AscendC::GlobalTensor<T> dstGlobal;
    srcGlobal.SetGlobalBuffer((__gm__ T*)recvBuf); // 通信后的数据为计算输入数据
    dstGlobal.SetGlobalBuffer((__gm__ T*)dstGm);

    AscendC::LocalMemAllocator<AscendC::Hardware::UB> ubAllocator;
    AscendC::LocalTensor<T> srcLocal = ubAllocator.Alloc<T, maxCount>();
    AscendC::LocalTensor<T> dstLocal = ubAllocator.Alloc<T, maxCount>();

    __gm__ uint64_t* taskArgs0 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[0]);
    __gm__ uint64_t* taskArgs1 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[1]);
    __gm__ uint64_t* taskArgs2 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[2]);
    __gm__ uint64_t* taskArgs3 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[3]);
    __gm__ uint64_t* taskArgs4 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[4]);
    __gm__ uint64_t* taskArgs5 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[5]);
    __gm__ uint64_t* taskArgs6 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[6]);
    __gm__ uint64_t* taskArgs7 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[7]);
    __gm__ uint64_t* taskArgs8 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[8]);
    __gm__ uint64_t* taskArgs9 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[9]);

    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs0, reinterpret_cast<uint64_t>(src));
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs1, reinterpret_cast<uint64_t>(recvBuf));
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs2, token);
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs3, currentRankSliceInputOffset);
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs4, currentRankSliceOutputOffset);
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs5, sliceSize);
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs6, goSize0);
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs7, goSize1);
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs8, goSize2);
    AscendC::WriteGmBypassDCache<uint64_t>(taskArgs9, goSize3);
    AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

    __gm__ uint64_t* eventAddr0 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[0]);
    __gm__ uint64_t* eventAddr1 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[1]);
    AscendC::WriteGmBypassDCache<uint64_t>(eventAddr0, 1);
    AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

    while (1) {
        uint64_t waitEvent = AscendC::ReadGmBypassDCache(eventAddr1);
        AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();
        AscendC::Nop<800>();
        if (waitEvent == 1) {
            break;
        }
    }

    AscendC::DataCopy(srcLocal, srcGlobal, count);
    AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(EVENT_ID0);
    AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(EVENT_ID0);

    T scalar = 8.0;
    AscendC::Adds(dstLocal, srcLocal, scalar, count);

    AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(EVENT_ID0);
    AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(EVENT_ID0);
    AscendC::DataCopy(dstGlobal, dstLocal, count);
    AscendC::PipeBarrier<PIPE_ALL>();
}
} // namespace ops_comm_ag

#endif // VECTOR_ALLGATHER_ADD_KERNEL_H
