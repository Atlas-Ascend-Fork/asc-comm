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

bool IsUbProtocol(CommProtocol protocol)
{
    return protocol == COMM_PROTOCOL_UBC_CTP || protocol == COMM_PROTOCOL_UBC_TP ||
        protocol == COMM_PROTOCOL_UB_MEM || protocol == COMM_PROTOCOL_UBOE || protocol == COMM_PROTOCOL_UBG;
}

bool IsRangeContained(uint64_t base, uint64_t capacity, uint64_t addr, uint64_t size)
{
    return addr >= base && size <= capacity && (addr - base) <= (capacity - size);
}

const HcommProtectionInfo *FindProtection(const HcommProtectionInfo *protections, uint32_t protectionNum,
                                         uint64_t addr, uint64_t size, bool useFirstAsFallback)
{
    if (protections == nullptr || protectionNum == 0) {
        return nullptr;
    }
    for (uint32_t index = 0; index < protectionNum; ++index) {
        if (IsRangeContained(protections[index].addr, protections[index].length, addr, size)) {
            return &protections[index];
        }
    }
    if (useFirstAsFallback) {
        HCCL_WARNING("[%s] addr[0x%llx], size[0x%llx] is outside local buffers; use the first protection.",
            __func__, addr, size);
        return &protections[0];
    }
    return nullptr;
}

HcclResult GetRtsq(HcommThreadEntity &thread, HcommRtsqContext *&rtsq)
{
    CHK_PTR_NULL(thread.sqContextAddr);
    rtsq = thread.sqContextAddr;
    return HCCL_SUCCESS;
}

HcclResult GetUbContext(HcommChannelEntity &channel, HcommJfsContext *&jfs)
{
    CHK_PRT_RET(!IsUbProtocol(channel.protocol),
        HCCL_ERROR("[%s] protocol[%d] is not UB.", __func__, channel.protocol), HCCL_E_NOT_SUPPORT);
    CHK_PRT_RET(channel.sqNum == 0, HCCL_ERROR("[%s] UB SQ context is empty.", __func__), HCCL_E_PARA);
    CHK_PTR_NULL(channel.sqContextAddr);
    jfs = &channel.sqContextAddr[0].context.jfs;
    CHK_PRT_RET(jfs->sqDepth == 0 || jfs->sqVa == 0 || jfs->maxReadSize == 0 || jfs->maxWriteSize == 0,
        HCCL_ERROR("[%s] invalid UB JFS context.", __func__), HCCL_E_PARA);
    return HCCL_SUCCESS;
}

HcclResult BuildUbSlices(const HcommChannelEntity &channel, uintptr_t localAddr, uintptr_t remoteAddr,
                         uint64_t len, Hccl::PodRmaBufSliceLite &local, Hccl::PodRmtRmaBufSliceLite &remote)
{
    const HcommProtectionInfo *localProtection = FindProtection(
        channel.localBufferAddr, channel.localBufferNum, localAddr, len, true);
    const HcommProtectionInfo *remoteProtection = FindProtection(
        channel.remoteBufferAddr, channel.remoteBufferNum, remoteAddr, len, false);
    CHK_PTR_NULL(localProtection);
    CHK_PTR_NULL(remoteProtection);
    CHK_PRT_RET(localProtection->type != HCOMM_PROTECTION_TYPE_URMA ||
        remoteProtection->type != HCOMM_PROTECTION_TYPE_URMA,
        HCCL_ERROR("[%s] UB buffer requires URMA protection.", __func__), HCCL_E_PARA);
    local = Hccl::PodRmaBufSliceLite(localAddr, len, 0, localProtection->protection.urma.tokenId);
    remote = Hccl::PodRmtRmaBufSliceLite(remoteAddr, len, 0,
        remoteProtection->protection.urma.tokenId, remoteProtection->protection.urma.tokenValue,
        std::numeric_limits<uint32_t>::max());
    return HCCL_SUCCESS;
}

HcclResult BuildUbRemoteNotify(const HcommChannelEntity &channel, uint32_t index,
                               Hccl::PodRmtRmaBufSliceLite &notify)
{
    CHK_PRT_RET(index >= channel.remoteNotifyNum || channel.remoteNotifyAddr == nullptr,
        HCCL_ERROR("[%s] remote notify index[%u] out of range[%u].", __func__, index,
            channel.remoteNotifyNum), HCCL_E_PARA);
    const HcommRmaNotify &resource = channel.remoteNotifyAddr[index].notify.rma;
    CHK_PRT_RET(resource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        HCCL_ERROR("[%s] UB notify requires URMA protection.", __func__), HCCL_E_PARA);
    notify = Hccl::PodRmtRmaBufSliceLite(resource.address, resource.size, 0,
        resource.protectionInfo.protection.urma.tokenId,
        resource.protectionInfo.protection.urma.tokenValue, resource.notifyId);
    return HCCL_SUCCESS;
}

