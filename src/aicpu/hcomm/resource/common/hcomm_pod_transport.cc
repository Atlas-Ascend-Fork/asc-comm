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
#include "hcomm_pod_transport.h"
#include "hcomm_pod_runtime.h"
#include "../connection/ub_conn_lite.h"
#include "../connection/ub_jetty_lite.h"
#include "../connection/rdma_vendor_1825_ops.h"
#include "securec.h"
#include "log/hcomm_log.h"

namespace {

constexpr uint32_t UB_STRONG_ORDER = 2U;
constexpr uint32_t UB_RELAX_ORDER = 1U;
constexpr uint32_t UB_COMPLETION = 1U;
constexpr uint32_t UB_NO_COMPLETION = 0U;
constexpr uint32_t POD_NOTIFY_VALUE = 1U;
constexpr uint32_t POD_NOTIFY_SIZE = sizeof(uint32_t);
constexpr uint32_t POD_DEFAULT_TIMEOUT = 1836U;
constexpr uint64_t RDMA_DMA_MAX_SIZE = 0x80000000ULL;

bool is_ub_protocol(CommProtocol protocol)
{
    return protocol == COMM_PROTOCOL_UBC_CTP || protocol == COMM_PROTOCOL_UBC_TP ||
        protocol == COMM_PROTOCOL_UB_MEM || protocol == COMM_PROTOCOL_UBOE || protocol == COMM_PROTOCOL_UBG;
}

bool is_range_contained(uint64_t base, uint64_t capacity, uint64_t addr, uint64_t size)
{
    return addr >= base && size <= capacity && (addr - base) <= (capacity - size);
}

const HcommRegedBufferEntity *find_buffer(const HcommRegedBufferEntity *buffers, uint32_t bufferNum,
                                        uint64_t addr, uint64_t size, bool useFirstAsFallback)
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
        HCCL_WARNING("[%s] addr[0x%llx], size[0x%llx] is outside local buffers; use the first protection.",
            __func__, addr, size);
        return &buffers[0];
    }
    return nullptr;
}

HcclResult get_rtsq(HcommThreadEntity &thread, HcommRtsqContext *&rtsq)
{
    CHK_PTR_NULL(thread.sqContextAddr);
    rtsq = thread.sqContextAddr;
    return HCCL_SUCCESS;
}

HcclResult get_ub_context(HcommChannelEntity &channel, HcommJfsContext *&jfs)
{
    CHK_PRT_RET(!is_ub_protocol(channel.protocol),
        HCCL_ERROR("[%s] protocol[%d] is not UB.", __func__, channel.protocol), HCCL_E_NOT_SUPPORT);
    CHK_PRT_RET(channel.sqNum == 0, HCCL_ERROR("[%s] UB SQ context is empty.", __func__), HCCL_E_PARA);
    CHK_PTR_NULL(channel.sqContextAddr);
    CHK_PRT_RET(channel.sqContextAddr[0].type != HCOMM_SQ_CONTEXT_TYPE_UB_JFS,
        HCCL_ERROR("[%s] SQ context is not UB JFS.", __func__), HCCL_E_PARA);
    jfs = &channel.sqContextAddr[0].contextInfo.ubJfs;
    CHK_PRT_RET(jfs->sqDepth == 0 || jfs->sqVa == 0 || jfs->maxReadSize == 0 || jfs->maxWriteSize == 0,
        HCCL_ERROR("[%s] invalid UB JFS context.", __func__), HCCL_E_PARA);
    return HCCL_SUCCESS;
}

HcclResult build_ub_slices(const HcommChannelEntity &channel, uintptr_t localAddr, uintptr_t remoteAddr,
                         uint64_t len, Asc::PodRmaBufSliceLite &local, Asc::PodRmtRmaBufSliceLite &remote)
{
    const HcommRegedBufferEntity *localBuffer = find_buffer(
        channel.localBufferAddr, channel.localBufferNum, localAddr, len, true);
    const HcommRegedBufferEntity *remoteBuffer = find_buffer(
        channel.remoteBufferAddr, channel.remoteBufferNum, remoteAddr, len, false);
    CHK_PTR_NULL(localBuffer);
    CHK_PTR_NULL(remoteBuffer);
    const auto &localResource = localBuffer->bufferInfo.rma;
    const auto &remoteResource = remoteBuffer->bufferInfo.rma;
    CHK_PRT_RET(localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
        remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        HCCL_ERROR("[%s] UB buffer requires URMA protection.", __func__), HCCL_E_PARA);
    local = Asc::PodRmaBufSliceLite(localAddr, len, 0, localResource.protectionInfo.memInfo.ub.tokenId);
    remote = Asc::PodRmtRmaBufSliceLite(remoteAddr, len, 0,
        remoteResource.protectionInfo.memInfo.ub.tokenId, remoteResource.protectionInfo.memInfo.ub.tokenValue,
        std::numeric_limits<uint32_t>::max());
    return HCCL_SUCCESS;
}

