/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/ccu_host_launch.h"

#include "base/dlog_pub.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <sys/syscall.h>
#include <unordered_map>
#include <unistd.h>

#include "xxhash_impl.inc"

#if defined(__has_include)
#if __has_include("aprof_pub.h") && __has_include("rt_external_base.h")
#include "aprof_pub.h"
#include "rt_external_base.h"
#define ASCCOMM_CCU_HAS_PROFAPI 1
#endif
#endif

#ifndef HCOMM_WEAK_SYMBOL
#define HCOMM_WEAK_SYMBOL __attribute__((weak))
#endif

#ifndef ASCCOMM_CCU_HOST_LAUNCH_HAS_HCOMM_TYPES
typedef uint64_t CcuKernelHandle;
typedef void* CcuKernelArg;
typedef uint64_t ThreadHandle;
typedef int32_t CommEngine;
typedef int32_t HcommResult;
constexpr CommEngine COMM_ENGINE_CCU = static_cast<CommEngine>(0);
#endif

extern "C" {
extern CcuResult HcommCcuKernelRegisterStart(CcuInsHandle insHandle) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuKernelRegister(
    CcuInsHandle insHandle, uint32_t dieId, const char* kernelFuncName, const void* kernelFunc, const void** kernelArgs,
    uint32_t argNum, CcuKernelHandle* kernelHandle) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuKernelRegisterEnd(CcuInsHandle insHandle) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuGetTaskArgsNum(CcuKernelHandle kernelHandle, uint32_t* taskArgsNum) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuKernelLaunch(
    ThreadHandle threadHandle, CcuKernelHandle kernelHandle, const void* taskArgs, uint32_t argNum) HCOMM_WEAK_SYMBOL;
extern HcommResult HcommThreadAllocWithStream(
    CommEngine engine, aclrtStream stream, uint32_t notifyNum, ThreadHandle* thread);
}

namespace {
constexpr uint64_t CCU_DIE0_MASK = 0x01U;
constexpr uint64_t CCU_DIE1_MASK = 0x02U;
constexpr uint32_t CCU_DIE0_ID = 0U;
constexpr uint32_t CCU_DIE1_ID = 1U;
constexpr uint32_t CCU_SUPPORTED_NUM_BLOCKS = 1U;
constexpr uint32_t CCU_SQE_ARGS_LEN = 13U;
constexpr int32_t CCU_LOG_MODULE_ID = 5;

#define ASCCOMM_CCU_LOG_ERROR(...)                      \
    do {                                                \
        if (DlogRecord != nullptr) {                    \
            dlog_error(CCU_LOG_MODULE_ID, __VA_ARGS__); \
        }                                               \
    } while (0)

bool IsCcuKernelLaunchApiAvailable()
{
    auto registerStart = HcommCcuKernelRegisterStart;
    auto registerKernel = HcommCcuKernelRegister;
    auto registerEnd = HcommCcuKernelRegisterEnd;
    auto getTaskArgsNum = HcommCcuGetTaskArgsNum;
    auto kernelLaunch = HcommCcuKernelLaunch;
    return registerStart != nullptr && registerKernel != nullptr && registerEnd != nullptr &&
           getTaskArgsNum != nullptr && kernelLaunch != nullptr;
}

bool IsSupportedSingleDieMask(uint64_t phyDieMask)
{
    return phyDieMask == CCU_DIE0_MASK || phyDieMask == CCU_DIE1_MASK;
}

uint32_t GetDieIdByMask(uint64_t phyDieMask) { return phyDieMask == CCU_DIE1_MASK ? CCU_DIE1_ID : CCU_DIE0_ID; }

CcuResult ValidateLaunchCfg(const asccomm_launch_kernel_cfg* cfg)
{
    if (cfg->ccu_schd.num_blocks != CCU_SUPPORTED_NUM_BLOCKS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] unsupported num_blocks[%u], expected[%u].", __func__, cfg->ccu_schd.num_blocks,
            CCU_SUPPORTED_NUM_BLOCKS);
        return CCU_E_PARA;
    }
    if (!IsSupportedSingleDieMask(cfg->ccu_schd.phy_die_mask)) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] unsupported phy_die_mask[0x%llx], expected[0x1 or 0x2].", __func__,
            static_cast<unsigned long long>(cfg->ccu_schd.phy_die_mask));
        return CCU_E_PARA;
    }
    return CCU_SUCCESS;
}

