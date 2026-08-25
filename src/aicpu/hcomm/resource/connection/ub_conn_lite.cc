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
#include "../../common/utils/exception_util.h"
#include "udma_data_struct.h"
#include "../../common/exception/internal_exception.h"
#include "../../common/exception/invalid_params_exception.h"
#include "../../common/utils/string_util.h"

namespace Asc {
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

u32 get_data_type_size(DataType dataType)
{
    switch (dataType) {
        case DataType::INT8:
        case DataType::UINT8:
        case DataType::HIF8:
        case DataType::FP8E4M3:
        case DataType::FP8E5M2:
        case DataType::FP8E8M0:
            return 1;
        case DataType::INT16:
        case DataType::UINT16:
        case DataType::FP16:
        case DataType::BFP16:
        case DataType::BF16_SAT:
            return 2;
        case DataType::INT32:
        case DataType::UINT32:
        case DataType::FP32:
            return 4;
        case DataType::INT64:
        case DataType::UINT64:
        case DataType::FP64:
            return 8;
        case DataType::INT128:
            return 16;
        default:
            throw_exception<InvalidParamsException>(string_format("unsupported data type[%u]", static_cast<u32>(dataType)));
    }
}

void UbConnLite::fill_comm_sqe(UdmaSqeCommon *sqe, const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg, u32 opCode,
                             SlicePosition slicePos)
{
    u32 cqeEn = (cfg.cqeEn && (slicePos == SlicePosition::LAST || slicePos == SlicePosition::ONLY)) ? 1 : 0;
    sqe->cqe       = cqeEn;
    sqe->owner     = (pi == (sqDepth_ - 1)) ? 1 : 0;
    sqe->opcode    = opCode;
    sqe->tpn       = tpn_;

    if (cfg.userConfig) {
        sqe->placeOdr = cfg.placeOdr;
        sqe->compOrder = cfg.compOrder;
        sqe->fence = cfg.fence;
    } else {
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
    }

    sqe->se           = 1; // 表示是否使能solicited event
    sqe->rmtJettyType = 1; // 00 JFR  01:JETTY  10:jettyGroup 11:reserved
    s32 ret           = memcpy_sp(sqe->rmtEid, RMT_EID_BYTE_SIZE, rmtEid_, RAW_SIZE);
    if (UNLIKELY(ret != 0)) {
        HCCL_ERROR("UbConnLite::fill_comm_sqe fill_comm_sqe memcpy failed, ret=%d", ret);
        throw_exception<InternalException>(string_format("UbConnLite::fill_comm_sqe memcpy_sp failed, ret = %d", ret));
    }

    sqe->sgeNum        = 1;
    sqe->targetHint    = 0;
    sqe->rmtObjId      = rmt.get_token_id();
    sqe->tokenEn       = 1;
    sqe->rmtTokenValue = rmt.get_token_value();
    sqe->rmtAddrLow    = rmt.get_addr() & ADDR_BIT_LOW;
    sqe->rmtAddrHigh   = rmt.get_addr() >> ADDR_BIT_OFFSET;
    HCCL_INFO("UbConnLite fill_comm_sqe UdmaSqeCommon slicePos[%d] sqe->cqe = %u, sqe->owner = %u sqe->opcode = %u, "
              "sqe->tpn = %u, sqe->rmtObjId = %u, sqe->rmtAddrLow = %u, sqe->rmtAddrHigh = %u, sqe->placeOdr = %u, "
              "sqe->compOrder = %u, sqe->fence = %u", slicePos, sqe->cqe, sqe->owner, sqe->opcode, sqe->tpn,
              sqe->rmtObjId, sqe->rmtAddrLow, sqe->rmtAddrHigh, sqe->placeOdr, sqe->compOrder, sqe->fence);
}

void UbConnLite::fill_comm_sqe_reduce_info(UdmaSqeCommon &sqeComm, ReduceOp reduceOp, DataType dataType, u32 udfType) const
{
    HCCL_INFO("[UbConnLite::%s] start", __func__);

    sqeComm.inlinedata.udfData.udfType    = udfType; // 0代表inline reduce

    switch (reduceOp) {
        case ReduceOp::SUM: sqeComm.inlinedata.udfData.reduceOp = 0xA; break;
        case ReduceOp::MAX: sqeComm.inlinedata.udfData.reduceOp = 0x8; break;
        case ReduceOp::MIN: sqeComm.inlinedata.udfData.reduceOp = 0x9; break;
        default:
            throw_exception<InvalidParamsException>(string_format("%s reduceOp[%u] is not supported.", __func__,
                static_cast<u32>(reduceOp)));
    }
    switch (dataType) {
        case DataType::INT8: sqeComm.inlinedata.udfData.reduceType = 0x0; break;
        case DataType::INT16: sqeComm.inlinedata.udfData.reduceType = 0x1; break;
        case DataType::INT32: sqeComm.inlinedata.udfData.reduceType = 0x2; break;
        case DataType::UINT8: sqeComm.inlinedata.udfData.reduceType = 0x3; break;
        case DataType::UINT16: sqeComm.inlinedata.udfData.reduceType = 0x4; break;
        case DataType::UINT32: sqeComm.inlinedata.udfData.reduceType = 0x5; break;
        case DataType::FP16: sqeComm.inlinedata.udfData.reduceType = 0x6; break;
        case DataType::FP32: sqeComm.inlinedata.udfData.reduceType = 0x7; break;
        case DataType::BFP16: sqeComm.inlinedata.udfData.reduceType = 0x8; break;
        case DataType::BF16_SAT: sqeComm.inlinedata.udfData.reduceType = 0x9; break;
        default:
            throw_exception<InvalidParamsException>(string_format("%s dataType[%u] is not supported.", __func__,
                static_cast<u32>(dataType)));
    }

    // udf字段是否有效
    sqeComm.udfFlag = 1;

    HCCL_INFO("[UbConnLite::%s] end, reduceOp[%u], reduceType[%u]", __func__,
        static_cast<u32>(reduceOp), static_cast<u32>(dataType));
}

void UbConnLite::process_slices(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, u32 maxSliceSize,
    std::function<void(const RmaBufSliceLite &, const RmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
    DataType dataType) const
{
    (void)dataType;
    // reduce操作需要保证切片大小是数据类型大小的整数倍
    u64 sliceSize = static_cast<u64>(maxSliceSize);

    u64 locBufSize    = loc.get_size();
    u64 sliceNum      = locBufSize / sliceSize;
    u64 lastSliceSize = locBufSize % sliceSize;

    u64 totalSize = sliceNum * sliceSize;

    if (UNLIKELY(loc.get_addr() > UINT64_MAX - totalSize || rmt.get_addr() > UINT64_MAX - totalSize)) {
        throw_exception<InternalException>("integer overflow occurs");
    }
    for (u64 sliceIdx = 0; sliceIdx < sliceNum; sliceIdx++) {
        u64 offset = sliceIdx * sliceSize;
        u64 locAddr = loc.get_addr() + offset;
        u64 rmtAddr = rmt.get_addr() + offset;

        HCCL_INFO("[UbConnLite::%s] Slice[%llu]: offset=0x%llx, locAddr=0x%llx, rmtAddr=0x%llx, size=0x%llx",
                    __func__, sliceIdx, offset, locAddr, rmtAddr, sliceSize);

        RmaBufSliceLite locSlice(locAddr, sliceSize, 0, loc.get_token_id());

        RmtRmaBufSliceLite rmtSlice(rmtAddr, sliceSize, 0, rmt.get_token_id(),
                                    rmt.get_token_value(), UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceIdx == 0) ? SlicePosition::FIRST : SlicePosition::MIDDLE;
        if ((sliceIdx == sliceNum - 1) && lastSliceSize == 0) {
            // SlicePosition::ONLY表示既是首片又是尾片的情况，只有一片的情况
            slicePos = (sliceIdx == 0) ? SlicePosition::ONLY : SlicePosition::LAST;
        }
        processOneSlice(locSlice, rmtSlice, slicePos);
    }

    if (lastSliceSize > 0) {
        RmaBufSliceLite lastLocSlice(loc.get_addr() + sliceNum * sliceSize, lastSliceSize, 0, loc.get_token_id());

        RmtRmaBufSliceLite lastRmtSlice(rmt.get_addr() + sliceNum * sliceSize, lastSliceSize, 0, rmt.get_token_id(),
                                        rmt.get_token_value(), UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceNum == 0) ? SlicePosition::ONLY : SlicePosition::LAST;
        processOneSlice(lastLocSlice, lastRmtSlice, slicePos);
        sliceNum++;
    }

    HCCL_INFO("[UbConnLite::%s] end, locBufSize[%u], sliceNUm[%u], sliceSize[%u], lastSliceSize[%u]", __func__,
              locBufSize, sliceNum, sliceSize, lastSliceSize);
}

void UbConnLite::process_slices_with_notify(
    const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, u32 maxSliceSize,
    std::function<void(const RmaBufSliceLite &, const RmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
    std::function<void(const RmaBufSliceLite &, const RmtRmaBufSliceLite &, SlicePosition)> processOneSliceWithNotify,
    DataType                                                                 dataType) const
{
    HCCL_INFO("[UbConnLite::%s] start", __func__);

    // reduce操作需要保证切片大小是数据类型大小的整数倍
    u32 sliceSize = maxSliceSize;
    if (dataType != DataType::INVALID) {
        u32 dataTypeSize = get_data_type_size(dataType);
        sliceSize        = maxSliceSize / dataTypeSize * dataTypeSize;
    }

    u32 locBufSize    = loc.get_size();
    u32 sliceNum      = locBufSize / sliceSize;
    u32 lastSliceSize = locBufSize % sliceSize;
    if (sliceNum > 0 && lastSliceSize == 0) {
        sliceNum--;
        lastSliceSize = sliceSize;
    }
    u64 totalSize = static_cast<u64>(sliceNum) * static_cast<u64>(sliceSize);
    if (UNLIKELY(loc.get_addr() > UINT64_MAX - totalSize || rmt.get_addr() > UINT64_MAX - totalSize)) {
        throw_exception<InternalException>("integer overflow occurs");
    }
    for (u32 sliceIdx = 0; sliceIdx < sliceNum; sliceIdx++) {
        RmaBufSliceLite locSlice(loc.get_addr() + sliceIdx * sliceSize, sliceSize, 0, loc.get_token_id());

        RmtRmaBufSliceLite rmtSlice(rmt.get_addr() + sliceIdx * sliceSize, sliceSize, 0, rmt.get_token_id(),
                                    rmt.get_token_value(), UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceIdx == 0) ? SlicePosition::FIRST : SlicePosition::MIDDLE;
        processOneSlice(locSlice, rmtSlice, slicePos);
    }

    if (lastSliceSize > 0) {
        RmaBufSliceLite lastLocSlice(loc.get_addr() + sliceNum * sliceSize, lastSliceSize, 0, loc.get_token_id());

        RmtRmaBufSliceLite lastRmtSlice(rmt.get_addr() + sliceNum * sliceSize, lastSliceSize, 0, rmt.get_token_id(),
                                        rmt.get_token_value(), UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceNum == 0) ? SlicePosition::ONLY : SlicePosition::LAST;
        processOneSliceWithNotify(lastLocSlice, lastRmtSlice, slicePos);
    }

    HCCL_INFO("[UbConnLite::%s] end, locBufSize[%u], sliceNUm[%u], sliceSize[%u], lastSliceSize[%u]", __func__,
              locBufSize, sliceNum, sliceSize, lastSliceSize);
}

void UbConnLite::fill_one_sqe_write(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg,
                                  UdmaSqeWrite *sqe, UdmaSqOpcode opCode, SlicePosition slicePos)
{
    HCCL_INFO("[UbConnLite::%s] start, loc size[%llu]", __func__, loc.get_size());

    sqe->comm.inlineEn = 0;
    fill_comm_sqe(&(sqe->comm), rmt, cfg, opCode, slicePos);
    fill_local_sge_sqe(&(sqe->u.sge), loc);
    if (sqe->u.sge.length == 0) {
        sqe->comm.sgeNum = 0;
    }

    HCCL_INFO("[UbConnLite::%s] end", __func__);
}

void UbConnLite::process_one_wqe(UdmaSqeWrite *sqe, UdmaSqOpcode opCode)
{
    HCCL_INFO("[UbConnLite::%s] start, opCode[%u]", __func__, static_cast<u32>(opCode));

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
            throw_exception<InternalException>(string_format("[UbConnLite::%s] memcpy_sp failed, ret = %d", __func__, ret));
        }
    }

    HCCL_INFO("[UbConnLite::%s] end, pi[%u]", __func__, pi);
}

void UbConnLite::process_one_wqe_with_notify(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt,
                                         const SqeConfigLite &cfg, UdmaSqeWriteWithNotify *sqe,
                                         const RmtRmaBufSliceLite &notify, u64 notifyData, u32 opCode,
                                         SlicePosition slicePos)
{
    HCCL_INFO("[UbConnLite::%s] start, locSize[%u], opCode[%u]", __func__, loc.get_size(), opCode);

    // sqOffset是用于计算Ubjetty中下wqe位置的偏移，小于sqDepth
    u32 sqOffset = pi % sqDepth_;
    if (sqOffset < sqDepth_ && (sqOffset + PI_NUM_TWO) >= sqDepth_) {
        piDetourCount++;
    }
    // pi维护用于传入DB Send用于Rtsq 敲door bell，要求u16数据结构并且自然增长
    pi = pi + PI_NUM_TWO;
    // 填充sqe
    sqe->comm.inlineEn = 0;
    fill_comm_sqe(&(sqe->comm), rmt, cfg, WRITE_WITH_NOTIFY_OPCODE, slicePos);
    fill_notify_sqe(&(sqe->notify), notify, notifyData);
    fill_local_sge_sqe(&(sqe->localU.sge), loc);
    if (sqe->localU.sge.length == 0) {
        sqe->comm.sgeNum = 0;
    }
    sqe->rsv1 = 0;
    sqe->rsv2 = 0;

    u8 *va = reinterpret_cast<u8 *>((sqVa_) + sqOffset * SQE_SIZE_64);
    if (!dwqeCacheLocked_) {
        // 带notify的wqe是96字节, 需要占用两个wqebb, 实际占用128字节
        if (sqOffset == sqDepth_ - 1) {
            memory_set_and_copy(va, SQE_SIZE_64, sqe);
            va  = reinterpret_cast<u8 *>(sqVa_);
            memory_set_and_copy(va, SQE_SIZE_64, reinterpret_cast<u8 *>(sqe) + SQE_SIZE_64);
        } else {
            memory_set_and_copy(va, SQE_SIZE_128, sqe);
        }
    }

    HCCL_INFO("[UbConnLite::%s] end, pi[%u]", __func__, pi);
}

void UbConnLite::memory_set_and_copy(u8 *va, u32 sqeSize, void *sqe)
{
    auto ret = memset_s(va, sqeSize, 0, sqeSize);
    if (UNLIKELY(ret != 0)) {
        throw_exception<InternalException>(string_format("[UbConnLite::%s] memset fail, ret = %d", __func__, ret));
    }
    ret = memcpy_sp(va, sqeSize, sqe, sqeSize);
    if (UNLIKELY(ret != 0)) {
        throw_exception<InternalException>(string_format("[UbConnLite::%s] not support this op type yet.", __func__));
    }
}

void UbConnLite::fill_notify_sqe(struct UdmaSqeNotify *sqe, const RmtRmaBufSliceLite &notify, u64 notifyData) const
{
    sqe->notifyTokenId    = notify.get_token_id();
    sqe->notifyTokenValue = notify.get_token_value();
    sqe->notifyAddrLow    = notify.get_addr() & ADDR_BIT_LOW;
    sqe->notifyAddrHigh   = notify.get_addr() >> ADDR_BIT_OFFSET;
    sqe->notifyDataLow    = notifyData & ADDR_BIT_LOW;
    sqe->notifyDataHigh   = notifyData >> ADDR_BIT_OFFSET;
    HCCL_INFO("UbConnLite fill_notify_sqe sqe->notifyAddrLow = %u "
              "sqe->notifyAddrHigh = %u, sqe->notifyDataLow = %u, sqe->notifyDataHigh = %u",
              sqe->notifyAddrLow, sqe->notifyAddrHigh, sqe->notifyDataLow, sqe->notifyDataHigh);
}

void UbConnLite::fill_local_sge_sqe(UdmaNormalSge *sqe, const RmaBufSliceLite &loc) const
{
    sqe->length       = loc.get_size();
    sqe->tokenId      = loc.get_token_id();
    sqe->dataAddrLow  = loc.get_addr() & ADDR_BIT_LOW;
    sqe->dataAddrHigh = loc.get_addr() >> ADDR_BIT_OFFSET;
    HCCL_INFO("UbConnLite fill_local_sge_sqe sqe->length = %u, sqe->dataAddrLow = %u "
              "sqe->dataAddrHigh = %u",
              sqe->length, sqe->dataAddrLow, sqe->dataAddrHigh);
}

void UbConnLite::read(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt,
                         const SqeConfigLite &cfg, ConnLiteOperationOut &out)
{
    process_slices(loc, rmt, maxReadSize,
        [&](const RmaBufSliceLite &locSlice, const RmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_READ, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_READ);
        });
    out.pi = pi;
}

