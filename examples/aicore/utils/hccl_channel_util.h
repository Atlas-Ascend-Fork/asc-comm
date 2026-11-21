/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_HCCL_CHANNEL_UTIL_H
#define ASC_COMM_EXAMPLES_UTILS_HCCL_CHANNEL_UTIL_H

#include <cstdint>
#include <cstring>

#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

namespace examples {

// 查找本端到对端的指定协议链路，并填充channel描述（对端rank、协议、两端endpoint、注册内存）。
// 入口先调用HcclChannelDescInit：HcclChannelDesc为POD，ABI头（version/magicWord/size）、
// notifyNum等字段未经初始化会使HcclChannelAcquire参数校验失败（HCCL_E_PARA）。
// 逐层扫描rank graph，取第一条匹配协议的链路；notifyNum保持HcclChannelDescInit的默认值。
// 未找到匹配链路时返回HCCL_E_NOT_SUPPORT。
inline HcclResult find_channel_desc(
    HcclComm comm, uint32_t rank, uint32_t peer_rank, CommProtocol protocol, HcclMemHandle* mem_handles,
    uint32_t mem_handle_num, HcclChannelDesc& channel_desc)
{
    if (HcclChannelDescInit(&channel_desc, 1U) != HCCL_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    uint32_t* layers = nullptr;
    uint32_t layer_num = 0;
    HcclResult ret = HcclRankGraphGetLayers(comm, &layers, &layer_num);
    if (ret != HCCL_SUCCESS || layer_num == 0U || layers == nullptr) {
        return HCCL_E_INTERNAL;
    }
    for (uint32_t layer = 0U; layer < layer_num; ++layer) {
        CommLink* links = nullptr;
        uint32_t link_num = 0;
        ret = HcclRankGraphGetLinks(comm, layers[layer], rank, peer_rank, &links, &link_num);
        if (ret != HCCL_SUCCESS || link_num == 0U || links == nullptr) {
            continue;
        }
        for (uint32_t i = 0U; i < link_num; ++i) {
            if (links[i].linkAttr.linkProtocol != protocol) {
                continue;
            }
            channel_desc.remoteRank = peer_rank;
            channel_desc.channelProtocol = protocol;
            channel_desc.localEndpoint = links[i].srcEndpointDesc;
            channel_desc.remoteEndpoint = links[i].dstEndpointDesc;
            channel_desc.memHandles = mem_handles;
            channel_desc.memHandleNum = mem_handle_num;
            return HCCL_SUCCESS;
        }
    }
    return HCCL_E_NOT_SUPPORT;
}

// 创建到对端的通信通道（AIV引擎；内部完成链路查找与channel描述填充）。
inline HcclResult acquire_channel(
    HcclComm comm, uint32_t rank, uint32_t peer_rank, CommProtocol protocol, HcclMemHandle* mem_handles,
    uint32_t mem_handle_num, ChannelHandle& channel)
{
    HcclChannelDesc channel_desc;
    HcclResult ret = find_channel_desc(comm, rank, peer_rank, protocol, mem_handles, mem_handle_num, channel_desc);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    ret = HcclChannelAcquire(comm, COMM_ENGINE_AIV, &channel_desc, 1U, &channel);
    if (ret == HCCL_SUCCESS && channel == 0U) {
        return HCCL_E_INTERNAL;
    }
    return ret;
}

// 在通道对端的注册内存中按tag查回buffer，输出其下标与地址；对端仅注册一块内存时直接返回0。
// mem_index/remote_addr允许传nullptr按需接收；下标供kernel侧按索引访问远端buffer时使用。
inline HcclResult get_remote_mem(
    HcclComm comm, ChannelHandle channel, const char* tag, uint32_t* mem_index, uint64_t* remote_addr)
{
    uint32_t mem_num = 0;
    CommMem* mems = nullptr;
    char** tags = nullptr;
    HcclResult ret = HcclChannelGetRemoteMems(comm, channel, &mem_num, &mems, &tags);
    if (ret != HCCL_SUCCESS || mem_num == 0U || mems == nullptr) {
        return HCCL_E_INTERNAL;
    }
    for (uint32_t i = 0U; i < mem_num; ++i) {
        if (tags != nullptr && tags[i] != nullptr && std::strcmp(tags[i], tag) == 0) {
            if (mem_index != nullptr) {
                *mem_index = i;
            }
            if (remote_addr != nullptr) {
                *remote_addr = reinterpret_cast<uint64_t>(mems[i].addr);
            }
            return HCCL_SUCCESS;
        }
    }
    if (mem_num == 1U) {
        if (mem_index != nullptr) {
            *mem_index = 0U;
        }
        if (remote_addr != nullptr) {
            *remote_addr = reinterpret_cast<uint64_t>(mems[0].addr);
        }
        return HCCL_SUCCESS;
    }
    return HCCL_E_INTERNAL;
}

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_HCCL_CHANNEL_UTIL_H
