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
 * @file ccu_aot_adapter.cpp
 * @brief CcuAotKernel AOT adapter — 走 HComm 正式路径执行 AOT .ccuobj
 *
 * 链接 libhcomm.so，实现完整的 Register → Translate → Override → Launch 流程。
 */

#include "ccu_aot_adapter.h"

#include <cctype>
#include <cstring>
#include <cstdlib> // getenv, atoi (sync-timeout override)
#include <vector>
#include <stdexcept>
#include <memory>
#include <functional>
#include <thread>
#include <chrono>
#include <string>
#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>
#include <limits>
#include <initializer_list>

// Shared table-driven relocation. These headers contain only relocation data
// and operate on the raw instruction buffer supplied by the loader.
#include "detail/ccu_reloc_format.h"
#include "detail/reloc_apply.h"
// The reader owns .ccuobj parsing; the adapter consumes its validated result.
#include "detail/ccuobj_reader.h"
#include "detail/ccu_resource_encoding.h"
#include "ccu_runtime_external.h"

// ACL
#include "acl/acl.h"

// hcomm public headers
#include "hccl/hccl_comm.h"
#include "hccl/hccl_types.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_rank_graph.h" // HcclRankGraphGetEndpointDesc
#include "hccl/hccl_channel.h"
#include "hccl/hccl_ccu_res.h" // HcclCommQueryCcuIns
#include "hcomm/ccu/ccu_res.h"
#include "hcomm/ccu/hcomm_res_ccu_entity_defs.h"
#include "hcomm/hcomm_res.h" // HcommChannelDestroy

// Deployed runtime launch ABI. Public C API: link libruntime.so instead of
// resolving rtCCULaunch / rtKernelLaunch by hand.
#include "runtime/runtime/kernel.h"

// AGENTS「日志打印规范」：统一分级日志宏 CCU_AOT_LOG{D,I,W,E}。
#include "ccu_aot_log.h"

// ============================================================================
// .ccuobj 解析
// ============================================================================

namespace {

// HComm's CCU launch path caches the first device observed by each host
// thread.  Reusing one host thread for rank 0 and rank 1 therefore cannot be
// repaired by a later aclrtSetDevice alone.  Run APIs that consume HComm's
// implicit current-device state on a fresh, synchronously joined thread.
template <typename Result, typename Callable>
static Result runOnFreshDeviceThread(int32_t device, Result deviceError, Callable&& callable)
{
    Result result = deviceError;
    try {
        std::thread worker([&]() {
            try {
                aclError rc = aclrtSetDevice(device);
                if (rc != ACL_SUCCESS) {
                    CCU_AOT_LOGE("device worker aclrtSetDevice(%d) failed: %d\n", device, static_cast<int>(rc));
                    return;
                }
                result = callable();
            } catch (const std::exception& error) {
                CCU_AOT_LOGE("device worker device=%d exception: %s\n", device, error.what());
                result = deviceError;
            } catch (...) {
                CCU_AOT_LOGE("device worker device=%d unknown exception\n", device);
                result = deviceError;
            }
        });
        worker.join();
    } catch (const std::exception& error) {
        CCU_AOT_LOGE("device worker create/join device=%d exception: %s\n", device, error.what());
    } catch (...) {
        CCU_AOT_LOGE("device worker create/join device=%d unknown exception\n", device);
    }
    return result;
}

struct CcuobjParsed {
    uint32_t kernelIndex = 0;
    std::string name;
    const uint8_t* microcode = nullptr;
    uint32_t instrCount = 0;
    uint32_t channelArgCount = 0;
    uint32_t taskArgCount = 0;
    uint32_t missionStartInstrId = 0;
    uint32_t missionInstrCount = 0;
    // ResourceManifest（ccu-allocate-resources 按类计的 0-based 资源数）
    uint32_t varCount = 0;
    uint32_t eventCount = 0;
    uint32_t localAddrCount = 0;
    uint32_t remoteAddrCount = 0;
    uint32_t msCount = 0;       // MS/CcuBuf scratch 槽数（manifest reserved low-32）
    uint32_t executorCount = 0; // LoopEngine executor 槽数（reserved high-32）
    // Kernel relocation entries. A kernel may have an empty slice only when
    // it needs no launch-time patching.
    std::vector<ccu::ccuobj::RelocEntry> relocs;
};

bool CopyParsedKernel(const ccu::ccuobj::ParsedKernel& k, uint32_t idx, CcuobjParsed& out)
{
    if (!k.microcode)
        return false;

    out.kernelIndex = idx;
    out.name.assign(k.name ? k.name : "", k.nameLen);
    out.microcode = k.microcode;
    out.instrCount = k.instrCount;
    out.channelArgCount = k.channelArgCount;
    out.taskArgCount = k.taskArgCount;
    out.missionStartInstrId = k.missionStartInstrId;
    out.missionInstrCount = k.missionInstrCount;
    out.varCount = k.varCount;
    out.eventCount = k.eventCount;
    out.localAddrCount = k.localAddrCount;
    out.remoteAddrCount = k.remoteAddrCount;
    out.msCount = k.msCount;
    out.executorCount = k.executorCount;
    out.relocs.assign(k.relocs, k.relocs + k.relocCount); // empty if none
    return true;
}

bool ParseCcuobjAll(const void* data, size_t size, std::vector<CcuobjParsed>& out)
{
    // Delegate to the authoritative hcomm-free reader (CcuobjReader.h). The
    // adapter keeps its own CcuobjParsed records (they own reloc vectors +
    // embed in kernel args / context); zero-copy microcode pointers still point
    // into the caller-owned blob.
    ccu::ccuobj::ParsedModule mod;
    const char* parseError = nullptr;
    if (!ccu::ccuobj::parseCcuobj(data, size, mod, &parseError)) {
        CCU_AOT_LOGE("ccuobj parser rejected image: reason=%s size=%zu\n", parseError ? parseError : "unknown", size);
        return false;
    }
    if (mod.version != 1 || mod.abiRev != ccu::ccuobj::kRelocAbiRev) {
        CCU_AOT_LOGE(
            "ccuobj version/abi mismatch: version=%u abiRev=%u expected=1/%u\n", mod.version, mod.abiRev,
            ccu::ccuobj::kRelocAbiRev);
        return false;
    }
    if (mod.kernels.empty())
        return false;

    out.clear();
    out.reserve(mod.kernels.size());
    for (size_t i = 0; i < mod.kernels.size(); ++i) {
        CcuobjParsed parsed;
        if (!CopyParsedKernel(mod.kernels[i], static_cast<uint32_t>(i), parsed))
            return false;
        out.push_back(std::move(parsed));
    }
    return true;
}

} // anonymous namespace

// Deployment CANN packages do not always expose the same ACL_ERROR_RT_* macro
// names. Keep the values local so the recovery policy compiles against the
// package headers that match the runtime libhcomm.so.
static constexpr int kRtTimeout = 207020;
static constexpr int kRtWaitTimeout = 107019;
static constexpr int kRtTaskTimeout = 107020;
static constexpr int kRtDeviceTaskAbort = 107022;
static constexpr int kRtStreamAbort = 107023;
static constexpr int kRtStreamSyncTimeout = 507046;
static constexpr int kRtCcuTimeout = 507915;
static constexpr int kRtTsError = 507001; // ACL_ERROR_RT_TS_ERROR：device TS 内部错误（mission 失败）
static constexpr uint64_t kAbortFinalizeSettleMs = 250;
static constexpr size_t kInstructionHexTextLength = 192;
static constexpr auto kDeviceAbortRetryDelay = std::chrono::milliseconds(10);

static bool isMissionTimeout(aclError rc)
{
    return rc == kRtStreamSyncTimeout || rc == kRtTaskTimeout || rc == kRtTimeout || rc == kRtCcuTimeout ||
           rc == kRtTsError; // TS 错误的 mission 须 device abort 清干净，否则污染 die
}

static bool shouldAbortDeviceTasks(aclError rc)
{
    if (const char* d = getenv("CCU_AOT_DEVICE_ABORT"))
        return atoi(d) == 1;
    return isMissionTimeout(rc);
}

struct DeviceQuiescenceResult {
    aclError rc = ACL_SUCCESS;
    uint32_t attempts = 0;
};

static uint64_t elapsedMs(std::chrono::steady_clock::time_point start)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

static const char* recoveryRcClass(aclError rc)
{
    if (rc == ACL_SUCCESS)
        return "success";
    if (rc == 1)
        return "generic-failure";
    if (rc == kRtWaitTimeout)
        return "wait-timeout";
    if (rc == kRtDeviceTaskAbort)
        return "device-aborting";
    if (rc == kRtStreamAbort)
        return "stream-aborting";
    if (isMissionTimeout(rc))
        return "mission-timeout";
    return "runtime-error";
}

static std::string sanitizeRecoveryError(const char* message)
{
    if (!message || !*message)
        return "none";
    std::string result;
    result.reserve(kInstructionHexTextLength);
    for (const unsigned char ch : std::string(message)) {
        if (result.size() == kInstructionHexTextLength)
            break;
        result.push_back(std::isspace(ch) || ch == '|' ? '_' : (std::isprint(ch) ? static_cast<char>(ch) : '?'));
    }
    return result.empty() ? "none" : result;
}

static DeviceQuiescenceResult waitForDeviceQuiescence(uint64_t timeoutMs)
{
    const uint64_t boundedMs = std::max<uint64_t>(
        1, std::min<uint64_t>(timeoutMs, static_cast<uint64_t>(std::numeric_limits<int32_t>::max())));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(boundedMs);
    DeviceQuiescenceResult result;
    do {
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int32_t waitMs = static_cast<int32_t>(
            std::max<int64_t>(1, std::min<int64_t>(remaining, std::numeric_limits<int32_t>::max())));
        ++result.attempts;
        result.rc = aclrtSynchronizeDeviceWithTimeout(waitMs);
        if (result.rc != kRtDeviceTaskAbort && result.rc != kRtStreamAbort)
            return result;
        if (std::chrono::steady_clock::now() >= deadline)
            return result;
        std::this_thread::sleep_for(kDeviceAbortRetryDelay);
    } while (std::chrono::steady_clock::now() < deadline);
    return result;
}

// ============================================================================
// CcuAotContext
// ============================================================================

struct CcuAotResourceBase {
    uint32_t xn = 0;
    uint32_t gsa = 0;
    uint32_t cke = 0;
    uint32_t ms = 0;
    uint32_t mission = 0;
    uint32_t instr = 0;
    int32_t die = -1;
};

// Kernel registration state is an implementation detail of the AOT adapter.
// Keep it outside the public/control-plane context so communicator ownership
// and .ccuobj execution state cannot grow into one object again.
struct CcuAotKernelState {
    struct RegisteredKernel {
        struct AotResourceRecord {
            struct TaskParam {
                uint8_t dieId = 0;
                uint8_t missionId = 0;
                uint32_t instrStartId = 0;
                uint32_t instrCount = 0;
                uint32_t key = 0;
                uint32_t argSize = 0;
            };
            CcuAotResourceBase base{};
            bool instructionLeased = false;
            uint32_t taskArgCount = 0;
            std::vector<TaskParam> taskParams;
            bool valid = false;
        };
        struct RankKernelRecord {
            CcuKernelHandle handle = 0;
            AotResourceRecord resources;
        };
        std::vector<uint8_t> blob;
        CcuobjParsed parsed;
        std::vector<RankKernelRecord> rankRecords; // [ndev]
        bool active = false;
    };
    std::vector<RegisteredKernel> registeredKernels;
    uint32_t defaultKernelId = UINT32_MAX;
    bool registered = false;
};

struct CcuAotPodRange {
    uint32_t resourceType = 0;
    uint32_t dieId = 0;
    uint32_t startId = 0;
    uint32_t count = 0;
};

struct CcuAotPodDie {
    bool enabled = false;
    uint32_t missionKey = 0;
    uint32_t loopChannelId = 0;
    uint64_t xnBaseAddr = 0;
    uint64_t resourceSpaceTokenId = 0;
    uint64_t resourceSpaceTokenValue = 0;
};

struct CcuAotPod {
    CcuInsHandle instanceHandle = 0;
    uint32_t dieNum = 0;
    uint32_t validDieMask = 0;
    CcuAotPodDie die[2]{};
    std::vector<CcuAotPodRange> ranges;
    HcommCcuChannelGetEntityCallBack channelQuery = nullptr;
    HcommCcuInstSpaceAllocCallBack allocInstruction = nullptr;
    HcommCcuInstSpaceSubmitCallBack submitInstructions = nullptr;
};

enum CcuAotHcommControlSlot : uint32_t {
    kHcommCcuControlSlotChannelEntity = 0,
    kHcommCcuControlSlotAllocInstruction = 1,
    kHcommCcuControlSlotSubmitInstructions = 2,
};

struct CcuAotContext {
    struct RankBinding {
        uint32_t globalRank = 0;
        uint32_t worldSize = 1;
    };
    uint32_t ndev = 0;
    // The attach path keeps one communicator per process. rankBinding is the
    // lightweight topology binding supplied by the attach entry point.
    RankBinding rankBinding;
    std::vector<int32_t> devices; // 逻辑设备 ID（按 rank）
    std::vector<HcclComm> comms;
    std::vector<CcuInsHandle> callerInstanceHandles;
    // Read-only instance snapshots borrowed from HComm. Pointers inside the
    // original entity are copied into value ranges before the query returns;
    // callback slots remain valid while the communicator is alive.
    std::vector<CcuAotPod> pods;
    // Peer slots are enumerated by ascending global rank, excluding the local
    // rank; channel argument i maps to peerChannels[rank][i].
    std::vector<std::vector<uint32_t>> peerRanks;         // [local rank][peer slot] -> global rank
    std::vector<std::vector<ChannelHandle>> peerChannels; // [local rank][peer slot]
    bool channelsAcquired = false;
    bool borrowedChannels = false;
    std::unique_ptr<CcuAotKernelState> kernelState = std::make_unique<CcuAotKernelState>();
};

