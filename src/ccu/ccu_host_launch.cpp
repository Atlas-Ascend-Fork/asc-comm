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
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <sys/syscall.h>
#include <unordered_map>
#include <unistd.h>

#include "xxhash_impl.inc"

// CCU数据面profiling依赖CANN pkg_inc下的profapi头文件; 无CANN头的环境自动降级为不支持profiling
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

// [DEBUG] launch链路追踪打印: 环境变量 ASCCOMM_CCU_LAUNCH_DEBUG=1 时输出到stderr, 不依赖dlog配置/级别
void CcuLaunchDebugLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void CcuLaunchDebugLog(const char* fmt, ...)
{
    static const bool enabled = (std::getenv("ASCCOMM_CCU_LAUNCH_DEBUG") != nullptr);
    if (!enabled) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    (void)fprintf(stderr, "[ASCCOMM_CCU_LAUNCH][tid=%ld] ", static_cast<long>(syscall(SYS_gettid)));
    (void)vfprintf(stderr, fmt, args);
    (void)fprintf(stderr, "\n");
    va_end(args);
}

// 阶段出入口统一包装: 进入打印step, 退出打印step+结果, 失败时同时打dlog_error
#define ASCCOMM_CCU_LAUNCH_TRACE_STEP(step, resultExpr)                       \
    do {                                                                      \
        CcuLaunchDebugLog("%s: enter.", (step));                              \
        const CcuResult stepRet = (resultExpr);                               \
        if (stepRet != CCU_SUCCESS) {                                         \
            CcuLaunchDebugLog("%s: FAILED, ret[%d].", (step),                 \
                static_cast<int32_t>(stepRet));                               \
            ASCCOMM_CCU_LOG_ERROR("[%s] %s failed, ret[%d].", __func__,       \
                (step), static_cast<int32_t>(stepRet));                       \
            return stepRet;                                                   \
        }                                                                     \
        CcuLaunchDebugLog("%s: OK.", (step));                                 \
    } while (0)

// 把CcuResult数值翻译为可读名称, 便于日志排查
const char* CcuResultToStr(CcuResult ret)
{
    switch (ret) {
        case CCU_SUCCESS:
            return "CCU_SUCCESS";
        case CCU_E_PARA:
            return "CCU_E_PARA";
        case CCU_E_PTR:
            return "CCU_E_PTR";
        case CCU_E_INTERNAL:
            return "CCU_E_INTERNAL";
        case CCU_E_NOT_SUPPORT:
            return "CCU_E_NOT_SUPPORT";
        case CCU_E_NOT_FOUND:
            return "CCU_E_NOT_FOUND";
        case CCU_E_UNAVAIL:
            return "CCU_E_UNAVAIL";
        case CCU_E_RUNTIME:
            return "CCU_E_RUNTIME";
        default:
            return "CCU_E_UNKNOWN";
    }
}

