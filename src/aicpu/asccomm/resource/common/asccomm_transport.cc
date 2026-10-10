/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <limits>
#include <vector>
#include "asccomm_transport.h"
#include "asccomm_runtime.h"
#include "../connection/ub_conn_lite.h"
#include "../connection/ub_jetty_lite.h"
#include "../connection/rdma_vendor_1825_ops.h"
#include "securec.h"
#include "asccomm_log.h"

namespace {

constexpr uint32_t UB_STRONG_ORDER = 2U;
constexpr uint32_t UB_RELAX_ORDER = 1U;
constexpr uint32_t UB_COMPLETION = 1U;
constexpr uint32_t UB_NO_COMPLETION = 0U;
constexpr uint32_t NOTIFY_VALUE = 1U;
constexpr uint32_t NOTIFY_SIZE = sizeof(uint32_t);
constexpr uint32_t DEFAULT_TIMEOUT = 1836U;
constexpr uint64_t RDMA_DMA_MAX_SIZE = 0x80000000ULL;

bool is_ub_protocol(CommProtocol protocol)
{
    return protocol == COMM_PROTOCOL_UBC_CTP || protocol == COMM_PROTOCOL_UBC_TP || protocol == COMM_PROTOCOL_UB_MEM ||
           protocol == static_cast<CommProtocol>(7) || protocol == static_cast<CommProtocol>(9);
}

bool is_range_contained(uint64_t base, uint64_t capacity, uint64_t addr, uint64_t size)
{
    return addr >= base && size <= capacity && (addr - base) <= (capacity - size);
}

const HcommRegedBufferEntity* find_buffer(
    const HcommRegedBufferEntity* buffers, uint32_t bufferNum, uint64_t addr, uint64_t size, bool useFirstAsFallback)
{
    if (buffers == nullptr || bufferNum == 0) {
        return nullptr;
    }
    for (uint32_t index = 0; index < bufferNum; ++index) {
        if (buffers[index].type == HCOMM_REGED_BUFFER_RMA &&
            is_range_contained(buffers[index].bufferInfo.rma.addr, buffers[index].bufferInfo.rma.size, addr, size)) {
            return &buffers[index];
        }
    }
    if (useFirstAsFallback && buffers[0].type == HCOMM_REGED_BUFFER_RMA) {
        ASCCOMM_WARNING(
            "[%s] addr[0x%llx], size[0x%llx] is outside local buffers; use the first protection.", __func__, addr,
            size);
        return &buffers[0];
    }
    return nullptr;
}

AsccommResult get_rtsq(ThreadEntity& thread, HcommRtsqContext*& rtsq)
{
    CHK_PTR_NULL(thread.sqContextAddr);
    rtsq = thread.sqContextAddr;
    return ASCCOMM_SUCCESS;
}

AsccommResult get_ub_context(ChannelEntity& channel, HcommJfsContext*& jfs)
{
    CHK_PRT_RET(
        !is_ub_protocol(channel.protocol), ASCCOMM_ERROR("[%s] protocol[%d] is not UB.", __func__, channel.protocol),
        ASCCOMM_E_NOT_SUPPORT);
    CHK_PRT_RET(channel.sqNum == 0, ASCCOMM_ERROR("[%s] UB SQ context is empty.", __func__), ASCCOMM_E_PARA);
    CHK_PTR_NULL(channel.sqContextAddr);
    CHK_PRT_RET(
        channel.sqContextAddr[0].type != HCOMM_SQ_CONTEXT_TYPE_UB_JFS,
        ASCCOMM_ERROR("[%s] SQ context is not UB JFS.", __func__), ASCCOMM_E_PARA);
    jfs = &channel.sqContextAddr[0].contextInfo.ubJfs;
    CHK_PRT_RET(
        jfs->sqDepth == 0 || jfs->sqVa == 0 || jfs->maxReadSize == 0 || jfs->maxWriteSize == 0,
        ASCCOMM_ERROR("[%s] invalid UB JFS context.", __func__), ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

AsccommResult build_ub_slices(
    const ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, Asc::RmaBufSliceLite& local,
    Asc::RmtRmaBufSliceLite& remote)
{
    const HcommRegedBufferEntity* localBuffer =
        find_buffer(channel.localBufferAddr, channel.localBufferNum, localAddr, len, true);
    const HcommRegedBufferEntity* remoteBuffers =
        channel.remoteBufferAddr != nullptr && channel.remoteBufferNum > 1 ? channel.remoteBufferAddr + 1 : nullptr;
    const HcommRegedBufferEntity* remoteBuffer = find_buffer(
        remoteBuffers, channel.remoteBufferNum > 0 ? channel.remoteBufferNum - 1 : 0, remoteAddr, len, false);
    CHK_PTR_NULL(localBuffer);
    CHK_PTR_NULL(remoteBuffer);
    const auto& localResource = localBuffer->bufferInfo.rma;
    const auto& remoteResource = remoteBuffer->bufferInfo.rma;
    CHK_PRT_RET(
        localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
            remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        ASCCOMM_ERROR("[%s] UB buffer requires URMA protection.", __func__), ASCCOMM_E_PARA);
    local = Asc::RmaBufSliceLite(localAddr, len, 0, localResource.protectionInfo.memInfo.ub.tokenId);
    remote = Asc::RmtRmaBufSliceLite(
        remoteAddr, len, 0, remoteResource.protectionInfo.memInfo.ub.tokenId,
        remoteResource.protectionInfo.memInfo.ub.tokenValue, std::numeric_limits<uint32_t>::max());
    return ASCCOMM_SUCCESS;
}

AsccommResult build_ub_remote_notify(const ChannelEntity& channel, uint32_t index, Asc::RmtRmaBufSliceLite& notify)
{
    CHK_PRT_RET(
        index >= channel.remoteNotifyNum || channel.remoteNotifyAddr == nullptr,
        ASCCOMM_ERROR("[%s] remote notify index[%u] out of range[%u].", __func__, index, channel.remoteNotifyNum),
        ASCCOMM_E_PARA);
    CHK_PRT_RET(
        channel.remoteNotifyAddr[index].type != HCOMM_REGED_NOTIFY_RMA_RT,
        ASCCOMM_ERROR("[%s] remote notify is not RMA RT.", __func__), ASCCOMM_E_PARA);
    const auto& resource = channel.remoteNotifyAddr[index].notifyInfo.rmaRt;
    CHK_PRT_RET(
        resource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        ASCCOMM_ERROR("[%s] UB notify requires URMA protection.", __func__), ASCCOMM_E_PARA);
    notify = Asc::RmtRmaBufSliceLite(
        resource.addr, resource.size, 0, resource.protectionInfo.memInfo.ub.tokenId,
        resource.protectionInfo.memInfo.ub.tokenValue, resource.notifyId);
    return ASCCOMM_SUCCESS;
}

void fill_ub_param(const HcommJfsContext& jfs, Asc::UbConnLiteParam& param)
{
    param.jettyId = jfs.jfsID;
    param.sqVa = jfs.sqVa;
    param.sqDepth = jfs.sqDepth;
    param.tpn = jfs.tpID;
    param.maxReadSize = jfs.maxReadSize;
    param.maxWriteSize = jfs.maxWriteSize;
    (void)memcpy_sp(param.rmtEid, sizeof(param.rmtEid), jfs.remoteEID, sizeof(jfs.remoteEID));
}

void import_ub_state(const HcommJfsContext& jfs, Asc::UbConnLite& connection)
{
    uint32_t sqHead = jfs.sqHead;
    uint32_t sqTail = jfs.sqTail;
    connection.import_queue_state(sqHead, sqTail);
}

void export_ub_state(Asc::UbConnLite& connection, HcommJfsContext& jfs)
{
    uint32_t sqHead = 0;
    uint32_t sqTail = 0;
    connection.export_queue_state(sqHead, sqTail);
    jfs.sqHead = sqHead;
    jfs.sqTail = sqTail;
}

AsccommResult ring_ub_doorbell(
    ThreadEntity& thread, const ChannelEntity& channel, const HcommJfsContext& jfs, uint16_t pi)
{
    HcommRtsqContext* rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    const Asc::UbJettyLiteId jettyId(channel.devInfo.dieId, channel.devInfo.funcId, jfs.jfsID);
    return asccomm_rtsq_ub_db_send(*rtsq, jettyId, pi);
}

Asc::SqeConfigLite get_ub_config(ChannelEntity& channel)
{
    Asc::SqeConfigLite cfg;
    if (channel.sqNum > 0 && channel.sqContextAddr != nullptr && channel.sqContextAddr[0].fence != 0) {
        cfg.fence = 1;
        cfg.placeOdr = UB_STRONG_ORDER;
        cfg.compOrder = UB_COMPLETION;
    }
    if (channel.sqNum > 0 && channel.sqContextAddr != nullptr) {
        channel.sqContextAddr[0].fence = 0;
    }
    return cfg;
}

template <typename Operation>
AsccommResult execute_ub_rma(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    Operation operation)
{
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::RmaBufSliceLite local(0, 0, 0, 0);
    Asc::RmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(build_ub_slices(channel, localAddr, remoteAddr, len, local, remote));

    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    Asc::ConnLiteOperationOut out{};
    Asc::SqeConfigLite cfg = get_ub_config(channel);
    operation(connection, local, remote, cfg, out);
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, channel, *jfs, out.pi);
}

AsccommResult get_rdma_contexts(ChannelEntity& channel, HcommRdmaSqContext*& sq, HcommRdmaCqContext*& cq)
{
    CHK_PRT_RET(
        channel.protocol != COMM_PROTOCOL_ROCE,
        ASCCOMM_ERROR("[%s] protocol[%d] is not RoCE.", __func__, channel.protocol), ASCCOMM_E_NOT_SUPPORT);
    CHK_PRT_RET(
        channel.sqNum == 0 || channel.cqNum == 0, ASCCOMM_ERROR("[%s] RoCE SQ/CQ context is empty.", __func__),
        ASCCOMM_E_PARA);
    CHK_PTR_NULL(channel.sqContextAddr);
    CHK_PTR_NULL(channel.cqContextAddr);
    CHK_PRT_RET(
        channel.sqContextAddr[0].type != HCOMM_SQ_CONTEXT_TYPE_ROCE ||
            channel.cqContextAddr[0].type != HCOMM_CQ_CONTEXT_TYPE_ROCE,
        ASCCOMM_ERROR("[%s] SQ/CQ context is not RoCE.", __func__), ASCCOMM_E_PARA);
    sq = &channel.sqContextAddr[0].contextInfo.roceSq;
    cq = &channel.cqContextAddr[0].contextInfo.roceCq;
    CHK_PRT_RET(
        sq->depth == 0 || sq->sqVa == 0 || cq->cqDepth == 0 || cq->cqVa == 0,
        ASCCOMM_ERROR("[%s] invalid RoCE SQ/CQ context.", __func__), ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

void build_rdma_context(
    const HcommRdmaSqContext& resourceSq, const HcommRdmaCqContext& resourceCq, Asc::RdmaSqContextLite& sq,
    Asc::RdmaCqContextLite& cq)
{
    sq.qpn = resourceSq.qpn;
    sq.sqVa = resourceSq.sqVa;
    sq.wqeSize = resourceSq.wqeSize;
    sq.depth = resourceSq.depth;
    sq.dbHwVa = resourceSq.dbHwVa;
    sq.dbSwVa = resourceSq.dbSwVa;
    sq.dbVendorSpecified = resourceSq.dbVendorSpecified;
    cq.cqVa = resourceCq.cqVa;
    cq.cqDepth = resourceCq.cqDepth;
    cq.dbSwVa = resourceCq.dbSwVa;
}

AsccommResult build_rdma_slices(
    const ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, Asc::RmaBufSliceLite& local,
    Asc::RmtRmaBufSliceLite& remote)
{
    const HcommRegedBufferEntity* localBuffer =
        find_buffer(channel.localBufferAddr, channel.localBufferNum, localAddr, len, true);
    const HcommRegedBufferEntity* remoteBuffers =
        channel.remoteBufferAddr != nullptr && channel.remoteBufferNum > 1 ? channel.remoteBufferAddr + 1 : nullptr;
    const HcommRegedBufferEntity* remoteBuffer = find_buffer(
        remoteBuffers, channel.remoteBufferNum > 0 ? channel.remoteBufferNum - 1 : 0, remoteAddr, len, false);
    CHK_PTR_NULL(localBuffer);
    CHK_PTR_NULL(remoteBuffer);
    const auto& localResource = localBuffer->bufferInfo.rma;
    const auto& remoteResource = remoteBuffer->bufferInfo.rma;
    CHK_PRT_RET(
        localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA ||
            remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA,
        ASCCOMM_ERROR("[%s] RoCE buffer requires RDMA protection.", __func__), ASCCOMM_E_PARA);
    local = Asc::RmaBufSliceLite(localAddr, len, localResource.protectionInfo.memInfo.roce.lkey, 0);
    remote = Asc::RmtRmaBufSliceLite(
        remoteAddr, len, remoteResource.protectionInfo.memInfo.roce.rkey, 0, 0, std::numeric_limits<uint32_t>::max());
    return ASCCOMM_SUCCESS;
}

AsccommResult build_rdma_notify_slices(
    const ChannelEntity& channel, uint32_t index, Asc::RmaBufSliceLite& local, Asc::RmtRmaBufSliceLite& remote)
{
    CHK_PRT_RET(
        index >= channel.remoteNotifyNum || channel.remoteNotifyAddr == nullptr,
        ASCCOMM_ERROR("[%s] remote notify index[%u] out of range[%u].", __func__, index, channel.remoteNotifyNum),
        ASCCOMM_E_PARA);
    const HcommRegedBufferEntity* notifyValueBuffer =
        channel.localBufferAddr != nullptr && channel.localBufferNum > 0 ? &channel.localBufferAddr[0] : nullptr;
    CHK_PTR_NULL(notifyValueBuffer);
    CHK_PRT_RET(
        notifyValueBuffer->type != HCOMM_REGED_BUFFER_RMA ||
            channel.remoteNotifyAddr[index].type != HCOMM_REGED_NOTIFY_RMA_RT,
        ASCCOMM_ERROR("[%s] RoCE notify resource type is invalid.", __func__), ASCCOMM_E_PARA);
    const auto& localResource = notifyValueBuffer->bufferInfo.rma;
    const auto& remoteResource = channel.remoteNotifyAddr[index].notifyInfo.rmaRt;
    CHK_PRT_RET(
        localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA ||
            remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA,
        ASCCOMM_ERROR("[%s] RoCE notify requires RDMA protection.", __func__), ASCCOMM_E_PARA);
    local =
        Asc::RmaBufSliceLite(localResource.addr, localResource.size, localResource.protectionInfo.memInfo.roce.lkey, 0);
    remote = Asc::RmtRmaBufSliceLite(
        remoteResource.addr, remoteResource.size, remoteResource.protectionInfo.memInfo.roce.rkey, 0, 0,
        remoteResource.notifyId);
    return ASCCOMM_SUCCESS;
}

template <typename Operation>
AsccommResult for_each_rdma_slice(
    const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote, Operation operation)
{
    uint64_t offset = 0;
    while (offset < local.get_size()) {
        const uint64_t remaining = local.get_size() - offset;
        const uint32_t sliceSize = static_cast<uint32_t>(remaining > RDMA_DMA_MAX_SIZE ? RDMA_DMA_MAX_SIZE : remaining);
        Asc::RmaBufSliceLite localSlice(local.get_addr() + offset, sliceSize, local.get_lkey(), 0);
        Asc::RmtRmaBufSliceLite remoteSlice(
            remote.get_addr() + offset, sliceSize, remote.get_rkey(), 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(operation(localSlice, remoteSlice));
        offset += sliceSize;
    }
    return ASCCOMM_SUCCESS;
}

enum class RdmaOperation {
    READ,
    WRITE,
    WRITE_REDUCE,
    WRITE_WITH_NOTIFY,
    WRITE_REDUCE_WITH_NOTIFY,
    NOTIFY_RECORD,
};

AsccommResult execute_rdma(
    ThreadEntity& thread, ChannelEntity& channel, RdmaOperation operation, uintptr_t localAddr, uintptr_t remoteAddr,
    uint64_t len, const Asc::ReduceIn* reduceIn, uint32_t notifyIndex)
{
    HcommRdmaSqContext* resourceSq = nullptr;
    HcommRdmaCqContext* resourceCq = nullptr;
    CHK_RET(get_rdma_contexts(channel, resourceSq, resourceCq));
    Asc::RdmaSqContextLite sq{};
    Asc::RdmaCqContextLite cq{};
    build_rdma_context(*resourceSq, *resourceCq, sq, cq);
    Asc::Rdma1825Ops ops(&sq, &cq);
    ops.import_queue_state(
        resourceSq->sqHead, resourceSq->sqTail, resourceCq->cqHead, resourceCq->cqTail, resourceCq->cqDbFlush != 0);

    Asc::RmaBufSliceLite local(0, 0, 0, 0);
    Asc::RmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    if (operation == RdmaOperation::NOTIFY_RECORD) {
        CHK_RET(build_rdma_notify_slices(channel, notifyIndex, local, remote));
    } else {
        CHK_RET(build_rdma_slices(channel, localAddr, remoteAddr, len, local, remote));
    }
    Asc::RmaBufSliceLite localNotify(0, 0, 0, 0);
    Asc::RmtRmaBufSliceLite remoteNotify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    if (operation == RdmaOperation::WRITE_WITH_NOTIFY || operation == RdmaOperation::WRITE_REDUCE_WITH_NOTIFY) {
        CHK_RET(build_rdma_notify_slices(channel, notifyIndex, localNotify, remoteNotify));
    }

    Asc::SqeConfigLite cfg;
    cfg.cqeEn = true;
    cfg.fence = channel.sqNum > 0 && channel.sqContextAddr != nullptr && channel.sqContextAddr[0].fence != 0 ? 1 : 0;
    if (channel.sqNum > 0 && channel.sqContextAddr != nullptr) {
        channel.sqContextAddr[0].fence = 0;
    }
    if (operation == RdmaOperation::READ) {
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.read(loc, rmt, cfg);
            }));
    } else if (operation == RdmaOperation::WRITE || operation == RdmaOperation::NOTIFY_RECORD) {
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.write(loc, rmt, cfg);
            }));
    } else if (operation == RdmaOperation::WRITE_REDUCE) {
        CHK_PTR_NULL(reduceIn);
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.write_reduce(loc, rmt, cfg, reduceIn->dataType, reduceIn->reduceOp);
            }));
    } else if (operation == RdmaOperation::WRITE_WITH_NOTIFY) {
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.write(loc, rmt, cfg);
            }));
        CHK_RET(ops.write(localNotify, remoteNotify, cfg));
    } else {
        CHK_PTR_NULL(reduceIn);
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.write_reduce(loc, rmt, cfg, reduceIn->dataType, reduceIn->reduceOp);
            }));
        CHK_RET(ops.write(localNotify, remoteNotify, cfg));
    }

    uint64_t dbAddr = 0;
    uint64_t dbValue = 0;
    CHK_RET(ops.build_doorbell(dbAddr, dbValue));
    bool cqDbFlush = false;
    ops.export_queue_state(resourceSq->sqHead, resourceSq->sqTail, resourceCq->cqHead, resourceCq->cqTail, cqDbFlush);
    resourceCq->cqDbFlush = cqDbFlush ? 1 : 0;
    HcommRtsqContext* rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    CHK_RET(asccomm_rtsq_rdma_db_send(*rtsq, dbAddr, dbValue));

    const int32_t pollNum =
        (operation == RdmaOperation::WRITE_WITH_NOTIFY || operation == RdmaOperation::WRITE_REDUCE_WITH_NOTIFY) ? 2 : 1;
    std::vector<int32_t> errList;
    AsccommResult pollRet = ops.poll_cq(pollNum, 5, errList);
    (void)ops.build_cq_doorbell(dbAddr, dbValue);
    ops.export_queue_state(resourceSq->sqHead, resourceSq->sqTail, resourceCq->cqHead, resourceCq->cqTail, cqDbFlush);
    resourceCq->cqDbFlush = cqDbFlush ? 1 : 0;
    return pollRet;
}