using VoidPackedCcuKernel = void (*)(void*);

struct VoidKernelRegisterCtx {
    const void* kernelFunc;
    void* packedArgs;
};

CcuResult VoidKernelTrampoline(CcuKernelArg arg);

CcuResult RegisterCcuKernel(
    const void* kernelFunc, const char* kernelName, const asccomm_launch_kernel_cfg* cfg, void* args,
    CcuKernelHandle& kernelHandle)
{
    CcuResult ret = HcommCcuKernelRegisterStart(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    const uint32_t dieId = GetDieIdByMask(cfg->ccu_schd.phy_die_mask);
    VoidKernelRegisterCtx registerCtx{
        kernelFunc,
        args,
    };

    const void* kernelArgs[] = {&registerCtx};

    // kernelFunc do not support func that return void , only support return CcuResult
    // kernelName非空时透传给注册侧(profiling名称等), 为空时注册侧使用默认名称
    ret = HcommCcuKernelRegister(
        cfg->ccu_ins, dieId, kernelName, reinterpret_cast<const void*>(VoidKernelTrampoline), kernelArgs, 1,
        &kernelHandle);
    if (ret != CCU_SUCCESS) {
        (void)HcommCcuKernelRegisterEnd(cfg->ccu_ins);
        return ret;
    }

    return HcommCcuKernelRegisterEnd(cfg->ccu_ins);
}

class KernelHandleCache {
public:
    CcuResult Match(
        const void* kernelFunc, const char* kernelName, const asccomm_launch_kernel_cfg* cfg, void* args,
        CcuKernelHandle& kernelHandle, uint32_t& taskArgsNum)
    {
        const uint64_t cacheTag = cfg->ccu_schd.binary_cache_tag;
        if (cacheTag == 0U) {
            return RegisterAndGetTaskArgsNum(kernelFunc, kernelName, cfg, args, kernelHandle, taskArgsNum);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = cache_.find(cacheTag);
        if (iter != cache_.end()) {
            kernelHandle = iter->second;
            return HcommCcuGetTaskArgsNum(kernelHandle, &taskArgsNum);
        }

        CcuResult ret = RegisterAndGetTaskArgsNum(kernelFunc, kernelName, cfg, args, kernelHandle, taskArgsNum);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        cache_[cacheTag] = kernelHandle;
        return CCU_SUCCESS;
    }

private:
    CcuResult RegisterAndGetTaskArgsNum(
        const void* kernelFunc, const char* kernelName, const asccomm_launch_kernel_cfg* cfg, void* args,
        CcuKernelHandle& kernelHandle, uint32_t& taskArgsNum)
    {
        CcuResult ret = RegisterCcuKernel(kernelFunc, kernelName, cfg, args, kernelHandle);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        ret = HcommCcuGetTaskArgsNum(kernelHandle, &taskArgsNum);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        return CCU_SUCCESS;
    }

    std::mutex mutex_;
    std::unordered_map<uint64_t, CcuKernelHandle> cache_;
};

KernelHandleCache& GetKernelHandleCache()
{
    static KernelHandleCache cache;
    return cache;
}

CcuResult VoidKernelTrampoline(CcuKernelArg arg)
{
    auto* ctx = static_cast<VoidKernelRegisterCtx*>(arg);
    if (ctx == nullptr || ctx->kernelFunc == nullptr || ctx->packedArgs == nullptr) {
        return CCU_E_PTR;
    }

    auto fn = reinterpret_cast<VoidPackedCcuKernel>(const_cast<void*>(ctx->kernelFunc));
    fn(ctx->packedArgs);
    return CCU_SUCCESS;
}

CcuResult ConvertHcommResult(HcommResult ret)
{
    switch (ret) {
        case 0:
            return CCU_SUCCESS;
        case 1:
            return CCU_E_PARA;
        case 2:
            return CCU_E_PTR;
        case 3:
            return CCU_E_INTERNAL;
        case 4:
            return CCU_E_INTERNAL;
        case 5:
            return CCU_E_NOT_SUPPORT;
        case 6:
            return CCU_E_NOT_FOUND;
        case 7:
            return CCU_E_UNAVAIL;
        default:
            return CCU_E_RUNTIME;
    }
}

class ThreadHandleCache {
public:
    CcuResult GetOrCreate(aclrtStream stream, ThreadHandle& thread)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = cache_.find(stream);
        if (iter != cache_.end()) {
            thread = iter->second;
            return CCU_SUCCESS;
        }

        ThreadHandle newThread = 0;
        const HcommResult allocRet = HcommThreadAllocWithStream(COMM_ENGINE_CCU, stream, 0, &newThread);
        if (allocRet != 0) {
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] HcommThreadAllocWithStream failed, ret[%d], stream[%p].", __func__, allocRet, stream);
            return ConvertHcommResult(allocRet);
        }
        if (newThread == 0) {
            ASCCOMM_CCU_LOG_ERROR("[%s] HcommThreadAllocWithStream returned an empty handle.", __func__);
            return CCU_E_INTERNAL;
        }

        cache_.emplace(stream, newThread);
        thread = newThread;
        return CCU_SUCCESS;
    }

private:
    std::mutex mutex_;
    std::unordered_map<aclrtStream, ThreadHandle> cache_;
};