HcclResult build_ub_remote_notify(const HcommChannelEntity &channel, uint32_t index,
                               Asc::PodRmtRmaBufSliceLite &notify)
{
    CHK_PRT_RET(index >= channel.remoteNotifyNum || channel.remoteNotifyAddr == nullptr,
        HCCL_ERROR("[%s] remote notify index[%u] out of range[%u].", __func__, index,
            channel.remoteNotifyNum), HCCL_E_PARA);
    CHK_PRT_RET(channel.remoteNotifyAddr[index].type != HCOMM_REGED_NOTIFY_RMA_RT,
        HCCL_ERROR("[%s] remote notify is not RMA RT.", __func__), HCCL_E_PARA);
    const auto &resource = channel.remoteNotifyAddr[index].notifyInfo.rmaRt;
    CHK_PRT_RET(resource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        HCCL_ERROR("[%s] UB notify requires URMA protection.", __func__), HCCL_E_PARA);
    notify = Asc::PodRmtRmaBufSliceLite(resource.addr, resource.size, 0,
        resource.protectionInfo.memInfo.ub.tokenId,
        resource.protectionInfo.memInfo.ub.tokenValue, resource.notifyId);
    return HCCL_SUCCESS;
}

void fill_ub_param(const HcommJfsContext &jfs, Asc::PodUbConnLiteParam &param)
{
    param.dieId = jfs.dieId;
    param.funcId = jfs.funcId;
    param.jettyId = jfs.jfsID;
    param.sqVa = jfs.sqVa;
    param.sqDepth = jfs.sqDepth;
    param.tpn = jfs.tpID;
    param.dwqeCacheLocked = jfs.dwqeCacheLocked != 0;
    param.maxReadSize = jfs.maxReadSize;
    param.maxWriteSize = jfs.maxWriteSize;
    (void)memcpy_sp(param.rmtEid, sizeof(param.rmtEid), jfs.remoteEID, sizeof(jfs.remoteEID));
}

void import_ub_state(const HcommJfsContext &jfs, Asc::PodUbConnLite &connection)
{
    connection.import_queue_state(jfs.pi, jfs.piDetourCount);
}

void export_ub_state(Asc::PodUbConnLite &connection, HcommJfsContext &jfs)
{
    connection.export_queue_state(jfs.pi, jfs.piDetourCount);
}

HcclResult ring_ub_doorbell(HcommThreadEntity &thread, const HcommJfsContext &jfs, uint16_t pi)
{
    HcommRtsqContext *rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    const Asc::PodUbJettyLiteId jettyId(jfs.dieId, jfs.funcId, jfs.jfsID);
    return hcomm_pod_rtsq_ub_db_send(*rtsq, jettyId, pi);
}

Asc::PodSqeConfigLite get_ub_config(HcommChannelEntity &channel)
{
    Asc::PodSqeConfigLite cfg;
    if (channel.fence != 0) {
        cfg.fence = 1;
        cfg.placeOdr = UB_STRONG_ORDER;
        cfg.compOrder = UB_COMPLETION;
    }
    channel.fence = 0;
    return cfg;
}

template <typename Operation>
HcclResult execute_ub_rma(HcommThreadEntity &thread, HcommChannelEntity &channel,
                        uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, Operation operation)
{
    HcommJfsContext *jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::PodRmaBufSliceLite local(0, 0, 0, 0);
    Asc::PodRmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(build_ub_slices(channel, localAddr, remoteAddr, len, local, remote));

    Asc::PodUbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::PodUbConnLite connection;
    connection.import_pod_context(param);
    import_ub_state(*jfs, connection);
    Asc::PodConnLiteOperationOut out{};
    Asc::PodSqeConfigLite cfg = get_ub_config(channel);
    operation(connection, local, remote, cfg, out);
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, *jfs, out.pi);
}

