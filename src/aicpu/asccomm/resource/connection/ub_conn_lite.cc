/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "../../diagnostics/profiling_session.h"

#include "ub_conn_lite.h"
#include <cstring>
#include "../../primitives/task_cache/binary_template.h"
#include "asccomm_log.h"
#include "../../common/utils/exception_util.h"
#include "udma_data_struct.h"
#include "../../common/exception/internal_exception.h"
#include "../../common/exception/invalid_params_exception.h"
#include "../../common/utils/string_util.h"

namespace Asc {
constexpr uint32_t ADDR_BIT_OFFSET = 32;
constexpr uint32_t SQE_SIZE_96 = 96;
constexpr uint32_t SQE_SIZE_128 = 128;
constexpr uint32_t SQE_SIZE_64 = 64;
constexpr uint32_t SQE_INLINE_DATA_SIZE = 16;
constexpr uint32_t RAW_SIZE = 16;
constexpr uint32_t RMT_EID_BYTE_SIZE = 16;
constexpr uint32_t PI_NUM_TWO = 2;
constexpr uint32_t WRITE_WITH_NOTIFY_OPCODE = 0x5;
constexpr uint32_t ADDR_BIT_LOW = 0xffffffff;
constexpr uint32_t UB_RELAX_ORDER = 0x1; // Relax Order表示当前SQE与后续Strong Order SQE有保序要求
constexpr uint32_t UB_STRONG_ORDER = 0x2; // Strong Order表示当前SQE有保序要求，该SQE不能超越前面的Relax Order SQE
constexpr uint32_t RMT_OBJ_ID_MASK = 0xfffff;

static_assert(sizeof(UdmaSqeWriteWithNotify) == SQE_SIZE_96, "notify WQE size must be 96 bytes");

uint32_t get_data_type_size(DataType dataType)
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
            throw_exception<InvalidParamsException>(
                string_format("unsupported data type[%u]", static_cast<uint32_t>(dataType)));
    }
}

void UbConnLite::fill_comm_sqe(
    UdmaSqeCommon* sqe, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg, uint32_t opCode, uint32_t sqOffset,
    SlicePosition slicePos)
{
    uint32_t cqeEn = (cfg.cqeEn && (slicePos == SlicePosition::LAST || slicePos == SlicePosition::ONLY)) ? 1 : 0;
    sqe->cqe = cqeEn;
    sqe->owner = (sqDepth_ != 0 && sqOffset == (sqDepth_ - 1)) ? 1 : 0;
    sqe->opcode = opCode;
    sqe->tpn = tpn_;

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

    sqe->se = 1;           // 表示是否使能solicited event
    sqe->rmtJettyType = 1; // 00 JFR  01:JETTY  10:jettyGroup 11:reserved
    int32_t ret = memcpy_sp(sqe->rmtEid, RMT_EID_BYTE_SIZE, rmtEid_, RAW_SIZE);
    if (UNLIKELY(ret != 0)) {
        ASCCOMM_ERROR("UbConnLite::fill_comm_sqe fill_comm_sqe memcpy failed, ret=%d", ret);
        throw_exception<InternalException>(string_format("UbConnLite::fill_comm_sqe memcpy_sp failed, ret = %d", ret));
    }

    sqe->sgeNum = 1;
    sqe->targetHint = 0;
    sqe->rmtObjId = rmt.get_token_id();
    sqe->tokenEn = 1;
    sqe->rmtTokenValue = rmt.get_token_value();
    sqe->rmtAddr.rmtAddrLow = rmt.get_addr() & ADDR_BIT_LOW;
    sqe->rmtAddr.rmtAddrHigh = rmt.get_addr() >> ADDR_BIT_OFFSET;
    ASCCOMM_INFO(
        "UbConnLite fill_comm_sqe UdmaSqeCommon slicePos[%d] sqe->cqe = %u, sqe->owner = %u sqe->opcode = %u, "
        "sqe->tpn = %u, sqe->rmtObjId = %u, sqe->rmtAddrLow = %u, sqe->rmtAddrHigh = %u, sqe->placeOdr = %u, "
        "sqe->compOrder = %u, sqe->fence = %u",
        slicePos, sqe->cqe, sqe->owner, sqe->opcode, sqe->tpn, sqe->rmtObjId, sqe->rmtAddr.rmtAddrLow,
        sqe->rmtAddr.rmtAddrHigh, sqe->placeOdr, sqe->compOrder, sqe->fence);
}

