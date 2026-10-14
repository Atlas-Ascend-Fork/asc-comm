/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_AICPU_WQEMGR_WQE_H
#define ASCCOMM_AICPU_WQEMGR_WQE_H

#include <stdint.h>

namespace Asc {

#define UDMA_SQE_RMT_EID_SIZE 4

enum UdmaSqOpcode : uint32_t {
    UDMA_OPC_SEND,
    UDMA_OPC_SEND_WITH_IMM,
    UDMA_OPC_SEND_WITH_INVALID,
    UDMA_OPC_WRITE,
    UDMA_OPC_WRITE_WITH_IMM,
    UDMA_OPC_READ = 0x6,
    UDMA_OPC_CAS,
    UDMA_OPC_FAA = 0xb,
    UDMA_OPC_NOP = 0x11,
    UDMA_OPC_INVALID = 0x12
};

struct UdmaNormalSge {
    uint32_t length;
    uint32_t tokenId;
    uint32_t dataAddrLow;
    uint32_t dataAddrHigh;
};

struct UdmaInlineData {
    uint8_t data[16];
};

struct UdfExtData { // UDF扩展数据
    uint32_t udfType : 8;
    uint32_t reduceType : 4;
    uint32_t reduceOp : 4;
    uint32_t rsv : 16;
};

struct UdmaSqe {
    uint32_t sqeBbIdx : 16;
    uint32_t placeOdr : 2;
    uint32_t compOrder : 1;
    uint32_t fence : 1;
    uint32_t se : 1;
    uint32_t cqe : 1;
    uint32_t inlineEn : 1;
    uint32_t rsv : 5;
    uint32_t tokenEn : 1;
    uint32_t rmtJettyType : 2;
    uint32_t owner : 1;
    uint32_t targetHint : 8;
    uint32_t opcode : 8;
    uint32_t rsv1 : 6;
    uint32_t inlineMsgLen : 10;
    uint32_t tpn : 24;
    uint32_t sgeNum : 8;
    uint32_t rmtObjId : 20;
    uint32_t rsv2 : 12;
    uint32_t rmtEid[UDMA_SQE_RMT_EID_SIZE];
    uint32_t rmtTokenValue;
    union {
        uint32_t rsv3;
        UdfExtData udfData;
    } inlinedata;

    uint32_t rmtAddrLow;
    uint32_t rmtAddrHigh;
    union {
        UdmaNormalSge sge;
        UdmaInlineData inlineData;
    } u;
};

union LocalValueU {
    UdmaNormalSge sge;
    UdmaInlineData inlineData;
};

struct UdmaSqeNotify {
    uint32_t notifyTokenId : 20;
    uint32_t rsv : 12;
    uint32_t notifyTokenValue;
    uint32_t notifyAddrLow;
    uint32_t notifyAddrHigh;
    uint32_t notifyDataLow;
    uint32_t notifyDataHigh;
};

struct UdmaSqeCommon {
    uint32_t sqeBbIdx : 16;
    uint32_t placeOdr : 2;
    uint32_t compOrder : 1;
    uint32_t fence : 1;
    uint32_t se : 1;
    uint32_t cqe : 1;
    uint32_t inlineEn : 1;
    uint32_t udfFlag : 1;
    uint32_t rsv : 4;
    uint32_t tokenEn : 1;
    uint32_t rmtJettyType : 2;
    uint32_t owner : 1;
    uint32_t targetHint : 8;
    uint32_t opcode : 8;
    uint32_t rsv1 : 6;
    uint32_t inlineMsgLen : 10;
    uint32_t tpn : 24;
    uint32_t sgeNum : 8;
    uint32_t rmtObjId : 20;
    uint32_t rsv2 : 12;
    uint32_t rmtEid[UDMA_SQE_RMT_EID_SIZE];
    uint32_t rmtTokenValue;
    union {
        uint32_t rsv3;
        UdfExtData udfData;
    } inlinedata;

    union {
        struct {
            uint32_t rmtAddrLow;
            uint32_t rmtAddrHigh;
        } rmtAddr;
        struct {
            uint32_t immDataLow;
            uint32_t immDataHigh;
        } immData;
    };
};

struct UdmaSqeWriteWithNotify {
    struct UdmaSqeCommon comm;
    struct UdmaSqeNotify notify;
    uint32_t rsv1;
    uint32_t rsv2;
    union LocalValueU localU;
};

struct UdmaSqeWrite {
    struct UdmaSqeCommon comm;
    union LocalValueU u;
};

struct UdmaSqeRead {
    struct UdmaSqeCommon comm;
    union LocalValueU u;
};

// JFC完成队列条目(CQE)。位域布局与aicore侧HcommUrmaJfcCqeCtx一致
// (asc-comm/src/aicore/hcomm/common/hcomm_inner_def.h)，AICPU软件轮询按64字节消费。
struct UdmaCqe {
    uint32_t sR : 1;       // 0:发送侧完成 1:接收侧完成(即本端recv命中)
    uint32_t isJetty : 1;  // 0:JFS/JFR 1:Jetty
    uint32_t owner : 1;    // CQE相位位，软件轮询按(cqeConsIdx / cqDepth) & 1 判圈
    uint32_t inlineEn : 1; // 接收数据是否inline在CQE中
    uint32_t opcode : 3;   // UdmaSqOpcode(接收侧即对端SEND/SEND_WITH_IMM)
    uint32_t fd : 1;       // 1:CQE由驱动flush产生(链路异常)
    uint32_t rsv : 8;
    uint32_t substatus : 8;  // 子错误码
    uint32_t status : 8;     // 错误码，0为成功
    uint32_t entryIdx : 16;  // 关联的WQE索引
    uint32_t localNumL : 16; // 本地jetty/JFS/JFR编号低位(诊断用)
    uint32_t localNumH : 4;  // 本地编号高位
    uint32_t rmtIdx : 20;    // 对端jetty/JFS索引
    uint32_t rsv1 : 8;
    uint32_t tpn : 24; // TP号
    uint32_t rsv2 : 8;
    uint32_t byteCnt;       // 实际接收字节数
    uint32_t userDataL;     // 驱动回填的用户数据低32位
    uint32_t userDataH;     // 驱动回填的用户数据高32位
    uint32_t rmtEid[4];     // 对端EID
    uint32_t dataL;         // 32位立即数(imm data)
    uint32_t dataH;         // 立即数高32位(公开API仅暴露32位，恒0)
    uint32_t inlineData[3]; // inline接收数据(inlineEn=1时有效)
};
static_assert(sizeof(UdmaCqe) == 64, "UDMA CQE must be 64 bytes");
} // namespace Asc
#endif // ASCCOMM_AICPU_RESOURCE_AI_CPU_RESOUCES_H_