HcclResult get_rdma_contexts(HcommChannelEntity &channel, HcommRdmaSqContext *&sq,
                           HcommRdmaCqContext *&cq)
{
    CHK_PRT_RET(channel.protocol != COMM_PROTOCOL_ROCE,
        HCCL_ERROR("[%s] protocol[%d] is not RoCE.", __func__, channel.protocol), HCCL_E_NOT_SUPPORT);
    CHK_PRT_RET(channel.sqNum == 0 || channel.cqNum == 0,
        HCCL_ERROR("[%s] RoCE SQ/CQ context is empty.", __func__), HCCL_E_PARA);
    CHK_PTR_NULL(channel.sqContextAddr);
    CHK_PTR_NULL(channel.cqContextAddr);
    CHK_PRT_RET(channel.sqContextAddr[0].type != HCOMM_SQ_CONTEXT_TYPE_ROCE ||
        channel.cqContextAddr[0].type != HCOMM_CQ_CONTEXT_TYPE_ROCE,
        HCCL_ERROR("[%s] SQ/CQ context is not RoCE.", __func__), HCCL_E_PARA);
    sq = &channel.sqContextAddr[0].contextInfo.roceSq;
    cq = &channel.cqContextAddr[0].contextInfo.roceCq;
    CHK_PRT_RET(sq->depth == 0 || sq->sqVa == 0 || cq->cqDepth == 0 || cq->cqVa == 0,
        HCCL_ERROR("[%s] invalid RoCE SQ/CQ context.", __func__), HCCL_E_PARA);
    return HCCL_SUCCESS;
}

void build_rdma_context(const HcommRdmaSqContext &podSq, const HcommRdmaCqContext &podCq,
                      Asc::PodRdmaSqContextLite &sq, Asc::PodRdmaCqContextLite &cq)
{
    sq.qpn = podSq.qpn;
    sq.sqVa = podSq.sqVa;
    sq.wqeSize = podSq.wqeSize;
    sq.depth = podSq.depth;
    sq.dbHwVa = podSq.dbHwVa;
    sq.dbSwVa = podSq.dbSwVa;
    sq.mtuShift = podSq.mtuShift;
    cq.cqVa = podCq.cqVa;
    cq.cqDepth = podCq.cqDepth;
    cq.dbSwVa = podCq.dbSwVa;
}

HcclResult build_rdma_slices(const HcommChannelEntity &channel, uintptr_t localAddr, uintptr_t remoteAddr,
                           uint64_t len, Asc::PodRmaBufSliceLite &local, Asc::PodRmtRmaBufSliceLite &remote)
{
    const HcommRegedBufferEntity *localBuffer = find_buffer(
        channel.localBufferAddr, channel.localBufferNum, localAddr, len, true);
    const HcommRegedBufferEntity *remoteBuffer = find_buffer(
        channel.remoteBufferAddr, channel.remoteBufferNum, remoteAddr, len, false);
    CHK_PTR_NULL(localBuffer);
    CHK_PTR_NULL(remoteBuffer);
    const auto &localResource = localBuffer->bufferInfo.rma;
    const auto &remoteResource = remoteBuffer->bufferInfo.rma;
    CHK_PRT_RET(localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA ||
        remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA,
        HCCL_ERROR("[%s] RoCE buffer requires RDMA protection.", __func__), HCCL_E_PARA);
    local = Asc::PodRmaBufSliceLite(localAddr, len, localResource.protectionInfo.memInfo.roce.lkey, 0);
    remote = Asc::PodRmtRmaBufSliceLite(remoteAddr, len, remoteResource.protectionInfo.memInfo.roce.rkey,
        0, 0, std::numeric_limits<uint32_t>::max());
    return HCCL_SUCCESS;
}

HcclResult build_rdma_notify_slices(const HcommChannelEntity &channel, uint32_t index,
                                 Asc::PodRmaBufSliceLite &local, Asc::PodRmtRmaBufSliceLite &remote)
{
    CHK_PRT_RET(index >= channel.remoteNotifyNum || channel.remoteNotifyAddr == nullptr,
        HCCL_ERROR("[%s] remote notify index[%u] out of range[%u].", __func__, index,
            channel.remoteNotifyNum), HCCL_E_PARA);
    CHK_PRT_RET(channel.notifyValueBuffer.type != HCOMM_REGED_BUFFER_RMA ||
        channel.remoteNotifyAddr[index].type != HCOMM_REGED_NOTIFY_RMA_RT,
        HCCL_ERROR("[%s] RoCE notify resource type is invalid.", __func__), HCCL_E_PARA);
    const auto &localResource = channel.notifyValueBuffer.bufferInfo.rma;
    const auto &remoteResource = channel.remoteNotifyAddr[index].notifyInfo.rmaRt;
    CHK_PRT_RET(localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA ||
        remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA,
        HCCL_ERROR("[%s] RoCE notify requires RDMA protection.", __func__), HCCL_E_PARA);
    local = Asc::PodRmaBufSliceLite(localResource.addr, localResource.size,
        localResource.protectionInfo.memInfo.roce.lkey, 0);
    remote = Asc::PodRmtRmaBufSliceLite(remoteResource.addr, remoteResource.size,
        remoteResource.protectionInfo.memInfo.roce.rkey, 0, 0, remoteResource.notifyId);
    return HCCL_SUCCESS;
}