void UbConnLite::read_reduce(ReduceIn reduceIn, const RmaBufSliceLite &loc,
                               const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg,
                               ConnLiteOperationOut &out)
{
    process_slices(loc, rmt, maxReadSize,
        [&](const RmaBufSliceLite &locSlice, const RmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_READ, slicePos);
            fill_comm_sqe_reduce_info(sqe.comm, reduceIn.reduceOp, reduceIn.dataType);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_READ);
        }, reduceIn.dataType);
    out.pi = pi;
}

void UbConnLite::write(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt,
                          const SqeConfigLite &cfg, ConnLiteOperationOut &out)
{
    process_slices(loc, rmt, maxWriteSize,
        [&](const RmaBufSliceLite &locSlice, const RmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
        });
    out.pi = pi;
}

void UbConnLite::inline_write(const u8 *data, u16 size, const RmtRmaBufSliceLite &rmt,
                                const SqeConfigLite &cfg, ConnLiteOperationOut &out)
{
    UdmaSqeWrite sqe{};
    sqe.comm.inlineEn = 1;
    sqe.comm.inlineMsgLen = size;
    fill_comm_sqe(&sqe.comm, rmt, cfg, UdmaSqOpcode::UDMA_OPC_WRITE);
    auto ret = memcpy_sp(sqe.u.inlineData.data, SQE_INLINE_DATA_SIZE, data, size);
    if (UNLIKELY(ret != 0)) {
        throw_exception<InternalException>(string_format("[UbConnLite::%s] memcpy_sp failed, ret = %d", __func__, ret));
    }
    process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
    out.pi = pi;
}