void UbConnLite::fill_comm_sqe_reduce_info(
    UdmaSqeCommon& sqeComm, ReduceOp reduceOp, DataType dataType, uint32_t udfType) const
{
    ASCCOMM_INFO("[UbConnLite::%s] start", __func__);

    sqeComm.inlinedata.udfData.udfType = udfType; // 0代表inline reduce

    switch (reduceOp) {
        case ReduceOp::SUM:
            sqeComm.inlinedata.udfData.reduceOp = 0xA;
            break;
        case ReduceOp::MAX:
            sqeComm.inlinedata.udfData.reduceOp = 0x8;
            break;
        case ReduceOp::MIN:
            sqeComm.inlinedata.udfData.reduceOp = 0x9;
            break;
        default:
            throw_exception<InvalidParamsException>(
                string_format("%s reduceOp[%u] is not supported.", __func__, static_cast<uint32_t>(reduceOp)));
    }
    switch (dataType) {
        case DataType::INT8:
            sqeComm.inlinedata.udfData.reduceType = 0x0;
            break;
        case DataType::INT16:
            sqeComm.inlinedata.udfData.reduceType = 0x1;
            break;
        case DataType::INT32:
            sqeComm.inlinedata.udfData.reduceType = 0x2;
            break;
        case DataType::UINT8:
            sqeComm.inlinedata.udfData.reduceType = 0x3;
            break;
        case DataType::UINT16:
            sqeComm.inlinedata.udfData.reduceType = 0x4;
            break;
        case DataType::UINT32:
            sqeComm.inlinedata.udfData.reduceType = 0x5;
            break;
        case DataType::FP16:
            sqeComm.inlinedata.udfData.reduceType = 0x6;
            break;
        case DataType::FP32:
            sqeComm.inlinedata.udfData.reduceType = 0x7;
            break;
        case DataType::BFP16:
            sqeComm.inlinedata.udfData.reduceType = 0x8;
            break;
        case DataType::BF16_SAT:
            sqeComm.inlinedata.udfData.reduceType = 0x9;
            break;
        default:
            throw_exception<InvalidParamsException>(
                string_format("%s dataType[%u] is not supported.", __func__, static_cast<uint32_t>(dataType)));
    }

    // udf字段是否有效
    sqeComm.udfFlag = 1;

    ASCCOMM_INFO(
        "[UbConnLite::%s] end, reduceOp[%u], reduceType[%u]", __func__, static_cast<uint32_t>(reduceOp),
        static_cast<uint32_t>(dataType));
}