AsccommResult convert_batch_reduce(
    AsccommDataType dataType, AsccommReduceOp reduceOp, Asc::ReduceIn& reduceIn, uint64_t count, uint64_t& len)
{
    Asc::DataType innerType = Asc::DataType::INVALID;
    uint32_t typeSize = 0;
    switch (dataType) {
        case ASCCOMM_DATA_TYPE_INT8:
            innerType = Asc::DataType::INT8;
            typeSize = 1;
            break;
        case ASCCOMM_DATA_TYPE_INT16:
            innerType = Asc::DataType::INT16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_INT32:
            innerType = Asc::DataType::INT32;
            typeSize = 4;
            break;
        case ASCCOMM_DATA_TYPE_FP16:
            innerType = Asc::DataType::FP16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_FP32:
            innerType = Asc::DataType::FP32;
            typeSize = 4;
            break;
        case ASCCOMM_DATA_TYPE_INT64:
            innerType = Asc::DataType::INT64;
            typeSize = 8;
            break;
        case ASCCOMM_DATA_TYPE_UINT64:
            innerType = Asc::DataType::UINT64;
            typeSize = 8;
            break;
        case ASCCOMM_DATA_TYPE_UINT8:
            innerType = Asc::DataType::UINT8;
            typeSize = 1;
            break;
        case ASCCOMM_DATA_TYPE_UINT16:
            innerType = Asc::DataType::UINT16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_UINT32:
            innerType = Asc::DataType::UINT32;
            typeSize = 4;
            break;
        case ASCCOMM_DATA_TYPE_FP64:
            innerType = Asc::DataType::FP64;
            typeSize = 8;
            break;
        case ASCCOMM_DATA_TYPE_BFP16:
            innerType = Asc::DataType::BFP16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_INT128:
            innerType = Asc::DataType::INT128;
            typeSize = 16;
            break;
        default:
            return ASCCOMM_E_PARA;
    }
    Asc::ReduceOp innerOp = Asc::ReduceOp::INVALID;
    switch (reduceOp) {
        case ASCCOMM_REDUCE_SUM:
            innerOp = Asc::ReduceOp::SUM;
            break;
        case ASCCOMM_REDUCE_PROD:
            innerOp = Asc::ReduceOp::PROD;
            break;
        case ASCCOMM_REDUCE_MAX:
            innerOp = Asc::ReduceOp::MAX;
            break;
        case ASCCOMM_REDUCE_MIN:
            innerOp = Asc::ReduceOp::MIN;
            break;
        default:
            return ASCCOMM_E_PARA;
    }
    CHK_PRT_RET(
        count > std::numeric_limits<uint64_t>::max() / typeSize,
        ASCCOMM_ERROR("[%s] reduce length overflow.", __func__), ASCCOMM_E_PARA);
    len = count * typeSize;
    reduceIn = Asc::ReduceIn(innerType, innerOp);
    return ASCCOMM_SUCCESS;
}

} // namespace

