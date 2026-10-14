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
#include "asccomm_primitives.h"
#include "asccomm_res_defs.h"
#include "asccomm_launch.h"
#include "asccomm_runtime.h"
#include "asccomm_transport.h"
#include "../../common/utils/exception_util.h"
#include "asccomm_log.h"

namespace {

struct CommContext {
    ThreadEntity* thread;
    ChannelEntity* channel;
};

template <typename Entity>
AsccommResult validate_entity(const Entity* entity, uint32_t version, uint32_t magicWord)
{
    CHK_PTR_NULL(entity);
    CHK_PRT_RET(
        entity->abiHeader.version != version || entity->abiHeader.magicWord != magicWord,
        ASCCOMM_ERROR(
            "[%s] invalid resource ABI header, version[%u], magicWord[0x%x].", __func__, entity->abiHeader.version,
            entity->abiHeader.magicWord),
        ASCCOMM_E_PARA);
    CHK_PRT_RET(
        entity->abiHeader.size < sizeof(Entity),
        ASCCOMM_ERROR(
            "[%s] resource entity size[%u] is less than expected size[%zu].", __func__, entity->abiHeader.size,
            sizeof(Entity)),
        ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

AsccommResult get_thread_entity(ThreadHandle thread, bool registerThread, bool resolveRtsq, ThreadEntity*& threadEntity)
{
    CHK_PRT_RET(thread == 0, ASCCOMM_ERROR("[%s] thread handle is 0.", __func__), ASCCOMM_E_PTR);
    // The public ThreadHandle is the device resource entity address.  The control
    // plane performs the original-handle -> POD conversion before launch.
    threadEntity = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(thread));
    CHK_RET(validate_entity(threadEntity, HCOMM_RESOURCE_THREAD_VERSION, HCOMM_RESOURCE_THREAD_MAGIC_WORD));
    CHK_PTR_NULL(threadEntity->sqContextAddr);
    if (resolveRtsq) {
        CHK_RET(asccomm_resolve_thread_rtsq(threadEntity));
    }
    if (registerThread) {
        asccomm_register_thread(thread);
    }
    return ASCCOMM_SUCCESS;
}

AsccommResult get_context(ThreadHandle thread, ChannelHandle channel, bool registerThread, CommContext& context)
{
    CHK_PRT_RET(channel == 0, ASCCOMM_ERROR("[%s] channel handle is 0.", __func__), ASCCOMM_E_PTR);
    CHK_RET(get_thread_entity(thread, registerThread, true, context.thread));
    // ChannelHandle follows the same resource-address ABI as ThreadHandle.
    context.channel = reinterpret_cast<ChannelEntity*>(static_cast<uintptr_t>(channel));
    CHK_RET(validate_entity(context.channel, HCOMM_RESOURCE_CHANNEL_VERSION, HCOMM_RESOURCE_CHANNEL_MAGIC_WORD));
    return ASCCOMM_SUCCESS;
}

AsccommResult get_thread_notify_id(const ThreadEntity& thread, uint32_t notifyIdx, uint32_t& notifyId)
{
    CHK_PRT_RET(
        notifyIdx >= thread.localNotifyNum || thread.localNotifyAddr == nullptr,
        ASCCOMM_ERROR("[%s] local notify index[%u] out of range[%u].", __func__, notifyIdx, thread.localNotifyNum),
        ASCCOMM_E_PARA);
    CHK_PRT_RET(
        thread.localNotifyAddr[notifyIdx].type != HCOMM_REGED_NOTIFY_IPC_RT,
        ASCCOMM_ERROR("[%s] local notify type[%u] is not IPC_RT.", __func__, thread.localNotifyAddr[notifyIdx].type),
        ASCCOMM_E_PARA);
    const int32_t resourceId = thread.localNotifyAddr[notifyIdx].notifyInfo.ipcRt.notifyId;
    CHK_PRT_RET(resourceId < 0, ASCCOMM_ERROR("[%s] invalid notify id[%d].", __func__, resourceId), ASCCOMM_E_PARA);
    notifyId = static_cast<uint32_t>(resourceId);
    return ASCCOMM_SUCCESS;
}

AsccommResult convert_reduce_in(
    AsccommDataType dataType, AsccommReduceOp reduceOp, Asc::DataType& innerDataType, Asc::ReduceOp& innerReduceOp,
    uint32_t& typeSize)
{
    switch (dataType) {
        case ASCCOMM_DATA_TYPE_INT8:
            innerDataType = Asc::DataType::INT8;
            typeSize = 1;
            break;
        case ASCCOMM_DATA_TYPE_INT16:
            innerDataType = Asc::DataType::INT16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_INT32:
            innerDataType = Asc::DataType::INT32;
            typeSize = 4;
            break;
        case ASCCOMM_DATA_TYPE_FP16:
            innerDataType = Asc::DataType::FP16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_FP32:
            innerDataType = Asc::DataType::FP32;
            typeSize = 4;
            break;
        case ASCCOMM_DATA_TYPE_INT64:
            innerDataType = Asc::DataType::INT64;
            typeSize = 8;
            break;
        case ASCCOMM_DATA_TYPE_UINT64:
            innerDataType = Asc::DataType::UINT64;
            typeSize = 8;
            break;
        case ASCCOMM_DATA_TYPE_UINT8:
            innerDataType = Asc::DataType::UINT8;
            typeSize = 1;
            break;
        case ASCCOMM_DATA_TYPE_UINT16:
            innerDataType = Asc::DataType::UINT16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_UINT32:
            innerDataType = Asc::DataType::UINT32;
            typeSize = 4;
            break;
        case ASCCOMM_DATA_TYPE_FP64:
            innerDataType = Asc::DataType::FP64;
            typeSize = 8;
            break;
        case ASCCOMM_DATA_TYPE_BFP16:
            innerDataType = Asc::DataType::BFP16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_INT128:
            innerDataType = Asc::DataType::INT128;
            typeSize = 16;
            break;
#ifndef OPEN_BUILD_PROJECT
        case ASCCOMM_DATA_TYPE_HIF8:
            innerDataType = Asc::DataType::HIF8;
            typeSize = 1;
            break;
        case ASCCOMM_DATA_TYPE_FP8E4M3:
            innerDataType = Asc::DataType::FP8E4M3;
            typeSize = 1;
            break;
        case ASCCOMM_DATA_TYPE_FP8E5M2:
            innerDataType = Asc::DataType::FP8E5M2;
            typeSize = 1;
            break;
        case ASCCOMM_DATA_TYPE_FP8E8M0:
            innerDataType = Asc::DataType::FP8E8M0;
            typeSize = 1;
            break;
#endif
        default:
            ASCCOMM_ERROR("[%s] dataType[%u] is not supported.", __func__, dataType);
            return ASCCOMM_E_PARA;
    }
    switch (reduceOp) {
        case ASCCOMM_REDUCE_SUM:
            innerReduceOp = Asc::ReduceOp::SUM;
            break;
        case ASCCOMM_REDUCE_PROD:
            innerReduceOp = Asc::ReduceOp::PROD;
            break;
        case ASCCOMM_REDUCE_MAX:
            innerReduceOp = Asc::ReduceOp::MAX;
            break;
        case ASCCOMM_REDUCE_MIN:
            innerReduceOp = Asc::ReduceOp::MIN;
            break;
        default:
            ASCCOMM_ERROR("[%s] reduceOp[%u] is not supported.", __func__, reduceOp);
            return ASCCOMM_E_PARA;
    }
    return ASCCOMM_SUCCESS;
}

AsccommResult get_reduce_params(
    AsccommDataType dataType, AsccommReduceOp reduceOp, uint64_t count, uint64_t& len, Asc::ReduceIn& reduceIn)
{
    Asc::DataType innerDataType = Asc::DataType::INVALID;
    Asc::ReduceOp innerReduceOp = Asc::ReduceOp::INVALID;
    uint32_t typeSize = 0;
    CHK_RET(convert_reduce_in(dataType, reduceOp, innerDataType, innerReduceOp, typeSize));
    CHK_PRT_RET(
        count > std::numeric_limits<uint64_t>::max() / typeSize,
        ASCCOMM_ERROR("[%s] reduce length overflow.", __func__), ASCCOMM_E_PARA);
    len = count * typeSize;
    reduceIn = Asc::ReduceIn(innerDataType, innerReduceOp);
    return ASCCOMM_SUCCESS;
}

template <typename Operation>
AsccommResult execute_api(Operation operation)
{
    AsccommResult ret = ASCCOMM_SUCCESS;
    EXCEPTION_CATCH(ret = operation(), ret = ASCCOMM_E_INTERNAL);
    return ret;
}

AsccommResult convert_timeout(float timeout, uint32_t& timeoutSeconds)
{
    if (std::isnan(timeout) || timeout < 0.0f || static_cast<double>(timeout) > static_cast<double>(UINT32_MAX)) {
        ASCCOMM_ERROR("[%s] timeout[%f] is invalid.", __func__, timeout);
        return ASCCOMM_E_PARA;
    }
    timeoutSeconds = static_cast<uint32_t>(timeout);
    return ASCCOMM_SUCCESS;
}

} // namespace

int32_t asccomm_batch_mode_start(const char* batchTag)
{
    return execute_api([&]() {
        asccomm_set_batch_launch_mode(true);
        return ASCCOMM_SUCCESS;
    });
}

int32_t asccomm_batch_mode_end(const char* batchTag)
{
    return execute_api([&]() {
        asccomm_set_batch_launch_mode(false);
        CHK_RET(asccomm_launch_registered_threads());
        asccomm_clear_registered_threads();
        return ASCCOMM_SUCCESS;
    });
}

int32_t asccomm_local_copy_on_thread(ThreadHandle thread, void* dst, const void* src, uint64_t len)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        ThreadEntity* threadEntity = nullptr;
        CHK_RET(get_thread_entity(thread, true, true, threadEntity));
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(asccomm_rtsq_sdma_copy(
                *threadEntity->sqContextAddr, reinterpret_cast<uintptr_t>(dst) + offset,
                reinterpret_cast<uintptr_t>(src) + offset, static_cast<uint32_t>(realSize)));
            offset += realSize;
        }
        return ASCCOMM_SUCCESS;
    });
}