void UbConnLite::process_slices(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, uint32_t maxSliceSize,
    std::function<void(const RmaBufSliceLite&, const RmtRmaBufSliceLite&, SlicePosition)> processOneSlice,
    DataType dataType) const
{
    (void)dataType;
    // reduce操作需要保证切片大小是数据类型大小的整数倍
    uint64_t sliceSize = static_cast<uint64_t>(maxSliceSize);

    uint64_t locBufSize = loc.get_size();
    uint64_t sliceNum = locBufSize / sliceSize;
    uint64_t lastSliceSize = locBufSize % sliceSize;

    uint64_t totalSize = sliceNum * sliceSize;

    if (UNLIKELY(loc.get_addr() > UINT64_MAX - totalSize || rmt.get_addr() > UINT64_MAX - totalSize)) {
        throw_exception<InternalException>("integer overflow occurs");
    }
    for (uint64_t sliceIdx = 0; sliceIdx < sliceNum; sliceIdx++) {
        uint64_t offset = sliceIdx * sliceSize;
        uint64_t locAddr = loc.get_addr() + offset;
        uint64_t rmtAddr = rmt.get_addr() + offset;

        ASCCOMM_INFO(
            "[UbConnLite::%s] Slice[%llu]: offset=0x%llx, locAddr=0x%llx, rmtAddr=0x%llx, size=0x%llx", __func__,
            sliceIdx, offset, locAddr, rmtAddr, sliceSize);

        RmaBufSliceLite locSlice(locAddr, sliceSize, 0, loc.get_token_id());

        RmtRmaBufSliceLite rmtSlice(rmtAddr, sliceSize, 0, rmt.get_token_id(), rmt.get_token_value(), UINT32_MAX);
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

        RmtRmaBufSliceLite lastRmtSlice(
            rmt.get_addr() + sliceNum * sliceSize, lastSliceSize, 0, rmt.get_token_id(), rmt.get_token_value(),
            UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceNum == 0) ? SlicePosition::ONLY : SlicePosition::LAST;
        processOneSlice(lastLocSlice, lastRmtSlice, slicePos);
        sliceNum++;
    }

    ASCCOMM_INFO(
        "[UbConnLite::%s] end, locBufSize[%u], sliceNUm[%u], sliceSize[%u], lastSliceSize[%u]", __func__, locBufSize,
        sliceNum, sliceSize, lastSliceSize);
}

void UbConnLite::process_slices_with_notify(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, uint32_t maxSliceSize,
    std::function<void(const RmaBufSliceLite&, const RmtRmaBufSliceLite&, SlicePosition)> processOneSlice,
    std::function<void(const RmaBufSliceLite&, const RmtRmaBufSliceLite&, SlicePosition)> processOneSliceWithNotify,
    DataType dataType) const
{
    ASCCOMM_INFO("[UbConnLite::%s] start", __func__);

    // reduce操作需要保证切片大小是数据类型大小的整数倍
    uint32_t sliceSize = maxSliceSize;
    if (dataType != DataType::INVALID) {
        uint32_t dataTypeSize = get_data_type_size(dataType);
        if (UNLIKELY(dataTypeSize == 0 || maxSliceSize < dataTypeSize)) {
            throw_exception<InvalidParamsException>(string_format(
                "%s maxSliceSize[%u] is less than dataTypeSize[%u]", __func__, maxSliceSize, dataTypeSize));
        }
        sliceSize = maxSliceSize / dataTypeSize * dataTypeSize;
    } else if (UNLIKELY(maxSliceSize == 0)) {
        throw_exception<InvalidParamsException>(string_format("%s maxSliceSize is zero", __func__));
    }

    uint64_t locBufSize = loc.get_size();
    uint64_t sliceNum = locBufSize / sliceSize;
    uint64_t lastSliceSize = locBufSize % sliceSize;
    if (sliceNum > 0 && lastSliceSize == 0) {
        sliceNum--;
        lastSliceSize = sliceSize;
    }
    uint64_t totalSize = sliceNum * sliceSize;
    if (UNLIKELY(loc.get_addr() > UINT64_MAX - totalSize || rmt.get_addr() > UINT64_MAX - totalSize)) {
        throw_exception<InternalException>("integer overflow occurs");
    }
    for (uint64_t sliceIdx = 0; sliceIdx < sliceNum; sliceIdx++) {
        RmaBufSliceLite locSlice(loc.get_addr() + sliceIdx * sliceSize, sliceSize, 0, loc.get_token_id());

        RmtRmaBufSliceLite rmtSlice(
            rmt.get_addr() + sliceIdx * sliceSize, sliceSize, 0, rmt.get_token_id(), rmt.get_token_value(), UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceIdx == 0) ? SlicePosition::FIRST : SlicePosition::MIDDLE;
        processOneSlice(locSlice, rmtSlice, slicePos);
    }

    if (lastSliceSize > 0) {
        RmaBufSliceLite lastLocSlice(loc.get_addr() + sliceNum * sliceSize, lastSliceSize, 0, loc.get_token_id());

        RmtRmaBufSliceLite lastRmtSlice(
            rmt.get_addr() + sliceNum * sliceSize, lastSliceSize, 0, rmt.get_token_id(), rmt.get_token_value(),
            UINT32_MAX);
        SlicePosition slicePos;
        slicePos = (sliceNum == 0) ? SlicePosition::ONLY : SlicePosition::LAST;
        processOneSliceWithNotify(lastLocSlice, lastRmtSlice, slicePos);
    }

    ASCCOMM_INFO(
        "[UbConnLite::%s] end, locBufSize[%llu], sliceNUm[%llu], sliceSize[%u], lastSliceSize[%llu]", __func__,
        locBufSize, sliceNum, sliceSize, lastSliceSize);
}