namespace {

// Values carried in HComm's instance snapshot range table.
enum CcuAotHcommResourceType : uint32_t {
    kHcommCcuResTypeLoop = 0,
    kHcommCcuResTypeBlockLoop = 1,
    kHcommCcuResTypeMs = 2,
    kHcommCcuResTypeBlockMs = 3,
    kHcommCcuResTypeCke = 4,
    kHcommCcuResTypeBlockCke = 5,
    kHcommCcuResTypeBlockXn = 6,
    kHcommCcuResTypeXn = 7,
    kHcommCcuResTypeGsa = 8,
    kHcommCcuResTypeBlockGsa = 9,
    kHcommCcuResTypeMission = 10,
};

static constexpr uint32_t kCcuAotMaxDieNum = 2;
static constexpr uint32_t kCcuAotChannelResourceCapacity = 16;

struct HcommRecoveryResult {
    bool attempted = false;
    int publicSuspendRc = -1;
    int publicResumeRc = -1;
    int rc = HCCL_E_INTERNAL;
    uint64_t elapsedMs = 0;
};

// The POD deployment exposes the recovery contract through public HComm/ACL
// APIs. A failed CCU mission is recovered through the communicator's public
// resume contract after the bounded device abort.
static HcommRecoveryResult recoverHcommCcuAbort(CcuAotContext* ctx, uint32_t rank)
{
    HcommRecoveryResult result;
    if (!ctx || rank >= ctx->ndev || rank >= ctx->devices.size() || rank >= ctx->comms.size() || !ctx->comms[rank] ||
        !HcclCommSuspend || !HcclCommResume)
        return result;

    result.attempted = true;
    const auto start = std::chrono::steady_clock::now();
    result.publicSuspendRc = runOnFreshDeviceThread(
        ctx->devices[rank], static_cast<int>(HCCL_E_INTERNAL), [&]() { return HcclCommSuspend(ctx->comms[rank]); });
    const bool suspendUnsupported = result.publicSuspendRc == HCCL_E_NOT_SUPPORT;
    if (result.publicSuspendRc != HCCL_SUCCESS && result.publicSuspendRc != HCCL_E_SUSPENDING && !suspendUnsupported) {
        result.rc = result.publicSuspendRc;
        result.elapsedMs = elapsedMs(start);
        return result;
    }

    // HComm's public contract requires Resume after aclrtDeviceTaskAbort,
    // including deployments where Suspend is intentionally unsupported.
    result.publicResumeRc = runOnFreshDeviceThread(
        ctx->devices[rank], static_cast<int>(HCCL_E_INTERNAL), [&]() { return HcclCommResume(ctx->comms[rank]); });
    result.rc = result.publicResumeRc;
    result.elapsedMs = elapsedMs(start);
    return result;
}

static const CcuAotPod* getPod(const CcuAotContext* ctx, uint32_t rank)
{
    if (!ctx || rank >= ctx->pods.size())
        return nullptr;
    const auto& pod = ctx->pods[rank];
    return pod.instanceHandle != 0 ? &pod : nullptr;
}

static int queryXnBaseAddr(const CcuAotContext* ctx, uint32_t rank, uint8_t dieId, uint64_t* xnBaseAddr)
{
    if (!xnBaseAddr || dieId >= kCcuAotMaxDieNum)
        return -1;
    const auto* pod = getPod(ctx, rank);
    if (!pod || !(pod->validDieMask & (1u << dieId)) || !pod->die[dieId].enabled)
        return -1;
    *xnBaseAddr = pod->die[dieId].xnBaseAddr;
    return *xnBaseAddr != 0 ? 0 : -1;
}

// ============================================================================
// Microcode relocation
// ============================================================================
//
// The AOT `.ccuobj` microcode references *pool-local 0-indexed* resource ids
// (the writer emits xn/gsa/cke/ms ids starting at 0 within each class — see
// lib/Writer/CcuobjWriter.cpp). hcomm, however, allocates each pool as a
// contiguous *physical* segment whose base id is generally non-zero (observed
// on hardware: xn@144, gsa@48, cke@224). Overriding the translated microcode
// with our bytes verbatim therefore makes every instruction read/write the
// wrong physical register → CCU executes garbage → launch timeout.
//
// So before the override we add each pool's physical base to the id-typed
// fields. We dispatch on the instruction opcode and rebase *only* the fields
// the writer populates with a resolved pool id; sentinel/unused fields (which
// the writer leaves at 0) and launch-time-patched fields (channelId / notify
// physical lookups / loopback channel) are left untouched.
//
// Objects with an unsupported ABI are rejected before relocation.

struct ResBase : CcuAotResourceBase {
    uint32_t blockLoopEngine = 0;
    // B.2: per-channel physical IDs (indexed by channel formal arg index)
    std::vector<HcommCcuChannelEntity> channelPods; // [channelArgIdx] -> POD snapshot
    uint32_t loopChannelId = 0;                     // self-loopback channel for local reduce
    bool hasLoopChannelId = false;
    uint32_t interDieLoopChannelId = 0; // src die -> peer die loop channel
    bool hasInterDieLoopChannelId = false;
};

// First non-empty physical segment across dies (v1 = single die, one segment
// per pool).

static bool getPodResourceRange(
    const CcuAotPod* pod, uint32_t resourceType, uint32_t dieId, uint32_t& startId, uint32_t& count)
{
    if (!pod)
        return false;
    for (const auto& range : pod->ranges) {
        if (range.resourceType != resourceType || range.dieId != dieId || range.count == 0)
            continue;
        startId = range.startId;
        count = range.count;
        return true;
    }
    return false;
}

static bool validPod(const CcuAotPod* pod)
{
    if (!pod) {
        CCU_AOT_LOGE("AOT POD materializer: instance entity is null\n");
        return false;
    }
    if (pod->dieNum == 0 || pod->dieNum > kCcuAotMaxDieNum) {
        CCU_AOT_LOGE("AOT POD materializer: invalid dieNum=%u\n", pod->dieNum);
        return false;
    }
    return true;
}

static uint32_t choosePodDie(const CcuAotPod* pod, int preferredDie)
{
    if (preferredDie >= 0 && static_cast<uint32_t>(preferredDie) < pod->dieNum &&
        (pod->validDieMask & (1u << preferredDie)) && pod->die[preferredDie].enabled)
        return static_cast<uint32_t>(preferredDie);

    for (uint32_t candidate = 0; candidate < pod->dieNum; ++candidate) {
        if ((pod->validDieMask & (1u << candidate)) && pod->die[candidate].enabled)
            return candidate;
    }
    return UINT32_MAX;
}

static bool fillPodResourceBase(
    const CcuAotPod* pod, uint32_t die, uint32_t blockType, uint32_t plainType, uint32_t demand, uint32_t& base,
    const char* name, bool requireRange = true)
{
    uint32_t start = 0;
    uint32_t count = 0;
    if (!getPodResourceRange(pod, blockType, die, start, count) &&
        !getPodResourceRange(pod, plainType, die, start, count)) {
        if (demand != 0 || requireRange) {
            CCU_AOT_LOGE("AOT POD materializer: die=%u %s range missing demand=%u\n", die, name, demand);
            return false;
        }
        return true;
    }
    if (demand > count) {
        CCU_AOT_LOGE("AOT POD materializer: die=%u %s demand=%u range=%u+%u\n", die, name, demand, start, count);
        return false;
    }
    base = start;
    return true;
}

static bool fillMsResourceBase(const CcuAotPod* pod, uint32_t die, uint32_t demand, uint32_t& encoded)
{
    uint32_t start = 0;
    uint32_t count = 0;
    const bool hasRange = getPodResourceRange(pod, kHcommCcuResTypeBlockMs, die, start, count) ||
                          getPodResourceRange(pod, kHcommCcuResTypeMs, die, start, count);
    CCU_AOT_LOGI(
        "AOT MS resource check: die=%u demand=%u range=%s start=%u count=%u\n", die, demand,
        hasRange ? "present" : "missing", hasRange ? start : 0, hasRange ? count : 0);
    if (!hasRange) {
        if (demand != 0) {
            CCU_AOT_LOGE("AOT POD materializer: die=%u MS range missing demand=%u\n", die, demand);
            return false;
        }
        return true;
    }
    if (demand > count) {
        CCU_AOT_LOGE("AOT POD materializer: die=%u MS demand=%u range=%u+%u\n", die, demand, start, count);
        return false;
    }
    if (!ccu::runtime::encodeCcuBufId(start, die, encoded)) {
        CCU_AOT_LOGE("AOT POD materializer: invalid MS range die=%u start=%u\n", die, start);
        return false;
    }
    return true;
}

static bool fillResBaseFromPod(
    const CcuAotPod* pod, const CcuobjParsed& parsed, ResBase& resBase, int preferredDie = -1)
{
    if (!validPod(pod))
        return false;

    const uint32_t die = choosePodDie(pod, preferredDie);
    if (die == UINT32_MAX) {
        CCU_AOT_LOGE("AOT POD materializer: no enabled die validMask=0x%x\n", pod->validDieMask);
        return false;
    }
    CCU_AOT_LOGI(
        "AOT POD die selection: preferred=%d selected=%u validMask=0x%x "
        "dieNum=%u\n",
        preferredDie, die, pod->validDieMask, pod->dieNum);

    resBase.die = static_cast<int>(die);
    const uint32_t xnDemand = parsed.varCount + parsed.localAddrCount + parsed.remoteAddrCount + parsed.taskArgCount;
    if (!fillPodResourceBase(pod, die, kHcommCcuResTypeBlockXn, kHcommCcuResTypeXn, xnDemand, resBase.xn, "XN"))
        return false;
    if (!fillPodResourceBase(
            pod, die, kHcommCcuResTypeBlockGsa, kHcommCcuResTypeGsa, parsed.localAddrCount + parsed.remoteAddrCount,
            resBase.gsa, "GSA"))
        return false;
    if (!fillPodResourceBase(
            pod, die, kHcommCcuResTypeBlockCke, kHcommCcuResTypeCke, parsed.eventCount, resBase.cke, "CKE"))
        return false;
    if (!fillMsResourceBase(pod, die, parsed.msCount, resBase.ms))
        return false;
    return fillPodResourceBase(
        pod, die, kHcommCcuResTypeBlockLoop, kHcommCcuResTypeLoop, parsed.executorCount, resBase.blockLoopEngine,
        "loop");
}

static bool materializeAotResourceBase(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, CcuAotResourceBase& out);

// die index of the first non-empty segment (-1 if none). The kernel/mission
// runs on the die where hcomm allocated its resources; microcode must be
// loaded into THAT die's instruction RAM, else the mission executes the die's
// original (empty) microcode and our override is a no-op.

static bool relocsNeedPool(const std::vector<ccu::ccuobj::RelocEntry>& relocs, ccu::ccuobj::RelocPool pool)
{
    for (const auto& r : relocs) {
        if (static_cast<ccu::ccuobj::RelocKind>(r.kind) == ccu::ccuobj::RelocKind::Physical &&
            static_cast<ccu::ccuobj::RelocPool>(r.pool) == pool) {
            return true;
        }
    }
    return false;
}

static bool isLoopChannelReloc(const ccu::ccuobj::RelocEntry& r)
{
    return static_cast<ccu::ccuobj::RelocKind>(r.kind) == ccu::ccuobj::RelocKind::Physical &&
           static_cast<ccu::ccuobj::RelocPool>(r.pool) == ccu::ccuobj::RelocPool::LoopChannel;
}

// Opcode constants mirrored in include/ccu/Writer/CcuMicrocodeCodes.h.
namespace code {
enum : uint16_t { Trans = 0x2 };
namespace trans {
enum : uint16_t { SyncCKE = 0xb };
}
} // namespace code

static constexpr size_t kCcuInstrBytes = 32;
static constexpr unsigned kInstructionOpcodeShift = 11;
static constexpr uint16_t kInstructionOpcodeMask = 0xfu;
static constexpr uint16_t kInstructionCodeMask = 0x7ffu;
static constexpr size_t kMaxU16FieldOffset = 30;
static constexpr uint32_t kMinWorldSize = 2;
static constexpr size_t kMaxResourceTypeCount = 32;
static constexpr uint32_t kXnPerSize = 8;
static constexpr uint64_t kResourceTokenFlagBit = 52;
static constexpr uint64_t kTokenIdShift = 32;
static constexpr uint64_t kTokenIdMask = 0xFFFFF;
static constexpr uint64_t kTokenValueMask = 0xFFFFFFFF;
static constexpr int kMaxDeviceAbortAttempts = 2;

static uint16_t instructionHeader(const uint8_t* instrBytes)
{
    uint16_t header = 0;
    std::copy(
        static_cast<const uint8_t*>(instrBytes), static_cast<const uint8_t*>(instrBytes) + sizeof(header),
        reinterpret_cast<uint8_t*>(&header));
    return header;
}

static bool isTransSyncCkeInstr(const uint8_t* instrBytes)
{
    const uint16_t header = instructionHeader(instrBytes);
    return ((header >> kInstructionOpcodeShift) & kInstructionOpcodeMask) == code::Trans &&
           (header & kInstructionCodeMask) == code::trans::SyncCKE;
}

static bool relocsNeedInterDieLoopChannel(const CcuobjParsed& parsed)
{
    if (!parsed.microcode)
        return false;
    for (const auto& r : parsed.relocs) {
        if (!isLoopChannelReloc(r) || r.instrIdx >= parsed.instrCount)
            continue;
        if (isTransSyncCkeInstr(parsed.microcode + static_cast<size_t>(r.instrIdx) * kCcuInstrBytes))
            return true;
    }
    return false;
}

// Table-driven channel/notify relocation. The shared applier resolves
// launch-time physical values through these callbacks.
struct ExternalRelocCtx {
    struct ResolvedArg {
        uint32_t kind = 0;
        std::vector<uint32_t> ids;
    };
    const ResolvedArg* args = nullptr;
    uint32_t count = 0;
};

// channelId field: writer uses 1-based channel numbering (0 = event mode / no
// channel). Out-of-range or 0 → return input unchanged (no-op).
static uint16_t cnChannel(void* ctx, uint16_t argIdx1)
{
    const ResBase& base = *static_cast<const ResBase*>(ctx);
    if (argIdx1 > 0 && static_cast<size_t>(argIdx1 - 1) < base.channelPods.size())
        return static_cast<uint16_t>(base.channelPods[argIdx1 - 1].channelId);
    return argIdx1;
}

static const HcommCcuChannelEntity* cnChannelPod(const ResBase& base, uint16_t channelArg)
{
    if (channelArg == 0)
        return base.channelPods.size() == 1 ? &base.channelPods[0] : nullptr;
    const size_t idx = static_cast<size_t>(channelArg - 1);
    return idx < base.channelPods.size() ? &base.channelPods[idx] : nullptr;
}

static uint16_t cnNotifyRmtCke(void* ctx, uint16_t channelArg, uint16_t logIdx)
{
    const ResBase& base = *static_cast<const ResBase*>(ctx);
    const auto* pod = cnChannelPod(base, channelArg);
    if (!pod || logIdx >= kCcuAotChannelResourceCapacity)
        return logIdx;
    return static_cast<uint16_t>(pod->remoteEventIds[logIdx]);
}

static uint16_t cnChannelRmtXn(void* ctx, uint16_t channelArg, uint16_t logIdx)
{
    const ResBase& base = *static_cast<const ResBase*>(ctx);
    const auto* pod = cnChannelPod(base, channelArg);
    if (!pod || logIdx >= kCcuAotChannelResourceCapacity)
        return logIdx;
    return static_cast<uint16_t>(pod->remoteVarIds[logIdx]);
}

static uint16_t cnNotifyLocCke(void* ctx, uint16_t channelArg, uint16_t logIdx)
{
    const ResBase& base = *static_cast<const ResBase*>(ctx);
    const auto* pod = cnChannelPod(base, channelArg);
    if (!pod || logIdx >= kCcuAotChannelResourceCapacity)
        return logIdx;
    return static_cast<uint16_t>(pod->localEventIds[logIdx]);
}

static bool applyChannelXnRelocs(
    std::vector<uint8_t>& instrBytes, const CcuobjParsed& parsed, const ResBase& base, uint32_t rank)
{
    for (const auto& reloc : parsed.relocs) {
        if (static_cast<ccu::ccuobj::RelocPool>(reloc.pool) != ccu::ccuobj::RelocPool::ChannelXn)
            continue;
        if (reloc.kind != static_cast<uint8_t>(ccu::ccuobj::RelocKind::Physical) || reloc.channelArg == 0 ||
            reloc.channelArg > base.channelPods.size() || reloc.reserved >= kCcuAotChannelResourceCapacity ||
            reloc.instrIdx >= parsed.instrCount || reloc.byteOff > kMaxU16FieldOffset) {
            CCU_AOT_LOGE("rank=%u kernel[%u] invalid channel-Xn relocation\n", rank, parsed.kernelIndex);
            return false;
        }
        const HcommCcuChannelEntity& pod = base.channelPods[reloc.channelArg - 1];
        if (pod.localVarIds[reloc.reserved] > UINT16_MAX) {
            CCU_AOT_LOGE("rank=%u kernel[%u] channel-Xn id exceeds instruction field\n", rank, parsed.kernelIndex);
            return false;
        }
        auto* field = reinterpret_cast<uint16_t*>(
            instrBytes.data() + static_cast<size_t>(reloc.instrIdx) * kCcuInstrBytes + reloc.byteOff);
        *field = static_cast<uint16_t>(pod.localVarIds[reloc.reserved]);
    }
    return true;
}

static uint16_t externalId(void* ctx, uint32_t kind, uint16_t slot)
{
    const auto* external = static_cast<ExternalRelocCtx*>(ctx);
    if (!external || !external->args)
        return slot;
    for (uint32_t i = 0; i < external->count; ++i) {
        const ExternalRelocCtx::ResolvedArg& arg = external->args[i];
        if (arg.kind != kind || slot >= arg.ids.size())
            continue;
        return static_cast<uint16_t>(arg.ids[slot]);
    }
    return slot;
}

static uint16_t externalXn(void* ctx, uint16_t slot) { return externalId(ctx, CCU_EXTERNAL_ARG_XN, slot); }

static uint16_t externalCke(void* ctx, uint16_t slot) { return externalId(ctx, CCU_EXTERNAL_ARG_EVENT, slot); }

static bool applyLoopChannelRelocs(
    std::vector<uint8_t>& instrBytes, const CcuobjParsed& parsed, const ResBase& base, uint32_t rank)
{
    for (const auto& r : parsed.relocs) {
        if (!isLoopChannelReloc(r))
            continue;
        if (r.byteOff > kMaxU16FieldOffset || r.instrIdx >= parsed.instrCount)
            continue;

        const bool interDie = isTransSyncCkeInstr(instrBytes.data() + static_cast<size_t>(r.instrIdx) * kCcuInstrBytes);
        if (interDie) {
            if (!base.hasInterDieLoopChannelId) {
                CCU_AOT_LOGI(
                    "rank=%u kernel[%u] LoopChannel inter-die relocation missing id\n", rank, parsed.kernelIndex);
                return false;
            }
        } else if (!base.hasLoopChannelId) {
            CCU_AOT_LOGI("rank=%u kernel[%u] LoopChannel self relocation missing id\n", rank, parsed.kernelIndex);
            return false;
        }

        auto* f = reinterpret_cast<uint16_t*>(
            instrBytes.data() + static_cast<size_t>(r.instrIdx) * kCcuInstrBytes + r.byteOff);
        *f = static_cast<uint16_t>(interDie ? base.interDieLoopChannelId : base.loopChannelId);
        CCU_AOT_LOGI(
            "rank=%u kernel[%u] loopChannel instr=%u %s id=%u\n", rank, parsed.kernelIndex, r.instrIdx,
            interDie ? "inter-die" : "self", interDie ? base.interDieLoopChannelId : base.loopChannelId);
    }
    return true;
}

} // anonymous namespace