void FillUbParam(const HcommJfsContext &jfs, Hccl::PodUbConnLiteParam &param)
{
    param.dieId = jfs.dieId;
    param.funcId = jfs.funcId;
    param.jettyId = jfs.jettyId;
    param.dbAddr = jfs.dbVa;
    param.sqVa = jfs.sqVa;
    param.sqDepth = jfs.sqDepth;
    param.tpn = jfs.tpn;
    param.dwqeCacheLocked = jfs.dwqeCacheLocked != 0;
    param.jfcPollMode = jfs.jfcPollMode;
    param.sqCiAddr = jfs.sqCiAddr;
    param.maxReadSize = jfs.maxReadSize;
    param.maxWriteSize = jfs.maxWriteSize;
    (void)memcpy_sp(param.rmtEid, sizeof(param.rmtEid), jfs.remoteEid, sizeof(jfs.remoteEid));
    (void)memcpy_sp(param.locEid, sizeof(param.locEid), jfs.localEid, sizeof(jfs.localEid));
}

void ImportUbState(const HcommJfsContext &jfs, Hccl::PodUbConnLite &connection)
{
    connection.ImportQueueState(jfs.pi, jfs.ci, jfs.piDetourCount, jfs.ciDetourCount);
}

void ExportUbState(Hccl::PodUbConnLite &connection, HcommJfsContext &jfs)
{
    connection.ExportQueueState(jfs.pi, jfs.ci, jfs.piDetourCount, jfs.ciDetourCount);
}

HcclResult RingUbDoorbell(HcommThreadEntity &thread, const HcommJfsContext &jfs, uint16_t pi)
{
    HcommRtsqContext *rtsq = nullptr;
    CHK_RET(GetRtsq(thread, rtsq));
    const Hccl::PodUbJettyLiteId jettyId(jfs.dieId, jfs.funcId, jfs.jettyId);
    return HcommPodRtsqUbDbSend(*rtsq, jettyId, pi);
}

Hccl::PodSqeConfigLite GetUbConfig(HcommChannelEntity &channel)
{
    Hccl::PodSqeConfigLite cfg;
    if (channel.fence != 0) {
        cfg.fence = 1;
        cfg.placeOdr = UB_STRONG_ORDER;
        cfg.compOrder = UB_COMPLETION;
    }
    channel.fence = 0;
    return cfg;
}

template <typename Operation>
HcclResult ExecuteUbRma(HcommThreadEntity &thread, HcommChannelEntity &channel,
                        uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, Operation operation)
{
    HcommJfsContext *jfs = nullptr;
    CHK_RET(GetUbContext(channel, jfs));
    Hccl::PodRmaBufSliceLite local(0, 0, 0, 0);
    Hccl::PodRmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(BuildUbSlices(channel, localAddr, remoteAddr, len, local, remote));

    Hccl::PodUbConnLiteParam param{};
    FillUbParam(*jfs, param);
    Hccl::PodUbConnLite connection;
    connection.ImportPodContext(param);
    ImportUbState(*jfs, connection);
    Hccl::PodConnLiteOperationOut out{};
    Hccl::PodSqeConfigLite cfg = GetUbConfig(channel);
    operation(connection, local, remote, cfg, out);
    ExportUbState(connection, *jfs);
    return RingUbDoorbell(thread, *jfs, out.pi);
}

HcclResult GetRdmaContexts(HcommChannelEntity &channel, HcommRdmaSqContext *&sq,
                           HcommRdmaCqContext *&cq)
{
    CHK_PRT_RET(channel.protocol != COMM_PROTOCOL_ROCE,
        HCCL_ERROR("[%s] protocol[%d] is not RoCE.", __func__, channel.protocol), HCCL_E_NOT_SUPPORT);
    CHK_PRT_RET(channel.sqNum == 0 || channel.cqNum == 0,
        HCCL_ERROR("[%s] RoCE SQ/CQ context is empty.", __func__), HCCL_E_PARA);
    CHK_PTR_NULL(channel.sqContextAddr);
    CHK_PTR_NULL(channel.cqContextAddr);
    sq = &channel.sqContextAddr[0].context.rdma;
    cq = &channel.cqContextAddr[0].context.rdma;
    CHK_PRT_RET(sq->dmaMode != 1 || sq->depth == 0 || sq->sqVa == 0 || cq->cqDepth == 0 || cq->cqVa == 0,
        HCCL_ERROR("[%s] invalid RoCE SQ/CQ context.", __func__), HCCL_E_PARA);
    return HCCL_SUCCESS;
}