template <typename Operation>
HcclResult for_each_rdma_slice(const Asc::PodRmaBufSliceLite &local, const Asc::PodRmtRmaBufSliceLite &remote,
                            Operation operation)
{
    uint64_t offset = 0;
    while (offset < local.get_size()) {
        const uint64_t remaining = local.get_size() - offset;
        const uint32_t sliceSize = static_cast<uint32_t>(remaining > RDMA_DMA_MAX_SIZE ?
            RDMA_DMA_MAX_SIZE : remaining);
        Asc::PodRmaBufSliceLite localSlice(local.get_addr() + offset, sliceSize, local.get_lkey(), 0);
        Asc::PodRmtRmaBufSliceLite remoteSlice(remote.get_addr() + offset, sliceSize, remote.get_rkey(),
            0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(operation(localSlice, remoteSlice));
        offset += sliceSize;
    }
    return HCCL_SUCCESS;
}

enum class RdmaOperation {
    READ,
    WRITE,
    WRITE_REDUCE,
    WRITE_WITH_NOTIFY,
    WRITE_REDUCE_WITH_NOTIFY,
    NOTIFY_RECORD,
};

HcclResult execute_rdma(HcommThreadEntity &thread, HcommChannelEntity &channel, RdmaOperation operation,
                       uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                       const Asc::PodReduceIn *reduceIn, uint32_t notifyIndex)
{
    HcommRdmaSqContext *podSq = nullptr;
    HcommRdmaCqContext *podCq = nullptr;
    CHK_RET(get_rdma_contexts(channel, podSq, podCq));
    Asc::PodRdmaSqContextLite sq{};
    Asc::PodRdmaCqContextLite cq{};
    build_rdma_context(*podSq, *podCq, sq, cq);
    Asc::PodRdma1825Ops ops(&sq, &cq);
    ops.import_queue_state(podSq->sqHead, podSq->sqTail, podCq->cqHead, podCq->cqTail,
        podCq->cqDbFlush != 0);

    Asc::PodRmaBufSliceLite local(0, 0, 0, 0);
    Asc::PodRmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    if (operation == RdmaOperation::NOTIFY_RECORD) {
        CHK_RET(build_rdma_notify_slices(channel, notifyIndex, local, remote));
    } else {
        CHK_RET(build_rdma_slices(channel, localAddr, remoteAddr, len, local, remote));
    }
    Asc::PodRmaBufSliceLite localNotify(0, 0, 0, 0);
    Asc::PodRmtRmaBufSliceLite remoteNotify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    if (operation == RdmaOperation::WRITE_WITH_NOTIFY ||
        operation == RdmaOperation::WRITE_REDUCE_WITH_NOTIFY) {
        CHK_RET(build_rdma_notify_slices(channel, notifyIndex, localNotify, remoteNotify));
    }

    Asc::PodSqeConfigLite cfg;
    cfg.cqeEn = true;
    cfg.fence = channel.fence != 0 ? 1 : 0;
    channel.fence = 0;
    if (operation == RdmaOperation::READ) {
        CHK_RET(for_each_rdma_slice(local, remote,
            [&](const Asc::PodRmaBufSliceLite &loc, const Asc::PodRmtRmaBufSliceLite &rmt) {
                return ops.read(loc, rmt, cfg);
            }));
    } else if (operation == RdmaOperation::WRITE || operation == RdmaOperation::NOTIFY_RECORD) {
        CHK_RET(for_each_rdma_slice(local, remote,
            [&](const Asc::PodRmaBufSliceLite &loc, const Asc::PodRmtRmaBufSliceLite &rmt) {
                return ops.write(loc, rmt, cfg);
            }));
    } else if (operation == RdmaOperation::WRITE_REDUCE) {
        CHK_PTR_NULL(reduceIn);
        CHK_RET(for_each_rdma_slice(local, remote,
            [&](const Asc::PodRmaBufSliceLite &loc, const Asc::PodRmtRmaBufSliceLite &rmt) {
                return ops.write_reduce(loc, rmt, cfg, reduceIn->dataType, reduceIn->reduceOp);
            }));
    } else if (operation == RdmaOperation::WRITE_WITH_NOTIFY) {
        CHK_RET(for_each_rdma_slice(local, remote,
            [&](const Asc::PodRmaBufSliceLite &loc, const Asc::PodRmtRmaBufSliceLite &rmt) {
                return ops.write(loc, rmt, cfg);
            }));
        CHK_RET(ops.write(localNotify, remoteNotify, cfg));
    } else {
        CHK_PTR_NULL(reduceIn);
        CHK_RET(for_each_rdma_slice(local, remote,
            [&](const Asc::PodRmaBufSliceLite &loc, const Asc::PodRmtRmaBufSliceLite &rmt) {
                return ops.write_reduce(loc, rmt, cfg, reduceIn->dataType, reduceIn->reduceOp);
            }));
        CHK_RET(ops.write(localNotify, remoteNotify, cfg));
    }

    u64 dbAddr = 0;
    u64 dbValue = 0;
    CHK_RET(ops.build_doorbell(dbAddr, dbValue));
    bool cqDbFlush = false;
    ops.export_queue_state(podSq->sqHead, podSq->sqTail, podCq->cqHead, podCq->cqTail, cqDbFlush);
    podCq->cqDbFlush = cqDbFlush ? 1 : 0;
    HcommRtsqContext *rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    CHK_RET(hcomm_pod_rtsq_rdma_db_send(*rtsq, dbAddr, dbValue));

    const int32_t pollNum = (operation == RdmaOperation::WRITE_WITH_NOTIFY ||
        operation == RdmaOperation::WRITE_REDUCE_WITH_NOTIFY) ? 2 : 1;
    std::vector<int32_t> errList;
    HcclResult pollRet = ops.poll_cq(pollNum, 5, errList);
    (void)ops.build_cq_doorbell(dbAddr, dbValue);
    ops.export_queue_state(podSq->sqHead, podSq->sqTail, podCq->cqHead, podCq->cqTail, cqDbFlush);
    podCq->cqDbFlush = cqDbFlush ? 1 : 0;
    return pollRet;
}

HcclResult convert_batch_reduce(HcommDataType dataType, HcommReduceOp reduceOp,
                              Asc::PodReduceIn &reduceIn, uint64_t count, uint64_t &len)
{
    Asc::PodDataType innerType = Asc::PodDataType::INVALID;
    uint32_t typeSize = 0;
    switch (dataType) {
        case HCOMM_DATA_TYPE_INT8: innerType = Asc::PodDataType::INT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_INT16: innerType = Asc::PodDataType::INT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT32: innerType = Asc::PodDataType::INT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP16: innerType = Asc::PodDataType::FP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_FP32: innerType = Asc::PodDataType::FP32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_INT64: innerType = Asc::PodDataType::INT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT64: innerType = Asc::PodDataType::UINT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT8: innerType = Asc::PodDataType::UINT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_UINT16: innerType = Asc::PodDataType::UINT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_UINT32: innerType = Asc::PodDataType::UINT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP64: innerType = Asc::PodDataType::FP64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_BFP16: innerType = Asc::PodDataType::BFP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT128: innerType = Asc::PodDataType::INT128; typeSize = 16; break;
        default: return HCCL_E_PARA;
    }
    Asc::PodReduceOp innerOp = Asc::PodReduceOp::INVALID;
    switch (reduceOp) {
        case HCOMM_REDUCE_SUM: innerOp = Asc::PodReduceOp::SUM; break;
        case HCOMM_REDUCE_PROD: innerOp = Asc::PodReduceOp::PROD; break;
        case HCOMM_REDUCE_MAX: innerOp = Asc::PodReduceOp::MAX; break;
        case HCOMM_REDUCE_MIN: innerOp = Asc::PodReduceOp::MIN; break;
        default: return HCCL_E_PARA;
    }
    CHK_PRT_RET(count > std::numeric_limits<uint64_t>::max() / typeSize,
        HCCL_ERROR("[%s] reduce length overflow.", __func__), HCCL_E_PARA);
    len = count * typeSize;
    reduceIn = Asc::PodReduceIn(innerType, innerOp);
    return HCCL_SUCCESS;
}

} // namespace

