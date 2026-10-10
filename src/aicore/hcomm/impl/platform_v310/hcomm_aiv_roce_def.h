/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file hcomm_aiv_roce_def.h
 * \brief Hcomm AIV RoCE definition for V310
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_DEF_H

#include "../../common/hcomm_inner_def.h"

namespace AscendC {
constexpr uint32_t ROCE_CQE_POS = 128;
constexpr uint32_t HCOMM_ROCE_WQEBB_SIZE = 64;
constexpr uint32_t HCOMM_ROCE_WQEBB_U32_NUM = HCOMM_ROCE_WQEBB_SIZE / sizeof(uint32_t);
constexpr uint32_t HCOMM_ROCE_WQEBB_U64_SIZE = 8U;
constexpr uint32_t HCOMM_ROCE_WQEBB_U64_NUM = HCOMM_ROCE_WQEBB_SIZE / HCOMM_ROCE_WQEBB_U64_SIZE;

constexpr uint32_t ROCE_1825_WQE_BYTE_BITS = 8U;
constexpr uint32_t ROCE_1825_WQE_HALFWORD_BITS = 16U;
constexpr uint32_t ROCE_1825_WQE_WORD_BITS = 32U;
constexpr uint32_t ROCE_1825_WQE_CTRL_SEG_SIZE = 16U;
constexpr uint32_t ROCE_1825_WQE_TASK_SEG_SIZE = 32U;
constexpr uint32_t ROCE_1825_WQE_DATA_SEG_SIZE = 16U;
constexpr uint32_t ROCE_1825_WQE_OWNER_SHIFT = 7;    // owner_sl owner bit
constexpr uint32_t ROCE_1825_WQE_CTRL_VALUE = 0x40;  // owner_sl fixed part
constexpr uint32_t ROCE_1825_WQE_SQ_VA_VALUE = 0x20; // df_tsl VA bit
constexpr uint32_t ROCE_1825_WQE_SQ_SIGNAL_SHIFT = 7;
constexpr uint32_t ROCE_1825_WQE_CQE_SIGNAL_SHIFT = 7; // df_tsl CR bit
constexpr uint32_t ROCE_1825_WQE_TASK_OPTYPE_SHIFT = 24;
constexpr uint32_t ROCE_1825_WQE_TASK_CQE_SIGNAL_SHIFT = 29;
constexpr uint32_t ROCE_1825_WQE_TASK_FENCE_SHIFT = 30;
constexpr uint32_t ROCE_1825_WQE_CMP_TASK_LEN1 = 1U;
constexpr uint32_t ROCE_1825_WQE_CMP_TASK_LEN_SHIFT = 28; // cl_pi CL field
constexpr uint32_t ROCE_1825_WQE_TASK_SEG_ALIGN = HCOMM_ROCE_WQEBB_U64_SIZE;
constexpr uint32_t ROCE_1825_WQE_FAST_DMA_SHIFT = 10;
constexpr uint32_t ROCE_1825_WQE_SSN_MASK = 0x3; // low 2 bits of SQ WQE sequence number
constexpr uint32_t ROCE_1825_WQE_SSN_SHIFT = 12; // wf_bdsl wqe_msn field
constexpr uint32_t ROCE_1825_WQE_SSN_BE_SHIFT =
    ROCE_1825_WQE_HALFWORD_BITS - ROCE_1825_WQE_SSN_SHIFT; // SSN occupies bits 5:4 after byte swap
constexpr uint32_t ROCE_1825_WQE_DATA_SEG_SHIFT = 4;
constexpr uint32_t ROCE_1825_WQE_SECTION_ALIGN_SHIFT = 3;
constexpr uint32_t ROCE_1825_WQE_RDMA_READ_LAST_EXT_LEN = 4;
constexpr uint32_t ROCE_1825_WQE_NEXT_SGE_INVALID = 1U << 31; // data seg le_key L bit
constexpr uint64_t ROCE_1825_WQE_MAX_DATA_LEN = 1ULL << 31U;

constexpr uint32_t ROCE_1825_SQ_DB_PI_HIGH_SHIFT = 8;   // high 8 bits of the SQ producer index
constexpr uint32_t ROCE_1825_SQ_DB_PI_FIELD_SHIFT = 32; // pi field offset in the 64-bit doorbell
constexpr uint32_t ROCE_1825_SQ_DB_TYPE = 21;
constexpr uint32_t ROCE_1825_SQ_DB_SGIT_IDX = 1;
constexpr uint32_t ROCE_1825_SQ_DB_VENDOR_COS_SHIFT = 24;
constexpr uint32_t ROCE_1825_SQ_DB_VENDOR_MTUSHIFT_SHIFT = 50;
constexpr uint32_t ROCE_1825_SQ_DB_VENDOR_FIELD_MASK = 0x7;

constexpr uint32_t ROCE_1825_CQE_OPCODE_SHIFT = 27;
constexpr uint32_t ROCE_1825_CQE_OPCODE_MASK = 0x1f;
constexpr uint32_t ROCE_1825_CQE_OPTYPE_INVALID = 0x1f;
constexpr uint32_t ROCE_1825_CQE_OPTYPE_ERROR = 0x1e;       // op_type = error coding
constexpr uint32_t ROCE_1825_CQE_UPDATE_CI_MASK = 0xffffff; // CQ consumer index is 24-bit
constexpr uint32_t ROCE_1825_CQE_OWNER_SHIFT = 31;          // owner bit at dw1[31]

enum class HCOMM_ROCE_OP_TYPE : uint32_t { WRITE = 4U, READ = 8U };

#if defined(UT_TEST)
constexpr uint32_t HCOMM_ROCE_MUTEX_ID = 26U;
#else
constexpr uint32_t HCOMM_ROCE_MUTEX_ID = 30U;
#endif

template <>
class HcommImpl<COMM_PROTOCOL_ROCE> {
public:
    __aicore__ inline HcommImpl() {}
    __aicore__ inline ~HcommImpl() {}
    __aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);
    template <typename T>
    __aicore__ inline int32_t Init(const LocalTensor<T>& buff, uint32_t len);
    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t WriteNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t ReadNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        typename T, bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t WriteValueNbi(ChannelHandle channel, GM_ADDR dst, T value);
    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t WriteWithNotifyNbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notifyAddr, uint64_t notifyVal);
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t Commit(ChannelHandle channel);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t Drain(ChannelHandle channel);
    __aicore__ inline int32_t Lock(ChannelHandle channel);
    __aicore__ inline int32_t Unlock(ChannelHandle channel);
    template <typename U>
    __aicore__ inline UbcBatchHandle MakeBatchHandle(
        ChannelHandle channel, const LocalTensor<U>& buff, uint32_t buffLen, GM_ADDR remoteAddr, GM_ADDR localAddr);
    __aicore__ inline UbcBatchHandle& GetHandleRef(
        UbcBatchHandle& batchHandle, uint32_t channelIndex, GM_ADDR remoteAddr);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t WriteNbi(UbcBatchHandle& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t WriteNbi(
        UbcBatchHandle& batchHandle, GM_ADDR dst, const BufDesc* srcDescs, uint32_t srcNum);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t ReadNbi(UbcBatchHandle& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t ReadNbi(
        UbcBatchHandle& batchHandle, const BufDesc* dstDescs, uint32_t dstNum, GM_ADDR src);
    __aicore__ inline int32_t BatchCommit(UbcBatchHandle& batchHandle);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t Drain(UbcBatchHandle& batchHandle);

private:
    __aicore__ inline void FillCtrlSeg(
        __ubuf__ RoceWqeEntry* wqePtr, uint32_t sqHead, uint32_t sqDepth, uint32_t enCqe);
    __aicore__ inline void FillTaskSeg(
        __ubuf__ RoceWqeEntry* wqePtr, GM_ADDR dst, uint64_t len, uint32_t opType, uint32_t rKey, uint32_t lKey,
        uint32_t fence);
    __aicore__ inline void FillDataSeg(__ubuf__ RoceWqeEntry* wqePtr, GM_ADDR src, uint64_t len, uint32_t lKey);
    __aicore__ inline void WriteInvalidWqebb(__gm__ uint8_t* sqAddr, uint32_t sqHead, uint32_t sqDepth);
    __aicore__ inline int32_t MakeWqe(
        __gm__ ChannelEntity* chnlPtr, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType, uint32_t sqHead,
        uint32_t sqDepth, uint32_t enCqe, uint32_t fence);
    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t PostSend(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType);
    __aicore__ inline uint64_t GetDbValue(uint32_t sqHead, uint32_t qpn, uint64_t vendor);
    __aicore__ inline void KnockDoorBell(
        __gm__ ChannelEntity* chnlPtr, uint32_t sqHead, LocalTensor<uint32_t>& cqeItem);
    __aicore__ inline bool CheckCqeOwner(__ubuf__ RoceCqeEntry* cqePtr, uint32_t cqTail, uint32_t depth);
    template <bool sqSafeMode = false>
    __aicore__ inline int32_t PollCq(
        __gm__ ChannelEntity* chnlPtr, uint32_t expectIdx, LocalTensor<uint32_t>& cqeItem, uint32_t threshold = 0);
    template <bool sqSafeMode = false>
    __aicore__ inline bool EnContinue(
        uint32_t expectIdx, uint32_t cqTail, uint32_t sqHead, uint32_t sqTail, uint32_t sqDepth, uint32_t threshold);
    __aicore__ inline bool CheckChannelParam(__gm__ ChannelEntity* chnlPtr);
    template <HCOMM_ROCE_OP_TYPE opType, auto const& config>
    __aicore__ inline int32_t BatchPostSend(UbcBatchHandle& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <HCOMM_ROCE_OP_TYPE opType, auto const& config>
    __aicore__ inline int32_t BatchPostSend(
        UbcBatchHandle& batchHandle, GM_ADDR remoteAddr, const BufDesc* localDescs, uint32_t segNum);
    __aicore__ inline uint32_t PollBatchCq(UbcBatchHandle& batchHandle, uint32_t expectIdx);

private:
    LocalTensor<uint32_t> wqeUB_;
    LocalTensor<uint32_t> cqeUB_;
};
} // namespace AscendC

#endif
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_DEF_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_DEF_H
#endif