ThreadHandleCache& GetThreadHandleCache()
{
    static ThreadHandleCache cache;
    return cache;
}

#ifdef ASCCOMM_CCU_HAS_PROFAPI

// ---------------------------------------------------------------------------
// CCU 数据面 Profiling (仅依赖 libprofapi.so, 不依赖 hcomm 的 DFX 模块)
// ---------------------------------------------------------------------------

namespace {

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
constexpr uint8_t CCU_PROF_INVALID_MISSION_ID = INVALID_TYPE_VALUE;     // 数据面无来源, 用非法值
constexpr uint16_t CCU_PROF_INVALID_INSTR_ID = INVALID_VALUE_CHANNELID; // 数据面无来源, 用非法值

// asccomm与hcomm的MsprofAdditionalInfo来自同一个libprofapi.so, 布局由
// MSPROF_ADDTIONAL_INFO_DATA_LENGTH保证; 只要msprof版本一致二者必然一致
struct AsccommCcuProfApi {
    int32_t (*msprofRegisterCallback)(uint32_t moduleId, ProfCommandHandle handle) = nullptr;
    int32_t (*msprofRegTypeInfo)(uint16_t level, uint32_t typeId, const char* typeName) = nullptr;
    int32_t (*msprofReportAdditionalInfo)(uint32_t nonPersistantFlag, const VOID_PTR data, uint32_t length) = nullptr;
    int32_t (*msprofReportApi)(uint32_t nonPersistantFlag, const MsprofApi* api) = nullptr;
    int32_t (*msprofReportCompactInfo)(uint32_t nonPersistantFlag, const MsprofCompactInfo* info, uint32_t length) =
        nullptr;
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
    runtimeApi.rtGetTaskIdAndStreamID =
        reinterpret_cast<decltype(runtimeApi.rtGetTaskIdAndStreamID)>(dlsym(handle, "rtGetTaskIdAndStreamID"));
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
            ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofRegTypeInfo failed, type[%u], ret[%d].", it.first, ret);
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

namespace {

// 数据面上报所需的launch上下文(LaunchCcuKernelWithStream在launch前后采集)
struct CcuProfReportContext {
    const void* kernelFunc = nullptr; // kernel函数标识(无名字时的itemId回退来源)
    const char* kernelName = nullptr; // kernel名字符串(编译器经launch入参传入, 可为空)
    uint32_t dieId = 0;
    uint64_t beginTime = 0;
    uint64_t endTime = 0;
};

// 是否具备上报条件: profapi可用且L1开启且14/15/16已注册
bool IsCcuProfReportReady()
{
    return AsccommCcuProfiling::GetInstance().profApiState.load() == ASCCOMM_CCU_PROFAPI_INITIALIZED &&
           AsccommCcuProfiling::GetInstance().enableHcclL1.load() &&
           AsccommCcuProfiling::GetInstance().ccuInfoTypesRegistered.load();
}

// 组装并上报一条type 14 ccu_task_info; 数据面无来源的字段按非法值填充
// (协议约定: 非法值由解析端按unknown呈现, 不伪造合法值)
bool ReportCcuTaskInfo(const CcuProfReportContext& ctx, uint32_t streamId, uint32_t taskId, uint32_t threadId)
{
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_CCU_TASK_INFO;
    reporterData.threadId = threadId;
    reporterData.dataLen = sizeof(MsprofCcuTaskInfo);
    reporterData.timeStamp = ctx.endTime;

    auto* ccuTaskInfo = reinterpret_cast<MsprofCcuTaskInfo*>(reporterData.data);
    ccuTaskInfo->version = 0;
    ccuTaskInfo->workFlowMode = HCCL_WORKFLOW_MODE_OP_BASE;
    // itemId优先用kernel名字符串: msprof按Str2Id的字符串表呈现语义名, 且与hcomm标准路径
    // 同名kernel的itemId一致; kernelName为空时回退到函数地址hash(旧调用方/异常路径)
    uint64_t itemId = 0;
    if (ctx.kernelName != nullptr) {
        itemId = g_asccommCcuProfApi.msprofStr2Id(ctx.kernelName, std::strlen(ctx.kernelName));
    } else {
        char funcTag[sizeof(ctx.kernelFunc) * 2 + 1] = {0};
        (void)snprintf(funcTag, sizeof(funcTag), "%p", ctx.kernelFunc);
        itemId = g_asccommCcuProfApi.msprofStr2Id(funcTag, static_cast<size_t>(std::strlen(funcTag)));
    }
    ccuTaskInfo->itemId = itemId;
    // 数据面无通信域/rank上下文, 按协议填非法值, 不得伪造
    ccuTaskInfo->groupName = DFX_INVALID_U64;
    ccuTaskInfo->rankId = INVALID_VALUE_RANKID;
    ccuTaskInfo->ranksize = INVALID_VALUE_RANKSIZE;
    ccuTaskInfo->streamId = static_cast<uint16_t>(streamId);
    ccuTaskInfo->taskId = taskId;
    ccuTaskInfo->dieId = static_cast<uint8_t>(ctx.dieId);
    // missionId/instrId在asc-comm直驱路径无来源(hcomm内部GetCcuProfilingInfo生成, 不外露)
    ccuTaskInfo->missionId = CCU_PROF_INVALID_MISSION_ID;
    ccuTaskInfo->instrId = CCU_PROF_INVALID_INSTR_ID;

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
void ReportCcuNodeApiAndBasicInfo(const CcuProfReportContext& ctx, uint32_t threadId)
{
    // itemId生成规则与type 14保持一致: kernel名优先, 空名回退函数地址hash
    uint64_t itemId = 0;
    if (ctx.kernelName != nullptr) {
        itemId = g_asccommCcuProfApi.msprofStr2Id(ctx.kernelName, std::strlen(ctx.kernelName));
    } else {
        char funcTag[sizeof(ctx.kernelFunc) * 2 + 1] = {0};
        (void)snprintf(funcTag, sizeof(funcTag), "%p", ctx.kernelFunc);
        itemId = g_asccommCcuProfApi.msprofStr2Id(funcTag, static_cast<size_t>(std::strlen(funcTag)));
    }

    // 1. Node API耗时条: msprof时间线上呈现为一次host API调用的起止区间
    MsprofApi apiData{};
    apiData.level = static_cast<uint16_t>(MSPROF_REPORT_NODE_LEVEL);
    apiData.type = MSPROF_REPORT_NODE_LAUNCH_TYPE;
    apiData.threadId = threadId;
    apiData.beginTime = ctx.beginTime;
    apiData.endTime = ctx.endTime;
    apiData.itemId = itemId;
    const int32_t apiRet = g_asccommCcuProfApi.msprofReportApi(1, &apiData);
    if (apiRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportApi(NodeApi) failed, ret[%d].", apiRet);
    }

    // 2. 节点基本信息: 为时间线上的节点提供opName/opType/taskType标识
    MsprofCompactInfo compactData{};
    compactData.level = static_cast<uint16_t>(MSPROF_REPORT_NODE_LEVEL);
    compactData.type = MSPROF_REPORT_NODE_BASIC_INFO_TYPE;
    compactData.threadId = threadId;
    compactData.dataLen = sizeof(MsprofNodeBasicInfo);
    compactData.timeStamp = ctx.endTime;
    compactData.data.nodeBasicInfo.opName = itemId;
    compactData.data.nodeBasicInfo.taskType = static_cast<uint32_t>(MSPROF_GE_TASK_TYPE_HCCL);
    compactData.data.nodeBasicInfo.opType = itemId;
    compactData.data.nodeBasicInfo.blockDim = 0;
    compactData.data.nodeBasicInfo.opFlag = 0;
    const int32_t compactRet = g_asccommCcuProfApi.msprofReportCompactInfo(1, &compactData, sizeof(MsprofCompactInfo));
    if (compactRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportCompactInfo(NodeBasicInfo) failed, ret[%d].", compactRet);
    }
}

// 公共payload组装辅助: 数据面无来源的字段按非法值填充
// (与hcomm CcuProfilingInfo初始化约定一致: channelId=0xFFFF/remoteRankId=0xFFFFFFFF,
//  其余无来源标量填INVALID/0xFF, 解析端按unknown呈现, 不伪造合法值)
struct CcuAdditionalInfoCommonFields {
    uint64_t itemId = 0;
    uint16_t streamId = static_cast<uint16_t>(INVALID_UINT);
    uint32_t taskId = INVALID_UINT;
    uint8_t dieId = 0;
};

// type 15/16 首个channel槽位是否填伪造的有效值(channelId=0, remoteRankId=0):
// msprof解析端会丢弃全无效channel的记录, 直驱路径暂无真实channel来源, 默认开启以
// 保证记录可见; 待数据面提供真实channel后置false或删除本开关
constexpr bool ASCCOMM_CCU_PROF_FAKE_CHANNEL = true;

// type 15 ccu_wait_signal_info: WaitCKE粒度. 直驱路径无ckeId/mask/channel等来源,
// 按要求以默认空值(非法值)填充上报; 结构中version/workFlowMode为协议常量
bool ReportCcuWaitSignalInfo(
    const CcuProfReportContext& ctx, const CcuAdditionalInfoCommonFields& common, uint32_t threadId)
{
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_CCU_WAIT_SIGNAL_INFO;
    reporterData.threadId = threadId;
    reporterData.dataLen = sizeof(MsprofCcuWaitSignalInfo);
    reporterData.timeStamp = ctx.endTime;

    auto* waitSignalInfo = reinterpret_cast<MsprofCcuWaitSignalInfo*>(reporterData.data);
    waitSignalInfo->version = 0;
    waitSignalInfo->itemId = common.itemId;
    waitSignalInfo->groupName = DFX_INVALID_U64; // 无通信域上下文 → 非法值
    waitSignalInfo->rankId = INVALID_VALUE_RANKID;
    waitSignalInfo->ranksize = INVALID_VALUE_RANKSIZE;
    waitSignalInfo->workFlowMode = HCCL_WORKFLOW_MODE_OP_BASE;
    waitSignalInfo->streamId = common.streamId;
    waitSignalInfo->taskId = common.taskId;
    waitSignalInfo->dieId = common.dieId;
    // ckeId/mask/missionId/instrId在直驱路径无来源, 填非法值
    waitSignalInfo->instrId = CCU_PROF_INVALID_INSTR_ID;
    waitSignalInfo->missionId = CCU_PROF_INVALID_MISSION_ID;
    waitSignalInfo->ckeId = INVALID_UINT;
    waitSignalInfo->mask = 0;
    for (uint32_t i = 0; i < CCU_MAX_CHANNEL_NUM; i++) {
        waitSignalInfo->channelId[i] = INVALID_VALUE_CHANNELID;
        waitSignalInfo->remoteRankId[i] = INVALID_VALUE_RANKID;
    }
    if constexpr (ASCCOMM_CCU_PROF_FAKE_CHANNEL) {
        waitSignalInfo->channelId[0] = 0;
        waitSignalInfo->remoteRankId[0] = 0;
    }

    const int32_t ret = g_asccommCcuProfApi.msprofReportAdditionalInfo(1, &reporterData, sizeof(MsprofAdditionalInfo));
    if (ret != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportAdditionalInfo(WaitSignal) failed, ret[%d].", ret);
        return false;
    }
    return true;
}

// type 16 ccu_group_info: LoopGroup粒度. 直驱路径无reduceOp/dataType/dataSize/channel等
// 来源, 按要求以默认空值(非法值)填充上报
bool ReportCcuGroupInfo(const CcuProfReportContext& ctx, const CcuAdditionalInfoCommonFields& common, uint32_t threadId)
{
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_CCU_GROUP_INFO;
    reporterData.threadId = threadId;
    reporterData.dataLen = sizeof(MsprofCcuGroupInfo);
    reporterData.timeStamp = ctx.endTime;

    auto* ccuGroupInfo = reinterpret_cast<MsprofCcuGroupInfo*>(reporterData.data);
    ccuGroupInfo->version = 0;
    ccuGroupInfo->itemId = common.itemId;
    ccuGroupInfo->groupName = DFX_INVALID_U64; // 无通信域上下文 → 非法值
    ccuGroupInfo->rankId = INVALID_VALUE_RANKID;
    ccuGroupInfo->ranksize = INVALID_VALUE_RANKSIZE;
    ccuGroupInfo->workFlowMode = HCCL_WORKFLOW_MODE_OP_BASE;
    ccuGroupInfo->streamId = common.streamId;
    ccuGroupInfo->taskId = common.taskId;
    ccuGroupInfo->dieId = common.dieId;
    // missionId/instrId在直驱路径无来源, 填非法值
    ccuGroupInfo->instrId = CCU_PROF_INVALID_INSTR_ID;
    ccuGroupInfo->missionId = CCU_PROF_INVALID_MISSION_ID;
    // reduceOpType/inputDataType/outputDataType/dataSize无来源: 类型类填INVALID_TYPE_VALUE,
    // dataSize为数值类填0(空值)
    ccuGroupInfo->reduceOpType = INVALID_TYPE_VALUE;
    ccuGroupInfo->inputDataType = INVALID_TYPE_VALUE;
    ccuGroupInfo->outputDataType = INVALID_TYPE_VALUE;
    ccuGroupInfo->dataSize = 0;
    for (uint32_t i = 0; i < CCU_MAX_CHANNEL_NUM; i++) {
        ccuGroupInfo->channelId[i] = INVALID_VALUE_CHANNELID;
        ccuGroupInfo->remoteRankId[i] = INVALID_VALUE_RANKID;
    }
    if constexpr (ASCCOMM_CCU_PROF_FAKE_CHANNEL) {
        ccuGroupInfo->channelId[0] = 0;
        ccuGroupInfo->remoteRankId[0] = 0;
    }

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
void ReportCcuHcclTaskApiAndInfo(const CcuProfReportContext& ctx, uint32_t threadId)
{
    constexpr char CCU_TASK_OP_NAME[] = "Ccu"; // hcomm PROF_TASK_OP_NAME_V2中TASK_CCU的桶名
    const size_t opNameLen = std::strlen(CCU_TASK_OP_NAME);
    const uint64_t itemId = g_asccommCcuProfApi.msprofStr2Id(CCU_TASK_OP_NAME, opNameLen);

    // 1. TaskApi耗时条: msprof通信视图中以"Ccu"类目呈现本次通信任务耗时
    MsprofApi apiData{};
    apiData.level = static_cast<uint16_t>(MSPROF_REPORT_HCCL_NODE_LEVEL);
    apiData.type = MSPROF_REPORT_HCCL_MASTER_TYPE;
    apiData.threadId = threadId;
    apiData.beginTime = ctx.beginTime;
    apiData.endTime = ctx.endTime;
    apiData.itemId = itemId;
    const int32_t apiRet = g_asccommCcuProfApi.msprofReportApi(1, &apiData);
    if (apiRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportApi(HcclTaskApi) failed, ret[%d].", apiRet);
    }

    // 2. hccl_info明细: 通信明细骨架(业务字段无来源按默认空值填充)
    MsprofAdditionalInfo reporterData{};
    reporterData.level = MSPROF_REPORT_HCCL_NODE_LEVEL;
    reporterData.type = MSPROF_REPORT_HCCL_TASK_INFO_TYPE;
    reporterData.threadId = threadId;
    reporterData.dataLen = sizeof(MsprofHcclInfo);
    reporterData.timeStamp = ctx.endTime;

    auto* hcclInfo = reinterpret_cast<MsprofHcclInfo*>(reporterData.data);
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
    hcclInfo->role = 0xFFFFFFFF;          // hcomm构造函数默认值
    hcclInfo->durationEstimated = 0.0;    // 无来源 → 空值
    hcclInfo->srcAddr = 0xFFFFFFFF;       // hcomm构造函数默认值
    hcclInfo->dstAddr = 0xFFFFFFFF;       // hcomm构造函数默认值
    hcclInfo->dataSize = 0;               // hcomm构造函数默认值
    hcclInfo->opType = 0xFFFFFFFF;        // hcomm构造函数默认值
    hcclInfo->dataType = 0xFFFFFFFF;      // hcomm构造函数默认值
    hcclInfo->linkType = 0xFFFFFFFF;      // hcomm构造函数默认值
    hcclInfo->transportType = 0xFFFFFFFF; // hcomm构造函数默认值
    hcclInfo->rdmaType = 0xFFFFFFFF;      // hcomm构造函数默认值
    hcclInfo->reserve2 = 0;

    const int32_t infoRet =
        g_asccommCcuProfApi.msprofReportAdditionalInfo(1, &reporterData, sizeof(MsprofAdditionalInfo));
    if (infoRet != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportAdditionalInfo(HcclInfo) failed, ret[%d].", infoRet);
    }
}

// launch后上报入口: 查询taskId/streamId并发送type 14; 任何失败只记日志, 不影响launch结果
void ReportCcuDataPlaneAfterLaunch(const CcuProfReportContext& ctx)
{
    if (!IsCcuProfReportReady()) {
        return;
    }
    // 1. Node粒度API耗时条(launch的host侧耗时)与节点基本信息, 对齐hcomm ReportNodeApi/
    //    ReportNodeBasicInfo; 二者不依赖taskId/streamId, 放在runtime查询前发送
    ReportCcuNodeApiAndBasicInfo(ctx, static_cast<uint32_t>(syscall(SYS_gettid)));
    // 2. hccl层(5500)通信任务画像(TaskApi耗时条+hccl_info明细), 不依赖taskId/streamId,
    //    紧随Node粒度之后发送
    ReportCcuHcclTaskApiAndInfo(ctx, static_cast<uint32_t>(syscall(SYS_gettid)));
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
    const uint32_t threadId = static_cast<uint32_t>(syscall(SYS_gettid));
    (void)ReportCcuTaskInfo(ctx, streamId, taskId, threadId);

    // 3. type 15/16 ccu_wait_signal_info / ccu_group_info: 三个payload共享的公共字段
    //    (itemId复用kernel名hash, streamId/taskId为runtime查询值, dieId来自die mask)
    uint64_t itemId = 0;
    if (ctx.kernelName != nullptr) {
        itemId = g_asccommCcuProfApi.msprofStr2Id(ctx.kernelName, std::strlen(ctx.kernelName));
    } else {
        char funcTag[sizeof(ctx.kernelFunc) * 2 + 1] = {0};
        (void)snprintf(funcTag, sizeof(funcTag), "%p", ctx.kernelFunc);
        itemId = g_asccommCcuProfApi.msprofStr2Id(funcTag, static_cast<size_t>(std::strlen(funcTag)));
    }
    const CcuAdditionalInfoCommonFields common{
        itemId, static_cast<uint16_t>(streamId), taskId, static_cast<uint8_t>(ctx.dieId)};
    (void)ReportCcuWaitSignalInfo(ctx, common, threadId);
    (void)ReportCcuGroupInfo(ctx, common, threadId);
}

} // namespace

#endif // ASCCOMM_CCU_HAS_PROFAPI

CcuResult LaunchCcuKernelWithStream(
    CcuKernelHandle kernelHandle, const void* taskArgs, uint32_t taskArgsNum, aclrtStream stream,
    const void* kernelFunc, const char* kernelName, uint64_t phyDieMask)
{
    if (stream == nullptr) {
        ASCCOMM_CCU_LOG_ERROR("[%s] stream is nullptr.", __func__);
        return CCU_E_PTR;
    }
    if (taskArgsNum > CCU_SQE_ARGS_LEN) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] task argument count[%u] exceeds maximum[%u].", __func__, taskArgsNum, CCU_SQE_ARGS_LEN);
        return CCU_E_NOT_SUPPORT;
    }

