/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You must not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_profiling.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

#include "base/dlog_pub.h"

// CCU数据面profiling依赖CANN pkg_inc下的profapi头文件; 无CANN头的环境自动降级为不支持profiling
#if defined(__has_include)
#if __has_include(<profiling/aprof_pub.h>) && __has_include(<runtime/rt_external_base.h>)
#include <profiling/aprof_pub.h>
#include <runtime/rt_external_base.h>
#define ASCCOMM_CCU_HAS_PROFAPI 1
#endif
#endif

#ifdef ASCCOMM_CCU_HAS_PROFAPI

namespace asc::ccu_profiling {
namespace {

constexpr int32_t CCU_LOG_MODULE_ID = 5;

#define ASCCOMM_CCU_LOG_ERROR(...)                      \
    do {                                                \
        if (DlogRecord != nullptr) {                    \
            dlog_error(CCU_LOG_MODULE_ID, __VA_ARGS__); \
        }                                               \
    } while (0)

// ---------------------------------------------------------------------------
// CCU 数据面 Profiling (仅依赖 libprofapi.so, 不依赖 hcomm 的 DFX 模块)
// ---------------------------------------------------------------------------

// Profapi 上报结构定义, 与 hcomm 侧 ProfilingHandler 保持一致
constexpr uint32_t MSPROF_REPORT_CCU_TASK_INFO = 14U;
constexpr uint32_t MSPROF_REPORT_CCU_WAIT_SIGNAL_INFO = 15U;
constexpr uint32_t MSPROF_REPORT_CCU_GROUP_INFO = 16U;
// hccl层(5500)hccl_info明细类型, 对齐hcomm ProfTaskType::TASK_HCCL_INFO
constexpr uint32_t MSPROF_REPORT_HCCL_TASK_INFO_TYPE = 0U; // "hccl_info"
constexpr uint16_t CCU_MAX_CHANNEL_NUM = 16U;
constexpr uint8_t INVALID_TYPE_VALUE = 0xFF; // reduceOpType、inputDataType、outputDataType非法值

struct MsprofCcuTaskInfo {
    uint8_t version;
    uint8_t workFlowMode;
    uint64_t itemId;    // CCU任务名 hash id
    uint64_t groupName; // 通信域 hash id
    uint32_t rankId;
    uint32_t ranksize; // CCU任务设计的Chip数目

    uint16_t streamId;
    uint32_t taskId;
    uint8_t dieId;     // CCU任务执行的DieId
    uint8_t missionId; // CCU任务执行的MissionId
    uint16_t instrId;
};

struct MsprofCcuGroupInfo {
    uint8_t version;
    uint64_t itemId;    // CCU任务名 hash id
    uint64_t groupName; // 通信域 hash id
    uint32_t rankId;
    uint32_t ranksize; // CCU任务设计的Chip数目
    uint8_t workFlowMode;

    uint16_t streamId;
    uint32_t taskId;
    uint8_t dieId; // CCU任务执行的DieId
    uint16_t instrId;
    uint8_t missionId; // CCU任务执行的MissionId

    uint8_t reduceOpType;   // 与HcclReduceOp类型保持一致
    uint8_t inputDataType;  // 与HcclDataType类型保持一致
    uint8_t outputDataType; // 与HcclDataType类型保持一致
    uint64_t dataSize;      // 输入数据大小

    uint16_t channelId[CCU_MAX_CHANNEL_NUM];    // LoopGroup所包含的搬运指令使用的ChannelId
    uint32_t remoteRankId[CCU_MAX_CHANNEL_NUM]; // LoopGroup所包含的搬运指令的对端
};

struct MsprofCcuWaitSignalInfo {
    uint8_t version;
    uint64_t itemId;    // CCU任务名 hash id
    uint64_t groupName; // 通信域 hash id
    uint32_t rankId;
    uint32_t ranksize; // CCU任务设计的Chip数目
    uint8_t workFlowMode;

    uint16_t streamId;
    uint32_t taskId;
    uint8_t dieId; // CCU任务执行的DieId
    uint16_t instrId;
    uint8_t missionId; // CCU任务执行的MissionId

