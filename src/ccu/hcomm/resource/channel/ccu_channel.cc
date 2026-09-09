/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/channel/ccu_channel.h"

#include <cstdint>

#include "hcomm/common/ccu_device_context.h"

namespace asc {

ccu_channel::ccu_channel(ChannelHandle channel) { result_ = init(channel); }

ccu_channel* ccu_channel::operator->() { return this; }

const ccu_channel* ccu_channel::operator->() const { return this; }

HcclResult ccu_channel::init(ChannelHandle channel)
{
    // 组装带 ABI 头的查询 POD，通知 hcomm 按当前 ABI 版本填写快照
    HcommCcuChannelPod query_pod{};
    query_pod.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
    query_pod.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
    query_pod.header.size = sizeof(HcommCcuChannelPod);

    // 通过控制面注入的函数表发起单次查询（不再分两阶段传数组地址）
    const auto channel_query = get_current_ccu_control_ops().channelQuery;
    if (channel_query == nullptr) {
        return HCCL_E_INTERNAL;
    }
    int32_t ret = channel_query(channel, &query_pod);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }
    // 远端 CCU buffer 尚未就绪时拒绝使用该 Channel
    if (query_pod.rmtCcuBufSize == 0) {
        return HCCL_E_INTERNAL;
    }
    // 快照整体拷贝到本地，后续算法只读这份 POD，不再触碰 hcomm 对象
    channel_pod_ = query_pod;
    return HCCL_SUCCESS;
}

// 从 POD 定长数组取第 index 个资源 id：先校验快照查询结果，再校验数组与下标
HcclResult ccu_channel::get_id_by_array(const uint32_t* ids, uint32_t index, uint32_t& id) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (ids == nullptr) {
        return HCCL_E_PTR;
    }
    if (index >= HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY) {
        return HCCL_E_PARA;
    }
    id = ids[index];
    return HCCL_SUCCESS;
}

HcclResult ccu_channel::get_result() const { return result_; }

uint32_t ccu_channel::get_die_id() const { return channel_pod_.dieId; }

uint32_t ccu_channel::get_channel_id() const { return channel_pod_.channelId; }

HcclResult ccu_channel::get_loc_cke_by_index(uint32_t index, uint32_t& local_cke_id) const
{
    return get_id_by_array(channel_pod_.localCkeIds, index, local_cke_id);
}

HcclResult ccu_channel::get_loc_xn_by_index(uint32_t index, uint32_t& local_xn_id) const
{
    return get_id_by_array(channel_pod_.localXnIds, index, local_xn_id);
}

HcclResult ccu_channel::get_rmt_cke_by_index(uint32_t index, uint32_t& remote_cke_id) const
{
    return get_id_by_array(channel_pod_.remoteCkeIds, index, remote_cke_id);
}

HcclResult ccu_channel::get_rmt_xn_by_index(uint32_t index, uint32_t& remote_xn_id) const
{
    return get_id_by_array(channel_pod_.remoteXnIds, index, remote_xn_id);
}

HcclResult ccu_channel::get_rmt_buffer(uint64_t& addr, uint32_t& size, uint32_t& token_id, uint32_t& token_value) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    // 远端 CCU 资源 buffer 的基址、长度与 token 已由 hcomm 在快照中按设备规格算好，这里直接返回
    addr = channel_pod_.rmtCcuBufAddr;
    size = channel_pod_.rmtCcuBufSize;
    token_id = channel_pod_.rmtCcuBufTokenId;
    token_value = channel_pod_.rmtCcuBufTokenValue;
    return HCCL_SUCCESS;
}

} // namespace asc