int32_t asccomm_local_reduce_on_thread(
    ThreadHandle thread, void* dst, const void* src, uint64_t count, AsccommDataType dataType, AsccommReduceOp reduceOp)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        ThreadEntity* threadEntity = nullptr;
        CHK_RET(get_thread_entity(thread, true, true, threadEntity));
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(asccomm_rtsq_sdma_reduce(
                *threadEntity->sqContextAddr, reinterpret_cast<uintptr_t>(dst) + offset,
                reinterpret_cast<uintptr_t>(src) + offset, static_cast<uint32_t>(realSize), reduceIn));
            offset += realSize;
        }
        return ASCCOMM_SUCCESS;
    });
}

int32_t asccomm_thread_notify_record_on_thread(ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx)
{
    return execute_api([&]() {
        ThreadEntity* threadEntity = nullptr;
        ThreadEntity* dstThreadEntity = nullptr;
        CHK_RET(get_thread_entity(thread, true, true, threadEntity));
        CHK_RET(get_thread_entity(dstThread, false, false, dstThreadEntity));
        uint32_t notifyId = 0;
        CHK_RET(get_thread_notify_id(*dstThreadEntity, dstNotifyIdx, notifyId));
        return asccomm_rtsq_notify_record(*threadEntity->sqContextAddr, notifyId);
    });
}