void UbConnLite::fill_one_sqe_write(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg, UdmaSqeWrite* sqe,
    UdmaSqOpcode opCode, SlicePosition slicePos)
{
    ASCCOMM_INFO("[UbConnLite::%s] start, loc size[%llu]", __func__, loc.get_size());

    sqe->comm.inlineEn = 0;
    const uint32_t sqOffset = (sqDepth_ != 0) ? (pi % sqDepth_) : 0;
    fill_comm_sqe(&(sqe->comm), rmt, cfg, opCode, sqOffset, slicePos);
    fill_local_sge_sqe(&(sqe->u.sge), loc);
    if (sqe->u.sge.length == 0) {
        sqe->comm.sgeNum = 0;
    }

    ASCCOMM_INFO("[UbConnLite::%s] end", __func__);
}

void UbConnLite::fill_one_sqe_send(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& target, const SqeConfigLite& cfg, UdmaSqeWrite* sqe,
    UdmaSqOpcode opCode, uint64_t immData, SlicePosition slicePos)
{
    ASCCOMM_INFO(
        "[UbConnLite::%s] start, loc size[%llu], remoteJettyId[%u]", __func__, loc.get_size(), target.get_token_id());

    sqe->comm.inlineEn = 0;
    const uint32_t sqOffset = (sqDepth_ != 0) ? (pi % sqDepth_) : 0;
    fill_comm_sqe(&(sqe->comm), target, cfg, opCode, sqOffset, slicePos);
    // SEND的rmtObjId是远端接收Jetty/JFR id，tokenEn表示远端token是否参与校验；二者不能复用
    // RMA内存token的“无条件启用token”语义。
    sqe->comm.rmtObjId = target.get_token_id() & RMT_OBJ_ID_MASK;
    sqe->comm.tokenEn = target.get_token_value() != 0 ? 1U : 0U;
    sqe->comm.rmtTokenValue = target.get_token_value();
    sqe->comm.rmtAddr.rmtAddrLow = 0;
    sqe->comm.rmtAddr.rmtAddrHigh = 0;
    fill_local_sge_sqe(&(sqe->u.sge), loc);
    if (sqe->u.sge.length == 0) {
        sqe->comm.sgeNum = 0;
    }
    if (opCode == UdmaSqOpcode::UDMA_OPC_SEND_WITH_IMM) {
        sqe->comm.immData.immDataLow = static_cast<uint32_t>(immData);
        sqe->comm.immData.immDataHigh = static_cast<uint32_t>(immData >> 32U);
    }

    ASCCOMM_INFO(
        "[UbConnLite::%s] end, opCode[%u], tokenEn[%u], remoteJettyId[%u], tokenValue[%u]", __func__,
        static_cast<uint32_t>(opCode), sqe->comm.tokenEn, sqe->comm.rmtObjId, sqe->comm.rmtTokenValue);
}

