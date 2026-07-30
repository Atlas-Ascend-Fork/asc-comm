/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 */
#ifndef ASC_COMM_EXAMPLE_CCU_DIRECT_COMMON_H
#define ASC_COMM_EXAMPLE_CCU_DIRECT_COMMON_H

#include <cstdint>
#include <vector>

#include <ccu/ccu_types.h>
#include <hccl/hccl_res.h>
#include <hccl/hccl_types.h>

#include "binary_stream.h"

using CcuKernelHandle = uint64_t;

constexpr uint32_t CCU_MAX_RANK_SIZE = 16;

struct CommBuffer {
    void *addr{nullptr};
    uint64_t size{0};
};

struct CcuKernelArgBase {
    ChannelHandle channels[CCU_MAX_RANK_SIZE]{};
    uint32_t channelCount{0};
};

struct AlgResourceCtxSerializable {
    CommBuffer cclMem;
    uint32_t notifyNumOnMainThread;
    std::vector<ThreadHandle> threads;
    std::vector<ChannelHandle> kernelChannels;
    std::vector<uint32_t> ccuKernelNum;
    std::vector<CcuKernelHandle> ccuKernels;

    std::vector<char> Serialize()
    {
        BinaryStream stream;
        stream << cclMem;
        stream << notifyNumOnMainThread;
        stream << threads;
        stream << kernelChannels;
        stream << ccuKernelNum;
        stream << ccuKernels;

        std::vector<char> result;
        stream.Dump(result);
        return result;
    }

    void DeSerialize(std::vector<char> &data)
    {
        BinaryStream stream(data);
        stream >> cclMem;
        stream >> notifyNumOnMainThread;
        stream >> threads;
        stream >> kernelChannels;
        stream >> ccuKernelNum;
        stream >> ccuKernels;
    }
};

inline HcclResult ConvertCcuToHccl(CcuResult result)
{
    switch (result) {
        case CCU_SUCCESS:
            return HCCL_SUCCESS;
        case CCU_E_PARA:
            return HCCL_E_PARA;
        case CCU_E_PTR:
            return HCCL_E_PTR;
        case CCU_E_INTERNAL:
            return HCCL_E_INTERNAL;
        case CCU_E_NOT_SUPPORT:
            return HCCL_E_NOT_SUPPORT;
        case CCU_E_NOT_FOUND:
            return HCCL_E_NOT_FOUND;
        case CCU_E_UNAVAIL:
            return HCCL_E_UNAVAIL;
        default:
            return HCCL_E_INTERNAL;
    }
}

#endif  // ASC_COMM_EXAMPLE_CCU_DIRECT_COMMON_H