// ============================================================================
// C API
// ============================================================================

extern "C" {
static bool ccuAotQueryCommTopology(
    const CcuAotContext* ctx, uint32_t localRank, uint32_t& globalRank, uint32_t& totalRanks)
{
    if (!ctx || localRank >= ctx->comms.size() || !ctx->comms[localRank])
        return false;
    HcclComm comm = ctx->comms[localRank];
    return HcclGetRankId(comm, &globalRank) == HCCL_SUCCESS && HcclGetRankSize(comm, &totalRanks) == HCCL_SUCCESS &&
           totalRanks != 0 && globalRank < totalRanks;
}

static uint32_t ccuAotTotalRanks(const CcuAotContext* ctx)
{
    if (!ctx)
        return 0;
    uint32_t globalRank = 0;
    uint32_t totalRanks = 0;
    if (ccuAotQueryCommTopology(ctx, 0, globalRank, totalRanks))
        return totalRanks;
    return ctx->rankBinding.worldSize;
}

static uint32_t ccuAotGlobalRank(const CcuAotContext* ctx, uint32_t localRank)
{
    // In one-rank-per-process attach/cluster mode, the explicit rank passed by
    // the caller is the authoritative global rank.  HcclGetRankId() may
    // report the communicator's local rank (zero in a single-comm process),
    // which would collapse rank 1 onto rank 0 while building peer tables and
    // while selecting channel diagnostics.
    if (!ctx || localRank >= ctx->ndev)
        return localRank;
    return ctx->rankBinding.globalRank;
}

static std::vector<uint32_t> ccuAotBuildPeerRanks(const CcuAotContext* ctx, uint32_t localRank)
{
    std::vector<uint32_t> peers;
    if (!ctx || localRank >= ctx->ndev)
        return peers;
    const uint32_t total = ccuAotTotalRanks(ctx);
    const uint32_t self = ccuAotGlobalRank(ctx, localRank);
    if (self >= total)
        return peers;
    peers.reserve(total > 0 ? total - 1 : 0);
    for (uint32_t r = 0; r < total; ++r) {
        if (r != self)
            peers.push_back(r);
    }
    return peers;
}

static void ccuAotInitPeerTables(CcuAotContext* ctx)
{
    if (!ctx)
        return;
    ctx->peerRanks.assign(ctx->ndev, {});
    ctx->peerChannels.assign(ctx->ndev, {});
    for (uint32_t r = 0; r < ctx->ndev; ++r) {
        ctx->peerRanks[r] = ccuAotBuildPeerRanks(ctx, r);
        ctx->peerChannels[r].assign(ctx->peerRanks[r].size(), 0);
    }
}

const char* ccu_aot_strerror(CcuAotError err)
{
    switch (err) {
        case CCU_AOT_SUCCESS:
            return "success";
        case CCU_AOT_E_INIT:
            return "init failed";
        case CCU_AOT_E_REGISTER:
            return "register failed";
        case CCU_AOT_E_LAUNCH:
            return "launch failed";
        case CCU_AOT_E_PARAM:
            return "invalid parameter";
        case CCU_AOT_E_NOT_SUBMITTED:
            return "launch rejected before submission";
    }
    return "unknown";
}

static CcuAotError ccuAotAttach(CcuAotContext** outCtx, void* comm, int32_t device, uint32_t rank, uint32_t worldSize)
{
    if (!outCtx || !comm || worldSize == 0 || rank >= worldSize)
        return CCU_AOT_E_PARAM;

    auto* ctx = new CcuAotContext();
    ctx->ndev = 1; // 本进程只持有一个 rank 的 comm
    ctx->rankBinding = {rank, worldSize};
    ctx->devices.assign(1, device);
    ctx->comms.assign(1, reinterpret_cast<HcclComm>(comm)); // 直接用用户 comm

    // ACL 生命周期归用户（attach 前用户已 aclInit + 建 comm）；只 SetDevice 保证落在
    // comm 创建时的同一张卡。不 aclInit（重复 init 会报错）。
    aclError aclRc = aclrtSetDevice(device);
    if (aclRc != 0) {
        CCU_AOT_LOGE("attach aclrtSetDevice(%d) failed: %d\n", device, (int)aclRc);
        delete ctx;
        return CCU_AOT_E_INIT;
    }

    // The user communicator owns the user stream and the HComm thread
    // associated with it.  Do not create a second adapter-owned stream/thread
    // here: HcclThreadAcquireWithStream must be paired by the communicator's
    // lifecycle, not by HcommThreadFree (the two handle maps are distinct).
    // Build peer topology from the attached communicator rather than leaving
    // an empty peerRanks[0] slot for the HComm backend launch path.
    ccuAotInitPeerTables(ctx);
    CCU_AOT_LOGI(
        "Attach OK (user comm): rank=%u/%u device=%d comm=%p; "
        "using caller stream/thread\n",
        rank, worldSize, device, comm);
    *outCtx = ctx;
    return CCU_AOT_SUCCESS;
}

CcuAotError ccu_aot_attach_current(CcuAotContext** outCtx, void* comm, uint32_t* rank, uint32_t* worldSize)
{
    if (!outCtx || !comm || !rank || !worldSize)
        return CCU_AOT_E_PARAM;
    *outCtx = nullptr;
    *rank = 0;
    *worldSize = 0;

    int32_t device = -1;
    const aclError deviceRc = aclrtGetDevice(&device);
    if (deviceRc != ACL_SUCCESS || device < 0) {
        CCU_AOT_LOGE("attach aclrtGetDevice failed: rc=%d device=%d\n", static_cast<int>(deviceRc), device);
        return CCU_AOT_E_INIT;
    }
    HcclComm hcclComm = reinterpret_cast<HcclComm>(comm);
    const HcclResult rankRc = HcclGetRankId(hcclComm, rank);
    const HcclResult sizeRc = HcclGetRankSize(hcclComm, worldSize);
    if (rankRc != HCCL_SUCCESS || sizeRc != HCCL_SUCCESS || *worldSize == 0 || *rank >= *worldSize) {
        CCU_AOT_LOGE(
            "attach communicator topology failed: rank-rc=%d "
            "size-rc=%d rank=%u world=%u comm=%p\n",
            static_cast<int>(rankRc), static_cast<int>(sizeRc), *rank, *worldSize, comm);
        return CCU_AOT_E_INIT;
    }
    return ccuAotAttach(outCtx, comm, device, *rank, *worldSize);
}

} // extern "C"