void UbConnLite::process_one_wqe(UdmaSqeWrite* sqe, UdmaSqOpcode opCode)
{
    ASCCOMM_INFO("[UbConnLite::%s] start, opCode[%u]", __func__, static_cast<uint32_t>(opCode));

    reserve_space(1);
    AsccommBinaryCache::CaptureWqe(AsccommBinaryCache::Kind::UbWqe, sqe, SQE_SIZE_64);
    if (AscProf::Enabled()) {
        AscProf::Wqe(profilingChannel_, sqe, SQE_SIZE_64, static_cast<uint16_t>(pi + 1));
    }
    // sqOffset是用于计算Ubjetty中下wqe位置的偏移，小于sqDepth
    uint32_t sqOffset = pi % sqDepth_;
    // pi维护用于传入DB Send用于Rtsq 敲door bell，要求uint16_t数据结构并且自然增长
    pi = pi + 1;

    // 写wqe到va
    uint8_t* va = reinterpret_cast<uint8_t*>(sqVa_ + sqOffset * SQE_SIZE_64);
    if (!dwqeCacheLocked_) {
        auto ret = memcpy_sp(va, SQE_SIZE_64, sqe, SQE_SIZE_64);
        if (UNLIKELY(ret != 0)) {
            throw_exception<InternalException>(
                string_format("[UbConnLite::%s] memcpy_sp failed, ret = %d", __func__, ret));
        }
    }
    ASCCOMM_INFO("[UbConnLite::%s] end, pi[%u]", __func__, pi);
}

void UbConnLite::process_one_wqe_with_notify(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg, UdmaSqeWriteWithNotify* sqe,
    const RmtRmaBufSliceLite& notify, uint64_t notifyData, uint32_t opCode, SlicePosition slicePos)
{
    ASCCOMM_INFO("[UbConnLite::%s] start, locSize[%u], opCode[%u]", __func__, loc.get_size(), opCode);

    // sqOffset是用于计算Ubjetty中下wqe位置的偏移，小于sqDepth
    reserve_space(2);
    uint32_t sqOffset = pi % sqDepth_;
    // pi维护用于传入DB Send用于Rtsq 敲door bell，要求uint16_t数据结构并且自然增长
    pi = pi + PI_NUM_TWO;
    // 填充sqe
    sqe->comm.inlineEn = 0;
    fill_comm_sqe(&(sqe->comm), rmt, cfg, WRITE_WITH_NOTIFY_OPCODE, sqOffset, slicePos);
    fill_notify_sqe(&(sqe->notify), notify, notifyData);
    fill_local_sge_sqe(&(sqe->localU.sge), loc);
    if (sqe->localU.sge.length == 0) {
        sqe->comm.sgeNum = 0;
    }
    sqe->rsv1 = 0;
    sqe->rsv2 = 0;

    // The logical structure is 96 bytes but occupies two 64-byte WQEBBs.
    // Materialize deterministic padding instead of reading past the stack object.
    alignas(8) uint8_t physical[SQE_SIZE_128]{};
    (void)memcpy_sp(physical, sizeof(physical), sqe, sizeof(*sqe));
    AsccommBinaryCache::CaptureWqe(AsccommBinaryCache::Kind::UbWqe, physical, sizeof(physical));
    if (AscProf::Enabled()) {
        AscProf::Wqe(profilingChannel_, physical, sizeof(physical), pi);
    }
    publish_double(physical, sqOffset);
}

void UbConnLite::publish_double(void* sqe, uint32_t sqOffset)
{
    uint8_t* va = reinterpret_cast<uint8_t*>((sqVa_) + sqOffset * SQE_SIZE_64);
    if (!dwqeCacheLocked_) {
        // 带notify的wqe是96字节, 需要占用两个wqebb, 实际占用128字节
        if (sqOffset == sqDepth_ - 1) {
            memory_set_and_copy(va, SQE_SIZE_64, sqe, SQE_SIZE_64);
            va = reinterpret_cast<uint8_t*>(sqVa_);
            memory_set_and_copy(
                va, SQE_SIZE_64, reinterpret_cast<const uint8_t*>(sqe) + SQE_SIZE_64, SQE_SIZE_96 - SQE_SIZE_64);
        } else {
            memory_set_and_copy(va, SQE_SIZE_128, sqe, SQE_SIZE_96);
        }
    }
}

