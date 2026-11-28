/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_api/resource/channel/ccu_channel.h"

#include <cstdint>

#include "ccu_api/common/ccu_device_context.h"
#include "ccu_api/resource/common/asc_ccu_res_snapshot.h"

namespace asc {

ccu_channel::ccu_channel(ChannelHandle channel) { result_ = init(channel); }

ccu_channel* ccu_channel::operator->() { return this; }

const ccu_channel* ccu_channel::operator->() const { return this; }

HcclResult ccu_channel::init(ChannelHandle channel)
{
    HcommCcuChannelEntity entity{};
    const auto channel_query = reinterpret_cast<HcommCcuChannelGetEntityCallBack>(
        get_current_ccu_control_ops()[HCOMM_CCU_CONTROL_SLOT_CHANNEL_ENTITY]);
    if (channel_query == nullptr) {
        return HCCL_E_INTERNAL;
    }
    int32_t ret = channel_query(channel, &entity);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }
    // ABI 头校验：实体必须是当前版本布局
    if (entity.header.version != HCOMM_CCU_CHANNEL_ABI_VERSION ||
        entity.header.magicWord != HCOMM_CCU_CHANNEL_MAGIC_WORD ||
        entity.header.size != sizeof(HcommCcuChannelEntity)) {
        return HCCL_E_INTERNAL;
    }
    // 远端 CCU 资源空间尚未就绪时拒绝使用该 Channel（定档：RegedBufferEntity 口径）
    if (entity.rmtCcuResBuffer.type != REGED_BUFFER_RMA || entity.rmtCcuResBuffer.bufferInfo.rma.addr == 0 ||
        entity.rmtCcuResBuffer.bufferInfo.rma.size == 0) {
        return HCCL_E_INTERNAL;
    }
    // 定长字段与变长数组分别拷贝；本地 remoteBuffers 指针指向自有存储
    remote_buffers_.assign(entity.remoteBuffers, entity.remoteBuffers + entity.remoteBufferNum);
    channel_entity_ = entity;
    channel_entity_.remoteBuffers = remote_buffers_.data();
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

// 资源区段基址与步长（对齐 hcomm CcuResSpecifications 的段布局：
// CCUM + 1M 指令预留 [+ 32K GSA(V1)] + XN 预留（V1 32K / V2 256K）之后为 CKE 区段）
namespace {
constexpr uint64_t CCU_RESOURCE_INS_RESERVE_SIZE = 0x100000;     // 指令预留空间 1M
constexpr uint64_t CCU_V1_RESOURCE_GSA_RESERVE_SIZE = 0x8000;    // V1 GSA 预留空间 32K
constexpr uint64_t CCU_RESOURCE_XN_V1_RESERVE_SIZE = 32 * 1024;  // XN 预留空间 32K
constexpr uint64_t CCU_RESOURCE_XN_V2_RESERVE_SIZE = 256 * 1024; // XN 预留空间 256K
constexpr uint64_t CCU_RESOURCE_CKE_PER_SIZE = 8;                // 单个 CKE 资源占用空间步长
constexpr uint64_t CCU_RESOURCE_XN_PER_SIZE = 8;                 // 单个 XN 资源占用空间步长

uint64_t get_xn_section_base(uint32_t ccu_version)
{
    // XN 区段直接位于指令预留之后
    return CCU_RESOURCE_INS_RESERVE_SIZE;
}

uint64_t get_cke_section_base(uint32_t ccu_version)
{
    if (ccu_version == 0 /* V1 */) {
        return CCU_RESOURCE_INS_RESERVE_SIZE + CCU_V1_RESOURCE_GSA_RESERVE_SIZE + CCU_RESOURCE_XN_V1_RESERVE_SIZE;
    }
    return CCU_RESOURCE_INS_RESERVE_SIZE + CCU_RESOURCE_XN_V2_RESERVE_SIZE; // V2
}
} // namespace

HcclResult ccu_channel::get_rmt_cke_addr_by_index(uint32_t index, uint64_t& rmt_cke_addr) const
{
    uint32_t remote_cke_id = 0;
    HcclResult ret = get_rmt_cke_by_index(index, remote_cke_id);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    uint64_t rmt_base = 0;
    uint32_t size = 0;
    uint32_t token_id = 0;
    uint32_t token_value = 0;
    ret = get_rmt_buffer(rmt_base, size, token_id, token_value);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    rmt_cke_addr = rmt_base + get_cke_section_base(channel_entity_.ccuVersion) +
                   static_cast<uint64_t>(remote_cke_id) * CCU_RESOURCE_CKE_PER_SIZE;
    return HCCL_SUCCESS;
}

HcclResult ccu_channel::get_rmt_var_addr_by_index(uint32_t index, uint64_t& rmt_var_addr) const
{
    uint32_t remote_xn_id = 0;
    HcclResult ret = get_rmt_xn_by_index(index, remote_xn_id);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    uint64_t rmt_base = 0;
    uint32_t size = 0;
    uint32_t token_id = 0;
    uint32_t token_value = 0;
    ret = get_rmt_buffer(rmt_base, size, token_id, token_value);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    rmt_var_addr = rmt_base + get_xn_section_base(channel_entity_.ccuVersion) +
                   static_cast<uint64_t>(remote_xn_id) * CCU_RESOURCE_XN_PER_SIZE;
    return HCCL_SUCCESS;
}

} // namespace asc
