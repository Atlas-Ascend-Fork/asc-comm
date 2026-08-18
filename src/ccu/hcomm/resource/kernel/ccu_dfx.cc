/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel_mgr.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

#include "hcomm/resource/representation/ccu_rep_v1.h"

namespace hcomm {
namespace {

using CcuRep::CcuRepBase;
using CcuRep::CcuRepType;

struct LoopXm {
    uint64_t loopCnt : 13;
    uint64_t gsaStride : 32;
    uint64_t loopCtxId : 8;
    uint64_t reserved : 11;
};

union LoopGroupXn {
    uint64_t value;
    struct {
        uint64_t reservedLow : 41;
        uint64_t loopInsCnt : 7;
        uint64_t expandOffset : 7;
        uint64_t expandCnt : 7;
        uint64_t reservedHigh : 2;
    } fields;
};

class DfxBuilder {
public:
    DfxBuilder(const HcommCcuDfxRequestPod &request, HcommCcuDfxEmitFn emit, void *context,
        const HcommCcuControlOpsPod &ops)
        : request_(request), emit_(emit), context_(context), ops_(ops)
    {
    }

    int32_t Run()
    {
        HcommCcuMissionContextPod mission{};
        if (ops_.missionContextQuery == nullptr ||
            ops_.missionContextQuery(request_.deviceId, request_.dieId, request_.execMissionId, &mission) != 0) {
            return static_cast<int32_t>(CcuResult::CCU_E_INTERNAL);
        }
        CcuKernel *kernel = CcuKernelMgr::GetInstance(request_.deviceId).GetKernel(request_.kernelHandle);
        if (kernel == nullptr) {
            return static_cast<int32_t>(CcuResult::CCU_E_NOT_FOUND);
        }
        EmitMission(mission.currentInstructionId);
        auto current = ResolveRep(*kernel, mission.currentInstructionId);
        if (current == nullptr) {
            return static_cast<int32_t>(CcuResult::CCU_E_NOT_FOUND);
        }
        auto previous = mission.currentInstructionId == 0 ? nullptr : kernel->GetRepByInstrId(mission.currentInstructionId - 1);
        if ((previous != nullptr && previous->Type() == CcuRepType::LOOPGROUP) ||
            current->Type() == CcuRepType::LOOPGROUP) {
            EmitLoopGroup(*kernel, previous != nullptr && previous->Type() == CcuRepType::LOOPGROUP ? previous : current);
        } else {
            EmitRep(current);
        }

        const uint16_t distance = 10;
        uint16_t begin = mission.currentInstructionId > distance ? mission.currentInstructionId - distance : 0;
        begin = std::max(begin, mission.startInstructionId);
        for (int32_t id = mission.currentInstructionId; id >= begin; --id) {
            if (kernel->GetRepByInstrId(static_cast<uint16_t>(id)) == nullptr) {
                begin = static_cast<uint16_t>(id + 1);
                break;
            }
        }
        for (uint32_t id = begin; id <= mission.currentInstructionId; ++id) {
            auto rep = kernel->GetRepByInstrId(static_cast<uint16_t>(id));
            if (rep != nullptr) {
                EmitRep(rep);
            }
        }
        return firstError_;
    }

private:
    HcommCcuDfxRecordPod Base(const std::shared_ptr<CcuRepBase> &rep, uint32_t type) const
    {
        HcommCcuDfxRecordPod record{};
        record.type = type;
        record.repType = static_cast<int32_t>(rep->Type());
        record.dieId = static_cast<uint8_t>(request_.dieId);
        record.missionId = static_cast<uint8_t>(request_.missionId);
        record.instrId = rep->StartInstrId();
        return record;
    }

    void Emit(HcommCcuDfxRecordPod &record)
    {
        if (firstError_ == 0) {
            firstError_ = emit_(context_, &record);
        }
    }

    uint64_t Resource(HcommCcuResourceQueryFn query, uint32_t id)
    {
        uint64_t value = UINT64_MAX;
        if (query == nullptr || query(request_.deviceId, request_.dieId, id, &value) != 0) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_INTERNAL) : firstError_;
        }
        return value;
    }