void UbConnLite::publish_template(void* bytes, uint32_t length, const AscProf::WqeDescription* description)
{
    // Compiled templates contain all physical bytes, including 128B padding.
    // Keep the native owner/PI ordering but avoid native capture/logging hooks.
    if (length != SQE_SIZE_64 && length != SQE_SIZE_128) {
        throw_exception<InvalidParamsException>("invalid UB template length");
    }
    const uint32_t blocks = length / SQE_SIZE_64;
    reserve_space(blocks);
    const uint32_t offset = pi % sqDepth_;
    auto* common = static_cast<UdmaSqeCommon*>(bytes);
    // Match #317 native builders: owner belongs to the starting WQEBB slot.
    common->owner = (offset == sqDepth_ - 1) ? 1 : 0;
    pi = pi + blocks;
    if (AscProf::Enabled()) {
        AscProf::Wqe(profilingChannel_, bytes, length, pi, description);
    }
    if (dwqeCacheLocked_) {
        return;
    }
    auto* target = reinterpret_cast<uint8_t*>(sqVa_ + offset * SQE_SIZE_64);
    // Constant lengths select securec's inline copy path, including ring wrap.
    int ret;
    if (length == SQE_SIZE_64) {
        ret = memcpy_sp(target, SQE_SIZE_64, bytes, SQE_SIZE_64);
    } else if (offset != sqDepth_ - 1) {
        ret = memcpy_sp(target, SQE_SIZE_128, bytes, SQE_SIZE_128);
    } else {
        ret = memcpy_sp(target, SQE_SIZE_64, bytes, SQE_SIZE_64);
        if (ret == 0) {
            ret = memcpy_sp(
                reinterpret_cast<void*>(sqVa_), SQE_SIZE_64, static_cast<uint8_t*>(bytes) + SQE_SIZE_64, SQE_SIZE_64);
        }
    }
    if (ret != 0) {
        throw_exception<InternalException>("UB template copy failed");
    }
}

void UbConnLite::memory_set_and_copy(uint8_t* va, uint32_t destSize, const void* sqe, uint32_t copySize)
{
    auto ret = memset_s(va, destSize, 0, destSize);
    if (UNLIKELY(ret != 0)) {
        throw_exception<InternalException>(string_format("[UbConnLite::%s] memset fail, ret = %d", __func__, ret));
    }
    ret = memcpy_sp(va, destSize, sqe, copySize);
    if (UNLIKELY(ret != 0)) {
        throw_exception<InternalException>(string_format("[UbConnLite::%s] not support this op type yet.", __func__));
    }
}

void UbConnLite::fill_notify_sqe(struct UdmaSqeNotify* sqe, const RmtRmaBufSliceLite& notify, uint64_t notifyData) const
{
    sqe->notifyTokenId = notify.get_token_id();
    sqe->notifyTokenValue = notify.get_token_value();
    sqe->notifyAddrLow = notify.get_addr() & ADDR_BIT_LOW;
    sqe->notifyAddrHigh = notify.get_addr() >> ADDR_BIT_OFFSET;
    sqe->notifyDataLow = notifyData & ADDR_BIT_LOW;
    sqe->notifyDataHigh = notifyData >> ADDR_BIT_OFFSET;
    ASCCOMM_INFO(
        "UbConnLite fill_notify_sqe sqe->notifyAddrLow = %u "
        "sqe->notifyAddrHigh = %u, sqe->notifyDataLow = %u, sqe->notifyDataHigh = %u",
        sqe->notifyAddrLow, sqe->notifyAddrHigh, sqe->notifyDataLow, sqe->notifyDataHigh);
}