AsccommResult asccomm_write(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [](Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
               const Asc::SqeConfigLite& cfg,
               Asc::ConnLiteOperationOut& out) { connection.write(local, remote, cfg, out); });
    }
    return execute_rdma(thread, channel, RdmaOperation::WRITE, localAddr, remoteAddr, len, nullptr, 0);
}

AsccommResult asccomm_write_reduce(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [&reduceIn](
                Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
                const Asc::SqeConfigLite& cfg, Asc::ConnLiteOperationOut& out) {
                connection.write_reduce(reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out);
            });
    }
    return execute_rdma(thread, channel, RdmaOperation::WRITE_REDUCE, localAddr, remoteAddr, len, &reduceIn, 0);
}

AsccommResult asccomm_write_with_notify(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    uint32_t remoteNotifyIdx)
{
    if (is_ub_protocol(channel.protocol)) {
        Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(build_ub_remote_notify(channel, remoteNotifyIdx, notify));
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [&notify](
                Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
                const Asc::SqeConfigLite& cfg, Asc::ConnLiteOperationOut& out) {
                connection.write_with_notify(local, remote, cfg, out, notify, NOTIFY_VALUE);
            });
    }
    return execute_rdma(
        thread, channel, RdmaOperation::WRITE_WITH_NOTIFY, localAddr, remoteAddr, len, nullptr, remoteNotifyIdx);
}