    ThreadHandle threadHandle = 0;
    CcuResult ret = GetThreadHandleCache().GetOrCreate(stream, threadHandle);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

#ifdef ASCCOMM_CCU_HAS_PROFAPI
    // 首次launch时初始化profapi并注册profiling开关callback(幂等, 失败自动降级不影响launch)
    (void)InitCcuProfilingOnce();

    // 数据面profiling上下文: kernel名优先(入参传入, 可为空), dieId来自cfg的die mask
    CcuProfReportContext profCtx{};
    profCtx.kernelFunc = kernelFunc;
    profCtx.kernelName = kernelName;
    profCtx.dieId = GetDieIdByMask(phyDieMask);
    const bool profEnabled = IsCcuProfReportReady();
    if (profEnabled) {
        profCtx.beginTime = g_asccommCcuProfApi.msprofSysCycleTime();
    }
#endif

    CcuResult launchRet = HcommCcuKernelLaunch(threadHandle, kernelHandle, taskArgs, taskArgsNum);

#ifdef ASCCOMM_CCU_HAS_PROFAPI
    if (profEnabled && launchRet == CCU_SUCCESS) {
        profCtx.endTime = g_asccommCcuProfApi.msprofSysCycleTime();
        ReportCcuDataPlaneAfterLaunch(profCtx);
    }
#endif