void BuildRdmaContext(const HcommRdmaSqContext &podSq, const HcommRdmaCqContext &podCq,
                      Hccl::PodRdmaSqContextLite &sq, Hccl::PodRdmaCqContextLite &cq)
{
    sq.qpn = podSq.qpn;
    sq.sqVa = podSq.sqVa;
    sq.wqeSize = podSq.wqeSize;
    sq.depth = podSq.depth;
    sq.headAddr = podSq.headAddr;
    sq.tailAddr = podSq.tailAddr;
    sq.dbHwVa = podSq.dbHwVa;
    sq.dbSwVa = podSq.dbSwVa;
    sq.sl = podSq.sl;
    sq.mtuShift = podSq.mtuShift;
    cq.cqn = podCq.cqn;
    cq.cqVa = podCq.cqVa;
    cq.cqeSize = podCq.cqeSize;
    cq.cqDepth = podCq.cqDepth;
    cq.headAddr = podCq.headAddr;
    cq.tailAddr = podCq.tailAddr;
    cq.dbHwVa = podCq.dbHwVa;
    cq.dbSwVa = podCq.dbSwVa;
}

HcclResult BuildRdmaSlices(const HcommChannelEntity &channel, uintptr_t localAddr, uintptr_t remoteAddr,
                           uint64_t len, Hccl::PodRmaBufSliceLite &local, Hccl::PodRmtRmaBufSliceLite &remote)
{
    const HcommProtectionInfo *localProtection = FindProtection(
        channel.localBufferAddr, channel.localBufferNum, localAddr, len, true);
    const HcommProtectionInfo *remoteProtection = FindProtection(
        channel.remoteBufferAddr, channel.remoteBufferNum, remoteAddr, len, false);
    CHK_PTR_NULL(localProtection);
    CHK_PTR_NULL(remoteProtection);
    CHK_PRT_RET(localProtection->type != HCOMM_PROTECTION_TYPE_RDMA ||
        remoteProtection->type != HCOMM_PROTECTION_TYPE_RDMA,
        HCCL_ERROR("[%s] RoCE buffer requires RDMA protection.", __func__), HCCL_E_PARA);
    local = Hccl::PodRmaBufSliceLite(localAddr, len, localProtection->protection.rdma.lkey, 0);
    remote = Hccl::PodRmtRmaBufSliceLite(remoteAddr, len, remoteProtection->protection.rdma.rkey,
        0, 0, std::numeric_limits<uint32_t>::max());
    return HCCL_SUCCESS;
}

HcclResult BuildRdmaNotifySlices(const HcommChannelEntity &channel, uint32_t index,
                                 Hccl::PodRmaBufSliceLite &local, Hccl::PodRmtRmaBufSliceLite &remote)
{
    CHK_PRT_RET(index >= channel.remoteNotifyNum || channel.remoteNotifyAddr == nullptr,
        HCCL_ERROR("[%s] remote notify index[%u] out of range[%u].", __func__, index,
            channel.remoteNotifyNum), HCCL_E_PARA);
    const HcommProtectionInfo &localResource = channel.notifyValueBuffer;
    const HcommRmaNotify &remoteResource = channel.remoteNotifyAddr[index].notify.rma;
    CHK_PRT_RET(localResource.type != HCOMM_PROTECTION_TYPE_RDMA ||
        remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA,
        HCCL_ERROR("[%s] RoCE notify requires RDMA protection.", __func__), HCCL_E_PARA);
    local = Hccl::PodRmaBufSliceLite(localResource.addr, localResource.length,
        localResource.protection.rdma.lkey, 0);
    remote = Hccl::PodRmtRmaBufSliceLite(remoteResource.address, remoteResource.size,
        remoteResource.protectionInfo.protection.rdma.rkey, 0, 0, remoteResource.notifyId);
    return HCCL_SUCCESS;
}