HcclResult hcomm_pod_write(HcommThreadEntity &thread, HcommChannelEntity &channel,
                         uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(thread, channel, localAddr, remoteAddr, len,
            [](Asc::PodUbConnLite &connection, const Asc::PodRmaBufSliceLite &local,
               const Asc::PodRmtRmaBufSliceLite &remote, const Asc::PodSqeConfigLite &cfg,
               Asc::PodConnLiteOperationOut &out) { connection.write_pod(local, remote, cfg, out); });
    }
    return execute_rdma(thread, channel, RdmaOperation::WRITE, localAddr, remoteAddr, len, nullptr, 0);
}

HcclResult hcomm_pod_write_reduce(HcommThreadEntity &thread, HcommChannelEntity &channel,
                               uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                               const Asc::PodReduceIn &reduceIn)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(thread, channel, localAddr, remoteAddr, len,
            [&reduceIn](Asc::PodUbConnLite &connection, const Asc::PodRmaBufSliceLite &local,
                const Asc::PodRmtRmaBufSliceLite &remote, const Asc::PodSqeConfigLite &cfg,
                Asc::PodConnLiteOperationOut &out) {
                connection.write_reduce_pod(reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out);
            });
    }
    return execute_rdma(thread, channel, RdmaOperation::WRITE_REDUCE,
        localAddr, remoteAddr, len, &reduceIn, 0);
}