    uint32_t ckeId;
    uint32_t mask;
    uint16_t channelId[CCU_MAX_CHANNEL_NUM];    // LoopGroup所包含的搬运指令使用的ChannelId
    uint32_t remoteRankId[CCU_MAX_CHANNEL_NUM]; // LoopGroup所包含的搬运指令的对端
};

// hccl层(5500)hccl_info明细payload, 与hcomm MsprofHcclInfo逐字一致(orion_adapter_rts.h);
// 无来源字段的默认值与hcomm构造函数一致(0xFFFFFFFF/0), 不足部分在ReportCcuHcclInfo中补齐
struct MsprofHcclInfo {
    uint64_t itemId;
    uint64_t cclTag;
    uint64_t groupName;
    uint32_t localRank;
    uint32_t remoteRank;
    uint32_t rankSize;
    uint32_t workFlowMode;
    uint32_t planeID;
    uint32_t ctxId;
    uint64_t notifyID;
    uint32_t stage;
    uint32_t role; // role {0: dst, 1:src}
    double durationEstimated;
    uint64_t srcAddr;
    uint64_t dstAddr;
    uint64_t dataSize;      // bytes
    uint32_t opType;        // {0: sum, 1: mul, 2: max, 3: min}
    uint32_t dataType;      // data type {0: INT8, 1: INT16, 2: INT32, 3: FP16, 4:FP32, 5:INT64, 6:UINT64}
    uint32_t linkType;      // link type {0: 'OnChip', 1: 'HCCS', 2: 'PCIe', 3: 'RoCE'}
    uint32_t transportType; // transport type {0: SDMA, 1: RDMA, 2:LOCAL}
    uint32_t rdmaType;      // RDMA type {0: RDMASendNotify, 1:RDMASendPayload}
    uint32_t reserve2;
};

static_assert(sizeof(MsprofCcuTaskInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(MsprofCcuGroupInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(MsprofCcuWaitSignalInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(MsprofHcclInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);

constexpr uint32_t INVALID_UINT = 0xFFFFFFFFU;
constexpr uint64_t DFX_INVALID_U64 = 0xFFFFFFFFFFFFFFFFULL;
constexpr uint32_t INVALID_VALUE_RANKID = 0xFFFFFFFFU;
constexpr uint32_t INVALID_VALUE_RANKSIZE = 0xFFFFFFFFU;
constexpr uint16_t INVALID_VALUE_CHANNELID = 0xFFFFU;
constexpr uint8_t HCCL_WORKFLOW_MODE_OP_BASE = 1;
constexpr uint32_t MSPROF_MODULE_ID_HCCL = 3U;
constexpr uint32_t ASCCOMM_CCU_PROFAPI_UNINITIALIZED = 0U;
constexpr uint32_t ASCCOMM_CCU_PROFAPI_INITIALIZED = 1U;
constexpr uint32_t ASCCOMM_CCU_PROFAPI_UNAVAILABLE = 2U;
constexpr uint8_t CCU_PROF_INVALID_MISSION_ID = INVALID_TYPE_VALUE; // 数据面无来源, 用非法值
constexpr uint16_t CCU_PROF_INVALID_INSTR_ID = INVALID_VALUE_CHANNELID; // 数据面无来源, 用非法值

// asccomm与hcomm的MsprofAdditionalInfo来自同一个libprofapi.so, 布局由
// MSPROF_ADDTIONAL_INFO_DATA_LENGTH保证; 只要msprof版本一致二者必然一致
struct AsccommCcuProfApi {
    int32_t (*msprofRegisterCallback)(uint32_t moduleId, ProfCommandHandle handle) = nullptr;
    int32_t (*msprofRegTypeInfo)(uint16_t level, uint32_t typeId, const char* typeName) = nullptr;
    int32_t (*msprofReportAdditionalInfo)(uint32_t nonPersistantFlag, const VOID_PTR data, uint32_t length) = nullptr;
    int32_t (*msprofReportApi)(uint32_t nonPersistantFlag, const MsprofApi* api) = nullptr;
    int32_t (*msprofReportCompactInfo)(uint32_t nonPersistantFlag, const MsprofCompactInfo* info, uint32_t length)
        = nullptr;
    uint64_t (*msprofStr2Id)(const char* hashInfo, size_t length) = nullptr;
    uint64_t (*msprofSysCycleTime)(void) = nullptr;
};

AsccommCcuProfApi g_asccommCcuProfApi;

// runtime侧函数(libruntime.so): 查询当前线程最近一次提交task的taskId/streamId,
// 与hcomm hrtGetTaskIdAndStreamID等价(rtGetTaskIdAndStreamID的薄封装)
struct AsccommCcuRuntimeApi {
    int32_t (*rtGetTaskIdAndStreamID)(uint32_t* taskId, uint32_t* streamId) = nullptr;
};

AsccommCcuRuntimeApi g_asccommCcuRuntimeApi;

bool LoadCcuProfApi()
{
    void* handle = dlopen("libprofapi.so", RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        return false;
    }
    AsccommCcuProfApi api;
    api.msprofRegisterCallback = reinterpret_cast<decltype(api.msprofRegisterCallback)>(
        dlsym(handle, "MsprofRegisterCallback"));
    api.msprofRegTypeInfo
        = reinterpret_cast<decltype(api.msprofRegTypeInfo)>(dlsym(handle, "MsprofRegTypeInfo"));
    api.msprofReportAdditionalInfo = reinterpret_cast<decltype(api.msprofReportAdditionalInfo)>(
        dlsym(handle, "MsprofReportAdditionalInfo"));
    api.msprofReportApi = reinterpret_cast<decltype(api.msprofReportApi)>(dlsym(handle, "MsprofReportApi"));
    api.msprofReportCompactInfo = reinterpret_cast<decltype(api.msprofReportCompactInfo)>(
        dlsym(handle, "MsprofReportCompactInfo"));
    api.msprofStr2Id = reinterpret_cast<decltype(api.msprofStr2Id)>(dlsym(handle, "MsprofStr2Id"));
    api.msprofSysCycleTime
        = reinterpret_cast<decltype(api.msprofSysCycleTime)>(dlsym(handle, "MsprofSysCycleTime"));
    if (api.msprofRegisterCallback == nullptr || api.msprofRegTypeInfo == nullptr
        || api.msprofReportAdditionalInfo == nullptr || api.msprofReportApi == nullptr
        || api.msprofReportCompactInfo == nullptr || api.msprofStr2Id == nullptr
        || api.msprofSysCycleTime == nullptr) {
        return false;
    }
    g_asccommCcuProfApi = api;
    return true;
}

// 加载libruntime.so中的task/stream id查询接口; 失败仅影响上报, 不影响launch
bool LoadCcuRuntimeApi()
{
    // 优先复用进程内已加载的libruntime, 未加载时dlopen补载
    void* handle = dlopen("libruntime.so", RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) {
        handle = dlopen("libruntime.so", RTLD_NOW | RTLD_GLOBAL);
    }
    if (handle == nullptr) {
        return false;
    }
    AsccommCcuRuntimeApi runtimeApi;
    runtimeApi.rtGetTaskIdAndStreamID = reinterpret_cast<decltype(runtimeApi.rtGetTaskIdAndStreamID)>(
        dlsym(handle, "rtGetTaskIdAndStreamID"));
    if (runtimeApi.rtGetTaskIdAndStreamID == nullptr) {
        return false;
    }
    g_asccommCcuRuntimeApi = runtimeApi;
    return true;
}

} // namespace

// CCU数据面profiling控制块: 独立于hcomm的DFX模块, 只维护数据面所需的L0/L1状态
struct AsccommCcuProfiling {
    std::atomic<uint32_t> profApiState{ASCCOMM_CCU_PROFAPI_UNINITIALIZED};
    std::atomic<bool> enableHcclL0{false};
    std::atomic<bool> enableHcclL1{false};
    std::atomic<bool> ccuInfoTypesRegistered{false};
    std::mutex ccuMutex; // 保护类型注册与flush的互斥

    static AsccommCcuProfiling& GetInstance()
    {
        static AsccommCcuProfiling instance;
        return instance;
    }

    int32_t CommandHandle(uint32_t rtType, void* data, uint32_t len);
    void StartCcuSubscribe(uint64_t profSwitch);
    void StopCcuSubscribe();

private:
    AsccommCcuProfiling() = default;
    ~AsccommCcuProfiling() = default;
    AsccommCcuProfiling(const AsccommCcuProfiling&) = delete;
    AsccommCcuProfiling& operator=(const AsccommCcuProfiling&) = delete;
};

namespace {

// 初始化profapi并注册profiling开关callback; 只允许成功一次, 失败后降级为不可用
bool InitCcuProfilingOnce()
{
    auto& prof = AsccommCcuProfiling::GetInstance();
    uint32_t expected = ASCCOMM_CCU_PROFAPI_UNINITIALIZED;
    if (!prof.profApiState.compare_exchange_strong(expected, ASCCOMM_CCU_PROFAPI_INITIALIZED)) {
        return prof.profApiState.load() == ASCCOMM_CCU_PROFAPI_INITIALIZED;
    }
    if (!LoadCcuProfApi()) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] load libprofapi.so failed, CCU data-plane profiling disabled.");
        prof.profApiState.store(ASCCOMM_CCU_PROFAPI_UNAVAILABLE);
        return false;
    }
    ProfCommandHandle callback = [](uint32_t rtType, void* data, uint32_t len) -> int32_t {
        return AsccommCcuProfiling::GetInstance().CommandHandle(rtType, data, len);
    };
    const int32_t ret = g_asccommCcuProfApi.msprofRegisterCallback(MSPROF_MODULE_ID_HCCL, callback);
    if (ret != 0) {
        ASCCOMM_CCU_LOG_ERROR(
            "[CcuProfiling] MsprofRegisterCallback failed, ret[%d], CCU data-plane profiling disabled.", ret);
        prof.profApiState.store(ASCCOMM_CCU_PROFAPI_UNAVAILABLE);
        return false;
    }
    ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] profapi callback registered, CCU data-plane profiling ready.");
    return true;
}

} // namespace

// 处理profapi下发的profiling开关命令, 与hcomm ProfilingHandler::CommandHandle语义一致
int32_t AsccommCcuProfiling::CommandHandle(uint32_t rtType, void* data, uint32_t len)
{
    (void)len;
    if (data == nullptr || rtType != RT_PROF_CTRL_SWITCH) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] invalid command, rtType[%u].", rtType);
        return 1; // HCCL_E_PARA
    }
    const rtProfCommandHandle_t* profConfigParam = static_cast<const rtProfCommandHandle_t*>(data);
    const uint32_t type = profConfigParam->type;
    switch (type) {
        case PROF_COMMANDHANDLE_TYPE_START:
            StartCcuSubscribe(profConfigParam->profSwitch);
            break;
        case PROF_COMMANDHANDLE_TYPE_STOP:
            StopCcuSubscribe();
            break;
        default:
            break;
    }
    return 0; // HCCL_SUCCESS
}

