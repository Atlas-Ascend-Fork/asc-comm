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
    // getChannelEntity 返回 hcomm 持有的 Channel Entity 借用指针（不透明 uint64_t 地址）
    uint64_t channel_entity_addr = 0;
    const auto channel_query = reinterpret_cast<HcommCcuGetChannelEntityFn>(
        get_current_ccu_control_ops()[HCOMM_CCU_ASC_CUSTOM_CHANNEL_ENTITY]);
    if (channel_query == nullptr) {
        return HCCL_E_INTERNAL;
    }
    int32_t ret = channel_query(channel, &channel_entity_addr);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }
    if (channel_entity_addr == 0) {
        return HCCL_E_INTERNAL;
    }
    // 借用期内深拷贝快照，此后不再触碰 hcomm 内存
    const auto* entity = reinterpret_cast<const HcommCcuChannelEntity*>(channel_entity_addr);
    // ABI 头校验：实体必须是当前版本布局
    if (entity->header.version != HCOMM_CCU_CHANNEL_ABI_VERSION ||
        entity->header.magicWord != HCOMM_CCU_CHANNEL_MAGIC_WORD ||
        entity->header.size != sizeof(HcommCcuChannelEntity)) {
        return HCCL_E_INTERNAL;
    }
    // 远端 CCU 资源空间尚未就绪时拒绝使用该 Channel（定档：RegedBufferEntity 口径）
    if (entity->rmtCcuResBuffer.type != REGED_BUFFER_RMA || entity->rmtCcuResBuffer.bufferInfo.rma.addr == 0 ||
        entity->rmtCcuResBuffer.bufferInfo.rma.size == 0) {
        return HCCL_E_INTERNAL;
    }
    // 快照整体拷贝到本地，后续算法只读这份实体，不再触碰 hcomm 对象
    channel_entity_ = *entity;
    return HCCL_SUCCESS;
}

// 从实体数组取第 index 个资源 id：先校验快照查询结果，再按有效数量（而非定长容量）校验下标
HcclResult ccu_channel::get_id_by_array(const uint32_t* ids, uint32_t valid_num, uint32_t index, uint32_t& id) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (ids == nullptr) {
        return HCCL_E_PTR;
    }
    if (index >= valid_num) {
        return HCCL_E_PARA;
    }
    id = ids[index];
    return HCCL_SUCCESS;
}

HcclResult ccu_channel::get_result() const { return result_; }

uint32_t ccu_channel::get_die_id() const { return channel_entity_.dieId; }

uint32_t ccu_channel::get_channel_id() const { return channel_entity_.channelId; }

HcclResult ccu_channel::get_loc_cke_by_index(uint32_t index, uint32_t& local_cke_id) const
{
    return get_id_by_array(channel_entity_.localEventIds, channel_entity_.localEventNum, index, local_cke_id);
}

HcclResult ccu_channel::get_loc_xn_by_index(uint32_t index, uint32_t& local_xn_id) const
{
    return get_id_by_array(channel_entity_.localVarIds, channel_entity_.localVarNum, index, local_xn_id);
}

HcclResult ccu_channel::get_rmt_cke_by_index(uint32_t index, uint32_t& remote_cke_id) const
{
    return get_id_by_array(channel_entity_.remoteEventIds, channel_entity_.remoteEventNum, index, remote_cke_id);
}

HcclResult ccu_channel::get_rmt_xn_by_index(uint32_t index, uint32_t& remote_xn_id) const
{
    return get_id_by_array(channel_entity_.remoteVarIds, channel_entity_.remoteVarNum, index, remote_xn_id);
}

HcclResult ccu_channel::get_rmt_buffer(uint64_t& addr, uint32_t& size, uint32_t& token_id, uint32_t& token_value) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    // 远端 CCU 资源空间的基址、长度与 token 已由 hcomm 装配为 RegedBufferEntity，这里拆解返回
    addr = channel_entity_.rmtCcuResBuffer.bufferInfo.rma.addr;
    size = static_cast<uint32_t>(channel_entity_.rmtCcuResBuffer.bufferInfo.rma.size);
    token_id = channel_entity_.rmtCcuResBuffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    token_value = channel_entity_.rmtCcuResBuffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    return HCCL_SUCCESS;
}

} // namespace asc
