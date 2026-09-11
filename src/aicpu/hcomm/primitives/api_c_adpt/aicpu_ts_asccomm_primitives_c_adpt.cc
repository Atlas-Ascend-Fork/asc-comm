/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cmath>
#include <limits>
#include "hcomm/asccomm_primitives.h"
#include "hcomm/hcomm_pod_resource.h"
#include "asccomm_launch.h"
#include "asccomm_runtime.h"
#include "asccomm_transport.h"
#include "../../common/utils/exception_util.h"
#include "log/hcomm_log.h"
namespace {

template <typename Entity>
HcclResult validate_entity(const Entity *entity, uint32_t version, uint32_t magicWord)
{
    CHK_PTR_NULL(entity);
    CHK_PRT_RET(entity->abiHeader.version != version || entity->abiHeader.magicWord != magicWord,
        HCCL_ERROR("[%s] invalid POD ABI header, version[%u], magicWord[0x%x].", __func__,
            entity->abiHeader.version, entity->abiHeader.magicWord), HCCL_E_PARA);
    CHK_PRT_RET(entity->abiHeader.size < sizeof(Entity),
        HCCL_ERROR("[%s] POD entity size[%u] is less than expected size[%zu].", __func__, entity->abiHeader.size,
            sizeof(Entity)),
        HCCL_E_PARA);
    return HCCL_SUCCESS;
}

// 契约(CHANNEL ABI=2)：thread实体由控制面预解析后随算子下发，此处仅做合法性校验；
// registerThread为true时登记batch模式批量launch
HcclResult check_thread_entity(HcommThreadEntity *thread, bool registerThread)
{
    CHK_RET(validate_entity(thread, HCOMM_POD_THREAD_VERSION, HCOMM_POD_THREAD_MAGIC_WORD));
    CHK_PTR_NULL(thread->sqContextAddr);
    if (registerThread) {
        asccomm_register_thread(thread);
        CHK_RET(asccomm_ensure_rtsq_resolved(*thread));
    }
    return HCCL_SUCCESS;
}

HcclResult check_channel_entity(HcommChannelEntity *channel)
{
    CHK_RET(validate_entity(channel, HCOMM_POD_CHANNEL_VERSION, HCOMM_POD_CHANNEL_MAGIC_WORD));
    return HCCL_SUCCESS;
}

HcclResult get_thread_notify_id(const HcommThreadEntity &thread, uint32_t notifyIdx, uint32_t &notifyId)
{
    CHK_PRT_RET(notifyIdx >= thread.localNotifyNum || thread.localNotifyAddr == nullptr,
        HCCL_ERROR("[%s] local notify index[%u] out of range[%u].", __func__, notifyIdx,
            thread.localNotifyNum), HCCL_E_PARA);
    // 契约v2：线程notify由ipcRt承载(HCOMM_NOTIFY_TYPE_LOCAL枚举仅作分类保留)
    CHK_PRT_RET(thread.localNotifyAddr[notifyIdx].type != HCOMM_REGED_NOTIFY_IPC_RT,
        HCCL_ERROR("[%s] local notify type[%u] is not IPC_RT.", __func__,
            thread.localNotifyAddr[notifyIdx].type), HCCL_E_PARA);
    const int32_t resourceId = thread.localNotifyAddr[notifyIdx].notifyInfo.ipcRt.notifyId;
    CHK_PRT_RET(resourceId < 0, HCCL_ERROR("[%s] invalid notify id[%d].", __func__, resourceId), HCCL_E_PARA);
    notifyId = static_cast<uint32_t>(resourceId);
    return HCCL_SUCCESS;
}

HcclResult convert_reduce_in(HcommDataType dataType, HcommReduceOp reduceOp, Asc::DataType &innerDataType,
                           Asc::ReduceOp &innerReduceOp, uint32_t &typeSize)
{
    switch (dataType) {
        case HCOMM_DATA_TYPE_INT8: innerDataType = Asc::DataType::INT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_INT16: innerDataType = Asc::DataType::INT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT32: innerDataType = Asc::DataType::INT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP16: innerDataType = Asc::DataType::FP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_FP32: innerDataType = Asc::DataType::FP32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_INT64: innerDataType = Asc::DataType::INT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT64: innerDataType = Asc::DataType::UINT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT8: innerDataType = Asc::DataType::UINT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_UINT16: innerDataType = Asc::DataType::UINT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_UINT32: innerDataType = Asc::DataType::UINT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP64: innerDataType = Asc::DataType::FP64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_BFP16: innerDataType = Asc::DataType::BFP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT128: innerDataType = Asc::DataType::INT128; typeSize = 16; break;
#ifndef OPEN_BUILD_PROJECT
        case HCOMM_DATA_TYPE_HIF8: innerDataType = Asc::DataType::HIF8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_FP8E4M3: innerDataType = Asc::DataType::FP8E4M3; typeSize = 1; break;
        case HCOMM_DATA_TYPE_FP8E5M2: innerDataType = Asc::DataType::FP8E5M2; typeSize = 1; break;
        case HCOMM_DATA_TYPE_FP8E8M0: innerDataType = Asc::DataType::FP8E8M0; typeSize = 1; break;
#endif
        default:
            HCCL_ERROR("[%s] dataType[%u] is not supported.", __func__, dataType);
            return HCCL_E_PARA;
    }
    switch (reduceOp) {
        case HCOMM_REDUCE_SUM: innerReduceOp = Asc::ReduceOp::SUM; break;
        case HCOMM_REDUCE_PROD: innerReduceOp = Asc::ReduceOp::PROD; break;
        case HCOMM_REDUCE_MAX: innerReduceOp = Asc::ReduceOp::MAX; break;
        case HCOMM_REDUCE_MIN: innerReduceOp = Asc::ReduceOp::MIN; break;
        default:
            HCCL_ERROR("[%s] reduceOp[%u] is not supported.", __func__, reduceOp);
            return HCCL_E_PARA;
    }
    return HCCL_SUCCESS;
}

HcclResult get_reduce_params(HcommDataType dataType, HcommReduceOp reduceOp, uint64_t count,
                           uint64_t &len, Asc::ReduceIn &reduceIn)
{
    Asc::DataType innerDataType = Asc::DataType::INVALID;
    Asc::ReduceOp innerReduceOp = Asc::ReduceOp::INVALID;
    uint32_t typeSize = 0;
    CHK_RET(convert_reduce_in(dataType, reduceOp, innerDataType, innerReduceOp, typeSize));
    CHK_PRT_RET(count > std::numeric_limits<uint64_t>::max() / typeSize,
        HCCL_ERROR("[%s] reduce length overflow.", __func__), HCCL_E_PARA);
    len = count * typeSize;
    reduceIn = Asc::ReduceIn(innerDataType, innerReduceOp);
    return HCCL_SUCCESS;
}

// 契约v2：POD DFX事件机制删除；观测由dlog(HCCL_INFO/HCCL_ERROR)直接输出。
// execute_api保留异常保护语义(仅try-catch，无DFX上报)。
template <typename Operation>
HcclResult execute_api(Operation operation)
{
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = operation(), ret = HCCL_E_INTERNAL);
    return ret;
}

