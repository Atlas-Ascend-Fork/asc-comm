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

#define HCOMM_POD_CHANNEL_VERSION 0U               ///< Channel POD ABI版本号
#define HCOMM_POD_CHANNEL_MAGIC_WORD 0x0fcf0f5fU  ///< Channel POD合法性校验魔术字
#define HCOMM_POD_THREAD_VERSION 0U                ///< Thread POD ABI版本号
#define HCOMM_POD_THREAD_MAGIC_WORD 0x0fcf0f6fU   ///< Thread POD合法性校验魔术字
#define HCOMM_POD_RTSQ_SQE_SIZE 64U                ///< 单个RTSQ SQE占用字节数
#define HCOMM_POD_RTSQ_BATCH_SIZE 128U             ///< 本地最多缓存的RTSQ SQE数量

#pragma pack(push, 4)

typedef enum {
    HCOMM_PROTECTION_TYPE_RDMA = 0,  ///< 使用RoCE/RDMA的lkey和rkey进行内存保护
    HCOMM_PROTECTION_TYPE_URMA = 1,  ///< 使用UB/URMA的tokenId和tokenValue进行内存保护
} HcommProtectionType;

typedef struct {
    uint32_t lkey;  ///< 本端RDMA内存区域访问密钥
    uint32_t rkey;  ///< 远端RDMA内存区域访问密钥
} HcommRdmaMemProtectionInfo;

typedef struct {
    uint32_t tokenId;     ///< URMA内存对象或Token标识
    uint32_t tokenValue;  ///< URMA远端访问校验值；本地SGE只使用tokenId
} HcommUrmaMemProtectionInfo;

typedef struct {
    uint32_t type;      ///< HcommProtectionType，指明protection联合体的有效成员
    uint32_t reserved;  ///**add** 保留字段，创建资源时必须置0
    uint64_t addr;      ///< 受保护内存区域的起始虚拟地址
    uint64_t length;    ///< 受保护内存区域的字节长度
    union {
        HcommRdmaMemProtectionInfo rdma;  ///< type为RDMA时有效
        HcommUrmaMemProtectionInfo urma;  ///< type为URMA时有效
        int8_t reserve[32];               ///< 联合体扩展空间，保证后续ABI可扩展
    } protection;                         ///< 协议相关的内存保护参数
} HcommProtectionInfo;

typedef struct {
    uint32_t dieId;            ///**add** UB Jetty所在Die编号，对应PodUbConnLite::dieId_
    uint32_t funcId;           ///**add** UB功能号，对应PodUbConnLite::funcId_
    uint32_t jettyId;          ///< UB Jetty编号，对应PodUbConnLite::jettyId_
    uint32_t tpn;              ///< 远端传输路径号，写入UB WQE的tpn字段
    uint32_t sqDepth;          ///< UB SQ的WQEBB深度，直接对应PodUbConnLite::sqDepth_
    uint32_t jfcPollMode;      ///**add** JFC完成队列轮询模式：0为STARS轮询，1为软件轮询
    uint32_t maxReadSize;      ///**add** 单个UB Read WQE允许的最大传输字节数
    uint32_t maxWriteSize;     ///**add** 单个UB Write WQE允许的最大传输字节数
    uint64_t sqVa;             ///< UB SQ WQEBB区域的CPU虚拟地址
    uint64_t dbVa;             ///< UB Doorbell地址
    uint64_t sqCiAddr;         ///**add** 软件轮询JFC时使用的SQ CI地址
    uint8_t remoteEid[16];     ///< 远端URMA EID，写入UB WQE
    uint8_t localEid[16];      ///**add** 本端URMA EID，用于连接标识和任务信息
    uint16_t pi;               ///**add** UB SQ软件生产者索引，跨接口调用持续递增
    uint16_t ci;               ///**add** UB SQ消费者索引，供完成状态维护使用
    uint32_t piDetourCount;    ///**add** PI绕过SQ尾部的累计次数
    uint32_t ciDetourCount;    ///**add** CI绕过SQ尾部的累计次数
    uint8_t dwqeCacheLocked;   ///**add** DWQE缓存是否已锁定；非0时不重复向sqVa拷贝WQE
    uint8_t reserved[3];       ///**add** 对齐及后续状态扩展空间，创建资源时置0
} HcommJfsContext;