bool IsCcuKernelLaunchApiAvailable()
{
    auto registerStart = HcommCcuKernelRegisterStart;
    auto registerKernel = HcommCcuKernelRegister;
    auto registerEnd = HcommCcuKernelRegisterEnd;
    auto getTaskArgsNum = HcommCcuGetTaskArgsNum;
    auto kernelLaunch = HcommCcuKernelLaunch;
    // 逐符号打印解析结果: 任一为NULL即launch整体不可用, 便于定位是libhcomm副本缺哪个符号
    CcuLaunchDebugLog(
        "IsCcuKernelLaunchApiAvailable: RegisterStart[%p] Register[%p] RegisterEnd[%p] GetTaskArgsNum[%p] "
        "KernelLaunch[%p], ThreadAllocWithStream(strong ref)[%p].",
        reinterpret_cast<const void*>(registerStart), reinterpret_cast<const void*>(registerKernel),
        reinterpret_cast<const void*>(registerEnd), reinterpret_cast<const void*>(getTaskArgsNum),
        reinterpret_cast<const void*>(kernelLaunch), reinterpret_cast<const void*>(HcommThreadAllocWithStream));
    const bool allAvailable = registerStart != nullptr && registerKernel != nullptr && registerEnd != nullptr
        && getTaskArgsNum != nullptr && kernelLaunch != nullptr;
    if (!allAvailable) {
        CcuLaunchDebugLog("IsCcuKernelLaunchApiAvailable: UNAVAILABLE, some weak symbols are NULL, "
                          "loaded libhcomm likely predates the CCU host-launch API set.");
    }
    return allAvailable;
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
    CcuLaunchDebugLog(
        "RegisterCcuKernel: begin, kernelFunc[%p] kernelName[%s] insHandle[0x%llx] num_blocks[%u] phy_die_mask[0x%llx] "
        "binary_cache_tag[0x%llx] args[%p].",
        kernelFunc, kernelName != nullptr ? kernelName : "(null)",
        static_cast<unsigned long long>(cfg->ccu_ins), cfg->ccu_schd.num_blocks,
        static_cast<unsigned long long>(cfg->ccu_schd.phy_die_mask),
        static_cast<unsigned long long>(cfg->ccu_schd.binary_cache_tag), args);

    CcuResult ret = HcommCcuKernelRegisterStart(cfg->ccu_ins);
    CcuLaunchDebugLog("RegisterCcuKernel: HcommCcuKernelRegisterStart ins[0x%llx] ret[%d(%s)].",
        static_cast<unsigned long long>(cfg->ccu_ins), static_cast<int32_t>(ret), CcuResultToStr(ret));
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] HcommCcuKernelRegisterStart failed, ret[%d], ins[0x%llx].", __func__, static_cast<int32_t>(ret),
            static_cast<unsigned long long>(cfg->ccu_ins));
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
    CcuLaunchDebugLog("RegisterCcuKernel: HcommCcuKernelRegister dieId[%u] kernelHandle[0x%llx] ret[%d(%s)].", dieId,
        static_cast<unsigned long long>(kernelHandle), static_cast<int32_t>(ret), CcuResultToStr(ret));
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] HcommCcuKernelRegister failed, ret[%d], dieId[%u], kernelName[%s].", __func__, static_cast<int32_t>(ret),
            dieId, kernelName != nullptr ? kernelName : "(null)");
        (void)HcommCcuKernelRegisterEnd(cfg->ccu_ins);
        return ret;
    }

    ret = HcommCcuKernelRegisterEnd(cfg->ccu_ins);
    CcuLaunchDebugLog("RegisterCcuKernel: HcommCcuKernelRegisterEnd ins[0x%llx] ret[%d(%s)].",
        static_cast<unsigned long long>(cfg->ccu_ins), static_cast<int32_t>(ret), CcuResultToStr(ret));
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] HcommCcuKernelRegisterEnd failed, ret[%d], kernelHandle[0x%llx].", __func__, static_cast<int32_t>(ret),
            static_cast<unsigned long long>(kernelHandle));
        return ret;
    }
    CcuLaunchDebugLog("RegisterCcuKernel: done, kernelHandle[0x%llx].",
        static_cast<unsigned long long>(kernelHandle));
    return CCU_SUCCESS;
}