AsccommResult asccomm_write_reduce_with_notify(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn, uint32_t remoteNotifyIdx)
{
    if (is_ub_protocol(channel.protocol)) {
        Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(build_ub_remote_notify(channel, remoteNotifyIdx, notify));
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [&reduceIn, &notify](
                Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
                const Asc::SqeConfigLite& cfg, Asc::ConnLiteOperationOut& out) {
                connection.write_reduce_with_notify(
                    reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out, notify, NOTIFY_VALUE);
            });
    }
    return execute_rdma(
        thread, channel, RdmaOperation::WRITE_REDUCE_WITH_NOTIFY, localAddr, remoteAddr, len, &reduceIn,
        remoteNotifyIdx);
}

AsccommResult asccomm_read(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [](Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
               const Asc::SqeConfigLite& cfg,
               Asc::ConnLiteOperationOut& out) { connection.read(local, remote, cfg, out); });
    }
    return execute_rdma(thread, channel, RdmaOperation::READ, localAddr, remoteAddr, len, nullptr, 0);
}

AsccommResult asccomm_read_reduce(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [&reduceIn](
                Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
                const Asc::SqeConfigLite& cfg,
                Asc::ConnLiteOperationOut& out) { connection.read_reduce(reduceIn, local, remote, cfg, out); });
    }
    // The original RoCE transport does not override ReadReduce and therefore performs no operation.
    return channel.protocol == COMM_PROTOCOL_ROCE ? ASCCOMM_SUCCESS : ASCCOMM_E_NOT_SUPPORT;
}