HcclResult hcomm_pod_write_with_notify(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                   uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                                   uint32_t remoteNotifyIdx)
{
    if (is_ub_protocol(channel.protocol)) {
        Asc::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(build_ub_remote_notify(channel, remoteNotifyIdx, notify));
        return execute_ub_rma(thread, channel, localAddr, remoteAddr, len,
            [&notify](Asc::PodUbConnLite &connection, const Asc::PodRmaBufSliceLite &local,
                const Asc::PodRmtRmaBufSliceLite &remote, const Asc::PodSqeConfigLite &cfg,
                Asc::PodConnLiteOperationOut &out) {
                connection.write_with_notify_pod(local, remote, cfg, out, notify, POD_NOTIFY_VALUE);
            });
    }
    return execute_rdma(thread, channel, RdmaOperation::WRITE_WITH_NOTIFY,
        localAddr, remoteAddr, len, nullptr, remoteNotifyIdx);
}

HcclResult hcomm_pod_write_reduce_with_notify(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                         uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                                         const Asc::PodReduceIn &reduceIn, uint32_t remoteNotifyIdx)
{
    if (is_ub_protocol(channel.protocol)) {
        Asc::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(build_ub_remote_notify(channel, remoteNotifyIdx, notify));
        return execute_ub_rma(thread, channel, localAddr, remoteAddr, len,
            [&reduceIn, &notify](Asc::PodUbConnLite &connection, const Asc::PodRmaBufSliceLite &local,
                const Asc::PodRmtRmaBufSliceLite &remote, const Asc::PodSqeConfigLite &cfg,
                Asc::PodConnLiteOperationOut &out) {
                connection.write_reduce_with_notify_pod(reduceIn.dataType, reduceIn.reduceOp,
                    local, remote, cfg, out, notify, POD_NOTIFY_VALUE);
            });
    }
    return execute_rdma(thread, channel, RdmaOperation::WRITE_REDUCE_WITH_NOTIFY,
        localAddr, remoteAddr, len, &reduceIn, remoteNotifyIdx);
}

HcclResult hcomm_pod_read(HcommThreadEntity &thread, HcommChannelEntity &channel,
                        uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(thread, channel, localAddr, remoteAddr, len,
            [](Asc::PodUbConnLite &connection, const Asc::PodRmaBufSliceLite &local,
               const Asc::PodRmtRmaBufSliceLite &remote, const Asc::PodSqeConfigLite &cfg,
               Asc::PodConnLiteOperationOut &out) { connection.read_pod(local, remote, cfg, out); });
    }
    return execute_rdma(thread, channel, RdmaOperation::READ, localAddr, remoteAddr, len, nullptr, 0);
}

HcclResult hcomm_pod_read_reduce(HcommThreadEntity &thread, HcommChannelEntity &channel,
                              uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                              const Asc::PodReduceIn &reduceIn)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(thread, channel, localAddr, remoteAddr, len,
            [&reduceIn](Asc::PodUbConnLite &connection, const Asc::PodRmaBufSliceLite &local,
                const Asc::PodRmtRmaBufSliceLite &remote, const Asc::PodSqeConfigLite &cfg,
                Asc::PodConnLiteOperationOut &out) {
                connection.read_reduce_pod(reduceIn, local, remote, cfg, out);
            });
    }
    // The original RoCE transport does not override ReadReduce and therefore performs no operation.
    return channel.protocol == COMM_PROTOCOL_ROCE ? HCCL_SUCCESS : HCCL_E_NOT_SUPPORT;
}

HcclResult hcomm_pod_notify_record(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                uint32_t remoteNotifyIdx)
{
    if (!is_ub_protocol(channel.protocol)) {
        return execute_rdma(thread, channel, RdmaOperation::NOTIFY_RECORD, 0, 0, 0, nullptr, remoteNotifyIdx);
    }
    HcommJfsContext *jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(build_ub_remote_notify(channel, remoteNotifyIdx, notify));
    Asc::PodUbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::PodUbConnLite connection;
    connection.import_pod_context(param);
    import_ub_state(*jfs, connection);
    Asc::PodSqeConfigLite cfg;
    if (remoteNotifyIdx == 1) {
        cfg.cqeEn = true;
        cfg.placeOdr = UB_STRONG_ORDER;
        cfg.compOrder = UB_COMPLETION;
    }
    Asc::PodConnLiteOperationOut out{};
    const uint32_t value = POD_NOTIFY_VALUE;
    connection.inline_write_pod(reinterpret_cast<const uint8_t *>(&value), POD_NOTIFY_SIZE, notify, cfg, out);
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, *jfs, out.pi);
}