void UbConnLite::fill_local_sge_sqe(UdmaNormalSge* sqe, const RmaBufSliceLite& loc) const
{
    sqe->length = loc.get_size();
    sqe->tokenId = loc.get_token_id();
    sqe->dataAddrLow = loc.get_addr() & ADDR_BIT_LOW;
    sqe->dataAddrHigh = loc.get_addr() >> ADDR_BIT_OFFSET;
    ASCCOMM_INFO(
        "UbConnLite fill_local_sge_sqe sqe->length = %u, sqe->dataAddrLow = %u "
        "sqe->dataAddrHigh = %u",
        sqe->length, sqe->dataAddrLow, sqe->dataAddrHigh);
}

void UbConnLite::read(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg, ConnLiteOperationOut& out)
{
    process_slices(
        loc, rmt, maxReadSize,
        [&](const RmaBufSliceLite& locSlice, const RmtRmaBufSliceLite& rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_READ, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_READ);
        });
    out.pi = pi;
}

void UbConnLite::read_reduce(
    ReduceIn reduceIn, const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg,
    ConnLiteOperationOut& out)
{
    process_slices(
        loc, rmt, maxReadSize,
        [&](const RmaBufSliceLite& locSlice, const RmtRmaBufSliceLite& rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_READ, slicePos);
            fill_comm_sqe_reduce_info(sqe.comm, reduceIn.reduceOp, reduceIn.dataType);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_READ);
        },
        reduceIn.dataType);
    out.pi = pi;
}

void UbConnLite::write(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg, ConnLiteOperationOut& out)
{
    process_slices(
        loc, rmt, maxWriteSize,
        [&](const RmaBufSliceLite& locSlice, const RmtRmaBufSliceLite& rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
        });
    out.pi = pi;
}

void UbConnLite::inline_write(
    const uint8_t* data, uint16_t size, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg,
    ConnLiteOperationOut& out)
{
    UdmaSqeWrite sqe{};
    sqe.comm.inlineEn = 1;
    sqe.comm.inlineMsgLen = size;
    const uint32_t sqOffset = (sqDepth_ != 0) ? (pi % sqDepth_) : 0;
    fill_comm_sqe(&sqe.comm, rmt, cfg, UdmaSqOpcode::UDMA_OPC_WRITE, sqOffset);
    auto ret = memcpy_sp(sqe.u.inlineData.data, SQE_INLINE_DATA_SIZE, data, size);
    if (UNLIKELY(ret != 0)) {
        throw_exception<InternalException>(string_format("[UbConnLite::%s] memcpy_sp failed, ret = %d", __func__, ret));
    }
    process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
    out.pi = pi;
}

void UbConnLite::write_reduce(
    DataType dataType, ReduceOp reduceOp, const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt,
    const SqeConfigLite& cfg, ConnLiteOperationOut& out)
{
    process_slices(
        loc, rmt, maxWriteSize,
        [&](const RmaBufSliceLite& locSlice, const RmtRmaBufSliceLite& rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_comm_sqe_reduce_info(sqe.comm, reduceOp, dataType);
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
        },
        dataType);
    out.pi = pi;
}

void UbConnLite::write_with_notify(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg, ConnLiteOperationOut& out,
    const RmtRmaBufSliceLite& notify, uint64_t notifyData)
{
    process_slices_with_notify(
        loc, rmt, maxWriteSize,
        [&](const RmaBufSliceLite& locSlice, const RmtRmaBufSliceLite& rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
        },
        [&](const RmaBufSliceLite& locSlice, const RmtRmaBufSliceLite& rmtSlice, SlicePosition slicePos) {
            UdmaSqeWriteWithNotify sqe{};
            process_one_wqe_with_notify(
                locSlice, rmtSlice, cfg, &sqe, notify, notifyData, WRITE_WITH_NOTIFY_OPCODE, slicePos);
        });
    out.pi = pi;
}

