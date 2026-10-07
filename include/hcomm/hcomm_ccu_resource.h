/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file hcomm_ccu_resource.h
 * @brief CCU instance、resource、channel 跨 SO POD 定义。
 */

#ifndef HCOMM_CCU_RESOURCE_H
#define HCOMM_CCU_RESOURCE_H

#include <stdint.h>
#include "hcomm_res_defs.h"
#include "hcomm/hcomm_res_entity_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 跨 SO POD 的 ABI 头部常量。
 * @note 布局变化时必须同步 bump 对应 POD 的 ABI 版本。
 */
static const uint32_t HCOMM_CCU_INSTANCE_HANDLE_ABI_VERSION = 1U;
static const uint32_t HCOMM_CCU_INSTANCE_HANDLE_MAGIC_WORD = 0x43435548U; ///< "CCUH"
static const uint32_t HCOMM_CCU_CHANNEL_ABI_VERSION = 1U;
static const uint32_t HCOMM_CCU_CHANNEL_MAGIC_WORD = 0x43435543U; ///< "CCUC"
static const uint32_t HCOMM_CCU_RES_DESC_ABI_VERSION = 1U;
static const uint32_t HCOMM_CCU_RES_DESC_MAGIC_WORD = 0x43524453U; ///< "CRDS"
static const uint32_t HCOMM_CCU_RES_ABI_VERSION = 1U;
static const uint32_t HCOMM_CCU_INSTANCE_MAGIC_WORD = 0x43435549U;     ///< "CCUI"
static const uint32_t HCOMM_CCU_DIE_METADATA_MAGIC_WORD = 0x4343444DU; ///< "CCDM"

/**
 * @brief CCU 编译期容量常量。
 * @note 作为数组维度与边界校验使用，跨 SO 双方必须保持一致。
 */
enum {
    HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY = 16, /**< 每个 channel 持有的 XN/CKE 资源槽位数 */
    HCOMM_CCU_RES_DESC_TYPE_COUNT = 32,       /**< 资源描述符类型槽位数（预留到 32，现用 0-7） */
    HCOMM_CCU_MAX_DIE_NUM = 2,           /**< 当前支持的 CCU die 数（现网 2 die；硬件扩展后调整） */
    HCOMM_CCU_RES_TYPE_COUNT = 32,       /**< 单 die 预留的资源类型槽位数（现用 0-10，11-31 预留） */
    HCOMM_CCU_BATCH_RES_TYPE_COUNT = 11, /**< HcommCcuBatchResType 枚举值数量（数组维度用） */
    HCOMM_CCU_INSTANCE_DIE_CAPACITY = 8, /**< Instance 预留的 die 槽位数（现网 2 个） */
    HCOMM_CCU_INSTR_WIDTH_BYTES = 32,    /**< 单条 CCU 微码指令宽度（sizeof(asc ccu_instr)） */
    HCOMM_CCU_RESOURCE_XN_PER_SIZE = 8,  /**< 单个 XN 资源占用的空间步长（地址换算用） */
    HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT = 8, /**< ascCustom 槽位总数（4-7 预留） */
};

/**
 * @brief CCU 版本常量。
 * @note ABI 内透传的设备级 CCU 版本。
 */
enum {
    HCOMM_CCU_VERSION_V1 = 0,
    HCOMM_CCU_VERSION_V2 = 1,
    HCOMM_CCU_VERSION_INVALID = 0xFFFFFFFFU, /**< 无效版本哨兵 */
};

/**
 * @brief ascCustom 槽位常量。
 * @note HcommCcuInstance.ascCustom 数组中回调槽位的固定含义。
 */
enum {
    HCOMM_CCU_ASC_CUSTOM_CHANNEL_ENTITY = 0,     /**< 槽 0：获取 ChannelEntity 接口 */
    HCOMM_CCU_ASC_CUSTOM_ALLOC_INST_SPACE = 1,   /**< 槽 1：按设备和 die 申请 CCU 指令空间 */
    HCOMM_CCU_ASC_CUSTOM_SUBMIT_INSTS = 2,       /**< 槽 2：下发 CCU 指令接口 */
    HCOMM_CCU_ASC_CUSTOM_RELEASE_INST_SPACE = 3, /**< 槽 3：归还 CCU 指令空间 */
};

/**
 * @brief 资源描述符统计口径的类型枚举。
 * @note block 与非 block 在此合并计数；值域为跨 SO ABI，不可改动。
 */