class KernelHandleCache {
public:
    CcuResult Match(
        const void* kernelFunc, const char* kernelName, const asccomm_launch_kernel_cfg* cfg, void* args,
        CcuKernelHandle& kernelHandle, uint32_t& taskArgsNum)
    {
        const uint64_t cacheTag = cfg->ccu_schd.binary_cache_tag;
        CcuLaunchDebugLog("KernelHandleCache::Match: kernelName[%s] cacheTag[0x%llx].",
            kernelName != nullptr ? kernelName : "(null)", static_cast<unsigned long long>(cacheTag));
        if (cacheTag == 0U) {
            CcuLaunchDebugLog("KernelHandleCache::Match: cacheTag==0, register fresh kernel each launch.");
            ASCCOMM_CCU_LAUNCH_TRACE_STEP("KernelHandleCache::Match->RegisterAndGetTaskArgsNum",
                RegisterAndGetTaskArgsNum(kernelFunc, kernelName, cfg, args, kernelHandle, taskArgsNum));
            CcuLaunchDebugLog("KernelHandleCache::Match: done (no-cache path), kernelHandle[0x%llx] taskArgsNum[%u].",
                static_cast<unsigned long long>(kernelHandle), taskArgsNum);
            return CCU_SUCCESS;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = cache_.find(cacheTag);
        if (iter != cache_.end()) {
            kernelHandle = iter->second;
            CcuLaunchDebugLog("KernelHandleCache::Match: cache HIT tag[0x%llx] -> kernelHandle[0x%llx], re-querying "
                              "taskArgsNum.",
                static_cast<unsigned long long>(cacheTag), static_cast<unsigned long long>(kernelHandle));
            return HcommCcuGetTaskArgsNum(kernelHandle, &taskArgsNum);
        }
        CcuLaunchDebugLog("KernelHandleCache::Match: cache MISS tag[0x%llx], registering new kernel.",
            static_cast<unsigned long long>(cacheTag));

        CcuResult ret = RegisterAndGetTaskArgsNum(kernelFunc, kernelName, cfg, args, kernelHandle, taskArgsNum);
        if (ret != CCU_SUCCESS) {
            CcuLaunchDebugLog("KernelHandleCache::Match: register FAILED ret[%d(%s)], tag[0x%llx].",
                static_cast<int32_t>(ret), CcuResultToStr(ret), static_cast<unsigned long long>(cacheTag));
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] kernel register failed, ret[%d], cacheTag[0x%llx].", __func__, static_cast<int32_t>(ret),
                static_cast<unsigned long long>(cacheTag));
            return ret;
        }
        cache_[cacheTag] = kernelHandle;
        CcuLaunchDebugLog("KernelHandleCache::Match: cached kernelHandle[0x%llx] taskArgsNum[%u] under tag[0x%llx].",
            static_cast<unsigned long long>(kernelHandle), taskArgsNum, static_cast<unsigned long long>(cacheTag));
        return CCU_SUCCESS;
    }

private:
    CcuResult RegisterAndGetTaskArgsNum(
        const void* kernelFunc, const char* kernelName, const asccomm_launch_kernel_cfg* cfg, void* args,
        CcuKernelHandle& kernelHandle, uint32_t& taskArgsNum)
    {
        ASCCOMM_CCU_LAUNCH_TRACE_STEP(
            "RegisterAndGetTaskArgsNum->RegisterCcuKernel", RegisterCcuKernel(kernelFunc, kernelName, cfg, args, kernelHandle));
        CcuResult ret = HcommCcuGetTaskArgsNum(kernelHandle, &taskArgsNum);
        CcuLaunchDebugLog("RegisterAndGetTaskArgsNum: HcommCcuGetTaskArgsNum kernelHandle[0x%llx] -> taskArgsNum[%u] "
                          "ret[%d(%s)].",
            static_cast<unsigned long long>(kernelHandle), taskArgsNum, static_cast<int32_t>(ret),
            CcuResultToStr(ret));
        if (ret != CCU_SUCCESS) {
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] HcommCcuGetTaskArgsNum failed, ret[%d], kernelHandle[0x%llx].", __func__, static_cast<int32_t>(ret),
                static_cast<unsigned long long>(kernelHandle));
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
            CcuLaunchDebugLog("ThreadHandleCache::GetOrCreate: cache HIT stream[%p] -> threadHandle[0x%llx].", stream,
                static_cast<unsigned long long>(thread));
            return CCU_SUCCESS;
        }
        CcuLaunchDebugLog("ThreadHandleCache::GetOrCreate: cache MISS stream[%p], calling "
                          "HcommThreadAllocWithStream(engine=%d, notifyNum=0)...",
            stream, static_cast<int32_t>(COMM_ENGINE_CCU));

        ThreadHandle newThread = 0;
        const HcommResult allocRet = HcommThreadAllocWithStream(COMM_ENGINE_CCU, stream, 0, &newThread);
        CcuLaunchDebugLog("ThreadHandleCache::GetOrCreate: HcommThreadAllocWithStream ret[%d] threadHandle[0x%llx] "
                          "stream[%p].",
            static_cast<int32_t>(allocRet), static_cast<unsigned long long>(newThread), stream);
        if (allocRet != 0) {
            CcuLaunchDebugLog("ThreadHandleCache::GetOrCreate: FAILED ret[%d], engine[%d]. Check libhcomm "
                              "HcommThreadAllocWithStream engine policy (e.g. PR5352 only-CPU/CPU_TS check).",
                static_cast<int32_t>(allocRet), static_cast<int32_t>(COMM_ENGINE_CCU));
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] HcommThreadAllocWithStream failed, ret[%d], stream[%p], engine[%d].", __func__, allocRet, stream,
                static_cast<int32_t>(COMM_ENGINE_CCU));
            return ConvertHcommResult(allocRet);
        }
        if (newThread == 0) {
            ASCCOMM_CCU_LOG_ERROR("[%s] HcommThreadAllocWithStream returned an empty handle.", __func__);
            return CCU_E_INTERNAL;
        }

        cache_.emplace(stream, newThread);
        thread = newThread;
        CcuLaunchDebugLog("ThreadHandleCache::GetOrCreate: cached threadHandle[0x%llx] for stream[%p].",
            static_cast<unsigned long long>(thread), stream);
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