namespace {

static bool validInstanceEntity(const HcommCcuInstanceEntity* entity, uint32_t rank)
{
    if (entity->header.version != HCOMM_CCU_INSTANCE_ENTITY_ABI_VERSION ||
        entity->header.magicWord != HCOMM_CCU_INSTANCE_ENTITY_MAGIC_WORD ||
        entity->header.size != sizeof(HcommCcuInstanceEntity) || entity->header.reserved != 0 ||
        entity->ccuInsKey == 0 || entity->ccuDieNum == 0 || entity->ccuDieNum > kCcuAotMaxDieNum) {
        CCU_AOT_LOGE("rank=%u invalid HComm instance entity\n", rank);
        return false;
    }
    return true;
}

static bool validDieMetadata(const HcommCcuDieMetadata* metadata)
{
    return metadata && metadata->header.version == HCOMM_CCU_RES_ABI_VERSION &&
           metadata->header.magicWord == HCOMM_CCU_DIE_METADATA_MAGIC_WORD &&
           metadata->header.size == sizeof(HcommCcuDieMetadata) && metadata->header.reserved == 0 &&
           metadata->enabled != 0;
}

static void copyDieMetadata(CcuAotPod& normalized, uint32_t die, const HcommCcuDieMetadata& metadata)
{
    normalized.validDieMask |= 1u << die;
    normalized.die[die].enabled = true;
    normalized.die[die].missionKey = metadata.missionKey;
    normalized.die[die].loopChannelId = metadata.loopChannelId;
    normalized.die[die].xnBaseAddr = metadata.ccuResBuffer.bufferInfo.rma.addr;
    normalized.die[die].resourceSpaceTokenId = metadata.ccuResBuffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    normalized.die[die].resourceSpaceTokenValue =
        metadata.ccuResBuffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
}

static void copyResourceRanges(CcuAotPod& normalized, uint32_t die, const HcommCcuDieRes& sourceDie)
{
    for (uint32_t type = 0; type < kMaxResourceTypeCount; ++type) {
        const auto& pack = sourceDie.resTypes[type];
        if (pack.resourceType != type || pack.resRanges == nullptr)
            continue;
        for (uint32_t index = 0; index < pack.resRangeNum; ++index) {
            const auto& range = pack.resRanges[index];
            if (range.count == 0 || range.reserved != 0)
                continue;
            normalized.ranges.push_back({pack.resourceType, die, range.startId, range.count});
        }
    }
}

static void copyControlSlots(CcuAotPod& normalized, const HcommCcuInstanceEntity& entity)
{
    normalized.channelQuery =
        reinterpret_cast<HcommCcuChannelGetEntityCallBack>(entity.ascCustom[kHcommCcuControlSlotChannelEntity]);
    normalized.allocInstruction =
        reinterpret_cast<HcommCcuInstSpaceAllocCallBack>(entity.ascCustom[kHcommCcuControlSlotAllocInstruction]);
    normalized.submitInstructions =
        reinterpret_cast<HcommCcuInstSpaceSubmitCallBack>(entity.ascCustom[kHcommCcuControlSlotSubmitInstructions]);
}

static CcuAotError normalizeInstanceEntity(uint32_t rank, const HcommCcuInstanceEntity& entity, CcuAotPod& normalized)
{
    normalized.instanceHandle = entity.ccuInsKey;
    normalized.dieNum = entity.ccuDieNum;
    for (uint32_t die = 0; die < entity.ccuDieNum; ++die) {
        const auto& sourceDie = entity.ccuDieReses[die];
        if (!validDieMetadata(sourceDie.dieMetadata))
            continue;
        copyDieMetadata(normalized, die, *sourceDie.dieMetadata);
        copyResourceRanges(normalized, die, sourceDie);
    }
    if (normalized.validDieMask == 0) {
        CCU_AOT_LOGE("rank=%u HComm instance entity has no enabled die\n", rank);
        return CCU_AOT_E_REGISTER;
    }
    copyControlSlots(normalized, entity);
    if (!normalized.channelQuery || !normalized.allocInstruction || !normalized.submitInstructions) {
        CCU_AOT_LOGE("rank=%u HComm instance entity control slots incomplete\n", rank);
        return CCU_AOT_E_REGISTER;
    }
    return CCU_AOT_SUCCESS;
}

static void selectInstanceHandle(CcuAotContext* ctx, uint32_t rank, CcuInsHandle& insHandle)
{
    const bool hasCallerInstance = rank < ctx->callerInstanceHandles.size() && ctx->callerInstanceHandles[rank] != 0;
    insHandle = hasCallerInstance ? ctx->callerInstanceHandles[rank] : ctx->pods[rank].instanceHandle;
    if (hasCallerInstance && ctx->pods[rank].instanceHandle != insHandle) {
        CCU_AOT_LOGI(
            "rank=%u using caller-owned HComm instance=0x%lx; "
            "POD instance=0x%lx is metadata-only\n",
            rank, static_cast<unsigned long>(insHandle), static_cast<unsigned long>(ctx->pods[rank].instanceHandle));
    }
}

static CcuAotError queryCcuInsHandle(CcuAotContext* ctx, uint32_t rank, CcuInsHandle* insHandle)
{
    if (!ctx || rank >= ctx->ndev || !insHandle)
        return CCU_AOT_E_PARAM;
    *insHandle = 0;
    if (ctx->pods.size() <= rank)
        ctx->pods.resize(ctx->ndev);

    alignas(HcommCcuInstanceEntity) uint8_t queryStorage[sizeof(HcommCcuInstanceEntity)]{};
    CcuInsHandle query = reinterpret_cast<CcuInsHandle>(queryStorage);
    uint32_t entityCount = 0;
    const HcclResult queryRc = HcclCommQueryCcuIns(ctx->comms[rank], &query, &entityCount);
    if (queryRc != HCCL_SUCCESS || entityCount != 1 || query == 0) {
        CCU_AOT_LOGE(
            "rank=%u HcclCommQueryCcuIns(entity) failed: rc=%d count=%u\n", rank, static_cast<int>(queryRc),
            entityCount);
        return CCU_AOT_E_REGISTER;
    }

    const auto* entity = reinterpret_cast<const HcommCcuInstanceEntity*>(query);
    if (!validInstanceEntity(entity, rank))
        return CCU_AOT_E_REGISTER;

    CcuAotPod normalized{};
    const CcuAotError normalizeRc = normalizeInstanceEntity(rank, *entity, normalized);
    if (normalizeRc != CCU_AOT_SUCCESS)
        return normalizeRc;
    ctx->pods[rank] = std::move(normalized);
    selectInstanceHandle(ctx, rank, *insHandle);
    return CCU_AOT_SUCCESS;
}

static bool queryChannelEntity(
    CcuAotContext* ctx, uint32_t rank, ChannelHandle channel, HcommCcuChannelEntity& channelPod)
{
    if (!ctx || rank >= ctx->pods.size() || channel == 0)
        return false;
    const auto* pod = getPod(ctx, rank);
    if (!pod || !pod->channelQuery)
        return false;
    auto* channelBytes = reinterpret_cast<uint8_t*>(&channelPod);
    std::fill(channelBytes, channelBytes + sizeof(channelPod), 0);
    const int32_t queryRc = pod->channelQuery(channel, &channelPod);
    if (queryRc != 0) {
        CCU_AOT_LOGE(
            "rank=%u HComm channel entity query failed: handle=0x%lx rc=%d\n", rank,
            static_cast<unsigned long>(channel), queryRc);
        return false;
    }
    return channelPod.header.version == HCOMM_CCU_CHANNEL_ABI_VERSION &&
           channelPod.header.magicWord == HCOMM_CCU_CHANNEL_MAGIC_WORD &&
           channelPod.header.size == sizeof(HcommCcuChannelEntity) && channelPod.header.reserved == 0;
}

static bool queryChannelDie(CcuAotContext* ctx, uint32_t rank, ChannelHandle channel, uint32_t& dieId)
{
    HcommCcuChannelEntity channelPod{};
    if (!queryChannelEntity(ctx, rank, channel, channelPod))
        return false;
    dieId = channelPod.dieId;
    return dieId < kCcuAotMaxDieNum;
}

static uint8_t chooseExternalResourceDie(CcuAotContext* ctx, uint32_t rank)
{
    uint32_t die = UINT32_MAX;
    if (rank < ctx->peerChannels.size()) {
        for (ChannelHandle channel : ctx->peerChannels[rank]) {
            if (queryChannelDie(ctx, rank, channel, die))
                return static_cast<uint8_t>(die);
        }
    }
    /* External resources may be allocated before the caller-owned channel
     * handles are passed through the launch schedule.  In that phase the
     * instance snapshot is the only authoritative die information; do not
     * fall back to die 0 when the POD exposes another valid die. */
    if (rank < ctx->pods.size()) {
        const auto& pod = ctx->pods[rank];
        for (uint32_t candidate = 0; candidate < pod.dieNum; ++candidate) {
            if ((pod.validDieMask & (1u << candidate)) != 0)
                return static_cast<uint8_t>(candidate);
        }
    }
    return 0;
}

static int chooseChannelResourceDie(CcuAotContext* ctx, uint32_t rank)
{
    if (!ctx)
        return -1;
    uint32_t die = UINT32_MAX;
    if (rank < ctx->peerChannels.size()) {
        for (ChannelHandle channel : ctx->peerChannels[rank]) {
            if (queryChannelDie(ctx, rank, channel, die))
                return static_cast<int>(die);
        }
    }
    return -1;
}

static CcuAotError allocExternalEventVaImpl(
    CcuAotContext* ctx, uint32_t rank, uint32_t count, uint64_t* handle, uint64_t* vas)
{
    if (!ctx || rank >= ctx->ndev || count == 0 || !handle || *handle != 0 || !vas)
        return CCU_AOT_E_PARAM;
    aclrtSetDevice(ctx->devices[rank]);
    CcuInsHandle insHandle = 0;
    CcuAotError qrc = queryCcuInsHandle(ctx, rank, &insHandle);
    if (qrc != CCU_AOT_SUCCESS)
        return qrc;
    const uint8_t dieId = chooseExternalResourceDie(ctx, rank);
    CcuEventHandle eventHandle = 0;
    CcuResult rc = HcommCcuEventAlloc(insHandle, dieId, count, &eventHandle);
    if (rc != CCU_SUCCESS || eventHandle == 0) {
        CCU_AOT_LOGE(
            "rank=%u external event alloc failed rc=%d count=%u die=%u\n", rank, static_cast<int>(rc), count, dieId);
        return CCU_AOT_E_REGISTER;
    }
    for (uint32_t i = 0; i < count; ++i) {
        rc = HcommCcuEventGetAddr(eventHandle, i, &vas[i]);
        if (rc != CCU_SUCCESS || vas[i] == 0)
            return CCU_AOT_E_REGISTER;
    }
    *handle = static_cast<uint64_t>(eventHandle);
    return CCU_AOT_SUCCESS;
}

static CcuAotError allocExternalXnVaImpl(
    CcuAotContext* ctx, uint32_t rank, uint32_t count, uint64_t* handle, uint64_t* vas)
{
    if (!ctx || rank >= ctx->ndev || count == 0 || !handle || *handle != 0 || !vas)
        return CCU_AOT_E_PARAM;
    aclrtSetDevice(ctx->devices[rank]);
    CcuInsHandle insHandle = 0;
    CcuAotError qrc = queryCcuInsHandle(ctx, rank, &insHandle);
    if (qrc != CCU_AOT_SUCCESS)
        return qrc;
    const uint8_t dieId = chooseExternalResourceDie(ctx, rank);
    CcuVariableHandle variableHandle = 0;
    CcuResult rc = HcommCcuVariableAlloc(insHandle, dieId, count, &variableHandle);
    if (rc != CCU_SUCCESS || variableHandle == 0) {
        CCU_AOT_LOGE(
            "rank=%u external xn alloc failed rc=%d count=%u die=%u\n", rank, static_cast<int>(rc), count, dieId);
        return CCU_AOT_E_REGISTER;
    }
    for (uint32_t i = 0; i < count; ++i) {
        rc = HcommCcuVariableGetAddr(variableHandle, i, &vas[i]);
        if (rc != CCU_SUCCESS || vas[i] == 0)
            return CCU_AOT_E_REGISTER;
    }
    *handle = static_cast<uint64_t>(variableHandle);
    return CCU_AOT_SUCCESS;
}

extern "C" CcuAotError ccu_aot_bind_channels(CcuAotContext* ctx, const uint64_t* channels, uint32_t channelCount)
{
    if (!ctx || !channels || channelCount == 0 || ctx->ndev != 1 || ctx->rankBinding.worldSize < kMinWorldSize ||
        channelCount != ctx->rankBinding.worldSize - 1)
        return CCU_AOT_E_PARAM;
    if (ctx->channelsAcquired)
        return ctx->borrowedChannels ? CCU_AOT_SUCCESS : CCU_AOT_E_REGISTER;
    for (uint32_t i = 0; i < channelCount; ++i)
        if (channels[i] == 0)
            return CCU_AOT_E_PARAM;
    if (ctx->peerChannels.size() != 1 || ctx->peerRanks.size() != 1)
        return CCU_AOT_E_PARAM;
    ctx->peerChannels[0].assign(channels, channels + channelCount);
    ctx->borrowedChannels = true;
    ctx->channelsAcquired = true;
    CCU_AOT_LOGI("rank=%u bound %u caller-owned HComm channels\n", ctx->rankBinding.globalRank, channelCount);
    return CCU_AOT_SUCCESS;
}

static bool materializeAotResourceBase(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, CcuAotResourceBase& out)
{
    if (!ctx || rank >= ctx->ndev)
        return false;
    const auto* pod = getPod(ctx, rank);
    if (!pod)
        return false;

    ResBase materialized;
    const int preferredDie = chooseChannelResourceDie(ctx, rank);
    if (!fillResBaseFromPod(pod, parsed, materialized, preferredDie))
        return false;

    uint32_t missionStart = 0;
    uint32_t missionCount = 0;
    const bool hasMission = getPodResourceRange(
        pod, kHcommCcuResTypeMission, static_cast<uint32_t>(materialized.die), missionStart, missionCount);

    out = {};
    out.xn = materialized.xn;
    out.gsa = materialized.gsa;
    out.cke = materialized.cke;
    out.ms = materialized.ms;
    out.mission = hasMission ? missionStart : 0;
    // Instruction space is leased immediately before the bytes are submitted.
    // The snapshot only supplies the allocator callback.
    out.instr = 0;
    out.die = materialized.die;
    CCU_AOT_LOGI(
        "rank=%u kernel[%u] AOT resource base from HComm POD: die=%d "
        "xn=%u gsa=%u cke=%u ms=%u mission=%u instr=%u\n",
        ccuAotGlobalRank(ctx, rank), parsed.kernelIndex, out.die, out.xn, out.gsa, out.cke, out.ms, out.mission,
        out.instr);
    return true;
}

static void logPreservedMsRange(
    const CcuAotPod* pod, const CcuobjParsed& parsed, const ResBase& resBase, uint32_t logRank,
    bool preserveMaterializedBase)
{
    if (resBase.die < 0 || static_cast<uint32_t>(resBase.die) >= kCcuAotMaxDieNum)
        return;

    uint32_t msStart = 0;
    uint32_t msCount = 0;
    const bool hasMsRange =
        getPodResourceRange(pod, kHcommCcuResTypeBlockMs, static_cast<uint32_t>(resBase.die), msStart, msCount) ||
        getPodResourceRange(pod, kHcommCcuResTypeMs, static_cast<uint32_t>(resBase.die), msStart, msCount);
    CCU_AOT_LOGI(
        "rank=%u kernel[%u] AOT MS range: demand=%u range=%s start=%u "
        "count=%u base=%u preserved=%u\n",
        logRank, parsed.kernelIndex, parsed.msCount, hasMsRange ? "present" : "missing", hasMsRange ? msStart : 0,
        hasMsRange ? msCount : 0, resBase.ms, preserveMaterializedBase ? 1u : 0u);
}

static bool fillLoopChannels(const CcuAotPod* pod, const CcuobjParsed& parsed, ResBase& resBase, uint32_t logRank)
{
    const bool needLoopChannel = relocsNeedPool(parsed.relocs, ccu::ccuobj::RelocPool::LoopChannel);
    const bool needInterDieLoop = relocsNeedInterDieLoopChannel(parsed);
    if (!pod || resBase.die < 0 || static_cast<uint32_t>(resBase.die) >= kCcuAotMaxDieNum)
        return !needLoopChannel;

    const auto& dieMetadata = pod->die[resBase.die];
    const uint32_t loopId = dieMetadata.loopChannelId;
    if (loopId < 0xFFFFu) {
        resBase.loopChannelId = loopId;
        resBase.hasLoopChannelId = true;
    }
    CCU_AOT_LOGI(
        "rank=%u kernel[%u] loopChannel self die=%d id=%u\n", logRank, parsed.kernelIndex, resBase.die, loopId);
    if (needLoopChannel && !resBase.hasLoopChannelId) {
        CCU_AOT_LOGE("rank=%u kernel[%u] LoopChannel self relocation failed\n", logRank, parsed.kernelIndex);
        return false;
    }

    if (!needInterDieLoop)
        return true;
    const uint8_t srcDie = static_cast<uint8_t>(resBase.die);
    const uint8_t dstDie = static_cast<uint8_t>(srcDie == 0 ? 1 : 0);
    const uint32_t interId = pod->die[srcDie].loopChannelId;
    if (interId < 0xFFFFu) {
        resBase.interDieLoopChannelId = interId;
        resBase.hasInterDieLoopChannelId = true;
    }
    CCU_AOT_LOGI(
        "rank=%u kernel[%u] loopChannel inter %u->%u id=%u\n", logRank, parsed.kernelIndex, srcDie, dstDie, interId);
    return resBase.hasInterDieLoopChannelId;
}

static void appendChannelPod(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, ResBase& resBase, uint32_t channelIndex,
    uint32_t logRank)
{
    ChannelHandle channel = 0;
    if (rank < ctx->peerChannels.size() && channelIndex < ctx->peerChannels[rank].size())
        channel = ctx->peerChannels[rank][channelIndex];
    uint32_t peerRank = UINT32_MAX;
    if (rank < ctx->peerRanks.size() && channelIndex < ctx->peerRanks[rank].size())
        peerRank = ctx->peerRanks[rank][channelIndex];

    HcommCcuChannelEntity channelPod{};
    if (!queryChannelEntity(ctx, rank, channel, channelPod)) {
        resBase.channelPods.emplace_back();
        CCU_AOT_LOGE(
            "rank=%u kernel[%u] channel[%u] peerRank=%u channel lookup "
            "failed: handle=0x%lx rc=%d\n",
            logRank, parsed.kernelIndex, channelIndex, peerRank, static_cast<unsigned long>(channel), -1);
        return;
    }

    resBase.channelPods.push_back(channelPod);
    CCU_AOT_LOGI(
        "rank=%u kernel[%u] channel[%u] peerRank=%u handle=0x%lx -> "
        "physChannelId=%u\n",
        logRank, parsed.kernelIndex, channelIndex, peerRank, static_cast<unsigned long>(channel), channelPod.channelId);
    if (!getenv("CCU_AOT_TRACE_CHANNEL_POD"))
        return;
    CCU_AOT_LOGI(
        "rank=%u kernel[%u] channel[%u] pod die=%u "
        "localXn[0..3]=[%u,%u,%u,%u] "
        "remoteXn[0..3]=[%u,%u,%u,%u] "
        "localCke[0]=%u remoteCke[0]=%u\n",
        logRank, parsed.kernelIndex, channelIndex, channelPod.dieId, channelPod.localVarIds[0],
        channelPod.localVarIds[1], channelPod.localVarIds[2], channelPod.localVarIds[3], channelPod.remoteVarIds[0],
        channelPod.remoteVarIds[1], channelPod.remoteVarIds[2], channelPod.remoteVarIds[3], channelPod.localEventIds[0],
        channelPod.remoteEventIds[0]);
}

static void appendChannelPods(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, ResBase& resBase, uint32_t logRank)
{
    for (uint32_t index = 0; index < parsed.channelArgCount; ++index)
        appendChannelPod(ctx, rank, parsed, resBase, index, logRank);
}

static bool validChannelReloc(const CcuobjParsed& parsed, const ccu::ccuobj::RelocEntry& reloc, const ResBase& resBase)
{
    const auto pool = static_cast<ccu::ccuobj::RelocPool>(reloc.pool);
    const bool channelXn = pool == ccu::ccuobj::RelocPool::ChannelXn || pool == ccu::ccuobj::RelocPool::ChannelRmtXn;
    const bool notify = pool == ccu::ccuobj::RelocPool::NotifyCkeRmt || pool == ccu::ccuobj::RelocPool::NotifyCkeLoc;

    if (!channelXn && reloc.reserved != 0)
        return false;
    if (!channelXn && !notify)
        return reloc.channelArg == 0;
    if (channelXn)
        return reloc.channelArg != 0 && reloc.channelArg <= parsed.channelArgCount &&
               reloc.reserved < kCcuAotChannelResourceCapacity;
    if (pool == ccu::ccuobj::RelocPool::NotifyCkeRmt && reloc.channelArg == 0)
        return false;
    if (reloc.channelArg > parsed.channelArgCount)
        return false;
    return reloc.channelArg == 0 || cnChannelPod(resBase, reloc.channelArg);
}

static bool validateChannelRelocs(const CcuobjParsed& parsed, const ResBase& resBase)
{
    for (const auto& reloc : parsed.relocs) {
        if (!validChannelReloc(parsed, reloc, resBase))
            return false;
    }
    return true;
}

static bool fillResBase(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, ResBase& resBase,
    bool preserveMaterializedBase = false)
{
    const uint32_t logRank = ccuAotGlobalRank(ctx, rank);
    const auto* registerPod = getPod(ctx, rank);
    if (!registerPod)
        return false;

    logPreservedMsRange(registerPod, parsed, resBase, logRank, preserveMaterializedBase);
    if (!preserveMaterializedBase &&
        !fillResBaseFromPod(registerPod, parsed, resBase, chooseChannelResourceDie(ctx, rank))) {
        CCU_AOT_LOGE(
            "rank=%u kernel[%u] AOT manifest exceeds HComm POD resource "
            "range\n",
            logRank, parsed.kernelIndex);
        return false;
    }

    CCU_AOT_LOGI(
        "rank=%u kernel[%u] resource base from HComm POD: die=%d xn=%u "
        "gsa=%u cke=%u ms=%u loop=%u\n",
        logRank, parsed.kernelIndex, resBase.die, resBase.xn, resBase.gsa, resBase.cke, resBase.ms,
        resBase.blockLoopEngine);
    CCU_AOT_LOGI(
        "rank=%u kernel[%u] resource-base xn=%u gsa=%u cke=%u ms=%u "
        "blockLoopEngine=%u\n",
        logRank, parsed.kernelIndex, resBase.xn, resBase.gsa, resBase.cke, resBase.ms, resBase.blockLoopEngine);

    if (!fillLoopChannels(registerPod, parsed, resBase, logRank))
        return false;
    appendChannelPods(ctx, rank, parsed, resBase, logRank);
    return validateChannelRelocs(parsed, resBase);
}

static bool applyImmediateRelocsForKernel(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, const ResBase& resBase,
    std::vector<uint8_t>& instrBytes)
{
    bool hasImmReloc = false;
    for (const auto& r : parsed.relocs) {
        if (r.kind >= static_cast<uint8_t>(ccu::ccuobj::RelocKind::VarBackingAddr)) {
            hasImmReloc = true;
            break;
        }
    }
    if (!hasImmReloc)
        return true;

    ccu::ccuobj::ImmValues iv;
    iv.physXnBase = resBase.xn;
    iv.xnPerSize = kXnPerSize;
    uint8_t dieId = (resBase.die >= 0) ? static_cast<uint8_t>(resBase.die) : 0;
    bool immOk = false;
    try {
        uint64_t xnBase = 0;
        if (queryXnBaseAddr(ctx, rank, dieId, &xnBase) != 0)
            throw std::runtime_error("POD Xn metadata unavailable/failed");
        iv.xnBaseAddr = xnBase;
        const auto* pod = getPod(ctx, rank);
        if (!pod || dieId >= kCcuAotMaxDieNum || !(pod->validDieMask & (1u << dieId)))
            throw std::runtime_error("POD die metadata unavailable/failed");
        const uint64_t tokenId = pod->die[dieId].resourceSpaceTokenId;
        const uint64_t tokenValue = pod->die[dieId].resourceSpaceTokenValue;
        iv.ccuResSpaceToken = (static_cast<uint64_t>(1) << kResourceTokenFlagBit) |
                              ((tokenId & kTokenIdMask) << kTokenIdShift) | (tokenValue & kTokenValueMask);
        immOk = true;
    } catch (const std::exception& e) {
        CCU_AOT_LOGE(
            "rank=%u kernel[%u] ls-imm: per-device base/token query failed: %s\n", rank, parsed.kernelIndex, e.what());
    } catch (...) {
        CCU_AOT_LOGI("rank=%u kernel[%u] ls-imm: per-device base/token query threw\n", rank, parsed.kernelIndex);
    }

    if (!immOk) {
        CCU_AOT_LOGI("rank=%u kernel[%u] ls-imm: NOT filled\n", rank, parsed.kernelIndex);
        return false;
    }
    ccu::ccuobj::applyImmediateRelocs(
        instrBytes.data(), parsed.relocs.data(), parsed.relocs.size(), iv, parsed.instrCount);
    CCU_AOT_LOGI(
        "rank=%u kernel[%u] ls-imm: xnBase=0x%lx resTok=0x%lx memTok=0x%lx\n", rank, parsed.kernelIndex,
        (unsigned long)iv.xnBaseAddr, (unsigned long)iv.ccuResSpaceToken, (unsigned long)iv.memToken);
    return true;
}

static bool initializeOverrideResBase(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, CcuAotResourceBase* recordedBase, ResBase& resBase)
{
    CcuAotResourceBase actualBase = recordedBase ? *recordedBase : CcuAotResourceBase{};
    const bool hasActualRepository = recordedBase && actualBase.die >= 0;
    if (hasActualRepository) {
        resBase.xn = actualBase.xn;
        resBase.gsa = actualBase.gsa;
        resBase.cke = actualBase.cke;
        resBase.ms = actualBase.ms;
        resBase.die = actualBase.die;
        CCU_AOT_LOGI(
            "rank=%u kernel[%u] resource base from AOT resource record: "
            "die=%d xn=%u gsa=%u cke=%u ms=%u\n",
            ccuAotGlobalRank(ctx, rank), parsed.kernelIndex, resBase.die, resBase.xn, resBase.gsa, resBase.cke,
            resBase.ms);
        return fillResBase(ctx, rank, parsed, resBase, true);
    }
    return fillResBase(ctx, rank, parsed, resBase);
}

static std::vector<uint8_t> copyInstructions(const CcuobjParsed& parsed)
{
    std::vector<uint8_t> instrBytes(parsed.instrCount * kCcuInstrBytes);
    std::copy(parsed.microcode, parsed.microcode + instrBytes.size(), instrBytes.data());
    return instrBytes;
}

static void applyPhysicalResourceRelocs(
    std::vector<uint8_t>& instrBytes, const CcuobjParsed& parsed, const ResBase& resBase)
{
    ccu::ccuobj::PhysBases bases{resBase.xn, resBase.gsa, resBase.cke, resBase.ms};
    ccu::ccuobj::applyResourceRelocs(
        instrBytes.data(), parsed.relocs.data(), parsed.relocs.size(), bases, parsed.instrCount);
}

static void logMsRelocs(
    const std::vector<uint8_t>& instrBytes, const CcuobjParsed& parsed, const ResBase& resBase, uint32_t rank)
{
    for (const auto& reloc : parsed.relocs) {
        if (static_cast<ccu::ccuobj::RelocPool>(reloc.pool) != ccu::ccuobj::RelocPool::Ms ||
            reloc.kind != static_cast<uint8_t>(ccu::ccuobj::RelocKind::Physical) ||
            reloc.instrIdx >= parsed.instrCount || reloc.byteOff > kMaxU16FieldOffset)
            continue;
        uint16_t logical = 0;
        uint16_t physical = 0;
        const size_t offset = static_cast<size_t>(reloc.instrIdx) * kCcuInstrBytes + reloc.byteOff;
        std::copy(
            parsed.microcode + offset, parsed.microcode + offset + sizeof(logical),
            reinterpret_cast<uint8_t*>(&logical));
        std::copy(
            instrBytes.data() + offset, instrBytes.data() + offset + sizeof(physical),
            reinterpret_cast<uint8_t*>(&physical));
        CCU_AOT_LOGI(
            "rank=%u kernel[%u] MS reloc instr=%u byte=%u logical=%u "
            "base=%u physical=%u\n",
            rank, parsed.kernelIndex, reloc.instrIdx, reloc.byteOff, logical, resBase.ms, physical);
    }
}

static bool applyChannelNotifyPhase(
    std::vector<uint8_t>& instrBytes, const CcuobjParsed& parsed, ResBase& resBase, uint32_t rank)
{
    const bool canPatchChannelNotify = !resBase.channelPods.empty();
    const bool needChannelXn = relocsNeedPool(parsed.relocs, ccu::ccuobj::RelocPool::ChannelXn);
    if (needChannelXn && !canPatchChannelNotify) {
        CCU_AOT_LOGE("rank=%u kernel[%u] channel-Xn relocation requires channel POD\n", rank, parsed.kernelIndex);
        return false;
    }
    if (!canPatchChannelNotify)
        return true;

    ccu::ccuobj::PhysLookups lookups{cnChannel, cnChannelRmtXn, cnNotifyRmtCke, cnNotifyLocCke, nullptr, &resBase};
    ccu::ccuobj::applyChannelNotifyRelocs(
        instrBytes.data(), parsed.relocs.data(), parsed.relocs.size(), lookups, parsed.instrCount);
    return applyChannelXnRelocs(instrBytes, parsed, resBase, rank);
}

static void logChannelRelocs(const std::vector<uint8_t>& instrBytes, const CcuobjParsed& parsed, uint32_t rank)
{
    for (const auto& reloc : parsed.relocs) {
        const auto pool = static_cast<ccu::ccuobj::RelocPool>(reloc.pool);
        if (pool != ccu::ccuobj::RelocPool::ChannelXn && pool != ccu::ccuobj::RelocPool::ChannelRmtXn)
            continue;
        if (reloc.instrIdx >= parsed.instrCount || reloc.byteOff > kMaxU16FieldOffset)
            continue;
        uint16_t logical = 0;
        uint16_t physical = 0;
        const size_t offset = static_cast<size_t>(reloc.instrIdx) * kCcuInstrBytes + reloc.byteOff;
        std::copy(
            parsed.microcode + offset, parsed.microcode + offset + sizeof(logical),
            reinterpret_cast<uint8_t*>(&logical));
        std::copy(
            instrBytes.data() + offset, instrBytes.data() + offset + sizeof(physical),
            reinterpret_cast<uint8_t*>(&physical));
        CCU_AOT_LOGI(
            "rank=%u kernel[%u] channel-reloc pool=%u instr=%u "
            "logical=%u physical=%u channelArg=%u index=%u\n",
            rank, parsed.kernelIndex, reloc.pool, reloc.instrIdx, logical, physical, reloc.channelArg, reloc.reserved);
    }
}

static bool applyExternalRelocPhase(
    std::vector<uint8_t>& instrBytes, const CcuobjParsed& parsed, const ExternalRelocCtx::ResolvedArg* externalArgs,
    uint32_t externalArgCount)
{
    if (externalArgCount == 0)
        return true;
    ExternalRelocCtx externalCtx{externalArgs, externalArgCount};
    ccu::ccuobj::ExternalLookups lookups{externalXn, externalCke, &externalCtx};
    ccu::ccuobj::applyExternalRelocs(
        instrBytes.data(), parsed.relocs.data(), parsed.relocs.size(), lookups, parsed.instrCount);
    return true;
}

static bool applyRelocPhases(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, const ExternalRelocCtx::ResolvedArg* externalArgs,
    uint32_t externalArgCount, ResBase& resBase, std::vector<uint8_t>& instrBytes)
{
    applyPhysicalResourceRelocs(instrBytes, parsed, resBase);
    if (getenv("CCU_AOT_TRACE_MS_RELOCS"))
        logMsRelocs(instrBytes, parsed, resBase, rank);
    if (!applyChannelNotifyPhase(instrBytes, parsed, resBase, rank))
        return false;
    if (getenv("CCU_AOT_TRACE_CHANNEL_RELOCS"))
        logChannelRelocs(instrBytes, parsed, rank);
    if (!applyExternalRelocPhase(instrBytes, parsed, externalArgs, externalArgCount))
        return false;
    if (!applyLoopChannelRelocs(instrBytes, parsed, resBase, rank))
        return false;
    if (!applyImmediateRelocsForKernel(ctx, rank, parsed, resBase, instrBytes))
        return false;
    return true;
}

static CcuAotError submitPodInstructions(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, const CcuAotPod* pod, uint32_t dieId,
    std::vector<uint8_t>& instrBytes, CcuAotResourceBase* recordedBase)
{
    if (!pod->allocInstruction || !pod->submitInstructions) {
        CCU_AOT_LOGE(
            "rank=%u kernel[%u] HComm POD instruction callbacks "
            "unavailable\n",
            rank, parsed.kernelIndex);
        return CCU_AOT_E_REGISTER;
    }

    uint32_t instructionId = 0;
    int rc = pod->allocInstruction(ctx->devices[rank], dieId, parsed.instrCount, &instructionId);
    if (rc != 0) {
        CCU_AOT_LOGE(
            "rank=%u kernel[%u] HComm POD instruction alloc failed "
            "die=%u count=%u rc=%d\n",
            rank, parsed.kernelIndex, dieId, parsed.instrCount, rc);
        return CCU_AOT_E_REGISTER;
    }
    if (recordedBase)
        recordedBase->instr = instructionId;

    rc = pod->submitInstructions(ctx->devices[rank], dieId, instructionId, instrBytes.data(), parsed.instrCount);
    if (rc == 0) {
        CCU_AOT_LOGI(
            "rank=%u kernel[%u] instruction base from HComm POD: "
            "die=%u start=%u count=%u\n",
            rank, parsed.kernelIndex, dieId, instructionId, parsed.instrCount);
        return CCU_AOT_SUCCESS;
    }

    // The current HComm POD has no per-kernel instruction-release callback.
    // Instruction space is reclaimed with the owning CCU feature/instance.
    if (recordedBase)
        recordedBase->instr = 0;
    CCU_AOT_LOGE(
        "rank=%u kernel[%u] HComm POD instruction submit failed "
        "die=%u start=%u count=%u rc=%d\n",
        rank, parsed.kernelIndex, dieId, instructionId, parsed.instrCount, rc);
    return CCU_AOT_E_REGISTER;
}

static void logOverrideSuccess(const CcuobjParsed& parsed, const ResBase& resBase, uint32_t dieId, uint32_t rank)
{
    const std::string channelText =
        resBase.channelPods.empty() ? std::string("none") : std::to_string(resBase.channelPods[0].channelId);
    const std::string loopSelfText =
        resBase.hasLoopChannelId ? std::to_string(resBase.loopChannelId) : std::string("none");
    const std::string loopInterText =
        resBase.hasInterDieLoopChannelId ? std::to_string(resBase.interDieLoopChannelId) : std::string("none");
    CCU_AOT_LOGI(
        "rank=%u kernel[%u] override OK: %u instrs mission=%u+%u "
        "die=%u | reloc xn+%u gsa+%u cke+%u ms+%u ch=%s "
        "loopSelf=%s loopInter=%s\n",
        rank, parsed.kernelIndex, parsed.instrCount, parsed.missionStartInstrId, parsed.missionInstrCount, dieId,
        resBase.xn, resBase.gsa, resBase.cke, resBase.ms, channelText.c_str(), loopSelfText.c_str(),
        loopInterText.c_str());
}

static CcuAotError overrideKernelInstructions(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed,
    const ExternalRelocCtx::ResolvedArg* externalArgs = nullptr, uint32_t externalArgCount = 0,
    CcuAotResourceBase* recordedBase = nullptr)
{
    ResBase resBase;
    if (!initializeOverrideResBase(ctx, rank, parsed, recordedBase, resBase))
        return CCU_AOT_E_REGISTER;

    const uint32_t dieId = resBase.die >= 0 ? static_cast<uint32_t>(resBase.die) : 0;
    if (rank >= ctx->pods.size() || !getPod(ctx, rank))
        return CCU_AOT_E_REGISTER;
    const auto* pod = getPod(ctx, rank);
    if (!pod)
        return CCU_AOT_E_REGISTER;

    std::vector<uint8_t> instrBytes = copyInstructions(parsed);
    if (!parsed.relocs.empty()) {
        if (!applyRelocPhases(ctx, rank, parsed, externalArgs, externalArgCount, resBase, instrBytes))
            return CCU_AOT_E_REGISTER;
        CCU_AOT_LOGI(
            "rank=%u kernel[%u] reloc: table-driven (%zu entries)\n", rank, parsed.kernelIndex, parsed.relocs.size());
    } else {
        CCU_AOT_LOGI("rank=%u kernel[%u] reloc: no relocation entries\n", rank, parsed.kernelIndex);
    }

    const CcuAotError status = submitPodInstructions(ctx, rank, parsed, pod, dieId, instrBytes, recordedBase);
    if (status != CCU_AOT_SUCCESS)
        return status;
    logOverrideSuccess(parsed, resBase, dieId, rank);
    return CCU_AOT_SUCCESS;
}

static CcuAotError overrideOneKernel(
    CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed, CcuKernelHandle handle,
    const ExternalRelocCtx::ResolvedArg* externalArgs = nullptr, uint32_t externalArgCount = 0,
    CcuAotResourceBase* recordedBase = nullptr)
{
    // The normal HComm POD path already owns the callback-created kernel
    // repository.  Relocation and instruction loading are local AOT work and
    // do not need to recover that private CcuKernel object just to update its
    // instruction-info cache.  Keep the handle in the AOT record, and use the
    // The saved resource record and POD callbacks are the only cross-SO state here.
    (void)handle;
    return overrideKernelInstructions(ctx, rank, parsed, externalArgs, externalArgCount, recordedBase);
}

static bool validRecordedResourceBase(
    const CcuAotPod* pod, const CcuAotKernelState::RegisteredKernel::AotResourceRecord& record)
{
    return pod && record.base.die >= 0 && static_cast<uint32_t>(record.base.die) < kCcuAotMaxDieNum &&
           (pod->validDieMask & (1u << record.base.die)) && pod->die[record.base.die].enabled;
}

static bool validMissionRange(const CcuobjParsed& parsed)
{
    return parsed.missionStartInstrId <= parsed.instrCount &&
           parsed.missionInstrCount <= parsed.instrCount - parsed.missionStartInstrId;
}

static uint32_t taskParamChunkCount(uint32_t taskArgCount)
{
    constexpr uint32_t kCcuSqeArgsLen = RT_CCU_SQE_ARGS_LEN;
    if (taskArgCount == 0)
        return 1;
    return (taskArgCount + kCcuSqeArgsLen - 1) / kCcuSqeArgsLen;
}

static bool validTaskParamPrefix(const CcuobjParsed& parsed, uint32_t taskCount)
{
    constexpr uint32_t kCcuSqeArgsLen = RT_CCU_SQE_ARGS_LEN;
    const uint32_t prefixInstrCount = (taskCount - 1) * kCcuSqeArgsLen;
    if (parsed.missionInstrCount >= prefixInstrCount)
        return true;
    CCU_AOT_LOGE(
        "rank=AOT task-arg chunks=%u require mission instructions=%u, "
        "but mission has only %u instructions\n",
        taskCount, prefixInstrCount, parsed.missionInstrCount);
    return false;
}

static void fillTaskParams(
    const CcuobjParsed& parsed, const CcuAotPod* pod, CcuAotKernelState::RegisteredKernel::AotResourceRecord& record,
    uint32_t taskCount)
{
    constexpr uint32_t kCcuSqeArgsLen = RT_CCU_SQE_ARGS_LEN;
    record.taskParams.clear();
    record.taskArgCount = parsed.taskArgCount;
    record.taskParams.reserve(taskCount);
    for (uint32_t index = 0; index < taskCount; ++index) {
        const uint32_t instrOffset = index * kCcuSqeArgsLen;
        const bool isLast = index + 1 == taskCount;
        const uint32_t instrCount = isLast ? parsed.missionInstrCount - instrOffset : kCcuSqeArgsLen;
        record.taskParams.push_back(
            {static_cast<uint8_t>(record.base.die), static_cast<uint8_t>(record.base.mission),
             record.base.instr + parsed.missionStartInstrId + instrOffset, instrCount,
             pod->die[record.base.die].missionKey, kCcuSqeArgsLen});
    }
}

static bool validCallerChannels(CcuAotContext* ctx, uint32_t rank, const CcuobjParsed& parsed)
{
    for (uint32_t index = 0; index < parsed.channelArgCount; ++index) {
        if (rank >= ctx->peerChannels.size() || index >= ctx->peerChannels[rank].size() ||
            !ctx->peerChannels[rank][index])
            return false;
        HcommCcuChannelEntity channelPod{};
        if (!queryChannelEntity(ctx, rank, ctx->peerChannels[rank][index], channelPod))
            return false;
    }
    return true;
}

static CcuAotError captureTaskParamRecord(
    CcuAotContext* ctx, uint32_t rank, CcuKernelHandle handle, const CcuobjParsed& parsed,
    CcuAotKernelState::RegisteredKernel::AotResourceRecord& record)
{
    if (!ctx || rank >= ctx->devices.size() || handle == 0)
        return CCU_AOT_E_PARAM;
    const auto* pod = getPod(ctx, rank);
    if (!validRecordedResourceBase(pod, record))
        return CCU_AOT_E_REGISTER;
    if (!validMissionRange(parsed)) {
        CCU_AOT_LOGE(
            "rank=%u invalid AOT mission range start=%u count=%u total=%u\n", ccuAotGlobalRank(ctx, rank),
            parsed.missionStartInstrId, parsed.missionInstrCount, parsed.instrCount);
        return CCU_AOT_E_REGISTER;
    }

    const uint32_t taskCount = taskParamChunkCount(parsed.taskArgCount);
    if (!validTaskParamPrefix(parsed, taskCount)) {
        CCU_AOT_LOGE(
            "rank=%u AOT task-arg chunks=%u require mission instructions=%u, "
            "but mission has only %u instructions\n",
            ccuAotGlobalRank(ctx, rank), taskCount, (taskCount - 1) * RT_CCU_SQE_ARGS_LEN, parsed.missionInstrCount);
        return CCU_AOT_E_REGISTER;
    }
    fillTaskParams(parsed, pod, record, taskCount);
    if (!validCallerChannels(ctx, rank, parsed))
        return CCU_AOT_E_REGISTER;
    return CCU_AOT_SUCCESS;
}

// Registration snapshots the physical task descriptors once. Launch submits
// the saved descriptors through the runtime primitive. HComm still owns the
// stream, device, and timeout; this path consumes the kernel state record.
static const CcuAotKernelState::RegisteredKernel::AotResourceRecord* findResourceRecord(
    const CcuAotContext* ctx, uint32_t rank, CcuKernelHandle handle)
{
    if (!ctx || handle == 0)
        return nullptr;
    for (const auto& kernel : ctx->kernelState->registeredKernels) {
        if (!kernel.active || rank >= kernel.rankRecords.size() || kernel.rankRecords[rank].handle != handle ||
            !kernel.rankRecords[rank].resources.valid)
            continue;
        return &kernel.rankRecords[rank].resources;
    }
    return nullptr;
}

static bool validLaunchRecord(const CcuAotKernelState::RegisteredKernel::AotResourceRecord* record, uint32_t argCount)
{
    return record && !record->taskParams.empty() && argCount == record->taskArgCount;
}

static rtCcuTaskInfo_t makeTaskInfo(
    const CcuAotKernelState::RegisteredKernel::AotResourceRecord::TaskParam& param, uint32_t timeoutSec)
{
    rtCcuTaskInfo_t taskInfo{};
    taskInfo.dieId = param.dieId;
    taskInfo.missionId = param.missionId;
    taskInfo.instStartId = param.instrStartId;
    taskInfo.instCnt = param.instrCount;
    taskInfo.key = param.key;
    taskInfo.argSize = param.argSize;
    taskInfo.timeout = timeoutSec;
    return taskInfo;
}

static bool submitTaskParam(const rtCcuTaskInfo_t& taskInfo, void* executionStream)
{
    rtCcuTaskInfo_t mutableTaskInfo = taskInfo;
    const rtError_t launchRc = rtCCULaunch(&mutableTaskInfo, reinterpret_cast<rtStream_t>(executionStream));
    return launchRc == 0;
}

static CcuAotError launchMaterializedKernel(
    CcuAotContext* ctx, uint32_t rank, CcuKernelHandle handle, const uint64_t* taskArgs, uint32_t argCount,
    uint32_t timeoutSec, const char* label, void* executionStream)
{
    if (!ctx || rank >= ctx->ndev || handle == 0 || !executionStream || (argCount != 0 && !taskArgs))
        return CCU_AOT_E_PARAM;

    const uint32_t logRank = ccuAotGlobalRank(ctx, rank);
    const auto* record = findResourceRecord(ctx, rank, handle);
    if (!validLaunchRecord(record, argCount)) {
        CCU_AOT_LOGE(
            "%s rank=%u materialized kernel handle=0x%llx not found\n", label, logRank,
            static_cast<unsigned long long>(handle));
        return CCU_AOT_E_LAUNCH;
    }

    uint32_t argOffset = 0;
    for (size_t index = 0; index < record->taskParams.size(); ++index) {
        const auto& param = record->taskParams[index];
        CCU_AOT_LOGI(
            "%s rank=%u AOT submit-summary task=%zu/%zu die=%u "
            "mission=%u instr=%u+%u key=%u argSize=%u stream=%p\n",
            label, logRank, index + 1, record->taskParams.size(), param.dieId, param.missionId, param.instrStartId,
            param.instrCount, param.key, param.argSize, executionStream);

        rtCcuTaskInfo_t taskInfo = makeTaskInfo(param, timeoutSec);
        const uint32_t available = argOffset < argCount ? argCount - argOffset : 0;
        const uint32_t copied = std::min(param.argSize, available);
        if (copied != 0) {
            std::copy(taskArgs + argOffset, taskArgs + argOffset + copied, std::begin(taskInfo.args));
        }
        argOffset += param.argSize;
        const bool submitted = submitTaskParam(taskInfo, executionStream);
        if (!submitted) {
            CCU_AOT_LOGE("%s rank=%u AOT rtCCULaunch failed: %d\n", label, logRank, static_cast<int>(!submitted));
            return CCU_AOT_E_LAUNCH;
        }
    }
    return CCU_AOT_SUCCESS;
}

static CcuAotError launchKernelHandleOnStream(
    CcuAotContext* ctx, uint32_t rank, CcuKernelHandle handle, const uint64_t* taskArgs, uint32_t argCount,
    const char* label, void* executionStream = nullptr)
{
    if (!ctx || rank >= ctx->ndev || handle == 0)
        return CCU_AOT_E_PARAM;
    if (argCount != 0 && !taskArgs)
        return CCU_AOT_E_PARAM;
    if (!executionStream || rank >= ctx->pods.size() || !getPod(ctx, rank))
        return CCU_AOT_E_PARAM;

    for (uint32_t k = 0; k < argCount; k++)
        CCU_AOT_LOGI("%s taskArg[%u] = 0x%lx\n", label, k, (unsigned long)taskArgs[k]);

    rtStream_t stream = reinterpret_cast<rtStream_t>(executionStream);
    ThreadHandle thread = 0;
    HcclResult threadRc = HcclThreadAcquireWithStream(ctx->comms[rank], COMM_ENGINE_CCU, stream, 0, &thread);
    if (threadRc != HCCL_SUCCESS || thread == 0) {
        CCU_AOT_LOGE(
            "%s rank=%u HcclThreadAcquireWithStream(user stream) failed: %d\n", label, rank,
            static_cast<int>(threadRc));
        return CCU_AOT_E_LAUNCH;
    }
    // The public POD launch contract does not expose a private launch-context
    // object. Runtime synchronization supplies the bounded wait; zero here
    // leaves the device task timeout at the HComm/runtime default.
    return launchMaterializedKernel(ctx, rank, handle, taskArgs, argCount, 0, label, stream);
}

static CcuAotError launchKernelHandle(
    CcuAotContext* ctx, uint32_t rank, CcuKernelHandle handle, const uint64_t* taskArgs, uint32_t argCount,
    const char* label, void* executionStream = nullptr)
{
    if (!ctx || rank >= ctx->ndev || !executionStream)
        return CCU_AOT_E_PARAM;
    return runOnFreshDeviceThread(ctx->devices[rank], CCU_AOT_E_LAUNCH, [&]() {
        return launchKernelHandleOnStream(ctx, rank, handle, taskArgs, argCount, label, executionStream);
    });
}

static CcuAotKernelState::RegisteredKernel* lookupRegisteredKernel(CcuAotContext* ctx, uint32_t kernelId)
{
    if (!ctx || kernelId >= ctx->kernelState->registeredKernels.size())
        return nullptr;
    auto& rk = ctx->kernelState->registeredKernels[kernelId];
    return rk.active ? &rk : nullptr;
}

static CcuAotError releaseRegisteredKernel(CcuAotContext* ctx, uint32_t kernelId)
{
    if (!ctx || kernelId >= ctx->kernelState->registeredKernels.size())
        return CCU_AOT_E_PARAM;
    auto& rk = ctx->kernelState->registeredKernels[kernelId];

    for (uint32_t rank = 0; rank < rk.rankRecords.size(); ++rank) {
        auto& rankRecord = rk.rankRecords[rank];
        if (rankRecord.handle == 0 && !rankRecord.resources.instructionLeased)
            continue;
        if (rankRecord.resources.instructionLeased) {
            // HComm owns instruction-space reclamation at feature/instance
            // teardown; only drop the adapter's lease bookkeeping here.
            rankRecord.resources.instructionLeased = false;
        }
        rankRecord.handle = 0;
    }

    rk.active = false;
    rk.blob.clear();
    ctx->kernelState->registered = false;
    ctx->kernelState->defaultKernelId = UINT32_MAX;
    for (uint32_t id = 0; id < ctx->kernelState->registeredKernels.size(); ++id) {
        const auto& candidate = ctx->kernelState->registeredKernels[id];
        if (!candidate.active)
            continue;
        ctx->kernelState->registered = true;
        if (ctx->kernelState->defaultKernelId == UINT32_MAX) {
            ctx->kernelState->defaultKernelId = id;
        }
    }
    return CCU_AOT_SUCCESS;
}

} // namespace

