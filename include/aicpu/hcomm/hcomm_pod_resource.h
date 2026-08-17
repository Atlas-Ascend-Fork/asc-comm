/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_POD_RESOURCE_H
#define HCOMM_POD_RESOURCE_H

// POD ABI resource definitions shared by resource producers and POD primitives.

#include <stdint.h>
#include "hcomm_res_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HCOMM_POD_CHANNEL_VERSION 0U             ///< Channel POD ABI版本号
#define HCOMM_POD_CHANNEL_MAGIC_WORD 0x0fcf0f5fU ///< Channel POD合法性校验魔术字
#define HCOMM_POD_THREAD_VERSION 0U              ///< Thread POD ABI版本号
#define HCOMM_POD_THREAD_MAGIC_WORD 0x0fcf0f6fU  ///< Thread POD合法性校验魔术字
#define HCOMM_POD_RTSQ_SQE_SIZE 64U              ///< 单个RTSQ SQE占用字节数
#define HCOMM_POD_RTSQ_BATCH_SIZE 128U           ///< 本地最多缓存的RTSQ SQE数量

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
            uint32_t dieId;          ///**add** UB Jetty所在Die编号
            uint32_t funcId;         ///**add** UB功能号
            uint32_t maxReadSize;    ///**add** 单个UB Read WQE允许的最大传输字节数
            uint32_t maxWriteSize;   ///**add** 单个UB Write WQE允许的最大传输字节数
            uint16_t pi;             ///**add** UB SQ软件生产者索引，由数据面持续维护
            uint32_t piDetourCount;  ///**add** PI绕过SQ尾部的累计次数，由数据面持续维护
//pi 和 piDetourCount 不需要控制面填成最终值。控制面只负责提供持久存储并初始化， 后续完全由 asc-comm 数据面维护。
            uint8_t dwqeCacheLocked; ///**add** DWQE缓存是否已锁定；当前控制面初始化为0
            uint8_t reserved[3];     ///**add** 对齐及后续状态扩展空间，创建资源时置0
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
            uint8_t mtuShift;
            uint32_t sqHead; ///**add** 软件维护的SQ生产者索引
            uint32_t sqTail; ///**add** 已完成WQE计数
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
            uint32_t cqHead;     ///**add** 软件轮询的下一个CQE索引
            uint32_t cqTail;     ///**add** 已消费CQE计数
            //内部维护
            uint8_t cqDbFlush;   ///**add** 是否需要刷新CQ软件Doorbell
            uint8_t reserved[3]; ///**add** 对齐及后续状态扩展空间，创建资源时置0
        } roceCq;
        uint8_t raws[120];
    } contextInfo;
} HcommCqContext;

typedef struct {
    uint64_t address;                       ///< RMA Notify内存地址
    int32_t notifyId;                       ///< Notify资源ID；负值表示无效
    uint32_t size;                          ///< Notify内存字节数
    HcommProtectionInfo protectionInfo;     ///< Notify内存的RDMA或URMA保护信息
} HcommRmaNotify;

typedef struct {
    uint32_t type;      ///< Notify类型判别值，由资源生产端和消费端约定
    uint32_t reserved;  ///**add** 保留字段，创建资源时置0
    union {
        struct {
            uint64_t address;  ///< HCCS Notify资源地址
            int32_t notifyId;  ///< Notify资源ID；负值表示无效
            uint32_t size;     ///< Notify资源占用字节数
        } hccs;                ///< HCCS场景的Notify描述
        HcommRmaNotify rma;    ///< UB/RoCE场景的RMA Notify描述
        int8_t reserve[64];    ///< Notify描述扩展空间
    } notify;                  ///< 类型相关的Notify资源
} HcommNotify;

typedef struct {
    uint32_t version;     ///< 设备信息结构版本号
    uint32_t devId;       ///< 设备物理或逻辑ID，由资源创建端约定
    uint8_t dieId;        ///< 设备Die编号
    uint8_t funcId;       ///< 设备Function编号
    uint8_t reserve[30];  ///< 设备信息扩展空间，创建资源时置0
} HcommDevInfo;

typedef struct {
    CommAbiHeader abiHeader;
    CommEngine engine;
    CommProtocol protocol;
    uint32_t localNotifyNum;
    uint32_t remoteNotifyNum;
    uint32_t localBufferNum;
    uint32_t remoteBufferNum;
    uint32_t sqNum;
    uint32_t cqNum;
    HcommRegedNotifyEntity *localNotifyAddr;
    HcommRegedNotifyEntity *remoteNotifyAddr;
    HcommRegedBufferEntity *localBufferAddr;
    HcommRegedBufferEntity *remoteBufferAddr;
    HcommSqContext *sqContextAddr;
    HcommCqContext *cqContextAddr;
    uint8_t reserve[160];

    // 放普通notify和buffer最后一个里面
    HcommRegedBufferEntity notifyValueBuffer; ///**add** RoCE Record使用的本地Notify值缓冲区
    HcommRegedNotifyEntity localDrainNotify;  ///**add** UB Drain读取完成后等待的本端Notify资源
    HcommRegedBufferEntity remoteDrainBuffer; ///**add** UB Drain发起Read时访问的远端常量缓冲区
    uint8_t fence;                            ///**add** 一次性Fence标志，由下一次相关传输消费并清零
    uint8_t stateReserved[3];                 ///**add** Channel运行状态扩展空间，创建资源时置0
    uint8_t podReserve[1024];                 ///< Channel POD ABI预留扩展空间，创建资源时置0
} HcommChannelEntity;

