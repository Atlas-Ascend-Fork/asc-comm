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
 * \file send_with_imm_def.h
 * \brief asccomm_send_with_imm_on_thread 真机样例的 Host/Device 共享定义。
 *
 * 本头文件同时被 Host 侧 .asc 与 AICPU 侧 .aicpu 包含，因此只依赖 <cstdint>，
 * 不引入 ACL/HCCL/AscendC 头文件。
 */

#ifndef ASCCOMM_EXAMPLE_SEND_WITH_IMM_DEF_H
#define ASCCOMM_EXAMPLE_SEND_WITH_IMM_DEF_H

#include <cstdint>

namespace SendWithImmExample {

constexpr int32_t SUCCESS = 0;
constexpr int32_t FAIL = -1;

// 通信缓冲区布局：前半段是本端发送源，后半段预留给对端 SEND_WITH_IMM 的数据落地观测。
constexpr uint64_t COMM_BUF_SIZE = 4ULL * 1024ULL * 1024ULL;
constexpr uint64_t SEND_DATA_OFFSET = 0ULL;
constexpr uint64_t RECV_DATA_OFFSET = 2ULL * 1024ULL * 1024ULL;
constexpr uint32_t DATA_SIZE = 256U;
constexpr uint64_t MULTI_SLICE_LEN = 1ULL * 1024ULL * 1024ULL;

// UB WQE 固定 64 字节，即 16 个 32 位小端字。字段布局对应
// src/aicpu/asccomm/resource/connection/udma_data_struct.h 中的 UdmaSqeCommon 与 UdmaSqeWrite。
constexpr uint32_t WQE_SIZE = 64U;
constexpr uint32_t WQE_IDX_FLAGS = 0U;          // sqeBbIdx|placeOdr|compOrder|fence|se|cqe|inlineEn|udfFlag|...
constexpr uint32_t WQE_IDX_OPCODE = 1U;         // targetHint[0,8) | opcode[8,16) | rsv1 | inlineMsgLen
constexpr uint32_t WQE_IDX_SGE_NUM = 2U;        // tpn[0,24) | sgeNum[24,32)
constexpr uint32_t WQE_IDX_IMM_LOW = 10U;       // 与 rmtAddrLow 共用 union
constexpr uint32_t WQE_IDX_IMM_HIGH = 11U;      // 与 rmtAddrHigh 共用 union
constexpr uint32_t WQE_IDX_SGE_LEN = 12U;       // UdmaNormalSge.length
constexpr uint32_t WQE_IDX_SGE_TOKEN = 13U;     // UdmaNormalSge.tokenId
constexpr uint32_t WQE_IDX_SGE_ADDR_LOW = 14U;  // UdmaNormalSge.dataAddrLow
constexpr uint32_t WQE_IDX_SGE_ADDR_HIGH = 15U; // UdmaNormalSge.dataAddrHigh

// 位域在 32 位字内自 LSB 起分配（ARM 小端 + GCC 位域约定）。
constexpr uint32_t WQE_CQE_BIT = 21U;
constexpr uint32_t WQE_INLINE_EN_BIT = 22U;
constexpr uint32_t WQE_OPCODE_SHIFT = 8U;
constexpr uint32_t WQE_SGE_NUM_SHIFT = 24U;

// UdmaSqOpcode::UDMA_OPC_SEND_WITH_IMM 的取值。
constexpr uint32_t OPC_SEND_WITH_IMM = 1U;

// AsccommResult 取值镜像，避免设备侧包含 Host 头文件。
constexpr int32_t RET_NOT_CALLED = -1;
constexpr int32_t RET_SUCCESS = 0;
constexpr int32_t RET_E_PTR = 2;
constexpr int32_t RET_E_NOT_SUPPORT = 5;

// pi 由数据面以 uint16_t 自然增长维护，回写到 HcommJfsContext.sqHead 时截断为 16 位。
constexpr uint32_t PI_MASK = 0xFFFFU;

inline uint32_t WqeCqe(const uint32_t* wqe) { return (wqe[WQE_IDX_FLAGS] >> WQE_CQE_BIT) & 0x1U; }

inline uint32_t WqeInlineEn(const uint32_t* wqe) { return (wqe[WQE_IDX_FLAGS] >> WQE_INLINE_EN_BIT) & 0x1U; }

inline uint32_t WqeOpcode(const uint32_t* wqe) { return (wqe[WQE_IDX_OPCODE] >> WQE_OPCODE_SHIFT) & 0xFFU; }

inline uint32_t WqeSgeNum(const uint32_t* wqe) { return (wqe[WQE_IDX_SGE_NUM] >> WQE_SGE_NUM_SHIFT) & 0xFFU; }

inline uint64_t WqeSgeAddr(const uint32_t* wqe)
{
    return (static_cast<uint64_t>(wqe[WQE_IDX_SGE_ADDR_HIGH]) << 32) | wqe[WQE_IDX_SGE_ADDR_LOW];
}

// 按 UB 单 WQE 最大写字节数推导期望切片数，与 UbConnLite::process_slices 的分片规则一致。
inline uint32_t ExpectSliceNum(uint64_t len, uint32_t maxWriteSize)
{
    if (len == 0ULL || maxWriteSize == 0U) {
        return 0U;
    }
    return static_cast<uint32_t>((len + maxWriteSize - 1ULL) / maxWriteSize);
}

enum CaseType : uint32_t {
    CASE_POSITIVE = 0U,    // 正常下发，校验 WQE 内容与 pi 推进
    CASE_ZERO_LENGTH = 1U, // len 为 0，期望成功且不产生 WQE
    CASE_NULL_SRC = 2U,    // src 为空，期望 RET_E_PTR
    CASE_BAD_THREAD = 3U,  // ThreadEntity 魔术字被破坏，期望 RET_E_PTR
    CASE_NON_UB = 4U,      // ChannelEntity 协议改为 RoCE，期望 RET_E_NOT_SUPPORT
};

constexpr uint32_t MAX_CASE_NUM = 8U;
constexpr uint32_t ENTITY_WORD_NUM = 64U; // ThreadEntity/ChannelEntity 固定 256 字节

struct CaseArgs {
    uint64_t threadHandle;
    uint64_t channelHandle;
    uint64_t srcAddr;
    uint64_t len;
    uint32_t immData;
    uint32_t caseType;
    int32_t expectRet;
    uint32_t reserved;
};

struct WqeSnapshot {
    uint32_t jfsValid; // 是否从真实 ChannelEntity 取到 UB JFS 上下文
    uint32_t wqeValid; // 是否成功回读到 WQE
    uint32_t opcode;
    uint32_t sgeNum;
    uint32_t cqe;
    uint32_t inlineEn;
    uint32_t immDataLow;
    uint32_t immDataHigh;
    uint32_t sgeLength;
    uint32_t sgeTokenId;
    uint64_t sgeAddr;
    uint32_t piBefore;
    uint32_t piAfter;
    uint32_t maxWriteSize;
    uint32_t sqDepth;
};

struct CaseResult {
    int32_t apiRet;
    WqeSnapshot wqe;
};

struct KernelContext {
    uint32_t caseNum;
    uint32_t reserved;
    CaseArgs args[MAX_CASE_NUM];
    CaseResult results[MAX_CASE_NUM];
    // 负向用例使用的实体副本：拷贝真实实体后只破坏单个字段，避免污染真实资源。
    uint32_t scratchThread[ENTITY_WORD_NUM];
    uint32_t scratchChannel[ENTITY_WORD_NUM];
};

struct KernelArgs {
    KernelContext* context;
};

} // namespace SendWithImmExample

#endif // ASCCOMM_EXAMPLE_SEND_WITH_IMM_DEF_H