static_assert(sizeof(MsprofCcuTaskInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(MsprofCcuGroupInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(MsprofCcuWaitSignalInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);

// 数据面单条CCU任务描述, 由 CcuKernel 侧在 launch 前填充
constexpr uint8_t CCU_PROF_TYPE_TASK = 0;
constexpr uint8_t CCU_PROF_TYPE_WAIT_SIGNAL = 1;
constexpr uint8_t CCU_PROF_TYPE_GROUP = 2;
constexpr uint8_t CCU_PROF_TYPE_MAP = 3;

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
    api.msprofStr2Id = reinterpret_cast<decltype(api.msprofStr2Id)>(dlsym(handle, "MsprofStr2Id"));
    api.msprofSysCycleTime
        = reinterpret_cast<decltype(api.msprofSysCycleTime)>(dlsym(handle, "MsprofSysCycleTime"));
    if (api.msprofRegisterCallback == nullptr || api.msprofRegTypeInfo == nullptr
        || api.msprofReportAdditionalInfo == nullptr || api.msprofStr2Id == nullptr
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

// [DEBUG] 验证用打印: 环境变量 ASCCOMM_CCU_PROF_DEBUG=1 时输出到stderr, 不依赖dlog配置
void CcuProfDebugLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void CcuProfDebugLog(const char* fmt, ...)
{
    static const bool enabled = (std::getenv("ASCCOMM_CCU_PROF_DEBUG") != nullptr);
    if (!enabled) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    (void)fprintf(stderr, "[ASCCOMM_CCU_PROF] ");
    (void)vfprintf(stderr, fmt, args);
    (void)fprintf(stderr, "\n");
    va_end(args);
}

// 初始化profapi并注册profiling开关callback; 只允许成功一次, 失败后降级为不可用
// 后续步骤将在 HcommCcuKernelLaunch 前调用本函数; 当前阶段先保留为可被上层引用的入口
[[maybe_unused]] bool InitCcuProfilingOnce()
{
    auto& prof = AsccommCcuProfiling::GetInstance();
    uint32_t expected = ASCCOMM_CCU_PROFAPI_UNINITIALIZED;
    if (!prof.profApiState.compare_exchange_strong(expected, ASCCOMM_CCU_PROFAPI_INITIALIZED)) {
        return prof.profApiState.load() == ASCCOMM_CCU_PROFAPI_INITIALIZED;
    }
    if (!LoadCcuProfApi()) {
        CcuProfDebugLog("Init FAILED: dlopen/dlsym libprofapi.so failed, profiling disabled.");
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] load libprofapi.so failed, CCU data-plane profiling disabled.");
        prof.profApiState.store(ASCCOMM_CCU_PROFAPI_UNAVAILABLE);
        return false;
    }
    ProfCommandHandle callback = [](uint32_t rtType, void* data, uint32_t len) -> int32_t {
        return AsccommCcuProfiling::GetInstance().CommandHandle(rtType, data, len);
    };
    const int32_t ret = g_asccommCcuProfApi.msprofRegisterCallback(MSPROF_MODULE_ID_HCCL, callback);
    if (ret != 0) {
        CcuProfDebugLog("Init FAILED: MsprofRegisterCallback ret=%d, profiling disabled.", ret);
        ASCCOMM_CCU_LOG_ERROR(
            "[CcuProfiling] MsprofRegisterCallback failed, ret[%d], CCU data-plane profiling disabled.", ret);
        prof.profApiState.store(ASCCOMM_CCU_PROFAPI_UNAVAILABLE);
        return false;
    }
    CcuProfDebugLog(
        "Init OK: profapi callback registered (moduleId=%u), waiting for START/STOP commands.",
        MSPROF_MODULE_ID_HCCL);
    ASCCOMM_CCU_LOG_ERROR(
        "[CcuProfiling] profapi callback registered, CCU data-plane profiling ready."); // TODO: 改为info级别日志
    return true;
}

} // namespace

// 处理profapi下发的profiling开关命令, 与hcomm ProfilingHandler::CommandHandle语义一致
int32_t AsccommCcuProfiling::CommandHandle(uint32_t rtType, void* data, uint32_t len)
{
    (void)len;
    CcuProfDebugLog("CommandHandle: rtType=%u", rtType);
    if (data == nullptr || rtType != RT_PROF_CTRL_SWITCH) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] invalid command, rtType[%u].", rtType);
        return 1; // HCCL_E_PARA
    }
    const rtProfCommandHandle_t* profConfigParam = static_cast<const rtProfCommandHandle_t*>(data);
    const uint32_t type = profConfigParam->type;
    CcuProfDebugLog("CommandHandle: type=%u profSwitch=0x%llx", type,
        static_cast<unsigned long long>(profConfigParam->profSwitch));
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
        CcuProfDebugLog("StartCcuSubscribe: L1 not set in profSwitch=0x%llx, skip.",
            static_cast<unsigned long long>(profSwitch));
        return;
    }
    std::lock_guard<std::mutex> lock(ccuMutex);
    enableHcclL0.store(true);
    enableHcclL1.store(true);
    if (ccuInfoTypesRegistered.load()) {
        CcuProfDebugLog("StartCcuSubscribe: types already registered, L0/L1 enabled.");
        return;
    }
    const std::pair<uint32_t, const char*> ccuInfoTypes[] = {
        {MSPROF_REPORT_CCU_TASK_INFO, "ccu_task_info"},
        {MSPROF_REPORT_CCU_WAIT_SIGNAL_INFO, "ccu_wait_signal_info"},
        {MSPROF_REPORT_CCU_GROUP_INFO, "ccu_group_info"},
    };
    for (const auto& it : ccuInfoTypes) {
        const int32_t ret = g_asccommCcuProfApi.msprofRegTypeInfo(
            static_cast<uint16_t>(MSPROF_REPORT_HCCL_NODE_LEVEL), it.first, it.second);
        if (ret != 0) {
            CcuProfDebugLog("StartCcuSubscribe: RegTypeInfo type=%u(%s) FAILED ret=%d.", it.first, it.second, ret);
            ASCCOMM_CCU_LOG_ERROR(
                "[CcuProfiling] MsprofRegTypeInfo failed, type[%u], ret[%d].", it.first, ret);
            return; // 下次Start重试
        }
        CcuProfDebugLog("StartCcuSubscribe: RegTypeInfo type=%u(%s) OK.", it.first, it.second);
    }
    ccuInfoTypesRegistered.store(true);
    CcuProfDebugLog("StartCcuSubscribe: ALL DONE, L0=1 L1=1 types registered, CCU profiling ACTIVE.");
}