int32_t asccomm_thread_notify_wait_on_thread(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut)
{
    return execute_api([&]() {
        ThreadEntity* threadEntity = nullptr;
        CHK_RET(get_thread_entity(thread, true, true, threadEntity));
        uint32_t notifyId = 0;
        CHK_RET(get_thread_notify_id(*threadEntity, notifyIdx, notifyId));
        return asccomm_rtsq_notify_wait(*threadEntity->sqContextAddr, notifyId, timeOut);
    });
}

int32_t asccomm_aclrt_notify_record_on_thread(ThreadHandle thread, uint64_t dstNotifyId)
{
    return execute_api([&]() {
        ThreadEntity* threadEntity = nullptr;
        CHK_RET(get_thread_entity(thread, true, true, threadEntity));
        return asccomm_rtsq_notify_record(*threadEntity->sqContextAddr, static_cast<uint32_t>(dstNotifyId));
    });
}

int32_t asccomm_aclrt_notify_wait_on_thread(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut)
{
    return execute_api([&]() {
        ThreadEntity* threadEntity = nullptr;
        CHK_RET(get_thread_entity(thread, true, true, threadEntity));
        return asccomm_rtsq_notify_wait(*threadEntity->sqContextAddr, static_cast<uint32_t>(notifyId), timeOut);
    });
}