HcclResult hcomm_pod_notify_wait(HcommThreadEntity &thread, const HcommChannelEntity &channel,
                              uint32_t localNotifyIdx, uint32_t timeout)
{
    CHK_PRT_RET(localNotifyIdx >= channel.localNotifyNum || channel.localNotifyAddr == nullptr,
        HCCL_ERROR("[%s] local notify index[%u] out of range[%u].", __func__, localNotifyIdx,
            channel.localNotifyNum), HCCL_E_PARA);
    HcommRtsqContext *rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    CHK_PRT_RET(channel.localNotifyAddr[localNotifyIdx].type != HCOMM_REGED_NOTIFY_RMA_RT,
        HCCL_ERROR("[%s] local notify is not RMA RT.", __func__), HCCL_E_PARA);
    const int32_t notifyId = channel.localNotifyAddr[localNotifyIdx].notifyInfo.rmaRt.notifyId;
    CHK_PRT_RET(notifyId < 0, HCCL_ERROR("[%s] invalid notify id[%d].", __func__, notifyId), HCCL_E_PARA);
    return hcomm_pod_rtsq_notify_wait(*rtsq, static_cast<uint32_t>(notifyId), timeout);
}

HcclResult hcomm_pod_fence(HcommChannelEntity &channel)
{
    channel.fence = 1;
    return HCCL_SUCCESS;
}

HcclResult hcomm_pod_drain(HcommThreadEntity &thread, HcommChannelEntity &channel)
{
    if (channel.protocol == COMM_PROTOCOL_ROCE) {
        return HCCL_SUCCESS;
    }
    CHK_PRT_RET(!is_ub_protocol(channel.protocol),
        HCCL_ERROR("[%s] protocol[%d] is not supported.", __func__, channel.protocol), HCCL_E_NOT_SUPPORT);
    if (channel.localDrainNotify.notifyInfo.rmaRt.size == 0 ||
        channel.remoteDrainBuffer.bufferInfo.rma.size == 0) {
        HCCL_WARNING("[%s] drain resource is empty; skip.", __func__);
        return HCCL_SUCCESS;
    }
    HcommJfsContext *jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    CHK_PRT_RET(channel.localDrainNotify.type != HCOMM_REGED_NOTIFY_RMA_RT ||
        channel.remoteDrainBuffer.type != HCOMM_REGED_BUFFER_RMA,
        HCCL_ERROR("[%s] UB drain resource type is invalid.", __func__), HCCL_E_PARA);
    const auto &localResource = channel.localDrainNotify.notifyInfo.rmaRt;
    const auto &remoteResource = channel.remoteDrainBuffer.bufferInfo.rma;
    CHK_PRT_RET(localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
        remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        HCCL_ERROR("[%s] UB drain requires URMA protection.", __func__), HCCL_E_PARA);
    Asc::PodRmaBufSliceLite local(localResource.addr, localResource.size, 0,
        localResource.protectionInfo.memInfo.ub.tokenId);
    Asc::PodRmtRmaBufSliceLite remote(remoteResource.addr, remoteResource.size, 0,
        remoteResource.protectionInfo.memInfo.ub.tokenId,
        remoteResource.protectionInfo.memInfo.ub.tokenValue,
        std::numeric_limits<uint32_t>::max());
    Asc::PodUbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::PodUbConnLite connection;
    connection.import_pod_context(param);
    import_ub_state(*jfs, connection);
    Asc::PodSqeConfigLite cfg;
    cfg.fence = 1;
    cfg.placeOdr = UB_STRONG_ORDER;
    cfg.compOrder = UB_COMPLETION;
    channel.fence = 0;
    Asc::PodConnLiteOperationOut out{};
    connection.read_pod(local, remote, cfg, out);
    export_ub_state(connection, *jfs);
    CHK_RET(ring_ub_doorbell(thread, *jfs, out.pi));
    HcommRtsqContext *rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    CHK_PRT_RET(localResource.notifyId < 0,
        HCCL_ERROR("[%s] invalid drain notify id[%d].", __func__, localResource.notifyId), HCCL_E_PARA);
    return hcomm_pod_rtsq_notify_wait(*rtsq, static_cast<uint32_t>(localResource.notifyId), POD_DEFAULT_TIMEOUT);
}