    // 单阶段查询 Channel POD 快照：先填充 ABI 头，再调用控制面注入的 channelQuery
    bool QueryChannel(ChannelHandle handle, HcommCcuChannelPod &channel)
    {
        channel.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
        channel.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
        channel.header.size = sizeof(HcommCcuChannelPod);
        if (ops_.channelQuery == nullptr || ops_.channelQuery(handle, &channel) != 0) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_INTERNAL) : firstError_;
            return false;
        }
        return true;
    }

    void SetChannel(ChannelHandle handle, uint16_t &id, ChannelHandle &recordHandle)
    {
        HcommCcuChannelPod channel{};
        recordHandle = handle;
        id = UINT16_MAX;
        // 查询失败时保留哨兵值 id=UINT16_MAX，交由上层按无效 channel 处理
        if (!QueryChannel(handle, channel)) {
            return;
        }
        // channelId 收窄为 uint16_t 前先做范围校验，避免静默截断
        if (channel.channelId > std::numeric_limits<uint16_t>::max()) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_PARA) : firstError_;
            return;
        }
        id = static_cast<uint16_t>(channel.channelId);
    }

    // 从 POD 定长数组取第 index 个资源 id，收窄为 uint16_t 前做指针/越界/溢出校验
    uint16_t IdByArray(const uint32_t *ids, uint16_t index)
    {
        if (ids == nullptr || index >= HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY ||
            ids[index] > std::numeric_limits<uint16_t>::max()) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_PARA) : firstError_;
            return UINT16_MAX;
        }
        return static_cast<uint16_t>(ids[index]);
    }

    // 取本端/远端 CKE（信号量）id：remote 为 true 时读远端数组，否则读本端数组
    uint16_t ChannelSignal(ChannelHandle handle, uint16_t index, bool remote)
    {
        HcommCcuChannelPod channel{};
        if (!QueryChannel(handle, channel)) {
            return UINT16_MAX;
        }
        return IdByArray(remote ? channel.remoteCkeIds : channel.localCkeIds, index);
    }

    // 取远端 XN（变量槽）id，用于分析 RV 类远程 POST 的变量地址
    uint16_t ChannelRemoteXn(ChannelHandle handle, uint16_t index)
    {
        HcommCcuChannelPod channel{};
        if (!QueryChannel(handle, channel)) {
            return UINT16_MAX;
        }
        return IdByArray(channel.remoteXnIds, index);
    }

    std::shared_ptr<CcuRepBase> ResolveRep(CcuKernel &kernel, uint16_t id)
    {
        auto rep = kernel.GetRepByInstrId(id);
        while (rep != nullptr && rep->Type() == CcuRepType::FUNC_BLOCK) {
            rep = std::static_pointer_cast<CcuRep::CcuRepBlock>(rep)->GetRepByInstrId(id);
        }
        return rep;
    }

    void EmitMission(uint16_t instructionId)
    {
        HcommCcuDfxRecordPod record{};
        record.type = HCOMM_CCU_DFX_RECORD_MISSION;
        record.repType = static_cast<int32_t>(CcuRepType::BASE);
        record.dieId = static_cast<uint8_t>(request_.dieId);
        record.missionId = static_cast<uint8_t>(request_.missionId);
        record.instrId = instructionId;
        Emit(record);
    }

    void EmitWait(const std::shared_ptr<CcuRepBase> &base, uint16_t signal, uint16_t mask, bool readValue)
    {
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        record.msg.waitSignal.signalId = signal;
        record.msg.waitSignal.signalMask = mask;
        record.msg.waitSignal.signalValue = readValue ? static_cast<uint16_t>(Resource(ops_.ckeQuery, signal)) : 0;
        std::fill(std::begin(record.msg.waitSignal.channelId), std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        Emit(record);
    }

    template <typename Rep>
    void EmitRemoteWait(const std::shared_ptr<CcuRepBase> &base, const std::shared_ptr<Rep> &rep, bool readValue)
    {
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        SetChannel(rep->GetChannel(), record.msg.waitSignal.channelId[0], record.msg.waitSignal.channelHandle[0]);
        record.msg.waitSignal.signalId = ChannelSignal(rep->GetChannel(), rep->GetSemIndex(), !readValue);
        record.msg.waitSignal.signalMask = rep->GetMask();
        record.msg.waitSignal.signalValue = readValue ?
            static_cast<uint16_t>(Resource(ops_.ckeQuery, record.msg.waitSignal.signalId)) : 0;
        std::fill(std::begin(record.msg.waitSignal.channelId) + 1, std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        Emit(record);
    }

    void EmitRemotePostVar(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepRemPostVar>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        SetChannel(rep->GetChannel(), record.msg.waitSignal.channelId[0], record.msg.waitSignal.channelHandle[0]);
        std::fill(std::begin(record.msg.waitSignal.channelId) + 1, std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        record.msg.waitSignal.signalId = ChannelSignal(rep->GetChannel(), rep->GetSemIndex(), true);
        record.msg.waitSignal.signalMask = rep->GetMask();
        record.msg.waitSignal.paramId = ChannelRemoteXn(rep->GetChannel(), rep->GetParamIndex());
        record.msg.waitSignal.paramValue = Resource(ops_.xnQuery, rep->GetParam().Id());
        Emit(record);
    }

    template <typename Rep>
    void EmitTransfer(const std::shared_ptr<CcuRepBase> &base, const std::shared_ptr<Rep> &rep)
    {
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_TRANSFER);
        record.msg.transMem.locAddr = Resource(ops_.gsaQuery, rep->GetLocAddrId());
        record.msg.transMem.locToken = Resource(ops_.xnQuery, rep->GetLocTokenId());
        record.msg.transMem.rmtAddr = Resource(ops_.gsaQuery, rep->GetRemAddrId());
        record.msg.transMem.rmtToken = Resource(ops_.xnQuery, rep->GetRemTokenId());
        record.msg.transMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.transMem.signalId = rep->GetSemId();
        record.msg.transMem.signalMask = rep->GetMask();
        SetChannel(rep->GetChannel(), record.msg.transMem.channelId, record.msg.transMem.channelHandle);
        record.msg.transMem.dataType = rep->GetDataType();
        record.msg.transMem.opType = rep->GetOpType();
        Emit(record);
    }

    void EmitLocalCopy(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepLocCpy>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_TRANSFER);
        record.msg.transMem.locAddr = Resource(ops_.gsaQuery, rep->GetSrcAddrId());
        record.msg.transMem.locToken = Resource(ops_.xnQuery, rep->GetSrcTokenId());
        record.msg.transMem.rmtAddr = Resource(ops_.gsaQuery, rep->GetDstAddrId());
        record.msg.transMem.rmtToken = Resource(ops_.xnQuery, rep->GetDstTokenId());
        record.msg.transMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.transMem.signalId = rep->GetSemId();
        record.msg.transMem.signalMask = rep->GetMask();
        record.msg.transMem.dataType = rep->GetDataType();
        record.msg.transMem.opType = rep->GetOpType();
        Emit(record);
    }

    void EmitBufferRead(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufRead>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->GetDstAddrId() & 0x7fffU;
        record.msg.bufTransMem.addr = Resource(ops_.gsaQuery, rep->GetSrcAddrId());
        record.msg.bufTransMem.token = Resource(ops_.xnQuery, rep->GetSrcTokenId());
        record.msg.bufTransMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.bufTransMem.signalId = rep->GetSemId();
        record.msg.bufTransMem.signalMask = rep->GetMask();
        SetChannel(rep->GetChannel(), record.msg.bufTransMem.channelId, record.msg.bufTransMem.channelHandle);
        Emit(record);
    }

    void EmitBufferWrite(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufWrite>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->GetSrcId() & 0x7fffU;
        record.msg.bufTransMem.addr = Resource(ops_.gsaQuery, rep->GetDstAddrId());
        record.msg.bufTransMem.token = Resource(ops_.xnQuery, rep->GetDstTokenId());
        record.msg.bufTransMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.bufTransMem.signalId = rep->GetSemId();
        record.msg.bufTransMem.signalMask = rep->GetMask();
        SetChannel(rep->GetChannel(), record.msg.bufTransMem.channelId, record.msg.bufTransMem.channelHandle);
        Emit(record);
    }

    void EmitBufferLocalRead(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufLocRead>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->GetDstId() & 0x7fffU;
        record.msg.bufTransMem.addr = Resource(ops_.gsaQuery, rep->GetSrcAddrId());
        record.msg.bufTransMem.token = Resource(ops_.xnQuery, rep->GetSrcTokenId());
        record.msg.bufTransMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.bufTransMem.signalId = rep->GetSemId();
        record.msg.bufTransMem.signalMask = rep->GetMask();
        record.msg.bufTransMem.channelId = UINT16_MAX;
        Emit(record);
    }

    void EmitBufferLocalWrite(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufLocWrite>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->GetSrcAddrId() & 0x7fffU;
        record.msg.bufTransMem.addr = Resource(ops_.gsaQuery, rep->GetDstAddrId());
        record.msg.bufTransMem.token = Resource(ops_.xnQuery, rep->GetDstTokenId());
        record.msg.bufTransMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.bufTransMem.signalId = rep->GetSemId();
        record.msg.bufTransMem.signalMask = rep->GetMask();
        record.msg.bufTransMem.channelId = UINT16_MAX;
        Emit(record);
    }

    void EmitBufferReduce(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufReduce>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_REDUCE);
        std::fill(std::begin(record.msg.bufferReduce.bufIds), std::end(record.msg.bufferReduce.bufIds), UINT16_MAX);
        const auto &buffers = rep->GetMem();
        for (size_t i = 0; i < std::min(buffers.size(), static_cast<size_t>(HCOMM_CCU_DFX_BUFFER_CAPACITY)); ++i) {
            record.msg.bufferReduce.bufIds[i] = buffers[i].Id() & 0x7fffU;
        }
        record.msg.bufferReduce.count = rep->GetCount();
        record.msg.bufferReduce.dataType = rep->GetDataType();
        record.msg.bufferReduce.outputDataType = rep->GetOutputDataType();
        record.msg.bufferReduce.opType = rep->GetOpType();
        record.msg.bufferReduce.signalId = rep->GetSemId();
        record.msg.bufferReduce.signalMask = rep->GetMask();
        record.msg.bufferReduce.xnIdLength = rep->GetXnLengthId();
        Emit(record);
    }

    void EmitLoop(CcuKernel &kernel, uint16_t instructionId)
    {
        auto base = kernel.GetRepByInstrId(instructionId);
        if (base == nullptr || base->Type() != CcuRepType::LOOP) {
            return;
        }
        auto rep = std::static_pointer_cast<CcuRep::CcuRepLoop>(base);
        union { uint64_t value; LoopXm fields; } loop{};
        loop.value = Resource(ops_.xnQuery, rep->GetLoopParam()->Id());
        HcommCcuLoopContextPod context{};
        if (ops_.loopContextQuery == nullptr ||
            ops_.loopContextQuery(request_.deviceId, request_.dieId, loop.fields.loopCtxId, &context) != 0) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_INTERNAL) : firstError_;
            return;
        }
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_LOOP);
        record.msg.loop.startInstrId = rep->GetLoopBlock()->StartInstrId();
        record.msg.loop.endInstrId = record.msg.loop.startInstrId + rep->GetLoopBlock()->InstrCount() - 1;
        record.msg.loop.loopEngineId = loop.fields.loopCtxId;
        record.msg.loop.loopCnt = loop.fields.loopCnt;
        record.msg.loop.loopCurrentCnt = context.currentCount;
        record.msg.loop.addrStride = context.addressStride;
        Emit(record);
        for (uint32_t id = record.msg.loop.startInstrId; id <= record.msg.loop.endInstrId; ++id) {
            EmitRep(rep->GetLoopBlock()->GetRepByInstrId(static_cast<uint16_t>(id)));
        }
    }

    void EmitLoopGroup(CcuKernel &kernel, const std::shared_ptr<CcuRepBase> &base)
    {
        if (base == nullptr) {
            return;
        }
        auto rep = std::static_pointer_cast<CcuRep::CcuRepLoopGroupBundle>(base);
        LoopGroupXn group{};
        group.value = Resource(ops_.xnQuery, rep->GetOffsetParam().Id());
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_LOOP_GROUP);
        record.msg.loopGroup.startLoopInsId = rep->GetStartLoopInstrId();
        record.msg.loopGroup.loopInsCnt = group.fields.loopInsCnt;
        record.msg.loopGroup.expandOffset = group.fields.expandOffset;
        record.msg.loopGroup.expandCount = group.fields.expandCnt;
        Emit(record);
        for (uint32_t i = 0; i < group.fields.loopInsCnt; ++i) {
            EmitLoop(kernel, static_cast<uint16_t>(record.msg.loopGroup.startLoopInsId + i));
        }
    }

    void EmitRep(const std::shared_ptr<CcuRepBase> &base)
    {
        if (base == nullptr) {
            return;
        }
        switch (base->Type()) {
            case CcuRepType::LOC_RECORD_EVENT: {
                auto rep = std::static_pointer_cast<CcuRep::CcuRepLocRecordEvent>(base);
                EmitWait(base, rep->GetEventId(), rep->GetMask(), true); break;
            }
            case CcuRepType::LOC_WAIT_EVENT: {
                auto rep = std::static_pointer_cast<CcuRep::CcuRepLocWaitEvent>(base);
                const uint16_t actual = static_cast<uint16_t>(Resource(ops_.ckeQuery, rep->GetEventId()));
                EmitWait(base, rep->GetEventId(), rep->GetMask(), true);
                for (uint32_t bit = 1; bit != 0 && bit <= UINT16_MAX; bit <<= 1U) {
                    if ((rep->GetMask() & bit) != 0 && (actual & bit) == 0) {
                        for (const auto &dependency : rep->GetDependencyInfo(bit)) {
                            EmitRep(dependency);
                        }
                    }
                }
                break;
            }
            case CcuRepType::LOC_WAIT_NOTIFY: {
                auto rep = std::static_pointer_cast<CcuRep::CcuRepLocWaitNotify>(base);
                EmitWait(base, rep->GetNotifyId(), rep->GetMask(), true); break;
            }
            case CcuRepType::REM_POST_SEM:
                EmitRemoteWait(base, std::static_pointer_cast<CcuRep::CcuRepRemPostSem>(base), false); break;
            case CcuRepType::REM_WAIT_SEM:
                EmitRemoteWait(base, std::static_pointer_cast<CcuRep::CcuRepRemWaitSem>(base), true); break;
            case CcuRepType::REM_POST_VAR: EmitRemotePostVar(base); break;
            case CcuRepType::RECORD_SHARED_NOTIFY: {
                auto rep = std::static_pointer_cast<CcuRep::CcuRepRecordSharedNotify>(base);
                EmitWait(base, rep->GetNotifyId(), rep->GetMask(), false); break;
            }
            case CcuRepType::READ: EmitTransfer(base, std::static_pointer_cast<CcuRep::CcuRepRead>(base)); break;
            case CcuRepType::WRITE: EmitTransfer(base, std::static_pointer_cast<CcuRep::CcuRepWrite>(base)); break;
            case CcuRepType::LOCAL_CPY:
            case CcuRepType::LOCAL_REDUCE: EmitLocalCopy(base); break;
            case CcuRepType::BUF_READ: EmitBufferRead(base); break;
            case CcuRepType::BUF_WRITE: EmitBufferWrite(base); break;
            case CcuRepType::BUF_LOC_READ: EmitBufferLocalRead(base); break;
            case CcuRepType::BUF_LOC_WRITE: EmitBufferLocalWrite(base); break;
            case CcuRepType::BUF_REDUCE: EmitBufferReduce(base); break;
            default: { auto record = Base(base, HCOMM_CCU_DFX_RECORD_DEFAULT); Emit(record); break; }
        }
    }

    const HcommCcuDfxRequestPod &request_;
    HcommCcuDfxEmitFn emit_;
    void *context_;
    const HcommCcuControlOpsPod &ops_;
    int32_t firstError_{0};
};

} // namespace

int32_t HcommCcuDiagnose(const HcommCcuDfxRequestPod *request, HcommCcuDfxEmitFn emit, void *context)
{
    if (request == nullptr || emit == nullptr || request->deviceId < 0 || request->kernelHandle == 0) {
        return static_cast<int32_t>(CcuResult::CCU_E_PARA);
    }
    try {
        CcuKernelMgr &manager = CcuKernelMgr::GetInstance(request->deviceId);
        return DfxBuilder(*request, emit, context, manager.GetControlOps()).Run();
    } catch (...) {
        return static_cast<int32_t>(CcuResult::CCU_E_INTERNAL);
    }
}

} // namespace hcomm