AsccommResult asccomm_notify_record(ThreadEntity& thread, ChannelEntity& channel, uint32_t remoteNotifyIdx)
{
    if (!is_ub_protocol(channel.protocol)) {
        return execute_rdma(thread, channel, RdmaOperation::NOTIFY_RECORD, 0, 0, 0, nullptr, remoteNotifyIdx);
    }
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(build_ub_remote_notify(channel, remoteNotifyIdx, notify));
    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    Asc::SqeConfigLite cfg;
    if (remoteNotifyIdx == 1) {
        cfg.cqeEn = true;
        cfg.placeOdr = UB_STRONG_ORDER;
        cfg.compOrder = UB_COMPLETION;
    }
    Asc::ConnLiteOperationOut out{};
    const uint32_t value = NOTIFY_VALUE;
    connection.inline_write(reinterpret_cast<const uint8_t*>(&value), NOTIFY_SIZE, notify, cfg, out);
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, channel, *jfs, out.pi);
}

AsccommResult asccomm_notify_wait(
    ThreadEntity& thread, const ChannelEntity& channel, uint32_t localNotifyIdx, uint32_t timeout)
{
    CHK_PRT_RET(
        channel.localNotifyAddr == nullptr || channel.localNotifyNum <= 1U ||
            localNotifyIdx >= channel.localNotifyNum - 1U,
        ASCCOMM_ERROR(
            "[%s] local notify index[%u] out of range[%u].", __func__, localNotifyIdx,
            channel.localNotifyNum > 0 ? channel.localNotifyNum - 1U : 0U),
        ASCCOMM_E_PARA);
    HcommRtsqContext* rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    const HcommRegedNotifyEntity& notify = channel.localNotifyAddr[localNotifyIdx + 1U];
    CHK_PRT_RET(
        notify.type != HCOMM_REGED_NOTIFY_RMA_RT, ASCCOMM_ERROR("[%s] local notify is not RMA RT.", __func__),
        ASCCOMM_E_PARA);
    const int32_t notifyId = notify.notifyInfo.rmaRt.notifyId;
    CHK_PRT_RET(notifyId < 0, ASCCOMM_ERROR("[%s] invalid notify id[%d].", __func__, notifyId), ASCCOMM_E_PARA);
    return asccomm_rtsq_notify_wait(*rtsq, static_cast<uint32_t>(notifyId), timeout);
}