typedef struct {
    uint32_t qpn;       ///< RoCE Queue Pair编号
    uint32_t wqeSize;   ///< 单个RoCE WQE的字节数
    uint32_t depth;     ///< RoCE SQ深度，当前实现要求为2的幂
    uint8_t sl;         ///< RoCE Service Level
    uint8_t mtuShift;   ///**add** MTU编码，写入RoCE Doorbell
    uint8_t dmaMode;    ///**add** DMA后端模式；当前POD实现支持值1（独立UB DMA）
    uint8_t reserved;   ///**add** 对齐及扩展字段，创建资源时置0
    uint64_t sqVa;      ///< RoCE SQ WQE区域的CPU虚拟地址
    uint64_t headAddr;  ///< 硬件或共享SQ Head地址，保留给队列状态同步
    uint64_t tailAddr;  ///< 硬件或共享SQ Tail地址，保留给队列状态同步
    uint64_t dbHwVa;    ///< RoCE SQ硬件Doorbell地址
    uint64_t dbSwVa;    ///**add** RoCE SQ软件PI写回地址
    uint32_t sqHead;    ///**add** 软件维护的SQ生产者索引，对应PodRdmaBaseOps::sqHead_
    uint32_t sqTail;    ///**add** 已完成WQE计数，对应PodRdmaBaseOps::sqTail_
} HcommRdmaSqContext;

typedef struct {
    uint32_t type;      ///< SQ上下文类型判别值，由资源生产端和消费端约定
    uint32_t reserved;  ///**add** 保留字段，创建资源时置0
    union {
        HcommJfsContext jfs;       ///< UB协议使用的JFS上下文
        HcommRdmaSqContext rdma;   ///< RoCE协议使用的SQ上下文
        int8_t reserve[128];       ///< SQ上下文扩展空间
    } context;                     ///< 协议相关的SQ上下文
} HcommSqContext;

typedef struct {
    uint32_t jfcId;      ///< UB Jetty完成队列标识
    uint32_t cqeSize;    ///< 单个JFC CQE的字节数
    uint32_t cqDepth;    ///< JFC完成队列深度
    uint32_t reserved;   ///**add** 保留字段，创建资源时置0
    uint64_t cqVa;       ///< JFC CQE区域的CPU虚拟地址
    uint64_t headAddr;   ///< JFC Head状态地址
    uint64_t tailAddr;   ///< JFC Tail状态地址
    uint64_t dbVa;       ///< JFC Doorbell地址
} HcommJfcContext;

typedef struct {
    uint32_t cqn;              ///< RoCE Completion Queue编号
    uint32_t cqeSize;          ///< 单个RoCE CQE的字节数
    uint32_t cqDepth;          ///< RoCE CQ深度
    uint32_t reserved;         ///**add** 保留字段，创建资源时置0
    uint64_t cqVa;             ///< RoCE CQE区域的CPU虚拟地址
    uint64_t headAddr;         ///< 硬件或共享CQ Head地址
    uint64_t tailAddr;         ///< 硬件或共享CQ Tail地址
    uint64_t dbHwVa;           ///< RoCE CQ硬件Doorbell地址
    uint64_t dbSwVa;           ///**add** RoCE CQ软件CI写回地址
    uint32_t cqHead;           ///**add** 软件轮询的下一个CQE索引，对应PodRdmaBaseOps::cqHead_
    uint32_t cqTail;           ///**add** 已消费CQE计数，对应PodRdmaBaseOps::cqTail_
    uint8_t cqDbFlush;         ///**add** 是否需要刷新CQ软件Doorbell
    uint8_t stateReserved[3];  ///**add** CQ运行状态扩展空间，创建资源时置0
} HcommRdmaCqContext;

typedef struct {
    uint32_t type;      ///< CQ上下文类型判别值，由资源生产端和消费端约定
    uint32_t reserved;  ///**add** 保留字段，创建资源时置0
    union {
        HcommJfcContext jfc;       ///< UB协议使用的JFC上下文
        HcommRdmaCqContext rdma;   ///< RoCE协议使用的CQ上下文
        int8_t reserve[128];       ///< CQ上下文扩展空间
    } context;                     ///< 协议相关的CQ上下文
} HcommCqContext;

typedef struct {
    uint64_t address;  ///< HCCS Notify资源地址
    int32_t notifyId;  ///< Notify资源ID；负值表示无效
    uint32_t size;     ///< Notify资源占用字节数
} HcommHccsNotify;

typedef struct {
    uint64_t address;                       ///< RMA Notify内存地址
    int32_t notifyId;                       ///< Notify资源ID；负值表示无效
    uint32_t size;                          ///< Notify内存字节数
    HcommProtectionInfo protectionInfo;     ///< Notify内存的RDMA或URMA保护信息
} HcommRmaNotify;

typedef struct {
    int32_t notifyId;  ///< 本地信号量Notify资源ID；负值表示无效
} HcommLocalNotify;