template <typename Operation>
HcclResult ForEachRdmaSlice(const Hccl::PodRmaBufSliceLite &local, const Hccl::PodRmtRmaBufSliceLite &remote,
                            Operation operation)
{
    uint64_t offset = 0;
    while (offset < local.GetSize()) {
        const uint64_t remaining = local.GetSize() - offset;
        const uint32_t sliceSize = static_cast<uint32_t>(remaining > RDMA_DMA_MAX_SIZE ?
            RDMA_DMA_MAX_SIZE : remaining);
        Hccl::PodRmaBufSliceLite localSlice(local.GetAddr() + offset, sliceSize, local.GetLkey(), 0);
        Hccl::PodRmtRmaBufSliceLite remoteSlice(remote.GetAddr() + offset, sliceSize, remote.GetRkey(),
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

HcclResult ExecuteRdma(HcommThreadEntity &thread, HcommChannelEntity &channel, RdmaOperation operation,
                       uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                       const Hccl::PodReduceIn *reduceIn, uint32_t notifyIndex)
{
    HcommRdmaSqContext *podSq = nullptr;
    HcommRdmaCqContext *podCq = nullptr;
    CHK_RET(GetRdmaContexts(channel, podSq, podCq));
    Hccl::PodRdmaSqContextLite sq{};
    Hccl::PodRdmaCqContextLite cq{};
    BuildRdmaContext(*podSq, *podCq, sq, cq);
    Hccl::PodRdma1825Ops ops(&sq, &cq);
    ops.ImportQueueState(podSq->sqHead, podSq->sqTail, podCq->cqHead, podCq->cqTail,
        podCq->cqDbFlush != 0);

    Hccl::PodRmaBufSliceLite local(0, 0, 0, 0);
    Hccl::PodRmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    if (operation == RdmaOperation::NOTIFY_RECORD) {
        CHK_RET(BuildRdmaNotifySlices(channel, notifyIndex, local, remote));
    } else {
        CHK_RET(BuildRdmaSlices(channel, localAddr, remoteAddr, len, local, remote));
    }
    Hccl::PodRmaBufSliceLite localNotify(0, 0, 0, 0);
    Hccl::PodRmtRmaBufSliceLite remoteNotify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    if (operation == RdmaOperation::WRITE_WITH_NOTIFY ||
        operation == RdmaOperation::WRITE_REDUCE_WITH_NOTIFY) {
        CHK_RET(BuildRdmaNotifySlices(channel, notifyIndex, localNotify, remoteNotify));
    }

    Hccl::PodSqeConfigLite cfg;
    cfg.cqeEn = true;
    cfg.fence = channel.fence != 0 ? 1 : 0;
    channel.fence = 0;
    if (operation == RdmaOperation::READ) {
        CHK_RET(ForEachRdmaSlice(local, remote,
            [&](const Hccl::PodRmaBufSliceLite &loc, const Hccl::PodRmtRmaBufSliceLite &rmt) {
                return ops.Read(loc, rmt, cfg);
            }));
    } else if (operation == RdmaOperation::WRITE || operation == RdmaOperation::NOTIFY_RECORD) {
        CHK_RET(ForEachRdmaSlice(local, remote,
            [&](const Hccl::PodRmaBufSliceLite &loc, const Hccl::PodRmtRmaBufSliceLite &rmt) {
                return ops.Write(loc, rmt, cfg);
            }));
    } else if (operation == RdmaOperation::WRITE_REDUCE) {
        CHK_PTR_NULL(reduceIn);
        CHK_RET(ForEachRdmaSlice(local, remote,
            [&](const Hccl::PodRmaBufSliceLite &loc, const Hccl::PodRmtRmaBufSliceLite &rmt) {
                return ops.WriteReduce(loc, rmt, cfg, reduceIn->dataType, reduceIn->reduceOp);
            }));
    } else if (operation == RdmaOperation::WRITE_WITH_NOTIFY) {
        CHK_RET(ForEachRdmaSlice(local, remote,
            [&](const Hccl::PodRmaBufSliceLite &loc, const Hccl::PodRmtRmaBufSliceLite &rmt) {
                return ops.Write(loc, rmt, cfg);
            }));
        CHK_RET(ops.Write(localNotify, remoteNotify, cfg));
    } else {
        CHK_PTR_NULL(reduceIn);
        CHK_RET(ForEachRdmaSlice(local, remote,
            [&](const Hccl::PodRmaBufSliceLite &loc, const Hccl::PodRmtRmaBufSliceLite &rmt) {
                return ops.WriteReduce(loc, rmt, cfg, reduceIn->dataType, reduceIn->reduceOp);
            }));
        CHK_RET(ops.Write(localNotify, remoteNotify, cfg));
    }

    u64 dbAddr = 0;
    u64 dbValue = 0;
    CHK_RET(ops.BuildDoorbell(dbAddr, dbValue));
    bool cqDbFlush = false;
    ops.ExportQueueState(podSq->sqHead, podSq->sqTail, podCq->cqHead, podCq->cqTail, cqDbFlush);
    podCq->cqDbFlush = cqDbFlush ? 1 : 0;
    HcommRtsqContext *rtsq = nullptr;
    CHK_RET(GetRtsq(thread, rtsq));
    CHK_RET(HcommPodRtsqRdmaDbSend(*rtsq, dbAddr, dbValue));

    const int32_t pollNum = (operation == RdmaOperation::WRITE_WITH_NOTIFY ||
        operation == RdmaOperation::WRITE_REDUCE_WITH_NOTIFY) ? 2 : 1;
    std::vector<int32_t> errList;
    HcclResult pollRet = ops.PollCq(pollNum, 5, errList);
    (void)ops.BuildCqDoorbell(dbAddr, dbValue);
    ops.ExportQueueState(podSq->sqHead, podSq->sqTail, podCq->cqHead, podCq->cqTail, cqDbFlush);
    podCq->cqDbFlush = cqDbFlush ? 1 : 0;
    return pollRet;
}

HcclResult ConvertBatchReduce(HcommDataType dataType, HcommReduceOp reduceOp,
                              Hccl::PodReduceIn &reduceIn, uint64_t count, uint64_t &len)
{
    Hccl::PodDataType innerType = Hccl::PodDataType::INVALID;
    uint32_t typeSize = 0;
    switch (dataType) {
        case HCOMM_DATA_TYPE_INT8: innerType = Hccl::PodDataType::INT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_INT16: innerType = Hccl::PodDataType::INT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT32: innerType = Hccl::PodDataType::INT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP16: innerType = Hccl::PodDataType::FP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_FP32: innerType = Hccl::PodDataType::FP32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_INT64: innerType = Hccl::PodDataType::INT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT64: innerType = Hccl::PodDataType::UINT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT8: innerType = Hccl::PodDataType::UINT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_UINT16: innerType = Hccl::PodDataType::UINT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_UINT32: innerType = Hccl::PodDataType::UINT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP64: innerType = Hccl::PodDataType::FP64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_BFP16: innerType = Hccl::PodDataType::BFP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT128: innerType = Hccl::PodDataType::INT128; typeSize = 16; break;
        default: return HCCL_E_PARA;
    }
    Hccl::PodReduceOp innerOp = Hccl::PodReduceOp::INVALID;
    switch (reduceOp) {
        case HCOMM_REDUCE_SUM: innerOp = Hccl::PodReduceOp::SUM; break;
        case HCOMM_REDUCE_PROD: innerOp = Hccl::PodReduceOp::PROD; break;
        case HCOMM_REDUCE_MAX: innerOp = Hccl::PodReduceOp::MAX; break;
        case HCOMM_REDUCE_MIN: innerOp = Hccl::PodReduceOp::MIN; break;
        default: return HCCL_E_PARA;
    }
    CHK_PRT_RET(count > std::numeric_limits<uint64_t>::max() / typeSize,
        HCCL_ERROR("[%s] reduce length overflow.", __func__), HCCL_E_PARA);
    len = count * typeSize;
    reduceIn = Hccl::PodReduceIn(innerType, innerOp);
    return HCCL_SUCCESS;
}

} // namespace

HcclResult HcommPodWrite(HcommThreadEntity &thread, HcommChannelEntity &channel,
                         uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    if (IsUbProtocol(channel.protocol)) {
        return ExecuteUbRma(thread, channel, localAddr, remoteAddr, len,
            [](Hccl::PodUbConnLite &connection, const Hccl::PodRmaBufSliceLite &local,
               const Hccl::PodRmtRmaBufSliceLite &remote, const Hccl::PodSqeConfigLite &cfg,
               Hccl::PodConnLiteOperationOut &out) { connection.WritePod(local, remote, cfg, out); });
    }
    return ExecuteRdma(thread, channel, RdmaOperation::WRITE, localAddr, remoteAddr, len, nullptr, 0);
}

HcclResult HcommPodWriteReduce(HcommThreadEntity &thread, HcommChannelEntity &channel,
                               uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                               const Hccl::PodReduceIn &reduceIn)
{
    if (IsUbProtocol(channel.protocol)) {
        return ExecuteUbRma(thread, channel, localAddr, remoteAddr, len,
            [&reduceIn](Hccl::PodUbConnLite &connection, const Hccl::PodRmaBufSliceLite &local,
                const Hccl::PodRmtRmaBufSliceLite &remote, const Hccl::PodSqeConfigLite &cfg,
                Hccl::PodConnLiteOperationOut &out) {
                connection.WriteReducePod(reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out);
            });
    }
    return ExecuteRdma(thread, channel, RdmaOperation::WRITE_REDUCE,
        localAddr, remoteAddr, len, &reduceIn, 0);
}

HcclResult HcommPodWriteWithNotify(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                   uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                                   uint32_t remoteNotifyIdx)
{
    if (IsUbProtocol(channel.protocol)) {
        Hccl::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(BuildUbRemoteNotify(channel, remoteNotifyIdx, notify));
        return ExecuteUbRma(thread, channel, localAddr, remoteAddr, len,
            [&notify](Hccl::PodUbConnLite &connection, const Hccl::PodRmaBufSliceLite &local,
                const Hccl::PodRmtRmaBufSliceLite &remote, const Hccl::PodSqeConfigLite &cfg,
                Hccl::PodConnLiteOperationOut &out) {
                connection.WriteWithNotifyPod(local, remote, cfg, out, notify, POD_NOTIFY_VALUE);
            });
    }
    return ExecuteRdma(thread, channel, RdmaOperation::WRITE_WITH_NOTIFY,
        localAddr, remoteAddr, len, nullptr, remoteNotifyIdx);
}

HcclResult HcommPodWriteReduceWithNotify(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                         uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                                         const Hccl::PodReduceIn &reduceIn, uint32_t remoteNotifyIdx)
{
    if (IsUbProtocol(channel.protocol)) {
        Hccl::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(BuildUbRemoteNotify(channel, remoteNotifyIdx, notify));
        return ExecuteUbRma(thread, channel, localAddr, remoteAddr, len,
            [&reduceIn, &notify](Hccl::PodUbConnLite &connection, const Hccl::PodRmaBufSliceLite &local,
                const Hccl::PodRmtRmaBufSliceLite &remote, const Hccl::PodSqeConfigLite &cfg,
                Hccl::PodConnLiteOperationOut &out) {
                connection.WriteReduceWithNotifyPod(reduceIn.dataType, reduceIn.reduceOp,
                    local, remote, cfg, out, notify, POD_NOTIFY_VALUE);
            });
    }
    return ExecuteRdma(thread, channel, RdmaOperation::WRITE_REDUCE_WITH_NOTIFY,
        localAddr, remoteAddr, len, &reduceIn, remoteNotifyIdx);
}

HcclResult HcommPodRead(HcommThreadEntity &thread, HcommChannelEntity &channel,
                        uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    if (IsUbProtocol(channel.protocol)) {
        return ExecuteUbRma(thread, channel, localAddr, remoteAddr, len,
            [](Hccl::PodUbConnLite &connection, const Hccl::PodRmaBufSliceLite &local,
               const Hccl::PodRmtRmaBufSliceLite &remote, const Hccl::PodSqeConfigLite &cfg,
               Hccl::PodConnLiteOperationOut &out) { connection.ReadPod(local, remote, cfg, out); });
    }
    return ExecuteRdma(thread, channel, RdmaOperation::READ, localAddr, remoteAddr, len, nullptr, 0);
}

HcclResult HcommPodReadReduce(HcommThreadEntity &thread, HcommChannelEntity &channel,
                              uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                              const Hccl::PodReduceIn &reduceIn)
{
    if (IsUbProtocol(channel.protocol)) {
        return ExecuteUbRma(thread, channel, localAddr, remoteAddr, len,
            [&reduceIn](Hccl::PodUbConnLite &connection, const Hccl::PodRmaBufSliceLite &local,
                const Hccl::PodRmtRmaBufSliceLite &remote, const Hccl::PodSqeConfigLite &cfg,
                Hccl::PodConnLiteOperationOut &out) {
                connection.ReadReducePod(reduceIn, local, remote, cfg, out);
            });
    }
    // The original RoCE transport does not override ReadReduce and therefore performs no operation.
    return channel.protocol == COMM_PROTOCOL_ROCE ? HCCL_SUCCESS : HCCL_E_NOT_SUPPORT;
}

HcclResult HcommPodNotifyRecord(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                uint32_t remoteNotifyIdx)
{
    if (!IsUbProtocol(channel.protocol)) {
        return ExecuteRdma(thread, channel, RdmaOperation::NOTIFY_RECORD, 0, 0, 0, nullptr, remoteNotifyIdx);
    }
    HcommJfsContext *jfs = nullptr;
    CHK_RET(GetUbContext(channel, jfs));
    Hccl::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(BuildUbRemoteNotify(channel, remoteNotifyIdx, notify));
    Hccl::PodUbConnLiteParam param{};
    FillUbParam(*jfs, param);
    Hccl::PodUbConnLite connection;
    connection.ImportPodContext(param);
    ImportUbState(*jfs, connection);
    Hccl::PodSqeConfigLite cfg;
    if (remoteNotifyIdx == 1) {
        cfg.cqeEn = true;
        cfg.placeOdr = UB_STRONG_ORDER;
        cfg.compOrder = UB_COMPLETION;
    }
    Hccl::PodConnLiteOperationOut out{};
    const uint32_t value = POD_NOTIFY_VALUE;
    connection.InlineWritePod(reinterpret_cast<const uint8_t *>(&value), POD_NOTIFY_SIZE, notify, cfg, out);
    ExportUbState(connection, *jfs);
    return RingUbDoorbell(thread, *jfs, out.pi);
}

HcclResult HcommPodNotifyWait(HcommThreadEntity &thread, const HcommChannelEntity &channel,
                              uint32_t localNotifyIdx, uint32_t timeout)
{
    CHK_PRT_RET(localNotifyIdx >= channel.localNotifyNum || channel.localNotifyAddr == nullptr,
        HCCL_ERROR("[%s] local notify index[%u] out of range[%u].", __func__, localNotifyIdx,
            channel.localNotifyNum), HCCL_E_PARA);
    HcommRtsqContext *rtsq = nullptr;
    CHK_RET(GetRtsq(thread, rtsq));
    CHK_PRT_RET(channel.localNotifyAddr[localNotifyIdx].type != HCOMM_NOTIFY_TYPE_LOCAL,
        HCCL_ERROR("[%s] local notify type[%u] is not LOCAL.", __func__,
            channel.localNotifyAddr[localNotifyIdx].type), HCCL_E_PARA);
    const int32_t notifyId = channel.localNotifyAddr[localNotifyIdx].notify.local.notifyId;
    CHK_PRT_RET(notifyId < 0, HCCL_ERROR("[%s] invalid notify id[%d].", __func__, notifyId), HCCL_E_PARA);
    return HcommPodRtsqNotifyWait(*rtsq, static_cast<uint32_t>(notifyId), timeout);
}

HcclResult HcommPodFence(HcommChannelEntity &channel)
{
    channel.fence = 1;
    return HCCL_SUCCESS;
}

HcclResult HcommPodDrain(HcommThreadEntity &thread, HcommChannelEntity &channel)
{
    if (channel.protocol == COMM_PROTOCOL_ROCE) {
        return HCCL_SUCCESS;
    }
    CHK_PRT_RET(!IsUbProtocol(channel.protocol),
        HCCL_ERROR("[%s] protocol[%d] is not supported.", __func__, channel.protocol), HCCL_E_NOT_SUPPORT);
    if (channel.drainNotify.size == 0 || channel.remoteDrainBuffer.size == 0) {
        HCCL_WARNING("[%s] drain resource is empty; skip.", __func__);
        return HCCL_SUCCESS;
    }
    HcommJfsContext *jfs = nullptr;
    CHK_RET(GetUbContext(channel, jfs));
    const HcommRmaNotify &localResource = channel.drainNotify;
    const HcommRmaNotify &remoteResource = channel.remoteDrainBuffer;
    CHK_PRT_RET(localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
        remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        HCCL_ERROR("[%s] UB drain requires URMA protection.", __func__), HCCL_E_PARA);
    Hccl::PodRmaBufSliceLite local(localResource.address, localResource.size, 0,
        localResource.protectionInfo.protection.urma.tokenId);
    Hccl::PodRmtRmaBufSliceLite remote(remoteResource.address, remoteResource.size, 0,
        remoteResource.protectionInfo.protection.urma.tokenId,
        remoteResource.protectionInfo.protection.urma.tokenValue,
        std::numeric_limits<uint32_t>::max());
    Hccl::PodUbConnLiteParam param{};
    FillUbParam(*jfs, param);
    Hccl::PodUbConnLite connection;
    connection.ImportPodContext(param);
    ImportUbState(*jfs, connection);
    Hccl::PodSqeConfigLite cfg;
    cfg.fence = 1;
    cfg.placeOdr = UB_STRONG_ORDER;
    cfg.compOrder = UB_COMPLETION;
    channel.fence = 0;
    Hccl::PodConnLiteOperationOut out{};
    connection.ReadPod(local, remote, cfg, out);
    ExportUbState(connection, *jfs);
    CHK_RET(RingUbDoorbell(thread, *jfs, out.pi));
    HcommRtsqContext *rtsq = nullptr;
    CHK_RET(GetRtsq(thread, rtsq));
    CHK_PRT_RET(localResource.notifyId < 0,
        HCCL_ERROR("[%s] invalid drain notify id[%d].", __func__, localResource.notifyId), HCCL_E_PARA);
    return HcommPodRtsqNotifyWait(*rtsq, static_cast<uint32_t>(localResource.notifyId), POD_DEFAULT_TIMEOUT);
}

HcclResult HcommPodBatchTransfer(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                 const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum)
{
    CHK_PTR_NULL(transferDescs);
    CHK_PRT_RET(transferDescNum == 0, HCCL_ERROR("[%s] transferDescNum is 0.", __func__), HCCL_E_PARA);
    HcommJfsContext *jfs = nullptr;
    CHK_RET(GetUbContext(channel, jfs));
    Hccl::PodUbConnLiteParam param{};
    FillUbParam(*jfs, param);
    Hccl::PodUbConnLite connection;
    connection.ImportPodContext(param);
    ImportUbState(*jfs, connection);
    Hccl::PodSqeConfigLite cfg = GetUbConfig(channel);
    Hccl::PodConnLiteOperationOut out{};

    for (uint32_t index = 0; index < transferDescNum; ++index) {
        const HcommBatchTransferDesc &desc = transferDescs[index];
        cfg.cqeEn = index == transferDescNum - 1;
        cfg.placeOdr = cfg.cqeEn ? UB_STRONG_ORDER : UB_RELAX_ORDER;
        cfg.compOrder = cfg.cqeEn ? UB_COMPLETION : UB_NO_COMPLETION;
        if (desc.transType == HCOMM_TRANSFER_TYPE_NOTIFY_RECORD) {
            Hccl::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
            CHK_RET(BuildUbRemoteNotify(channel, desc.transferInfo.notifyRecord.notifyIdx, notify));
            const uint32_t value = POD_NOTIFY_VALUE;
            connection.InlineWritePod(reinterpret_cast<const uint8_t *>(&value), POD_NOTIFY_SIZE,
                notify, cfg, out);
            continue;
        }

        uintptr_t localAddr = 0;
        uintptr_t remoteAddr = 0;
        uint64_t len = 0;
        uint32_t notifyIdx = 0;
        Hccl::PodReduceIn reduceIn{Hccl::PodDataType::INVALID, Hccl::PodReduceOp::INVALID};
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
            CHK_RET(ConvertBatchReduce(desc.transferInfo.reduce.dataType, desc.transferInfo.reduce.reduceOp,
                reduceIn, desc.transferInfo.reduce.count, len));
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeReduceWithNotify.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeReduceWithNotify.dst);
            notifyIdx = desc.transferInfo.writeReduceWithNotify.notifyIdx;
            CHK_RET(ConvertBatchReduce(desc.transferInfo.writeReduceWithNotify.dataType,
                desc.transferInfo.writeReduceWithNotify.reduceOp, reduceIn,
                desc.transferInfo.writeReduceWithNotify.count, len));
        } else {
            HCCL_ERROR("[%s] unsupported transfer type[%d].", __func__, desc.transType);
            return HCCL_E_PARA;
        }
        CHK_PRT_RET(localAddr == 0 || remoteAddr == 0,
            HCCL_ERROR("[%s] null address at batch index[%u].", __func__, index), HCCL_E_PTR);
        Hccl::PodRmaBufSliceLite local(0, 0, 0, 0);
        Hccl::PodRmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(BuildUbSlices(channel, localAddr, remoteAddr, len, local, remote));
        if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE) {
            connection.WritePod(local, remote, cfg, out);
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_READ) {
            connection.ReadPod(local, remote, cfg, out);
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE_REDUCE) {
            connection.WriteReducePod(reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out);
        } else if (desc.transType == HCOMM_TRANSFER_TYPE_READ_REDUCE) {
            connection.ReadReducePod(reduceIn, local, remote, cfg, out);
        } else {
            Hccl::PodRmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
            CHK_RET(BuildUbRemoteNotify(channel, notifyIdx, notify));
            if (desc.transType == HCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY) {
                connection.WriteWithNotifyPod(local, remote, cfg, out, notify, POD_NOTIFY_VALUE);
            } else {
                connection.WriteReduceWithNotifyPod(reduceIn.dataType, reduceIn.reduceOp,
                    local, remote, cfg, out, notify, POD_NOTIFY_VALUE);
            }
        }
    }
    ExportUbState(connection, *jfs);
    return RingUbDoorbell(thread, *jfs, out.pi);
}