AsccommResult asccomm_fence(ChannelEntity& channel)
{
    CHK_PRT_RET(
        channel.sqNum == 0 || channel.sqContextAddr == nullptr, ASCCOMM_ERROR("[%s] SQ context is empty.", __func__),
        ASCCOMM_E_PARA);
    channel.sqContextAddr[0].fence = 1;
    return ASCCOMM_SUCCESS;
}

AsccommResult asccomm_drain(ThreadEntity& thread, ChannelEntity& channel)
{
    if (channel.protocol == COMM_PROTOCOL_ROCE) {
        return ASCCOMM_SUCCESS;
    }
    CHK_PRT_RET(
        !is_ub_protocol(channel.protocol),
        ASCCOMM_ERROR("[%s] protocol[%d] is not supported.", __func__, channel.protocol), ASCCOMM_E_NOT_SUPPORT);
    if (channel.localNotifyNum == 0 || channel.remoteBufferNum == 0 || channel.localNotifyAddr == nullptr ||
        channel.remoteBufferAddr == nullptr) {
        ASCCOMM_WARNING("[%s] drain resource is empty; skip.", __func__);
        return ASCCOMM_SUCCESS;
    }
    const HcommRegedNotifyEntity* localDrainNotify = &channel.localNotifyAddr[0];
    const HcommRegedBufferEntity* remoteDrainBuffer = &channel.remoteBufferAddr[0];
    if (localDrainNotify->type != HCOMM_REGED_NOTIFY_RMA_RT || remoteDrainBuffer->type != HCOMM_REGED_BUFFER_RMA) {
        ASCCOMM_WARNING("[%s] drain resource is empty; skip.", __func__);
        return ASCCOMM_SUCCESS;
    }
    if (localDrainNotify->notifyInfo.rmaRt.size == 0 || remoteDrainBuffer->bufferInfo.rma.size == 0) {
        ASCCOMM_WARNING("[%s] drain resource is empty; skip.", __func__);
        return ASCCOMM_SUCCESS;
    }
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    const auto& localResource = localDrainNotify->notifyInfo.rmaRt;
    const auto& remoteResource = remoteDrainBuffer->bufferInfo.rma;
    CHK_PRT_RET(
        localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
            remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        ASCCOMM_ERROR("[%s] UB drain requires URMA protection.", __func__), ASCCOMM_E_PARA);
    Asc::RmaBufSliceLite local(
        localResource.addr, localResource.size, 0, localResource.protectionInfo.memInfo.ub.tokenId);
    Asc::RmtRmaBufSliceLite remote(
        remoteResource.addr, remoteResource.size, 0, remoteResource.protectionInfo.memInfo.ub.tokenId,
        remoteResource.protectionInfo.memInfo.ub.tokenValue, std::numeric_limits<uint32_t>::max());
    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    Asc::SqeConfigLite cfg;
    cfg.fence = 1;
    cfg.placeOdr = UB_STRONG_ORDER;
    cfg.compOrder = UB_COMPLETION;
    if (channel.sqNum > 0 && channel.sqContextAddr != nullptr) {
        channel.sqContextAddr[0].fence = 0;
    }
    Asc::ConnLiteOperationOut out{};
    connection.read(local, remote, cfg, out);
    export_ub_state(connection, *jfs);
    CHK_RET(ring_ub_doorbell(thread, channel, *jfs, out.pi));
    HcommRtsqContext* rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    CHK_PRT_RET(
        localResource.notifyId < 0,
        ASCCOMM_ERROR("[%s] invalid drain notify id[%d].", __func__, localResource.notifyId), ASCCOMM_E_PARA);
    return asccomm_rtsq_notify_wait(*rtsq, static_cast<uint32_t>(localResource.notifyId), DEFAULT_TIMEOUT);
}