HcclResult convert_timeout(float timeout, uint32_t &timeoutSeconds)
{
    if (std::isnan(timeout) || timeout < 0.0f ||
        static_cast<double>(timeout) > static_cast<double>(UINT32_MAX)) {
        HCCL_ERROR("[%s] timeout[%f] is invalid.", __func__, timeout);
        return HCCL_E_PARA;
    }
    timeoutSeconds = static_cast<uint32_t>(timeout);
    return HCCL_SUCCESS;
}

} // namespace

int32_t asccomm_batch_mode_start(const char *batchTag)
{
    return execute_api([&]() {
        asccomm_set_batch_launch_mode(true);
        return HCCL_SUCCESS;
    });
}

int32_t asccomm_batch_mode_end(const char *batchTag)
{
    return execute_api([&]() {
        asccomm_set_batch_launch_mode(false);
        const HcclResult ret = asccomm_launch_registered_threads();
        asccomm_clear_registered_threads();
        return ret;
    });
}

int32_t asccomm_local_copy_on_thread(HcommThreadEntity *thread, void *dst, const void *src, uint64_t len)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        CHK_RET(check_thread_entity(thread, true));
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(asccomm_rtsq_sdma_copy(
                *thread->sqContextAddr, reinterpret_cast<uintptr_t>(dst) + offset,
                reinterpret_cast<uintptr_t>(src) + offset, static_cast<uint32_t>(realSize)));
            offset += realSize;
        }
        return HCCL_SUCCESS;
    });
}

int32_t asccomm_local_reduce_on_thread(
    HcommThreadEntity *thread, void *dst, const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        CHK_RET(check_thread_entity(thread, true));
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(asccomm_rtsq_sdma_reduce(
                *thread->sqContextAddr, reinterpret_cast<uintptr_t>(dst) + offset,
                reinterpret_cast<uintptr_t>(src) + offset, static_cast<uint32_t>(realSize), reduceIn));
            offset += realSize;
        }
        return HCCL_SUCCESS;
    });
}

int32_t asccomm_thread_notify_record_on_thread(
    HcommThreadEntity *thread, HcommThreadEntity *dstThread, uint32_t dstNotifyIdx)
{
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_thread_entity(dstThread, false));
        uint32_t notifyId = 0;
        CHK_RET(get_thread_notify_id(*dstThread, dstNotifyIdx, notifyId));
        return asccomm_rtsq_notify_record(*thread->sqContextAddr, notifyId);
    });
}

int32_t asccomm_thread_notify_wait_on_thread(HcommThreadEntity *thread, uint32_t notifyIdx, uint32_t timeOut)
{
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, true));
        uint32_t notifyId = 0;
        CHK_RET(get_thread_notify_id(*thread, notifyIdx, notifyId));
        return asccomm_rtsq_notify_wait(*thread->sqContextAddr, notifyId, timeOut);
    });
}