// L1打开时注册14/15/16类型, 注册成功后置位ccuInfoTypesRegistered
void AsccommCcuProfiling::StartCcuSubscribe(uint64_t profSwitch)
{
    if ((profSwitch & PROF_TASK_TIME_L1_MASK) == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(ccuMutex);
    enableHcclL0.store(true);
    enableHcclL1.store(true);
    if (ccuInfoTypesRegistered.load()) {
        return;
    }
    const std::pair<uint32_t, const char*> ccuInfoTypes[] = {
        {MSPROF_REPORT_CCU_TASK_INFO, "ccu_task_info"},
        {MSPROF_REPORT_CCU_WAIT_SIGNAL_INFO, "ccu_wait_signal_info"},
        {MSPROF_REPORT_CCU_GROUP_INFO, "ccu_group_info"},
        // hccl层(5500)hccl_info明细类型, 对齐hcomm CallProfRegTaskTypeApi(仅注册hccl_info,
        // 直驱路径无DPU任务, 不注册dpu_hccl_info)
        {MSPROF_REPORT_HCCL_TASK_INFO_TYPE, "hccl_info"},
    };
    for (const auto& it : ccuInfoTypes) {
        const int32_t ret = g_asccommCcuProfApi.msprofRegTypeInfo(
            static_cast<uint16_t>(MSPROF_REPORT_HCCL_NODE_LEVEL), it.first, it.second);
        if (ret != 0) {
            ASCCOMM_CCU_LOG_ERROR(
                "[CcuProfiling] MsprofRegTypeInfo failed, type[%u], ret[%d].", it.first, ret);
            return; // 下次Start重试
        }
    }
    ccuInfoTypesRegistered.store(true);
}

void AsccommCcuProfiling::StopCcuSubscribe()
{
    enableHcclL0.store(false);
    enableHcclL1.store(false);
    ccuInfoTypesRegistered.store(false);
}

// --------------------------------------------------------------------------
// 数据面上报: launch后组装MsprofAdditionalInfo并发送
// --------------------------------------------------------------------------

// 是否具备上报条件: profapi可用且L1开启且14/15/16已注册
bool IsCcuProfReportReady()
{
    return AsccommCcuProfiling::GetInstance().profApiState.load() == ASCCOMM_CCU_PROFAPI_INITIALIZED
           && AsccommCcuProfiling::GetInstance().enableHcclL1.load()
           && AsccommCcuProfiling::GetInstance().ccuInfoTypesRegistered.load();
}

// 组装并上报一条type 14 ccu_task_info; 数据面无来源的字段按非法值填充
// (协议约定: 非法值由解析端按unknown呈现, 不伪造合法值)
uint64_t GetItemId(const ccu_prof_report_context& ctx)
{
    if (ctx.kernel_name != nullptr) {
        return g_asccommCcuProfApi.msprofStr2Id(ctx.kernel_name, std::strlen(ctx.kernel_name));
    }

    char funcTag[sizeof(ctx.kernel_func) * 2 + 1] = {0};
    (void)snprintf(funcTag, sizeof(funcTag), "%p", ctx.kernel_func);
    return g_asccommCcuProfApi.msprofStr2Id(funcTag, static_cast<size_t>(std::strlen(funcTag)));
}

uint64_t GetItemId(const profiling_detail& detail)
{
    const size_t nameLength = strnlen(detail.name, profiling_detail::max_name_length);
    if (nameLength == 0) {
        return DFX_INVALID_U64;
    }
    return g_asccommCcuProfApi.msprofStr2Id(detail.name, nameLength);
}

bool report_ccu_task_info(const ccu_prof_report_context& ctx, const profiling_detail& detail, uint32_t stream_id,
    uint32_t task_id, uint32_t thread_id)
{
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_CCU_TASK_INFO;
    reporterData.threadId = thread_id;
    reporterData.dataLen = sizeof(MsprofCcuTaskInfo);
    reporterData.timeStamp = ctx.end_time;

    auto* ccuTaskInfo = reinterpret_cast<MsprofCcuTaskInfo*>(reporterData.data);
    ccuTaskInfo->version = 0;
    ccuTaskInfo->workFlowMode = HCCL_WORKFLOW_MODE_OP_BASE;
    ccuTaskInfo->itemId = GetItemId(detail);
    // 数据面无通信域/rank上下文, 按协议填非法值, 不得伪造
    ccuTaskInfo->groupName = DFX_INVALID_U64;
    ccuTaskInfo->rankId = INVALID_VALUE_RANKID;
    ccuTaskInfo->ranksize = INVALID_VALUE_RANKSIZE;
    ccuTaskInfo->streamId = static_cast<uint16_t>(stream_id);
    ccuTaskInfo->taskId = task_id;
    ccuTaskInfo->dieId = detail.die_id;
    ccuTaskInfo->missionId = detail.mission_id;
    ccuTaskInfo->instrId = detail.instr_id;

    const int32_t ret = g_asccommCcuProfApi.msprofReportAdditionalInfo(1, &reporterData, sizeof(MsprofAdditionalInfo));
    if (ret != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportAdditionalInfo failed, ret[%d].", ret);
        return false;
    }
    return true;
}

// 组装并上报Node粒度的API耗时条(type=MSPROF_REPORT_NODE_LAUNCH_TYPE)与节点基本信息
// (type=MSPROF_REPORT_NODE_BASIC_INFO_TYPE), payload组装逐字对齐hcomm
// DfxProfilingHandler::ReportNodeApi / ReportNodeBasicInfo; 直驱路径的"API"即
// asccomm_ccu_host_kernel_launch本身, beginTime/endTime为launch前后采样值;
// 二者统一挂L1门控(与type 14同条件), 任何失败只记日志, 不影响launch结果
bool report_ccu_node_api_and_basic_info(const ccu_prof_report_context& ctx, uint32_t thread_id)
{
    const uint64_t itemId = GetItemId(ctx);

    // 1. Node API耗时条: msprof时间线上呈现为一次host API调用的起止区间
    MsprofApi apiData{};
    apiData.level = static_cast<uint16_t>(MSPROF_REPORT_NODE_LEVEL);
    apiData.type = MSPROF_REPORT_NODE_LAUNCH_TYPE;
    apiData.threadId = thread_id;
    apiData.beginTime = ctx.begin_time;
    apiData.endTime = ctx.end_time;
    apiData.itemId = itemId;
    const int32_t apiRet = g_asccommCcuProfApi.msprofReportApi(1, &apiData);
    if (apiRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportApi(NodeApi) failed, ret[%d].", apiRet);
    }

    // 2. 节点基本信息: 为时间线上的节点提供opName/opType/taskType标识
    MsprofCompactInfo compactData{};
    compactData.level = static_cast<uint16_t>(MSPROF_REPORT_NODE_LEVEL);
    compactData.type = MSPROF_REPORT_NODE_BASIC_INFO_TYPE;
    compactData.threadId = thread_id;
    compactData.dataLen = sizeof(MsprofNodeBasicInfo);
    compactData.timeStamp = ctx.end_time;
    compactData.data.nodeBasicInfo.opName = itemId;
    compactData.data.nodeBasicInfo.taskType = static_cast<uint32_t>(MSPROF_GE_TASK_TYPE_HCCL);
    compactData.data.nodeBasicInfo.opType = itemId;
    compactData.data.nodeBasicInfo.blockDim = 0;
    compactData.data.nodeBasicInfo.opFlag = 0;
    const int32_t compactRet
        = g_asccommCcuProfApi.msprofReportCompactInfo(1, &compactData, sizeof(MsprofCompactInfo));
    if (compactRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportCompactInfo(NodeBasicInfo) failed, ret[%d].", compactRet);
    }
    return apiRet == 0 && compactRet == 0;
}

// 公共payload组装辅助: 数据面无来源的字段按非法值填充
// (与hcomm CcuProfilingInfo初始化约定一致: channelId=0xFFFF/remoteRankId=0xFFFFFFFF,
//  其余无来源标量填INVALID/0xFF, 解析端按unknown呈现, 不伪造合法值)
// type 15 ccu_wait_signal_info: WaitCKE粒度, 字段来自数据面真实明细。
bool report_ccu_wait_signal_info(const ccu_prof_report_context& ctx, const profiling_detail& detail,
    uint32_t stream_id, uint32_t task_id, uint32_t thread_id)
{
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_CCU_WAIT_SIGNAL_INFO;
    reporterData.threadId = thread_id;
    reporterData.dataLen = sizeof(MsprofCcuWaitSignalInfo);
    reporterData.timeStamp = ctx.end_time;

    auto* waitSignalInfo = reinterpret_cast<MsprofCcuWaitSignalInfo*>(reporterData.data);
    waitSignalInfo->version = 0;
    waitSignalInfo->itemId = GetItemId(detail);
    waitSignalInfo->groupName = DFX_INVALID_U64; // 无通信域上下文 → 非法值
    waitSignalInfo->rankId = INVALID_VALUE_RANKID;
    waitSignalInfo->ranksize = INVALID_VALUE_RANKSIZE;
    waitSignalInfo->workFlowMode = HCCL_WORKFLOW_MODE_OP_BASE;
    waitSignalInfo->streamId = static_cast<uint16_t>(stream_id);
    waitSignalInfo->taskId = task_id;
    waitSignalInfo->dieId = detail.die_id;
    waitSignalInfo->instrId = detail.instr_id;
    waitSignalInfo->missionId = detail.mission_id;
    waitSignalInfo->ckeId = detail.cke_id;
    waitSignalInfo->mask = detail.mask;
    (void)std::memcpy(waitSignalInfo->channelId, detail.channel_id, sizeof(waitSignalInfo->channelId));
    (void)std::memcpy(
        waitSignalInfo->remoteRankId, detail.remote_rank_id, sizeof(waitSignalInfo->remoteRankId));

    const int32_t ret = g_asccommCcuProfApi.msprofReportAdditionalInfo(1, &reporterData, sizeof(MsprofAdditionalInfo));
    if (ret != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportAdditionalInfo(WaitSignal) failed, ret[%d].", ret);
        return false;
    }
    return true;
}

// type 16 ccu_group_info: LoopGroup粒度. 直驱路径无reduceOp/dataType/dataSize/channel等
// 来源, 按要求以默认空值(非法值)填充上报
bool report_ccu_group_info(const ccu_prof_report_context& ctx, const profiling_detail& detail, uint32_t stream_id,
    uint32_t task_id, uint32_t thread_id)
{
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_CCU_GROUP_INFO;
    reporterData.threadId = thread_id;
    reporterData.dataLen = sizeof(MsprofCcuGroupInfo);
    reporterData.timeStamp = ctx.end_time;

    auto* ccuGroupInfo = reinterpret_cast<MsprofCcuGroupInfo*>(reporterData.data);
    ccuGroupInfo->version = 0;
    ccuGroupInfo->itemId = GetItemId(detail);
    ccuGroupInfo->groupName = DFX_INVALID_U64; // 无通信域上下文 → 非法值
    ccuGroupInfo->rankId = INVALID_VALUE_RANKID;
    ccuGroupInfo->ranksize = INVALID_VALUE_RANKSIZE;
    ccuGroupInfo->workFlowMode = HCCL_WORKFLOW_MODE_OP_BASE;
    ccuGroupInfo->streamId = static_cast<uint16_t>(stream_id);
    ccuGroupInfo->taskId = task_id;
    ccuGroupInfo->dieId = detail.die_id;
    ccuGroupInfo->instrId = detail.instr_id;
    ccuGroupInfo->missionId = detail.mission_id;
    ccuGroupInfo->reduceOpType = detail.reduce_op_type;
    ccuGroupInfo->inputDataType = detail.input_data_type;
    ccuGroupInfo->outputDataType = detail.output_data_type;
    ccuGroupInfo->dataSize = detail.data_size;
    (void)std::memcpy(ccuGroupInfo->channelId, detail.channel_id, sizeof(ccuGroupInfo->channelId));
    (void)std::memcpy(ccuGroupInfo->remoteRankId, detail.remote_rank_id, sizeof(ccuGroupInfo->remoteRankId));

    const int32_t ret = g_asccommCcuProfApi.msprofReportAdditionalInfo(1, &reporterData, sizeof(MsprofAdditionalInfo));
    if (ret != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportAdditionalInfo(GroupInfo) failed, ret[%d].", ret);
        return false;
    }
    return true;
}

// hccl层(5500)通信任务画像: TaskApi耗时条 + hccl_info明细, 对齐hcomm ReportHcclTaskApi /
// FillTaskAdditionInfo; itemId用hcomm同款任务类型名"Ccu"(方案A: 与hcomm标准路径同桶,
// msprof通信视图中两路径CCU耗时聚合可比); type固定MASTER(直驱单stream无主从概念);
// MsprofHcclInfo的业务字段(cclTag/rank/addr/dataSize等)在直驱路径无来源, 按hcomm构造
// 函数默认值(0xFFFFFFFF/0)填充; 任何失败只记日志, 不影响launch结果
bool report_ccu_hccl_task_api_and_info(const ccu_prof_report_context& ctx, uint32_t thread_id)
{
    constexpr char CCU_TASK_OP_NAME[] = "Ccu"; // hcomm PROF_TASK_OP_NAME_V2中TASK_CCU的桶名
    const size_t opNameLen = std::strlen(CCU_TASK_OP_NAME);
    const uint64_t itemId = g_asccommCcuProfApi.msprofStr2Id(CCU_TASK_OP_NAME, opNameLen);

    // 1. TaskApi耗时条: msprof通信视图中以"Ccu"类目呈现本次通信任务耗时
    MsprofApi apiData{};
    apiData.level = static_cast<uint16_t>(MSPROF_REPORT_HCCL_NODE_LEVEL);
    apiData.type = MSPROF_REPORT_HCCL_MASTER_TYPE;
    apiData.threadId = thread_id;
    apiData.beginTime = ctx.begin_time;
    apiData.endTime = ctx.end_time;
    apiData.itemId = itemId;
    const int32_t apiRet = g_asccommCcuProfApi.msprofReportApi(1, &apiData);
    if (apiRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportApi(HcclTaskApi) failed, ret[%d].", apiRet);
    }

    // 2. hccl_info明细: 通信明细骨架(业务字段无来源按默认空值填充)
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_HCCL_TASK_INFO_TYPE;
    reporterData.threadId = thread_id;
    reporterData.dataLen = sizeof(MsprofHcclInfo);
    reporterData.timeStamp = ctx.end_time;

    auto* hcclInfo = reinterpret_cast<MsprofHcclInfo*>(reporterData.data);
    hcclInfo->itemId = itemId;
    hcclInfo->cclTag = DFX_INVALID_U64;        // 无通信算子上下文 → 非法值
    hcclInfo->groupName = DFX_INVALID_U64;     // 无通信域上下文 → 非法值
    hcclInfo->localRank = INVALID_VALUE_RANKID;
    hcclInfo->remoteRank = INVALID_VALUE_RANKID;
    hcclInfo->rankSize = INVALID_VALUE_RANKSIZE;
    hcclInfo->workFlowMode = HCCL_WORKFLOW_MODE_OP_BASE;
    hcclInfo->planeID = 0;                     // 无来源 → 空值
    hcclInfo->ctxId = 0;                       // 无来源 → 空值
    hcclInfo->notifyID = DFX_INVALID_U64;      // 无来源 → 非法值
    hcclInfo->stage = 0;                       // 无来源 → 空值
    hcclInfo->role = 0xFFFFFFFF;               // hcomm构造函数默认值
    hcclInfo->durationEstimated = 0.0;         // 无来源 → 空值
    hcclInfo->srcAddr = 0xFFFFFFFF;            // hcomm构造函数默认值
    hcclInfo->dstAddr = 0xFFFFFFFF;            // hcomm构造函数默认值
    hcclInfo->dataSize = 0;                    // hcomm构造函数默认值
    hcclInfo->opType = 0xFFFFFFFF;             // hcomm构造函数默认值
    hcclInfo->dataType = 0xFFFFFFFF;           // hcomm构造函数默认值
    hcclInfo->linkType = 0xFFFFFFFF;           // hcomm构造函数默认值
    hcclInfo->transportType = 0xFFFFFFFF;      // hcomm构造函数默认值
    hcclInfo->rdmaType = 0xFFFFFFFF;           // hcomm构造函数默认值
    hcclInfo->reserve2 = 0;

    const int32_t infoRet
        = g_asccommCcuProfApi.msprofReportAdditionalInfo(1, &reporterData, sizeof(MsprofAdditionalInfo));
    if (infoRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportAdditionalInfo(HcclInfo) failed, ret[%d].", infoRet);
    }
    return apiRet == 0 && infoRet == 0;
}

// launch后上报入口: type 14/15/16按数据面明细逐条分发, 对齐HCOMM ReportCcuInfo。
void report_ccu_data_plane_after_launch(
    const ccu_prof_report_context& ctx, const std::vector<profiling_detail>& details)
{
    if (!IsCcuProfReportReady()) {
        return;
    }
    // 1. Node粒度API耗时条(launch的host侧耗时)与节点基本信息, 对齐hcomm ReportNodeApi/
    //    ReportNodeBasicInfo; 二者不依赖taskId/streamId, 放在runtime查询前发送
    const uint32_t threadId = static_cast<uint32_t>(syscall(SYS_gettid));
    (void)report_ccu_node_api_and_basic_info(ctx, threadId);
    // 2. hccl层(5500)通信任务画像(TaskApi耗时条+hccl_info明细), 不依赖taskId/streamId,
    //    紧随Node粒度之后发送
    (void)report_ccu_hccl_task_api_and_info(ctx, threadId);
    // 3. type 14 ccu_task_info(依赖runtime查询刚提交task的taskId/streamId)
    if (g_asccommCcuRuntimeApi.rtGetTaskIdAndStreamID == nullptr && !LoadCcuRuntimeApi()) {
        return;
    }
    uint32_t taskId = INVALID_UINT;
    uint32_t streamId = INVALID_UINT;
    if (g_asccommCcuRuntimeApi.rtGetTaskIdAndStreamID(&taskId, &streamId) != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] rtGetTaskIdAndStreamID failed, skip data-plane report.");
        return;
    }
    for (const auto& detail : details) {
        switch (detail.type) {
            case detail_type::task:
                (void)report_ccu_task_info(ctx, detail, streamId, taskId, threadId);
                break;
            case detail_type::wait_signal:
                (void)report_ccu_wait_signal_info(ctx, detail, streamId, taskId, threadId);
                break;
            case detail_type::group:
                (void)report_ccu_group_info(ctx, detail, streamId, taskId, threadId);
                break;
            default:
                ASCCOMM_CCU_LOG_ERROR(
                    "[CcuProfiling] unsupported detail type[%u].", static_cast<uint32_t>(detail.type));
                break;
        }
    }
}