void AsccommCcuProfiling::StopCcuSubscribe()
{
    CcuProfDebugLog("StopCcuSubscribe: CCU profiling DEACTIVATED.");
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
    return AsccommCcuProfiling::GetInstance().profApiState.load() == ASCCOMM_CCU_PROFAPI_INITIALIZED
           && AsccommCcuProfiling::GetInstance().enableHcclL1.load()
           && AsccommCcuProfiling::GetInstance().ccuInfoTypesRegistered.load();
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
    CcuProfDebugLog(
        "ReportCcuTaskInfo: type=14 itemId=0x%llx streamId=%u taskId=%u dieId=%u name=%s, ret=%d.",
        static_cast<unsigned long long>(ccuTaskInfo->itemId), streamId, taskId, ctx.dieId,
        ctx.kernelName != nullptr ? ctx.kernelName : "<null>", ret);
    if (ret != 0) {
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] MsprofReportAdditionalInfo failed, ret[%d].", ret);
        return false;
    }
    return true;
}

// launch后上报入口: 查询taskId/streamId并发送type 14; 任何失败只记日志, 不影响launch结果
void ReportCcuDataPlaneAfterLaunch(const CcuProfReportContext& ctx)
{
    if (!IsCcuProfReportReady()) {
        return;
    }
    if (g_asccommCcuRuntimeApi.rtGetTaskIdAndStreamID == nullptr && !LoadCcuRuntimeApi()) {
        CcuProfDebugLog("ReportCcuDataPlaneAfterLaunch: rtGetTaskIdAndStreamID unavailable, skip.");
        return;
    }
    uint32_t taskId = INVALID_UINT;
    uint32_t streamId = INVALID_UINT;
    if (g_asccommCcuRuntimeApi.rtGetTaskIdAndStreamID(&taskId, &streamId) != 0) {
        CcuProfDebugLog("ReportCcuDataPlaneAfterLaunch: rtGetTaskIdAndStreamID failed, skip.");
        ASCCOMM_CCU_LOG_ERROR("[CcuProfiling] rtGetTaskIdAndStreamID failed, skip data-plane report.");
        return;
    }
    (void)ReportCcuTaskInfo(ctx, streamId, taskId, static_cast<uint32_t>(syscall(SYS_gettid)));
}

} // namespace