int32_t asccomm_aclrt_notify_record_on_thread(HcommThreadEntity *thread, uint64_t dstNotifyId)
{
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, true));
        return asccomm_rtsq_notify_record(*thread->sqContextAddr, static_cast<uint32_t>(dstNotifyId));
    });
}

int32_t asccomm_aclrt_notify_wait_on_thread(HcommThreadEntity *thread, uint64_t notifyId, uint32_t timeOut)
{
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, true));
        return asccomm_rtsq_notify_wait(*thread->sqContextAddr, static_cast<uint32_t>(notifyId), timeOut);
    });
}

int32_t asccomm_write_on_thread(HcommThreadEntity *thread, HcommChannelEntity *channel, void *dst, const void *src,
    uint64_t len)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_write(
            *thread, *channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len);
    });
}

int32_t asccomm_write_reduce_on_thread(HcommThreadEntity *thread, HcommChannelEntity *channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_write_reduce(
            *thread, *channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len, reduceIn);
    });
}

int32_t asccomm_write_with_notify_on_thread(HcommThreadEntity *thread, HcommChannelEntity *channel, void *dst,
    const void *src, uint64_t len, uint32_t remoteNotifyIdx)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_write_with_notify(
            *thread, *channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            remoteNotifyIdx);
    });
}

int32_t asccomm_write_reduce_with_notify_on_thread(HcommThreadEntity *thread, HcommChannelEntity *channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp, uint32_t remoteNotifyIdx)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_write_reduce_with_notify(
            *thread, *channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len, reduceIn,
            remoteNotifyIdx);
    });
}

int32_t asccomm_read_on_thread(HcommThreadEntity *thread, HcommChannelEntity *channel, void *dst, const void *src,
    uint64_t len)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_read(
            *thread, *channel, reinterpret_cast<uintptr_t>(dst), reinterpret_cast<uintptr_t>(src), len);
    });
}

int32_t asccomm_read_reduce_on_thread(HcommThreadEntity *thread, HcommChannelEntity *channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_read_reduce(
            *thread, *channel, reinterpret_cast<uintptr_t>(dst), reinterpret_cast<uintptr_t>(src), len, reduceIn);
    });
}

int32_t asccomm_batch_transfer_on_thread(HcommThreadEntity *thread, HcommChannelEntity *channel,
    const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum)
{
    return execute_api([&]() {
        CHK_PTR_NULL(transferDescs);
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_batch_transfer(*thread, *channel, transferDescs, transferDescNum);
    });
}

int32_t asccomm_channel_notify_record_on_thread(
    HcommThreadEntity *thread, HcommChannelEntity *channel, uint32_t remoteNotifyIdx)
{
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_notify_record(*thread, *channel, remoteNotifyIdx);
    });
}

int32_t asccomm_channel_notify_wait_on_thread(
    HcommThreadEntity *thread, HcommChannelEntity *channel, uint32_t localNotifyIdx, uint32_t timeOut)
{
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_notify_wait(*thread, *channel, localNotifyIdx, timeOut);
    });
}

int32_t asccomm_channel_fence_on_thread(HcommThreadEntity *thread, HcommChannelEntity *channel)
{
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, false));
        CHK_RET(check_channel_entity(channel));
        return asccomm_fence(*channel);
    });
}

int32_t asccomm_channel_notify_wait_on_thread_with_default_timeout(HcommThreadEntity *thread,
    HcommChannelEntity *channel, uint32_t localNotifyIdx)
{
    const uint32_t timeout = asccomm_get_notify_wait_timeout();
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_notify_wait(*thread, *channel, localNotifyIdx, timeout);
    });
}

int32_t asccomm_thread_notify_wait_on_thread_with_default_timeout(HcommThreadEntity *thread, uint32_t notifyIdx)
{
    const uint32_t timeout = asccomm_get_notify_wait_timeout();
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, true));
        uint32_t notifyId = 0;
        CHK_RET(get_thread_notify_id(*thread, notifyIdx, notifyId));
        return asccomm_rtsq_notify_wait(*thread->sqContextAddr, notifyId, timeout);
    });
}

int32_t asccomm_set_notify_wait_timeout(float timeOut)
{
    return execute_api([&]() {
        uint32_t timeoutSeconds = 0;
        CHK_RET(convert_timeout(timeOut, timeoutSeconds));
        asccomm_set_notify_wait_timeout(timeoutSeconds);
        return HCCL_SUCCESS;
    });
}

int32_t asccomm_thread_res_acquire_timeout(float timeOut)
{
    return execute_api([&]() {
        uint32_t timeoutSeconds = 0;
        CHK_RET(convert_timeout(timeOut, timeoutSeconds));
        asccomm_set_sq_full_timeout(timeoutSeconds);
        return HCCL_SUCCESS;
    });
}

int32_t asccomm_channel_drain_on_thread(HcommThreadEntity *thread, HcommChannelEntity *channel)
{
    return execute_api([&]() {
        CHK_RET(check_thread_entity(thread, true));
        CHK_RET(check_channel_entity(channel));
        return asccomm_drain(*thread, *channel);
    });
}