typedef enum tagHcommCcuResDescType {
    CCU_RES_DESC_LOOP = 0,     /**< LoopEngine（loop + block loop 之和） */
    CCU_RES_DESC_MS = 1,       /**< Memory Slice（ms + block ms 之和） */
    CCU_RES_DESC_CKE = 2,      /**< 完成事件/信号量（cke + block cke 之和） */
    CCU_RES_DESC_XN = 3,       /**< 变量（xn + block xn 之和） */
    CCU_RES_DESC_COUNT_XN = 4, /**< 计数变量（预留，当前恒为 0） */
    CCU_RES_DESC_GSA = 5,      /**< 通用存储地址（gsa + block gsa 之和） */
    CCU_RES_DESC_INS = 6,      /**< 指令空间（指令条数） */
    CCU_RES_DESC_MISSION = 7,  /**< mission 数 */
} HcommCcuResDescType;

/**
 * @brief CCU 批量资源类型枚举。
 * @note resTypes 数组按枚举值索引；block 与非 block 资源在 range 视图中分开表示。
 */
typedef enum tagHcommCcuBatchResType {
    HCOMM_CCU_BATCH_RES_LOOP = 0,       /**< LoopEngine 资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_LOOP = 1, /**< block LoopEngine 资源 */
    HCOMM_CCU_BATCH_RES_MS = 2,         /**< Memory Slice 资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_MS = 3,   /**< block Memory Slice 资源 */
    HCOMM_CCU_BATCH_RES_CKE = 4,        /**< 完成事件/信号量资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_CKE = 5,  /**< block 完成事件/信号量资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_XN = 6,   /**< block 变量资源 */
    HCOMM_CCU_BATCH_RES_XN = 7,         /**< 变量资源 */
    HCOMM_CCU_BATCH_RES_GSA = 8,        /**< 通用存储地址资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_GSA = 9,  /**< block 通用存储地址资源 */
    HCOMM_CCU_BATCH_RES_MISSION = 10,   /**< mission 资源 */
} HcommCcuBatchResType;

struct HcommCcuInstance;

#ifndef CHANNEL_HANDLE_DEFINED
#define CHANNEL_HANDLE_DEFINED
/**
 * @brief 通道句柄类型。
 * @note 不透明唯一标识，跨 SO 以整型值传递。
 */
typedef uint64_t ChannelHandle;
#endif

/**
 * @brief 资源描述符句柄类型。
 * @note 不对用户暴露 POD，由 asc-comm 侧 handle table 解析。
 */
typedef uint64_t HcommCcuResDescHandle;

/**
 * @brief hcomm 与 asc-comm 间传递的 CCU Instance 完整句柄。
 * @note header 用于校验版本、魔数和结构大小；ccuInsKey 由 CcuInstanceMgr 分配，用于跨 SO 查找；
 *       ccuInsPtr 为 hcomm 持有的 CCU Instance 借用指针，随实例生命周期有效。
 * @note 本类型仅在 hcomm 与 asc-comm 契约面及 hcomm 内部使用；对外 API 的不透明实例句柄仍为
 *       CcuInsHandle(uint64_t token)。
 */
typedef struct {
    CommAbiHeader header;                     // ABI 头：version/magic/size 校验.
    uint64_t ccuInsKey;                       // 不透明实例键（0 为无效句柄哨兵）
    const struct HcommCcuInstance* ccuInsPtr; // hcomm 持有的 CCU Instance 借用指针
} HcommCcuInstanceHandle;

/**
 * @brief 填充 CCU Instance 完整句柄的 ABI 头。
 *
 * 统一填写 version、magicWord、size 与 reserved，供 hcomm 与 asc-comm 间
 * 传递 HcommCcuInstanceHandle 时校验 POD 契约。
 *
 * @param handle 输出：待填充的完整句柄。
 *
 * @return 成功返回 void。
 * @note 所有产出完整句柄的路径必须调用本函数，禁止手写 ABI 头。
 */
static inline void HcommCcuFillInsHandleHeader(HcommCcuInstanceHandle* handle)
{
    handle->header.version = HCOMM_CCU_INSTANCE_HANDLE_ABI_VERSION;
    handle->header.magicWord = HCOMM_CCU_INSTANCE_HANDLE_MAGIC_WORD;
    handle->header.size = sizeof(HcommCcuInstanceHandle);
    handle->header.reserved = 0;
}