    return launchRet;
}

} // namespace

extern "C" uint64_t asccomm_ccu_get_launch_hash_tag(const char* tag)
{
    if (tag == nullptr) {
        return 0;
    }
    return static_cast<uint64_t>(XXH3_64bits(tag, std::strlen(tag)));
}

extern "C" ccu_result asccomm_ccu_host_kernel_launch(
    const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args)
{
    if (kernel_func == nullptr || cfg == nullptr || args == nullptr) {
        return CCU_E_PTR;
    }
    CcuResult ret = ValidateLaunchCfg(cfg);
    if (ret != CCU_SUCCESS) {
        return ret;
    }
    if (!IsCcuKernelLaunchApiAvailable()) {
        return CCU_E_NOT_SUPPORT;
    }

    CcuKernelHandle kernelHandle = 0;
    uint32_t taskArgsNum = 0;
    ret = GetKernelHandleCache().Match(kernel_func, kernel_name, cfg, args, kernelHandle, taskArgsNum);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    return LaunchCcuKernelWithStream(
        kernelHandle, args, taskArgsNum, cfg->stream, kernel_func, kernel_name, cfg->ccu_schd.phy_die_mask);
}

extern "C" CcuResult HcommCcuHostKernelLaunch(
    const void* kernel_func, const char* kernel_name, const HcommLaunchKernelCfg* cfg, void* args)
{
    if (cfg == nullptr) {
        return asccomm_ccu_host_kernel_launch(kernel_func, kernel_name, nullptr, args);
    }

    asccomm_launch_kernel_cfg asccommCfg{};
    asccommCfg.ccu_schd.num_blocks = cfg->ccuSchd.numBlocks;
    asccommCfg.ccu_schd.reserved = cfg->ccuSchd.reserved;
    asccommCfg.ccu_schd.phy_die_mask = cfg->ccuSchd.phyDieMask;
    asccommCfg.ccu_schd.binary_cache_tag = cfg->ccuSchd.binaryCacheTag;
    asccommCfg.ccu_ins = cfg->ccuIns;
    asccommCfg.stream = cfg->stream;
    asccommCfg.attrs = cfg->attrs;
    return asccomm_ccu_host_kernel_launch(kernel_func, kernel_name, &asccommCfg, args);
}

extern "C" uint64_t HcommCcuGetLaunchHashTag(const char* tag) { return asccomm_ccu_get_launch_hash_tag(tag); }
