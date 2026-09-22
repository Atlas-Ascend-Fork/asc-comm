/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/common/asc_ccu_res_snapshot.h"
#include "hcomm/common/ccu_log.h"

#include <algorithm>
#include <limits>

namespace asc {
namespace {

std::vector<asc_ccu_res_range>* get_ranges(asc_ccu_res_repository& repository, uint32_t resource_type, uint32_t die_id)
{
    switch (resource_type) {
        case HCOMM_CCU_BATCH_RES_LOOP:
            return &repository.loop_engine[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_LOOP:
            return &repository.block_loop_engine[die_id];
        case HCOMM_CCU_BATCH_RES_MS:
            return &repository.ms[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_MS:
            return &repository.block_ms[die_id];
        case HCOMM_CCU_BATCH_RES_CKE:
            return &repository.cke[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_CKE:
            return &repository.block_cke[die_id];
        case HCOMM_CCU_BATCH_RES_XN:
            return &repository.xn[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_XN:
            return &repository.block_xn[die_id];
        case HCOMM_CCU_BATCH_RES_GSA:
            return &repository.gsa[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_GSA:
            return &repository.block_gsa[die_id];
        case HCOMM_CCU_BATCH_RES_MISSION:
            return &repository.mission[die_id];
        default:
            return nullptr; // INS 不走 range 视图，由 ascCustom.allocInstSpace 按需申请
    }
}

bool is_instance_header_valid(const HcommCcuInstance& instance)
{
    return instance.header.version == HCOMM_CCU_RES_ABI_VERSION &&
           instance.header.magicWord == HCOMM_CCU_INSTANCE_MAGIC_WORD &&
           instance.header.size == sizeof(HcommCcuInstance) && instance.header.reserved == 0;
}

bool is_die_metadata_valid(const HcommCcuDieMetadata& metadata)
{
    return metadata.header.version == HCOMM_CCU_RES_ABI_VERSION &&
           metadata.header.magicWord == HCOMM_CCU_DIE_METADATA_MAGIC_WORD &&
           metadata.header.size == sizeof(HcommCcuDieMetadata) && metadata.header.reserved == 0 &&
           metadata.reserved == 0;
}

// 从 die 元数据推导有效 die 位图
uint32_t derive_valid_die_mask(const HcommCcuInstance& instance)
{
    uint32_t mask = 0;
    for (uint32_t slot = 0; slot < instance.ccuDieNum && slot < HCOMM_CCU_INSTANCE_DIE_CAPACITY; ++slot) {
        const HcommCcuDieMetadata* metadata = instance.ccuDieReses[slot].dieMetadata;
        if (metadata != nullptr && metadata->enabled != 0) {
            mask |= 1U << slot;
        }
    }
    return mask;
}

// ascCustom 校验：槽 0-2 （ChannelEntity/AllocInstSpace/SubmitInsts）
bool is_asc_custom_valid(const uint64_t (&asc_custom)[HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT])
{
    return asc_custom[HCOMM_CCU_ASC_CUSTOM_CHANNEL_ENTITY] != 0 &&
           asc_custom[HCOMM_CCU_ASC_CUSTOM_ALLOC_INST_SPACE] != 0 && asc_custom[HCOMM_CCU_ASC_CUSTOM_SUBMIT_INSTS] != 0;
}

bool ranges_overlap(const asc_ccu_res_range& left, const asc_ccu_res_range& right)
{
    if (left.resource_type != right.resource_type || left.die_id != right.die_id) {
        return false;
    }
    const uint64_t left_end = static_cast<uint64_t>(left.start_id) + left.count;
    const uint64_t right_end = static_cast<uint64_t>(right.start_id) + right.count;
    return static_cast<uint64_t>(left.start_id) < right_end && static_cast<uint64_t>(right.start_id) < left_end;
}

// 把一段 wire range（类型与 die 来自外层索引）追加进本地仓储，并做形状与重叠校验
CcuResult append_wire_ranges(
    const HcommCcuResRanges& type_view, uint32_t resource_type, uint32_t die_id, uint32_t valid_die_mask,
    std::vector<asc_ccu_res_range>& all_ranges, asc_ccu_res_repository& repository)
{
    if (type_view.resourceType != resource_type || type_view.reserved != 0) {
        return CcuResult::CCU_E_PARA;
    }
    if (type_view.resRangeNum > 0 && type_view.resRanges == nullptr) {
        return CcuResult::CCU_E_PARA;
    }
    for (uint32_t index = 0; index < type_view.resRangeNum; ++index) {
        const HcommCcuResRange& range = type_view.resRanges[index];
        if (range.reserved != 0 || (valid_die_mask & (1U << die_id)) == 0 || range.count == 0 ||
            static_cast<uint64_t>(range.startId) + range.count >
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U) {
            return CcuResult::CCU_E_PARA;
        }
        asc_ccu_res_range local{resource_type, die_id, range.startId, range.count};
        for (const auto& existing : all_ranges) {
            if (ranges_overlap(existing, local)) {
                return CcuResult::CCU_E_PARA;
            }
        }
        all_ranges.push_back(local);
        auto* destination = get_ranges(repository, resource_type, die_id);
        if (destination == nullptr) {
            return CcuResult::CCU_E_PARA;
        }
        destination->push_back(local);
    }
    return CcuResult::CCU_SUCCESS;
}
} // namespace

CcuResult asc_ccu_res_snapshot::load(const HcommCcuInstance& instance)
{
    if (!is_instance_header_valid(instance) || instance.ccuDieNum > HCOMM_CCU_INSTANCE_DIE_CAPACITY ||
        instance.ccuDieNum != HCOMM_CCU_MAX_DIE_NUM || // 现网 2 die；硬件扩展后放开
        instance.ccuVersion > HCOMM_CCU_VERSION_V2 || !is_asc_custom_valid(instance.ascCustom)) {
        return CcuResult::CCU_E_PARA;
    }

    asc_ccu_res_repository instance_snapshot{};
    std::vector<asc_ccu_res_range> all_ranges{};
    std::array<HcommCcuDieMetadata, HCOMM_CCU_MAX_DIE_NUM> die_metadata{};

    // 先按 dieId 顺序装载各 die；有效位图从 dieMetadata.enabled 推导
    uint32_t derived_die_mask = 0;
    for (uint32_t slot = 0; slot < HCOMM_CCU_INSTANCE_DIE_CAPACITY; ++slot) {
        const HcommCcuDieRes& die_res = instance.ccuDieReses[slot];
        if (slot < instance.ccuDieNum) {
            if (die_res.dieId != slot) {
                return CcuResult::CCU_E_PARA;
            }
            // 两遍装载：第一遍取元数据（enabled 决定位图），第二遍按位图校验 range 视图
            if (die_res.dieMetadata == nullptr) {
                // 未启用 die：视图必须为空
                if (die_res.resTypeNum != 0) {
                    return CcuResult::CCU_E_PARA;
                }
                continue;
            }
            if (!is_die_metadata_valid(*die_res.dieMetadata) || die_res.dieMetadata->enabled > 1) {
                return CcuResult::CCU_E_PARA;
            }
            die_metadata[slot] = *die_res.dieMetadata; // 深拷贝元数据，此后不再触碰 hcomm 内存
            if (die_metadata[slot].enabled != 0) {
                derived_die_mask |= 1U << slot;
            }
        } else if (
            die_res.dieId != 0 || die_res.resTypeNum != 0 || die_res.dieMetadata != nullptr ||
            std::any_of(die_res.reserved, die_res.reserved + 16, [](uint64_t field) { return field != 0; })) {
            // 超出 ccuDieNum 的预留槽位必须整体为零
            return CcuResult::CCU_E_PARA;
        }
    }
    if (derived_die_mask == 0 || (derived_die_mask & ~((1U << HCOMM_CCU_MAX_DIE_NUM) - 1U)) != 0) {
        return CcuResult::CCU_E_PARA;
    }
    // 第二遍：按推导位图校验并装载 range（启用 die 必须有元数据；未启用 die 不得携带元数据）
    for (uint32_t slot = 0; slot < instance.ccuDieNum; ++slot) {
        const HcommCcuDieRes& die_res = instance.ccuDieReses[slot];
        const bool enabled = (derived_die_mask & (1U << slot)) != 0;
        if (enabled) {
            if (die_res.dieMetadata == nullptr) {
                return CcuResult::CCU_E_PARA;
            }
            for (uint32_t type = 0; type < HCOMM_CCU_BATCH_RES_TYPE_COUNT; ++type) {
                CCU_CHK_RET(append_wire_ranges(
                    die_res.resTypes[type], type, die_res.dieId, derived_die_mask, all_ranges, instance_snapshot));
            }
        } else if (die_res.dieMetadata != nullptr || die_res.resTypeNum != 0) {
            return CcuResult::CCU_E_PARA;
        }
    }

    asc_ccu_res_repository current_repo = instance_snapshot;
    ccu_version_ = instance.ccuVersion;
    valid_die_mask_ = derived_die_mask;
    for (uint32_t i = 0; i < HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT; ++i) {
        control_ops_[i] = instance.ascCustom[i];
    }
    initial_repo_ = std::move(instance_snapshot);
    res_repo_ = std::move(current_repo);
    die_metadata_ = die_metadata;
    return CcuResult::CCU_SUCCESS;
}

CcuResult asc_ccu_res_snapshot::reset()
{
    if (valid_die_mask_ == 0) {
        return CcuResult::CCU_E_UNAVAIL;
    }
    res_repo_ = initial_repo_;
    return CcuResult::CCU_SUCCESS;
}

asc_ccu_res_repository& asc_ccu_res_snapshot::get_ccu_res_repo() { return res_repo_; }

const HcommCcuDieMetadata* asc_ccu_res_snapshot::get_die_metadata(uint32_t die_id) const
{
    return die_id < HCOMM_CCU_MAX_DIE_NUM && (valid_die_mask_ & (1U << die_id)) != 0 ? &die_metadata_[die_id] : nullptr;
}

uint32_t asc_ccu_res_snapshot::get_ccu_version() const { return ccu_version_; }

uint32_t asc_ccu_res_snapshot::get_valid_die_mask() const { return valid_die_mask_; }

const uint64_t* asc_ccu_res_snapshot::get_control_ops() const { return control_ops_; }

} // namespace asc