typedef enum {
    HCOMM_NOTIFY_TYPE_HCCS = 0,   ///< HCCS场景的Notify描述
    HCOMM_NOTIFY_TYPE_RMA = 1,    ///< UB/RoCE场景的RMA Notify描述
    HCOMM_NOTIFY_TYPE_LOCAL = 2,  ///< 本地信号量Notify，仅携带notifyId
} HcommNotifyType;

typedef struct {
    uint32_t type;      ///< HcommNotifyType，指明notify联合体的有效成员
    uint32_t reserved;  ///**add** 保留字段，创建资源时置0
    union {
        HcommHccsNotify hccs;      ///< HCCS场景的Notify描述
        HcommRmaNotify rma;        ///< UB/RoCE场景的RMA Notify描述
        HcommLocalNotify local;    ///< 本地信号量Notify描述，用于local侧线程/通道间同步
        int8_t reserve[64];        ///< Notify描述扩展空间
    } notify;                      ///< 类型相关的Notify资源
} HcommNotify;

typedef struct {
    uint32_t version;     ///< 设备信息结构版本号
    uint32_t devId;       ///< 设备物理或逻辑ID，由资源创建端约定
    uint8_t dieId;        ///< 设备Die编号
    uint8_t funcId;       ///< 设备Function编号
    uint8_t reserve[30];  ///< 设备信息扩展空间，创建资源时置0
} HcommDevInfo;

typedef struct {
    CommAbiHeader abiHeader;                   ///< ABI版本、魔术字和结构大小，用于句柄合法性校验
    CommEngine engine;                         ///< 通信执行引擎类型
    CommProtocol protocol;                     ///< 通信协议；POD传输当前处理UB系列和RoCE
    HcommDevInfo devInfo;                      ///< Channel所属设备信息
    uint32_t localNotifyNum;                   ///< localNotifyAddr数组元素个数
    HcommNotify *localNotifyAddr;              ///< 本端Notify资源数组，用于Channel Wait
    uint32_t remoteNotifyNum;                  ///< remoteNotifyAddr数组元素个数
    HcommNotify *remoteNotifyAddr;             ///< 远端Notify资源数组，用于Record和带Notify写
    uint32_t localBufferNum;                   ///< localBufferAddr数组元素个数
    HcommProtectionInfo *localBufferAddr;      ///< 本端已注册通信内存及保护信息数组
    uint32_t remoteBufferNum;                  ///< remoteBufferAddr数组元素个数
    HcommProtectionInfo *remoteBufferAddr;     ///< 远端已注册通信内存及保护信息数组
    uint32_t sqNum;                            ///< sqContextAddr数组元素个数
    HcommSqContext *sqContextAddr;             ///< 协议发送队列上下文数组，当前数据面使用索引0
    uint32_t cqNum;                            ///< cqContextAddr数组元素个数
    HcommCqContext *cqContextAddr;             ///< 协议完成队列上下文数组，RoCE当前使用索引0
    HcommProtectionInfo notifyValueBuffer;     ///**add** RoCE Record时作为本地源数据的Notify值缓冲区
    HcommRmaNotify drainNotify;                ///**add** UB Drain读取完成后等待的本端Notify资源
    HcommRmaNotify remoteDrainBuffer;          ///**add** UB Drain发起Read时访问的远端常量缓冲区
    uint8_t fence;                             ///**add** 一次性Fence标志，由下一次相关传输消费并清零
    uint8_t stateReserved[3];                  ///**add** Channel运行状态扩展空间，创建资源时置0
    uint8_t reserve[1024];                     ///< Channel ABI预留扩展空间，创建资源时置0
} HcommChannelEntity;

typedef struct {
    uint32_t localDevId;     ///**add** 调用halSqCqQuery/halSqCqConfig使用的本地设备ID
    uint32_t streamId;       ///**add** RTSQ关联的Stream ID，写入部分SQE或用于诊断
    uint32_t sqId;           ///< 硬件RTSQ编号
    uint32_t sqDepth;        ///< 硬件RTSQ可容纳的SQE数量
    uint32_t sqHead;         ///**add** 最近一次查询到的硬件RTSQ Head
    uint32_t sqTail;         ///**add** POD运行时维护的硬件RTSQ Tail
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

#ifdef __cplusplus
#include <type_traits>
static_assert(std::is_pod<HcommProtectionInfo>::value, "HcommProtectionInfo must be POD");
static_assert(std::is_pod<HcommChannelEntity>::value, "HcommChannelEntity must be POD");
static_assert(std::is_pod<HcommThreadEntity>::value, "HcommThreadEntity must be POD");
static_assert(alignof(HcommChannelEntity) == 4, "HcommChannelEntity ABI alignment must be 4 bytes");
static_assert(alignof(HcommThreadEntity) == 4, "HcommThreadEntity ABI alignment must be 4 bytes");
#endif

#endif // HCOMM_POD_RESOURCE_H
