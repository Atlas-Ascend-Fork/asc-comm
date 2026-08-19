/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "ub_conn_lite.h"
#include "log/hcomm_log.h"
#include "exception_util.h"
#include "udma_data_struct.h"
#include "internal_exception.h"
#include "invalid_params_exception.h"
#include "string_util.h"

namespace Hccl {
constexpr u32 ADDR_BIT_OFFSET            = 32;
constexpr u32 SQE_SIZE_128               = 128;
constexpr u32 SQE_SIZE_64                = 64;
constexpr u32 SQE_INLINE_DATA_SIZE       = 16;
constexpr u32 RAW_SIZE                   = 16;
constexpr u32 RMT_EID_BYTE_SIZE          = 16;
constexpr u32 PI_NUM_TWO                 = 2;
constexpr u32 WRITE_WITH_NOTIFY_OPCODE   = 0x5;
constexpr u32 ADDR_BIT_LOW               = 0xffffffff;
constexpr u32 UB_RELAX_ORDER             = 0x1; // Relax Order表示当前SQE与后续Strong Order SQE有保序要求
constexpr u32 UB_STRONG_ORDER            = 0x2; // Strong Order表示当前SQE有保序要求，该SQE不能超越前面的Relax Order SQE

u32 GetDataTypeSize(PodDataType dataType)
{
    switch (dataType) {
        case PodDataType::INT8:
        case PodDataType::UINT8:
        case PodDataType::HIF8:
        case PodDataType::FP8E4M3:
        case PodDataType::FP8E5M2:
        case PodDataType::FP8E8M0:
            return 1;
        case PodDataType::INT16:
        case PodDataType::UINT16:
        case PodDataType::FP16:
        case PodDataType::BFP16:
        case PodDataType::BF16_SAT:
            return 2;
        case PodDataType::INT32:
        case PodDataType::UINT32:
        case PodDataType::FP32:
            return 4;
        case PodDataType::INT64:
        case PodDataType::UINT64:
        case PodDataType::FP64:
            return 8;
        case PodDataType::INT128:
            return 16;
        default:
            THROW<InvalidParamsException>(StringFormat("unsupported data type[%u]", static_cast<u32>(dataType)));
    }
}

void PodUbConnLite::FillCommSqe(PodUdmaSqeCommon *sqe, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg, u32 opCode,
                             SlicePosition slicePos)
{
    u32 cqeEn = (cfg.cqeEn && (slicePos == SlicePosition::LAST || slicePos == SlicePosition::ONLY)) ? 1 : 0;
    sqe->cqe       = cqeEn;
    sqe->owner     = (pi == (sqDepth_ - 1)) ? 1 : 0;
    sqe->opcode    = opCode;
    sqe->tpn       = tpn_;

    // 当前片是ONLY片(只有一片的情况)和最后一片的情况，全严格保序
    if (slicePos == SlicePosition::ONLY || slicePos == SlicePosition::LAST) {
        sqe->placeOdr = UB_STRONG_ORDER;
        sqe->compOrder = 1;
        sqe->fence = 1;
    } else {
        // 中间片写死配置，第一片由全局cfg配置
        sqe->placeOdr = (slicePos == SlicePosition::MIDDLE) ? UB_RELAX_ORDER : cfg.placeOdr;
        sqe->compOrder = (slicePos == SlicePosition::MIDDLE) ? 0 : cfg.compOrder;
        sqe->fence = (slicePos == SlicePosition::MIDDLE) ? 0 : cfg.fence;
    }

    sqe->se           = 1; // 表示是否使能solicited event
    sqe->rmtJettyType = 1; // 00 JFR  01:JETTY  10:jettyGroup 11:reserved
    s32 ret           = memcpy_sp(sqe->rmtEid, RMT_EID_BYTE_SIZE, rmtEid_, RAW_SIZE);
    if (UNLIKELY(ret != 0)) {
        HCCL_ERROR("PodUbConnLite::FillCommSqe FillCommSqe memcpy failed, ret=%d", ret);
        THROW<InternalException>(StringFormat("PodUbConnLite::FillCommSqe memcpy_sp failed, ret = %d", ret));
    }

    sqe->sgeNum        = 1;
    sqe->targetHint    = 0;
    sqe->rmtObjId      = rmt.GetTokenId();
    sqe->tokenEn       = 1;
    sqe->rmtTokenValue = rmt.GetTokenValue();
    sqe->rmtAddrLow    = rmt.GetAddr() & ADDR_BIT_LOW;
    sqe->rmtAddrHigh   = rmt.GetAddr() >> ADDR_BIT_OFFSET;
    HCCL_INFO("UbConnLite FillCommSqe PodUdmaSqeCommon slicePos[%d] sqe->cqe = %u, sqe->owner = %u sqe->opcode = %u, "
              "sqe->tpn = %u, sqe->rmtObjId = %u, sqe->rmtAddrLow = %u, sqe->rmtAddrHigh = %u, sqe->placeOdr = %u, "
              "sqe->compOrder = %u, sqe->fence = %u", slicePos, sqe->cqe, sqe->owner, sqe->opcode, sqe->tpn,
              sqe->rmtObjId, sqe->rmtAddrLow, sqe->rmtAddrHigh, sqe->placeOdr, sqe->compOrder, sqe->fence);
}

void PodUbConnLite::FillCommSqeReduceInfo(PodUdmaSqeCommon &sqeComm, PodReduceOp reduceOp, PodDataType dataType, u32 udfType) const
{
    HCCL_INFO("[PodUbConnLite::%s] start", __func__);

    sqeComm.inlinedata.udfData.udfType    = udfType; // 0代表inline reduce

    switch (reduceOp) {
        case PodReduceOp::SUM: sqeComm.inlinedata.udfData.reduceOp = 0xA; break;
        case PodReduceOp::MAX: sqeComm.inlinedata.udfData.reduceOp = 0x8; break;
        case PodReduceOp::MIN: sqeComm.inlinedata.udfData.reduceOp = 0x9; break;
        default:
            THROW<InvalidParamsException>(StringFormat("%s reduceOp[%u] is not supported.", __func__,
                static_cast<u32>(reduceOp)));
    }
    switch (dataType) {
        case PodDataType::INT8: sqeComm.inlinedata.udfData.reduceType = 0x0; break;
        case PodDataType::INT16: sqeComm.inlinedata.udfData.reduceType = 0x1; break;
        case PodDataType::INT32: sqeComm.inlinedata.udfData.reduceType = 0x2; break;
        case PodDataType::UINT8: sqeComm.inlinedata.udfData.reduceType = 0x3; break;
        case PodDataType::UINT16: sqeComm.inlinedata.udfData.reduceType = 0x4; break;
        case PodDataType::UINT32: sqeComm.inlinedata.udfData.reduceType = 0x5; break;
        case PodDataType::FP16: sqeComm.inlinedata.udfData.reduceType = 0x6; break;
        case PodDataType::FP32: sqeComm.inlinedata.udfData.reduceType = 0x7; break;
        case PodDataType::BFP16: sqeComm.inlinedata.udfData.reduceType = 0x8; break;
        case PodDataType::BF16_SAT: sqeComm.inlinedata.udfData.reduceType = 0x9; break;
        default:
            THROW<InvalidParamsException>(StringFormat("%s dataType[%u] is not supported.", __func__,
                static_cast<u32>(dataType)));
    }

    // udf字段是否有效
    sqeComm.udfFlag = 1;

    HCCL_INFO("[PodUbConnLite::%s] end, reduceOp[%u], reduceType[%u]", __func__,
        static_cast<u32>(reduceOp), static_cast<u32>(dataType));
}

void PodUbConnLite::ProcessSlices(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, u32 maxSliceSize,
    std::function<void(const PodRmaBufSliceLite &, const PodRmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
    PodDataType dataType) const
{
    (void)dataType;
    // reduce操作需要保证切片大小是数据类型大小的整数倍
    u64 sliceSize = static_cast<u64>(maxSliceSize);

    u64 locBufSize    = loc.GetSize();
    u64 sliceNum      = locBufSize / sliceSize;
    u64 lastSliceSize = locBufSize % sliceSize;

    u64 totalSize = sliceNum * sliceSize;

    if (UNLIKELY(loc.GetAddr() > UINT64_MAX - totalSize || rmt.GetAddr() > UINT64_MAX - totalSize)) {
        THROW<InternalException>("integer overflow occurs");
    }
    for (u64 sliceIdx = 0; sliceIdx < sliceNum; sliceIdx++) {
        u64 offset = sliceIdx * sliceSize;
        u64 locAddr = loc.GetAddr() + offset;
        u64 rmtAddr = rmt.GetAddr() + offset;

        HCCL_INFO("[PodUbConnLite::%s] Slice[%llu]: offset=0x%llx, locAddr=0x%llx, rmtAddr=0x%llx, size=0x%llx",
                    __func__, sliceIdx, offset, locAddr, rmtAddr, sliceSize);

        PodRmaBufSliceLite locSlice(locAddr, sliceSize, 0, loc.GetTokenId());

        PodRmtRmaBufSliceLite rmtSlice(rmtAddr, sliceSize, 0, rmt.GetTokenId(),
                                    rmt.GetTokenValue(), UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceIdx == 0) ? SlicePosition::FIRST : SlicePosition::MIDDLE;
        if ((sliceIdx == sliceNum - 1) && lastSliceSize == 0) {
            // SlicePosition::ONLY表示既是首片又是尾片的情况，只有一片的情况
            slicePos = (sliceIdx == 0) ? SlicePosition::ONLY : SlicePosition::LAST;
        }
        processOneSlice(locSlice, rmtSlice, slicePos);
    }

    if (lastSliceSize > 0) {
        PodRmaBufSliceLite lastLocSlice(loc.GetAddr() + sliceNum * sliceSize, lastSliceSize, 0, loc.GetTokenId());

        PodRmtRmaBufSliceLite lastRmtSlice(rmt.GetAddr() + sliceNum * sliceSize, lastSliceSize, 0, rmt.GetTokenId(),
                                        rmt.GetTokenValue(), UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceNum == 0) ? SlicePosition::ONLY : SlicePosition::LAST;
        processOneSlice(lastLocSlice, lastRmtSlice, slicePos);
        sliceNum++;
    }

    HCCL_INFO("[PodUbConnLite::%s] end, locBufSize[%u], sliceNUm[%u], sliceSize[%u], lastSliceSize[%u]", __func__,
              locBufSize, sliceNum, sliceSize, lastSliceSize);
}

void PodUbConnLite::ProcessSlicesWithNotify(
    const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, u32 maxSliceSize,
    std::function<void(const PodRmaBufSliceLite &, const PodRmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
    std::function<void(const PodRmaBufSliceLite &, const PodRmtRmaBufSliceLite &, SlicePosition)> processOneSliceWithNotify,
    PodDataType                                                                 dataType) const
{
    HCCL_INFO("[PodUbConnLite::%s] start", __func__);

    // reduce操作需要保证切片大小是数据类型大小的整数倍
    u32 sliceSize = maxSliceSize;
    if (dataType != PodDataType::INVALID) {
        u32 dataTypeSize = GetDataTypeSize(dataType);
        sliceSize        = maxSliceSize / dataTypeSize * dataTypeSize;
    }

    u32 locBufSize    = loc.GetSize();
    u32 sliceNum      = locBufSize / sliceSize;
    u32 lastSliceSize = locBufSize % sliceSize;
    if (sliceNum > 0 && lastSliceSize == 0) {
        sliceNum--;
        lastSliceSize = sliceSize;
    }
    u64 totalSize = static_cast<u64>(sliceNum) * static_cast<u64>(sliceSize);
    if (UNLIKELY(loc.GetAddr() > UINT64_MAX - totalSize || rmt.GetAddr() > UINT64_MAX - totalSize)) {
        THROW<InternalException>("integer overflow occurs");
    }
    for (u32 sliceIdx = 0; sliceIdx < sliceNum; sliceIdx++) {
        PodRmaBufSliceLite locSlice(loc.GetAddr() + sliceIdx * sliceSize, sliceSize, 0, loc.GetTokenId());

        PodRmtRmaBufSliceLite rmtSlice(rmt.GetAddr() + sliceIdx * sliceSize, sliceSize, 0, rmt.GetTokenId(),
                                    rmt.GetTokenValue(), UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceIdx == 0) ? SlicePosition::FIRST : SlicePosition::MIDDLE;
        processOneSlice(locSlice, rmtSlice, slicePos);
    }

    if (lastSliceSize > 0) {
        PodRmaBufSliceLite lastLocSlice(loc.GetAddr() + sliceNum * sliceSize, lastSliceSize, 0, loc.GetTokenId());

        PodRmtRmaBufSliceLite lastRmtSlice(rmt.GetAddr() + sliceNum * sliceSize, lastSliceSize, 0, rmt.GetTokenId(),
                                        rmt.GetTokenValue(), UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceNum == 0) ? SlicePosition::ONLY : SlicePosition::LAST;
        processOneSliceWithNotify(lastLocSlice, lastRmtSlice, slicePos);
    }

    HCCL_INFO("[PodUbConnLite::%s] end, locBufSize[%u], sliceNUm[%u], sliceSize[%u], lastSliceSize[%u]", __func__,
              locBufSize, sliceNum, sliceSize, lastSliceSize);
}

void PodUbConnLite::FillOneSqeWrite(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                                  PodUdmaSqeWrite *sqe, PodUdmaSqOpcode opCode, SlicePosition slicePos)
{
    HCCL_INFO("[PodUbConnLite::%s] start, loc size[%llu]", __func__, loc.GetSize());

    sqe->comm.inlineEn = 0;
    FillCommSqe(&(sqe->comm), rmt, cfg, opCode, slicePos);
    FillLocalSgeSqe(&(sqe->u.sge), loc);
    if (sqe->u.sge.length == 0) {
        sqe->comm.sgeNum = 0;
    }

    HCCL_INFO("[PodUbConnLite::%s] end", __func__);
}

void PodUbConnLite::ProcessOneWqe(PodUdmaSqeWrite *sqe, PodUdmaSqOpcode opCode)
{
    HCCL_INFO("[PodUbConnLite::%s] start, opCode[%u]", __func__, static_cast<u32>(opCode));

    // sqOffset是用于计算Ubjetty中下wqe位置的偏移，小于sqDepth
    u32 sqOffset = pi % sqDepth_;
    if (sqOffset < sqDepth_ && (sqOffset + 1) >= sqDepth_) {
        piDetourCount++;
    }
    // pi维护用于传入DB Send用于Rtsq 敲door bell，要求u16数据结构并且自然增长
    pi = pi + 1;

    // 写wqe到va
    u8 *va = reinterpret_cast<u8 *>(sqVa_ + sqOffset * SQE_SIZE_64);
    if (!dwqeCacheLocked_) {
        auto ret = memcpy_sp(va, SQE_SIZE_64, sqe, SQE_SIZE_64);
        if (UNLIKELY(ret != 0)) {
            THROW<InternalException>(StringFormat("[PodUbConnLite::%s] memcpy_sp failed, ret = %d", __func__, ret));
        }
    }

    HCCL_INFO("[PodUbConnLite::%s] end, pi[%u], ci[%u]", __func__, pi, ci);
}

void PodUbConnLite::ProcessOneWqeWithNotify(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt,
                                         const PodSqeConfigLite &cfg, PodUdmaSqeWriteWithNotify *sqe,
                                         const PodRmtRmaBufSliceLite &notify, u64 notifyData, u32 opCode,
                                         SlicePosition slicePos)
{
    HCCL_INFO("[PodUbConnLite::%s] start, locSize[%u], opCode[%u]", __func__, loc.GetSize(), opCode);

    // sqOffset是用于计算Ubjetty中下wqe位置的偏移，小于sqDepth
    u32 sqOffset = pi % sqDepth_;
    if (sqOffset < sqDepth_ && (sqOffset + PI_NUM_TWO) >= sqDepth_) {
        piDetourCount++;
    }
    // pi维护用于传入DB Send用于Rtsq 敲door bell，要求u16数据结构并且自然增长
    pi = pi + PI_NUM_TWO;
    // 填充sqe
    sqe->comm.inlineEn = 0;
    FillCommSqe(&(sqe->comm), rmt, cfg, WRITE_WITH_NOTIFY_OPCODE, slicePos);
    FillNotifySqe(&(sqe->notify), notify, notifyData);
    FillLocalSgeSqe(&(sqe->localU.sge), loc);
    if (sqe->localU.sge.length == 0) {
        sqe->comm.sgeNum = 0;
    }
    sqe->rsv1 = 0;
    sqe->rsv2 = 0;

    u8 *va = reinterpret_cast<u8 *>((sqVa_) + sqOffset * SQE_SIZE_64);
    if (!dwqeCacheLocked_) {
        // 带notify的wqe是96字节, 需要占用两个wqebb, 实际占用128字节
        if (sqOffset == sqDepth_ - 1) {
            MemorySetAndCopy(va, SQE_SIZE_64, sqe);
            va  = reinterpret_cast<u8 *>(sqVa_);
            MemorySetAndCopy(va, SQE_SIZE_64, reinterpret_cast<u8 *>(sqe) + SQE_SIZE_64);
        } else {
            MemorySetAndCopy(va, SQE_SIZE_128, sqe);
        }
    }

    HCCL_INFO("[PodUbConnLite::%s] end, pi[%u], ci[%u]", __func__, pi, ci);
}

void PodUbConnLite::MemorySetAndCopy(u8 *va, u32 sqeSize, void *sqe)
{
    auto ret = memset_s(va, sqeSize, 0, sqeSize);
    if (UNLIKELY(ret != 0)) {
        THROW<InternalException>(StringFormat("[PodUbConnLite::%s] memset fail, ret = %d", __func__, ret));
    }
    ret = memcpy_sp(va, sqeSize, sqe, sqeSize);
    if (UNLIKELY(ret != 0)) {
        THROW<InternalException>(StringFormat("[PodUbConnLite::%s] not support this op type yet.", __func__));
    }
}

void PodUbConnLite::FillNotifySqe(struct PodUdmaSqeNotify *sqe, const PodRmtRmaBufSliceLite &notify, u64 notifyData) const
{
    sqe->notifyTokenId    = notify.GetTokenId();
    sqe->notifyTokenValue = notify.GetTokenValue();
    sqe->notifyAddrLow    = notify.GetAddr() & ADDR_BIT_LOW;
    sqe->notifyAddrHigh   = notify.GetAddr() >> ADDR_BIT_OFFSET;
    sqe->notifyDataLow    = notifyData & ADDR_BIT_LOW;
    sqe->notifyDataHigh   = notifyData >> ADDR_BIT_OFFSET;
    HCCL_INFO("UbConnLite FillNotifySqe sqe->notifyAddrLow = %u "
              "sqe->notifyAddrHigh = %u, sqe->notifyDataLow = %u, sqe->notifyDataHigh = %u",
              sqe->notifyAddrLow, sqe->notifyAddrHigh, sqe->notifyDataLow, sqe->notifyDataHigh);
}

void PodUbConnLite::FillLocalSgeSqe(PodUdmaNormalSge *sqe, const PodRmaBufSliceLite &loc) const
{
    sqe->length       = loc.GetSize();
    sqe->tokenId      = loc.GetTokenId();
    sqe->dataAddrLow  = loc.GetAddr() & ADDR_BIT_LOW;
    sqe->dataAddrHigh = loc.GetAddr() >> ADDR_BIT_OFFSET;
    HCCL_INFO("UbConnLite FillLocalSgeSqe sqe->length = %u, sqe->dataAddrLow = %u "
              "sqe->dataAddrHigh = %u",
              sqe->length, sqe->dataAddrLow, sqe->dataAddrHigh);
}

void PodUbConnLite::ReadPod(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt,
                         const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out)
{
    ProcessSlices(loc, rmt, maxReadSize,
        [&](const PodRmaBufSliceLite &locSlice, const PodRmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            PodUdmaSqeWrite sqe{};
            FillOneSqeWrite(locSlice, rmtSlice, cfg, &sqe, PodUdmaSqOpcode::UDMA_OPC_READ, slicePos);
            ProcessOneWqe(&sqe, PodUdmaSqOpcode::UDMA_OPC_READ);
        });
    out.pi = pi;
}

void PodUbConnLite::ReadReducePod(PodReduceIn reduceIn, const PodRmaBufSliceLite &loc,
                               const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                               PodConnLiteOperationOut &out)
{
    ProcessSlices(loc, rmt, maxReadSize,
        [&](const PodRmaBufSliceLite &locSlice, const PodRmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            PodUdmaSqeWrite sqe{};
            FillOneSqeWrite(locSlice, rmtSlice, cfg, &sqe, PodUdmaSqOpcode::UDMA_OPC_READ, slicePos);
            FillCommSqeReduceInfo(sqe.comm, reduceIn.reduceOp, reduceIn.dataType);
            ProcessOneWqe(&sqe, PodUdmaSqOpcode::UDMA_OPC_READ);
        }, reduceIn.dataType);
    out.pi = pi;
}

void PodUbConnLite::WritePod(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt,
                          const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out)
{
    ProcessSlices(loc, rmt, maxWriteSize,
        [&](const PodRmaBufSliceLite &locSlice, const PodRmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            PodUdmaSqeWrite sqe{};
            FillOneSqeWrite(locSlice, rmtSlice, cfg, &sqe, PodUdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            ProcessOneWqe(&sqe, PodUdmaSqOpcode::UDMA_OPC_WRITE);
        });
    out.pi = pi;
}

void PodUbConnLite::InlineWritePod(const u8 *data, u16 size, const PodRmtRmaBufSliceLite &rmt,
                                const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out)
{
    PodUdmaSqeWrite sqe{};
    sqe.comm.inlineEn = 1;
    sqe.comm.inlineMsgLen = size;
    FillCommSqe(&sqe.comm, rmt, cfg, PodUdmaSqOpcode::UDMA_OPC_WRITE);
    auto ret = memcpy_sp(sqe.u.inlineData.data, SQE_INLINE_DATA_SIZE, data, size);
    if (UNLIKELY(ret != 0)) {
        THROW<InternalException>(StringFormat("[PodUbConnLite::%s] memcpy_sp failed, ret = %d", __func__, ret));
    }
    ProcessOneWqe(&sqe, PodUdmaSqOpcode::UDMA_OPC_WRITE);
    out.pi = pi;
}

void PodUbConnLite::WriteReducePod(PodDataType dataType, PodReduceOp reduceOp, const PodRmaBufSliceLite &loc,
                                const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                                PodConnLiteOperationOut &out)
{
    ProcessSlices(loc, rmt, maxWriteSize,
        [&](const PodRmaBufSliceLite &locSlice, const PodRmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            PodUdmaSqeWrite sqe{};
            FillCommSqeReduceInfo(sqe.comm, reduceOp, dataType);
            FillOneSqeWrite(locSlice, rmtSlice, cfg, &sqe, PodUdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            ProcessOneWqe(&sqe, PodUdmaSqOpcode::UDMA_OPC_WRITE);
        }, dataType);
    out.pi = pi;
}

void PodUbConnLite::WriteWithNotifyPod(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt,
                                    const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out,
                                    const PodRmtRmaBufSliceLite &notify, u64 notifyData)
{
    ProcessSlicesWithNotify(loc, rmt, maxWriteSize,
        [&](const PodRmaBufSliceLite &locSlice, const PodRmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            PodUdmaSqeWrite sqe{};
            FillOneSqeWrite(locSlice, rmtSlice, cfg, &sqe, PodUdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            ProcessOneWqe(&sqe, PodUdmaSqOpcode::UDMA_OPC_WRITE);
        },
        [&](const PodRmaBufSliceLite &locSlice, const PodRmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            PodUdmaSqeWriteWithNotify sqe{};
            ProcessOneWqeWithNotify(locSlice, rmtSlice, cfg, &sqe, notify, notifyData,
                                    WRITE_WITH_NOTIFY_OPCODE, slicePos);
        });
    out.pi = pi;
}

void PodUbConnLite::WriteReduceWithNotifyPod(PodDataType dataType, PodReduceOp reduceOp,
                                          const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt,
                                          const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out,
                                          const PodRmtRmaBufSliceLite &notify, u64 notifyData)
{
    ProcessSlicesWithNotify(loc, rmt, maxWriteSize,
        [&](const PodRmaBufSliceLite &locSlice, const PodRmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            PodUdmaSqeWrite sqe{};
            FillCommSqeReduceInfo(sqe.comm, reduceOp, dataType);
            FillOneSqeWrite(locSlice, rmtSlice, cfg, &sqe, PodUdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            ProcessOneWqe(&sqe, PodUdmaSqOpcode::UDMA_OPC_WRITE);
        },
        [&](const PodRmaBufSliceLite &locSlice, const PodRmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            PodUdmaSqeWriteWithNotify sqe{};
            FillCommSqeReduceInfo(sqe.comm, reduceOp, dataType);
            ProcessOneWqeWithNotify(locSlice, rmtSlice, cfg, &sqe, notify, notifyData,
                                    WRITE_WITH_NOTIFY_OPCODE, slicePos);
        }, dataType);
    out.pi = pi;
}

void PodUbConnLite::ImportPodContext(const PodUbConnLiteParam &liteParam)
{
    dieId_ = liteParam.dieId;
    funcId_ = liteParam.funcId;
    jettyId_ = liteParam.jettyId;
    dbAddr_ = liteParam.dbAddr;
    sqVa_ = liteParam.sqVa;
    sqDepth_ = liteParam.sqDepth;
    dwqeCacheLocked_ = liteParam.dwqeCacheLocked;
    jfcPollMode_ = liteParam.jfcPollMode;
    tpn_ = liteParam.tpn;
    maxReadSize = liteParam.maxReadSize;
    maxWriteSize = liteParam.maxWriteSize;
    (void)memcpy_sp(rmtEid_, sizeof(rmtEid_), liteParam.rmtEid, sizeof(liteParam.rmtEid));
    (void)memcpy_sp(locEid_, sizeof(locEid_), liteParam.locEid, sizeof(liteParam.locEid));
}

void PodUbConnLite::ImportQueueState(u16 queuePi, u16 queueCi, u32 queuePiDetourCount,
                                  u32 queueCiDetourCount)
{
    pi = queuePi;
    ci = queueCi;
    piDetourCount = queuePiDetourCount;
    ciDetourCount = queueCiDetourCount;
}

void PodUbConnLite::ExportQueueState(u16 &queuePi, u16 &queueCi, u32 &queuePiDetourCount,
                                  u32 &queueCiDetourCount) const
{
    queuePi = pi;
    queueCi = ci;
    queuePiDetourCount = piDetourCount;
    queueCiDetourCount = ciDetourCount;
}

} // namespace Hccl