void UbConnLite::write_reduce(DataType dataType, ReduceOp reduceOp, const RmaBufSliceLite &loc,
                                const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg,
                                ConnLiteOperationOut &out)
{
    process_slices(loc, rmt, maxWriteSize,
        [&](const RmaBufSliceLite &locSlice, const RmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_comm_sqe_reduce_info(sqe.comm, reduceOp, dataType);
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
        }, dataType);
    out.pi = pi;
}

void UbConnLite::write_with_notify(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt,
                                    const SqeConfigLite &cfg, ConnLiteOperationOut &out,
                                    const RmtRmaBufSliceLite &notify, u64 notifyData)
{
    process_slices_with_notify(loc, rmt, maxWriteSize,
        [&](const RmaBufSliceLite &locSlice, const RmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
        },
        [&](const RmaBufSliceLite &locSlice, const RmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            UdmaSqeWriteWithNotify sqe{};
            process_one_wqe_with_notify(locSlice, rmtSlice, cfg, &sqe, notify, notifyData,
                                    WRITE_WITH_NOTIFY_OPCODE, slicePos);
        });
    out.pi = pi;
}

void UbConnLite::write_reduce_with_notify(DataType dataType, ReduceOp reduceOp,
                                          const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt,
                                          const SqeConfigLite &cfg, ConnLiteOperationOut &out,
                                          const RmtRmaBufSliceLite &notify, u64 notifyData)
{
    process_slices_with_notify(loc, rmt, maxWriteSize,
        [&](const RmaBufSliceLite &locSlice, const RmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_comm_sqe_reduce_info(sqe.comm, reduceOp, dataType);
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
        },
        [&](const RmaBufSliceLite &locSlice, const RmtRmaBufSliceLite &rmtSlice, SlicePosition slicePos) {
            UdmaSqeWriteWithNotify sqe{};
            fill_comm_sqe_reduce_info(sqe.comm, reduceOp, dataType);
            process_one_wqe_with_notify(locSlice, rmtSlice, cfg, &sqe, notify, notifyData,
                                    WRITE_WITH_NOTIFY_OPCODE, slicePos);
        }, dataType);
    out.pi = pi;
}

void UbConnLite::import_context(const UbConnLiteParam &liteParam)
{
    dieId_ = liteParam.dieId;
    funcId_ = liteParam.funcId;
    jettyId_ = liteParam.jettyId;
    sqVa_ = liteParam.sqVa;
    sqDepth_ = liteParam.sqDepth;
    dwqeCacheLocked_ = liteParam.dwqeCacheLocked;
    tpn_ = liteParam.tpn;
    maxReadSize = liteParam.maxReadSize;
    maxWriteSize = liteParam.maxWriteSize;
    (void)memcpy_sp(rmtEid_, sizeof(rmtEid_), liteParam.rmtEid, sizeof(liteParam.rmtEid));
}

void UbConnLite::import_queue_state(u16 queuePi, u32 queuePiDetourCount)
{
    pi = queuePi;
    piDetourCount = queuePiDetourCount;
}

void UbConnLite::export_queue_state(u16 &queuePi, u32 &queuePiDetourCount) const
{
    queuePi = pi;
    queuePiDetourCount = piDetourCount;
}

} // namespace Asc