#endif // ASCCOMM_CCU_HAS_PROFAPI

CcuResult LaunchCcuKernelWithStream(
    CcuKernelHandle kernelHandle, const void* taskArgs, uint32_t taskArgsNum, aclrtStream stream,
    const void* kernelFunc, const char* kernelName, uint64_t phyDieMask)
{
    CcuLaunchDebugLog(
        "LaunchCcuKernelWithStream: begin, kernelHandle[0x%llx] kernelName[%s] taskArgsNum[%u] taskArgs[%p] "
        "stream[%p] phy_die_mask[0x%llx] dieId[%u].",
        static_cast<unsigned long long>(kernelHandle), kernelName != nullptr ? kernelName : "(null)", taskArgsNum,
        taskArgs, stream, static_cast<unsigned long long>(phyDieMask), GetDieIdByMask(phyDieMask));
    if (stream == nullptr) {
        ASCCOMM_CCU_LOG_ERROR("[%s] stream is nullptr.", __func__);
        return CCU_E_PTR;
    }
    if (taskArgsNum > CCU_SQE_ARGS_LEN) {
        CcuLaunchDebugLog("LaunchCcuKernelWithStream: FAILED, taskArgsNum[%u] > max[%u].", taskArgsNum,
            CCU_SQE_ARGS_LEN);
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] task argument count[%u] exceeds maximum[%u].", __func__, taskArgsNum, CCU_SQE_ARGS_LEN);
        return CCU_E_NOT_SUPPORT;
    }
    if (taskArgsNum > 0 && taskArgs == nullptr) {
        ASCCOMM_CCU_LOG_ERROR("[%s] taskArgs is nullptr while taskArgsNum[%u] > 0.", __func__, taskArgsNum);
        return CCU_E_PTR;
    }
    if (taskArgsNum > 0 && taskArgs != nullptr) {
        // 打印前几个taskArgs, 便于和样例侧构造的taskArgs数组对账(全量逐条打过于冗长)
        CcuLaunchDebugLog("LaunchCcuKernelWithStream: taskArgs[0..%u):", taskArgsNum);
        for (uint32_t i = 0; i < taskArgsNum; i++) {
            CcuLaunchDebugLog("  taskArgs[%u] = 0x%llx.", i,
                static_cast<unsigned long long>(static_cast<const uint64_t*>(taskArgs)[i]));
        }
    }

    ThreadHandle threadHandle = 0;
    CcuResult ret = GetThreadHandleCache().GetOrCreate(stream, threadHandle);
    if (ret != CCU_SUCCESS) {
        CcuLaunchDebugLog("LaunchCcuKernelWithStream: FAILED at ThreadHandleCache, ret[%d(%s)].",
            static_cast<int32_t>(ret), CcuResultToStr(ret));
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

    CcuLaunchDebugLog("LaunchCcuKernelWithStream: calling HcommCcuKernelLaunch threadHandle[0x%llx] "
                      "kernelHandle[0x%llx] argNum[%u]...",
        static_cast<unsigned long long>(threadHandle), static_cast<unsigned long long>(kernelHandle), taskArgsNum);
    CcuResult launchRet = HcommCcuKernelLaunch(threadHandle, kernelHandle, taskArgs, taskArgsNum);
    CcuLaunchDebugLog("LaunchCcuKernelWithStream: HcommCcuKernelLaunch ret[%d(%s)].", static_cast<int32_t>(launchRet),
        CcuResultToStr(launchRet));
    if (launchRet != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] HcommCcuKernelLaunch failed, ret[%d], threadHandle[0x%llx] kernelHandle[0x%llx] argNum[%u].",
            __func__, static_cast<int32_t>(launchRet), static_cast<unsigned long long>(threadHandle),
            static_cast<unsigned long long>(kernelHandle), taskArgsNum);
        return launchRet;
    }
    CcuLaunchDebugLog("LaunchCcuKernelWithStream: done OK, task submitted to stream[%p].", stream);

