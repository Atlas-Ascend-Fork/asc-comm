/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_RES_ENTITY_DEFS_H
#define HCOMM_RES_ENTITY_DEFS_H

#include <stdint.h>
#include <stddef.h>
#include "hcomm_res_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t ChannelPtr;

typedef enum {
    PROTECTION_TYPE_INVALID = -1,
    PROTECTION_TYPE_ROCE = 0,
    PROTECTION_TYPE_UB = 1,
} ProtectionType;

typedef enum {
    SQ_CONTEXT_TYPE_INVALID = -1,
    SQ_CONTEXT_TYPE_UB_JFS = 0,
    SQ_CONTEXT_TYPE_ROCE = 1,
} SqContextType;

typedef enum {
    CQ_CONTEXT_TYPE_INVALID = -1,
    CQ_CONTEXT_TYPE_UB_JFC = 0,
    CQ_CONTEXT_TYPE_ROCE = 1,
} CqContextType;

typedef enum {
    REGED_NOTIFY_INVALID = -1,
    REGED_NOTIFY_IPC_RT = 0,
    REGED_NOTIFY_IPC_MEM = 1,
    REGED_NOTIFY_RMA_RT = 2,
    REGED_NOTIFY_RMA_MEM = 3,
} RegedNotifyType;

typedef enum {
    REGED_BUFFER_INVALID = -1,
    REGED_BUFFER_IPC = 0,
    REGED_BUFFER_RMA = 1,
} RegedBufferType;

typedef struct {
    ProtectionType type;
    union {
        struct {
            uint32_t lkey;
            uint32_t rkey;
        } roce;
        struct {
            uint32_t tokenId;
            uint32_t tokenValue;
        } ub;
        uint8_t raws[24];
    } memInfo;
} ProtectionInfo; // 32B

typedef struct {
    RegedBufferType type;
    union {
        struct {
            uint64_t addr;
            uint64_t size;
        } ipc;
        struct {
            uint64_t addr;
            uint64_t size;
            ProtectionInfo protectionInfo;
        } rma;
        uint8_t raws[56];
    } bufferInfo;
} RegedBufferEntity; // 64B

typedef struct {
    RegedNotifyType type;
    union {
        struct {
            uint64_t addr;
            uint32_t size;
            int32_t notifyId;
        } ipcRt;
        struct {
            uint64_t addr;
            uint32_t size;
        } ipcMem;
        struct {
            uint64_t addr;
            uint32_t size;
            int32_t notifyId;
            ProtectionInfo protectionInfo;
        } rmaRt;
        struct {
            uint64_t addr;
            uint32_t size;
            ProtectionInfo protectionInfo;
        } rmaMem;
        uint8_t raws[56];
    } notifyInfo;
} RegedNotifyEntity; // 64B

typedef struct {
    SqContextType type;
    union {
        struct {
            uint64_t sqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbVa;
            uint32_t jfsID;
            uint32_t wqeSize;
            uint32_t sqDepth;
            uint32_t tpID;
            uint8_t remoteEID[16];
        } ubJfs;
        struct {
            uint64_t sqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbHwVa;
            uint64_t dbSwVa;
            uint32_t qpn;
            uint32_t wqeSize;
            uint32_t depth;
            uint8_t sl;
            uint64_t dbVendorSpecified;
        } roceSq;
        uint8_t raws[120];
    } contextInfo;
} SqContext;

typedef struct {
    CqContextType type;
    union {
        struct {
            uint64_t scqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbVa;
            uint32_t jfcID;
            uint32_t cqeSize;
            uint32_t cqDepth;
        } ubJfc;
        struct {
            uint64_t cqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbHwVa;
            uint64_t dbSwVa;
            uint32_t cqn;
            uint32_t cqeSize;
            uint32_t cqDepth;
            uint64_t dbVendorSpecified;
        } roceCq;
        uint8_t raws[120];
    } contextInfo;
} CqContext;