extern "C" {
CcuAotError ccu_aot_external_event_alloc_va(
    CcuAotContext* ctx, uint32_t rank, uint32_t count, uint64_t* handle, uint64_t* vas)
{
    return allocExternalEventVaImpl(ctx, rank, count, handle, vas);
}

CcuAotError ccu_aot_external_xn_alloc_va(
    CcuAotContext* ctx, uint32_t rank, uint32_t count, uint64_t* handle, uint64_t* vas)
{
    return allocExternalXnVaImpl(ctx, rank, count, handle, vas);
}

static bool validExternalRegistrationArgs(const CcuAotExternalArg* externalArgs, uint32_t externalArgCount)
{
    for (uint32_t i = 0; i < externalArgCount; ++i) {
        if (externalArgs[i].count == 0 || externalArgs[i].acquire_handle == 0 ||
            (externalArgs[i].kind != CCU_EXTERNAL_ARG_EVENT && externalArgs[i].kind != CCU_EXTERNAL_ARG_XN))
            return false;
    }
    return true;
}

static bool parseRegisteredKernelBlob(
    const void* ccuobj, size_t size, uint32_t kernelIndex, CcuAotKernelState::RegisteredKernel& entry,
    std::vector<CcuobjParsed>& parsedKernels)
{
    entry.blob.assign(static_cast<const uint8_t*>(ccuobj), static_cast<const uint8_t*>(ccuobj) + size);
    if (!ParseCcuobjAll(entry.blob.data(), entry.blob.size(), parsedKernels)) {
        CCU_AOT_LOGE(".ccuobj parse failed\n");
        return false;
    }
    if (kernelIndex >= parsedKernels.size()) {
        CCU_AOT_LOGI("kernelIndex=%u out of range (kernels=%zu)\n", kernelIndex, parsedKernels.size());
        return false;
    }
    entry.parsed = parsedKernels[kernelIndex];
    return true;
}

static bool registrationSupportsExternalArgs(uint32_t externalArgCount)
{
    if (externalArgCount == 0)
        return true;
    CCU_AOT_LOGE("AOT external resource registration is not supported by the "
                 "POD-only path yet\n");
    return false;
}

static bool registrationHasRequiredChannels(CcuAotContext* ctx, const CcuobjParsed& parsed)
{
    CCU_AOT_LOGI(
        "AOT registration mode=pod-owned kernel[%u] ndev=%u "
        "channels=%u\n",
        parsed.kernelIndex, ctx->ndev, parsed.channelArgCount);
    if (parsed.channelArgCount != 0 && !ctx->channelsAcquired) {
        CCU_AOT_LOGE(
            "AOT registration requires caller-provided channels; "
            "kernel has channel args=%u\n",
            parsed.channelArgCount);
        return false;
    }
    CCU_AOT_LOGI("AOT registration stage=caller-resource-check OK channels=%u\n", parsed.channelArgCount);
    return true;
}

static void logRegisteredKernel(
    const std::vector<CcuobjParsed>& parsedKernels, uint32_t kernelId,
    const CcuAotKernelState::RegisteredKernel& kernel)
{
    CCU_AOT_LOGI(
        "Parsed %zu kernels; register kernelId=%u kernel[%u]=%s instr=%u, "
        "channels=%u, taskArgs=%u | manifest var=%u event=%u localAddr=%u "
        "remoteAddr=%u ms=%u\n",
        parsedKernels.size(), kernelId, kernel.parsed.kernelIndex, kernel.parsed.name.c_str(), kernel.parsed.instrCount,
        kernel.parsed.channelArgCount, kernel.parsed.taskArgCount, kernel.parsed.varCount, kernel.parsed.eventCount,
        kernel.parsed.localAddrCount, kernel.parsed.remoteAddrCount, kernel.parsed.msCount);
}

static void registerRankKernel(
    CcuAotContext* ctx, uint32_t rank, uint32_t newKernelId, CcuAotKernelState::RegisteredKernel& kernel,
    CcuAotKernelState::RegisteredKernel::RankKernelRecord& rankRecord, CcuAotError& status)
{
    aclrtSetDevice(ctx->devices[rank]);
    rankRecord.handle = static_cast<CcuKernelHandle>(newKernelId + 1);

    CcuInsHandle metadataHandle = 0;
    status = queryCcuInsHandle(ctx, rank, &metadataHandle);
    if (status != CCU_AOT_SUCCESS) {
        CCU_AOT_LOGE(
            "rank=%u kernel[%u] AOT registration stage=context-POD-query "
            "failed rc=%d\n",
            ccuAotGlobalRank(ctx, rank), kernel.parsed.kernelIndex, static_cast<int>(status));
        return;
    }

    CcuAotResourceBase materializedBase{};
    if (!materializeAotResourceBase(ctx, rank, kernel.parsed, materializedBase)) {
        CCU_AOT_LOGE(
            "rank=%u kernel[%u] AOT registration "
            "stage=resource-materialization failed\n",
            ccuAotGlobalRank(ctx, rank), kernel.parsed.kernelIndex);
        status = CCU_AOT_E_REGISTER;
        return;
    }
    rankRecord.resources.base = materializedBase;

    status = overrideOneKernel(ctx, rank, kernel.parsed, rankRecord.handle, nullptr, 0, &rankRecord.resources.base);
    if (status != CCU_AOT_SUCCESS) {
        CCU_AOT_LOGE(
            "rank=%u kernel[%u] AOT registration "
            "stage=instruction-load failed rc=%d\n",
            ccuAotGlobalRank(ctx, rank), kernel.parsed.kernelIndex, static_cast<int>(status));
        return;
    }
    rankRecord.resources.instructionLeased = true;

    status = captureTaskParamRecord(ctx, rank, rankRecord.handle, kernel.parsed, rankRecord.resources);
    if (status != CCU_AOT_SUCCESS) {
        CCU_AOT_LOGE(
            "rank=%u kernel[%u] AOT registration stage=task-param failed "
            "rc=%d\n",
            ccuAotGlobalRank(ctx, rank), kernel.parsed.kernelIndex, static_cast<int>(status));
        return;
    }
    rankRecord.resources.valid = true;
}

static CcuAotError rollbackRegisteredKernel(CcuAotContext* ctx, uint32_t kernelId);

static CcuAotError registerRanksInParallel(
    CcuAotContext* ctx, uint32_t newKernelId, CcuAotKernelState::RegisteredKernel& kernel)
{
    std::vector<CcuAotError> statuses(ctx->ndev, CCU_AOT_SUCCESS);
    std::vector<std::thread> threads;
    CCU_AOT_LOGI("AOT registration stage=materialize-launch-record begin ranks=%u\n", ctx->ndev);
    for (uint32_t rank = 0; rank < ctx->ndev; ++rank) {
        threads.emplace_back([&, rank]() {
            registerRankKernel(ctx, rank, newKernelId, kernel, kernel.rankRecords[rank], statuses[rank]);
        });
    }
    for (auto& thread : threads)
        thread.join();
    aclrtSetDevice(ctx->devices[0]);

    for (uint32_t rank = 0; rank < ctx->ndev; ++rank) {
        if (statuses[rank] == CCU_AOT_SUCCESS)
            continue;
        CCU_AOT_LOGE(
            "rank=%u AOT registration failed rc=%d\n", ccuAotGlobalRank(ctx, rank), static_cast<int>(statuses[rank]));
        (void)rollbackRegisteredKernel(ctx, newKernelId);
        return statuses[rank];
    }
    return CCU_AOT_SUCCESS;
}

static CcuAotError rollbackRegisteredKernel(CcuAotContext* ctx, uint32_t kernelId)
{
    CcuAotError releaseRc = releaseRegisteredKernel(ctx, kernelId);
    if (releaseRc == CCU_AOT_SUCCESS && kernelId + 1 == ctx->kernelState->registeredKernels.size())
        ctx->kernelState->registeredKernels.pop_back();
    return releaseRc;
}

CcuAotError ccu_aot_register_kernel_with_external(
    CcuAotContext* ctx, const void* ccuobj, size_t size, uint32_t kernelIndex, const CcuAotExternalArg* externalArgs,
    uint32_t externalArgCount, uint32_t* kernelId)
{
    if (!ctx || !ccuobj || size == 0)
        return CCU_AOT_E_PARAM;
    if (externalArgCount != 0 && !externalArgs)
        return CCU_AOT_E_PARAM;
    if (!validExternalRegistrationArgs(externalArgs, externalArgCount))
        return CCU_AOT_E_PARAM;
    if (kernelId)
        *kernelId = UINT32_MAX;

    CcuAotKernelState::RegisteredKernel entry;
    entry.rankRecords.resize(ctx->ndev);
    std::vector<CcuobjParsed> parsedKernels;
    if (!parseRegisteredKernelBlob(ccuobj, size, kernelIndex, entry, parsedKernels))
        return kernelIndex >= parsedKernels.size() ? CCU_AOT_E_PARAM : CCU_AOT_E_REGISTER;
    if (!registrationSupportsExternalArgs(externalArgCount))
        return CCU_AOT_E_REGISTER;
    if (!registrationHasRequiredChannels(ctx, entry.parsed))
        return CCU_AOT_E_REGISTER;

    const uint32_t newKernelId = static_cast<uint32_t>(ctx->kernelState->registeredKernels.size());
    ctx->kernelState->registeredKernels.push_back(std::move(entry));
    auto& kernel = ctx->kernelState->registeredKernels.back();
    logRegisteredKernel(parsedKernels, newKernelId, kernel);

    const CcuAotError status = registerRanksInParallel(ctx, newKernelId, kernel);
    if (status != CCU_AOT_SUCCESS)
        return status;

    kernel.active = true;
    ctx->kernelState->registered = true;
    if (ctx->kernelState->defaultKernelId == UINT32_MAX)
        ctx->kernelState->defaultKernelId = newKernelId;
    if (kernelId)
        *kernelId = newKernelId;
    CCU_AOT_LOGI("register kernelId=%u OK (live=%zu)\n", newKernelId, ctx->kernelState->registeredKernels.size());
    return CCU_AOT_SUCCESS;
}

CcuAotError ccu_aot_unregister_kernel(CcuAotContext* ctx, uint32_t kernelId)
{
    if (!lookupRegisteredKernel(ctx, kernelId))
        return CCU_AOT_E_PARAM;
    return releaseRegisteredKernel(ctx, kernelId);
}

CcuAotError ccu_aot_launch_registered_on_stream(
    CcuAotContext* ctx, uint32_t kernelId, uint32_t rank, const uint64_t* taskArgs, uint32_t argCount,
    const CcuAotExternalArg* externalArgs, uint32_t externalArgCount, void* executionStream)
{
    if (!ctx || !ctx->kernelState->registered || !executionStream || rank >= ctx->ndev ||
        (externalArgCount != 0 && !externalArgs))
        return CCU_AOT_E_PARAM;
    for (uint32_t i = 0; i < externalArgCount; ++i) {
        if (externalArgs[i].count == 0 || externalArgs[i].acquire_handle == 0 ||
            (externalArgs[i].kind != CCU_EXTERNAL_ARG_EVENT && externalArgs[i].kind != CCU_EXTERNAL_ARG_XN))
            return CCU_AOT_E_PARAM;
    }
    auto* rk = lookupRegisteredKernel(ctx, kernelId);
    if (!rk || rank >= rk->rankRecords.size() || rk->rankRecords[rank].handle == 0)
        return CCU_AOT_E_PARAM;
    const bool needsExternal = relocsNeedPool(rk->parsed.relocs, ccu::ccuobj::RelocPool::ExternalXn) ||
                               relocsNeedPool(rk->parsed.relocs, ccu::ccuobj::RelocPool::ExternalCke);
    if (needsExternal && externalArgCount == 0)
        return CCU_AOT_E_PARAM;

    return launchKernelHandle(
        ctx, rank, rk->rankRecords[rank].handle, taskArgs, argCount, "rank-kernel-user-stream", executionStream);
}

struct SyncRecoveryTrace {
    aclError syncRc = ACL_SUCCESS;
    uint64_t syncElapsedMs = 0;
    std::string syncError = "none";

    aclError streamAbortRc = ACL_SUCCESS;
    uint64_t streamAbortElapsedMs = 0;
    std::string streamAbortError = "none";

    aclError deviceAbortRc = ACL_SUCCESS;
    uint64_t deviceAbortElapsedMs = 0;
    std::string deviceAbortError = "none";

    aclError deviceRetryAbortRc = ACL_SUCCESS;
    uint64_t deviceRetryAbortElapsedMs = 0;
    std::string deviceRetryAbortError = "none";

    aclError deviceConfirmRc = ACL_SUCCESS;
    aclError deviceFirstConfirmRc = ACL_SUCCESS;
    uint64_t deviceConfirmElapsedMs = 0;
    std::string deviceConfirmError = "none";

    aclError streamQueryRc = ACL_SUCCESS;
    aclrtStreamStatus streamStatus = ACL_STREAM_STATUS_RESERVED;
    std::string streamQueryError = "none";

    uint32_t deviceConfirmAttempts = 0;
    uint32_t deviceConfirmRounds = 0;
    uint32_t deviceAbortAttempts = 0;
    uint32_t deviceAbortTimeoutSeconds = 1;
    uint64_t deviceRecoveryBudgetMs = 0;
    std::chrono::steady_clock::time_point deviceRecoveryDeadline{};

    bool deviceAbortAttempted = false;
    bool deviceRetryAbortAttempted = false;
    bool deviceConfirmAttempted = false;
    bool hcommFinalizeCompleted = false;
    HcommRecoveryResult hcommFinalize{};
};

int32_t boundedSyncTimeoutMs(uint64_t timeout)
{
    const uint64_t bounded =
        std::max<uint64_t>(1, std::min<uint64_t>(timeout, static_cast<uint64_t>(std::numeric_limits<int32_t>::max())));
    return static_cast<int32_t>(bounded);
}

bool shouldAbortDevice(aclError rc, bool requireDeviceRecovery, bool allowForcedDeviceAbort)
{
    return requireDeviceRecovery || isMissionTimeout(rc) || (allowForcedDeviceAbort && shouldAbortDeviceTasks(rc));
}

void abortExecutionStream(aclrtStream stream, SyncRecoveryTrace& trace)
{
    if (!stream)
        return;
    const auto start = std::chrono::steady_clock::now();
    trace.streamAbortRc = aclrtStreamAbort(stream);
    trace.streamAbortElapsedMs = elapsedMs(start);
    if (trace.streamAbortRc != ACL_SUCCESS)
        trace.streamAbortError = sanitizeRecoveryError(aclGetRecentErrMsg());
}

void abortDeviceTasks(
    CcuAotContext* ctx, uint32_t rank, uint64_t abortTimeout, CcuAotSyncResult& out, SyncRecoveryTrace& trace)
{
    trace.deviceRecoveryBudgetMs = std::max<uint64_t>(
        1, std::min<uint64_t>(abortTimeout, static_cast<uint64_t>(std::numeric_limits<int32_t>::max())));
    const uint64_t roundedSeconds =
        trace.deviceRecoveryBudgetMs / 1000 + (trace.deviceRecoveryBudgetMs % 1000 != 0 ? 1 : 0);
    const uint64_t timeoutSeconds =
        std::min<uint64_t>(roundedSeconds, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()));
    trace.deviceAbortTimeoutSeconds = std::max<uint32_t>(1, static_cast<uint32_t>(timeoutSeconds));

    out.abortAction = CCU_AOT_ABORT_DEVICE;
    trace.deviceAbortAttempted = true;
    trace.deviceAbortAttempts = 1;
    const auto start = std::chrono::steady_clock::now();
    trace.deviceAbortRc = aclrtDeviceTaskAbort(ctx->devices[rank], trace.deviceAbortTimeoutSeconds);
    trace.deviceAbortElapsedMs = elapsedMs(start);
    if (trace.deviceAbortRc != ACL_SUCCESS)
        trace.deviceAbortError = sanitizeRecoveryError(aclGetRecentErrMsg());
    out.terminal = trace.deviceAbortRc == ACL_SUCCESS;
}

