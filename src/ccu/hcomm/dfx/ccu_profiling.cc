/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/dfx/ccu_profiling.h"

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#include <string>

#include "base/dlog_pub.h"

#include <cxxabi.h>

#include <profiling/aprof_pub.h>
#include <runtime/rt_external_base.h>

namespace asc::ccu_profiling {
namespace {

constexpr int32_t CCU_LOG_MODULE_ID = 5;

#define ASCCOMM_CCU_LOG_ERROR(...)                      \
    do {                                                \
        if (DlogRecord != nullptr) {                    \
            dlog_error(CCU_LOG_MODULE_ID, __VA_ARGS__); \
        }                                               \
    } while (0)

#define ASCCOMM_CCU_LOG_INFO(...)                                  \
    do {                                                           \
        if (DlogRecord != nullptr) {                               \
            DlogRecord(CCU_LOG_MODULE_ID, DLOG_INFO, __VA_ARGS__); \
        }                                                          \
    } while (0)

// ---------------------------------------------------------------------------
// CCU 数据面 Profiling
// ---------------------------------------------------------------------------

// Profapi上报结构定义
constexpr uint32_t MSPROF_REPORT_CCU_TASK_INFO = 14U;
constexpr uint32_t MSPROF_REPORT_CCU_WAIT_SIGNAL_INFO = 15U;
constexpr uint32_t MSPROF_REPORT_CCU_GROUP_INFO = 16U;
// hccl层(MSPROF_REPORT_HCCL_NODE_LEVEL)hccl_info明细类型
constexpr uint32_t MSPROF_REPORT_HCCL_TASK_INFO_TYPE = 0U; // "hccl_info"
constexpr uint16_t CCU_MAX_CHANNEL_NUM = 16U;
constexpr uint8_t INVALID_TYPE_VALUE = 0xFF; // reduceOpType、inputDataType、outputDataType非法值

struct msprof_ccu_task_info {
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

struct msprof_ccu_group_info {
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

struct msprof_ccu_wait_signal_info {
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

struct msprof_hccl_info {
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

static_assert(sizeof(msprof_ccu_task_info) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(msprof_ccu_group_info) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(msprof_ccu_wait_signal_info) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(msprof_hccl_info) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);

constexpr uint32_t INVALID_UINT = 0xFFFFFFFFU;                          // 无来源 uint32 字段的非法值
constexpr uint64_t DFX_INVALID_U64 = 0xFFFFFFFFFFFFFFFFULL;             // 无来源 uint64 字段的非法值
constexpr uint32_t INVALID_VALUE_RANKID = 0xFFFFFFFFU;                  // 无 rank 上下文时的非法 rank
constexpr uint32_t INVALID_VALUE_RANKSIZE = 0xFFFFFFFFU;                // 无通信域上下文时的非法 rank 数
constexpr uint16_t INVALID_VALUE_CHANNELID = 0xFFFFU;                   // 无 channel 时的非法 channel id
constexpr uint8_t HCCL_WORKFLOW_MODE_OP_BASE = 1;                       // 单算子直调模式
constexpr uint32_t MSPROF_MODULE_ID_HCCL = 3U;                          // profapi 注册的模块 ID
constexpr uint32_t ASCCOMM_CCU_PROFAPI_UNINITIALIZED = 0U;              // 尚未初始化 profapi
constexpr uint32_t ASCCOMM_CCU_PROFAPI_INITIALIZED = 1U;                // profapi 已初始化
constexpr uint32_t ASCCOMM_CCU_PROFAPI_UNAVAILABLE = 2U;                // profapi 初始化失败，禁用上报
constexpr uint8_t CCU_PROF_INVALID_MISSION_ID = INVALID_TYPE_VALUE;     // 无来源 mission 的非法值
constexpr uint16_t CCU_PROF_INVALID_INSTR_ID = INVALID_VALUE_CHANNELID; // 无来源 instruction 的非法值

struct asccomm_ccu_prof_api {
    int32_t (*msprofRegisterCallback)(uint32_t moduleId, ProfCommandHandle handle) = nullptr;
    int32_t (*msprofRegTypeInfo)(uint16_t level, uint32_t typeId, const char* typeName) = nullptr;
    int32_t (*msprofReportAdditionalInfo)(uint32_t nonPersistantFlag, const VOID_PTR data, uint32_t length) = nullptr;
    int32_t (*msprofReportApi)(uint32_t nonPersistantFlag, const MsprofApi* api) = nullptr;
    int32_t (*msprofReportCompactInfo)(uint32_t nonPersistantFlag, const MsprofCompactInfo* info, uint32_t length) =
        nullptr;
    uint64_t (*msprofStr2Id)(const char* hashInfo, size_t length) = nullptr;
    uint64_t (*msprofSysCycleTime)(void) = nullptr;
};

asccomm_ccu_prof_api g_asccommCcuProfApi;

struct asccomm_ccu_runtime_api {
    int32_t (*rtGetTaskIdAndStreamID)(uint32_t* taskId, uint32_t* streamId) = nullptr;
};

asccomm_ccu_runtime_api g_asccommCcuRuntimeApi;

bool load_ccu_prof_api()
{
    void* handle = dlopen("libprofapi.so", RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        return false;
    }
    asccomm_ccu_prof_api api;
    api.msprofRegisterCallback =
        reinterpret_cast<decltype(api.msprofRegisterCallback)>(dlsym(handle, "MsprofRegisterCallback"));
    api.msprofRegTypeInfo = reinterpret_cast<decltype(api.msprofRegTypeInfo)>(dlsym(handle, "MsprofRegTypeInfo"));
    api.msprofReportAdditionalInfo =
        reinterpret_cast<decltype(api.msprofReportAdditionalInfo)>(dlsym(handle, "MsprofReportAdditionalInfo"));
    api.msprofReportApi = reinterpret_cast<decltype(api.msprofReportApi)>(dlsym(handle, "MsprofReportApi"));
    api.msprofReportCompactInfo =
        reinterpret_cast<decltype(api.msprofReportCompactInfo)>(dlsym(handle, "MsprofReportCompactInfo"));
    api.msprofStr2Id = reinterpret_cast<decltype(api.msprofStr2Id)>(dlsym(handle, "MsprofStr2Id"));
    api.msprofSysCycleTime = reinterpret_cast<decltype(api.msprofSysCycleTime)>(dlsym(handle, "MsprofSysCycleTime"));
    if (api.msprofRegisterCallback == nullptr || api.msprofRegTypeInfo == nullptr ||
        api.msprofReportAdditionalInfo == nullptr || api.msprofReportApi == nullptr ||
        api.msprofReportCompactInfo == nullptr || api.msprofStr2Id == nullptr || api.msprofSysCycleTime == nullptr) {
        return false;
    }
    g_asccommCcuProfApi = api;
    return true;
}

// 加载libruntime.so中的task/stream id查询接口; 失败仅影响上报, 不影响launch
bool load_ccu_runtime_api()
{
    // 优先复用进程内已加载的libruntime, 未加载时dlopen补载
    void* handle = dlopen("libruntime.so", RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) {
        handle = dlopen("libruntime.so", RTLD_NOW | RTLD_GLOBAL);
    }
    if (handle == nullptr) {
        return false;
    }
    asccomm_ccu_runtime_api runtimeApi;
    runtimeApi.rtGetTaskIdAndStreamID =
        reinterpret_cast<decltype(runtimeApi.rtGetTaskIdAndStreamID)>(dlsym(handle, "rtGetTaskIdAndStreamID"));
    if (runtimeApi.rtGetTaskIdAndStreamID == nullptr) {
        return false;
    }
    g_asccommCcuRuntimeApi = runtimeApi;
    return true;
}

} // namespace

// CCU profiling 控制块，维护 profapi 状态和 L1 开关。
struct asccomm_ccu_profiling {
    std::atomic<uint32_t> profApiState{ASCCOMM_CCU_PROFAPI_UNINITIALIZED};
    std::atomic<bool> enableHcclL1{false};
    std::atomic<bool> ccuInfoTypesRegistered{false};
    std::mutex ccuMutex; // 保护类型注册状态的互斥

