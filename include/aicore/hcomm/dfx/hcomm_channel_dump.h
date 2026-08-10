/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
#define ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_DFX_UNDEF_INCLUDE_INTERNAL_HEADERS
#endif

#include "hcomm_channel_dump_info.h"
#include "hcomm/hcomm_common.h"
#if __has_include("impl/adv_api/detail/hcomm/common/hcomm_inner_def.h")
#include "impl/adv_api/detail/hcomm/common/hcomm_inner_def.h"
#else
#include "src/aicore/hcomm/detail/common/hcomm_inner_def.h"
#endif

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <limits.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unistd.h>

#include "acl/acl.h"
#include "acl/acl_rt.h"

namespace AscendC {
namespace HcommDfx {

constexpr const char *QUEUE_READ_BINARY_NAME = "hcomm_dfx_queue_read_kernel_910_95.o";
constexpr const char *QUEUE_READ_KERNEL_NAME = "hcomm_dfx_queue_read_kernel";
constexpr uint32_t LAUNCH_ATTR_NUM = 3U;
constexpr uint32_t QUEUE_READ_TIMEOUT_US = 2000000U;
constexpr uint32_t QUEUE_LEADING_WINDOW = 4U;
constexpr uint32_t QUEUE_TAIL_WINDOW = 4U;

struct QueueReadKernel {
    aclrtBinHandle binHandle = nullptr;
    aclrtFuncHandle funcHandle = nullptr;
};

inline std::mutex g_queueReadMutex;
inline std::unordered_map<int32_t, QueueReadKernel> g_queueReadKernelByDevice;

inline bool IsTraceEnabled()
{
    const char *enabled = std::getenv("HCOMM_DFX_DUMP_TRACE");
    return enabled != nullptr && enabled[0] != '\0' && enabled[0] != '0';
}

inline void Trace(const char *format, ...)
{
    if (!IsTraceEnabled()) {
        return;
    }
    std::fprintf(stderr, "[HcommDfxDump][TRACE] ");
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

inline std::string GetCurrentHeaderRuntimeDir()
{
    Dl_info info;
    std::memset(&info, 0, sizeof(info));
    if (dladdr(reinterpret_cast<void *>(&GetCurrentHeaderRuntimeDir), &info) == 0 || info.dli_fname == nullptr) {
        return ".";
    }
    char realPath[PATH_MAX] = {};
    const char *path = realpath(info.dli_fname, realPath);
    std::string soPath = path == nullptr ? info.dli_fname : realPath;
    size_t slash = soPath.find_last_of('/');
    return slash == std::string::npos ? "." : soPath.substr(0, slash);
}

inline std::string GetQueueReadBinaryPath()
{
    const char *path = std::getenv("HCOMM_DFX_QUEUE_READ_KERNEL_PATH");
    if (path != nullptr && path[0] != '\0') {
        return path;
    }
    return GetCurrentHeaderRuntimeDir() + "/" + QUEUE_READ_BINARY_NAME;
}

inline bool LoadQueueReadBinary(const std::string &binaryPath, aclrtBinHandle &binHandle)
{
    char realPath[PATH_MAX] = {};
    if (realpath(binaryPath.c_str(), realPath) == nullptr) {
        Trace("queue read binary path invalid path=%s", binaryPath.c_str());
        return false;
    }

    aclrtBinaryLoadOption option;
    std::memset(&option, 0, sizeof(option));
    option.type = ACL_RT_BINARY_LOAD_OPT_LAZY_LOAD;
    option.value.cpuKernelMode = 1;
    aclrtBinaryLoadOptions options;
    std::memset(&options, 0, sizeof(options));
    options.numOpt = 1;
    options.options = &option;
    aclError ret = aclrtBinaryLoadFromFile(realPath, &options, &binHandle);
    if (ret != ACL_SUCCESS) {
        Trace("aclrtBinaryLoadFromFile failed path=%s ret=%d", realPath, ret);
        return false;
    }
    return true;
}

inline bool GetQueueReadKernel(QueueReadKernel &kernel)
{
    int32_t deviceId = 0;
    aclError ret = aclrtGetDevice(&deviceId);
    if (ret != ACL_SUCCESS) {
        Trace("aclrtGetDevice failed ret=%d", ret);
        return false;
    }

    std::lock_guard<std::mutex> lock(g_queueReadMutex);
    auto it = g_queueReadKernelByDevice.find(deviceId);
    if (it != g_queueReadKernelByDevice.end()) {
        kernel = it->second;
        return kernel.funcHandle != nullptr;
    }

    QueueReadKernel newKernel;
    const std::string binaryPath = GetQueueReadBinaryPath();
    if (!LoadQueueReadBinary(binaryPath, newKernel.binHandle)) {
        return false;
    }
    ret = aclrtBinaryGetFunction(newKernel.binHandle, QUEUE_READ_KERNEL_NAME, &newKernel.funcHandle);
    if (ret != ACL_SUCCESS || newKernel.funcHandle == nullptr) {
        Trace("aclrtBinaryGetFunction failed kernel=%s ret=%d", QUEUE_READ_KERNEL_NAME, ret);
        (void)aclrtBinaryUnLoad(newKernel.binHandle);
        return false;
    }
    g_queueReadKernelByDevice[deviceId] = newKernel;
    kernel = newKernel;
    Trace("loaded queue read kernel path=%s device=%d", binaryPath.c_str(), deviceId);
    return true;
}

template <typename T>
inline bool CopyFromDevice(uint64_t addr, T &value, const char *name)
{
    if (addr == 0) {
        return false;
    }
    std::memset(&value, 0, sizeof(T));
    aclError ret = aclrtMemcpy(&value, sizeof(T), reinterpret_cast<const void *>(addr), sizeof(T),
        ACL_MEMCPY_DEVICE_TO_HOST);
    if (ret != ACL_SUCCESS) {
        Trace("copy %s failed addr=0x%llx size=%llu ret=%d", name, static_cast<unsigned long long>(addr),
            static_cast<unsigned long long>(sizeof(T)), ret);
        return false;
    }
    return true;
}

class QueueReader {
public:
    QueueReader()
    {
        if (!GetQueueReadKernel(kernel_)) {
            return;
        }
        aclError ret = aclrtMalloc(reinterpret_cast<void **>(&deviceResult_), sizeof(HcommDfxQueueReadResult),
            ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) {
            Trace("aclrtMalloc queue read result failed ret=%d", ret);
            return;
        }
        ret = aclrtCreateStream(&stream_);
        if (ret != ACL_SUCCESS) {
            Trace("aclrtCreateStream queue read failed ret=%d", ret);
            (void)aclrtFree(deviceResult_);
            deviceResult_ = nullptr;
            return;
        }
        ready_ = true;
    }

    ~QueueReader()
    {
        if (stream_ != nullptr) {
            (void)aclrtDestroyStream(stream_);
        }
        if (deviceResult_ != nullptr) {
            (void)aclrtFree(deviceResult_);
        }
    }

    bool Read(uint64_t addr, uint8_t *data, uint32_t size)
    {
        if (!ready_ || addr == 0 || data == nullptr || size == 0 || size > HCOMM_DFX_QUEUE_ENTRY_BYTES) {
            return false;
        }

        aclrtLaunchKernelCfg cfg;
        aclrtLaunchKernelAttr attr[LAUNCH_ATTR_NUM];
        std::memset(&cfg, 0, sizeof(cfg));
        std::memset(attr, 0, sizeof(attr));
        attr[0].id = ACL_RT_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
        attr[0].value.schemMode = 1;
        attr[1].id = ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT_US;
        attr[1].value.timeoutUs.timeoutLow = QUEUE_READ_TIMEOUT_US;
        attr[1].value.timeoutUs.timeoutHigh = 0;
        attr[2].id = ACL_RT_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
        attr[2].value.engineType = ACL_RT_ENGINE_TYPE_AIV;
        cfg.numAttrs = LAUNCH_ATTR_NUM;
        cfg.attrs = attr;

        HcommDfxQueueReadArgs args = {addr, reinterpret_cast<uint64_t>(deviceResult_), size, 0};
        aclError ret = aclrtLaunchKernelWithHostArgs(kernel_.funcHandle, 1, stream_, &cfg, &args, sizeof(args), nullptr, 0);
        if (ret == ACL_SUCCESS) {
            ret = aclrtSynchronizeStream(stream_);
        }
        HcommDfxQueueReadResult result;
        std::memset(&result, 0, sizeof(result));
        if (ret == ACL_SUCCESS) {
            ret = aclrtMemcpy(&result, sizeof(result), deviceResult_, sizeof(result), ACL_MEMCPY_DEVICE_TO_HOST);
        }
        if (ret != ACL_SUCCESS || result.ret != HCOMM_DFX_QUEUE_READ_SUCCESS || result.size != size) {
            Trace("queue read failed addr=0x%llx ret=%d entryRet=%u size=%u expect=%u",
                static_cast<unsigned long long>(addr), ret, result.ret, result.size, size);
            return false;
        }
        std::memcpy(data, result.data, size);
        return true;
    }

private:
    QueueReadKernel kernel_;
    void *deviceResult_ = nullptr;
    aclrtStream stream_ = nullptr;
    bool ready_ = false;
};

inline uint64_t PtrToU64(const void *ptr)
{
    return reinterpret_cast<uint64_t>(ptr);
}

inline uint64_t GetBufferAddr(const RegedBufferEntity &buffer)
{
    return buffer.type == REGED_BUFFER_IPC ? buffer.bufferInfo.ipc.addr : buffer.bufferInfo.rma.addr;
}

inline uint64_t GetBufferSize(const RegedBufferEntity &buffer)
{
    return buffer.type == REGED_BUFFER_IPC ? buffer.bufferInfo.ipc.size : buffer.bufferInfo.rma.size;
}

inline uint64_t GetSqVa(const SqContext &ctx)
{
    if (ctx.type == SQ_CONTEXT_TYPE_UB_JFS) {
        return ctx.contextInfo.ubJfs.sqVa;
    }
    if (ctx.type == SQ_CONTEXT_TYPE_ROCE) {
        return ctx.contextInfo.roceSq.sqVa;
    }
    return 0;
}

inline uint64_t GetCqVa(const CqContext &ctx)
{
    if (ctx.type == CQ_CONTEXT_TYPE_UB_JFC) {
        return ctx.contextInfo.ubJfc.scqVa;
    }
    if (ctx.type == CQ_CONTEXT_TYPE_ROCE) {
        return ctx.contextInfo.roceCq.cqVa;
    }
    return 0;
}

inline uint32_t GetSqDepth(const SqContext &ctx)
{
    return ctx.type == SQ_CONTEXT_TYPE_UB_JFS ? ctx.contextInfo.ubJfs.sqDepth :
           (ctx.type == SQ_CONTEXT_TYPE_ROCE ? ctx.contextInfo.roceSq.depth : 0);
}

inline uint32_t GetCqDepth(const CqContext &ctx)
{
    return ctx.type == CQ_CONTEXT_TYPE_UB_JFC ? ctx.contextInfo.ubJfc.cqDepth :
           (ctx.type == CQ_CONTEXT_TYPE_ROCE ? ctx.contextInfo.roceCq.cqDepth : 0);
}

inline uint32_t GetSqEntrySize(const SqContext &ctx)
{
    return ctx.type == SQ_CONTEXT_TYPE_UB_JFS ? ctx.contextInfo.ubJfs.wqeSize :
           (ctx.type == SQ_CONTEXT_TYPE_ROCE ? ctx.contextInfo.roceSq.wqeSize : 0);
}

inline uint32_t GetCqEntrySize(const CqContext &ctx)
{
    return ctx.type == CQ_CONTEXT_TYPE_UB_JFC ? ctx.contextInfo.ubJfc.cqeSize :
           (ctx.type == CQ_CONTEXT_TYPE_ROCE ? ctx.contextInfo.roceCq.cqeSize : 0);
}

inline uint64_t GetSqHeadAddr(const SqContext &ctx)
{
    return ctx.type == SQ_CONTEXT_TYPE_UB_JFS ? ctx.contextInfo.ubJfs.headAddr :
           (ctx.type == SQ_CONTEXT_TYPE_ROCE ? ctx.contextInfo.roceSq.headAddr : 0);
}

inline uint64_t GetSqTailAddr(const SqContext &ctx)
{
    return ctx.type == SQ_CONTEXT_TYPE_UB_JFS ? ctx.contextInfo.ubJfs.tailAddr :
           (ctx.type == SQ_CONTEXT_TYPE_ROCE ? ctx.contextInfo.roceSq.tailAddr : 0);
}

inline uint64_t GetCqHeadAddr(const CqContext &ctx)
{
    return ctx.type == CQ_CONTEXT_TYPE_UB_JFC ? ctx.contextInfo.ubJfc.headAddr :
           (ctx.type == CQ_CONTEXT_TYPE_ROCE ? ctx.contextInfo.roceCq.headAddr : 0);
}

inline uint64_t GetCqTailAddr(const CqContext &ctx)
{
    return ctx.type == CQ_CONTEXT_TYPE_UB_JFC ? ctx.contextInfo.ubJfc.tailAddr :
           (ctx.type == CQ_CONTEXT_TYPE_ROCE ? ctx.contextInfo.roceCq.tailAddr : 0);
}

inline uint64_t GetSqDbVa(const SqContext &ctx)
{
    return ctx.type == SQ_CONTEXT_TYPE_UB_JFS ? ctx.contextInfo.ubJfs.dbVa : 0;
}

inline uint64_t GetCqDbVa(const CqContext &ctx)
{
    return ctx.type == CQ_CONTEXT_TYPE_UB_JFC ? ctx.contextInfo.ubJfc.dbVa : 0;
}

inline bool ReadQueueCounter(uint64_t addr, uint32_t &value, const char *name)
{
    value = 0;
    if (addr == 0) {
        return false;
    }
    return CopyFromDevice(addr, value, name);
}

inline void CollectBuffers(uint64_t addr, uint32_t num, HcommDfxBufferInfo *buffers, uint32_t &dumpNum)
{
    dumpNum = 0;
    if (addr == 0 || buffers == nullptr) {
        return;
    }
    const uint32_t limit = std::min<uint32_t>(num, HCOMM_DFX_MAX_BUFFERS);
    for (uint32_t idx = 0; idx < limit; ++idx) {
        RegedBufferEntity buffer;
        if (!CopyFromDevice(addr + static_cast<uint64_t>(idx) * sizeof(RegedBufferEntity), buffer, "RegedBufferEntity")) {
            continue;
        }
        buffers[dumpNum].type = static_cast<int32_t>(buffer.type);
        buffers[dumpNum].addr = GetBufferAddr(buffer);
        buffers[dumpNum].size = GetBufferSize(buffer);
        ++dumpNum;
    }
}

inline bool AppendQueueEntry(HcommDfxChannelInfo &channel, QueueReader &reader, uint32_t entryType, uint32_t queueIndex,
    uint32_t entryIndex, uint32_t cursor, uint64_t addr, uint32_t size)
{
    if (channel.queueEntryDumpNum >= HCOMM_DFX_MAX_QUEUE_ENTRIES || addr == 0 || size == 0) {
        return false;
    }
    for (uint32_t idx = 0; idx < channel.queueEntryDumpNum; ++idx) {
        const HcommDfxQueueEntry &entry = channel.queueEntries[idx];
        if (entry.entryType == entryType && entry.queueIndex == queueIndex && entry.entryIndex == entryIndex &&
            entry.addr == addr) {
            return entry.valid != 0;
        }
    }
    HcommDfxQueueEntry &entry = channel.queueEntries[channel.queueEntryDumpNum++];
    entry.entryType = entryType;
    entry.queueIndex = queueIndex;
    entry.entryIndex = entryIndex;
    entry.cursor = cursor;
    entry.addr = addr;
    entry.size = std::min<uint32_t>(size, HCOMM_DFX_QUEUE_ENTRY_BYTES);
    entry.valid = reader.Read(addr, entry.data, entry.size) ? 1U : 0U;
    return entry.valid != 0;
}

inline void CollectSqContexts(uint64_t addr, const ChannelEntity &entity, HcommDfxChannelInfo &channel,
    QueueReader &reader)
{
    (void)entity;
    channel.sqContextDumpNum = 0;
    if (addr == 0) {
        return;
    }
    const uint32_t limit = std::min<uint32_t>(entity.sqNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < limit; ++idx) {
        SqContext ctx;
        if (!CopyFromDevice(addr + static_cast<uint64_t>(idx) * sizeof(SqContext), ctx, "SqContext")) {
            continue;
        }
        HcommDfxQueueContext &out = channel.sqContexts[channel.sqContextDumpNum++];
        out.type = static_cast<int32_t>(ctx.type);
        out.va = GetSqVa(ctx);
        out.headAddr = GetSqHeadAddr(ctx);
        out.tailAddr = GetSqTailAddr(ctx);
        out.dbVa = GetSqDbVa(ctx);
        out.depth = GetSqDepth(ctx);
        out.entrySize = GetSqEntrySize(ctx);
        out.headValid = ReadQueueCounter(out.headAddr, out.head, "SqHead") ? 1U : 0U;
        out.tailValid = ReadQueueCounter(out.tailAddr, out.tail, "SqTail") ? 1U : 0U;
        out.rawSize = std::min<uint32_t>(sizeof(SqContext), HCOMM_DFX_CONTEXT_BYTES);
        std::memcpy(out.raw, &ctx, out.rawSize);
        if (ctx.type != SQ_CONTEXT_TYPE_UB_JFS || out.va == 0 || out.depth == 0 || out.entrySize == 0) {
            continue;
        }
        const uint32_t leading = std::min<uint32_t>(out.depth, QUEUE_LEADING_WINDOW);
        for (uint32_t entryIdx = 0; entryIdx < leading; ++entryIdx) {
            uint32_t bytes = out.entrySize <= HCOMM_DFX_QUEUE_ENTRY_BYTES / 2U ? out.entrySize * 2U : out.entrySize;
            (void)AppendQueueEntry(channel, reader, HCOMM_DFX_ENTRY_URMA_WQE, idx, entryIdx, entryIdx,
                out.va + static_cast<uint64_t>(entryIdx) * out.entrySize, bytes);
        }
        const uint32_t dumpNum = out.head == 0 ? 1U : std::min<uint32_t>(out.head, QUEUE_TAIL_WINDOW);
        const uint32_t start = out.head >= dumpNum ? out.head - dumpNum : 0U;
        for (uint32_t entryIdx = 0; entryIdx < dumpNum; ++entryIdx) {
            const uint32_t slot = (start + entryIdx) % out.depth;
            uint32_t bytes = out.entrySize <= HCOMM_DFX_QUEUE_ENTRY_BYTES / 2U ? out.entrySize * 2U : out.entrySize;
            (void)AppendQueueEntry(channel, reader, HCOMM_DFX_ENTRY_URMA_WQE, idx, slot, start + entryIdx,
                out.va + static_cast<uint64_t>(slot) * out.entrySize, bytes);
        }
    }
}

inline void CollectCqContexts(uint64_t addr, const ChannelEntity &entity, HcommDfxChannelInfo &channel,
    QueueReader &reader)
{
    (void)entity;
    channel.cqContextDumpNum = 0;
    if (addr == 0) {
        return;
    }
    const uint32_t limit = std::min<uint32_t>(entity.cqNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < limit; ++idx) {
        CqContext ctx;
        if (!CopyFromDevice(addr + static_cast<uint64_t>(idx) * sizeof(CqContext), ctx, "CqContext")) {
            continue;
        }
        HcommDfxQueueContext &out = channel.cqContexts[channel.cqContextDumpNum++];
        out.type = static_cast<int32_t>(ctx.type);
        out.va = GetCqVa(ctx);
        out.headAddr = GetCqHeadAddr(ctx);
        out.tailAddr = GetCqTailAddr(ctx);
        out.dbVa = GetCqDbVa(ctx);
        out.depth = GetCqDepth(ctx);
        out.entrySize = GetCqEntrySize(ctx);
        out.headValid = ReadQueueCounter(out.headAddr, out.head, "CqHead") ? 1U : 0U;
        out.tailValid = ReadQueueCounter(out.tailAddr, out.tail, "CqTail") ? 1U : 0U;
        out.rawSize = std::min<uint32_t>(sizeof(CqContext), HCOMM_DFX_CONTEXT_BYTES);
        std::memcpy(out.raw, &ctx, out.rawSize);
        if (ctx.type != CQ_CONTEXT_TYPE_UB_JFC || out.va == 0 || out.depth == 0 || out.entrySize == 0) {
            continue;
        }
        const uint32_t leading = std::min<uint32_t>(out.depth, QUEUE_LEADING_WINDOW);
        for (uint32_t entryIdx = 0; entryIdx < leading; ++entryIdx) {
            (void)AppendQueueEntry(channel, reader, HCOMM_DFX_ENTRY_URMA_CQE, idx, entryIdx, entryIdx,
                out.va + static_cast<uint64_t>(entryIdx) * out.entrySize, out.entrySize);
        }
        const uint32_t dumpNum = out.tail < (QUEUE_TAIL_WINDOW - 1U) ? out.tail + 1U : QUEUE_TAIL_WINDOW;
        const uint32_t start = out.tail >= dumpNum ? out.tail - dumpNum + 1U : 0U;
        for (uint32_t entryIdx = 0; entryIdx < dumpNum; ++entryIdx) {
            const uint32_t cursor = start + entryIdx;
            const uint32_t slot = cursor % out.depth;
            (void)AppendQueueEntry(channel, reader, HCOMM_DFX_ENTRY_URMA_CQE, idx, slot, cursor,
                out.va + static_cast<uint64_t>(slot) * out.entrySize, out.entrySize);
        }
    }
}

inline bool CollectChannel(ChannelHandle channelHandle, HcommDfxChannelInfo &channel, QueueReader &reader)
{
    std::memset(&channel, 0, sizeof(channel));
    channel.handle = channelHandle;
    if (channelHandle == 0) {
        return false;
    }

    ChannelEntity entity;
    if (!CopyFromDevice(channelHandle, entity, "ChannelEntity")) {
        return false;
    }
    channel.valid = 1U;
    channel.channelEntitySize = std::min<uint32_t>(sizeof(ChannelEntity), HCOMM_DFX_CHANNEL_ENTITY_BYTES);
    std::memcpy(channel.channelEntity, &entity, channel.channelEntitySize);
    channel.engine = static_cast<int32_t>(entity.engine);
    channel.protocol = entity.protocol;
    channel.localBufferNum = entity.localBufferNum;
    channel.remoteBufferNum = entity.remoteBufferNum;
    channel.sqNum = entity.sqNum;
    channel.cqNum = entity.cqNum;

    CollectBuffers(PtrToU64(entity.localBufferAddr), entity.localBufferNum, channel.localBuffers,
        channel.localBufferDumpNum);
    CollectBuffers(PtrToU64(entity.remoteBufferAddr), entity.remoteBufferNum, channel.remoteBuffers,
        channel.remoteBufferDumpNum);
    CollectSqContexts(PtrToU64(entity.sqContextAddr), entity, channel, reader);
    CollectCqContexts(PtrToU64(entity.cqContextAddr), entity, channel, reader);
    if (channel.sqContextDumpNum > 0) {
        channel.sqHead = channel.sqContexts[0].head;
        channel.sqTail = channel.sqContexts[0].tail;
    }
    if (channel.cqContextDumpNum > 0) {
        channel.cqHead = channel.cqContexts[0].head;
        channel.cqTail = channel.cqContexts[0].tail;
    }
    return true;
}

inline void PrintRaw16(const HcommDfxQueueEntry &entry)
{
    uint32_t rawSize = entry.size < 16U ? entry.size : 16U;
    for (uint32_t idx = 0; idx < rawSize; ++idx) {
        std::printf(" %02x", entry.data[idx]);
    }
}

inline int32_t ParseAndPrintChannelDump(const HcommDfxChannelDumpData &dump)
{
    if (dump.magic != HCOMM_DFX_CHANNEL_DUMP_MAGIC || dump.size < sizeof(HcommDfxChannelDumpData)) {
        std::printf("[HcommDfxDump][parsed] invalid dump magic=0x%x size=%u\n", dump.magic, dump.size);
        return HCOMM_FAILED;
    }
    for (uint32_t channelIdx = 0; channelIdx < dump.channelNum; ++channelIdx) {
        const auto &channel = dump.channels[channelIdx];
        std::printf("[HcommDfxDump][parsed] Channel[%u] handle=0x%llx valid=%u engine=%d protocol=%d "
                    "localBufferNum=%u remoteBufferNum=%u sqNum=%u cqNum=%u sqHead=%u sqTail=%u "
                    "cqHead=%u cqTail=%u\n",
            channelIdx, static_cast<unsigned long long>(channel.handle), channel.valid, channel.engine,
            channel.protocol, channel.localBufferNum, channel.remoteBufferNum, channel.sqNum, channel.cqNum,
            channel.sqHead, channel.sqTail, channel.cqHead, channel.cqTail);
        for (uint32_t idx = 0; idx < channel.localBufferDumpNum; ++idx) {
            const auto &buffer = channel.localBuffers[idx];
            std::printf("[HcommDfxDump][parsed] localBuffer[%u] type=%d addr=0x%llx size=%llu\n", idx,
                buffer.type, static_cast<unsigned long long>(buffer.addr), static_cast<unsigned long long>(buffer.size));
        }
        for (uint32_t idx = 0; idx < channel.remoteBufferDumpNum; ++idx) {
            const auto &buffer = channel.remoteBuffers[idx];
            std::printf("[HcommDfxDump][parsed] remoteBuffer[%u] type=%d addr=0x%llx size=%llu\n", idx,
                buffer.type, static_cast<unsigned long long>(buffer.addr), static_cast<unsigned long long>(buffer.size));
        }
        for (uint32_t idx = 0; idx < channel.sqContextDumpNum; ++idx) {
            const auto &sq = channel.sqContexts[idx];
            std::printf("[HcommDfxDump][parsed] SqContext[%u] type=%d sqVa=0x%llx depth=%u entrySize=%u "
                        "head=%u tail=%u dbVa=0x%llx\n",
                idx, sq.type, static_cast<unsigned long long>(sq.va), sq.depth, sq.entrySize, sq.head, sq.tail,
                static_cast<unsigned long long>(sq.dbVa));
        }
        for (uint32_t idx = 0; idx < channel.cqContextDumpNum; ++idx) {
            const auto &cq = channel.cqContexts[idx];
            std::printf("[HcommDfxDump][parsed] CqContext[%u] type=%d cqVa=0x%llx depth=%u entrySize=%u "
                        "head=%u tail=%u dbVa=0x%llx\n",
                idx, cq.type, static_cast<unsigned long long>(cq.va), cq.depth, cq.entrySize, cq.head, cq.tail,
                static_cast<unsigned long long>(cq.dbVa));
        }
        for (uint32_t idx = 0; idx < channel.queueEntryDumpNum; ++idx) {
            const auto &entry = channel.queueEntries[idx];
            const char *type = entry.entryType == HCOMM_DFX_ENTRY_URMA_WQE ? "WQE" : "CQE";
            std::printf("[HcommDfxDump][parsed] Hcomm URMA %s queue=%u index=%u cursor=%u addr=0x%llx "
                        "size=%u valid=%u raw[000]",
                type, entry.queueIndex, entry.entryIndex, entry.cursor, static_cast<unsigned long long>(entry.addr),
                entry.size, entry.valid);
            if (entry.valid != 0) {
                PrintRaw16(entry);
            }
            std::printf("\n");
        }
    }
    return HCOMM_SUCCESS;
}

} // namespace HcommDfx

inline int32_t HcommDfxGetChannelDumpInfoSize(uint32_t channelNum, const ChannelHandle *channels, uint32_t *len)
{
    if (len == nullptr || (channelNum != 0 && channels == nullptr)) {
        return HCOMM_FAILED;
    }
    *len = sizeof(HcommDfxChannelDumpData);
    return HCOMM_SUCCESS;
}

inline int32_t HcommDfxDumpChannelInfo(uint32_t channelNum, const ChannelHandle *channels, void *buffer, uint32_t len)
{
    if (channelNum == 0 || channels == nullptr || buffer == nullptr || len < sizeof(HcommDfxChannelDumpData)) {
        return HCOMM_FAILED;
    }

    auto *dump = reinterpret_cast<HcommDfxChannelDumpData *>(buffer);
    std::memset(dump, 0, sizeof(HcommDfxChannelDumpData));
    dump->magic = HCOMM_DFX_CHANNEL_DUMP_MAGIC;
    dump->version = HCOMM_DFX_CHANNEL_DUMP_VERSION;
    dump->size = sizeof(HcommDfxChannelDumpData);
    dump->channelNum = std::min<uint32_t>(channelNum, HCOMM_DFX_MAX_CHANNELS);

    HcommDfx::QueueReader reader;
    int32_t ret = HCOMM_SUCCESS;
    for (uint32_t idx = 0; idx < dump->channelNum; ++idx) {
        if (!HcommDfx::CollectChannel(channels[idx], dump->channels[idx], reader)) {
            ret = HCOMM_FAILED;
        }
    }
    return ret;
}

inline int32_t HcommDfxParseAndPrintChannelInfo(const void *buffer, uint32_t len)
{
    if (buffer == nullptr || len < sizeof(HcommDfxChannelDumpData)) {
        return HCOMM_FAILED;
    }
    const auto *dump = reinterpret_cast<const HcommDfxChannelDumpData *>(buffer);
    if (dump->size > len) {
        return HCOMM_FAILED;
    }
    return HcommDfx::ParseAndPrintChannelDump(*dump);
}

} // namespace AscendC

#if defined(HCOMM_DFX_UNDEF_INCLUDE_INTERNAL_HEADERS)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_DFX_UNDEF_INCLUDE_INTERNAL_HEADERS
#endif

#endif // ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