extern "C++" DeviceQuiescenceResult confirmDeviceQuiescence(uint64_t timeout, SyncRecoveryTrace& trace)
{
    const auto start = std::chrono::steady_clock::now();
    DeviceQuiescenceResult result = waitForDeviceQuiescence(timeout);
    trace.deviceConfirmElapsedMs += elapsedMs(start);
    trace.deviceConfirmRc = result.rc;
    trace.deviceConfirmAttempts += result.attempts;
    if (trace.deviceConfirmRc != ACL_SUCCESS)
        trace.deviceConfirmError = sanitizeRecoveryError(aclGetRecentErrMsg());
    return result;
}

void recoverThroughHcomm(CcuAotContext* ctx, uint32_t rank, CcuAotSyncResult& out, SyncRecoveryTrace& trace)
{
    trace.hcommFinalize = recoverHcommCcuAbort(ctx, rank);
    if (trace.hcommFinalize.rc == HCCL_SUCCESS) {
        out.terminal = 1;
        trace.hcommFinalizeCompleted = true;
    }
}

uint32_t remainingRecoverySeconds(const SyncRecoveryTrace& trace)
{
    const auto now = std::chrono::steady_clock::now();
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(trace.deviceRecoveryDeadline - now).count();
    if (remaining <= 0)
        return 0;
    const uint64_t seconds =
        static_cast<uint64_t>(remaining) / 1000 + (static_cast<uint64_t>(remaining) % 1000 != 0 ? 1 : 0);
    return std::max<uint32_t>(
        1, static_cast<uint32_t>(std::min<uint64_t>(seconds, std::numeric_limits<uint32_t>::max())));
}