void UbConnLite::write_reduce_with_notify(
    DataType dataType, ReduceOp reduceOp, const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt,
    const SqeConfigLite& cfg, ConnLiteOperationOut& out, const RmtRmaBufSliceLite& notify, uint64_t notifyData)
{
    process_slices_with_notify(
        loc, rmt, maxWriteSize,
        [&](const RmaBufSliceLite& locSlice, const RmtRmaBufSliceLite& rmtSlice, SlicePosition slicePos) {
            UdmaSqeWrite sqe{};
            fill_comm_sqe_reduce_info(sqe.comm, reduceOp, dataType);
            fill_one_sqe_write(locSlice, rmtSlice, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_WRITE, slicePos);
            process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_WRITE);
        },
        [&](const RmaBufSliceLite& locSlice, const RmtRmaBufSliceLite& rmtSlice, SlicePosition slicePos) {
            UdmaSqeWriteWithNotify sqe{};
            fill_comm_sqe_reduce_info(sqe.comm, reduceOp, dataType);
            process_one_wqe_with_notify(
                locSlice, rmtSlice, cfg, &sqe, notify, notifyData, WRITE_WITH_NOTIFY_OPCODE, slicePos);
        },
        dataType);
    out.pi = pi;
}

void UbConnLite::send(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& target, const SqeConfigLite& cfg, ConnLiteOperationOut& out)
{
    ASCCOMM_INFO("[UbConnLite::%s] start, loc size[%llu]", __func__, loc.get_size());

    // UB_CTP（4KB）/UB_RTP（64KB）单条消息上限与hcomm下发的maxWriteSize不配套，send不支持按maxWriteSize拆包，
    // 每次调用仅下发一条WQE；零长度消息同样下发一条sgeNum=0的WQE，保证对端可感知本次消息且PI推进
    UdmaSqeWrite sqe{};
    fill_one_sqe_send(loc, target, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_SEND, 0, SlicePosition::ONLY);
    process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_SEND);
    out.pi = pi;

    ASCCOMM_INFO("[UbConnLite::%s] end, pi[%u]", __func__, out.pi);
}

void UbConnLite::send_with_imm(
    const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& target, uint64_t immData, const SqeConfigLite& cfg,
    ConnLiteOperationOut& out)
{
    ASCCOMM_INFO("[UbConnLite::%s] start, loc size[%llu], immData[%llu]", __func__, loc.get_size(), immData);

    // UB_CTP（4KB）/UB_RTP（64KB）单条消息上限与hcomm下发的maxWriteSize不配套，send不支持按maxWriteSize拆包，
    // 每次调用仅下发一条WQE；零长度消息同样下发一条sgeNum=0的WQE，immData随该WQE正常传递给对端
    UdmaSqeWrite sqe{};
    fill_one_sqe_send(loc, target, cfg, &sqe, UdmaSqOpcode::UDMA_OPC_SEND_WITH_IMM, immData, SlicePosition::ONLY);
    process_one_wqe(&sqe, UdmaSqOpcode::UDMA_OPC_SEND_WITH_IMM);
    out.pi = pi;

    ASCCOMM_INFO("[UbConnLite::%s] end, pi[%u]", __func__, out.pi);
}

void UbConnLite::import_context(const UbConnLiteParam& liteParam)
{
    jettyId_ = liteParam.jettyId;
    sqVa_ = liteParam.sqVa;
    sqDepth_ = liteParam.sqDepth;
    dwqeCacheLocked_ = liteParam.dwqeCacheLocked;
    tpn_ = liteParam.tpn;
    maxReadSize = liteParam.maxReadSize;
    maxWriteSize = liteParam.maxWriteSize;
    (void)memcpy_sp(rmtEid_, sizeof(rmtEid_), liteParam.rmtEid, sizeof(liteParam.rmtEid));
}

void UbConnLite::import_queue_state(uint32_t sqHead) { pi = static_cast<uint16_t>(sqHead); }

void UbConnLite::export_queue_state(uint32_t& sqHead) const { sqHead = static_cast<uint32_t>(pi); }

} // namespace Asc