typedef struct {
    uint32_t devPhyId;       ///**add** Stream所属物理设备ID，保留用于资源标识和任务上报
    uint32_t localDevId;     ///**add** 调用halSqCqQuery/halSqCqConfig使用的本地设备ID
    uint32_t streamId;       ///**add** RTSQ关联的Stream ID，写入部分SQE或用于诊断
    uint32_t sqId;           ///< 硬件RTSQ编号
    uint32_t sqDepth;        ///< 硬件RTSQ可容纳的SQE数量
    uint32_t sqHead;         ///**add** 最近一次查询到的硬件RTSQ Head
    uint32_t sqTail;         ///**add** POD运行时维护的硬件RTSQ Tail
    uint32_t taskId;         ///**add** 下一个SQE使用的任务ID基值
    uint32_t taskIdEnd;      ///**add** 当前批次任务ID的结束边界，到达后重新申请
    uint32_t pendingSqeCnt;  ///**add** localSqeBuffer中尚未提交到硬件RTSQ的SQE数量
    uint32_t sqFullTimeout;  ///**add** 等待硬件RTSQ空闲的超时秒数；0表示不启用超时判断
    uint64_t sqBaseAddr;     ///< 硬件RTSQ SQE环形缓冲区的CPU虚拟地址
    uint8_t launchFlag;      ///**add** 非0时在非Batch模式下每生成一个SQE立即提交
    uint8_t reserved[3];     ///**add** RTSQ状态扩展及对齐空间，创建资源时置0
    uint8_t localSqeBuffer[HCOMM_POD_RTSQ_SQE_SIZE * HCOMM_POD_RTSQ_BATCH_SIZE]; ///**add** 待提交SQE的本地连续缓存
} HcommRtsqContext;

typedef struct {
    CommAbiHeader abiHeader;               ///< ABI版本、魔术字和结构大小，用于句柄合法性校验
    HcommDevInfo devInfo;                  ///< Thread所属设备信息
    uint32_t localNotifyNum;               ///< localNotifyAddr数组元素个数
    HcommNotify *localNotifyAddr;          ///< Thread本地Notify数组，用于线程间Record/Wait
    HcommRtsqContext *sqContextAddr;       ///< Thread直接持有的POD RTSQ运行上下文
    uint8_t reserve[1024];                 ///< Thread ABI预留扩展空间，创建资源时置0
} HcommThreadEntity;

#pragma pack(pop)

#if defined(__GNUC__)
#define HCOMM_POD_RESOURCE_API __attribute__((visibility("default")))
#else
#define HCOMM_POD_RESOURCE_API
#endif

HCOMM_POD_RESOURCE_API int32_t HcommGetPodThreadResource(ThreadHandle thread, HcommThreadEntity **resource);
HCOMM_POD_RESOURCE_API int32_t HcommGetPodChannelResource(ChannelHandle channel, HcommChannelEntity **resource);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <type_traits>
using HcommRdmaMemProtectionInfo = decltype(((HcommProtectionInfo *)nullptr)->memInfo.roce);
using HcommUrmaMemProtectionInfo = decltype(((HcommProtectionInfo *)nullptr)->memInfo.ub);
using HcommJfsContext = decltype(((HcommSqContext *)nullptr)->contextInfo.ubJfs);
using HcommRdmaSqContext = decltype(((HcommSqContext *)nullptr)->contextInfo.roceSq);
using HcommRdmaCqContext = decltype(((HcommCqContext *)nullptr)->contextInfo.roceCq);
using HcommHccsNotify = decltype(((HcommNotify *)nullptr)->notify.hccs);
static_assert(std::is_pod<HcommProtectionInfo>::value, "HcommProtectionInfo must be POD");
static_assert(std::is_pod<HcommChannelEntity>::value, "HcommChannelEntity must be POD");
static_assert(std::is_pod<HcommThreadEntity>::value, "HcommThreadEntity must be POD");
static_assert(alignof(HcommChannelEntity) == 4, "HcommChannelEntity ABI alignment must be 4 bytes");
static_assert(alignof(HcommThreadEntity) == 4, "HcommThreadEntity ABI alignment must be 4 bytes");
#endif

#endif // HCOMM_POD_RESOURCE_H