#define HCOMM_RESOURCE_CHANNEL_VERSION 0U             ///< Channel resource ABI版本号
#define HCOMM_RESOURCE_CHANNEL_MAGIC_WORD 0x0fcf0f5fU ///< Channel 资源合法性校验魔术字
#define HCOMM_RESOURCE_THREAD_VERSION 0U              ///< Thread resource ABI版本号
#define HCOMM_RESOURCE_THREAD_MAGIC_WORD 0x0fcf0f6fU  ///< Thread 资源合法性校验魔术字
#define HCOMM_RESOURCE_RTSQ_SQE_SIZE 64U              ///< 单个RTSQ SQE占用字节数
#define HCOMM_RESOURCE_RTSQ_BATCH_SIZE 128U           ///< 本地最多缓存的RTSQ SQE数量
#define HCOMM_RESOURCE_RTSQ_TASK_ID_BATCH_SIZE 1024U  ///< 单次申请的RTSQ Task ID数量
#define HCOMM_RESOURCE_CHANNEL_ENTITY_SIZE 256U       ///< Channel resource ABI固定大小
#define HCOMM_RESOURCE_THREAD_ENTITY_SIZE 256U        ///< Thread resource ABI固定大小

#pragma pack(push, 4)

typedef enum {
    HCOMM_PROTECTION_TYPE_INVALID = -1,
    HCOMM_PROTECTION_TYPE_ROCE = 0,
    HCOMM_PROTECTION_TYPE_RDMA = HCOMM_PROTECTION_TYPE_ROCE,
    HCOMM_PROTECTION_TYPE_UB = 1,
    HCOMM_PROTECTION_TYPE_URMA = HCOMM_PROTECTION_TYPE_UB,
} HcommProtectionType;

typedef enum {
    HCOMM_SQ_CONTEXT_TYPE_INVALID = -1,
    HCOMM_SQ_CONTEXT_TYPE_UB_JFS = 0,
    HCOMM_SQ_CONTEXT_TYPE_ROCE = 1,
} HcommSqContextType;

typedef enum {
    HCOMM_CQ_CONTEXT_TYPE_INVALID = -1,
    HCOMM_CQ_CONTEXT_TYPE_UB_JFC = 0,
    HCOMM_CQ_CONTEXT_TYPE_ROCE = 1,
} HcommCqContextType;

typedef enum {
    HCOMM_REGED_NOTIFY_INVALID = -1,
    HCOMM_REGED_NOTIFY_IPC_RT = 0,
    HCOMM_REGED_NOTIFY_IPC_MEM = 1,
    HCOMM_REGED_NOTIFY_RMA_RT = 2,
    HCOMM_REGED_NOTIFY_RMA_MEM = 3,
} HcommRegedNotifyType;

typedef enum {
    HCOMM_REGED_BUFFER_INVALID = -1,
    HCOMM_REGED_BUFFER_IPC = 0,
    HCOMM_REGED_BUFFER_RMA = 1,
} HcommRegedBufferType;

typedef enum {
    HCOMM_NOTIFY_TYPE_HCCS = 0,
    HCOMM_NOTIFY_TYPE_RMA = 1,
    HCOMM_NOTIFY_TYPE_LOCAL = 2,
} HcommNotifyType;

typedef struct {
    HcommProtectionType type;
    union {
        struct {
            uint32_t lkey;
            uint32_t rkey;
        } roce;
        struct {
            uint32_t tokenId;
            uint32_t tokenValue;
        } ub;
        uint8_t raws[24];
    } memInfo;
} HcommProtectionInfo;

typedef struct {
    HcommRegedBufferType type;
    union {
        struct {
            uint64_t addr;
            uint64_t size;
        } ipc;
        struct {
            uint64_t addr;
            uint64_t size;
            HcommProtectionInfo protectionInfo;
        } rma;
        uint8_t raws[56];
    } bufferInfo;
} HcommRegedBufferEntity;

typedef struct {
    HcommRegedNotifyType type;
    union {
        struct {
            uint64_t addr;
            uint32_t size;
            int32_t notifyId;
        } ipcRt;
        struct {
            uint64_t addr;
            uint32_t size;
        } ipcMem;
        struct {
            uint64_t addr;
            uint32_t size;
            int32_t notifyId;
            HcommProtectionInfo protectionInfo;
        } rmaRt;
        struct {
            uint64_t addr;
            uint32_t size;
            HcommProtectionInfo protectionInfo;
        } rmaMem;
        uint8_t raws[56];
    } notifyInfo;
} HcommRegedNotifyEntity;