int32_t asccomm_write_on_thread(ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t len)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_write(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len);
    });
}

int32_t asccomm_write_reduce_on_thread(
    ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t count, AsccommDataType dataType,
    AsccommReduceOp reduceOp)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_write_reduce(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            reduceIn);
    });
}

int32_t asccomm_write_with_notify_on_thread(
    ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t len, uint32_t remoteNotifyIdx)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_write_with_notify(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            remoteNotifyIdx);
    });
}

int32_t asccomm_write_reduce_with_notify_on_thread(
    ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t count, AsccommDataType dataType,
    AsccommReduceOp reduceOp, uint32_t remoteNotifyIdx)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_write_reduce_with_notify(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            reduceIn, remoteNotifyIdx);
    });
}

int32_t asccomm_read_on_thread(ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t len)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_read(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(dst), reinterpret_cast<uintptr_t>(src), len);
    });
}

int32_t asccomm_read_reduce_on_thread(
    ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t count, AsccommDataType dataType,
    AsccommReduceOp reduceOp)
{
    return execute_api([&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_read_reduce(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(dst), reinterpret_cast<uintptr_t>(src), len,
            reduceIn);
    });
}

int32_t asccomm_batch_transfer_on_thread(
    ThreadHandle thread, ChannelHandle channel, const AsccommBatchTransferDesc* transferDescs, uint32_t transferDescNum)
{
    return execute_api([&]() {
        CHK_PTR_NULL(transferDescs);
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_batch_transfer(*context.thread, *context.channel, transferDescs, transferDescNum);
    });
}

int32_t asccomm_channel_notify_record_on_thread(ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx)
{
    return execute_api([&]() {
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_notify_record(*context.thread, *context.channel, remoteNotifyIdx);
    });
}

int32_t asccomm_channel_notify_wait_on_thread(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut)
{
    return execute_api([&]() {
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_notify_wait(*context.thread, *context.channel, localNotifyIdx, timeOut);
    });
}

int32_t asccomm_channel_fence_on_thread(ThreadHandle thread, ChannelHandle channel)
{
    return execute_api([&]() {
        CommContext context{};
        CHK_RET(get_context(thread, channel, false, context));
        return asccomm_fence(*context.channel);
    });
}

int32_t asccomm_channel_notify_wait_on_thread_with_default_timeout(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx)
{
    const uint32_t timeout = asccomm_get_notify_wait_timeout();
    return execute_api([&]() {
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_notify_wait(*context.thread, *context.channel, localNotifyIdx, timeout);
    });
}

int32_t asccomm_thread_notify_wait_on_thread_with_default_timeout(ThreadHandle thread, uint32_t notifyIdx)
{
    const uint32_t timeout = asccomm_get_notify_wait_timeout();
    return execute_api([&]() {
        ThreadEntity* threadEntity = nullptr;
        CHK_RET(get_thread_entity(thread, true, true, threadEntity));
        uint32_t notifyId = 0;
        CHK_RET(get_thread_notify_id(*threadEntity, notifyIdx, notifyId));
        return asccomm_rtsq_notify_wait(*threadEntity->sqContextAddr, notifyId, timeout);
    });
}

int32_t asccomm_set_notify_wait_timeout(float timeOut)
{
    return execute_api([&]() {
        uint32_t timeoutSeconds = 0;
        CHK_RET(convert_timeout(timeOut, timeoutSeconds));
        asccomm_set_notify_wait_timeout(timeoutSeconds);
        return ASCCOMM_SUCCESS;
    });
}

int32_t asccomm_thread_res_acquire_timeout(float timeOut)
{
    return execute_api([&]() {
        uint32_t timeoutSeconds = 0;
        CHK_RET(convert_timeout(timeOut, timeoutSeconds));
        asccomm_set_sq_full_timeout(timeoutSeconds);
        return ASCCOMM_SUCCESS;
    });
}

int32_t asccomm_channel_drain_on_thread(ThreadHandle thread, ChannelHandle channel)
{
    return execute_api([&]() {
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_drain(*context.thread, *context.channel);
    });
}