void retryDeviceAbort(CcuAotContext* ctx, uint32_t rank, CcuAotSyncResult& out, SyncRecoveryTrace& trace)
{
    trace.deviceRecoveryDeadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(trace.deviceRecoveryBudgetMs);
    const uint32_t retrySeconds = remainingRecoverySeconds(trace);
    if (retrySeconds == 0)
        return;

    trace.deviceRetryAbortAttempted = true;
    trace.deviceAbortAttempts = kMaxDeviceAbortAttempts;
    const auto start = std::chrono::steady_clock::now();
    trace.deviceRetryAbortRc = aclrtDeviceTaskAbort(ctx->devices[rank], retrySeconds);
    trace.deviceRetryAbortElapsedMs = elapsedMs(start);
    if (trace.deviceRetryAbortRc != ACL_SUCCESS)
        trace.deviceRetryAbortError = sanitizeRecoveryError(aclGetRecentErrMsg());
    if (trace.deviceRetryAbortRc == ACL_SUCCESS) {
        out.terminal = 1;
        return;
    }

    const auto afterRetry = std::chrono::steady_clock::now();
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(trace.deviceRecoveryDeadline - afterRetry).count();
    if (remaining <= 0)
        return;
    ++trace.deviceConfirmRounds;
    const DeviceQuiescenceResult confirm = confirmDeviceQuiescence(static_cast<uint64_t>(remaining), trace);
    if (confirm.rc == ACL_SUCCESS)
        out.terminal = 1;
}