typedef struct {
    HcommSqContextType type; ///< SQ上下文类型
    uint8_t fence; ///< 一次性Fence标志，由下一次相关传输消费并清零(独立字段，不与union重叠)
    uint8_t reserved[3]; ///< 对齐保留
    union {
        struct {
            uint64_t sqVa;     ///< UB SQ的CPU虚拟地址(WQE写入位置)
            uint64_t headAddr; ///< SQ硬件Head地址(预留)
            uint64_t tailAddr; ///< SQ CI地址(软件轮询预留，当前恒0)
            uint64_t dbVa;     ///< Doorbell寄存器地址(预留，doorbell走RTSQ UBDMA SQE)
            uint32_t jfsID;    ///< Jetty ID
            uint32_t wqeSize;  ///< 单个WQE字节数(64)
            uint32_t sqDepth;  ///< WQEBB深度(控制面已乘4，数据面直用)
            uint32_t tpID;     ///< TP号
            uint8_t remoteEID[16]; ///< 远端EID【硬件逆序形态】；控制面已Reversed()，数据面直取填入WQE.rmtEid
            uint32_t sqHead;       ///< 软件维护的SQ生产者索引(pi，数据面持续自增)
            uint32_t sqTail;       ///< 已完成WQE计数(ci，数据面维护；当前无消费方可恒0)
            uint32_t maxReadSize;  ///< 单个UB Read WQE允许的最大传输字节数
            uint32_t maxWriteSize; ///< 单个UB Write WQE允许的最大传输字节数
        } ubJfs;
        struct {
            uint64_t sqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbHwVa;
            uint64_t dbSwVa;
            uint32_t qpn;
            uint32_t wqeSize;
            uint32_t depth;
            uint8_t sl;
            uint64_t dbVendorSpecified; ///< RoCE厂商Doorbell字段，包含mtuShift和dbCos
            uint32_t sqHead;            ///< 软件维护的SQ生产者索引
            uint32_t sqTail;            ///< 已完成WQE计数
        } roceSq;
        uint8_t raws[120];
    } contextInfo;
} HcommSqContext;

typedef struct {
    HcommCqContextType type; ///< CQ上下文类型
    union {
        struct {
            uint64_t scqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbVa;
            uint32_t jfcID;
            uint32_t cqeSize;
            uint32_t cqDepth;
        } ubJfc;
        struct {
            uint64_t cqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbHwVa;
            uint64_t dbSwVa;
            uint32_t cqn;
            uint32_t cqeSize;
            uint32_t cqDepth;
            uint32_t cqHead;   ///< 软件轮询的下一个CQE索引
            uint32_t cqTail;   ///< 已消费CQE计数
            uint8_t cqDbFlush; ///< 是否需要刷新CQ软件Doorbell
        } roceCq;
        uint8_t raws[124];
    } contextInfo;
} HcommCqContext;

typedef struct {
    uint32_t version;    ///< 设备信息结构版本号
    uint32_t devId;      ///< 设备物理或逻辑ID，由资源创建端约定
    uint8_t dieId;       ///< 设备Die编号(UB doorbell jetty三元素之一)
    uint8_t funcId;      ///< 设备Function编号(UB doorbell jetty三元素之一)
    uint8_t reserve[30]; ///< 设备信息扩展空间，创建资源时置0
} HcommDevInfo;

typedef struct {
    CommAbiHeader abiHeader;
    CommEngine engine;
    CommProtocol protocol;
    HcommDevInfo devInfo; ///< Channel本地通信资源所属设备信息(dieId/funcId为doorbell唯一来源)
    uint32_t localNotifyNum;
    uint32_t remoteNotifyNum;
    uint32_t localBufferNum;
    uint32_t remoteBufferNum;
    uint32_t sqNum;
    uint32_t cqNum;
    HcommRegedNotifyEntity* localNotifyAddr;
    HcommRegedNotifyEntity* remoteNotifyAddr;
    HcommRegedBufferEntity* localBufferAddr;
    HcommRegedBufferEntity* remoteBufferAddr;
    HcommSqContext* sqContextAddr;
    HcommCqContext* cqContextAddr;
    ChannelHandle originChannelHandle; ///< 控制面原始Channel句柄值；数据面只作关联信息，不解引用
} ChannelEntity;