HcclResult hcomm_pod_batch_transfer(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                 const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum)
{
    CHK_PTR_NULL(transferDescs);
    CHK_PRT_RET(transferDescNum == 0, HCCL_ERROR("[%s] transferDescNum is 0.", __func__), HCCL_E_PARA);
    HcommJfsContext *jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::PodUbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::PodUbConnLite connection;
    connection.import_pod_context(param);
    import_ub_state(*jfs, connection);
    Asc::PodSqeConfigLite cfg = get_ub_config(channel);
    Asc::PodConnLiteOperationOut out{};

    for (uint32_t index = 0; index < transferDescNum; ++index) {
        const HcommBatchTransferDesc &desc = transferDescs[index];
        cfg.cqeEn = index == transferDescNum - 1;
        cfg.placeOdr = cfg.cqeEn ? UB_STRONG_ORDER : UB_RELAX_ORDER;
        cfg.compOrder = cfg.cqeEn ? UB_COMPLETION : UB_NO_COMPLETION;
        if (desc.transType == HCOMM_TRANSFER_TYPE_NOTIFY_RECORD) {
            Asc::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
            CHK_RET(build_ub_remote_notify(channel, desc.transferInfo.notifyRecord.notifyIdx, notify));
            const uint32_t value = POD_NOTIFY_VALUE;
            connection.inline_write_pod(reinterpret_cast<const uint8_t *>(&value), POD_NOTIFY_SIZE,
                notify, cfg, out);
            continue;
        }

        uintptr_t localAddr = 0;
        uintptr_t remoteAddr = 0;
        uint64_t len = 0;
        uint32_t notifyIdx = 0;
        Asc::PodReduceIn reduceIn{Asc::PodDataType::INVALID, Asc::PodReduceOp::INVALID};
        if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.write.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.write.dst);
            len = desc.transferInfo.write.len;
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_READ) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.read.dst);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.read.src);
            len = desc.transferInfo.read.len;
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeWithNotify.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeWithNotify.dst);
            len = desc.transferInfo.writeWithNotify.len;
            notifyIdx = desc.transferInfo.writeWithNotify.notifyIdx;
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE_REDUCE ||
                   desc.transType == HCOMM_TRANSFER_TYPE_READ_REDUCE) {
            localAddr = desc.transType == HCOMM_TRANSFER_TYPE_WRITE_REDUCE ?
                reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.src) :
                reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.dst);
            remoteAddr = desc.transType == HCOMM_TRANSFER_TYPE_WRITE_REDUCE ?
                reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.dst) :
                reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.src);
            CHK_RET(convert_batch_reduce(desc.transferInfo.reduce.dataType, desc.transferInfo.reduce.reduceOp,
                reduceIn, desc.transferInfo.reduce.count, len));
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeReduceWithNotify.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeReduceWithNotify.dst);
            notifyIdx = desc.transferInfo.writeReduceWithNotify.notifyIdx;
            CHK_RET(convert_batch_reduce(desc.transferInfo.writeReduceWithNotify.dataType,
                desc.transferInfo.writeReduceWithNotify.reduceOp, reduceIn,
                desc.transferInfo.writeReduceWithNotify.count, len));
        } else {
            HCCL_ERROR("[%s] unsupported transfer type[%d].", __func__, desc.transType);
            return HCCL_E_PARA;
        }
        CHK_PRT_RET(localAddr == 0 || remoteAddr == 0,
            HCCL_ERROR("[%s] null address at batch index[%u].", __func__, index), HCCL_E_PTR);
        Asc::PodRmaBufSliceLite local(0, 0, 0, 0);
        Asc::PodRmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(build_ub_slices(channel, localAddr, remoteAddr, len, local, remote));
        if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE) {
            connection.write_pod(local, remote, cfg, out);
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_READ) {
            connection.read_pod(local, remote, cfg, out);
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE_REDUCE) {
            connection.write_reduce_pod(reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out);
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_READ_REDUCE) {
            connection.read_reduce_pod(reduceIn, local, remote, cfg, out);
        } else {
            Asc::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
            CHK_RET(build_ub_remote_notify(channel, notifyIdx, notify));
            if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY) {
                connection.write_with_notify_pod(local, remote, cfg, out, notify, POD_NOTIFY_VALUE);
            } else {
                connection.write_reduce_with_notify_pod(reduceIn.dataType, reduceIn.reduceOp,
                    local, remote, cfg, out, notify, POD_NOTIFY_VALUE);
            }
        }
    }
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, *jfs, out.pi);
}