bool init_ccu_profiling_once()
{
    return InitCcuProfilingOnce();
}

bool is_ccu_prof_report_ready()
{
    return IsCcuProfReportReady();
}

uint64_t msprof_sys_cycle_time()
{
    return g_asccommCcuProfApi.msprofSysCycleTime();
}

} // namespace asc::ccu_profiling

#else

namespace asc::ccu_profiling {

bool init_ccu_profiling_once()
{
    return false;
}

bool is_ccu_prof_report_ready()
{
    return false;
}

uint64_t msprof_sys_cycle_time()
{
    return 0;
}

bool report_ccu_node_api_and_basic_info(const ccu_prof_report_context&, uint32_t)
{
    return false;
}

bool report_ccu_hccl_task_api_and_info(const ccu_prof_report_context&, uint32_t)
{
    return false;
}

bool report_ccu_task_info(const ccu_prof_report_context&, const profiling_detail&, uint32_t, uint32_t, uint32_t)
{
    return false;
}

bool report_ccu_wait_signal_info(
    const ccu_prof_report_context&, const profiling_detail&, uint32_t, uint32_t, uint32_t)
{
    return false;
}

bool report_ccu_group_info(const ccu_prof_report_context&, const profiling_detail&, uint32_t, uint32_t, uint32_t)
{
    return false;
}

void report_ccu_data_plane_after_launch(const ccu_prof_report_context&, const std::vector<profiling_detail>&)
{
}

} // namespace asc::ccu_profiling

#endif