// RTSQ硬件字段由HCOMM控制面在资源构造阶段查询并填充；数据面只消费初始快照，
// 并在提交过程中维护运行态head/tail。
typedef struct {
    uint32_t localDevId;    ///< 调用halSqCqQuery/halSqCqConfig使用的本地设备ID(控制面填充)
    uint32_t streamId;      ///< RTSQ关联的Stream ID，写入部分SQE或用于诊断
    uint32_t sqId;          ///< 硬件RTSQ编号
    uint32_t sqDepth;       ///< 硬件RTSQ可容纳的SQE数量
    uint32_t sqHead;        ///< 最近一次查询到的硬件RTSQ Head
    uint32_t sqTail;        ///< 资源运行时维护的硬件RTSQ Tail
    uint64_t sqBaseAddr;    ///< 硬件RTSQ SQE环形缓冲区的CPU虚拟地址
    uint64_t localSqeAddr;  ///< 控制面申请、数据面维护的本地SQE缓存地址
    uint32_t taskId;        ///< 数据面下一次使用的Task ID，控制面创建资源时置0
    uint32_t taskIdEnd;     ///< 当前已申请Task ID区间的开区间上界，控制面创建资源时置0
    uint32_t pendingSqeCnt; ///< 本地缓存中尚未提交的SQE数量
    uint8_t launchFlag;     ///< 非0时在非Batch模式下每生成一个SQE立即提交
    uint8_t reserved[3];    ///< RTSQ状态扩展及对齐空间，创建资源时置0
} HcommRtsqContext;

typedef struct {
    CommAbiHeader abiHeader;                 ///< ABI版本、魔术字和结构大小，用于句柄合法性校验
    HcommDevInfo devInfo;                    ///< Thread所属设备信息
    uint32_t localNotifyNum;                 ///< localNotifyAddr数组元素个数
    HcommRegedNotifyEntity* localNotifyAddr; ///< Thread本地Notify数组，用于线程间Record/Wait(type=IPC_RT)
    HcommRtsqContext* sqContextAddr;         ///< Thread直接持有的resource RTSQ运行上下文
    ThreadHandle originThreadHandle; ///< 控制面原始Thread句柄值；数据面只作关联信息，不解引用
    uint8_t raw[172];                ///< Thread固定256字节中的ABI预留扩展空间

} ThreadEntity;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <type_traits>
using HcommRdmaMemProtectionInfo = decltype(((HcommProtectionInfo*)nullptr)->memInfo.roce);
using HcommUrmaMemProtectionInfo = decltype(((HcommProtectionInfo*)nullptr)->memInfo.ub);
using HcommJfsContext = decltype(((HcommSqContext*)nullptr)->contextInfo.ubJfs);
using HcommRdmaSqContext = decltype(((HcommSqContext*)nullptr)->contextInfo.roceSq);
using HcommRdmaCqContext = decltype(((HcommCqContext*)nullptr)->contextInfo.roceCq);
static_assert(std::is_pod<HcommProtectionInfo>::value, "HcommProtectionInfo must be POD");
static_assert(std::is_pod<HcommRegedBufferEntity>::value, "HcommRegedBufferEntity must be POD");
static_assert(std::is_pod<HcommRegedNotifyEntity>::value, "HcommRegedNotifyEntity must be POD");
static_assert(std::is_pod<HcommSqContext>::value, "HcommSqContext must be POD");
static_assert(std::is_pod<HcommCqContext>::value, "HcommCqContext must be POD");
static_assert(
    sizeof(ChannelEntity) <= HCOMM_RESOURCE_CHANNEL_ENTITY_SIZE, "ChannelEntity ABI size must not exceed 256 bytes");
static_assert(sizeof(ThreadEntity) == HCOMM_RESOURCE_THREAD_ENTITY_SIZE, "ThreadEntity ABI size must be 256 bytes");
static_assert(alignof(ChannelEntity) == 4, "ChannelEntity ABI alignment must be 4 bytes");
static_assert(alignof(ThreadEntity) == 4, "ThreadEntity ABI alignment must be 4 bytes");
static_assert(
    offsetof(ThreadEntity, originThreadHandle) + sizeof(ThreadHandle) <= HCOMM_RESOURCE_THREAD_ENTITY_SIZE,
    "ThreadEntity origin handle must fit in the fixed ABI storage");
static_assert(
    offsetof(ChannelEntity, originChannelHandle) + sizeof(ChannelHandle) <= HCOMM_RESOURCE_CHANNEL_ENTITY_SIZE,
    "ChannelEntity origin handle must fit in the fixed ABI storage");
#endif

#endif // HCOMM_RES_ENTITY_DEFS_H
