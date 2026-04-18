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
 * \file hcomm_inner_def.h
 * \brief Hcomm inner definition
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "impl/adv_api/detail/hcomm/common/hcomm_inner_def.h is an internal header file and must not be used directly. Functions or variables defined in this file may be removed in the future. Please use \"#include \"adv_api/activation/simplesoftmax.h\"\" and use public functions or variables defined in interface headers files.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_INNER_DEF_H__
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_INNER_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_INNER_DEF_H

#include <cstdint>

namespace AscendC {

typedef struct {
    uint32_t type; // 0 RDMA   1 URMA
    uint64_t addr;
    uint64_t length;
    union {
        struct {
            uint32_t lkey;
            uint32_t rkey;
        } rdmaMemProtectionInfo;
        struct {
            uint32_t tokenID;
            uint32_t tokenValue;
        } urmaMemProtectionInfo;
        int8_t reserve[32];
    } pti;
} ProtectionInfo;

typedef struct {
    uint32_t type;
    union {
        struct {
            uint32_t jfsID;
            uint64_t sqVa;
            uint32_t wqeSize;
            uint32_t sqDepth;
            uint32_t tpID;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint8_t remoteEID[16];
            uint64_t dbVa;
        } jfsContext; // 48+16=64Bytes
        struct {
            uint32_t qpn;
            uint64_t sqVa;     // sq的基地址，wqe后续往这个地址放
            uint32_t wqeSize;  // 单个wqe的size
            uint32_t depth;    // sq队列长度
            uint64_t headAddr; // sq_PI
            uint64_t tailAddr; // sq_CI
            uint8_t sl;
            uint64_t dbVa; // 敲doorbell的地址
            int8_t dbMode; // 0-hw/1-sw
        } rdmaSqContext;   // 46 Bytes
        int8_t reserve[128];
    } ctx;
} SqContext;

typedef struct {
    uint32_t type;
    union {
        struct {
            uint32_t jfcID;
            uint64_t scqVa;
            uint32_t cqeSize;
            uint32_t cqDepth;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbVa;
        } jfcContext;
        struct {
            uint32_t cqn;
            uint64_t cqVa;
            uint32_t cqeSize;
            uint32_t cqDepth;
            uint64_t headAddr;
            uint64_t tailAddr; // cq_CI
            uint64_t dbVa;
            int8_t dbMode; // 0-hw/1-sw
        } rdmaCqContext;
        int8_t reserve[128];
    } ctx;
} CqContext;

typedef struct {
    uint32_t type;
    union {
        struct {
            uint64_t address;
            int32_t notifyId;
            uint32_t size; // 默认4Byte
        } hccsNotify;
        struct {
            uint64_t address; // 创建notify的时候申请的内存的地址（寄存器的虚地址）
            int32_t notifyId; // remote 时不用
            uint32_t size;    // 默认4Byte
            ProtectionInfo protectionInfo;
        } rmaNotify;
        int8_t reserve[64];
    };
} Notify;

// AIV直驱RoCE，无需考虑notify
// write with notify的语义，地址是GM地址
typedef struct {   // channel 本身一片内存
    uint32_t type; // RDMA, SDMA, URMA
    // local notify
    uint32_t localNotifyNum; // 几个local notify
    Notify* localNotifyAddr; // local notify的数组，分别指向不同的内存片
    // remote notify
    uint32_t remoteNotifyNum;
    Notify* remoteNotifyAddr;
    // Local buffer
    uint32_t localBufferNum;
    ProtectionInfo* localBufferAddr;
    // Remote buffer
    uint32_t remoteBufferNum;
    ProtectionInfo* remoteBufferAddr;
    // SQ
    uint32_t sqNum;
    SqContext* sqContextAddr; // write/read/commit 操作这个字段
    // CQ
    uint32_t cqNum;
    CqContext* cqContextAddr; // wait 操作这个字段
    // AIV Status
    uint8_t aivStatus[64];
    // reserve
    uint8_t reserve[1024];
} Channel;

// 宏隔离
typedef struct {
    // Control Segment
    union {
        struct {
            uint32_t o : 1; // Owner
            uint32_t ctrlSl : 2;
            uint32_t csl : 2;
            uint32_t difSl : 3;
            uint32_t cr : 1;
            uint32_t df : 1;
            uint32_t va : 1;
            uint32_t tsl : 5;
            uint32_t cf : 1;
            uint32_t wf : 1;
            uint32_t rsvd0 : 4;
            uint32_t rrvSl : 2;
            uint32_t bdsLen : 8;
        } bs;
        uint32_t value;
    } dw0;
    union {
        struct {
            uint32_t cl : 4;
            uint32_t rsvd1 : 8;
            uint32_t maskPi : 20;
        } bs;
        uint32_t value;
    } dw1;
} RoceWqeCtrlSeg;

typedef struct {
    // Task Segment
    union {
        struct {
            uint32_t se : 1;
            uint32_t f : 1;
            uint32_t c : 1;
            uint32_t opType : 5;
            uint32_t so : 1;
            uint32_t rsvd0 : 3;
            uint32_t dif : 1;
            uint32_t ext : 1;
            uint32_t xrcSrqn : 18;
        } bs;
        uint32_t value;
    } dw0;
} RoceWqeTaskSeg;

typedef struct {
    uint32_t bufAddrHigh32;
    uint32_t bufAddrLow32;
    uint32_t rLen;
    uint32_t leKey;
} RoceWqeDataSeg;

typedef struct {
    RoceWqeCtrlSeg ctrl;
    uint64_t doorbell;
    RoceWqeTaskSeg task;
    uint32_t dataLen;
    uint32_t immeData;
    uint32_t firstLast : 1;
    uint32_t nxtEthHdr : 7;
    uint32_t cmdLen : 8;
    uint32_t rsvd0 : 8;
    uint32_t lastExtLen : 8;
    uint32_t vaHigh32;
    uint32_t vaLow32;
    uint32_t rKey;
    uint32_t rsvd1;
    RoceWqeDataSeg data;
} RoceWqeEntry;

typedef struct {
    uint32_t cqe0;
    uint32_t cqe1;
    uint32_t cqe2;
    uint32_t cqe3;
    uint32_t cqe4;
    uint32_t cqe5;
    uint32_t cqe6;
    uint32_t cqe7;
} RoceCqeEntry;

typedef struct {
    union {
        struct {
            uint32_t pi : 8;
            uint32_t resv : 8;
            uint32_t xrcvld : 1;
            uint32_t vxlan : 1;
            uint32_t mtuShift : 3;
            uint32_t sgidIndex : 7;
            uint32_t queueId : 4;
            uint32_t qpn : 20;
            uint32_t cntxSize : 2;
            uint32_t n : 1;
            uint32_t c : 1;
            uint32_t cos : 3;
            uint32_t type : 5;
        } bs;
        uint64_t value;
    } dw0;
} RoceDbEntry;

#define HCOMM_WQE_BDSL_OFFSET 0
#define HCOMM_WQE_TSL_OFFSET 16
#define HCOMM_WQE_VA_OFFSET 21
#define HCOMM_WQE_CR_OFFSET 23
#define HCOMM_WQE_CTRLSL_OFFSET 29
#define HCOMM_WQE_CL_OFFSET 28
#define HCOMM_WQE_OWNER_OFFSET 31
#define HCOMM_WQE_OP_TYPE_OFFSET 24
#define HCOMM_WQE_C_OFFSET 29

constexpr int32_t HCOMM_FAILED = -1;
constexpr int32_t HCOMM_SUCCESS = 0;
constexpr uint32_t HCOMM_MEM_BLOCK_SIZE = 32;

} // namespace AscendC
#endif // IMPL_HCOMM_HCOMM_INNER_DEF_H
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_INNER_DEF_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_INNER_DEF_H__
#endif