/**
 * @brief CCU Channel 单次查询快照实体（16 槽 + 计数 + 事件掩码 + 远端 buffer 数组）。
 *
 * localVarIds/remoteVarIds 为本/远端 XN 槽，localEventIds/remoteEventIds 为本/远端 CKE 槽，
 * 实际有效数量由 localVarNum/remoteVarNum/localEventNum/remoteEventNum 给出（<= 容量）。
 * localEventMasks/remoteEventMasks 为预留槽位：mask 是 kernel 构建期的按次参数，
 * 非 channel 属性，当前固定填 0。
 * rmtCcuResBuffer 为对端 CCU 资源空间（RegedBufferEntity 口径，取代旧散字段）。
 * remoteBuffers 为 hcomm channel 内部远端 buffer 列表的借用指针，channel 销毁后失效；
 * 当前数据面未消费（翻译器仍按单 CCU 资源空间假设），为形状兼容预留。
 * 调用方提供存储并填好 ABI 头，hcomm 持锁一次填满快照，之后数据面只读该实体。
 */
typedef struct {
    CommAbiHeader header; // ABI 头：version/magic/size 校验

    int32_t deviceLogicId; // 所在设备逻辑 id
    uint32_t ccuVersion;   // CCU 版本（V1/V2）
    uint32_t dieId;        // die id
    uint32_t channelId;    // channel id（DFX 记录用）

    RegedBufferEntity rmtCcuResBuffer; // 对端 CCU 资源空间（基址/大小/token 折叠为 buffer 实体）

    uint32_t localVarNum;                   // 本端 XN 有效数量（<= HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY）
    uint32_t remoteVarNum;                  // 远端 XN 有效数量
    uint32_t localEventNum;                 // 本端 CKE 有效数量
    uint32_t remoteEventNum;                // 远端 CKE 有效数量
    uint32_t remoteBufferNum;               // 远端 buffer 数量（remoteBuffers 数组长度）
    const RegedBufferEntity* remoteBuffers; // 远端 buffer 数组借用指针（hcomm 持有，当前数据面不消费）

    uint32_t localVarIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];      // 本端 XN id 槽
    uint32_t remoteVarIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];     // 远端 XN id 槽
    uint32_t localEventIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];    // 本端 CKE id 槽
    uint32_t remoteEventIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];   // 远端 CKE id 槽
    uint32_t localEventMasks[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];  // 本端 CKE 掩码槽（预留，填 0）
    uint32_t remoteEventMasks[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY]; // 远端 CKE 掩码槽（预留，填 0）

} HcommCcuChannelEntity;

/**
 * @brief CCU 资源描述符 POD。
 * @note 按资源类型统计申请数量；创建时设置 dieId，查询后填入数量。
 */
typedef struct {
    CommAbiHeader header; // ABI 头：version/magic/size 校验
    uint32_t dieId;       // 资源归属的 die id（创建描述符时设置）
    uint32_t resLength;   // resNum 数组的有效长度（<= HCOMM_CCU_RES_DESC_TYPE_COUNT）
    uint32_t resNum[HCOMM_CCU_RES_DESC_TYPE_COUNT]; // 按资源类型索引的数量
} HcommCcuResDesc;

/**
 * @brief 一段连续 CCU 资源。
 * @note 起始 id + 数量；所属资源类型与 die 由外层 HcommCcuResRanges / HcommCcuDieRes 表达。
 */
typedef struct {
    uint32_t startId;  // 连续资源的起始 ID
    uint32_t count;    // 从 startId 开始的资源数量，必须大于 0
    uint64_t reserved; // 预留字段，当前必须为 0
} HcommCcuResRange;

/**
 * @brief 一类资源的全部 range 段。
 * @note 按资源类型枚举值挂在 HcommCcuDieRes.resTypes[type] 上；resRanges 指向 hcomm CcuInstance
 *       持有的只读数组，asc-comm 在 register_start 期间深拷贝。
 */
typedef struct {
    uint32_t resourceType;             // 资源类别（HcommCcuBatchResType）
    const HcommCcuResRange* resRanges; // 该类型的 range 数组（只读借用）
    uint32_t resRangeNum;              // range 数组元素数量
    uint64_t reserved;                 // 预留字段，当前必须为 0
} HcommCcuResRanges;

/**
 * @brief 单个 die 的资源元数据。
 * @note 承载回路 channel 与 CCU 资源空间 buffer；ccuResBuffer 按
 *       {REGED_BUFFER_RMA, addr=XN 基址, ProtectionInfo.ub{tokenId, tokenValue}} 装配。
 * @note loopChannelId 为该 die 唯一的环回 channel，硬件上 die 内与 die 间访问共用同一条。
 */