    static asccomm_ccu_profiling& GetInstance()
    {
        static asccomm_ccu_profiling instance;
        return instance;
    }

    int32_t CommandHandle(uint32_t rtType, void* data, uint32_t len);
    void StartCcuSubscribe(uint64_t profSwitch);
    void StopCcuSubscribe();

private:
    asccomm_ccu_profiling() = default;
    ~asccomm_ccu_profiling() = default;
    asccomm_ccu_profiling(const asccomm_ccu_profiling&) = delete;
    asccomm_ccu_profiling& operator=(const asccomm_ccu_profiling&) = delete;
};

// 初始化profapi并注册profiling开关callback; 只允许成功一次, 失败后降级为不可用
bool init_ccu_profiling_once()
{
    auto& prof = asccomm_ccu_profiling::GetInstance();
    uint32_t expected = ASCCOMM_CCU_PROFAPI_UNINITIALIZED;
    if (!prof.profApiState.compare_exchange_strong(expected, ASCCOMM_CCU_PROFAPI_INITIALIZED)) {
        return prof.profApiState.load() == ASCCOMM_CCU_PROFAPI_INITIALIZED;
    }
    if (!load_ccu_prof_api()) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] load libprofapi.so failed, CCU data-plane profiling disabled.");
        prof.profApiState.store(ASCCOMM_CCU_PROFAPI_UNAVAILABLE);
        return false;
    }
    ProfCommandHandle callback = [](uint32_t rtType, void* data, uint32_t len) -> int32_t {
        return asccomm_ccu_profiling::GetInstance().CommandHandle(rtType, data, len);
    };
    const int32_t ret = g_asccommCcuProfApi.msprofRegisterCallback(MSPROF_MODULE_ID_HCCL, callback);
    if (ret != 0) {
        ASCCOMM_CCU_LOG_ERROR(
            "[CcuProfiling] MsprofRegisterCallback failed, ret[%d], CCU data-plane profiling disabled.", ret);
        prof.profApiState.store(ASCCOMM_CCU_PROFAPI_UNAVAILABLE);
        return false;
    }
    ASCCOMM_CCU_LOG_INFO("[CcuProfiling] profapi callback registered, CCU data-plane profiling ready.");
    return true;
}

// 处理 profapi 下发的 profiling 开关命令。
int32_t asccomm_ccu_profiling::CommandHandle(uint32_t rtType, void* data, uint32_t len)
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
void asccomm_ccu_profiling::StartCcuSubscribe(uint64_t profSwitch)
{
    if ((profSwitch & PROF_TASK_TIME_L1_MASK) == 0) {
        return;
    }

    bool typesRegistered = false;
    {
        std::lock_guard<std::mutex> lock(ccuMutex);
        enableHcclL1.store(true);
        typesRegistered = ccuInfoTypesRegistered.load();
        if (!typesRegistered) {
            const std::pair<uint32_t, const char*> ccuInfoTypes[] = {
                {MSPROF_REPORT_CCU_TASK_INFO, "ccu_task_info"},
                {MSPROF_REPORT_CCU_WAIT_SIGNAL_INFO, "ccu_wait_signal_info"},
                {MSPROF_REPORT_CCU_GROUP_INFO, "ccu_group_info"},
                // 注册 hccl_info 类型；直驱路径无 DPU 任务，不注册 dpu_hccl_info。
                {MSPROF_REPORT_HCCL_TASK_INFO_TYPE, "hccl_info"},
            };
            for (const auto& it : ccuInfoTypes) {
                const int32_t ret = g_asccommCcuProfApi.msprofRegTypeInfo(
                    static_cast<uint16_t>(MSPROF_REPORT_HCCL_NODE_LEVEL), it.first, it.second);
                if (ret != 0) {
                    ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofRegTypeInfo failed, type[%u], ret[%d].", it.first, ret);
                    return; // 下次Start重试
                }
            }
            ccuInfoTypesRegistered.store(true);
            typesRegistered = true;
        }
    }
}

void asccomm_ccu_profiling::StopCcuSubscribe()
{
    enableHcclL1.store(false);
    ccuInfoTypesRegistered.store(false);
    std::lock_guard<std::mutex> lock(ccuMutex);
}

// --------------------------------------------------------------------------
// 数据面上报: launch后组装MsprofAdditionalInfo并发送
// --------------------------------------------------------------------------

// 是否具备上报条件: profapi可用且L1开启且14/15/16已注册
bool is_ccu_prof_report_ready()
{
    auto& profiling = asccomm_ccu_profiling::GetInstance();
    const bool profApiReady = profiling.profApiState.load() == ASCCOMM_CCU_PROFAPI_INITIALIZED;
    const bool l1Enabled = profiling.enableHcclL1.load();
    const bool typesRegistered = profiling.ccuInfoTypesRegistered.load();
    const bool reportReady = profApiReady && l1Enabled && typesRegistered;
    return reportReady;
}

// Demangle 编译器符号名，去掉函数参数和 namespace 前缀
void get_display_kernel_name(
    const char* kernel_name, char (&display_name)[ccu_prof_report_context::max_kernel_name_length])
{
    if (kernel_name == nullptr || kernel_name[0] == '\0' || kernel_name[0] != '_') {
        (void)snprintf(
            display_name, ccu_prof_report_context::max_kernel_name_length, "%s",
            kernel_name == nullptr ? "" : kernel_name);
        return;
    }

    int status = 0;
    char* demangled_name = abi::__cxa_demangle(kernel_name, nullptr, nullptr, &status);
    if (status != 0 || demangled_name == nullptr) {
        (void)snprintf(display_name, ccu_prof_report_context::max_kernel_name_length, "%s", kernel_name);
        return;
    }

    std::string name(demangled_name);
    free(demangled_name);

    const size_t parameter_pos = name.find('(');
    if (parameter_pos != std::string::npos) {
        name = name.substr(0, parameter_pos);
    }

    const size_t function_pos = name.rfind("::");
    if (function_pos != std::string::npos) {
        name = name.substr(function_pos + 2);
    }

    (void)snprintf(display_name, ccu_prof_report_context::max_kernel_name_length, "%s", name.c_str());
}

// 将 kernel_name 转为 itemId；kernel_name 为空时使用函数地址字符串生成 itemId。
uint64_t get_item_id(const ccu_prof_report_context& ctx)
{
    char display_name[ccu_prof_report_context::max_kernel_name_length] = {};
    const size_t nameLength = strnlen(ctx.kernel_name, ccu_prof_report_context::max_kernel_name_length);
    if (nameLength != 0) {
        get_display_kernel_name(ctx.kernel_name, display_name);
    } else {
        (void)snprintf(display_name, sizeof(display_name), "%p", ctx.kernel_func);
    }

    return g_asccommCcuProfApi.msprofStr2Id(display_name, static_cast<size_t>(std::strlen(display_name)));
}

uint64_t get_item_id(const profiling_detail& detail)
{
    const size_t nameLength = strnlen(detail.name, profiling_detail::max_name_length);
    if (nameLength == 0) {
        return DFX_INVALID_U64;
    }
    char display_name[profiling_detail::max_name_length] = {};
    get_display_kernel_name(detail.name, display_name);
    return g_asccommCcuProfApi.msprofStr2Id(display_name, std::strlen(display_name));
}

// 上报 type 14 ccu_task_info
bool report_ccu_task_info(
    const ccu_prof_report_context& ctx, const profiling_detail& detail, uint32_t stream_id, uint32_t task_id,
    uint32_t thread_id)
{
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_CCU_TASK_INFO;
    reporterData.threadId = thread_id;
    reporterData.dataLen = sizeof(msprof_ccu_task_info);
    reporterData.timeStamp = ctx.end_time;

    auto* ccuTaskInfo = reinterpret_cast<msprof_ccu_task_info*>(reporterData.data);
    ccuTaskInfo->version = 0;
    ccuTaskInfo->workFlowMode = HCCL_WORKFLOW_MODE_OP_BASE;
    ccuTaskInfo->itemId = get_item_id(detail);
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

// 上报 node-level 数据：launch API 耗时条，节点基础信息。
bool report_ccu_node_api_and_basic_info(const ccu_prof_report_context& ctx, uint32_t thread_id)
{
    const uint64_t itemId = get_item_id(ctx);

    // 1. launch API耗时条，上报开始时间、结束时间和节点名。
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

    // 2. 节点基础信息，上报 opName/opType/taskType/blockDim/opFlag。
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
    const int32_t compactRet = g_asccommCcuProfApi.msprofReportCompactInfo(1, &compactData, sizeof(MsprofCompactInfo));
    if (compactRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportCompactInfo(NodeBasicInfo) failed, ret[%d].", compactRet);
    }
    return apiRet == 0 && compactRet == 0;
}

// 上报一条 type 15 ccu_wait_signal_info，
bool report_ccu_wait_signal_info(
    const ccu_prof_report_context& ctx, const profiling_detail& detail, uint32_t stream_id, uint32_t task_id,
    uint32_t thread_id)
{
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_CCU_WAIT_SIGNAL_INFO;
    reporterData.threadId = thread_id;
    reporterData.dataLen = sizeof(msprof_ccu_wait_signal_info);
    reporterData.timeStamp = ctx.end_time;

    auto* waitSignalInfo = reinterpret_cast<msprof_ccu_wait_signal_info*>(reporterData.data);
    waitSignalInfo->version = 0;
    waitSignalInfo->itemId = get_item_id(detail);
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
    (void)std::memcpy(waitSignalInfo->remoteRankId, detail.remote_rank_id, sizeof(waitSignalInfo->remoteRankId));

    const int32_t ret = g_asccommCcuProfApi.msprofReportAdditionalInfo(1, &reporterData, sizeof(MsprofAdditionalInfo));
    if (ret != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportAdditionalInfo(WaitSignal) failed, ret[%d].", ret);
        return false;
    }
    return true;
}

// 上报一条 type 16 ccu_group_info
bool report_ccu_group_info(
    const ccu_prof_report_context& ctx, const profiling_detail& detail, uint32_t stream_id, uint32_t task_id,
    uint32_t thread_id)
{
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_CCU_GROUP_INFO;
    reporterData.threadId = thread_id;
    reporterData.dataLen = sizeof(msprof_ccu_group_info);
    reporterData.timeStamp = ctx.end_time;

    auto* ccuGroupInfo = reinterpret_cast<msprof_ccu_group_info*>(reporterData.data);
    ccuGroupInfo->version = 0;
    ccuGroupInfo->itemId = get_item_id(detail);
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

// 上报 hccl-level 数据：任务 API 耗时条，hccl_info 明细。
bool report_ccu_hccl_task_api_and_info(const ccu_prof_report_context& ctx, uint32_t thread_id)
{
    constexpr char CCU_TASK_OP_NAME[] = "ccu";
    const size_t opNameLen = std::strlen(CCU_TASK_OP_NAME);
    const uint64_t itemId = g_asccommCcuProfApi.msprofStr2Id(CCU_TASK_OP_NAME, opNameLen);

    // 1. 任务API耗时条，上报开始时间、结束时间和任务名 "Ccu"。
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

    // 2. hccl_info 明细，上报 itemId
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_HCCL_TASK_INFO_TYPE;
    reporterData.threadId = thread_id;
    reporterData.dataLen = sizeof(msprof_hccl_info);
    reporterData.timeStamp = ctx.end_time;

    auto* hcclInfo = reinterpret_cast<msprof_hccl_info*>(reporterData.data);
    hcclInfo->itemId = itemId;
    hcclInfo->cclTag = DFX_INVALID_U64;    // 无通信算子上下文 → 非法值
    hcclInfo->groupName = DFX_INVALID_U64; // 无通信域上下文 → 非法值
    hcclInfo->localRank = INVALID_VALUE_RANKID;
    hcclInfo->remoteRank = INVALID_VALUE_RANKID;
    hcclInfo->rankSize = INVALID_VALUE_RANKSIZE;
    hcclInfo->workFlowMode = HCCL_WORKFLOW_MODE_OP_BASE;
    hcclInfo->planeID = 0;                // 无来源 → 空值
    hcclInfo->ctxId = 0;                  // 无来源 → 空值
    hcclInfo->notifyID = DFX_INVALID_U64; // 无来源 → 非法值
    hcclInfo->stage = 0;                  // 无来源 → 空值
    hcclInfo->role = 0xFFFFFFFF;          // 无来源，填非法值
    hcclInfo->durationEstimated = 0.0;    // 无来源 → 空值
    hcclInfo->srcAddr = 0xFFFFFFFF;       // 无来源，填非法值
    hcclInfo->dstAddr = 0xFFFFFFFF;       // 无来源，填非法值
    hcclInfo->dataSize = 0;               // 无来源，填空值
    hcclInfo->opType = 0xFFFFFFFF;        // 无来源，填非法值
    hcclInfo->dataType = 0xFFFFFFFF;      // 无来源，填非法值
    hcclInfo->linkType = 0xFFFFFFFF;      // 无来源，填非法值
    hcclInfo->transportType = 0xFFFFFFFF; // 无来源，填非法值
    hcclInfo->rdmaType = 0xFFFFFFFF;      // 无来源，填非法值
    hcclInfo->reserve2 = 0;

    const int32_t infoRet =
        g_asccommCcuProfApi.msprofReportAdditionalInfo(1, &reporterData, sizeof(MsprofAdditionalInfo));
    if (infoRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportAdditionalInfo(HcclInfo) failed, ret[%d].", infoRet);
    }
    return apiRet == 0 && infoRet == 0;
}

// launch 后上报入口：先上报 Node 和 hccl 层数据，再按明细类型上报 type 14/15/16。
void report_ccu_data_plane_after_launch(
    const ccu_prof_report_context& ctx, const std::vector<profiling_detail>& details)
{
    if (!is_ccu_prof_report_ready()) {
        return;
    }
    // 1. 上报 launch API耗时条和节点基础信息；二者不依赖 taskId/streamId。
    const uint32_t threadId = static_cast<uint32_t>(syscall(SYS_gettid));
    (void)report_ccu_node_api_and_basic_info(ctx, threadId);
    // 2. 上报任务API耗时条和hccl_info；二者不依赖 taskId/streamId。
    (void)report_ccu_hccl_task_api_and_info(ctx, threadId);
    if (!ctx.task_info_valid) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] task info is invalid, skip data-plane report.");
        return;
    }
    const uint32_t taskId = ctx.task_id;
    const uint32_t streamId = ctx.stream_id;
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

bool query_ccu_task_id_and_stream_id(uint32_t& task_id, uint32_t& stream_id)
{
    if (g_asccommCcuRuntimeApi.rtGetTaskIdAndStreamID == nullptr && !load_ccu_runtime_api()) {
        return false;
    }
    task_id = INVALID_UINT;
    stream_id = INVALID_UINT;
    if (g_asccommCcuRuntimeApi.rtGetTaskIdAndStreamID(&task_id, &stream_id) != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] rtGetTaskIdAndStreamID failed.");
        return false;
    }
    return true;
}

uint64_t msprof_sys_cycle_time() { return g_asccommCcuProfApi.msprofSysCycleTime(); }

} // namespace asc::ccu_profiling