#ifdef ASCCOMM_CCU_HAS_PROFAPI
    if (profEnabled) {
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
    CcuLaunchDebugLog(
        "asccomm_ccu_host_kernel_launch: ENTER kernel_func[%p] kernel_name[%s] ins[0x%llx] num_blocks[%u] "
        "phy_die_mask[0x%llx] cache_tag[0x%llx] stream[%p] args[%p].",
        kernel_func, kernel_name != nullptr ? kernel_name : "(null)",
        cfg != nullptr ? static_cast<unsigned long long>(cfg->ccu_ins) : 0ULL,
        cfg != nullptr ? cfg->ccu_schd.num_blocks : 0U,
        cfg != nullptr ? static_cast<unsigned long long>(cfg->ccu_schd.phy_die_mask) : 0ULL,
        cfg != nullptr ? static_cast<unsigned long long>(cfg->ccu_schd.binary_cache_tag) : 0ULL,
        cfg != nullptr ? cfg->stream : nullptr, args);
    if (kernel_func == nullptr || cfg == nullptr || args == nullptr) {
        CcuLaunchDebugLog("asccomm_ccu_host_kernel_launch: FAILED, nullptr arg (func/cfg/args).");
        return CCU_E_PTR;
    }
    CcuResult ret = ValidateLaunchCfg(cfg);
    if (ret != CCU_SUCCESS) {
        CcuLaunchDebugLog("asccomm_ccu_host_kernel_launch: FAILED at ValidateLaunchCfg, ret[%d(%s)].",
            static_cast<int32_t>(ret), CcuResultToStr(ret));
        return ret;
    }
    if (!IsCcuKernelLaunchApiAvailable()) {
        CcuLaunchDebugLog("asccomm_ccu_host_kernel_launch: FAILED at IsCcuKernelLaunchApiAvailable -> "
                          "CCU_E_NOT_SUPPORT (see per-symbol print above).");
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] CCU launch API unavailable (some HcommCcu* weak symbols unresolved), return CCU_E_NOT_SUPPORT.",
            __func__);
        return CCU_E_NOT_SUPPORT;
    }

    CcuKernelHandle kernelHandle = 0;
    uint32_t taskArgsNum = 0;
    ret = GetKernelHandleCache().Match(kernel_func, kernel_name, cfg, args, kernelHandle, taskArgsNum);
    if (ret != CCU_SUCCESS) {
        CcuLaunchDebugLog("asccomm_ccu_host_kernel_launch: FAILED at KernelHandleCache::Match, ret[%d(%s)].",
            static_cast<int32_t>(ret), CcuResultToStr(ret));
        return ret;
    }
    CcuLaunchDebugLog("asccomm_ccu_host_kernel_launch: kernel ready, kernelHandle[0x%llx] taskArgsNum[%u].",
        static_cast<unsigned long long>(kernelHandle), taskArgsNum);

    ret = LaunchCcuKernelWithStream(kernelHandle, args, taskArgsNum, cfg->stream, kernel_func, kernel_name,
        cfg->ccu_schd.phy_die_mask);
    CcuLaunchDebugLog("asccomm_ccu_host_kernel_launch: EXIT ret[%d(%s)].", static_cast<int32_t>(ret),
        CcuResultToStr(ret));
    return ret;
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