typedef struct {
    CommAbiHeader header;           // ABI 头：version/magic/size 校验
    uint32_t enabled;               // 该 die 是否启用（0/1）
    uint32_t missionKey;            // mission 密钥
    uint32_t loopChannelId;         // 环回 channel id（每 die 一条，die 内/间访问共用）
    RegedBufferEntity ccuResBuffer; // CCU 资源空间（XN 基址 + token）映射后的 buffer 信息
    uint32_t reserved;              // 预留字段，当前必须为 0
} HcommCcuDieMetadata;

/**
 * @brief 单个 die 的全部资源视图。
 * @note 按资源类型索引 range 段数组，并携带 die 元数据指针。
 */
typedef struct {
    uint32_t dieId;      // 该资源视图所属 die
    uint32_t resTypeNum; // 已使用的资源类型数（<= HCOMM_CCU_RES_TYPE_COUNT）
    HcommCcuResRanges resTypes[HCOMM_CCU_RES_TYPE_COUNT]; // 按资源类型枚举值索引
    const HcommCcuDieMetadata* dieMetadata;               // hcomm 持有的 die 元数据（只读借用）
    uint64_t reserved[16];                                // 预留字段，当前必须清零
} HcommCcuDieRes;

/**
 * @brief 查询 CCU Channel 实体的回调函数原型。
 *
 * @param channel 输入：通道句柄。
 * @param channelEntityPtr 输出：实体借用地址。
 *
 * @return 成功返回 0，其他失败。
 */
typedef int32_t (*HcommCcuGetChannelEntityFn)(ChannelHandle channel, uint64_t* channelEntityPtr);

/**
 * @brief 按 die 申请 CCU 指令空间的回调函数原型。
 *
 * @param deviceLogicId 输入：设备逻辑 id。
 * @param dieId 输入：die id。
 * @param instNum 输入：申请的指令条数。
 * @param startInstId 输出：申请到的起始指令 id。
 *
 * @return 成功返回 0，其他失败。
 */
typedef int32_t (*HcommCcuAllocInstSpaceFn)(
    int32_t deviceLogicId, uint32_t dieId, uint32_t instNum, uint32_t* startInstId);

/**
 * @brief 下发 CCU 指令的回调函数原型。
 *
 * @param deviceLogicId 输入：设备逻辑 id。
 * @param dieId 输入：die id。
 * @param startInstructionId 输入：起始指令 id。
 * @param data 输入：主机侧指令数据地址。
 * @param instNum 输入：下发的指令条数。
 *
 * @return 成功返回 0，其他失败。
 */
typedef int32_t (*HcommCcuSubmitInstsFn)(
    int32_t deviceLogicId, uint32_t dieId, uint32_t startInstructionId, const void* data, uint32_t instNum);

/**
 * @brief 归还 CCU 指令空间的回调函数原型。
 *
 * @param deviceLogicId 输入：设备逻辑 id。
 * @param dieId 输入：die id。
 * @param startInstId 输入：起始指令 id。
 * @param instNum 输入：归还的指令条数。
 *
 * @return 成功返回 0，其他失败。
 */
typedef int32_t (*HcommCcuReleaseInstSpaceFn)(
    int32_t deviceLogicId, uint32_t dieId, uint32_t startInstId, uint32_t instNum);

/**
 * @brief CCU Instance 资源快照 POD。
 * @note 资源按 die 与类型组织；指令空间不进入 range 视图，由 ascCustom 按需申请和释放。
 * @note 设备号由调用线程获取，die 启用状态由 dieMetadata.enabled 推导，句柄代际由 ccuInsKey 承担。
 */
typedef struct HcommCcuInstance {
    CommAbiHeader header; // ABI 头：version/magic/size 校验
    uint32_t ccuDieNum;   // POD 中 die 槽位数（<= HCOMM_CCU_INSTANCE_DIE_CAPACITY）
    uint32_t ccuVersion;  // 当前设备的 CCU 版本（V1/V2）
    HcommCcuDieRes ccuDieReses[HCOMM_CCU_INSTANCE_DIE_CAPACITY]; // 按 die 组织的资源视图（预留 8 槽）
    uint64_t ascCustom[HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT];         // ascCustom 回调槽位数组
} HcommCcuInstance;

#ifdef __cplusplus
} // extern "C"

#endif // __cplusplus

#endif // HCOMM_CCU_RESOURCE_H