void recoverAfterFailedDeviceAbort(
    CcuAotContext* ctx, uint32_t rank, uint64_t abortTimeout, CcuAotSyncResult& out, SyncRecoveryTrace& trace)
{
    trace.deviceConfirmAttempted = true;
    trace.deviceConfirmRounds = 1;
    const uint64_t settleTimeout = std::max<uint64_t>(1, std::min<uint64_t>(abortTimeout, kAbortFinalizeSettleMs));
    const DeviceQuiescenceResult confirm = confirmDeviceQuiescence(settleTimeout, trace);
    trace.deviceFirstConfirmRc = confirm.rc;
    if (trace.deviceConfirmRc == ACL_SUCCESS) {
        out.terminal = 1;
        return;
    }

    recoverThroughHcomm(ctx, rank, out, trace);
    if (!out.terminal)
        retryDeviceAbort(ctx, rank, out, trace);
}

void queryExecutionStream(aclrtStream stream, SyncRecoveryTrace& trace)
{
    if (!stream)
        return;
    trace.streamQueryRc = aclrtStreamQuery(stream, &trace.streamStatus);
    if (trace.streamQueryRc != ACL_SUCCESS)
        trace.streamQueryError = sanitizeRecoveryError(aclGetRecentErrMsg());
}

const char* terminalSource(const SyncRecoveryTrace& trace, const CcuAotSyncResult& out)
{
    if (!out.terminal)
        return "none";
    if (trace.hcommFinalizeCompleted)
        return "hcomm-public-recovery";
    if (trace.deviceRetryAbortAttempted && trace.deviceRetryAbortRc == ACL_SUCCESS)
        return "device-abort-finalize";
    if (trace.deviceConfirmAttempted && trace.deviceConfirmRc == ACL_SUCCESS)
        return "device-confirm";
    if (trace.deviceAbortAttempted)
        return "device-abort";
    return "stream-abort";
}

void logRecoveryDetail(
    uint32_t rank, CcuAotContext* ctx, const CcuAotSyncResult& out, const SyncRecoveryTrace& trace, aclrtStream stream,
    const std::chrono::steady_clock::time_point& recoveryStart)
{
    CCU_AOT_LOGE(
        "rank=%u recovery-detail sync-rc=%d sync-class=%s sync-ms=%llu "
        "stream-abort-rc=%d stream-abort-class=%s stream-abort-ms=%llu "
        "device-abort-rc=%d device-abort-class=%s device-abort-ms=%llu "
        "device-abort-attempts=%u device-abort-seq=%d,%d "
        "device-retry-abort-class=%s device-retry-abort-ms=%llu "
        "device-confirm-rc=%d device-confirm-class=%s "
        "device-confirm-first-rc=%d device-confirm-rounds=%u "
        "device-confirm-attempts=%u device-confirm-ms=%llu "
        "hcomm-public=%u public-suspend-rc=%d public-resume-rc=%d "
        "hcomm-recovery-rc=%d hcomm-recovery-ms=%llu "
        "stream-query-rc=%d stream-status=%d tail-rc=%d tail-ms=%llu "
        "recovery-round-budget-ms=%llu terminal=%u terminal-source=%s "
        "total-ms=%llu "
        "recent-error=sync:%s,stream-abort:%s,device-abort:%s,"
        "device-retry-abort:%s,device-confirm:%s,stream-query:%s\n",
        rank, static_cast<int>(trace.syncRc), recoveryRcClass(trace.syncRc),
        static_cast<unsigned long long>(trace.syncElapsedMs), stream ? static_cast<int>(trace.streamAbortRc) : -1,
        stream ? recoveryRcClass(trace.streamAbortRc) : "not-attempted",
        static_cast<unsigned long long>(trace.streamAbortElapsedMs),
        trace.deviceAbortAttempted ? static_cast<int>(trace.deviceAbortRc) : -1,
        trace.deviceAbortAttempted ? recoveryRcClass(trace.deviceAbortRc) : "not-attempted",
        static_cast<unsigned long long>(trace.deviceAbortElapsedMs), trace.deviceAbortAttempts,
        trace.deviceAbortAttempted ? static_cast<int>(trace.deviceAbortRc) : -1,
        trace.deviceRetryAbortAttempted ? static_cast<int>(trace.deviceRetryAbortRc) : -1,
        trace.deviceRetryAbortAttempted ? recoveryRcClass(trace.deviceRetryAbortRc) : "not-attempted",
        static_cast<unsigned long long>(trace.deviceRetryAbortElapsedMs),
        trace.deviceConfirmAttempted ? static_cast<int>(trace.deviceConfirmRc) : -1,
        trace.deviceConfirmAttempted ? recoveryRcClass(trace.deviceConfirmRc) : "not-attempted",
        trace.deviceConfirmAttempted ? static_cast<int>(trace.deviceFirstConfirmRc) : -1, trace.deviceConfirmRounds,
        trace.deviceConfirmAttempts, static_cast<unsigned long long>(trace.deviceConfirmElapsedMs),
        trace.hcommFinalize.attempted ? 1u : 0u, trace.hcommFinalize.publicSuspendRc,
        trace.hcommFinalize.publicResumeRc, trace.hcommFinalize.attempted ? trace.hcommFinalize.rc : -1,
        static_cast<unsigned long long>(trace.hcommFinalize.elapsedMs),
        stream ? static_cast<int>(trace.streamQueryRc) : -1, stream ? static_cast<int>(trace.streamStatus) : -1,
        static_cast<int>(CCU_AOT_SUCCESS), 0ULL, static_cast<unsigned long long>(trace.deviceRecoveryBudgetMs),
        out.terminal, terminalSource(trace, out), static_cast<unsigned long long>(elapsedMs(recoveryStart)),
        trace.syncError.c_str(), trace.streamAbortError.c_str(), trace.deviceAbortError.c_str(),
        trace.deviceRetryAbortError.c_str(), trace.deviceConfirmError.c_str(), trace.streamQueryError.c_str());
    (void)ctx;
}

static CcuAotError ccuAotSyncImpl(
    CcuAotContext* ctx, uint32_t rank, void* stream, uint64_t syncTimeout, uint64_t abortTimeout,
    bool allowForcedDeviceAbort, bool requireDeviceRecovery, CcuAotSyncResult* out)
{
    if (!ctx || rank >= ctx->ndev || !stream || !out)
        return CCU_AOT_E_PARAM;
    *out = {CCU_AOT_SUCCESS, CCU_AOT_ABORT_NONE, 0, 0};
    aclrtSetDevice(ctx->devices[rank]);

    SyncRecoveryTrace trace;
    aclrtStream executionStream = reinterpret_cast<aclrtStream>(stream);
    const auto recoveryStart = std::chrono::steady_clock::now();
    const auto syncStart = std::chrono::steady_clock::now();
    trace.syncRc = executionStream ?
                       aclrtSynchronizeStreamWithTimeout(executionStream, boundedSyncTimeoutMs(syncTimeout)) :
                       aclrtSynchronizeDeviceWithTimeout(boundedSyncTimeoutMs(syncTimeout));
    trace.syncElapsedMs = elapsedMs(syncStart);
    if (trace.syncRc != ACL_SUCCESS)
        trace.syncError = sanitizeRecoveryError(aclGetRecentErrMsg());

    if (trace.syncRc != ACL_SUCCESS) {
        out->status = CCU_AOT_E_LAUNCH;
        out->runtimeCode = static_cast<uint32_t>(trace.syncRc);
        if (executionStream)
            out->abortAction = CCU_AOT_ABORT_STREAM_ONLY;
        abortExecutionStream(executionStream, trace);

        if (shouldAbortDevice(trace.syncRc, requireDeviceRecovery, allowForcedDeviceAbort)) {
            abortDeviceTasks(ctx, rank, abortTimeout, *out, trace);
        } else {
            out->terminal = executionStream && trace.streamAbortRc == ACL_SUCCESS;
        }
    } else {
        out->terminal = 1;
        if (requireDeviceRecovery) {
            out->status = CCU_AOT_E_LAUNCH;
            out->abortAction = CCU_AOT_ABORT_DEVICE;
        }
    }

    if (trace.deviceAbortAttempted && trace.deviceAbortRc != ACL_SUCCESS) {
        recoverAfterFailedDeviceAbort(ctx, rank, abortTimeout, *out, trace);
    }

    if (trace.syncRc != ACL_SUCCESS) {
        queryExecutionStream(executionStream, trace);
        logRecoveryDetail(rank, ctx, *out, trace, executionStream, recoveryStart);
    }
    return out->status;
}

CcuAotError ccu_aot_sync_ex_on_stream(
    CcuAotContext* ctx, uint32_t rank, void* executionStream, uint64_t syncTimeout, uint64_t abortTimeout,
    uint32_t requireDeviceRecovery, CcuAotSyncResult* result)
{
    if (!executionStream || requireDeviceRecovery > 1)
        return CCU_AOT_E_PARAM;
    return ccuAotSyncImpl(
        ctx, rank, executionStream, syncTimeout, abortTimeout,
        /*allowForcedDeviceAbort=*/false, requireDeviceRecovery != 0, result);
}

static CcuAotError releaseContextChannels(CcuAotContext* ctx, uint32_t rank)
{
    if (!ctx || rank >= ctx->ndev || rank >= ctx->devices.size())
        return CCU_AOT_E_PARAM;
    std::vector<ChannelHandle> channels;
    if (rank < ctx->peerChannels.size())
        for (ChannelHandle channel : ctx->peerChannels[rank])
            if (channel != 0)
                channels.push_back(channel);
    std::sort(channels.begin(), channels.end());
    channels.erase(std::unique(channels.begin(), channels.end()), channels.end());
    if (channels.empty())
        return CCU_AOT_SUCCESS;

    if (ctx->borrowedChannels) {
        if (rank < ctx->peerChannels.size())
            std::fill(ctx->peerChannels[rank].begin(), ctx->peerChannels[rank].end(), 0);
        return CCU_AOT_SUCCESS;
    }

    HcclResult rc = runOnFreshDeviceThread(ctx->devices[rank], HCCL_E_INTERNAL, [&]() {
        return HcclChannelDestroy(ctx->comms[rank], channels.data(), static_cast<uint32_t>(channels.size()));
    });
    if (rc != 0) {
        CCU_AOT_LOGE("rank=%u HcclChannelDestroy count=%zu rc=%d\n", rank, channels.size(), (int)rc);
        return CCU_AOT_E_REGISTER;
    }
    if (rank < ctx->peerChannels.size())
        std::fill(ctx->peerChannels[rank].begin(), ctx->peerChannels[rank].end(), 0);
    return CCU_AOT_SUCCESS;
}

CcuAotError ccu_aot_finalize(CcuAotContext* ctx)
{
    if (!ctx)
        return CCU_AOT_E_PARAM;
    for (uint32_t id = 0; id < ctx->kernelState->registeredKernels.size(); ++id) {
        const auto& rk = ctx->kernelState->registeredKernels[id];
        const bool hasHandle = std::any_of(
            rk.rankRecords.begin(), rk.rankRecords.end(),
            [](const CcuAotKernelState::RegisteredKernel::RankKernelRecord& record) { return record.handle != 0; });
        if (!rk.active && !hasHandle)
            continue;
        CcuAotError releaseRc = releaseRegisteredKernel(ctx, id);
        if (releaseRc != CCU_AOT_SUCCESS) {
            CCU_AOT_LOGE(
                "finalize stage=kernel-release kernelId=%u rc=%d; "
                "context retained for retry\n",
                id, static_cast<int>(releaseRc));
            return releaseRc;
        }
    }
    // Tear down multi-rank control-plane resources by ownership layer. Never
    // destroy one communicator while another rank still owns a channel or an
    // execution stream from the same communication domain.
    for (uint32_t i = 0; i < ctx->ndev; ++i) {
        CcuAotError channelRc = releaseContextChannels(ctx, i);
        if (channelRc != CCU_AOT_SUCCESS) {
            CCU_AOT_LOGE(
                "finalize rank=%u stage=channel-release rc=%d; "
                "context retained for retry\n",
                i, static_cast<int>(channelRc));
            return channelRc;
        }
    }
    ctx->channelsAcquired = false;
    // The communicator and ACL lifetime belong to the caller.
    delete ctx;
    return CCU_AOT_SUCCESS;
}

extern "C" CcuAotError ccu_aot_bind_instance(CcuAotContext* ctx, uint32_t rank, uint64_t instanceHandle)
{
    if (!ctx || rank >= ctx->ndev || instanceHandle == 0)
        return CCU_AOT_E_PARAM;
    if (ctx->callerInstanceHandles.size() <= rank)
        ctx->callerInstanceHandles.resize(ctx->ndev, 0);
    CcuInsHandle& bound = ctx->callerInstanceHandles[rank];
    if (bound != 0 && bound != instanceHandle)
        return CCU_AOT_E_PARAM;
    bound = instanceHandle;
    CCU_AOT_LOGI("rank=%u bound caller-owned HComm instance=0x%lx\n", rank, static_cast<unsigned long>(instanceHandle));
    return CCU_AOT_SUCCESS;
}

} // extern "C"