AsccommResult asccomm_batch_transfer(
    ThreadEntity& thread, ChannelEntity& channel, const AsccommBatchTransferDesc* transferDescs,
    uint32_t transferDescNum)
{
    CHK_PTR_NULL(transferDescs);
    CHK_PRT_RET(transferDescNum == 0, ASCCOMM_ERROR("[%s] transferDescNum is 0.", __func__), ASCCOMM_E_PARA);
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    Asc::SqeConfigLite cfg = get_ub_config(channel);
    Asc::ConnLiteOperationOut out{};

    for (uint32_t index = 0; index < transferDescNum; ++index) {
        const AsccommBatchTransferDesc& desc = transferDescs[index];
        cfg.cqeEn = index == transferDescNum - 1;
        cfg.placeOdr = cfg.cqeEn ? UB_STRONG_ORDER : UB_RELAX_ORDER;
        cfg.compOrder = cfg.cqeEn ? UB_COMPLETION : UB_NO_COMPLETION;
        if (desc.transType == ASCCOMM_TRANSFER_TYPE_NOTIFY_RECORD) {
            Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
            const uint32_t notifyIdx = desc.transferInfo.notifyRecord.notifyIdx;
            CHK_RET(build_ub_remote_notify(channel, notifyIdx, notify));
            if (notifyIdx == 1) {
                cfg.cqeEn = true;
                cfg.placeOdr = UB_STRONG_ORDER;
                cfg.compOrder = UB_COMPLETION;
            }
            const uint32_t value = NOTIFY_VALUE;
            connection.inline_write(reinterpret_cast<const uint8_t*>(&value), NOTIFY_SIZE, notify, cfg, out);
            continue;
        }

        uintptr_t localAddr = 0;
        uintptr_t remoteAddr = 0;
        uint64_t len = 0;
        uint32_t notifyIdx = 0;
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.write.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.write.dst);
            len = desc.transferInfo.write.len;
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_READ) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.read.dst);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.read.src);
            len = desc.transferInfo.read.len;
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeWithNotify.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeWithNotify.dst);
            len = desc.transferInfo.writeWithNotify.len;
            notifyIdx = desc.transferInfo.writeWithNotify.notifyIdx;
        } else if (
            desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE ||
            desc.transType == ASCCOMM_TRANSFER_TYPE_READ_REDUCE) {
            localAddr = desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE ?
                            reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.src) :
                            reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.dst);
            remoteAddr = desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE ?
                             reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.dst) :
                             reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.src);
            CHK_RET(convert_batch_reduce(
                desc.transferInfo.reduce.dataType, desc.transferInfo.reduce.reduceOp, reduceIn,
                desc.transferInfo.reduce.count, len));
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeReduceWithNotify.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeReduceWithNotify.dst);
            notifyIdx = desc.transferInfo.writeReduceWithNotify.notifyIdx;
            CHK_RET(convert_batch_reduce(
                desc.transferInfo.writeReduceWithNotify.dataType, desc.transferInfo.writeReduceWithNotify.reduceOp,
                reduceIn, desc.transferInfo.writeReduceWithNotify.count, len));
        } else {
            ASCCOMM_ERROR("[%s] unsupported transfer type[%d].", __func__, desc.transType);
            return ASCCOMM_E_PARA;
        }
        CHK_PRT_RET(
            localAddr == 0 || remoteAddr == 0, ASCCOMM_ERROR("[%s] null address at batch index[%u].", __func__, index),
            ASCCOMM_E_PTR);
        Asc::RmaBufSliceLite local(0, 0, 0, 0);
        Asc::RmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(build_ub_slices(channel, localAddr, remoteAddr, len, local, remote));
        if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE) {
            connection.write(local, remote, cfg, out);
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_READ) {
            connection.read(local, remote, cfg, out);
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE) {
            connection.write_reduce(reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out);
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_READ_REDUCE) {
            connection.read_reduce(reduceIn, local, remote, cfg, out);
        } else {
            Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
            CHK_RET(build_ub_remote_notify(channel, notifyIdx, notify));
            if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY) {
                connection.write_with_notify(local, remote, cfg, out, notify, NOTIFY_VALUE);
            } else {
                connection.write_reduce_with_notify(
                    reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out, notify, NOTIFY_VALUE);
            }
        }
    }
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, channel, *jfs, out.pi);
}
